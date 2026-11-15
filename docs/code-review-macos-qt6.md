# JPEGsnoop Qt port: code review

Branch `macos-qt6` (upstream `JPEGsnoopQt` plus the Qt 6/macOS build fixes), reviewed
2026-11-14 on macOS 27.2 (arm64) with Homebrew Qt 6.12.

## Summary

- **No unit tests exist** on either branch: no test code, no test framework, no sample
  images, no CI. The only runtime checks are 111 `Q_ASSERT`s, which are compiled out of
  release builds (`QT_NO_DEBUG`), so the shipped app has none.
- **The decoder is not safe on untrusted files.** A crafted JPEG can overflow a heap buffer
  (C1), crash the app with a one-byte change (C2), or make it hang (H1–H8, most of which
  loop until memory runs out). Most of these are inherited from the Windows 1.8.0 code,
  so the released Windows build is affected too.
- **The Qt port introduced many wrong results.** Most come from translating
  `CString::Format` to `QString::arg` by hand: GPS values print as `%.3f`, JPEG comments
  and Photoshop strings print as decimal character codes, IPTC values are dropped, and
  several checks behind the "image was edited" assessment are broken (R1–R10).
- **Normal GUI use can crash or show stale data.** Opening a second file doesn't decode
  its image (U1), and moving the mouse over the preview can dereference null or read out
  of bounds (U2, U3).
- **A fresh install starts with image decoding switched off** (S1).

Origin labels: **Port** means introduced by the Qt port (master 1.8.0 was right);
**Inherited** means master 1.8.0 has the same bug. Line numbers refer to `macos-qt6`
as reviewed, before the fixes below.

## Fix status

### Crashes and hangs (commit `3fdd556`)

**Fixed**, each covered by a case in `tests/run-tests.sh` (see [tests/README.md](../tests/README.md)):
- **Crashes:** C1–C6, and from C7 the DHT statistics divide-by-zero.
- **Hangs:** H1–H8.
- **GUI:** U1–U3.
- **Also:** R7 (portrait subsampling), fixed with C5 on the same lines.

**Checks:**
- **Tests:** all 32 cases at the time passed with and without ASan/UBSan.
- **Reviewers' files:** all 78 crafted files from the review run clean under the sanitizers.
- **3,273-file corpus:** no AddressSanitizer errors (180 before) and no hangs. The only
  reports left are the open C7 items.
- **Slow, not hung:** five files with a 65535-pixel side exceeded the 60 s limit of the
  unoptimised sanitizer build; a normal build decodes them in 2–13 s.
- **Valid files unaffected:** decode reports before and after these fixes are identical
  for 21 valid JPEGs.

### Wrong-output port regressions

**Fixed:** R1–R6, R8–R12, R16, R19 and the UTF-16 EXIF tags (`XPTitle`, `XPComment`,
`XPAuthor`, `XPKeywords`) from section 8. Found and fixed along the way:
- The signed-short pair at `JfifDecode.cpp:3274` printed its first value twice (inherited).
- ICC four-character codes lost master's `0x` prefix, and `Uint2Chars` let a `%` in the
  file bytes be substituted.
- Printable-character checks used `isprint()`. That accepts bytes above 0x7F in the UTF-8
  locale Qt sets at startup, while master's Windows build showed them as `.`; a
  locale-independent ASCII check is now used everywhere.
- Same printf-style formats in code the UI doesn't reach yet: WindowBuf search status and
  overlay report, the user-DB save error, export progress, and a DICOM-only message.

21 new cases check the corrected output (53 in all).

**Checks:**
- **Tests catch the bugs:** run against the previous commit, 20 of the 21 new cases
  fail. The exception is R7, already fixed there.
- **All 53 pass** with and without ASan/UBSan.
- **No runtime format warnings:** Qt's "QString::arg: Argument missing" no longer appears
  for any of 126 test files (21 warning lines before).
- **Expected changes only:** decode reports of 21 valid JPEGs change only where intended:
  GPS values, ICC codes and non-ASCII bytes in hex-dump text columns.

### Settings and GUI behaviour

**Fixed:** S1–S5.
- **S1:** settings missing from the store keep the constructor defaults, so a fresh install
  decodes images and shows the coach tips again.
- **S2:** the Maker Notes option toggles and has the right checkmark.
- **S3:** the max-errors setting is loaded from where it is stored.
- **S4:** the Configuration dialog:
  - Save applies and stores the settings and closes the dialog;
  - Cancel closes it;
  - Browse picks a folder;
  - Reset re-enables the coach tips;
  - the number fields accept whole numbers only;
  - it opens modally, so one dialog at a time and none leaked;
  - the title typo is fixed.
- **S5:**
  - Save Log suggests `<folder>/<name>.txt`.
  - An empty file reports "File length is zero".
  - Reprocess with no file open does nothing.
  - Status messages reach the status bar, repainting without handling user input.
  - Errors that were set up but never shown now appear.
  - The update-check date uses the month.
  - The log is enabled by default.
