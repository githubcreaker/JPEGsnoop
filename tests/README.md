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
    - `--about` chooses Help > About JPEGsnoop after the first file and prints
      the item's menu role, the dialog's texts and links, and its icon's size,
      pixel ratio and number of colours, then clicks OK.
    - `--edit STEPS` runs STEPS on the report after each file (and, with
      `--edit-before`, once before the first): a comma-separated list of
      `state`, `selectall` and `copy` (choose Edit > Select All or Copy),
      `key:NAME` (press `selectall`, `copy`, `cut`, `paste`, `undo`,
      `backspace` or `shift-up`, as Cmd+A and so on, in the focused widget),
      `type:TEXT`, `click` (click in the report) and `unfocus`. The keys and
      clicks go through Qt's shortcut handling, as a user's do (QtTest). After
      each step it prints how much of the report is selected (`all`, `part` or
      `none`), which Edit items are enabled, its undo history, where the focus
      is, whether the report changed, and what the clipboard holds (it starts
      with a marker; the offscreen platform's clipboard is the driver's own).
      After a copy it also prints the clipboard's formats, the colours and
      fonts in its HTML, and the report's own colours.

      Edit > Find's steps: `find` and `findnext` (choose Edit > Find or Find
      Next), `key:find` and `key:findnext` (Cmd+F, Cmd+G), and in the Find
      dialog `what:TEXT` (type TEXT, percent-encoded, in place of its text),
      `case`, `word`, `up`, `down` (click those options), `next` (click Find
      Next), `return`, `escape` and `cancel`. `select:TEXT` selects the first
      TEXT in the report, `line:N` and `line:end` put the caret at the start of
      line N or at the end, `cover` moves the dialog over the selection, `top`
      moves the main window to the top of the screen, and `activate` brings it
      to the front. With any of these, each step also prints a `harness: find`
      line: which Find items are enabled, the dialog and its fields, the
      selection's line, length and text and which match of the dialog's text it
      is, where the dialog is (`over` it, `above`, `below` or `beside`), and
      the status bar's message.
    - `--overlay STEPS` prints a `harness: overlay file N` summary of each
      file's report (APP0's version, the overlays its scan section lists, the
      first one's length and offset, and the header's overlay line). After the
      first file (or file N with `--overlay-at N`) it chooses Tools > File
      Overlay and answers the dialog with STEPS: `on`, `off`, `offset=HEX`,
      `value=HEX` (typed, so Len follows it), `len=N`, `load`, `fields`,
      `apply`, `ok` and `cancel`, with `+` to choose the menu item again. It
      prints the dialog's fields when it shows and at `apply`, `ok` and
      `cancel`, and the summary after `apply`, after the dialog closes and
      after File > Reprocess. `--export-menus` also says whether Export JPEG's
      Enable Overlays box is available.
    - `--offset STEPS` prints a `harness: offset file N` summary of each file's
      report and preview: the start offset, whether an SOI was found there and
      the scan decoded, the signature section's offset, the assessment, and the
      preview's size and MD5, with `ref=same` if it is the image a separate
      decode from that offset gives. After the first file (or file N with
      `--offset-at N`; `0` is before any file) it prints File > Offset's place
      in the menu, chooses it and answers the dialog with STEPS: `hex` and `dec`
      (click those), `value=TEXT` (replace the field's text by typing TEXT,
      percent-encoded), `type=TEXT` (type it as the field is), `fields`, `ok`,
      `cancel`, `return` and `escape`, with `+` to choose the menu item again.
      It prints the dialog's fields (base, text, focus and selection) when it
      shows and after `hex` and `dec`, and the summary after each opening and
      after File > Reprocess. (The offscreen platform doesn't activate the
      dialog again after a message box, so focus then shows as none.)
    - `--search STEPS` prints a `harness: search file N` summary of each file,
      as `--offset` does. After the first file (or file N with
      `--search-at N`; `0` is before any file, and with `--offset`, after its
      steps) it prints the places and shortcuts of Tools > Image Search Fwd
      and Rev, then takes STEPS: `fwd` and `rev` (choose those), `ctrl2` and
      `ctrl1` (press their shortcuts, with the main window brought to the
      front first: offscreen, its shortcuts don't reach the image window) and
      `gone` (move the file away for the next step, then put it back). It
      prints the summary after each step and after File > Reprocess.

    A message box, such as the one for a file that can't be opened, is closed
    and its text printed as `harness: message box: ...`;
  - `export-jpeg <file> [--all] [--force-soi] [--force-eoi] [--ignore-eoi]
    [--dht-avi] [--overlays] [--overlay STEPS] [--out PATH] [--out-same]
    [--out-same-case]` opens the file in the real `MainWindow` and runs Export
    JPEG with those settings (`--all` is Extract All, `--overlays` is Enable
    Overlays), to a temporary `out.jpg` unless `--out` gives a path.
    `--overlay` first answers Tools > File Overlay's dialog, as for `gui`, and
    afterwards reprocesses and prints the overlay summary again.
    `--out-same` and `--out-same-case` export over the file itself, by its name
    or by its name in the other case. It prints the size and MD5 of each file
    written, a `harness: exported N md5s=...` line, whether the source is
    unchanged, and the report from the export on;
  - `export-tiff <file> --format rgb8|rgb16|ycc8 [--out PATH] [--out-same]`
    runs Export TIFF the same way, then reads the TIFF back and checks its tags
    and every sample against a separate decode of the file
    (`harness: tiff ... check=ok`);
  - `batch <folder> [<file>...] --batch STEPS` copies the folder to a
    temporary `batch/in`, so a batch can write beside its files. It opens any
    files given (answering `--overlay`'s and `--offset`'s steps for the
    last), then chooses File > Batch Process and answers its dialog with
    STEPS:
    - `in=PATH` and `out=PATH` type a folder: a path below the temporary
      `batch` folder, `/...` or `~...` as given, `raw:` then text typed as
      it is, or `-` for nothing;
    - `subfolders` and `all` tick the boxes, and `fields` prints the fields;
    - `browse-in=PATH` and `browse-out=PATH` click Choose... and choose that
      folder (without `=PATH`, cancel it);
    - `ok` and `cancel`, with `+` to choose the menu item again.

    After each batch it prints each file written or changed below `batch`
    with its size and MD5, a `files:` line listing them, and the report.
    Paths print with `<batch>` and `<home>` for those folders.
    - `--batch-cat REL` prints a file written (any number of times).
    - `--batch-compare` opens each file processed and checks that its log is
      the report shown.
    - `--batch-unreadable REL` makes a file or folder below `batch/in`
      unreadable first, and `--batch-mode OCTAL:REL` gives it those
      permissions (`444:sub`: read, but not searched). Both are put back
      afterwards.
    - `--batch-link sym:NAME=TARGET` and `hard:NAME=TARGET` make a symbolic
      or hard link NAME to TARGET first, both below `batch` (the symbolic link
      holds TARGET's full path, which needn't exist). Afterwards it prints
      `harness: batch link ... now exists= symlink= names=` for NAME (`names`
      is its number of hard links) and `target exists= unchanged=`.
    - `--batch-cancel-after N` and `--batch-close-after N` click Cancel, or
      close the window, when the progress reaches N files.
    - `--batch-open-missing` opens a missing file afterwards.
    - With `--overlay` or `--offset`, that summary is printed again once File
      > Reprocess shows the file after the batch.

    It also prints the menu item's place, the progress dialog and its
    labels, the folders saved in the settings, and with a file open, the
    file that Reprocess shows afterwards;
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
  masks and tagged blocks). Folders for the `batch` mode come from `tree()`.
