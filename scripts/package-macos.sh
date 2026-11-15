#!/usr/bin/env bash
#
# Build JPEGsnoopQt and package it as a self-contained macOS app in a zip.
#
# Usage: scripts/package-macos.sh [--no-build] [--build-dir DIR]
#
#   --no-build        Package the existing build instead of running qmake/make.
#   --build-dir DIR   Build folder (default: build-macos in the repo root).
#
# Requires Qt 6 (for example `brew install qtbase`) and the Xcode command line
# tools. Set QMAKE to pick a specific Qt; macdeployqt is taken from the same Qt.
#
# Output: <build-dir>/JPEGsnoopQt-<version>-macos-<arch>.zip
#
# What it does, and why (steps 2-5 change the app in the build folder, which
# becomes the packaged app):
# 1. qmake + make (output in <build-dir>/make.log).
# 2. macdeployqt copies the Qt frameworks, plugins and their libraries into the
#    bundle. Qt 6 finds Contents/PlugIns by itself, so no qt.conf is needed.
# 3. Removes absolute LC_RPATHs (such as /opt/homebrew/lib), so no library can
#    be picked up from outside the bundle.
# 4. Raises LSMinimumSystemVersion to the highest minimum macOS of any bundled
#    binary. Homebrew builds its libraries for the macOS it runs on, so the
#    bundle can need a newer macOS than the app itself was compiled for.
# 5. Ad-hoc signs the bundle, checks the signature (strict) and that every
#    library dependency resolves inside the bundle or to macOS, then zips it
#    without extended attributes and checks the extracted copy again.
#
# Build outside iCloud-synced folders (Desktop/Documents): the iCloud file
# provider tags bundle folders with com.apple.FinderInfo, which codesign rejects.
#
# The app is ad-hoc signed, not notarized: on other Macs Gatekeeper blocks it
# until the user allows it in System Settings > Privacy & Security.

set -euo pipefail

APP_NAME=JPEGsnoopQt

log() { printf '==> %s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

usage() {
  cat <<EOF
Usage: $0 [--no-build] [--build-dir DIR]

  --no-build        Package the existing build instead of running qmake/make.
  --build-dir DIR   Build folder (default: build-macos in the repo root).

Set QMAKE to use a specific Qt 6 qmake.
EOF
}

repo_root=$(cd "$(dirname "$0")/.." && pwd)
build_dir="$repo_root/build-macos"
do_build=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-build) do_build=0 ;;
    --build-dir)
      [[ $# -ge 2 ]] || die "--build-dir needs a folder"
      build_dir=$2
      shift
      ;;
    -h | --help) usage; exit 0 ;;
    *) usage >&2; die "unknown option: $1" ;;
  esac
  shift
done

[[ $(uname -s) == Darwin ]] || die "this script only runs on macOS"

# --- Tools --------------------------------------------------------------------

qmake=${QMAKE:-$(command -v qmake6 || command -v qmake || true)}
[[ -n $qmake && -x $qmake ]] || die "qmake not found; install Qt 6 (brew install qtbase) or set QMAKE"
qt_version=$("$qmake" -query QT_VERSION)
[[ $qt_version == 6.* ]] || die "Qt 6 required, but $qmake is Qt $qt_version"

macdeployqt="$("$qmake" -query QT_INSTALL_BINS)/macdeployqt"
[[ -x $macdeployqt ]] || macdeployqt=$(command -v macdeployqt || true)
[[ -n $macdeployqt && -x $macdeployqt ]] || die "macdeployqt not found for $qmake"

for tool in codesign ditto file install_name_tool lipo make otool plutil; do
  command -v "$tool" >/dev/null || die "missing tool: $tool (install the Xcode command line tools)"
done

# --- Build --------------------------------------------------------------------

mkdir -p "$build_dir"
build_dir=$(cd "$build_dir" && pwd)
app="$build_dir/$APP_NAME.app"

if [[ $do_build -eq 1 ]]; then
  log "Building with Qt $qt_version in $build_dir"
  # qmake's Makefile only writes Info.plist when it is missing, so a bundle ID
  # or version changed in JPEGsnoopQt.pro would not reach an existing build
  rm -f "$app/Contents/Info.plist"
  if ! (cd "$build_dir" && "$qmake" "$repo_root/JPEGsnoopQt.pro" &&
        make -j"$(sysctl -n hw.ncpu)") >"$build_dir/make.log" 2>&1; then
    tail -n 30 "$build_dir/make.log" >&2
    die "build failed; full output in $build_dir/make.log"
  fi
fi
[[ -d $app ]] || die "$app not found; build first or drop --no-build"

version=$(sed -n 's/^#define VERSION_STR "\(.*\)".*/\1/p' "$repo_root/source/snoop.h")
[[ -n $version ]] || die "could not read VERSION_STR from source/snoop.h"
arch=$(lipo -archs "$app/Contents/MacOS/$APP_NAME" | tr ' ' '+')
zip="$build_dir/$APP_NAME-$version-macos-$arch.zip"

# --- Helpers ------------------------------------------------------------------

# Print the Mach-O files under $1, NUL-separated.
macho_files() {
  find "$1" -type f -print0 | while IFS= read -r -d '' f; do
    if file -b "$f" | grep -q '^Mach-O'; then
      printf '%s\0' "$f"
    fi
  done
}