- **Also:** the batch-folder defaults are the home folder instead of `C:\`.

9 new cases cover these (62 in all).

**Checks:**
- **Tests catch the bugs:** run against the previous commit, all 9 new cases fail, and so
  does the second-file GUI case, because the old fresh-install default left decoding off.
- **All 62 pass** with and without ASan/UBSan.
- **Decode output unchanged:** reports are identical for 21 valid JPEGs.
- **No slowdown:** decode time with the status updates is unchanged (within noise).

### Leaks

**Fixed:** everything in section 6, plus these:
- `~MainWindow` was empty, so `CDocLog`, `CDbSigs` and `CwindowBuf` were never freed.
- `m_pFile` was uninitialised.
- `DatabaseExtraLoad` leaked its buffer on one early return.

In detail:
- **Preview and histograms:** they are freed when replaced and in the destructor, and
  initialised to null.
- **One `QFile` per open:** it is freed at the next open, on a failed open and at exit.
- **Dialogs:** the Reprocess tip, the auto-reprocess tip and the first-run notice are stack
  objects. (The Configuration dialog was fixed with the settings work; the print dialog was
  already freed.)
- **Signature hashing:** it uses a `QByteArray` instead of a 2000-byte buffer. The buffer
  was leaked per call for the image signature and was bounded only by a compiled-out
  `Q_ASSERT`.
- **Smaller fixes:** `delete[]` for the file buffer, and the export output file is freed
  when it can't be opened.
- **Left alone:** the PSD image allocation at `DecodePs.cpp:385` belongs to the disabled,
  unfinished PSD decoder (see section 7).

**Checks:**
- **Measured growth:** after the first open, memory grew 75.9 MB over 20 re-opens of a
  1024×768 file, and 144.3 MB over 5 re-opens with histograms on and every channel
  shown. It now levels off at about 15 MB, the allocator keeping freed pages: the same
  after 20 and 60 re-opens.
- **Tests:** two new cases check this with a 30 MB limit (66 in all). Both fail against
  the previous commit.
- **All 66 pass** with and without ASan/UBSan, including GUI runs that destroy the main
  window.
- **Decode output unchanged:** reports, signatures included, are identical for 32 files.

### macOS items and U4

**Fixed:** M1–M4 and U4 (section 5 and section 4); M5 except the signing.
- **M1 Dark Mode:** ordinary report text no longer sets a colour, so it follows the
  system's text colour. Headers, warnings, errors and "good" lines each have a light and
  a dark shade, chosen from the report's background. Switching macOS between Light and
  Dark while a report is open recolours it. Every shade has at least 4.5:1 contrast
  (WCAG AA) on its background. Before: ordinary text had 1.25:1 in Dark Mode, and error
  lines had 3.99:1 even in Light Mode.
- **M2 Zoom buttons:** they ask the theme for `zoom-in` and `zoom-out`, which macOS
  shows as magnifier symbols (checked with the Cocoa platform); other systems fall back
  to the bundled images.
- **M3 Preferences:** "Configuration..." is marked as the Preferences item, so on macOS
  it is always Preferences... in the app menu, with ⌘, (other systems keep it under
  Options). The auto-reprocess tip names Preferences (Cmd+,) on macOS.
- **M4 Retina:** the preview draws the image itself instead of stretching a label:
  - 100% is one image pixel per screen pixel, on Retina too.
  - Below 100% it shows one smoothly reduced copy at screen resolution. At 12.5% the
    old label was stretched over the whole window, so the preview looked blocky.
  - Above 100% it enlarges only the part on screen, as sharp pixel blocks.
  - Mouse positions are converted to image pixels for the status bar and the marker.
  - Moving to a screen with a different scale redraws it.
  - The histograms are drawn at screen resolution.
- **U4:** showing 800% took 193 MB for a 1024×768 image, about 0.7 MB now, because no
  enlarged copy of the whole image is made.
- **M5 Packaging:** bundle ID `com.impulseadventure.JPEGsnoopQt` (the name macOS gives
  the app's settings), version 1.8.0 from `snoop.h`, and an app icon made from
  `images/jpegsnoop.ico` by `scripts/make-macos-icon.py`. The 48-pixel art is enlarged
  in whole-pixel steps, so the large sizes are sharp but look like pixel art.
  `NSHighResolutionCapable` is set. The packaging script now rewrites `Info.plist` on
  each build; qmake only wrote it when it was missing.
- **Also:** the unused `labelClick` widget is removed.
- **Still ad-hoc signed:** Developer ID signing and notarization need an Apple Developer
  certificate, and there is none in the keychain here.

10 new cases cover these (74 in all). `--scale` in the test driver simulates a Retina
or 1.5× screen.

**Checks:**
- **Tests catch the bugs:** run against the previous commit, all 10 new cases fail. The
  preview cases can't use the old preview widget, so there they fail by construction.
- **All 74 pass** with and without ASan/UBSan.
- **Preview checked pixel by pixel** at every zoom from 12.5% to 800%, on 1×, 1.5× and
  2× screens.
- **Decode output unchanged:** reports are identical for 24 files.
- **Packaged app:** the zipped app has the bundle ID, version and icon, and its strict
  signature check passes.
- **Not checked in the running app:** the menu, icons and Dark Mode switch were checked
  with the test driver and Qt's macOS platform, but not by using the app on screen.

### Rest of C7

**Fixed:** the three C7 items left after the crash fixes, plus two errors in the average
luminance found on the way. All are inherited from master.
- **IDCT range:** a pixel value beyond `int16_t`, from corrupt coefficients, now
  saturates at -32768 or 32767. Before, the conversion was undefined, and on arm64 it
  wrapped: a crafted 16×16 edge reported pre-ranged Y from -25928 to 25416. Valid files
  never get near the limits.
- **Histogram sums:** they are 64-bit. In 32 bits they overflowed on valid images: a
  plain white 1600×1400 image reported a pre-ranged Y average of -901.4 instead of
  1016.0.
- **Average Pixel Luminance:** this line is in every report, with histograms on or off.
  - Its sum was 32-bit, so it wrapped above 16.8 million bright pixels: a white
    4200×4100 image reported 5 instead of 255. A 24-megapixel photo is affected when its
    average is above about 179.
  - It divided by (width + 1) × (height + 1) instead of width × height, so it was always
    low: 239 for a white 32×32 image. Small images are affected most.
- **GPS degrees and minutes:** the decoder reads each 32-bit part of a fraction as signed,
  so a corrupt value can be negative, and it was converted to an unsigned integer
  (undefined). It is now printed signed: `-1 deg 30' 0.000"` where the arm64 build printed
  `0 deg`.

**Found, fixed later** (inherited, report output; see R20–R22 below):
- The histogram minimums and maximums start at 0 instead of at the first value, so a
  minimum never shows above 0 or a maximum below 0. A white image shows `min=0` (R20).
- EXIF `RATIONAL` values are read as signed, but the EXIF standard makes them unsigned,
  so a part at 2^31 or above prints negative (the GPS case above; R21).

5 new cases cover these (79 in all).

**Checks:**
- **Tests catch the bugs:** run against the previous commit, all 5 new cases fail, with
  and without the sanitizers.