- `run-tests.sh` runs each case with a time limit. A case fails on a crash,
  timeout, sanitizer report, missing expected output or forbidden output.
  Output of failed cases is kept in `build-tests*/logs/`.

Case ids start with the finding they cover in
[docs/code-review-macos-qt6.md](../docs/code-review-macos-qt6.md)
(`C` memory safety, `H` hangs, `U` GUI, `R` report output, `S` settings and
GUI behaviour, `L` leaks, `M` macOS; `XP` covers the UTF-16 EXIF tags; `F1`–`F10`
are in its "Follow-up review" section, `PSD` in its "PSD decoding" section, `E`
in its "Export JPEG and Export TIFF" section, `D` in its "Detailed Decode"
section, `A` in its "About" section, `T` in its "Select All and Copy"
section, `FN` in its "Find" section, `OV` in its "File Overlay" section,
`BP` in its "Batch Process" section, `FO` in its "File > Offset" section, and `IS`
in its "Tools > Image Search Fwd and Rev" section);
`ok_*` cases are valid files that must keep decoding.

## Adding a case

Add the file and a `case(...)` line to `gen_cases.py`: an id, the finding, the
mode, the file name(s) (`"-"` for the `settings` mode, which takes none),
driver flags, a timeout in seconds, and optionally an extended regex the output
must contain (`expect`) or must not contain (`forbid`).
