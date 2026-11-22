#!/usr/bin/env bash
#
# Build the headless test driver (and the app, which a few cases start) and run
# the JPEGsnoop regression cases.
#
# Usage: tests/run-tests.sh [--asan] [--only REGEX] [--no-build]
#
#   --asan        Build and run with AddressSanitizer and UndefinedBehaviorSanitizer
#                 (build folder build-tests-asan instead of build-tests).
#   --only REGEX  Run only the cases whose id matches REGEX (extended regex); an
#                 invalid one, or one that matches no case, is an error.
#   --no-build    Reuse the existing builds.
#
# Requires Qt 6 (QMAKE to pick one), the Xcode command line tools, and Python 3
# with Pillow for generating the test files (uses `uv` when available).
# Exit status: 0 when no case fails, 1 otherwise (2 for an error before any
# case runs). A case is skipped (not failed) when the driver says it can't run
# on this Mac ("harness: skipped:"), as File > Print Setup's panel can't
# without a printer set up.

set -euo pipefail

die() { printf 'error: %s\n' "$*" >&2; exit 2; }

repo_root=$(cd "$(dirname "$0")/.." && pwd)
asan=0
only=""
do_build=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --asan) asan=1 ;;
    --only)
      [[ $# -ge 2 ]] || die "--only needs a pattern"
      only=$2
      shift
      ;;
    --no-build) do_build=0 ;;
    -h | --help) sed -n '3,19p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "unknown option: $1" ;;
  esac
  shift
done

# (bash's =~ returns 2 for a pattern it can't compile)
if [[ -n $only ]]; then
  only_rc=0
  # shellcheck disable=SC2319  # the test's own status is the one wanted
  [[ "" =~ $only ]] || only_rc=$?
  [[ $only_rc -ne 2 ]] || die "--only: not a valid extended regex: $only"
fi

qmake=${QMAKE:-$(command -v qmake6 || command -v qmake || true)}
[[ -n $qmake ]] || die "qmake not found; install Qt 6 or set QMAKE"

if [[ $asan -eq 1 ]]; then
  build_dir="$repo_root/build-tests-asan"
  # -O0: optimising DbSigs.cpp (a 3,347-entry signature table) with sanitizers
  # takes over 15 minutes; unoptimised it compiles in seconds.
  qmake_args=("CONFIG+=sanitizer" "CONFIG+=sanitize_address" "CONFIG+=sanitize_undefined"
              -after "QMAKE_CXXFLAGS_RELEASE -= -O2" "QMAKE_CXXFLAGS_RELEASE += -O0 -g")
  time_scale=4
else
  build_dir="$repo_root/build-tests"
  qmake_args=()
  time_scale=1
fi
harness="$build_dir/jpegsnoop-harness"
# The app itself, where the driver looks for it (cmdline --app)
app_dir="$build_dir/app"
cases_dir="$build_dir/cases"
logs_dir="$build_dir/logs"

# --- Build ----------------------------------------------------------------------
if [[ $do_build -eq 1 ]]; then
  mkdir -p "$build_dir"
  printf '==> Building the test driver in %s\n' "$build_dir"
  if ! (cd "$build_dir" &&
        "$qmake" "$repo_root/tests/harness/harness.pro" ${qmake_args[@]+"${qmake_args[@]}"} &&
        make -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)") >"$build_dir/make.log" 2>&1; then
    tail -n 30 "$build_dir/make.log" >&2
    die "build failed; full output in $build_dir/make.log"
  fi
  mkdir -p "$app_dir"
  printf '==> Building the app in %s\n' "$app_dir"
  if ! (cd "$app_dir" &&
        "$qmake" "$repo_root/JPEGsnoopQt.pro" ${qmake_args[@]+"${qmake_args[@]}"} &&
        make -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)") >"$app_dir/make.log" 2>&1; then
    tail -n 30 "$app_dir/make.log" >&2
    die "app build failed; full output in $app_dir/make.log"
  fi
fi
[[ -x $harness ]] || die "$harness not found; run without --no-build"