- **All 79 pass** with and without ASan/UBSan.
- **Sanitizer corpus clean:** the 3,273 files from the [sanitizer run](#sanitizer-run)
  give no ASan or UBSan reports. After the crash fixes, 98 still had C7 reports. Nine very
  tall or wide files exceeded 60 s in the unoptimised sanitizer build with eight running
  at once. A normal build decodes them in 0.5–7.3 s, the same as before the change.
- **Expected changes only:** reports of 32 valid files, decoded with and without full
  IDCT and histograms, differ only in the Average Pixel Luminance line. It rises by 1–15
  in 22 of them.

### Inherited R13–R15, R17 and R18

**Fixed:** R13–R15, R17 and R18 (its average-luminance item was fixed with C7).
- **R13 Big-endian thumbnails:** the thumbnail's Compression value is now taken as decoded
  for its own format. A 4-byte read made it 0x00060000 in big-endian files, so their
  thumbnails were never analysed.
- **R14 Redefined Huffman tables:** a DHT now clears the table it defines. The fast lookup
  kept the old table's entries for the prefixes of the new table's longer codes. A noisy
  64×64 image with its AC table defined twice had 14 scan errors; it now decodes cleanly.
- **R15 Chroma placement:** subsampled blocks are now 8 pixels times their expansion
  factor apart, and the scan-error position follows the same rule. With Y 4x1 and Cb/Cr
  2x1, the second chroma block was drawn at x 8–23 instead of 16–31, so part of each MCU
  had the wrong colour and the rest none. libjpeg (through Pillow) decodes the test file to
  the same colours, within 1.
- **R17 Clipping notes and totals:**
  - Each "YCC Clipped" note gives the file position of its own MCU, not the end of the scan.
  - The totals count every clipped pixel; the YCC totals stopped at the 10 that were noted.
  - Padding up to whole MCUs is not counted: a test image reported 128 RGB clips for 96
    clipped pixels.
  - The totals start again at each preview calculation instead of adding up.
  - Clipping is only counted with Histogram RGB on, and without it the report said 0 for
    everything. It now says that clipping wasn't counted.
- **R18:**
  - **UserComment:** a count below 8 wrapped, and the text after the field was printed as
    the comment.
  - **ICC header,** checked against ICC.1:2022 (7.2.4, Tables 21 and 22) and, for the
    flags, Little CMS. The version printed nibbles 7 to 4, so 4.2 was "0.4.2.0"; it is now
    "4.2.0.0", as the spec writes it. "Can't be used independently", negative polarity and
    black & white media were each reversed, so ordinary profiles (all flags 0) showed all
    three.
  - **EXIF byte order:** a header with neither "II" nor "MM" now gives an error and its IFDs
    are skipped. Before, the byte order was left from the previous file, or never set; it is
    now reset for each file.
  - **DQT quality estimate:** a zero chroma entry counts as 999.99%, as for luminance. It
    took the previous entry's percentage, or an uninitialised one for the first entry.
  - **Photoshop quality:** an APP13 without one no longer zeroes a quality found earlier,
    such as Save for Web from APP12 "Ducky". The "Photoshop IRB detected" note and its weight
    in the assessment were lost.
  - **Truncated scans:** the "Early EOF" error was already reported, but the scan decoder
    decoded the zeros read past the end as image data. It now stops there and says in which
    MCU.
  - **BufX:** a 2- or 4-byte read ending on the last byte of its window, such as the end of
    the file, returned 0: an IPTC Record Version of 4 showed as 0.
  - **FlashPix fields** were read with two `m_nPos++` in one expression, which is undefined.
    Clang's 8 `-Wunsequenced` warnings are gone. Its output was right and is unchanged; a
    new case checks it.
- **Also:** `BufX`, `ReadBe4` and the buffer search shifted bytes of 0x80 and above into the
  sign bit of an `int` (undefined in C++17); they now shift unsigned values.

**Found, fixed later** (inherited; R22 below): a UserComment with the "UNICODE" character
code is read one byte at a time and stops at the first zero byte, so it shows at most one
character.

19 new cases cover these (98 in all). The test driver can print decoded pixel colours
(`--pixels`), and `gen_cases.py` has a small encoder for files Pillow can't write.

**Checks:**
- **Tests catch the bugs:** run against the previous commit, 18 of the 19 new cases fail.
  The FlashPix case passes there too, as its output didn't change.
- **All 98 pass** with and without ASan/UBSan.
- **Sanitizer corpus still clean:** the 3,273 files give no ASan or UBSan reports. Only
  two 65535-pixel files exceeded 60 s in the sanitizer build, as before.
- **Expected changes only:** of 64 reports (32 valid files, with and without full IDCT and
  histograms), 33 change, and only in the clipping totals without histograms, the offsets in
  clipping notes, and the ICC header lines. The notes themselves are the same. Pillow reads
  the same ICC values from the two corpus seeds with profiles.

### R20–R22

**Fixed:** R20–R22, the three report findings left from the C7 and R18 work (all inherited).
- **R20 Histogram minimums and maximums:** each minimum now starts at the largest value
  and each maximum at the smallest, so the first pixel sets both. A white image shows
  pre-ranged Y `min= 1016 max= 1016` (was `min=0`), a black one `min=-1024 max=-1024`
  (was `max=0`). If no pixel was counted, the line shows zeros rather than the starting
  extremes and a NaN average.
- **R21 RATIONAL:** RATIONAL parts are read as unsigned and SRATIONAL parts as signed, both
  for the value and for the fraction shown. XResolution 3000000000/1 now shows as that,
  and a GPS degree of 0xFFFFFFFF/1 as 4294967295. A GPS value stored as SRATIONAL can
  still be negative; the C7 case now uses one, so that path stays tested.
- **R22 Unicode UserComment:** text with the "UNICODE" character code is read as 16-bit
  units in the EXIF byte order (a byte order mark, if present, decides instead), up to the
  first zero unit. "Hello World" now shows in full in both byte orders.

6 new cases cover these (104 in all).

**Checks:**
- **Tests catch the bugs:** run against the previous commit, all 6 new cases fail.
- **All 104 pass** with and without ASan/UBSan.
- **Expected changes only:** of the 64 reports of 32 valid files, only the 24 with
  histograms change, and only in their min/max values; the averages are the same. None of
  these files has a large RATIONAL or a Unicode comment.

### Follow-up review (F1–F10)

A follow-up review on 2026-11-14 (not in the repository) checked
commit `fa81c75` and found ten more problems, all inherited from master. Each was
reproduced here first, with the review's own numbers, before fixing.

**Fixed:**
- **F1 Later scan headers (crash):** only the first scan sets the image details. A
  3-component scan after a Y-only one switched the decoder to three components while only
  the Y map existed, so choosing an image channel read a null chroma map (a crash in normal
  builds too).
- **F2 Photoshop string lengths (hang):** a length from the file may move the cursor no
  further than the end of its resource block, and never so far that the position wraps.
  Descriptor items, list items and slices must each move forward. A key length of
  0xFFFFFFF8 moved the cursor back 8 bytes, so one item repeated for 2^28 declared items.
  The Unicode string reader had the same unchecked length.
- **F3 RGB JPEGs:** three components are RGB when libjpeg would say so: no JFIF APP0, and
  Adobe APP14 transform 0, or no APP14 and component IDs 'R','G','B'. The preview then uses
  R, G and B as they are, and the statistics, channel views and brightest pixel get the
  equivalent YCbCr. Red decoded as (75,255,28); the test images now match libjpeg.
- **F4 Stale preview:** the preview and both histograms are cleared before each file is
  analysed (open, Reprocess, empty file, failed open), so an undecodable file such as a
  progressive JPEG no longer shows the previous file's image.
- **F5 Histogram bars:** bar heights use a 64-bit product. A bin of over 35.8 million
  pixels overflowed, so a white 6000×6000 image's only bar wasn't drawn.
- **F6 MCU padding:** average luminance, the histograms, their statistics and the
  brightest-pixel search use only pixels inside the image, like the clipping counts
  (R17). A 9×8 image of eight black columns and one white averaged 127 instead of 28.
- **F7 Multi-scan JPEGs:** still only the first scan is decoded, but the report now says
  so. It warns when the first scan lacks some of the image's components and for each
  scan that isn't decoded.
- **F8 XMP:** the packet runs to the true end of the segment (its last byte was dropped),
  the last line is printed even without a final newline, and lines are decoded as UTF-8.
- **F9 SSHORT:** values are sign-extended in all three paths: -1, -2 printed as
  65535, 65534.
- **F10 Undefined DQT:** a component whose quantization table was never defined is an
  error, and the scan isn't decoded. It was used as an all-zero table.

**Also:** the SOF component IDs are stored at index 1..Nf, which a header comment had
wrong.

16 new cases cover these (120 in all). In decode mode with `--histo`, the test driver
also counts the pixels drawn in each histogram image, so F5 shows in normal builds too.

**Checks:**
- **Tests catch the bugs:** run against the previous commit, 15 of the 16 new cases fail.
  The one that passes, an Adobe file saying YCbCr, is a control: it must stay YCbCr.
- **All 120 pass** with and without ASan/UBSan.
- **Sanitizer corpus still clean:** the 3,273 files give no ASan or UBSan reports; the
  same two 65535-pixel files are slow under the sanitizers.
- **Expected changes only:** of the 64 reports of 32 valid files, 14 change, all for the
  7 files whose size isn't a whole number of MCUs. Only average luminance, histogram
  statistics and histogram bars change: the 97×61 images now average 66 instead of 64.

**Not fixed:** the unfinished port features the follow-up review lists. Select All,
Find, Export JPEG/TIFF, Detailed Decode and About do nothing; Batch Process isn't in any
menu; PSD images aren't decoded (see section 8).

**Second check of `cf42610`:** the same reviewer found F1, F3 and F5–F10 fixed, and two
fixes incomplete. Both are now fixed:
- **F2:** the limit stopped applying once the cursor reached the resource's end. A
  descriptor that ended right after its name read its classID length from the data after
  the resource, and then that many characters (a million, in the reviewer's file). Now,
  while a resource is parsed, descriptors and lists read nothing past its end. A field
  that doesn't fit (a string's length or text, the 4-character form, a descriptor or
  list count, an item's type, or an integer or boolean value) is reported once with its
  offset, and parsing of that resource stops at its end. The limit applies only inside
  resources: the PSD layer records use the same string reader, with the old wrap check
  only. Lists also printed their OSType key from the wrong variable, so it was always
  empty.
- **F4:** after an empty file or a failed open, the screen was cleared, but the decoder
  still had the previous file's image, so a zoom or channel change drew it again. The
  decoder now forgets its image, preview and histograms whenever the views are cleared.
  The image source is always marked changed first, so the next file is decoded afresh.
  A failed open also added its error to the end of the previous file's report, which
  stayed; the report is now cleared first, as it already was for an empty file.

9 new cases (129 in all): 6 fail against `cf42610`, and the report one against
`90f2ab2`, which had the preview fix but not this one. The other two check that
the test driver closes the failed open's message box, which used to wait for a click,
and that the error is in the report. The driver also checks the preview again after
`--cycle-channels`.

**Checks:**
- **All 129 pass** with and without ASan/UBSan.
- **Cut-off sweep:** a 170-byte slices resource whose descriptor has an integer, a text
  string, a list, an enum and a nested descriptor, with its length set to every value
  from 0 to 170. Before, 152 of the 171 files read past the resource. Now no descriptor
  does: 154 report the overrun and stop, and 8 cut between items just end the
  descriptor early, as before. The 7 cut inside the slice header's two 4-byte version
  numbers still read those fixed fields past the end, which the existing "Parsing
  exceeded expected length" error reports, as for other resource types. Under ASan/UBSan
  the sweep gives no reports.
- **Sanitizer corpus still clean:** the 3,273 files give no ASan or UBSan reports; the
  same two 65535-pixel files are slow under the sanitizers.
- **No change for valid files:** the 64 reports of the 32 valid files are the same as
  for `cf42610`, as are the reports of the 639 corpus files that contain 8BIM resources
  (none of which has a descriptor).

**Still open:** Developer ID signing and notarization (M5); latent code; the unfinished
port features above.

## How this was reviewed

- Six parallel reviews, one per area, each comparing the Qt code against `upstream/master`:
  decoder metadata half (`JfifDecode.cpp` 1–4973), decoder markers half (4974–end),
  `ImgDecode`, `DecodePs`, the buffer/database/config layer, and the Qt UI.
- Each reviewer reproduced findings with crafted files, a headless test driver around the
  real decoder classes, small test programs linking the app's object files, or
  ASan/UBSan builds. "Traced" means confirmed by reading the code, not by running it.
- I re-checked the main findings myself against the source, and reran the crash, hang and
  wrong-output cases marked **re-checked** below.
- A separate ASan/UBSan build was run over 3,273 malformed and valid JPEGs
  (see [Sanitizer run](#sanitizer-run)). It independently hit C2, C3 and H1 and found the
  extra undefined behaviour listed under C7.

---

## 1. Memory safety and crashes from opening a file

**C1. Pixel-map size overflows: heap buffer overflow write**
`ImgDecode.cpp:3393-3425`, `2833-2838` · Inherited (wrap), Port (abort) · reproduced with ASan
`nPixMapW * nPixMapH` is computed in `int32_t`. An SOF of 65529–65535 pixels square pads
to 65536×65536, the product wraps to 0, `new int16_t[0]` is allocated and `SetFullRes`
then writes decoded pixels past it (ASan: heap-buffer-overflow WRITE at 2947). Products
between 2^31 and 2^32 go negative and abort with `std::bad_alloc`.
Fix: compute sizes in `size_t`, cap total pixels, check the allocation.

**C2. SOS Huffman table selector ≥ 4 reads ~16 GB out of bounds**
`ImgDecode.cpp:3555-3572` · Port (64-bit) · reproduced (SIGBUS)
`SetDhtTables` rejects the selector and leaves `m_anDhtTblSel` at -1, decoding continues,
and the check loop indexes `m_anDhtLookupSize[c][0xFFFFFFFF]`. Harmless on 32-bit Windows,
a crash on 64-bit. One edited byte in any JPEG triggers it.

**C3. DHT with more than 256 codes reads past a stack array**
`JfifDecode.cpp:5163-5188` · Inherited · reproduced with ASan
`anDhtCodeVal[257]` is filled up to 256 entries, then read by index up to the sum of the
code counts (up to 4080). Index 256 is never initialised. The values reach the log when
Expand DHT is on.

**C4. RGB histogram writes one element past each array**
`ImgDecode.h:162`, `ImgDecode.cpp:4983-4987` · Port · reproduced
`HISTO_BINS` was changed from 128 to 255 (`// 128` left as a comment), so `bin_divider` is
1 and a channel value of 255 indexes element 255 of a 255-element array, spilling into
the next array. A pure-white image draws the same histogram as a pure-black one.

**C5. Divisions by zero**
`JfifDecode.cpp:9985-9986`, `ImgDecode.cpp:3269-3270` · Inherited · reproduced with UBSan
A second SOF stores a zero sampling factor before validation rejects it, and an SOS
with more components than the SOF leaves a factor at 0. arm64 silently returns 0
("Chroma subsampling: 0x0"); an x86_64 build would crash.

**C6. Undefined shifts in the Huffman mask table**
`ImgDecode.cpp:1094-1097`, `JfifDecode.cpp:511-514` · Inherited · UBSan on every file
`(1 << 31) - 1` overflows a signed int and `<<= 32` shifts by the type width. Correct
results on arm64 today, but a compiler may miscompile it. Use `1u` and special-case 0.

**C7. Other undefined behaviour found by the sanitizer run** · Inherited unless noted
- `ImgDecode.cpp:2919`: corrupt coefficients give IDCT values outside `int16_t` and the
  `static_cast<int16_t>(fVal * 8)` is undefined (73 files). Garbage pixels, no crash.
- Statistics sums are `int32_t` (`ImgDecode.h:257`, `267`, `287`; summed at
  `ImgDecode.cpp:4902-4908`, `5019-5025`, `5236`, `5369`) and overflowed on 23 corrupt
  files. Large valid photos with histograms on may overflow them too (not measured).
- `ImgDecode.cpp:4291`: DHT statistics divide by a zero total when a table is empty.
- `JfifDecode.cpp:2382`: a negative GPS degree value is cast to `uint32_t`.

## 2. Hangs and runaway memory from crafted files

All of these are reproduced and inherited from master. Most add a log line per pass, so
memory grows by hundreds of MB within seconds until the app is killed.

| # | Where | Trigger |
|---|---|---|
| H1 | `JfifDecode.cpp:5796-5829` | EXIF IFD whose "next IFD" offset points to itself; no visited-set or count limit. **Re-checked:** 519 MB in 10 s |
| H2 | `JfifDecode.cpp:4349-4367`, `DecodePs.cpp:2966`, `2819` | APP13 8BIM block with length 0xFFFFFFF4: the 32-bit position wraps back to the same header; loop not bounded by the APP13 length. **Re-checked:** 282 MB in 10 s |
| H3 | `JfifDecode.cpp:5666-5707` | XMP APP1 with length < 32: `nPosMarkerEnd - m_nPos` wraps to ~4 billion bytes to append (1.7 GB in 15 s) |
| H4 | `JfifDecode.cpp:9411-9731` | AVI chunk size 0xFFFFFFF8 wraps back to the same chunk; `DecodeAvi` runs on every file opened |
| H5 | `JfifDecode.cpp:9234-9254` | Embedded-thumbnail SOS skip has no EOF check; runs ~4 billion iterations past EOF |
| H6 | `JfifDecode.cpp:6087-6103` | APP0 JFIF thumbnail dump appends an ever-growing line per pixel: an 18-byte APP0 declaring 255×255 makes ~166 M characters |
| H7 | `DecodePs.cpp:1418-1434`, `1506`, `1538`, `3011`, `3365`, `3411` | Photoshop counts from the file (grid guides, layer groups, slices, descriptor and list items) are never checked against the block end |
| H8 | `DecodePs.cpp:527` → `WindowBuf.cpp:884` | IPTC numeric field with a byte count other than 1, 2 or 4 opens a modal dialog per field |

Fix pattern: bound every loop by the segment end, require forward progress, compute
positions in 64 bits or check for wrap, and cap counts and recursion depth.

## 3. Wrong results in the report and the edit assessment

**R1. GPS values print printf placeholders** — Port · reproduced
`JfifDecode.cpp:2396`, `3450`, `3454`, `3458`: `QString("%.3f").arg(x)` has no `%1`.
Every photo with GPS shows `37 deg 48' %.3f"`, `GPSAltitude = %.3f m`,
`GPSTimeStamp = %.0f:%.0f:%.2f`. Fix: `.arg(v, 0, 'f', 3)`.

**R2. JPEG comments print as decimal character codes** — Port · **re-checked**
`JfifDecode.cpp:6915`: `QString("%1").arg(uint8_t)` formats a number. The comment
"CREATOR: gd-jpeg v1.0 …" prints as `Comment=6782696584…`, and the "comment names known
editing software" check (`SearchCom`) can never match. Same pattern at 5998 (unreachable).

**R3. IPTC values are dropped** — Port · reproduced
`DecodePs.cpp:621-625`: five `.arg()` calls for four placeholders; the indent fills
`%1` and the value falls off. Captions, keywords and copyright never appear.

**R4. Photoshop strings and OSType keys print as decimal codes** — Port · reproduced
`DecodePs.cpp:550`, `698-702`, `904`, `966`, `3384-3388`, `3427-3431`. Because OSTypes
never match `"Objc"`, `"TEXT"`, `"long"` and the rest, descriptor parsing falls out of
step. Fix with `QChar(byte)`, but **add a recursion depth limit in the same change**:
the fix re-enables Descriptor → OSType → Descriptor recursion that has no limit (a
reviewer's patched build reached 2.1 GB on 20,000 nested items).

**R5. Photoshop Unicode strings are cut to one character** — Port · reproduced
`DecodePs.cpp:722-762` builds a `QString` from UTF-16 bytes as a C string. "Adobe
Photoshop" in Version Info shows as "A"; same for alpha channel and slice names.
(The same mistake in `WindowBuf::BufReadUniStr2` affects EXIF XP* tags; see section 8.)

**R6. RAW-converter detection broken in the edit assessment** — Port · reproduced
`JfifDecode.cpp:4137`, `4145`: `"[%1]:[%1],"` puts the tag name in both slots, so
`m_strImgExtras` never contains `[Canon.ImageType]:[CRW:` or `[NikonN.Quality]:[RAW`.
A Canon CRW conversion was assessed "Class 4 … EXIF fields indicate original".

**R7. Portrait images report the wrong chroma subsampling** — Port · reproduced
`JfifDecode.cpp:9996`: `"%1x%1"` gives "1x1" instead of "1x2" for portrait 4:2:2, which
also breaks the signature subsampling match.

**R8. Known-software comment check tests containment backwards** — Port · reproduced
`DbSigs.cpp:854`: `listEntry.contains(comment)` instead of `comment.contains(listEntry)`
(master 798). Real comments never match; one-letter comments match.

**R9. Camera exception lookup compares models wrongly** — Port · reproduced
`DbSigs.cpp:971`, `989`, `997`: tests `model != entry` where master tested for an empty
model, and the wildcard prefix logic differs. Edited SONY/HTC/Apple files without
makernotes are no longer flagged as likely edited.

**R10. Three HP signature entries can never match** — Port · checked by diff
`Signatures.inl:634-636`: `"(V01.00)d"` became `"(V01.00d)"` during conversion. The other
3,344 entries match master exactly.

**R11. Two-value fields print the first value twice** — Port · reproduced
`JfifDecode.cpp:3096` (`"%1, %1"`): ISOSpeedRatings [100, 200] prints `100, 100`.
Line 3272 has the same output but master had that bug too.

**R12. Unknown tag names lose their number** — Port · reproduced
`JfifDecode.cpp:1632` (`"GPS.0x%"`), `1040` (`"Canon.x%1.x%1"`). Visible only with
"hide unknown EXIF tags" off.

**R13. Big-endian EXIF thumbnails are never analysed** — Inherited · reproduced
`JfifDecode.cpp:4210` reads the 2-byte Compression value with `ReadSwap4`, so in Motorola
order 6 becomes 0x00060000. Fix: `ReadSwap2`.

**R14. Huffman fast lookup goes stale when a DHT table is redefined** — Inherited · reproduced
`ImgDecode.cpp:941-1023`: `m_anDhtLookupfast` is cleared once per file. A valid file that
redefines a table before SOS (allowed by T.81) decoded at 9 dB with 10 Huffman errors;
libjpeg decodes it fine. Fix: clear `[class][dest]` when `nInd == 0`.

**R15. Chroma misplaced for some subsampling layouts** — Inherited · reproduced
`ImgDecode.cpp:2898` places blocks at `nCssXInd*8` without the expansion factor. Y 4x1
with Cb/Cr 2x1 decodes at 17 dB (red); common layouts are fine (42–50 dB).

**R16. RGB histogram drawn one row too high** — Port · reproduced
`ImgDecode.cpp:4617` has an extra `- 1`; full-height bars spill into the band above.

**R17. "YCC Clipped" notes and totals are misleading** — Inherited · reproduced
`ImgDecode.cpp:5004-5198`. The MCU coordinates are right (all clips seen come from MCU
row 0 because only the first 10 are printed), but "@ Offset" is the bit reader's position
after the whole scan, so it is always the end of the scan. The `nClip*` totals only count
inside the first-10 guard, include padding pixels, and accumulate across preview changes.

**R18. Other inherited decode details**
- `JfifDecode.cpp:3712`: UserComment with count < 8 wraps and reads past its data (bounded by EOF).
- `JfifDecode.cpp:4480-4486`, `4640-4676`: ICC version printed as "0.4.0.0" and some flag bits reversed (checked against the spec from memory, not a copy).
- `JfifDecode.cpp:5740-5752`: EXIF byte order is uninitialised when the header is neither "II" nor "MM".
- `JfifDecode.cpp:6376-6382`: DQT chroma quality estimate uses an uninitialised percentage when a table entry is 0.
- `JfifDecode.cpp:4373`: a later APP13 zeroes the Save-for-Web quality set by the APP12 Ducky handler.
- `ImgDecode.cpp:5476`: average luminance adds 1 to both dimensions, and the uint32 sum overflows above ~16.8 MP.
- `ImgDecode.cpp` `BuffAddByte`: truncated scans decode zeros past EOF with no error.
- `WindowBuf.cpp:864`, `924`: `BufX` uses `<` where `<=` is needed, so a multi-byte read ending on the last byte of the file returns 0.
- `JfifDecode.cpp:4817-4935`: `Buf(m_nPos++) << 8) + Buf(m_nPos++)` is unsequenced (undefined); verified correct with clang -O2 today.

**R19. Log text regressions** — Port
`JfifDecode.cpp:9548` (`"[%.2f]"`), `9707` (`"[1s]"`), `6491` (DAC index off by one),
`7971-7985` (thumbnail hash lowercase), `3935-3944` (U+FFFD where an en dash was);
`ImgDecode.cpp:3650` (`"%%"`); `DecodePs.cpp:862` (stray `s`).

Found while fixing C7 and R18 (all inherited; line numbers as of commit `a624bd9`):

**R20. Histogram minimums and maximums start at 0** — Inherited · reproduced
`ImgDecode.cpp:3816` clears the statistics with `memset`, so a minimum never shows above 0
and a maximum never below 0: a white image reports `min=0`, a black one `max=0`.

**R21. EXIF RATIONAL values are read as signed** — Inherited · reproduced
`JfifDecode.cpp:2314`, `2347`: RATIONAL is two unsigned 32-bit parts (SRATIONAL is the
signed one), but both are read as `int`. XResolution 3000000000/1 prints as
-1294967296/1, and a GPS degree of 0xFFFFFFFF/1 as -1.

**R22. Unicode UserComment shows at most one character** — Inherited · reproduced
`JfifDecode.cpp:3731`: the text after the "UNICODE" character code is read one byte at a
time and stops at the first zero byte: "Hello World" shows as "H" in Intel-order EXIF and
as nothing in Motorola order.

## 4. GUI and settings

**U1. Opening a second file doesn't decode its image; can then crash** — Port · **re-checked**
`mainwindow.cpp:593-621`: only `reprocess()` emits `ImgSrcChanged`, where master's
`Reset()` did it on every open. The second file's report has no scan section and the
preview and histograms still show the first file. Opening a grayscale JPEG, then a colour
one, then hovering over the preview or picking View > Image Chan > YCC crashes (SIGSEGV).
Fix: emit `ImgSrcChanged()` in `open()`.

**U2. Preview mouse handlers don't check for a decoded image** — Port · reproduced
`Viewer.cpp:81-108`, `ImgDecode.cpp:5848-5880`, `6008-6050`. After the decoder resets
(Decode Image unticked then Reprocess, or a non-decodable file opened after a decodable
one) the old preview stays visible and the next mouse move dereferences null.

**U3. Preview coordinates aren't converted: out-of-bounds reads** — Port · reproduced with ASan
`Viewer.cpp:83`, `96-102`. Label coordinates are used as image pixels with no zoom
division or bounds check (the label is also stretched), and `SetMarkerBlk` receives
pixels where it expects 8×8 block coordinates. Hovering or clicking reads past the MCU and
block maps; dragging outside the window gives negative indices.

**U4. 800% zoom allocates 64× the image** — Port · reproduced
`Viewer.cpp:121` builds a full-size scaled `QImage`/`QPixmap` (master scaled while
painting). 300 MB for a 1 MP image; about 6 GB for a 24 MP photo.

**S1. Fresh install starts with Decode Image off** — Port · **re-checked**
`SnoopConfig.cpp:184`, `204-205`: missing keys fall back to `false` instead of the
constructor defaults (`true`), and the result is stored. New users get no preview,
histograms or scan statistics, and both coach messages are suppressed.

**S2. Maker Notes option can't be toggled; Hide Unknown checkmark inverted** — Port · reproduced
`SnoopConfig.cpp:678` tests `bOutputDHTexpand` instead of `bDecodeMaker`;
`mainwindow.cpp:414` initialises `hideUnknownTagsAct` from `decodeMaker()`.

**S3. Max-errors setting never persists** — Port · reproduced
`SnoopConfig.cpp:198` loads `General\ErrMaxDecodeScan`, `272` stores `Report\…`.

**S4. Configuration dialog: Save does nothing** — Port · reproduced
`snoopconfigdialog.cpp:81-84` (`onSave()` empty), `30`; a new unparented dialog per
opening (`mainwindow.cpp:821`); Browse can't select a folder (`62`, `ShowDirsOnly` without
directory mode).

**S5. Other GUI issues**
- `mainwindow.cpp:775`: Save Log suggests `…/photo.jpg/photo.txt` (`absoluteFilePath` for `absolutePath`).
- `mainwindow.cpp:647-663`: empty files are ignored silently, but Reprocess and Save Log then act on them.
- `mainwindow.cpp:138`, `849`: auto-reprocess with no file open shows "Couldn't open file: []" (only with a hand-edited setting).
- `JfifDecode.cpp:7585`: `emit(strText, 0);` is a no-op, so the status bar never updates; the `processEvents()` after it allows re-entry during long operations.
- `msgBox.setText()` without `exec()` at `JfifDecode.cpp:8759`, `8768`, `9816`, `10167`, `10195`, `10208`, `10244`, `10397`, `10409`: those messages never appear.
- `SnoopConfig.cpp:163`: date format `"yyyymmdd"` uses minutes (`mm`); only a stored value is affected.
- `DocLog.cpp:25-34`: `m_bEn` uninitialised (masked because callers call `Enable()`).

## 5. macOS-specific

- **M1. Dark Mode:** `DocLog.cpp:66-101` hard-codes the log text colour, ordinary text to RGB(1,1,1), on the system background, so the report is near-black on dark grey. **Re-checked** in source. Use palette colours or force a light background.
- **M2. Zoom buttons show a printer icon:** `mainwindow.cpp:295`, `301` ask the theme for `"document-print"`; Qt 6.7+ on macOS supplies the SF Symbols printer. **Re-checked** in source.
- **M3. Configuration becomes Preferences:** `mainwindow.cpp:427`: "Configuration..." is moved to the app menu as Preferences by Qt's menu-role heuristic, while the coach text still says "Configuration".
- **M4. Not Retina-aware:** `Viewer.cpp:121`, `mainwindow.cpp:714`, `720`: preview and histogram pixmaps ignore the device pixel ratio, so "100%" isn't 1:1 on Retina screens.
- **M5. Packaging** (from the build work): bundle ID is the placeholder `com.yourcompany.JPEGsnoopQt`, no app icon, ad-hoc signed only.

## 6. Leaks

- `ImgDecode.cpp:5479`, `4547`, `4631`: `m_pDibTemp` and both histogram `QImage`s are allocated on every decode and every View > Image Chan change and never freed (~96 MB per change for a 24 MP image). The pointers are also never initialised.
- `mainwindow.cpp:623`, `639`, `662`, `866`: a `QFile` per open; `SnoopConfig.cpp:649`, `214`: `Q_Note`/`Q_Eula` dialogs; `JfifDecode.cpp:7711`: `new unsigned char[2000]` per signature (two per decode); `mainwindow.cpp:821`: configuration dialog.
- `WindowBuf.cpp:60`: `delete` on `new[]` memory (Inherited).

## 7. Latent: disabled or unreachable code

These can't be reached in this build, but will matter when the features are finished.

- **PSD files are never parsed** (`JfifDecode.cpp:9859`: `DecodePsd` call commented out, `bDecPsdOk = false`). Inside that code: heap overflow from `nNumChans*nHeight` wrap (`DecodePs.cpp:2412`, `2426`, ASan-confirmed), a 0xFFFFFFF4 length loop (`2638`, loops at `1687`, `1983`, `2684`), a 16 GB allocation from an inverted layer rect (`1911`, `2146`), leaked channel arrays (`1866`), `char` vs `signed char` RLE (`2243`, breaks on Linux arm64), and an unchecked RLE writer (`2283-2340`). The `#else` branch at `9880` doesn't compile.
- **User signature DB:** `DbSigs.cpp:756` writes with `strlen` over an uninitialised buffer; `589-600` has no `DBEX_ENTRIES_MAX` check (overflows `m_sSigListExtra[300]`); `185` reads 1 byte instead of 4; `689` uses `"[%s]"`.
- **Export JPEG:** `JfifDecode.cpp:10321` skips writing the MJPEG DHT but logs that it did; `10232`, `10429` leak the `QFile` on failure.
- **Signature submit:** `strFormData` is never built (`8846` commented out).
- `General.cpp:107-111` (`Uint2DotByte`): operator precedence makes every field the low byte; `93-98` (`Uint2Chars`): chained `.arg()` substitutes `%` in file bytes.
- `WindowBuf.cpp:584`, `196`, `347`: printf formats passed to `.arg()`; `716`: `OverlayGet` has no bounds check.
- `q_decodedetaildlg.cpp:38-46`: empty `accept()`/`reject()`, so the dialog can't be closed once wired up.

## 8. Found earlier, during the macOS build work

- `WindowBuf.cpp:1113` (`BufReadUniStr2`): builds a `QString` from UTF-16 bytes as a C string, so EXIF `XPTitle`/`XPComment`/`XPAuthor`/`XPKeywords` show one letter, and FlashPix stream positions drift (`JfifDecode.cpp:4853`).
- `DbSigs.cpp:83`: writes `dbPath() + "Signatures.dat"` without a path separator, dumping the built-in list next to the app-data folder on every launch.
- `Viewer.cpp:29`, `56`: preview opens at 12.5% and `setScaledContents` stretches it, so it looks blocky.
- About 25 menu actions are created but never connected (batch, export, find, copy, detailed decode, DB, about, updates); no command-line mode; `FileTiff`, `JPEGsnoopCore`, `DecodeDicom` aren't built; user DB load/save disabled.
- Fixed already in `0a70ad1`: inverted APP0 JFIF/AVI1 checks (every JFIF file was reported as MotionJPEG) and the `wchar_t`-size user DB read.

## Sanitizer run

**Setup.** The decoder classes were rebuilt with AddressSanitizer and UndefinedBehaviorSanitizer
(plus float divide-by-zero) and driven headlessly in non-interactive mode with full IDCT
and histograms on, capped at 30 s and 1.5 GB per file.

**Corpus: 3,273 files from 12 starting images**
- **Starting images:** the two sample JPEGs plus Pillow-made grayscale, 4:4:4 q100, 4:2:2, 4:2:0 q5, progressive, CMYK, optimized Huffman, EXIF with GPS, 1×1 and 17×9 images.
- **Truncations** at about 50 offsets each.
- **Random changes:** 120 files per image with 1–16 bytes altered, mostly in the headers.
- **Field-targeted changes:**
  - every segment length set to 0–3, ±1 and 0xFFFF;
  - SOF height, width, component count, sampling factors and table IDs;
  - zeroed DQT tables;
  - inflated DHT code counts and bad DHT class/ID bytes;
  - SOS component count and table selectors;
  - DRI values;
  - random EXIF offsets and counts.

**Results.** It took 11 minutes; 2,990 files ran clean and 283 were flagged.

| Count | Result | Finding |
|---|---|---|
| 162 | ASan stack-buffer-overflow at `JfifDecode.cpp:5188` | C3 (every DHT with inflated counts) |
| 18 | ASan bus error at `ImgDecode.cpp:3560` | C2 |
| 1 | Hang: a random byte made IFD0's "next" pointer point to itself (5 GB after 90 s without ASan) | H1 |
| 2 | Timeouts on 65535-pixel-tall/wide SOFs; just slow under ASan (14 s, 726 MB without it) | none |
| 100 | UBSan only | C7 |

UBSan also reported C6 on every file; those reports were excluded from the flagging.

**Not covered by this run:** PSD files (the parser is disabled), AVI files, GUI paths, and
C1 (needs both dimensions near 65535; the corpus changed one field at a time). The
reviewers' hand-crafted files cover those cases.

## Recommended next steps

1. Fix the reachable memory-safety bugs and hangs first: C1, C2, C3, C4, U1–U3, H1–H8.
2. Fix the port regressions in sections 3 and 4. Most are one-line format-string or
   condition fixes; R1–R4 and R6–R9 change what users see on everyday files.
3. Turn the review tooling into tests: the headless decoder driver plus a small corpus of
   valid images with expected report snippets (GPS, IPTC, comments, subsampling) as a
   QtTest suite, and the crafted crash/hang files as regression cases under ASan/UBSan
   with a time and memory limit. Run both in CI on a macOS runner.
4. Report the inherited memory-safety bugs (C1, C3, H1–H6) upstream, since the released
   Windows 1.8.0 is affected.
