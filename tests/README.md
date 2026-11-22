# JPEGsnoop regression tests

Regression tests for the Qt version. They run the app's real decoder and main
window headlessly against generated files that used to crash or hang it.

```bash
tests/run-tests.sh            # normal build, in build-tests/
tests/run-tests.sh --asan     # with AddressSanitizer + UBSan, in build-tests-asan/
tests/run-tests.sh --only H   # only cases whose id matches a regex
```

It builds the test driver and, in `app/` in the same folder, the app itself,
which a few cases start. A `--only` pattern that isn't a valid regex, or that
matches no case, is an error (exit status 2).

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
    - `--zoom PCT` picks that View > Zoom level (`800` for 800%; `fit` for
      Zoom to Fit Window, which is the default), and
      `--max-zoom-mb N` reports whether showing it took more than N MB;
    - `--check-preview` compares the preview with the decoded image: its size, where
      mouse positions land in the image, and the pixels drawn, in screen pixels,
      with the red line where the image ends inside its last, partial MCUs
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
    - `--about` chooses Help > About JPEGsnoop after the first file
      (`--about-button` clicks the toolbar's About button instead) and prints
      the item's menu role, the dialog's texts and links, and its icon's size,
      pixel ratio and number of colours, then clicks OK.
    - `--buttons` prints, after the first file, the toolbar's buttons in
      order, each by its tooltip (`-` for a separator), and for each its tip,
      key, menus, whether it is on and whether it has an icon.
    - `--menus` prints, after the first file, each menu of the menu bar with
      the items shown in it (`-` for a separator, a submenu by its name).
    - `--edit STEPS` runs STEPS on the report after each file (and, with
      `--edit-before`, once before the first): a comma-separated list of
      `state`, `selectall` and `copy` (choose Edit > Select All or Copy),
      `button:copy` (click the toolbar's Copy button), `key:NAME` (press
      `selectall`, `copy`, `cut`, `paste`, `undo`, `backspace` or
      `shift-up`, as Cmd+A and so on, in the focused widget),
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
    - `--lookup STEPS` chooses Tools > Lookup MCU Offset after the first file
      (or file N with `--lookup-at N`; `0` is before any file, and with
      `--offset` or `--search`, after their steps), printing the menu item's
      place first, and answers the dialog with STEPS: `x=TEXT` and `y=TEXT`
      (type TEXT over the field, percent-encoded), `recalc`, `return`,
      `escape`, `close` and `fields`, with `+` to choose the menu item again.
      It prints the dialog's fields (the ranges, X and Y, the MCU and file
      offset shown, and the focus) when it shows and after each step.
      `--hover X,Y[/X,Y...]` then moves the mouse over the preview to each
      image pixel (which may be off the image), or off the preview altogether
      for `leave`, and prints the image window's status bar (MCU, file offset
      and YCC).
    - `--dqt CHOICE[+CHOICE...]` chooses Tools > Search Executable for DQT
      after the first file (or file N with `--dqt-at N`; `0` is before any
      file), printing the menu item's place first, and answers its file
      dialog with each CHOICE: `cancel`, or the file to search
      (percent-encoded; a relative path is in the folder of the files given).
      It prints the dialog's title and filters, the report's search section,
      a `harness: dqt found ORDER TABLE ENTRIES @ OFFSET` line for each match,
      and the tables searched for (`standard`, and `file` for the file
      shown's).
    - `--add-db OPENING[+OPENING...]` chooses Tools > Add Camera/SW to DB
      after the first file (or file N with `--add-db-at N`; `0` is before any
      file), printing the menu item's place and the report's signature search
      first, and answers its dialog with each OPENING's steps, separated by
      commas: `quality=TEXT` and `software=TEXT` (type TEXT over the field,
      percent-encoded), `paste-software=TEXT` (paste it there), `source=cam`,
      `source=sw` or `source=unsure`, `ok`, `return`, `cancel`, `escape` and
      `fields`. An OPENING `folder=NAME` instead sets Options >
      Configuration's user database folder to NAME in the settings folder
      (empty for none) and saves. `other=NAME` has another copy of JPEGsnoop,
      which read the user database when this one started, add software NAME
      with the file shown's signatures, and `other-remove=NAME` has it take
      out the entry for software NAME; `lock` holds the database's lock
      until the openings end, as another copy does while it writes; and
      `stale-lock` leaves a lock file an hour old, as a copy that stopped
      while writing would. `file-mode=OCTAL` sets the database file's
      permissions, and `db-file=NAME` replaces it with NAME (in the folder of
      the files given), as another program might. It prints the dialog's
      fields and note when it shows and after each step. An OPENING
      `manage[,STEP...]` chooses Tools > Manage Local DB instead (printing
      its place in the menu the first time) and answers its dialog with the
      steps `select=N[;N...]` (click row N, then Command-click the others),
      `remove`, `remove-all`, `delete` and `backspace` (the keys, in the
      list), `list`, `ok`, `return`, `cancel`, `escape`, and the openings
      from `other=` to `db-file=` above (while the dialog is open); it prints
      the dialog's rows and note when it shows, and its state after each
      step. Then it prints the user
      database file read as Windows wrote it (each entry's fields), the
      folder's files, whether a file was written beside the folder, the
      database as the next start reads it, and the signature search again
      after File > Reprocess.
      `--user-db [FOLDER:]NAME` (any number) copies NAME, in the folder of the
      files given, into the user database folder as `JPEGsnoop_db.dat` before
      the window opens (or into FOLDER in the settings folder), and
      `--user-db-mode OCTAL` sets that copy's permissions. `--db-dir NAME`
      makes NAME in the settings folder (not created) the user database
      folder, instead of the settings folder itself; `--db-dir-mode OCTAL`
      creates it and sets its permissions until the window closes.
    - `--mcu-grid STEP[,STEP...]` chooses View > Overlays > MCU Grid after the
      first file (or file N with `--mcu-grid-at N`; `0` is before any file),
      printing the menu item's place, shortcut and check mark first, and does
      each STEP: `toggle` (choose the menu item), `key` (press its shortcut),
      `cmd-g` (press Cmd+G, Edit > Find Next's key on macOS), `zoom=PCT`,
      `channel=NAME` (View > Image Chan), `scroll=X;Y` (scroll the image
      window there, in points), `size=W;H` (make it that size), `grid`, `end`
      and `buffer`. After each step it prints the check mark and how often
      Find Next ran. `grid`, and every file after it is shown, print where the
      grid is drawn: the screen columns and rows its dots are on, their
      spacing (`gap`) and colours, and how many other pixels differ from the
      decoded image and the red line (`stray`). `end`, and every file, print
      where the red line is drawn where the image ends inside its last,
      partial MCUs, found from the red and white pixels that differ from the
      image (use an image that is neither): the screen column down the right
      and the row along the bottom, the rows and columns each spans, its red
      dots' spacing (`every`) and the first one's place (`from`), how many
      pixels are red and white, how many are elsewhere (`stray`), and how
      many that differ have the grid's colour or another. `buffer` grabs the
      image window as it was painted (a scroll copies what was shown and
      paints only what comes into view) and prints how many of its pixels
      painting it all again changes, how many have the grid's colour, and
      how many the red line's. On a Retina screen the grab has one pixel per
      point.
    - `--bars STEP[,STEP...]` (`--bars-at N`; `0` is before any file) prints
      the places of View > Toolbar and View > Status Bar, and whether the
      toolbar, the main window's status bar and the image window's are shown
      and the items checked. Then it does each STEP: `toolbar` and
      `statusbar` (choose the menu item), `context` (print the toolbar area's
      context menu and choose the toolbar's item) and `hide-toolbar` (hide the
      toolbar itself, as closing it does when it floats). After each step it
      prints the same, and whether the report and the image window's view got
      `taller`, `shorter` or stayed the `same`; after every file, the same as
      `harness: bars file N:`. The step `widgets` prints what each status bar
      holds besides its messages: its widgets, a label with its text.
    - `--open CHOICE[+CHOICE...]` (`--open-at N`; `0` is before any file)
      chooses File > Open Image and answers its file dialog: `cancel`, or the
      file to open (percent-encoded; a relative path is in the folder of the
      files given). It prints the dialog's title, filters, the one selected,
      its mode and whether it is read-only, then the file the report is of
      and whether it was read as an AVI.
    - `--note ANSWER` starts with the low-resolution notice on (otherwise the
      driver keeps it off) and answers each note: `ok` (its checkbox as it
      is), `keep` (untick it, then OK), `escape`, or `finder=NAME` (OK, but
      only after NAME has been sent as Finder does and given time to open).
      It prints each note's text and checkbox and whether the report and
      preview are empty behind it; at the end it closes the window and prints
      how many showed and the setting as saved. `--settings-saved` saves the
      settings as the window opens, as accepting the license does.
    - `--image-window STEP[,STEP...]` prints the image window's title and
      width after each file: whether it is wider than at first, and whether
      an indicator's text is cut. After the first file it does each STEP and
      prints the same: `idct=full`, `idct=none`, `reprocess`, `chan=NAME`,
      `zoom=PCT`, `hover=X:Y` (on that image pixel), `widest` (the widest
      values the indicators can show) and `show`. For the zoom: `zoom=` also
      takes `fit`, `in` and `out`; `menu` prints the menu; `grid`, `new`,
      `size=W:H`, `scroll=X:Y` (or `scroll=mid`) and `wait` (for a resize's
      smoothing) do those; `key=fit|in|out` and `key-image=...` press the
      shortcut in the main or the image window; `view` prints the zoom, the
      one checked, whether it fits, the image pixel in the middle and whether
      a reduced image is smooth; `sweep=MCU` fits it at 24 window sizes and
      checks every screen pixel of those enlarged. `report` prints whether
      the report is at its top or end, its last line and the status bar's
      message; `intro` prints the report's lines about the file;
      `skip-large`, `option` and `saved` choose, print and save Options >
      Skip Hashes on 4 GiB or Larger Files. `--full-idct` starts with Full
      IDCT saved.
    - `--hash-large` starts with that option off (also in `batch`), and
      `--hash-dialog cancel|close|quit|finder=NAME` answers a large file's
      hashing progress. In `batch`, `--batch-sparse REL=SIZE` adds a file
      that takes no room, and `--batch-cancel-hash` cancels while one is
      hashed.
    - `--print STEP[,STEP...]` (`--print-at N`; `0` is before any file) prints
      File > Print Setup's place, makes the shared printer write a PDF in the
      settings folder, and answers Print Setup's and Print's system dialogs
      itself: nothing is printed, and the system's page layout and print
      panels, which the driver can't answer, are never shown. Its answer to
      the page layout panel does nothing for a printer that makes PDF files,
      as Qt's macOS dialog does; Print Setup's own dialog for such a printer
      is answered on screen. Steps: `setup=landscape`, `setup=portrait`,
      `setup=a4`, `setup=letter` or `setup=cancel`; `print`, `print=selection`,
      `print=landscape`, `print=save-pdf` (as Save as PDF in the print panel)
      or `print=cancel` (the PDF's pages and first page size are printed);
      `preview` (Print Preview's printer and pages, then closed);
      `printer=native` (this Mac's default printer, which Print refuses; with
      no printer set up it prints `harness: skipped: ...` and stops) and
      `printer=pdf`; `select=N`, `select-all` and `select-none` in the report.
      A step may end in `#LABEL`, printed with it, to tell the same step done
      twice apart (`print#1,print#2`).
      It prints each dialog's printer and options when it shows, and the
      printer's format, orientation, paper, range and margins after each step.
    - `--recent STEP[,STEP...]` (`--recent-at N`; `0` is before any file)
      prints File > Recent File's place, its items (text, and the file name in
      its tip) and the list as saved; then each STEP and them again:
      `open=NAME` (as File > Open opens it), `open-copy=NAME:PATH` and
      `link=NAME:PATH` (a copy of NAME, or a link to it, at PATH in the
      settings folder, opened), `choose=N`, `gone=PATH` and `back=PATH` (move
      PATH in the settings folder away and back), `reprocess` and `menu`. `name=PATH` prints how an
      item names PATH (`~/` is the home folder; no file is needed). Values are
      percent-encoded. `--recent-stored NAME[+NAME...]` saves a list before
      the window opens, as a previous start would have.
    - `--new STEP[,STEP...]` (`--new-at N`; `0` is before any file) prints
      where the toolbar's New button is (toolbar, position, tips, shortcut,
      menus) and what is shown: the report, the image window, the preview,
      the histograms, which items that need a file are on, the Edit items
      that need text, and File > Recent File's items. Steps, each followed by
      the same: `button` (click it), `key` (press its shortcut in the main
      window), `key-image` (in the image window), `hide-toolbar`,
      `reprocess`, `open=NAME`.
    - `--outside STEP[,STEP...]` (`--outside-at N`; `0` is before any file)
      hands files to the app from outside it and prints, after each step, the
      file the report is of and its lines, File > Recent File's files,
      whether the main window is minimized, and which window is active. NAMEs
      are in the folder of the files given, percent-encoded, several joined by
      `+`. Steps: `drop=NAMES` (dragged over the report and dropped, through
      the same Qt entry points as a drag from Finder; it prints what the drag
      and the drop were answered), `drop-move=NAMES` (a drag that only offers
      to move them), `drop-text=TEXT`, `drop-web=URL`, `drop-image=NAME` (on
      the image window), `drop-histogram=NAME`, `finder=NAMES` (as Finder
      sends them for Open With or a drop on the Dock icon: one event each,
      one after another), `finder-url=URL`, `args=ARGS` (as `main()` passes
      the app's arguments: a NAME as its whole path, one starting with `-` as
      it is, an empty part as an empty argument), `args-here=ARGS` (each as
      it is, the current folder being the files' folder), `open=NAME`,
      `paths` (the whole paths, from the files' folder, of the report's file
      and File > Recent File's files, and the file's size), `minimize`,
      `activate-image` and `wait`. `in-dialog=STEP[;STEP...]` does the steps
      while a dialog is open and prints what is shown then too;
      `in-decode=NAME:STEP` opens NAME, then opens it again and does STEP
      while it is decoded, and prints whether STEP came during the decode and
      whether the report just after is the same as the first one;
      `in-print=STEP` and `in-setup=STEP` choose File > Print or Print Setup
      and do STEP while its panel is up, as Qt runs macOS's panels. No panel
      is shown: Print prints to a PDF in a temporary folder, and Print
      Setup's panel is cancelled. They print what is shown then, whether the
      report is the same when the panel closes, and Print's PDF. Print
      Setup's panel needs a printer set up: without one, `in-setup=STEP`
      prints `harness: skipped: ...`. `in-pdf-setup=STEP` does STEP while
      Print Setup shows its own dialog for a printer making PDFs (as with no
      printer set up), then cancels it.

    A message box, such as the one for a file that can't be opened, is closed
    and its text printed as `harness: message box: ...`;
  - `export-jpeg <file> [--all] [--force-soi] [--force-eoi] [--ignore-eoi]
    [--dht-avi] [--overlays] [--overlay STEPS] [--out PATH] [--out-same]
    [--out-same-case] [--src-link]` opens the file in the real `MainWindow`
    and runs Export JPEG with those settings (`--all` is Extract All,
    `--overlays` is Enable Overlays), to a temporary `out.jpg` unless `--out`
    gives a path. `--overlay` first answers Tools > File Overlay's dialog, as
    for `gui`, and afterwards reprocesses and prints the overlay summary
    again. `--out-same` and `--out-same-case` export over the file itself, by
    its name or by its name in the other case. `--src-link` opens the file
    through a link named `out.000001.jpg`, Extract All's first name, and
    prints whether the link is still there (`harness: source link kept=1`).
    It prints the size and MD5 of each file written, a `harness: exported N
    md5s=...` line, whether the source is unchanged, and the report from the
    export on;
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
  - `cmdline <file>... [OWN...] -- ARGS...` runs the app's command-line mode
    with ARGS, as `main()` does when the app is started with options (`-i`,
    `-b` and the rest), in a temporary folder holding a copy of each file
    given, which is the current folder. OWN can be:
    - `--put DEST=NAME` copies NAME (from the folder of the files given) to
      DEST, and `--mkdir DIR` makes a folder;
    - `--link DEST=TARGET` makes a symbolic link;
    - `--chmod OCTAL:REL` sets permissions for the run (put back afterwards);
    - `--sparse NAME=SIZE` makes a file of SIZE bytes that takes no room on the
      disk (a file over 64 MiB isn't read: its MD5 shows as `big` and its size);
    - `--setting KEY=VALUE` saves a setting first (such as
      `General\DumpScan=false`), and `--no-eula` leaves the license terms not
      accepted;
    - `--app` starts the app itself instead (`app/JPEGsnoopQt.app` beside the
      driver), as one does in Terminal: its `main()` as built, not offscreen.
      It reads your own settings, so only ARGS that read none run: the
      summary or an error in them. The status is `crash` if it stopped on a
      signal; the settings aren't listed.

    It prints the exit status with every file in the folder, each line
    written to the standard output and error (`harness: cmdline out:` and
    `err:`), each file's size, MD5, number of DHT markers and whether the run
    made or changed it, each line of each `.txt` log that is text
    (`harness: cmdline log NAME: LINE`), and which saved settings changed. If
    ARGS would open the window instead, it prints only
    `harness: cmdline window input=FILE`. As `main()` does, it gives Qt none of
    ARGS;
  - `settings-name` checks the settings' name against the bundle ID of the
    app built beside the driver, and that app's own domain (both only read);
  - `file-facts WHAT` hashes files it makes as the report's intro does, as
    they change before or while they are read, or are of 4 GiB (sparse, so
    they take no room; see the usage for WHAT), and prints how often a file
    was read;
  - `settings` exercises settings load/store, option toggles and the
    Configuration dialog, and prints that dialog's groups, their heights,
    the room below them and its checkboxes, the license terms' groups,
    checkboxes and room below them, and which settings no longer kept are
    stored;
  - `--scale F`, in any mode, runs as if on a screen with F device pixels per
    point (2 for Retina).

  It always runs offscreen with throwaway settings, so it never shows a window
  or touches your JPEGsnoop settings.
- `gen_cases.py` writes the test files and `cases.tsv`. Every file is built from
  synthetic images (Pillow gradients, solid colours and seeded noise) plus
  hand-made segments, so no third-party images are needed. Files that Pillow
  can't write, such as unusual sampling factors or levels outside 0..255, come
  from `dc_only_jpeg()`, a small encoder for images made of flat 8×8 blocks.
  It can add restart markers (with fill bytes before them), and it returns
  where it wrote each MCU, which the Lookup MCU Offset cases check the offsets
  against.
  Photoshop files come from `psd()`, which writes raw or PackBits image data,
  and layers from `psd_layers()` (one) or `psd_layer_section()` (any, with
  masks and tagged blocks). Folders for the `batch` mode come from `tree()`.
- `run-tests.sh` runs each case with a time limit. A case fails on a crash,
  timeout, sanitizer report, missing expected output or forbidden output.
  Output of failed cases is kept in `build-tests*/logs/`. A case the driver
  can't run on this Mac, printing `harness: skipped: REASON` (the Print Setup
  panel's, without a printer set up), is listed as `SKIP` and counted apart;
  it doesn't fail the run.

Case ids start with the finding they cover in
[docs/code-review-macos-qt6.md](../docs/code-review-macos-qt6.md)
(`C` memory safety, `H` hangs, `U` GUI, `R` report output, `S` settings and
GUI behaviour, `L` leaks, `M` macOS; `XP` covers the UTF-16 EXIF tags; `F1`–`F10`
are in its "Follow-up review" section, `PSD` in its "PSD decoding" section, `E`
in its "Export JPEG and Export TIFF" section, `D` in its "Detailed Decode"
section, `A` in its "About" section, `T` in its "Select All and Copy"
section, `FN` in its "Find" section, `OV` in its "File Overlay" section,
`BP` in its "Batch Process" section, `FO` in its "File > Offset" section, `IS`
in its "Tools > Image Search Fwd and Rev" section, `LM` in its "Tools > Lookup MCU
Offset" section, `DQ` in its "Tools > Search Executable for DQT" section,
`AD` in its "Tools > Add Camera/SW to DB" section, `MD` in its "Tools >
Manage Local DB" section, `MG` in its "View > Overlays > MCU Grid" section, and
`VB` in its "View > Toolbar and Status Bar" section, `PS` in its "File > Print
Setup" section, `RF` in its "File > Recent File" section, `NB` in its "The
toolbar's New button" section, `IE` in its "The red line where the image
ends inside its MCUs" section, `OF` in its "Opening a file from outside the
app: a drop, Finder, the command line" section, `TB` in its "The toolbar's
Copy and About buttons" section, `RM` in its "Removed: Check for Updates,
Edit > Undo, View > Adjust, Submit signature" section, `CL` in its
"Command-line mode" section, `OD` in its "File > Open's AVI Movie, and the
status bar's "1234"" section, `PI` in its "The image window's indicators
with no pixel under the mouse" section, `RN`, `IT` and `IW` in its "The
low-resolution notice, and the image window's title and width" section, and
`SM` in its "One settings file, named as the bundle ID" section, `ZM` in
its "The zoom menu: Zoom to Fit Window, and up to 6400%" section, `NS` in
its "Why an image isn't shown" section, `FF` in its "The file's dates and
hashes" section, and `SF` in its "Options > Skip Hashes on 4 GiB or Larger
Files" section);
`ok_*` cases are valid files that must keep decoding.

## Adding a case

Add the file and a `case(...)` line to `gen_cases.py`: an id, the finding, the
mode, the file name(s) (`"-"` for the `settings` mode, which takes none),
driver flags, a timeout in seconds, and optionally an extended regex the output
must contain (`expect`) or must not contain (`forbid`).