# --- Generate test files ----------------------------------------------------------
printf '==> Generating test files\n'
rm -rf "$cases_dir"
if command -v uv >/dev/null; then
  uv run --quiet --no-project --with pillow python -I "$repo_root/tests/gen_cases.py" "$cases_dir" >/dev/null
else
  python3 -c 'import PIL' 2>/dev/null || die "Python 3 with Pillow is required (or install uv)"
  python3 -I "$repo_root/tests/gen_cases.py" "$cases_dir" >/dev/null
fi

# --- Run ------------------------------------------------------------------------
if [[ -n $only ]]; then
  selected=0
  while IFS=$'\t' read -r id _; do
    if [[ $id =~ $only ]]; then
      selected=$((selected + 1))
    fi
  done <"$cases_dir/cases.tsv"
  [[ $selected -gt 0 ]] || die "--only $only matches no case"
fi

rm -rf "$logs_dir"
mkdir -p "$logs_dir"
export QT_QPA_PLATFORM=offscreen
export ASAN_OPTIONS=detect_leaks=0:symbolize=0:hard_rss_limit_mb=2000
export UBSAN_OPTIONS=print_stacktrace=1:symbolize=0

pass=0
skip=0
fail=0
while IFS=$'\t' read -r id finding mode files flags timeout expect forbid; do
  if [[ -n $only ]] && ! [[ $id =~ $only ]]; then
    continue
  fi

  args=("$mode")
  if [[ $files != "-" ]]; then
    IFS=',' read -r -a names <<<"$files"
    for name in "${names[@]}"; do
      args+=("$cases_dir/files/$name")
    done
  fi
  if [[ $flags != "-" ]]; then
    read -r -a flag_list <<<"$flags"
    args+=("${flag_list[@]}")
  fi

  out="$logs_dir/$id.out"
  err="$logs_dir/$id.err"
  set +e
  # Subshell with its stderr discarded: keeps bash's "Bus error" job notices
  # out of the report (the exit status still carries the signal). The explicit
  # exit stops bash from exec-ing the command in place of the subshell.
  (timeout $((timeout * time_scale)) "$harness" "${args[@]}" >"$out" 2>"$err"; exit $?) 2>/dev/null
  rc=$?
  set -e

  reason=""
  skipped=""
  if [[ $rc -eq 124 ]]; then
    reason="timed out after $((timeout * time_scale)) s"
  elif grep -q -E 'ERROR: AddressSanitizer|runtime error' "$err"; then
    reason="sanitizer: $(grep -m1 -E 'ERROR: AddressSanitizer|runtime error' "$err" | sed -E 's#^.*/source/##' | cut -c1-110)"
  elif [[ $rc -ne 0 ]]; then
    reason="exit status $rc"
  elif grep -q '^harness: skipped: ' "$out"; then
    # (Something this Mac lacks, such as a printer)
    skipped=$(grep -m1 '^harness: skipped: ' "$out" | sed 's/^harness: skipped: //' | cut -c1-110)
  elif [[ $expect != "-" ]] && ! grep -q -E "$expect" "$out"; then
    reason="output lacks /$expect/"
  elif [[ $forbid != "-" ]] && grep -q -E "$forbid" "$out"; then
    reason="output contains /$forbid/"
  fi

  if [[ -n $skipped ]]; then
    skip=$((skip + 1))
    printf 'SKIP  %-4s %-30s %s\n' "$finding" "$id" "$skipped"
    rm -f "$out" "$err"
  elif [[ -z $reason ]]; then
    pass=$((pass + 1))
    printf 'PASS  %-4s %s\n' "$finding" "$id"
    rm -f "$out" "$err"
  else
    fail=$((fail + 1))
    printf 'FAIL  %-4s %-30s %s\n' "$finding" "$id" "$reason"
  fi
done <"$cases_dir/cases.tsv"

printf '\n%d passed, ' "$pass"
[[ $skip -eq 0 ]] || printf '%d skipped, ' "$skip"
printf '%d failed' "$fail"
[[ $fail -eq 0 ]] || printf '; output of failed cases in %s' "$logs_dir"
printf '\n'
[[ $fail -eq 0 ]]
