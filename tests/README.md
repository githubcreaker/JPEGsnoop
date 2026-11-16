# JPEGsnoop regression tests

Regression tests for the Qt version. They run the app's real decoder and main
window headlessly against generated files that used to crash or hang it.

```bash
tests/run-tests.sh            # normal build, in build-tests/
tests/run-tests.sh --asan     # with AddressSanitizer + UBSan, in build-tests-asan/
tests/run-tests.sh --only H   # only cases whose id matches a regex
```

Requirements: Qt 6 (`brew install qtbase`; set `QMAKE` to pick another Qt),
the Xcode command line tools, and Python 3 with Pillow (the script uses `uv`
when it is installed, so Pillow needn't be installed separately).

## How it works

- `harness/` builds `jpegsnoop-harness` from the app's sources (shared through
  `JPEGsnoopQt.pri`) plus a small driver:
  - `decode <file> [--ac] [--histo] [--unknown-exif] [--pixels X,Y/X,Y...]
    [--start N] [--detail X,Y,LEN] [--preview-md5]` decodes one file in
    non-interactive mode and prints the report
    (`--unknown-exif` also reports unknown EXIF tags, which the app hides by
    default; `--pixels` prints the decoded colour at each point; with
    `--histo` it also counts the pixels drawn in each histogram image;
    `--start` decodes from file offset N; `--preview-md5` prints an MD5 of the
    decoded image). `--detail` turns on Options > Scan Segment > Detailed
    Decode for LEN MCUs from MCU X,Y, prints a `harness: detail blocks=...`
    summary of what the report shows (ending with the DC of the first 8
    blocks' matrices), and decodes the file again without it:
    `harness: detail changes report=0 image=0` means it only added lines;
  - `gui <file>... [--mouse] [--log] [--reprocess-first] [--histo]
    [--cycle-channels] [--repeat N] [--max-growth-mb N]` opens the files in the
    real `MainWindow` like a fresh install. `--mouse` moves and clicks over the
    preview window, `--log` prints the report after each file,
    `--reprocess-first` runs Reprocess before opening anything, `--histo` turns
    the histograms on, `--cycle-channels` shows every View > Image Chan channel
    after each file, `--repeat` opens the files N times, and `--max-growth-mb`
    reports whether memory grew by more than N MB after the first round (a leak
    check; skipped under ASan). After each file in the first round:
    - `--zoom PCT` picks that View > Zoom level (`800` for 800%), and
      `--max-zoom-mb N` reports whether showing it took more than N MB;
    - `--check-preview` compares the preview with the decoded image: its size, where
      mouse positions land in the image, and the pixels drawn, in screen pixels
      (with `--cycle-channels`, again after the channels, as "preview after
      channels");
    - `--check-histo` checks that the histograms are drawn at screen resolution;
    - `--check-theme` reports the lowest contrast between report text and its
      background, then switches between light and dark palettes and reports it again
      (`--dark` starts with a dark palette, as macOS Dark Mode does).

    - `--export-menus` chooses Tools > Export JPEG and Tools > Export TIFF,
      answering each dialog with OK and printing its title (`--export-menus-cancel`
      cancels the settings and format dialogs instead). `--export-menus-all` ticks
      Extract All and prints the numbered files written; `--old-numbered` leaves
      files named as the first of them and as the name chosen beforehand, and
      `--replace` answers Replace when asked (otherwise questions are cancelled).
    - `--detail-dlg STEPS` chooses Options > Scan Segment > Detailed Decode after
      the first file (or file N with `--detail-dlg-at N`) and answers its dialog
      with STEPS, a comma-separated list of `load` (Load Coords), `x=N`, `y=N`,
      `len=N`, `on`, `off`, `ok` and `cancel`; `+` chooses the menu item again
      for the steps after it. It prints the dialog's fields each time it shows,
      the setting chosen, and a summary of the report's detailed decode as it is,
      after File > Reprocess and (with `--cycle-channels`) after the channels.
      `--mark X,Y/X,Y...` first clicks the image at those pixels after each file;
      `--auto-reprocess` turns on Automatic Reprocessing and `--no-decode-image`
      turns off Decode Image.

    A message box, such as the one for a file that can't be opened, is closed
    and its text printed as `harness: message box: ...`;
  - `export-jpeg <file> [--all] [--force-soi] [--force-eoi] [--ignore-eoi]
    [--dht-avi] [--out PATH] [--out-same] [--out-same-case]` opens the file in
    the real `MainWindow` and runs Export JPEG with those settings (`--all` is
    Extract All), to a temporary `out.jpg` unless `--out` gives a path.
    `--out-same` and `--out-same-case` export over the file itself, by its name
    or by its name in the other case. It prints the size and MD5 of each file
    written, a `harness: exported N md5s=...` line, whether the source is
    unchanged, and the report from the export on;
  - `export-tiff <file> --format rgb8|rgb16|ycc8 [--out PATH] [--out-same]`
    runs Export TIFF the same way, then reads the TIFF back and checks its tags
    and every sample against a separate decode of the file
    (`harness: tiff ... check=ok`);
  - `settings` exercises settings load/store, option toggles and the
    Configuration dialog;
  - `--scale F`, in any mode, runs as if on a screen with F device pixels per
    point (2 for Retina).

  It always runs offscreen with throwaway settings, so it never shows a window
  or touches your JPEGsnoop settings.
- `gen_cases.py` writes the test files and `cases.tsv`. Every file is built from
  synthetic images (Pillow gradients, solid colours and seeded noise) plus
  hand-made segments, so no third-party images are needed. Files that Pillow
  can't write, such as unusual sampling factors or levels outside 0..255, come
  from `dc_only_jpeg()`, a small encoder for images made of flat 8×8 blocks.
  Photoshop files come from `psd()`, which writes raw or PackBits image data,
  and layers from `psd_layers()` (one) or `psd_layer_section()` (any, with
  masks and tagged blocks).
- `run-tests.sh` runs each case with a time limit. A case fails on a crash,
  timeout, sanitizer report, missing expected output or forbidden output.
  Output of failed cases is kept in `build-tests*/logs/`.

Case ids start with the finding they cover in
[docs/code-review-macos-qt6.md](../docs/code-review-macos-qt6.md)
(`C` memory safety, `H` hangs, `U` GUI, `R` report output, `S` settings and
GUI behaviour, `L` leaks, `M` macOS; `XP` covers the UTF-16 EXIF tags; `F1`–`F10`
are in its "Follow-up review" section, `PSD` in its "PSD decoding" section, `E`
in its "Export JPEG and Export TIFF" section, and `D` in its "Detailed Decode"
section);
`ok_*` cases are valid files that must keep decoding.

## Adding a case

Add the file and a `case(...)` line to `gen_cases.py`: an id, the finding, the
mode, the file name(s) (`"-"` for the `settings` mode, which takes none),
driver flags, a timeout in seconds, and optionally an extended regex the output
must contain (`expect`) or must not contain (`forbid`).