# Print the minimum macOS version a Mach-O file declares.
min_macos() {
  otool -l "$1" | awk '
    $2 == "LC_BUILD_VERSION"      { b = 1 }
    b && $1 == "minos"            { print $2; exit }
    $2 == "LC_VERSION_MIN_MACOSX" { v = 1 }
    v && $1 == "version"          { print $2; exit }'
}

# Print the LC_RPATH entries of a Mach-O file, one per line.
rpaths() {
  otool -l "$1" | awk '
    $1 == "cmd" && $2 == "LC_RPATH" { r = 1; next }
    r && $1 == "path" { sub(/^ *path /, ""); sub(/ \(offset [0-9]+\)$/, ""); print; r = 0 }'
}

# --- Package the app in the build folder --------------------------------------

log "Bundling Qt with $macdeployqt"
deploy_out=$("$macdeployqt" "$app" -always-overwrite -no-codesign -verbose=1 2>&1) || {
  printf '%s\n' "$deploy_out" >&2
  die "macdeployqt failed"
}
if printf '%s\n' "$deploy_out" | grep -q '^ERROR'; then
  printf '%s\n' "$deploy_out" >&2
  die "macdeployqt reported errors"
fi

log "Removing absolute library search paths"
while IFS= read -r -d '' f; do
  while IFS= read -r rp; do
    case "$rp" in
      /*)
        if ! err=$(install_name_tool -delete_rpath "$rp" "$f" 2>&1); then
          die "install_name_tool failed on $f: $err"
        fi
        printf '    %s: removed %s\n' "${f#"$app"/}" "$rp" >&2
        ;;
    esac
  done < <(rpaths "$f")
done < <(macho_files "$app")

plist="$app/Contents/Info.plist"
bundle_min=$(
  while IFS= read -r -d '' f; do min_macos "$f"; done < <(macho_files "$app") |
    sort -V | tail -n 1
)
declared_min=$(plutil -extract LSMinimumSystemVersion raw "$plist" 2>/dev/null || echo 0)
if [[ -n $bundle_min && $(printf '%s\n%s\n' "$declared_min" "$bundle_min" | sort -V | tail -n 1) != "$declared_min" ]]; then
  log "Raising LSMinimumSystemVersion from $declared_min to $bundle_min (required by bundled libraries)"
  plutil -replace LSMinimumSystemVersion -string "$bundle_min" "$plist"
fi
min_os=$(plutil -extract LSMinimumSystemVersion raw "$plist")

log "Signing (ad-hoc)"
# codesign lists every file it replaces a signature on: show that only if it fails
if ! sign_out=$(codesign --force --deep --sign - "$app" 2>&1); then
  printf '%s\n' "$sign_out" >&2
  die "codesign failed"
fi
codesign --verify --deep --strict "$app" || die "signature check failed"

log "Checking that every library resolves inside the bundle or to macOS"
# @rpath is checked against Contents/Frameworks: after step 3 the only
# rpaths left point there (or to folders that don't exist).
problems=0
while IFS= read -r -d '' f; do
  own_id=$(otool -D "$f" | sed -n 2p)
  dir=$(dirname "$f")
  while IFS= read -r dep; do
    [[ $dep == "$own_id" ]] && continue
    case "$dep" in
      /System/* | /usr/lib/*) continue ;;
      @rpath/*) target="$app/Contents/Frameworks/${dep#@rpath/}" ;;
      @executable_path/*) target="$app/Contents/MacOS/${dep#@executable_path/}" ;;
      @loader_path/*) target="$dir/${dep#@loader_path/}" ;;
      *) target="" ;;
    esac
    if [[ -z $target || ! -e $target ]]; then
      printf '    %s -> %s\n' "${f#"$app"/}" "$dep" >&2
      problems=$((problems + 1))
    fi
  done < <(otool -L "$f" | awk 'NR > 1 && $0 !~ /:$/ { print $1 }')
done < <(macho_files "$app")
[[ $problems -eq 0 ]] || die "$problems library reference(s) above point outside the bundle or are missing"

log "Creating $zip"
rm -f "$zip"
(cd "$build_dir" && ditto -c -k --keepParent --norsrc --noextattr --noqtn "$APP_NAME.app" "$zip")

log "Checking the zipped app"
check_dir=$(mktemp -d "${TMPDIR:-/tmp}/jpegsnoop-zipcheck.XXXXXX")
cleanup() {
  if [[ -n ${check_dir:-} && -d $check_dir ]]; then
    rm -rf "$check_dir"
  fi
}
trap cleanup EXIT
ditto -x -k "$zip" "$check_dir"
codesign --verify --deep --strict "$check_dir/$APP_NAME.app" || die "zipped app fails the signature check"

zip_size=$(du -h "$zip" | cut -f1 | tr -d ' ')
app_size=$(du -sh "$app" | cut -f1 | tr -d ' ')
cat <<EOF

Packaged $APP_NAME $version ($arch, Qt $qt_version)
  $zip
  zip $zip_size, app $app_size, needs macOS $min_os or later
  ad-hoc signed, not notarized: other Macs will ask before opening it
EOF
