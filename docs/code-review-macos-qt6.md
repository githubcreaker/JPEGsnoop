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
  in whole-pixel steps, so the large sizes are sharp but look like pixel art. (It first
  used the `.ico`'s 16-colour frames; fixed later, see [About](#about).)
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

### PSD decoding

The follow-up review's list of port gaps included the PSD decode call, which was still
commented out, so a `.psd` file was parsed as a JPEG and reported only "File did not
start with JPEG marker". The call is back, and the composite image is shown in the
preview, as on Windows. The PSD parser had never run in this port, and it needed these
fixes first:

**Fixed:**
- **Image:** the Windows code drew into a DIB, which is stored bottom row first; the
  port's `QImage` version allocated an image and drew nothing into it. The image is now
  drawn into the preview the right way up. 8-bit RGB and grayscale images are shown.
  Other depths and colour modes, and sizes over Photoshop's 30,000-pixel limit, get a
  note that the image isn't shown.
- **Preview window:** it only showed a decoded JPEG scan. It now shows a PSD image, titled
  "Image" as on Windows. Over a PSD, the mouse doesn't look up MCUs, which it hasn't got.
- **Memory safety and hangs** (the PSD items in section 7, all inherited):
  - **Row table:** `nNumChans * nHeight` wrapped in 32 bits, so the table was empty and
    the decode read past it (heap overflow). The count is now 64-bit.
  - **Lengths near 4 GB:** an additional layer info length of 0xFFFFFFF4 wrapped the
    position back to the same block, so the decode never ended. Every length is now
    checked against the end of the file before it is used: sections, layer records,
    channel lists, masks, blending ranges, extra data, additional layer info, row tables
    and image data.
  - **Inverted layers:** a layer whose bottom was above its top was nearly 4 billion rows
    tall and allocated a 16 GB row table. That table must now fit in the file.
  - **RLE runs past the row:** they were written past the row and past the image. A row
    must now decode to exactly its size. The run count is now an `int8_t` (a `char` is
    unsigned on Linux arm64), and -128 does nothing, as PackBits defines.
  - **Leaks:** a layer's channel arrays were freed only after a successful decode; now they
    always are.
  - **Bad dimensions:** a width or depth of 0 with a huge height no longer steps through
    billions of empty rows.
- **Rows sized by depth:** rows were counted at a byte per pixel, so a 16-bit RLE image
  failed on its first row (inherited). 1-, 16- and 32-bit rows are now the right size,
  though only 8-bit images are drawn.
- **A bad layer or resource no longer hides the image:** each section's length is known,
  so an error in one is reported and the decode goes on after it. One RGBA file written
  by macOS's `sips` has a layer channel whose rows don't match its row lengths. The
  Windows code would stop there and show nothing; now the composite is shown.
- **Also:**
  - After a PSD error the decode stops, instead of going on to parse the file as a JPEG.
  - Layer masks and blending ranges are skipped by their lengths (a mask's rectangles are
    read first; see below). Reading them field by field relied on guesses about their
    layout.
  - "8B64" additional layer info is accepted.
  - The debug-build asserts on "Lr16" blocks and RLE errors are gone.
  - The PSD-only branch for builds without image decoding compiles.

Layers aren't drawn, as on Windows, where nothing turned that on.

19 new cases (148 in all): 18 fail against `6ad33ef`. The other checks that a bad RLE
row leaves no image, which holds trivially when nothing is decoded.

**Checks:**
- **All 148 pass** with and without ASan/UBSan.
- **Pixels:** five PSDs written by macOS's `sips` (RGB, grayscale, RGB with alpha and a
  layer, a flat colour, and 1000×700 with noise) decode to the same pixels as Pillow's
  reading of them, all 777,751. A sixth, 16-bit, is reported without an image.
- **GUI:** PSD previews at 12.5%, 100% and 800% match the decoded image, with mouse moves
  and channel changes. Going from a JPEG to a PSD and back shows each image, and a bad
  PSD after a good one shows none.
- **Fuzzing, partial:** an ASan/UBSan run over 47,146 damaged copies of 17 PSDs (every
  cut in the first 1,200 bytes, 4- and 2-byte fields set to extreme values at every
  offset in the first 600 bytes, and random changes) was stopped after 24 minutes,
  about 3,000 files. It used the build before the empty-row check. None of the files it
  reached crashed or gave a sanitizer report, and none was slow without the sanitizers.
  The whole set ran later, on the build after the second check below.
- **No change for other files:** the 64 reports of the 32 valid files are the same as
  for `6ad33ef`.
- **Not checked at run time: leaks.** macOS's ASan has no leak checker.

**Second check of `052cf64`:** a reviewer found three more problems. All are now fixed,
along with a fourth that the fixes exposed:
- **Size overflow (hang):** the raw image size, rows times row bytes, could still wrap in
  64 bits. A small file whose header gave 32768 channels of 2^20 rows, 2^29 bytes each,
  passed the size check and then stepped through 2^35 rows. The header must now be within
  Photoshop's limits for a PSD (56 channels, 30,000 pixels a side, and 1, 8, 16 or 32
  bits a sample), and every size is multiplied with an overflow check.
- **Tagged block padding** (inherited): every block's length was padded to 4 bytes, but
  in a layer record the specification pads it to an even number. After a 10-byte "luni"
  name such as "Cat", the next block was read 2 bytes late: its signature was reported
  as unknown, and the layer ID it held was lost. Blocks in a layer record are now padded
  to 2, and those at the end of the layer section to 4, as psd-tools reads them.
- **Mask channels** (inherited): every channel was decoded at the layer's size, so a 4×4
  layer's 2×2 user mask gave an RLE error and lost the rest of the layers. The user mask
  (-2) and real user mask (-3) channels now use their rectangles from the layer mask
  data. Each channel is also bounded by its length in the layer record, and the next
  starts after it, so a bad channel is skipped on its own.
- **Global layer mask info** (inherited): it is optional, but it was always read, from
  whatever followed the layer info. Files from `sips` have none, and once the bad channel
  no longer stopped the layers, their image data was read as this field, giving a false
  error. It is now read only if the section has room for it and no tagged block
  starts there, as psd-tools does.

4 new cases (152 in all), which all fail against `052cf64`; the size one times out.
Two cases now expect the new messages, and the empty-row case now uses a layer, because
the header limits stop such a composite first.

**Checks:**
- **All 152 pass** with and without ASan/UBSan.
- **psd-tools agrees:** it reads the new files as intended: layer ID 7 after "Cat", mask
  channels of 2×2 and 1×3, and no global layer mask info. The five `sips` PSDs still
  match Pillow's pixels, and the RGBA one now reports only its bad channel.
- **Fuzzing:** 11,563 damaged copies of five layer and mask files ran with no crashes
  or hangs (cut at every byte up to 700, 4- and 2-byte fields set to extreme values at
  every offset up to 450, and random changes). Under ASan/UBSan, 1,500 of them, picked at
  random, gave no reports.
- **Full fuzz run:** all 58,709 damaged files, these and the 47,146 above, then ran
  under ASan/UBSan on the `642da2d` build, 8 at a time, in 3 hours 10 minutes. None
  crashed, hung or gave a sanitizer report. 66 took over 5 seconds while sharing the
  machine, but one at a time each takes under 0.9 seconds with the sanitizers and under
  0.5 without.
- **Still no change for other files:** the 64 reports of the 32 valid files are the same
  as for `6ad33ef`.

**Still open:** Developer ID signing and notarization (M5); latent code; the other
unfinished port features (Select All, Find, Export JPEG/TIFF, Detailed Decode, About and
Batch Process).

### Export JPEG and Export TIFF

Tools > Export JPEG and Tools > Export TIFF were in the menu but did nothing. Both now
work as on Windows. Export JPEG copies the JPEG at the start offset to a file of its
own, or with Extract All, every JPEG in the file. Export TIFF saves the decoded image as
8-bit RGB, 16-bit RGB or 8-bit YCC. The JPEG copy code had been ported but was never
called; the TIFF writer and both settings dialogs were missing. Export JPEG analyzes
the file again first, as on Windows, and adds what it did to the report.

**Fixed while porting** (inherited unless marked):
- **Dropped byte:** the copy loops ran `while (pos < last)` over an inclusive range, so a
  range one byte past a whole number of 128 KB blocks lost its last byte: the byte
  before SOS, or the EOI's 0xD9.
- **Overwriting the source:** the exports refused only an output name spelled exactly
  like the source's. Saving `photo.jpg` over `Photo.JPG`, the same file on macOS's usual
  case-insensitive disks, or through a link, truncated the file being read. They now
  compare the files themselves. Output goes through `QSaveFile`, and every write is
  checked, so a failed export leaves no partial file.
- **Motion JPEG DHT** (port, section 7): the option logged "Inserting standard AVI DHT
  huffman table" but wrote nothing.
- **Force EOI** added a second EOI to an image that had one.
- **Nothing exported:** a file with no JPEG at all was told to force an EOI. The missing
  image data is now reported first, and a failed export says why in a message box, not
  only at the end of the report.
- **Extract All:**
  - **Report and start offset:** it left the report of the last place it looked and
    moved the start offset there. It now keeps the offset, shows the file's report as
    before, and lists each JPEG it exported or why it couldn't.
  - **Thumbnails** (port): reading past the end of the file marks the file buffer bad
    until it is reloaded. Windows reopened the file for each JPEG; the port didn't, so
    an EXIF thumbnail after the main image "ran out of buffer" and wasn't exported. The
    buffer is now reloaded each time.
  - **Message boxes:** each JPEG is analyzed without the report or message boxes, as a
    video can have thousands of frames. A write error stops it, with one message box.
- **Export TIFF:**
  - **Cancel:** cancelling the format dialog still wrote the file.
  - **No image:** it exported whatever its bitmap pointer held. It now says there is no
    decoded image. YCC needs a decoded JPEG scan, so a PSD's image is RGB only.
  - **Size:** a JPEG's image is exported at its own size. Windows included the padding to
    whole MCUs, so a 9×8 image came out 16×8.
  - **ReferenceBlackWhite:** written as 0 to 255 for every sample, but Cb and Cr are
    centred on 128. libtiff then converts a YCC export's colours about 80 levels off on
    average. With 0, 255, 128, 255, 128, 255, the TIFF default for YCbCr, they match
    the RGB export within 1.
  - **16-bit:** values were shifted left by 8, so white was 65280; they are now multiplied
    by 257, so it is 65535. Width, height and the strip fields are LONGs, not SHORTs.
- **Dialogs:** the settings dialog's start offset was always blank: it was given an empty
  string. Force EOI and Ignore EOI now exclude each other. Overlays is disabled, as
  nothing can add an overlay until File Overlay is ported. After a file that couldn't
  be opened, the menus stay enabled, so Export JPEG now says there is nothing to
  export instead of going through both dialogs to do nothing.

**Tests:** 38 new `E` cases (190 in all). The harness has new `export-jpeg` and
`export-tiff` modes, which run the real `MainWindow`, and `gui --export-menus`, which
goes through the menus and dialogs.
- **Export JPEG:** each case checks the written file's MD5 against the bytes the
  generator expects. Covered: whole files, trailing data, the block edge, missing SOI
  and EOI with each option, the Motion JPEG DHT, and the same file by another case.
  Extract All is tested with two JPEGs among other data, Motion JPEG frames without
  DHTs, and an EXIF thumbnail.
- **Export TIFF:** each case reads the file back and checks its tags and every sample
  against a separate decode.

**Checks:**
- **All 190 pass** with and without ASan/UBSan.
- **No change for other files:** the reports of 40 valid files and PSDs are the same as
  for `642da2d`.
- **Other readers:** Pillow, tifffile, libtiff (`tiffinfo`, no warnings) and macOS's
  ImageIO (`sips`) read every TIFF at the right size and depth, and agree on the
  samples. The files came from a 1024×768 JPEG, 4:2:0, greyscale, an RGB JPEG and a
  PSD. 16-bit samples are exactly the 8-bit ones times 257.
- **YCC colours:** libtiff's conversion of each YCC export matches its RGB export
  within 1 level.
- **ImageIO swaps red and blue** in uncompressed YCbCr TIFFs, Pillow's as well as these,
  so Preview shows a YCC export in the wrong colours. The samples are right, as libtiff
  shows.
- **Exported JPEGs:** frames exported with the Motion JPEG DHT decode in Pillow to the
  same pixels as the JPEG they came from.
- **Block edge:** a simulation of the old copy loop on the block-edge test file drops
  the byte at 131,072, the last before SOS.
- **Not checked:** a real Motion JPEG AVI. The tests use frames among other data, and the
  DHT option's default, which comes from the AVI check, is untested.

**Second check:** a reviewer found two more problems. Both are now fixed:
- **Numbered files replaced without asking:** Extract All asks for a name, then writes
  numbered names made from it. Files that already had those names were replaced
  silently, while the file dialog asked about the name chosen, which isn't written. The
  file dialog no longer asks about that name. Instead, if numbered files exist, a
  message says how many, names one, and asks before replacing them; Cancel exports
  nothing. Numbers the export doesn't reach are left as they are, and the message says
  so.
- **A frame's own Huffman tables replaced** (inherited): the Motion JPEG option inserted
  all four standard tables before SOS, replacing any the frame defines. A frame with
  optimized tables then decoded wrongly, though the export reported success. Only the
  tables the image doesn't define before its scan are now inserted, and the report says
  which, or that it defines them all. If the segments before SOS can't all be read,
  none are inserted, and the report says so.

9 new cases (199 in all):
- **Motion JPEG AVI:** a test AVI is exported through the menus with Extract All. The
  option is on by default, a frame with optimized tables comes out unchanged, and a
  frame without tables gets all four.
- **Tables:** a JPEG with all its tables comes out unchanged, and one without its chroma
  tables gets DC 1 and AC 1, byte for byte.
- **Replacing:** through the menus, Extract All asks before replacing a numbered file,
  keeps it on Cancel and replaces it on Replace, and doesn't ask about the name chosen.

**Checks:**
- **All 199 pass** with and without ASan/UBSan.
- **Not checked:** an AVI from a camera; the test AVI is made by the test generator.

**Third check:** the same reviewer found one more case. A marker may have fill bytes
(0xFF) before it, but a fill byte right before SOS left one byte, which failed the check
for a full marker. The check then stopped, so a frame without tables got none, though
the same frame without the fill byte got all four. The last byte before SOS is now
taken as a fill byte. 3 new cases (202 in all): a frame with one and with three fill
bytes before SOS, and one in an AVI exported through the menus. All three fail with the
old check.

**Checks:**
- **All 202 pass** with and without ASan/UBSan.
- **The reviewer's AVI:** its frame is exported with all four tables and decodes in
  Pillow to the same pixels as the JPEG it was made from.

**Still open:** Developer ID signing and notarization (M5); latent code; Select All,
Find, File Overlay, Detailed Decode, About and Batch Process.

### Detailed Decode

Options > Scan Segment > Detailed Decode did nothing. Its dialog had been ported, but
nothing opened it, and its OK and Cancel did nothing, so it couldn't have been closed
(section 7). It now works as on Windows:
- **Choosing the MCUs:** type the first MCU and how many to report, or click Load Coords
  to take them from the last one or two positions clicked on the image.
- **The report:** the next decode reports each variable-length code in those MCUs, each
  block's coefficients, and the first MCU's pixels.
- **New:** with Automatic Reprocessing on, that decode starts on OK, as for the other
  Scan Segment options. Windows waited for the next decode.

The decoder already had the code. Problems fixed in it (inherited unless marked):
- **The image changed in No IDCT mode:** the detailed path always ran the full IDCT. In
  No IDCT (DC only) mode, the default, the MCUs reported were drawn at full resolution
  and the rest from their DC, which changed the average luminance and brightest pixel
  too. The image now follows the decode mode. The report still lists every coefficient,
  and the pixel dump notes that it shows DC values only.
- **Cr labelled as Cb:** blocks were named after their quantization table. Cr nearly
  always shares table 1 with Cb, so both were "Chr(0)". They are now named after the
  component: Lum, Chr(0) for Cb and Chr(1) for Cr.
- **Pixel dump:**
  - **Last row:** a row ended at the next MCU's first pixel, so an MCU in the last column
    lost its last row.
  - **Repeated:** the dump is made when the preview is calculated, so each View > Image
    Chan change added another copy to the end of the report. Only a decode makes it now.
- **Range:**
  - **Wrapping:** the start and length were added in 32 bits. A start far below the
    image wrapped around to MCU [0,0], and a length near 2^32 reported nothing.
  - **Start past the edge:** a start beyond the right edge picked an MCU in the next row.
    A start outside the image now reports nothing, and a note says so.
- **Load Coords:**
  - **Reverse order:** after two clicks, later MCU first, the length was right but the
    range began at the later MCU.
  - **One click:** it loaded a length of 0, which reports nothing. It now loads 1.
  - **Failed open** (port): positions clicked on the previous image stayed after a
    failed open, so Load Coords offered them for a file that hadn't been decoded.
- **Dialog** (port): fixed positions are replaced by layouts.
  - **Fields** must be whole numbers, checked on OK. The length must be at least 1
    while the decode is on.
  - **Load Coords** is unavailable until a position is clicked.
  - **Decode Image off:** a note says so, as nothing is decoded then.
- **New report line:** the setting applies to every file decoded until it's turned off,
  so the scan section now says what it covers, as in "Detailed Decode: 6 MCUs from MCU
  [1,0]".

**Tests:** 40 new `D` cases (242 in all).
- **`decode --detail X,Y,LEN`:** summarizes what the report shows, then decodes the file
  again without the detail. Cases check that only detail lines are added and the image
  is unchanged, in both decode modes. Files: every MCU of 4:2:0, 4:4:4 and greyscale
  images, restart markers, a corrupt scan, a truncated scan, an RGB JPEG and a
  multi-scan file.
- **`gui --detail-dlg`:** clicks the image and goes through the menu and dialog.
- **Against the old code:** the decode cases fail before this change. They show the Cr
  label, 15 dump rows instead of 16, a start past the edge picking MCU [0,1], and the
  image changing in No IDCT mode. The menu cases did nothing.

**Checks:**
- **All 242 pass** with and without ASan/UBSan.
- **No change with the setting off:** the reports and images of the 109 test files that
  run in under a minute, in both decode modes, are the same as for `525232b`.
- **Every test file in detail:** decoded with every MCU reported, in both modes, each
  gives the same report apart from the detail lines, and the same image.
- **Speed:** an MCU of a 4:2:0 image is about 80 report lines. With a 1024×768 JPEG,
  100 MCUs took 1.2 seconds and all 3,072 (249,000 lines) 29 seconds; an earlier run
  on a busier machine took twice as long. Nearly all of the time is spent in Qt's text
  widget: with the report off, a decode that took 8.8 seconds took 0.46. Windows had
  no limit, and none is added.
- **Not checked:** the dialog in the macOS style. The tests run offscreen, where it was
  checked in Qt's Fusion style.

**Second check:** a reviewer found three errors in what the detail lines say. All are
fixed, along with two more found while checking them (all inherited):
- **Long codes cut off:** each code was shown in the 4 bytes from where it starts. A
  16-bit code with a 10-bit value from bit 7 takes 33 bits, so its last bit was missing.
  As many bytes as the code needs are now shown, 5 at most. Lines that fit in 4 bytes
  are unchanged: 64,000 lines from six files are the same as before.
- **DC in the matrices:** each matrix showed the block's DC difference, not its DC, so in
  a flat image every block after the first had DC 0. The component's predictor is now
  added, read after any restart marker in the block. The code's own line still shows
  the difference, as coded.
- **RGB JPEG labels:** an RGB JPEG's blocks were Lum, Chr(0) and Chr(1). They are now R,
  G and B.
- **Found while checking:**
  - **DC of 0 marked "EOB":** a DC difference of 0 has size 0, which the decoder returns
    as it does an end of block, and the line said "EOB".
  - **After a restart marker:** the first code was reported at the padding before the
    marker, with the padding's bits. The normal decode gave the same position for a bad
    code there: "Bad huffman code @ 0x00000298.0" for a code at 0x29B. Both now give the
    position after the marker.

10 new cases (252 in all), with generated files:
- **Long code:** a JPEG with the 26-bit code at bit 7. Its line must show all 26 bits.
- **DC:** flat grey and colour images, with and without restart markers. Every matrix
  must show the same DC: 416 for grey, and -32, -336 and 432 for Y, Cb and Cr.
- **Other files:** an RGB JPEG, and a JPEG with an invalid code right after a restart
  marker.
- **Against the old code:** each case fails before these fixes but one. In the flat grey
  file with a restart marker after every block, each difference is the whole DC, so its
  matrices were already right.

**Checks:**
- **All 252 pass** with and without ASan/UBSan.
- **Setting off:** the reports are the same as for `525232b` for every test file but the
  new one with a bad code after a restart marker, where only the error's position
  changes.
- **Every test file in detail:** still adds only detail lines, in both decode modes.
- **The reviewer's files:** the long code shows all 26 bits, and all three flat blocks
  show DC 416.

**Still open:** Developer ID signing and notarization (M5); latent code; Select All,
Find, File Overlay, About and Batch Process.

### About

Help > About JPEGsnoop did nothing. It now opens the About box Windows has: the icon,
name, version and copyright, and links to the documentation and the site.
- **macOS menu:** the item has Qt's About role, so on macOS it is in the application
  menu as "About JPEGsnoop", where Mac apps put it, as Configuration already is.
  Elsewhere it stays in Help.
- **Changes from Windows:**
  - **https links:** both pages answer over https (checked 2026-11-15); Windows used
    http. The documentation link still ends in `?ver=` and the version.
  - **Qt version:** a line gives the Qt it runs on, for bug reports.
  - **Icon:** the 48-pixel frame of `images/jpegsnoop.ico` with the most colours (the
    32-bit one), doubled pixel for pixel on a Retina screen so it stays sharp.

**Found and fixed: the macOS app icon used the 16-colour art.**
`images/jpegsnoop.icns` was built from the `.ico`'s 16-colour frames.
`scripts/make-macos-icon.py` asked Pillow for each size, and Pillow returns the first
frame of that size, which in this file is the 16-colour one. The 48-pixel art came out
with 11 colours instead of 821, and without the 32-bit frame's shading and shadow.
- **Fix:** the script now takes each size's frame with the most colours (the 32-bit
  one), and `images/jpegsnoop.icns` is rebuilt from them.
- **Checked:**
  - The 16 and 32-pixel images in the new `.icns` equal the 32-bit frames pixel for
    pixel, and the 1024-pixel one equals the 48-pixel frame enlarged 16 times.
  - Every size now has the 32-bit frame's colours: 163, 465 and 821 for the 16, 32 and
    48-pixel art.
  - Running the script again gives a byte-identical file. Pillow 10.0.1 and 13.0.0
    pick the same frames.
  - The packaged app and its zip carry the new icon, and the signature checks out.

**Tests:** 6 new `A` cases (258 in all). `gui --about` chooses the menu item and prints
its menu role, the dialog's texts and links, and the icon's size, pixel ratio and
number of colours, then clicks OK. The expected version is read from `source/snoop.h`.
On the old code all six fail: the item had no handler.

**Checks:**
- **All 258 pass** with and without ASan/UBSan.
- **Layout:** checked offscreen in Qt's Fusion style, with light and dark palettes.
- **Not checked:** the dialog on screen in the macOS style, the application menu, and
  the link colour in Dark Mode. The test's dark palette sets no link colour, so links
  there are the light palette's dark blue; on macOS, Qt takes the system's.

**Still open:** Developer ID signing and notarization (M5); latent code; Select All,
Find, File Overlay and Batch Process. The new icon has not been seen in the Finder or
Dock, only as the images in the `.icns`.

### Select All and Copy

Edit > Select All and Edit > Copy had no handlers: Select All was enabled after the
first file but did nothing, and Copy was never enabled. On Windows both come with the
report view. Now, as there:
- **Select All** selects the whole report. It also puts the focus in the report, so the
  selection shows as active and Shift with the arrow keys can change it.
- **Copy** copies what is selected in the report, and is enabled only while something
  is.
- **Shortcuts:** Cmd+A and Cmd+C work wherever the focus is in the main window.
- **When Select All is on:** whenever the report has any text, as on Windows. Before
  any file the report is empty, so it is off.

The report is now a small class of its own, `CReportView` (`source/reportview.cpp`).

**Found and fixed: the report could be changed.** It was editable; on Windows it is
read-only.
- **Typing:** after Cmd+A, one key replaced the whole report (typing "x" left a
  one-character report). Cmd+V pasted into it, and Delete removed text.
- **Undo:** the report kept an undo step for each line the decoder added (1,010 for a
  16 KB report), so Cmd+Z removed the end of the report.
- **Saving:** Save Log and Print wrote the changed text.

It is now read-only and keeps no undo history. The caret and keyboard selection stay.

**Found and fixed: copying in Dark Mode.** Copy puts the selection on the clipboard as
plain text and as HTML, for apps that paste formatted text.
- **Colours:** the HTML had the colours on screen. In Dark Mode those are pale, made
  for a dark background: against a white page they give 1.8–3.0:1 (headers 2.6:1,
  warnings 2.3:1, OK lines 1.8:1). The HTML now always has the colours for a light
  background, the ones Windows uses (8.6:1 for headers). The report on screen keeps
  its own colours.
- **Font:** Qt's HTML named no font, so pasted text took the app's own, usually
  proportional, and the report's columns no longer lined up. It now names the report's
  font (Courier), as the RTF Windows copies does.
- **Formats:** Qt also offered Markdown and OpenDocument versions; the copy now has
  only plain text and HTML.

**Second check:** a further review found one case missed. Select All came on only once
a file opened, so when the first file was empty or couldn't be opened, the report held
the error but Select All stayed off, and Select All then Copy did nothing. It now
follows the report's text, as above. Two cases cover it: an empty first file and a
missing one.

**Tests:** 21 new `T` cases (279 in all). `gui --edit STEPS` chooses the Edit items,
presses keys, types and clicks in the report through Qt's shortcut handling (QtTest),
and prints after each step:
- how much of the report is selected, and which Edit items are enabled;
- its undo history, and where the focus is;
- whether the report changed, and what the clipboard holds;
- after a copy, the clipboard's formats and the colours and fonts in its HTML.

On the old code 16 fail. The other five check behaviour that already worked: both items
off before a file, Copy off with nothing selected, a click clearing the selection, Cmd+A
and Cmd+C typed in the focused report, and the next file starting with nothing selected.
Before the second check's fix, only its two new cases fail.

**Checks:**
- **All 279 pass** with and without ASan/UBSan.
- **Same as Qt's copy:** for a report with every kind of line, the text is the same as
  Qt's own copy, and the HTML differs only by the font.
- **Your clipboard is safe:** the tests' clipboard is the offscreen platform's own,
  inside the test process (the platform has no macOS pasteboard code), so running them
  never touches the Mac's clipboard.
- **Not checked:**
  - with the macOS platform: the Edit menu, Cmd+A and Cmd+C through the menu bar, and
    pasting into other apps;
  - Cmd+A and Cmd+C while the image window is in front. Offscreen, the main window's
    shortcuts don't reach that window.

**Still open:** Developer ID signing and notarization (M5); latent code; Find, File
Overlay and Batch Process.

### Find

Edit > Find and Find Next did nothing: Find was enabled once a file opened, and Find
Next never was. On Windows both come with the report view, from MFC's rich edit view
(MFC 11: the Windows version is built with Visual Studio 2012). Now, as there:
- **Find** opens the Find dialog: Find what, Match whole word only, Match case, Up or
  Down, Find Next and Cancel. It stays open while searching.
  - **Its text:** the selected text if that is on one line, otherwise the last text
    searched for. The options are the last search's.
  - **Already open:** Find only brings it to the front.
- **Find Next** in the dialog (or Return) selects the next match and scrolls to it.
  Edit > Find Next repeats the last search, also with the dialog closed.
- **Where a search starts:** going down, at the character after the selection's start,
  or at the caret. Going up, it finds the last match that starts before that point.
  (Going up, MFC hands Rich Edit a starting point whose exact effect isn't documented;
  the port mirrors the rule for going down.)
- **Going round:** the first search after Find (or after one that found nothing) goes
  on from the other end of the report if it finds nothing, as far as where it started.
  After that, reaching the end stops the search once, and the next one goes on from the
  top (from the bottom, going up). Loading a file leaves the caret at the end of the
  report, so the first search finds the first match.
- **The dialog moves out of the way:** when it covers the text found, it moves above it,
  or below it if there isn't room above.
- **When they're on:** Find whenever the report has text, like Select All. Find Next
  when the report has text and there has been a search. The last search lasts until the
  app quits.
- **Shortcuts:** Cmd+F and Cmd+G.

The search is `CReportView::findText`, with the same rules as MFC 11's
`CRichEditView::FindTextSimple`. That code is the same from MFC 10 through 14.24; MFC 9
used different rules. The dialog is `CFindDialog` (`source/finddialog.cpp`).

**Changes from Windows:**
- **Status bar:** when nothing is found, Windows only beeps. The port also beeps, and
  says why in the status bar: `Cannot find "X"`, or that it reached the end (or start)
  of the report and Find Next goes on from the top (or bottom). The next search that
  finds something clears it.
- **A new report starts a new search.** On Windows a search under way carried on into
  the next file's report: if it had found something, a search that found nothing after
  the caret stopped there, and only the one after went round.

**Tests:** 36 new `FN` cases (315 in all). New `gui --edit` steps:
- choose Find and Find Next, or press Cmd+F and Cmd+G;
- fill in and click the dialog's fields and buttons, and press Return or Escape;
- select text in the report, or put the caret on a line;
- move the dialog over the selection, or the main window to the top of the screen.

After each step a `harness: find` line prints:
- which Find items are enabled, and the dialog's fields;
- the selection, and which match of the dialog's text it is;
- where the dialog is (over the selection, above or below it), and the status bar's
  message.

On the old code 35 fail. The other one checks that Find is off before a file, which it
already was. Three deliberate breaks each fail their own cases:
- **No new search for a new report:** the next-file case fails.
- **The dialog not moving:** both moving cases fail.
- **No going round:** 12 cases fail.

**Checks:**
- **All 315 pass** with and without ASan/UBSan.
- **Layout:** rendered offscreen, the dialog has the Windows Find dialog's layout.
- **Not checked:**
  - with the macOS platform: the dialog's look, Cmd+F and Cmd+G through the menu bar,
    and the beep;
  - Cmd+G while the Find dialog is in front. Offscreen, the main window's shortcuts
    don't reach the dialog; on macOS the menu bar should handle it.

**Still open:** Developer ID signing and notarization (M5); latent code; File Overlay
and Batch Process.

### File Overlay

Tools > File Overlay did nothing. On Windows it sets a run of bytes to decode in place
of the file's own at an offset, to try out a change without editing the file. The
buffer code for it was already in the port; only the dialog and its handler were
missing. Now, as there:
- **The dialog** ("File Overlay Test"): Overlay Enabled?, File Offset (hex) with Load
  Original, the file's first 8 bytes there in hex and binary, the new value (hex) and
  Len (bytes).
- **OK or Apply** puts the overlay in place, or removes it with Overlay Enabled off, and
  reprocesses the file. Apply leaves the dialog open; Cancel changes nothing more.
- **Reopened,** the dialog shows the overlay in place.
- **Reprocess keeps it,** and the scan section lists it, as on Windows.
- **Export JPEG's Enable Overlays,** disabled until now because nothing could add an
  overlay, is available again. With it, the export has the overlaid bytes; without it,
  the file's own (see the second check below).

The dialog is `COverlayDialog` (`source/overlaydialog.cpp`).

**Found and fixed** (all as on Windows 1.8.0):
- **A second overlay instead of a new one:** each OK or Apply with the overlay on added
  another. One made earlier at another offset stayed in place, the report listed both,
  and the dialog showed the first. There is now one overlay, replaced each time.
- **The next file was overlaid too:** nothing removed an overlay when another file was
  opened, so that file was decoded with those bytes replaced. Opening a file now
  removes it.
- **Bytes from past the value:** the value went into a 16-byte array, so a longer one
  overran it, and a Len longer than the value overlaid whatever followed it in memory.
  Fields that can't be used are now refused with a message saying why:
  - an offset that isn't hex (Windows read it as 0), or is past the end of the file;
  - a value that isn't whole hex bytes, or is over 499 bytes, the most an overlay
    holds;
  - a Len not from 1 to the value's length;
  - an overlay that runs past the end of the file.

**Changes from Windows:**
- **The file's bytes** follow the offset as it is typed. On Windows they stayed those of
  the last offset loaded until Load Original was pressed; that button is still there.
- **Len** follows the number of bytes typed, and can then be made shorter. On Windows
  it had to be typed as well.
- **The report says where:** the scan section's overlay line now gives the offset. The
  report's header also gets a line, `Overlay: [2] bytes replaced at offset
  [0x0000000B]`, since with Decode Image off there is no scan section, and nothing said
  the decode used other bytes.

**Unchanged from Windows:** reads of 2 or 4 bytes at once (`CwindowBuf::BufX`) don't
see overlays. The JPEG and EXIF decoding reads a byte at a time, so it does; an AVI's
RIFF headers and most of a Photoshop file's fields don't.

**Tests:** 44 new `OV` cases (359 in all). `gui --overlay STEPS` answers the dialog (on
or off, offset, value, Len, Load Original, Apply, OK, Cancel) and prints:
- the dialog's fields, including the file's bytes it shows;
- a summary of the report after each step that changes it: APP0's version, which the
  tests overlay, the overlays the scan section lists, and the header's line;
- the same summary after File > Reprocess, and for each file opened.

`export-jpeg` takes the same steps and `--overlays`, and the tests compare the export's
MD5 with that of the file with those bytes changed. Afterwards it reprocesses and prints
the summary again, to show the overlay is still in place.

On the old code 35 fail. The other nine check what held because nothing could add an
overlay: no change on Cancel or with it off, the next file, exports without overlays,
the source file kept, and the menu off after a failed first open. Three deliberate
breaks each fail their own cases:
- **Adding overlays (as Windows):** the case for a second Apply fails.
- **Keeping it for the next file:** the next-file case fails.
- **No header line:** 7 cases fail.

**Second check:** a further review found that an export without Enable Overlays still
analyzed the overlaid bytes. Export JPEG analyzes the file again to find the JPEG's
markers (and Extract All searches for each SOI), and that read through the overlay,
while the copy took the file's own bytes. With FFD9 overlaid at 0x2A1, in the scan, the
export reported success but wrote the file's first 675 bytes of 1,380, without an EOI;
Extract All did the same. Windows has the same problem. Now, without Enable Overlays,
the overlay is set aside for the export and put back after it:
- **The export** is the whole file, as it is on disk.
- **The report** after the export is of the file without the overlay, and ends with a
  note saying so.
- **The overlay** is still in place: the next Reprocess shows it.

With Enable Overlays the export still ends at the overlaid EOI, since that is the file
it was asked for. Seven cases cover this: both kinds of export, with and without
overlays; the note; and the overlay kept after each kind. With the set-aside switched
off, the two exports without overlays and the note fail.

**Third check:** the note said "the file was analyzed and exported" even when the export
failed. Exporting to a folder that doesn't exist reported the write error and wrote
nothing, then claimed the export. The note now says "exported" only when the export
succeeded, and otherwise "the file was analyzed as it is on disk". A failed Extract All
gets the second wording too, though it may have written some files before the error: the
lines above the note list each one. Three cases cover a failed export: the note for each
kind, and the overlay kept. With the old wording, the two note cases fail.

**Checks:**
- **All 359 pass** with and without ASan/UBSan, including a 499-byte overlay of zeros
  over the file's tables and frame header.
- **Not checked:** the dialog on the macOS platform.

**Still open:** Developer ID signing and notarization (M5); latent code; Batch Process.

### Batch Process

File > Batch Process wasn't in any menu: its action was made but never connected. On
Windows it analyzes each image in a folder and saves each report as a log. Now, as there:
- **The menu item** is in the File menu after Reprocess File, and is on with no file
  open.
- **The dialog** ("Batch Processing") has two notes, the input image folder and the
  output log folder (each with Choose...), Process subfolders? and Extract all JPEGs.
  It starts with the last folders used, which are saved with the settings. Choosing the
  input folder makes it the output folder too.
- **Each file's log** is its path below the input folder, in the output folder, with
  `.txt` added. Subfolders are made as needed. Without Extract All, only `.jpg` and
  `.jpeg` files are processed, in any case.
- **With Extract All,** every file is processed, and each JPEG in it is exported to
  `<its path>.export.000001.jpg`, `.000002.jpg`, ..., as Tools > Export JPEG's Extract
  All does.
- **The report** then lists what was done: the settings, master's line for each file
  (`Batch processing file      1 of      4 ( 25.00 %): [...]`), the log written or the
  error, and the totals. A message box says it is done.

The batch has its own decoders (`CBatchProcess`, `source/batchprocess.cpp`), as master's
had its own `CJPEGsnoopCore`. So the file shown keeps its image, start offset and File
Overlay, which isn't applied to the batch's files, and File > Reprocess shows its report
again. The dialog is `CBatchDialog` (`source/batchdialog.cpp`). Extract All's loop is now
`extractAllJpegs()`, shared with Export JPEG, and the report's header lines are shared
too.

**Found and fixed** (all as on Windows 1.8.0):
- **Files replaced without asking:** logs and numbered exports already there were
  overwritten. The batch now asks first, naming one of them and how many there are.
- **Links to folders were followed:** one leading back to a parent folder made the batch
  process the same files again at each level below it, until the paths got too long (from
  the code). Links to folders are now skipped; links to files are processed.
- **The output folder was searched:** when it was inside the input folder, each batch
  with subfolders and Extract All processed the last one's logs and exports, a level
  deeper each time. It is now left out, unless it is the input folder itself (the
  default).
- **Logs were written through links:** a log's name that was a link to another file,
  symbolic or hard, put the log into that file, even when it was one of the images being
  processed (master opened each log to write in place, from the code). Each log now
  replaces the name itself and nothing else: a symbolic link there is removed, and the
  log is written beside it and renamed into place, which replaces a hard link rather
  than the file it shares. A failed log leaves the old one. Extract All's numbered
  exports remove a symbolic link the same way (they were already renamed into place). A
  link that leads nowhere is now in the Replace question too.
- **Folders that couldn't be read looked empty:** master's search found nothing in them,
  so their files were left out without a word, and an input folder it couldn't read had
  nothing to process (from the code). The dialog now refuses an input folder it can't
  read and says why. A subfolder it can't read is listed in the report with the reason,
  counted ("Folders not read"), and named in the final message. When no files are left
  to process, the message says that the folders it couldn't read were left out. Hidden
  folders aren't searched, so a disk's unreadable `.Trashes` and the like aren't errors.
- **Motion JPEG frames couldn't be opened:** Extract All exported them without the
  Huffman tables they need. They now get the standard tables, as Export JPEG does by
  default.
- **Errors weren't reported:**
  - A file that couldn't be opened was skipped without a word.
  - A log that couldn't be written showed a message box for each file.

  Both are now listed in the report and counted in the final message.
- **The folders weren't checked:** with an input folder that didn't exist, the batch
  found nothing and said "Batch Processing Complete!". The dialog now refuses a missing
  input folder, a file, an empty field or a relative path, and says why. A folder with
  nothing to process, and an output folder that can't be made, are reported before
  anything is written.
- **It couldn't be stopped:** there was only a wait cursor. A progress dialog now shows
  the file and the count, and Cancel (or closing the main window) stops the batch after
  the file it is on.

**Changes from Windows:**
- **Hidden files and folders** (names starting with a dot) are left out. On some disks
  macOS puts a `._name.jpg` file beside each image, which isn't an image.
- **The logs are UTF-8,** as File > Save Log writes. Master wrote them in the Windows
  code page, with characters it lacks changed to "?". Each log is exactly the report
  File > Open shows.
- **Wording:** "folder" for "directory", as macOS says, and "Choose..." for "...". Note 2
  names the menu item: Options > Scan Segment > Decode Image.
- **"~"** in a folder field is the home folder.
- **The report** lists the logs written, the files exported and any errors, with totals.
  Master's had only its line for each file.

**Unchanged from Windows:**
- **The options:** every file is decoded with the current options (Decode Image, Full
  IDCT and so on), from its start.
- **Extract All's settings** are Export JPEG's defaults, with no way to change them.
- **No command line:** master's `-b` and `-br` options aren't ported.

**Tests:** 95 new `BP` cases (454 in all). The driver's `batch` mode copies a folder that
`gen_cases.py` makes, answers the dialog, and prints each file the batch writes and its
MD5, a list of them, and the report. The cases cover:
- **The dialog:** the menu item, the fields and their checks, and Choose....
- **The files processed:** extensions in capitals, subfolders, a folder named like an
  image, hidden files, and a link to a parent folder.
- **The logs:** each one the report File > Open shows (7 compared), in UTF-8, and an
  empty file's.
- **Extract All:** two JPEGs in one file, a Motion JPEG AVI, and the output folder inside
  the input folder.
- **The Replace question,** and the errors: an unreadable file, a log that can't be
  written, and an output folder that can't be made.
- **Links among the names it writes:** a log or export named like a symbolic or hard
  link to an image, to a file that isn't processed, or to nothing. Each one leaves the
  linked file as it was.
- **Folders it can't read:** the input folder, a subfolder, one deeper down, one it can
  read but not search, a hidden one, and one holding the only images.
- **Stopping:** Cancel, closing the window, and message boxes on again afterwards.
- **The file shown,** kept with its File Overlay.

On the previous commit 86 fail. The other nine check what held because no batch could
run: nothing written on Cancel or without Replace, hidden files, the output folder, the
sources kept, the file shown and its overlay, and message boxes afterwards. Nine
deliberate breaks each fail their own cases:
- **Listing hidden files:** 22 cases fail.
- **Searching the output folder:** the output-folder case fails.
- **No tables for Motion JPEG:** the Motion JPEG case fails.
- **Leaving message boxes off:** 5 cases fail.
- **Writing the log through a link** (as before the fix): 6 cases fail.
- **Writing an export through a link:** the export case fails.
- **Not checking subfolders:** 7 cases fail.
- **Not checking the input folder:** its case fails.
- **Not asking about links that lead nowhere:** 2 cases fail.

The hard-linked export passes without the fix too: it was already renamed into place.

**Checks:**
- **All 454 pass** with and without ASan/UBSan, including every Export JPEG case on the
  shared Extract All loop.
- **The layout:** the dialog and the progress were rendered offscreen and looked at.
- **Not checked:** the dialogs on the macOS platform, including the native folder
  chooser, and a large folder.

**Still open:** Developer ID signing and notarization (M5); latent code; the other menu
items not yet ported, such as File > Offset, Tools > Image Search, Lookup MCU Offset,
Search Executable for DQT, and the signature database items.

### File > Offset

File > Offset did nothing: the menu item had no handler. On Windows it decodes the file
from an offset, such as where a JPEG embedded in it starts. Now, as there:
- **The menu item** ("Offset...") is in the File menu, between separators after Batch
  Process.
- **The dialog** ("Starting File Offset") has Offset (Bytes), Hex or Decimal, and the
  offset. It opens in hex with the offset in use (`0x00000000` for a file just opened),
  selected so typing replaces it. Hex and Decimal change the offset typed to that base.
- **OK decodes the file from the offset:**
  - the report starts there ("Start Offset: 0x0000062c");
  - the preview shows the image found there;
  - the signature section gives the offset, and the assessment is skipped, as master
    does for any offset but 0.
- **The offset applies until another file is opened,** which starts from 0 again:
  - Reprocess keeps it;
  - Export JPEG exports the JPEG at the offset, and its dialog shows it;
  - a File Overlay stays in place;
  - Batch Process decodes its files from their start, and leaves it in place.

The dialog is `COffsetDialog` (`source/offsetdialog.cpp`).

**Found and fixed** (all as on Windows 1.8.0):
- **The scan at the new offset wasn't decoded:** master decoded the file again and only
  then marked the image source changed. The scan decoder skips a scan when the source
  is unchanged, so once a scan had been decoded (as when the file starts with a JPEG),
  the decode from the new offset skipped its scan. The report lacked the scan's
  decode, and the preview kept the image from before (from the code); the next
  Reprocess put it right. The source is now marked changed first. With master's order,
  the port's report from the new offset has no scan decode and no preview.
- **Hex or Decimal lost the offset typed:** with text that wasn't a number in its base,
  master said "Invalid hex string" (or decimal) and changed the offset to 0. Its code
  noted that it should keep the choice instead. Now the choice goes back and the text
  stays, with a message saying why. Text that is a number in the base clicked, such as
  0x62C typed with Decimal chosen, is kept as it is.
- **Offsets that can't be used:** master took an empty field as 0, and a number over 32
  bits as 0xFFFFFFFF (from the code). It decoded past the end of the file as if 0x00
  bytes were there: the report said "Expected marker 0xFF, got 0x00 @ offset
  0x00001388" for a 2,747-byte file. OK now refuses each of these, and says why:
  - an empty field;
  - text that isn't a number in the base chosen;
  - an offset at or past the end of the file (the message gives its size).

  The dialog stays open.
- **The menu item was always on:** with no file analyzed it did nothing, and its code
  noted that it should be disabled. It is now on once a file has been opened, like the
  Tools items. After a file that couldn't be opened, or was empty, it says why.

**Changes from Windows:** spaces around the offset are ignored (master refused them).

**Unchanged from Windows:** the dialog always opens in hex, and the offset isn't saved
with the settings.

**Also fixed:** `CimgDecode`'s constructor called `Reset()`, which reads
`m_bScanErrorsDisable`, before `ResetState()` set it, as in master. When the flag read as
set, `Reset()` also left the count of scan warnings unset. UBSan reported a load of 254
in a decoder on the stack: the test driver's separate decode, for `ref=`. The app's
decoders are on the heap, and no test had seen it there. The constructor now sets the
flag, the count and the restart interval (copied by `Reset()` before a SOF sets it)
first. Without this, 3 cases fail under UBSan.

**Tests:** 39 new cases, 37 `FO` and 2 `BP` (493 in all). `gui --offset STEPS` prints
the menu item's place. It answers the dialog with these steps: Hex, Decimal, text
typed, OK, Cancel, Return and Escape. It prints:
- the dialog's fields: base, text, focus and selection;
- a summary of the report and preview: the start offset, whether an SOI was found
  there and the scan decoded, the signature section's offset and the assessment;
- the preview's size and MD5, with `ref=same` when it is the image a separate decode
  from that offset gives.

The summary is printed for each file, after each OK, and after File > Reprocess. The
batch mode takes `--offset` for the file shown. The cases cover:
- **The menu item:** off with no file or after a failed first open, and the message
  after a missing or empty file.
- **The offset:** in hex, with or without 0x, or in decimal, and with spaces around it.
- **The keys:** OK, Return, Cancel and Escape.
- **Two JPEGs in one file:** each one decoded, and the last byte.
- **The scan decoded first:** a file that starts with a JPEG.
- **Hex and Decimal:** switching both ways, and refusals.
- **Each refused offset.**
- **Where the offset applies:** kept by Reprocess and Export JPEG, reset by the next
  file, alongside a File Overlay, and with Batch Process.

On the previous commit 37 fail. The other two check what held because the offset never
changed: the next file starts from 0, and a batch decodes its files from their start.
Six deliberate breaks each fail their own cases:
- **Master's order** (the source marked changed after the decode): the case with a
  scan decoded first fails.
- **Not starting the next file from 0:** the next-file case fails.
- **The menu item always on:** the two cases with no file fail.
- **Not checking that the file was analyzed:** the missing and empty file cases fail.
- **Not checking the file's size:** the 3 cases past the end fail.
- **Master's Hex and Decimal:** 4 cases fail.

A seventh break, not selecting the offset when the dialog opens, failed nothing. A
dialog selects its first field's text when it shows, as tabbing into it does, so the
call that did it was taken out.

**Checks:**
- **All 493 pass** with and without ASan/UBSan.
- **The layout:** the dialog was rendered offscreen at 1x and 2x and looked at.
- **Not checked:**
  - the dialog on the macOS platform;
  - focus there after a message box (the offscreen platform doesn't activate the
    dialog again).

**Still open:** Developer ID signing and notarization (M5); latent code; the other menu
items not yet ported, such as Tools > Image Search Fwd and Rev (which also set the
offset), Lookup MCU Offset, Search Executable for DQT, and the signature database
items.

### Tools > Image Search Fwd and Rev

Tools > Image Search Fwd and Rev did nothing: the menu items had no handlers. On Windows
they step through the JPEGs in a file, such as a video's frames or the previews in a
camera's raw file. Now, as there:
- **The menu items** are the first two in the Tools menu. Their shortcuts are Ctrl+2
  (Fwd) and Ctrl+1 (Rev), which are Cmd+2 and Cmd+1 on macOS. Their status tips are
  "Search for next Image" and "Search for previous Image".
- **Fwd** finds the next SOI marker (FF D8 FF) after the start offset and decodes the
  file from there, as File > Offset does. If there is none, it says so and the offset
  stays.
- **Rev** finds the one before the start offset. If there is none, it says so and
  decodes the file from its start, where the file was decoded from when opened.
- **The search sees the File Overlay,** as the decoder does: an overlay can hide an
  SOI or make one.
- **The offset found applies as File > Offset's does:** Reprocess and Export JPEG keep
  it, and opening a file starts from 0 again.
- **The menu items are on once a file has been opened.** After a file that couldn't be
  opened, or was empty, they say why.
- **A file moved, removed or emptied since it was opened:** the report says so, as
  after Reprocess, and the preview of what was there goes.

**Found and fixed** (both as on Windows 1.8.0):
- **The search was slow:** master compared each position through `Buf()` three times,
  about 60 MB/s here. Searching 300 MB took about 5 s, and the window doesn't respond
  while it runs, with no way to stop it. To the end of a 2 GB video, that is about half
  a minute. `CwindowBuf::BufSearch` now reads the file in 64 KiB blocks with the
  overlay applied, as `Buf()` reads it, and compares the bytes in memory. The same
  search takes about 0.6 s (measured with hyperfine against opening the file alone).
  Extract All (in Export JPEG and Batch Process) uses the same search. A scratch
  program compared it with master's loop:
  - 2.4 million searches on random files, with overlays and with searches across the
    block edges;
  - the same position every time, found or not.
- **Rev at the start of the file did nothing,** with no message. It now says there is
  nothing before it to search.

**Changes from Windows:** the messages name the offset searched from ("No JPEG found
after offset 0x0000062C (no SOI marker, FF D8 FF)."), and Rev's says that the file is
decoded from its start. Master showed that message before decoding again, so the report
behind it still showed the old offset; it now comes after.

**Unchanged from Windows:** Rev with no JPEG before the offset goes back to the start
of the file, even when there is no JPEG there.

**The shortcuts with the image window in front:** the image window is a window of its
own, and it comes to the front when it first shows an image. On macOS the main window's
menu bar stays in place while it is in front, and the menu bar takes Cmd+2 and Cmd+1
before any window does (from Qt's Cocoa code), so holding Cmd+2 steps through a video's
frames. Offscreen, the main window's shortcuts work only with that window in front, so
the test driver brings it there first, as a click on it would.

**Tests:** 31 new `IS` cases (524 in all). `gui --search STEPS` prints the menu items'
places and shortcuts. Its steps choose Fwd and Rev, press Ctrl+2 and Ctrl+1, or move
the file away for the next step. It prints `--offset`'s summary of the report and
preview after each step and after File > Reprocess. The cases cover:
- **The menu items:** their place, tips and shortcuts; off with no file or after a
  failed first open; and the message after a missing or empty file.
- **Fwd:** each JPEG in a file in turn, then none (the message, and the offset stays);
  from an offset File > Offset set; and with the scan at the start decoded first.
- **Rev:** the JPEG before; none (back to the start, and the message); at the start;
  a JPEG at offset 0; and from an offset.
- **The shortcuts.**
- **Where the offset applies:** Reprocess, Export JPEG and the next file.
- **The File Overlay:** hiding an SOI, and making one, found both ways.
- **A file moved away** since it was opened.
- **A file over 128 KiB,** with a JPEG whose SOI runs across a block edge.

On the previous commit 28 fail. The other three check what held because nothing moved:
the menu items are off with no file or after a failed first open, and the next file
starts from 0. Nine deliberate breaks each fail their own cases:
- **The menu items always on:** the two cases with no file fail.
- **No shortcut for Fwd:** 3 cases fail.
- **Not checking that the file was analyzed:** the missing and empty file cases fail.
- **Master's Rev at the start** (nothing): 1 case fails.
- **Keeping the preview when the file has gone:** 1 case fails.
- **Rev staying put when it finds nothing:** 1 case fails.
- **Fwd from the start offset itself, not after it:** 12 cases fail.
- **Blocks without the bytes after their last position:** the block-edge case fails.
- **A search that doesn't see the overlay:** the 3 overlay cases fail.

**Checks:**
- **All 524 pass** with and without ASan/UBSan.
- **Not checked:**
  - on the macOS platform: the shortcuts with the image window in front, the wait cursor
    and the status bar's "Searching forward..." during a long search;
  - a real video's frames.

**Still open:** Developer ID signing and notarization (M5); latent code; the other menu
items not yet ported, such as Lookup MCU Offset, Search Executable for DQT, and the
signature database items.

### Tools > Lookup MCU Offset

Tools > Lookup MCU Offset did nothing: the menu item had no handler. On Windows it says
where in the file the MCU holding a pixel starts, for tracing damage in an image back to
the file's bytes (to change them with File Overlay, say). Now, as there:
- **The menu item** comes after Image Search Rev and a separator. Its status tip is
  "Calculate file offset for an MCU".
- **The dialog,** "Lookup File Offset of MCU", takes a pixel's X and Y, with their
  ranges above them. Recalc shows where the scan data of the MCU holding that pixel
  starts in the file, as a hex byte offset and a bit ("0x0000026F : 0").
- **It reads the decoder's map** of where each MCU started, made when the scan was
  decoded, so the file isn't read again. The offsets are the file's: after File > Offset
  or Image Search they include the offset decoded from. The dialog is modal, so the
  image can't change while it is open.
- **The menu item is on once a file has been opened.** After a file that couldn't be
  opened, or was empty, it says why.

**Found and fixed** (all as on Windows 1.8.0):
- **With no scan decoded,** Windows still showed the dialog, with ranges of 0 to
  4294967295 (0 − 1), and Recalc read a map that wasn't there. That happens with
  Options > Scan Segment > Decode Image off, a progressive JPEG, a Photoshop file's
  image, or a file with no image. It now says that no image was decoded, and names the
  option when that is off.
- **After a restart marker** (DRI), each MCU that starts a restart interval was given
  the position where the previous MCU's bits ended, before the padding and the RSTn
  marker: 0x000000B1 : 4 for an MCU that starts at 0x000000B4 : 0. The decoder deals
  with the marker when it reads the MCU's first code, and the position was taken before
  that. It is now taken when the first code is read, as Detailed Decode's positions
  already were. The image window's status bar and the report's clipping notes ("Y
  Overflow @ Offset"), which use the same map, were wrong in the same way.
- **More than 256 MB into a file** the offsets wrapped around: the map kept them in
  32 bits, 28 of them for the byte. For a JPEG 0x10000100 into a video, an MCU at
  0x1000019F showed as 0x0000019F. The map now holds 64 bits per MCU.
- **When the scan stops early** (in a truncated or corrupt file), the MCUs the decoder
  didn't reach showed 0x00000000 : 0. The first MCU of each later row showed where the
  decoder was after the data ran out: past the end of the file, at the EOI marker, or in
  the 0xFF fill bytes that may come before a marker (T.81 B.1.1.2). They now show "Not
  decoded", and so does the image window's status bar ("File: not decoded"). The MCU in
  which the data ran out keeps its offset. The scan's data ends at the end of the file,
  or at its first marker other than RSTn, less the fill bytes before it. A 0xFF in scan
  data is followed by 0x00, so the 0xFF bytes just before a marker can only be fill.
- **At the end of a file of 0xFFFFFFFF bytes,** the largest the app opens, the decoder's
  position after the end wrapped around to 0. It then decoded the following rows from
  the start of the file, so the preview showed that data and the lookup gave offsets
  such as 0x00000001 : 4. The position now stops at the end.

**Changes from Windows:**
- **The dialog shows the MCU** the pixel is in, such as [1,1]: the coordinates Detailed
  Decode takes.
- **Return calculates.** On Windows, OK was the default button, so Return closed the
  dialog without calculating. OK and Cancel, which both only closed it, are now one Close
  button.
- **Changing X or Y clears the result.** On Windows it stayed until the next Recalc, next
  to the new coordinates.
- **The messages:** "X must be a whole number from 0 to 39." Windows said "Pixel X
  coord out of range". For text that wasn't a number, MFC said so and Windows then
  calculated from the last number.
- **The results can be selected and copied.**

**Unchanged from Windows:** the ranges are the decoded image's size, padded to whole
MCUs as the preview is: a 36 × 20 image takes X 0..39 and Y 0..23. A pixel in the
padding is in the last MCU of its row or column.

**Tests:** 63 new `LM` cases (587 in all). `gui --lookup STEPS` chooses the menu item
and answers its dialog: it types X and Y, clicks Recalc or Close, and presses Return or
Escape. It prints the dialog's fields each time. `--hover X,Y` moves the mouse over the
preview and prints the image window's status bar. The test files come from
`dc_only_jpeg()`, which records where it writes each MCU and can now write restart
markers, with fill bytes before them. So every offset is checked against the file as
written. The cases cover:
- **The menu item:** its place and tip; off with no file; and the message after a
  missing or empty file.
- **No scan decoded:** Decode Image off, a progressive JPEG, a Photoshop file, and a
  progressive JPEG after a decoded one.
- **The dialog:** its fields when it opens, and the ranges.
- **The offsets:**
  - MCUs at the start, middle and end of rows, and at a bit other than 0;
  - a pixel in the padding;
  - MCUs of 8×8, 16×16 and 16×8 pixels.
- **Restart markers:** the MCUs after RST0 and RST1, one starting a row, and one between
  markers. Also with Detailed Decode on, which decodes its MCUs separately, and with fill
  bytes before each marker.
- **A scan cut short:** with no EOI after the cut, with one, and with fill bytes before
  it. The cases check the MCU the scan ends in, the one after, and the first of the next
  row, and the image window's status bar.
- **Offsets in the file:**
  - after File > Offset;
  - more than 256 MB in, and a scan cut short at the end of a file of 0xFFFFFFFF bytes
    (both sparse files);
  - in the second file opened.
- **What can be typed:**
  - rejected: out of range, not a number, empty, negative, too large for 64 bits, and
    hex;
  - accepted: spaces around the number, and the largest X and Y.
- **The dialog's buttons and keys:**
  - Return, Escape and Close;
  - a changed field clearing the result;
  - a fresh dialog each time.

On the previous commit 62 fail. The other checks that the menu item is off with no file,
which it already was. Thirteen deliberate breaks each fail their own cases:
- **Recording each MCU's position as before,** where its decode starts: 13 cases fail.
  These are the restart-marker cases (with and without fill bytes), the Detailed
  Decode one, and the cases for MCUs the decoder goes on to after a cut.
- **Not checking where the scan data ends:** 7 cases fail. These are the MCUs at the EOI
  marker or in its fill bytes, and the first MCU of the next row in each cut file.
- **MCUs not reached left at 0,** as before: 9 cases fail.
- **32-bit map entries,** as before: the 3 cases past 256 MB fail.
- **Ending the scan data at the marker, not its fill bytes:** the 2 fill-byte cases for
  MCUs after the cut fail.
- **The position after the end wrapping around:** the 2 cases for the next row in the
  0xFFFFFFFF-byte file fail.
- **Not recording positions in Detailed Decode's decode:** 1 case fails.
- **Not checking that a scan was decoded:** 4 cases fail.
- **Return doing nothing:** 1 case fails.
- **Return closing the dialog,** as on Windows: 2 cases fail.
- **Not clearing the result when a field changes:** 1 case fails.
- **No range check:** 3 cases fail.
- **The image window ignoring "not decoded":** 2 cases fail.

**Checks:**
- **All 587 pass** with and without ASan/UBSan.
- **The app builds** with no new warnings.
- **Not checked:**
  - on the macOS platform: the dialog's look, and Return in it;
  - a real camera file with restart markers, and a real video over 256 MB.

**Still open:**
- Developer ID signing and notarization (M5); latent code.
- The menu items not yet ported: Search Executable for DQT and the signature database
  items.
- **Fill bytes before an RST marker** (found while testing): the report's marker list
  takes the first 0xFF of the fill as the end of the scan. It then says "Restart
  marker [0xFFd0] detected outside scan". The scan's decode, and the lookup, are right.

### Tools > Search Executable for DQT

Tools > Search Executable for DQT did nothing: the menu item had no handler. On Windows
it looks in another file, such as a camera maker's program, for quantization tables. A
program that writes JPEGs often keeps its tables in its code, so finding one there
suggests which program wrote an image. Now, as there:
- **The menu item** comes after File Overlay. Its status tip is "Find DQT Table in
  Executable", and it needs no file open.
- **It asks for a file** ("Open Executable file for Search"). It searches it for the
  JPEG standard luminance table (T.81 Table K.1) and, when a file has been analyzed,
  that file's luminance table (DQT table 0).
- **Each table is searched for in ten forms:**
  - in zigzag order, as a DQT segment has it ("pre-zigzag"), and in row order
    ("post-zigzag");
  - with 1-byte entries, and with 2- and 4-byte entries in big-endian and little-endian
    order ("endian byteswap"). The 1-byte form is searched only for a table whose
    entries all fit in a byte.
- **Each place found is listed** at the end of the report ("Found @ 0x00000A1B"), under
  its table and form. Reprocess clears the list, as on Windows.
- **A table of one value** (such as the 1s of quality 100) isn't searched for, as it
  would match any run of that byte. The report says so.

**Found and fixed** (all as on Windows 1.8.0):
- **Tables at the start and end of the file weren't found.** The search never compared
  offset 0, and it stopped 2 × length − 1 bytes before the end: 127 to 511 bytes,
  depending on the entry size.
- **The search was slow.** It compared one byte at a time through `Buf()`, in 20 passes
  over the file, and the window doesn't respond while it runs. `CwindowBuf::BufSearchX`
  now reads the file in 64 KiB blocks and searches them in memory. Searching a 64 MiB
  file for both tables took about 4.5 s and now takes about 0.2 s (measured with
  hyperfine against choosing the menu item and cancelling).
- **A table with entries over 255 skipped the standard table's 1-byte search too,**
  though the standard table has a 1-byte form.
- **The note about a table of one value was garbled.** Its format string had `_T(` in
  it, so it read "...constant nValue (0x01),_T(" and the next line started with ")".
- **A file shown without a luminance table** (a Photoshop file, say) was taken to have a
  table of 0s, and the report called it a constant table (0x00). It now says that the
  file has no luminance table, so only the standard one is searched for.
- **An empty file** made the file buffer show two errors: "BufFileSet() File length
  zero", then "BufLoadWindow() and no file open". It now says that the file is empty.

**Changes from Windows:**
- **The file dialog** shows all files first, since macOS programs have no extension.
  Then come Executable (.exe), DLL Library (.dll) and, added, Dynamic Library (.dylib).
- **An app chosen as a whole** (a .app bundle, which the macOS open panel can return as
  one item) is searched by its program: the file its Info.plist's CFBundleExecutable
  names in Contents/MacOS. The report gives that file's path.
- **A folder, or a file of 4 GB or more,** gets a message. Files are read with 32-bit
  offsets, as images are.

**Unchanged from Windows:** the report's wording, including "pre-zigzag" and
"post-zigzag". The luminance table is table 0, which isn't always the table the Y
component uses.

**Tests:** 45 new `DQ` cases (632 in all). `gui --dqt CHOICE[+CHOICE...]` chooses the
menu item and answers its file dialog with each file, or cancels it. It prints the
report's search section, each match with its table and form, and which tables were
searched for. The file searched, `dq_exe.bin`, is random bytes with these tables in it:
- the standard table in all ten forms, the first at offset 0, and once more ending at
  the end of the file;
- ok_444.jpg's luminance table in two forms;
- a table with entries over 255 in its 2-byte big-endian form.

The cases cover:
- **The menu item and its dialog,** cancelling it, and a search with no file open.
- **The report's section:** its header, file name and size, the tables, and the end.
- **The standard table in each form,** at the start and end of the file, and the
  number of matches (so nothing else matches).
- **The file shown's table** in both orders, in the second file opened, and in a
  second search.
- **Tables not searched for:** a table of one value, a file without a luminance table,
  and no file analyzed. Also a 16-bit table, which is found in 2-byte form, with only
  its own 1-byte search skipped.
- **What can be chosen:** an empty file, an app, a folder, a missing file, and one too
  large (a sparse file of 4 GB and a byte).

On the previous commit 43 fail. The other two check that no search happens, which was
already so. Twelve deliberate breaks each fail their own cases:
- **Starting after the start position,** as on Windows: 8 cases fail.
- **Stopping before the end,** as on Windows: 6 cases fail.
- **Skipping the 1-byte search for both tables,** as on Windows: 1 case fails.
- **Not checking for a luminance table:** 1 case fails.
- **Searching for a table of one value:** 3 cases fail.
- **The two orders swapped:** 16 cases fail.
- **The two byte orders swapped:** 11 cases fail.
- **Only the first match of each form:** 6 cases fail.
- **An app searched as a folder:** 2 cases fail.
- **No check for a folder:** 1 case fails.
- **No check for an empty file:** 1 case fails.
- **No check for a file of 4 GB or more:** 2 cases fail.

**Checks:**
- **All 632 pass** with and without ASan/UBSan.
- **The app builds** with no new warnings.
- **Not checked:**
  - on the macOS platform: whether the open panel returns an app as one item, and its
    filters; the wait cursor and the status bar during a long search;
  - a real camera maker's program.

**Still open:** Developer ID signing and notarization (M5); latent code; the signature
database items; fill bytes before an RST marker in the report's marker list (see Tools >
Lookup MCU Offset).

### Tools > Add Camera/SW to DB

Tools > Add Camera/SW to DB did nothing: the menu item had no handler. The user
signature database it adds to didn't work either: nothing read it, and nothing could
write it. Now, as on Windows:
- **The menu item** comes after Export TIFF and a separator, with the shortcut Alt+S
  (Option-S on macOS). It needs a file analyzed.
- **Its dialog** shows the file's EXIF Make, Model and Software and its compression
  signature, which can't be changed. It asks for the quality setting and where the
  file came from: a camera, software (whose name it needs), or "I really don't know".
  The source the report suggested is chosen at first.
- **OK adds the entry to the user database,** `JPEGsnoop_db.dat` in the folder that
  Options > Configuration names, and says "Added Compression signature to database".
  A software entry keeps no make or model, as on Windows.
- **Every signature search uses the user database:** the report counts its entries
  ("1 user(*)") and marks them with "*". It is read at the start, as on Windows.

**Found and fixed** (all as on Windows 1.8.0, unless marked as the Qt port's):
- **The database was never read or written** (the Qt port). Nothing loaded it, its
  string writer was commented out, and its path had no separator
  (`<folder>JPEGsnoop_db.dat`, beside the folder). The app also wrote an empty
  `<folder>Signatures.dat` beside the folder at every start. This Mac's temp folder had
  23,079 of them from test runs, which are left there.
- **The same entry could be added again and again.** Windows has an "already in
  database" check, but its flag is never set. An entry the built-in database has was
  also added, then dropped at the next start. Both now get "Compression signature
  already in database". For software, "the same" means the same name and signature.
  For anything else, it means the same make, model, subsampling and signature.
- **An entry for a built-in camera with other subsampling was dropped** at the start,
  though the signature search doesn't count the built-in entry as a match for it.
  Subsampling now counts.
- **The new entry's "valid" flag was never set,** so whether Windows wrote it to the
  file depended on what was in memory there before (found by reading the code).
- **Windows read only the low byte of the entry count,** so a database of 260 entries
  had 4 at the next start. The whole count is now read. A file that lists more than
  300 entries, the limit, has its first 300 used, and the report says so.
- **Windows read only the low byte of each UTF-16 character,** so "Łódź" came back as
  "Aódz".
- **Changing the database folder kept the old folder's entries.** The next entry added
  then wrote them all over the new folder's file. The new folder's database is now read.
- **A file that couldn't be read in full could be lost.** Windows replaced a file
  without its header at the start with an empty database, keeping a copy. It read an
  unknown version's file as empty and a damaged one as far as it could, and the next
  entry added then replaced either without a copy. Now nothing is written at the start.
  The report's signature search says what is wrong, for example "NOTE: User database
  [...] isn't a JPEGsnoop user database". The entries before the problem are used, and
  the next entry added keeps the old file as `JPEGsnoop_db.dat.bak` first. An older
  version's file is kept the same way, as Windows kept it when it rewrote it.
- **An entry that couldn't be saved** stayed in the list for that session only. It is
  now not added, and the message says why.
- **A newline in a field** (one can be pasted into the Software field) ended the field
  early when the file was read back. Every field after it was then out of step. It is
  now saved as a space.

**Changes from Windows:**
- **Nothing is sent over the network.** Windows also posted the entry to
  impulseadventure.com over plain HTTP when "Submit signature to Online DB" was on,
  which it is by default. The post included the make, model, EXIF Software, the JPEG
  comment, MakerNote fields and the user's notes. The dialog's note said that only the
  signature was sent. The dialog's Notes field, which only the post used, is gone. Its
  note now says where the entry goes and that nothing is sent. The title says "Add"
  rather than "Submit".
- **No database file is written until an entry is added.** Windows wrote an empty one
  at the first start, and on a later start said it was missing if it had gone.
- **Messages:**
  - With no signature, a message box says what one needs. Windows said "No valid
    signature could be created, so DB submit is temporarily disabled", and also added
    that to the report.
  - A file that couldn't be analyzed gets a message. The menu item stays enabled after
    one, and the decoder still has the previous file's signature. (Windows enabled the
    item only while a file was analyzed.)
  - A save that fails says why.
- **The quality and software name** are saved without spaces around them, and a name
  of spaces doesn't count as one.
- **An editor number other than 1 to 3** in the file is read as unknown and written
  back as 0. Windows kept the number. Both show such an entry as "??".

**Unchanged from Windows:**
- **The file's format** (version 03, UTF-16LE), so a database from Windows can be copied
  over and used. Versions 02 and 01 are read too.
- **The wording** of the dialog and of the messages above.
- **The 300-entry limit** and its message.
- **What the dialog starts with:**
  - the source the report suggests ("I really don't know" when it suggests neither, the
    signature search is off, or the file was decoded from an offset);
  - the quality from the maker notes;
  - the EXIF Software as the software name. A camera entry doesn't keep it.
- **A software entry the user adds has no match string,** so it never makes the report
  say "EXIF Software field recognized as from editor". (The Windows source says "Not
  quite right perfect" here.)

**Tests:** 88 new `AD` cases (720 in all).
- **The driver:** `gui --add-db OPENING[+OPENING...]` chooses the menu item and answers
  its dialog. It types the quality or software name, pastes into the software field,
  chooses the source, and presses OK, Return, Cancel or Escape. An opening
  `folder=NAME` changes the database folder in Options > Configuration.
- **What it prints:**
  - the dialog's fields and note;
  - the database file, read by the driver's own parser as Windows wrote it;
  - the database as the next start reads it (a `CDbSigs` of the driver's own);
  - the folder's files, and whether a file was written beside the folder;
  - the report's signature search before and after File > Reprocess.
- **Setup options:** `--user-db` copies a database into the folder before the window
  opens (`--user-db-mode` sets its permissions), and `--db-dir` names a folder that
  isn't there.

The cases cover:
- **The menu item and its dialog:**
  - the menu item's place, and that it needs a file;
  - the fields and the suggested source for a camera, software and an unsure file;
  - the note;
  - Cancel, Escape and Return.
- **Adding:**
  - a camera, software and an unsure entry, each written as Windows wrote it and read
    back at the next start;
  - the camera's entry changing the file's assessment from Class 4 to Class 3;
  - the signatures in order, and a pasted newline;
  - a software name that is empty or only spaces, then given.
- **Already in the database:**
  - twice, also with another quality or as unsure;
  - built in for a camera and for software;
  - the same camera with other subsampling, which is added and kept.
- **Nothing to add:** no signature (a Photoshop file), and a file that couldn't be
  analyzed.
- **Windows' files:**
  - versions 03, 02 and 01, with a name beyond Latin-1 and an unknown editor;
  - a built-in duplicate dropped, but not one with other subsampling;
  - 260 entries, 301, and a full database of 300;
  - an older version's file kept when replaced.
- **Files that can't be read in full:** not a database, version 04, damaged in two
  ways, too large, and unreadable. Each gets the report's note, and is kept or left
  alone when an entry is added.
- **Folders:** one that isn't there, a change of folder in Options > Configuration,
  and no folder.

On the previous commit 76 fail. The other 12 check what was already so, such as the
menu item's place and that Cancel writes nothing. Twenty-seven deliberate breaks each
fail their own cases:
- **Reading as Windows did:** only the count's low byte, 5 cases fail; only each
  character's low byte, 1 fails.
- **The database not read at the start:** 31 fail.
- **No separator in its path:** 48 fail.
- **The empty `Signatures.dat` written at the start again:** 2 fail.
- **The new entry's "valid" flag not set,** as on Windows: 21 fail.
- **Duplicates:**
  - one built in not refused: 3 fail;
  - one the user added not refused: 4 fail;
  - subsampling not compared, as on Windows: 4 fail;
  - software names not compared: 1 fails.
- **Reading the file:**
  - built-in entries not dropped at the start: 1 fails;
  - no limit of 300 while reading: 2 fail (the app crashes);
  - an unknown editor kept: 1 fails;
  - the list not emptied before a folder's database is read: 2 fail.
- **A folder change not read,** as on Windows: 3 fail.
- **Old files:** no copy of a file that couldn't be read in full, 6 fail; none of an
  older version's file, 2 fail.
- **No note in the report:** 7 fail.
- **An entry kept after a failed save,** as on Windows: 1 fails.
- **Saving:** no newline replaced, 1 fails; the signatures not in order, 1 fails.
- **The dialog:**
  - no suggested source: 16 fail;
  - no software name required: 4 fail;
  - spaces kept: 3 fail;
  - no subsampling passed on: 12 fail.
- **No check for a signature:** 2 fail.
- **No check for an analyzed file:** 1 fails.

**Second check:** a further review found three problems, each reproduced and now fixed:
- **Two copies of JPEGsnoop with the same database folder lost entries.** Each read the
  file at its start and wrote back what it had read plus its own entry, so the second to
  add wrote over what the first added. Windows does the same. An entry is now added
  under a lock, `JPEGsnoop_db.dat.lock` beside the file: the file is read again, then
  written, and the lock removed.
  - A copy that finds the lock waits up to 5 seconds, then says "[...] is being
    changed by another copy of JPEGsnoop (it holds [...lock]). Try again."
  - A lock left by a copy that stopped is taken over: Qt's `QLockFile` treats one as
    stale when its process has gone or it is over 30 seconds old.
  - A folder where the lock can't be made gets "[...] couldn't be written: [...lock]
    couldn't be made", and a folder that isn't there "there is no folder [...]".
- **An entry could make the file too large to read back.** The start reads at most
  4 MiB, but nothing limited what was written: a 4,194,272-byte database took an entry,
  said "Added", and at the next start had no entries at all. An entry that would take
  the file over 4 MiB is now refused, and the file is left as it was: "[...] would be N
  bytes with it, more than the 4194304 that JPEGsnoop reads, so it wasn't changed".
  Usual entries are about 200 bytes. Only very long names, such as a pasted software
  name, come near the limit.
- **A version 01 entry wasn't found as a duplicate when the image's tables, as they
  are, have the higher signature.** Version 01 kept only that signature. An entry being
  added puts the lower one first, so the comparison missed it, and the same entry was
  added again. Signatures now match as they do in the signature search: either of the
  entry's two against either of the other's. Every built-in entry has both signatures,
  so this affected only version 01 files.

The driver gains openings `other=NAME`, `lock` and `stale-lock`, and the option
`--db-dir-mode`. `other=NAME` has another copy of JPEGsnoop add software NAME (a
`CDbSigs` of the driver's own, which read the database when the window opened). `lock`
holds the lock as such a copy does while it writes. `stale-lock` leaves an hour-old lock
file. `--db-dir-mode` makes the folder read-only.

27 new cases (747 in all) cover:
- **Version 01:** a software and a camera entry already in, with the file left as it was.
- **The size:** an entry that brings the file to exactly 4 MiB is added and read back.
  One that would make it 2 bytes more is refused, and the file and the list stay as
  they were.
- **Another copy:** adding before this one and after it, with both entries kept, and
  the same entry already in.
- **The lock:** held by another copy, left by a copy that stopped, and a read-only
  folder where it can't be made.
- **A failed save** (a read-only file, an unreadable one, a file over the limit): the
  entry isn't kept.

The missing-folder case now checks the whole message. On the previous commit 18 of the
new cases fail. The other 9 check what was already so, such as an entry that brings the
file to exactly 4 MiB. Ten new deliberate breaks each fail their own cases:
- **Signatures compared one way**, as before: 3 fail.
- **The size:** no check, 4 fail; refusing a file of exactly 4 MiB, 2 fail; the entry
  kept after the check fails, 2 fail.
- **The file not read again:** 6 fail.
- **The lock:** none taken, 5 fail; one left by a copy that stopped not taken over, 2
  fail; the lock not removed after adding, 13 fail.
- **No check for the folder:** 1 fails.
- **The entry kept when the old file can't be copied:** 2 fail.

The first 27 breaks were run again on the new code, and each still fails its own cases.

**Checks:**
- **All 747 pass** with and without ASan/UBSan.
- **The app builds** with no new warnings.
- **Not checked:**
  - on the macOS platform: the dialog's layout, and Option-S;
  - the quality taken from real maker notes;
  - a database written by Windows itself (the test files follow the Windows source);
  - the effect on File > Batch Process, which uses the same database.

**Still open:** Tools > Manage Local DB, which will need the same lock and reading
again before it writes. Options > Configuration's "Submit signature to Online DB" does
nothing. Developer ID signing and notarization (M5). Latent code. Fill
bytes before an RST marker in the report's marker list (see Tools > Lookup MCU Offset).

### Tools > Manage Local DB

Tools > Manage Local DB did nothing: the menu item had no handler. Now, as on Windows:
- **The menu item** comes after Add Camera/SW to DB, with Windows' status tip "Manage
  the local User signatures". It needs no file open.
- **Its dialog,** "Manage User Database", lists the user database's entries in the
  file's order. The signatures built into JPEGsnoop aren't listed.
- **Remove and Remove All** take entries out of the list. **OK** takes them out of the
  file; **Cancel** leaves it as it was.
- **Nothing is said when it works,** as on Windows. The report shows the change after
  File > Reprocess, as after Add Camera/SW to DB.

**Found and fixed** (all as on Windows 1.8.0, unless marked as the Qt port's):
- **The menu item did nothing** (the Qt port).
- **What another copy of JPEGsnoop did to the file was undone.** Windows listed the
  entries it had read at its start, and OK wrote that list back without the ones
  removed. So an entry another copy had added since was lost, and one it had removed came
  back. Windows wrote the file this way even when none was removed. Now:
  - the dialog reads the file when it opens;
  - OK takes the lock that Add Camera/SW to DB takes, reads the file again, and takes
    out the entries removed. Each is found by all its fields, and an entry another copy
    has already taken out is skipped;
  - what another copy added or took out, before the dialog opened or while it was open,
    stays as it did.
- **The list didn't say what an entry was.** Each row read "Make: [...] Model: [...]
  Qual: [...]", with a software name in place of the make. It didn't show whether an
  entry was for a camera or for software, an unsure entry's software name, the
  subsampling or the signature. So the same camera at two subsamplings, which the
  database keeps apart, made two rows that looked the same.

**Changes from Windows:**
- **The list has columns:** Source (Camera, Software, Unknown, or "Not set" for an editor
  number other than 1 to 3), Make, Model, Software, Quality, Subsampling and Signature
  (the lower of the two). A field over 200 characters is shown cut short.
- **Several entries can be removed at once** (Command-click and Shift-click), and Delete
  or Backspace in the list is Remove. The row after those removed is chosen next
  (Windows chose none), so Remove can be pressed again. Remove is off with nothing
  chosen, and Remove All with nothing left.
- **A line beside the buttons** counts the entries and those removed, for example "2
  entries, 1 removed: OK takes it out of the file." With none, it says why: none added
  yet, no folder, or a file that couldn't be read.
- **The dialog's note** says what Remove and OK do and where the database is. For a
  file that couldn't be read in full, it gives the report's note and says that the file
  is kept as `JPEGsnoop_db.dat.bak` before it is changed.
- **OK with none removed doesn't write the file.**
- **A file of the same entry twice** (Windows could add one again): each row removed
  takes out one.
- **Older and damaged files** are kept as `JPEGsnoop_db.dat.bak` before they are
  replaced, as when an entry is added, and a message says so.
- **No file larger than the start reads** (4 MiB) is written. Only an older version's
  file can grow: it used single bytes, and version 03 uses two for each character. The
  removal is then refused, with the file left as it was: "[...] would be N bytes as
  version 03, more than the 4194304 that JPEGsnoop reads, so it wasn't changed".
- **A save that fails says why,** for example "ERROR: The entry wasn't removed: [...] is
  being changed by another copy of JPEGsnoop (it holds [...lock]). Try again."

**Unchanged from Windows:** the title, the four buttons, Cancel, the menu item's place
and status tip, that it needs no file open, and that built-in signatures can't be removed.

`DatabaseExtraClean()`, which emptied the database without the lock, is gone; nothing
used it. Add Camera/SW to DB now takes the lock and keeps old files through the same two
functions as Manage Local DB, with the same messages as before.

**Tests:** 76 new `MD` cases (823 in all).
- **The driver:** `--add-db` takes an opening `manage[,STEP...]`, which chooses the menu
  item and answers its dialog:
  - `select=N[;N...]` clicks row N and Command-clicks the others;
  - `remove`, `remove-all`, `delete` and `backspace` (the keys, in the list), `list`;
  - `ok`, `return`, `cancel`, `escape`;
  - the other copy's openings, while the dialog is open.

  The new opening `other-remove=NAME` has the other copy take out an entry. The
  driver prints the dialog's rows and note, and after each step the rows chosen, the
  buttons and the count.

The cases cover:
- **The menu item:** its place and tip, and the dialog before any file is open.
- **The dialog:**
  - the columns and each kind of row;
  - a long field;
  - the note and count for an empty database, no folder, and a file that can't be read.
- **Choosing and removing:**
  - one row, two rows, the last row;
  - Delete and Backspace, also with nothing chosen;
  - Remove All;
  - the row chosen next.
- **OK, Return, Cancel and Escape:**
  - the file, read as Windows wrote it and as the next start reads it;
  - no message;
  - the report's count after File > Reprocess;
  - the dialog opened again, and the same entry added again after it was removed;
  - OK with none removed (a version 02 file isn't rewritten).
- **Older and damaged files:** versions 02 and 01, a damaged file and one of 301
  entries (the `.bak` checked for all but version 01); the same entry twice.
- **The size:** a version 02 file that would be 2 bytes over the limit as version 03,
  one that would be exactly at it, and removing the large entry instead.
- **Another copy:**
  - an entry it added before the dialog opened is listed;
  - one it added while the dialog was open is kept;
  - one it removed isn't written back;
  - one both removed: nothing is written.
- **The lock:** held, left by a copy that stopped, and a read-only folder; also a
  read-only file.

On the previous commit 64 of the new cases fail. The other 12 pass because the menu item
did nothing there: they check what should stay as it was, such as the file after Cancel
or a failed save. Twenty new deliberate breaks each fail their own cases:
- **The menu item:** no handler, 63 fail; off until a file is open, 65 fail.
- **The list not read again when the dialog opens:** 1 fails.
- **OK:**
  - the file not read again before it is written: 3 fail;
  - no lock: 5 fail;
  - the file written with none removed, as on Windows: 2 fail;
  - every copy of a duplicate taken out: 1 fails;
  - the list in memory not updated after the file is written: 2 fail;
  - the entries left in the list taken out instead: 33 fail.
- **The size:** no check, 3 fail; refusing a file of exactly 4 MiB, 1 fails.
- **Messages:** none for a failed save, 5 fail; one when it simply worked, 2 fail.
- **The dialog:**
  - a software name listed as the make, as on Windows: 3 fail;
  - long fields not cut short: 1 fails;
  - one row at a time, as on Windows: 6 fail;
  - Delete and Backspace ignored: 2 fail;
  - no row chosen after a removal, as on Windows: 5 fail;
  - Remove on with nothing chosen: 10 fail;
  - the note without the report's note: 1 fails.

The 37 breaks from Tools > Add Camera/SW to DB were moved onto the new code, where the
lock and the copying of old files are now in shared functions. Each still fails the same
`AD` cases, and many fail new ones too. The exception is the break that keeps an entry
when the old file can't be copied. Before, it also changed the message, so the message
check caught it. Now it only keeps the entry, and the report's count catches it (2
fail): after a failed copy, and after a failed write, which goes through the same
function now.

**Second check:** a review before the commit found one problem, reproduced and now fixed.
Another program could make the file unreadable, or damage it, while the dialog was open.
If none of the entries removed was then in the part that could be read, OK closed the
dialog without a word, and nothing was removed. They may be in the part that wasn't read,
so nothing is written, and the message gives the reason, as the report's note would: for
example "ERROR: The entry wasn't removed: [...] couldn't be opened: Permission denied".
An entry found in the part that was read is still taken out, with the file kept as
`.bak` first, as for a file damaged before the dialog opened.

The driver gains the openings `file-mode=OCTAL` and `db-file=NAME`, which change the file
from outside, also while the dialog is open. 7 new cases (83 `MD`, 830 in all) cover:
- the file made unreadable, with one entry removed and with two;
- the file damaged, with the entry removed in the part not read, and in the part read.

On the previous commit 5 of them fail; the other 2 check that the file is left as it was.
A new break that ignores the error fails 3. The 57 earlier breaks were run again with the
new cases, and none of them passes a case it failed before.

**Checks:**
- **All 830 pass** with and without ASan/UBSan.
- **The app builds** with no new warnings.
- **cppcheck 2.22** (`--enable=warning,style,performance,portability --library=qt`) finds
  nothing new in the changed files, compared with the previous commit. In the driver its
  one new finding, "Condition '!bShown' is always true", is wrong: the dialog's timer sets
  `bShown` while the menu item runs. It reports the same line in the Add code too. (The
  driver needs `-D'__has_feature(x)=0'`, or cppcheck skips it.)
- **Not checked:**
  - on the macOS platform: the dialog's layout and Shift-click;
  - two copies of the app at once (the driver's other copy is a second `CDbSigs` in the
    same process);
  - a database folder on a network volume, where the lock may not work.

**Still open:** Options > Configuration's "Submit signature to Online DB" does nothing.
Developer ID signing and notarization (M5). Latent code. Fill bytes before an RST marker
in the report's marker list (see Tools > Lookup MCU Offset).

### View > Overlays > MCU Grid

View > Overlays > MCU Grid did nothing: the menu item had no handler. The code that drew
the grid was in the decoder's drawing routine, which the Qt port had commented out, and
that copy never set its dotted pen. Now, as on Windows:
- **The menu item** has a check mark and Windows' status tip "Display MCU Boundary grid".
  It is off at the start and isn't saved, and it needs no file open.
- **The grid** is a dotted dark grey line (RGB 32,32,32) along the top and the left of
  each MCU, drawn over the image window's image, with the image showing between the dots.
  Each dot is one screen pixel, also on a Retina screen, on every third pixel counted from
  the image's top or left edge. There is none at 12.5%, where an MCU of 8 pixels is one
  screen pixel.
- **It stays on** from file to file, follows each file's MCU size, and is kept when the
  zoom or the channel changes.

**Found and fixed** (the Qt port's):
- **The menu item did nothing.**
- **Command-G was both Find Next's key and MCU Grid's.** Qt turns Windows' Ctrl+G into
  Command-G on macOS, which is also Edit > Find Next's standard key there. Once a search
  had made Find Next available, Qt's own shortcut handling ran neither ("Ambiguous
  shortcut overload"), as the driver shows. The macOS menu bar handles keys itself, so
  there one of them may have won, but not both. The Find tests didn't see it: they press
  Find Next's first key, which on the test platform is F3. MCU Grid now has Control-G on
  macOS, the same keys as Windows' Ctrl+G, and Command-G is Find Next's alone.

**Changes from Windows:**
- **Control-G** on macOS, as above.
- **No grid over a Photoshop file's image,** which has no MCUs.
- **The grid is drawn by the image window** (`PreviewCanvas`), not the decoder:
  `CimgDecode::ViewMcuOverlay()` is gone, and the menu item sets the setting rather than
  toggling it, so the check mark and the grid always agree.

**Unchanged from Windows:** the menu item's place, the grid's look, none at 12.5%, off at
the start, not saved.

**Not ported:** Windows' drawing routine also drew a red line where the image ends inside
its last, partial MCUs, whether or not the grid was on. The image window draws no such
line.

**Tests:** 30 new `MG` cases (860 in all).
- **The driver:** `gui --mcu-grid STEP[,STEP...]` (`--mcu-grid-at N`, 0 before any file)
  prints the menu item's place, shortcut and check mark. It then chooses the menu item,
  presses its key or Cmd+G, picks a zoom or a channel, and reports the grid. The report
  compares the image window's drawing, in screen pixels, with the decoded image. It gives
  the screen columns and rows the dots are on, their spacing and colours, and how many
  other pixels differ. It is printed after every file too.
- **Test images:** white images, so that the dots are the only dark pixels.

The cases cover:
- **The menu item:** its place, tip, shortcut and check mark; choosing it, and its key;
  before any file is open.
- **Command-G** after a search: Find Next runs, the grid is unchanged.
- **The grid:** MCUs of 16x16, 8x8 and 16x8, a grayscale image, and an image rounded up
  to whole MCUs.
- **Zoom:** 12.5% (none), 25%, 50%, 150%, 200% and 800%; a Retina screen at 50%, 100% and
  200%.
- **Channels:** another channel, and back.
- **Later files:** another MCU size, a progressive JPEG (no image), a Photoshop file (no
  grid), and a JPEG after it.

On the previous commit 27 of them fail. The other 3 check what was already so: no grid
while it is off, and the menu item enabled with no file open. Seventeen deliberate breaks
each fail their own cases:
- **The menu item:** no handler, 19 fail; not checkable, 26 fail; checked at the start,
  29 fail; Command-G as its key, 3 fail.
- **Updating the grid:** not when the menu item is chosen, 7 fail; not when an image is
  drawn, 4 fail; turned off for each file, 3 fail.
- **Drawing it:**
  - not at all, 19 fail;
  - not below 100%, 3 fail;
  - at 12.5% too, 1 fails; not at 25%, 1 fails;
  - a line two screen pixels wide on a Retina screen, 3 fail;
  - solid, 19 fail; black, 19 fail;
  - along the right of each MCU, 19 fail;
  - columns spaced by the MCU's height, 2 fail;
  - over a Photoshop file's image, 1 fails.

**Second check:** a review before the commit found one problem, reproduced and now fixed.
Scrolling the image window copies what it shows and paints only what comes into view.
The grid was drawn with a dotted pen, whose dots start where the area being painted
starts, so the new dots fell between the copied ones. At 2x and 100%, painting the
whole window again after a scroll changed thousands of the window's pixels. Each dot is
now drawn as one screen pixel at a place counted from the image's edge, so a dot is in
the same place however much is painted.

The driver gains the steps `scroll=X;Y`, `size=W;H` and `buffer`. `buffer` grabs the
window as it was painted and counts the pixels that painting it all again changes, and
those with the grid's colour. The full-canvas grab the other cases use paints everything
again, which is why they didn't see this. 9 new cases (39 `MG`, 869 in all) cover:
- scrolling down and right, twice, and back up and left;
- at 50%, 100% and 150%, and on a Retina screen;
- making the window larger;
- the grid off.

On the previous commit 8 of them fail; the other checks the grid off. Two new breaks:
the dotted pen back, 6 fail; the dots counted from the area being painted, 7 fail. The
17 earlier breaks, those for drawing rewritten for the new code, each still fail the same
cases among the first 30. Making the window larger didn't show the problem with the
dotted pen either; its case stays as a check.

**Checks:**
- **All 869 pass** with and without ASan/UBSan.
- **The app builds** with no new warnings.
- **cppcheck 2.22** finds nothing new in the changed app files, and two findings fewer
  (in the removed `ViewMcuOverlay()`). Its two findings in the new driver code are fixed.
- **Not checked:** on the macOS platform, where the menu bar handles key equivalents
  itself: Control-G and Command-G there, and the grid's look on screen.

**Still open:**
- **Windows' red line** at the image's real size, above.
- **Menu items not yet ported:** View > Toolbar and Status Bar, File > Print Setup and
  Recent File, and the toolbar's New button.
- **For a decision:** Options > Check for Updates (Windows fetched a file over plain
  HTTP), Edit > Undo and View > Adjust (both greyed out; Windows' Adjust did nothing),
  and Options > Configuration's "Submit signature to Online DB", which does nothing.
- **Also:** Developer ID signing and notarization (M5). Latent code. Fill bytes before an
  RST marker in the report's marker list (see Tools > Lookup MCU Offset).

### View > Toolbar and Status Bar

View > Toolbar and View > Status Bar did nothing but change their check marks: neither
had a handler, so choosing View > Toolbar took its check mark off while the toolbar
stayed. Now, as on Windows:
- **View > Toolbar** shows or hides the toolbar, and **View > Status Bar** the status
  bars, each with Windows' status tip ("Show or hide the toolbar", "Show or hide the
  status bar").
- **Both are shown at the start,** and the setting isn't saved (Windows didn't save it
  either).
- **The report and the image take the space** a hidden bar leaves.

**Found and fixed** (the Qt port's):
- **Neither item did anything.**
- **The toolbar's two switches disagreed.** The toolbar area's context menu, which Qt
  provides, named the toolbar "File" and could hide it, but View > Toolbar stayed checked.
  View > Toolbar is now the toolbar's own show and hide action, named "Toolbar" in both
  places, so its check mark follows the toolbar however it is hidden: from the menu, the
  context menu, or by closing it while it floats.

**Changes from Windows:**
- **View > Status Bar covers two status bars.** Windows had one, below the report and the
  image, with the messages and the MCU, file position and YCC under the mouse. Here the
  messages are in the main window's status bar and the rest in the image window's, so the
  item shows or hides both.

**Unchanged from Windows:** the names, the places (after View > Adjust and a separator),
the tips, shown at the start, not saved.

**Tests:** 16 new `VB` cases (885 in all).
- **The driver:** `gui --bars STEP[,STEP...]` (`--bars-at N`, 0 before any file) prints
  the items' places, and whether the toolbar and both status bars are shown and the items
  checked. Its steps choose either item, choose the toolbar in the toolbar area's context
  menu, or hide the toolbar itself. After each it prints the same, and whether the report
  and the image window's view got taller, shorter or stayed the same.

The cases cover:
- **The items:** their places and tips; both shown and checked at the start.
- **Status Bar:** hiding both status bars and showing them again; the report and the image
  take the space and give it back.
- **Toolbar:** hiding and showing it; the report takes the space, the image's view
  doesn't change; both bars hidden.
- **The check mark:** following the toolbar when the context menu hides and shows it, and
  when the toolbar itself is hidden; the context menu names it "Toolbar".
- **Before any file is open,** with the image window not yet shown: its status bar stays
  hidden when it appears; and a later file.

On the previous commit 15 of them fail. The other checks what was already so: both shown
and checked at the start. Eight deliberate breaks each fail their own cases:
- **Status Bar:** no handler, 6 fail; only the main window's, 6 fail; only the image
  window's, 6 fail; unchecked at the start, 13 fail.
- **Toolbar:** no handler, 10 fail; an action of its own that shows and hides the toolbar
  but doesn't follow it, 5 fail; named "File", 16 fail.
- **No tips:** 2 fail.

**Checks:**
- **All 885 pass** with and without ASan/UBSan.
- **The app builds** with no new warnings.
- **cppcheck 2.22** finds nothing new in the changed files.
- **Not checked:** on the macOS platform, the toolbar's look and the toolbar area's
  context menu.

**Still open:**
- **Windows' red line** where the image ends inside its last MCUs (see View > Overlays >
  MCU Grid).
- **Menu items not yet ported:** File > Print Setup and Recent File, and the toolbar's New
  button.
- **For a decision:** Options > Check for Updates (Windows fetched a file over plain
  HTTP), Edit > Undo and View > Adjust (both greyed out; Windows' Adjust did nothing),
  and Options > Configuration's "Submit signature to Online DB", which does nothing.
- **Also:** Developer ID signing and notarization (M5). Latent code. Fill bytes before an
  RST marker in the report's marker list (see Tools > Lookup MCU Offset).

### File > Print Setup

File > Print Setup did nothing: the menu item had no handler. Print and Print Preview
each made a printer of their own, so nothing chosen in one carried over to the other.
Now, as on Windows:
- **Print Setup** shows the page setup dialog (on macOS, the system's page layout panel)
  for the printer, paper and orientation, with MFC's status tip "Change the printer and
  printing options". It needs no file open.
- **Print, Print Preview and Print Setup share one printer** for the session, as MFC's
  shared printer settings did. What one of them sets, the others use: for example,
  landscape chosen in Print Setup is what Print prints and Print Preview shows, and so is
  a change made in Print's dialog. The setting isn't saved.
- **A page range is for one job:** each Print and Print Preview starts with every page.
  Choosing Selection for one job isn't kept for the next, which on the shared printer
  would have printed or previewed only a selection that might be gone.

**Found and fixed** (the Qt port's):
- **Print Setup did nothing.**
- **Choices didn't carry over.** Print and Print Preview each made a new printer, so the
  paper and orientation chosen for one were gone for the other and for the next job.

**Changes from Windows:**
- **The dialog** is the system's page layout panel on macOS, which sets the paper, the
  orientation and the printer to format for, rather than Windows' Print Setup dialog.
  For a printer that makes PDF files it is this version's own (see "Second check"
  below). The menu item keeps Windows' name.
- **`MainWindow::setPrintDialogRunner()`** runs Print Setup's and Print's system
  dialogs, and shows them unless something else is set. On macOS they are the system's
  panels, which the test driver can't answer, so it sets its own. Print Preview's
  dialog and Print Setup's own dialog are Qt windows, so the driver answers them as it
  does other dialogs.

**Unchanged from Windows:** the name and place (after Print Preview), shared for the
session, not saved.

**Tests:** 21 new `PS` cases (906 in all).
- **The driver:** `gui --print STEP[,STEP...]` (`--print-at N`) prints the item's place.
  It makes the shared printer write a PDF in the settings folder, so nothing is ever
  printed. Even on the test platform, the printer is this Mac's real default printer
  until then. It answers Print Setup's dialog (landscape, portrait, A4, Letter, Cancel)
  and Print's (all, Selection, landscape, Cancel), and refuses Print if it isn't writing
  a PDF. It reads the PDF's pages and page size, and Print Preview's printer and pages.
  It can also select text in the report.

The cases cover:
- **The item:** its place and tip.
- **Print Setup:** its dialog edits the shared printer; landscape, portrait again,
  Letter, Letter in landscape, and Cancel, each as Print then prints it; landscape as
  Print Preview shows it, and twice in a row; before any file is open.
- **Sharing:** Print's and Print Preview's printer is the shared one; landscape chosen
  in Print's dialog is what Print Preview shows next; Cancel in Print writes nothing.
- **Selection:** offered only when report text is selected; printing it gives one page;
  the next Print and Print Preview are of every page again.

The 5 cases that don't choose Print all fail on the previous commit. The 16 that do
weren't run there, as its Print shows the system's print panel, which could print to a
real printer; the driver's copy for that run skips Print. Nine deliberate breaks each
fail their own cases:
- **Print Setup:** no handler, 9 fail; its dialog on a printer of its own, 9 fail; no
  tip, 1 fails.
- **Print:** a printer of its own, 12 fail; the range not reset, 1 fails; Selection not
  offered, 1 fails.
- **Print Preview:** a printer of its own, 4 fail; the range not reset, 1 fails.
- **A new printer each time it is asked for:** all 21 fail (the driver crashes: the
  printer it set up is gone).

**Second check:** a review before the commit found one problem, reproduced and now
fixed. Qt's macOS page setup dialog does nothing for a printer that makes PDF files:
its `exec()` returns Rejected at once, and `setVisible()` returns early
([`qpagesetupdialog_mac.mm`](https://github.com/qt/qtbase/blob/dev/src/printsupport/dialogs/qpagesetupdialog_mac.mm)).
The shared printer is one when no printer is set up, and after Save as PDF in the print
panel, which sets a `.pdf` file. Print Setup then opened nothing, for the rest of the
session. The tests didn't see it, because the driver answered every page setup dialog
itself.

Now, on macOS, Print Setup shows this version's own dialog for such a printer. It offers
the paper size (the common sizes, and the printer's own if it isn't one of them) and
the orientation, starting from the printer's, with OK and Cancel. It changes only those:
the printer keeps making PDF files, to the same file, with the same margins. The driver
now answers the page layout panel as Qt's macOS dialog does, doing nothing for such a
printer, and answers the new dialog on screen. Its printer line gains the format and the
margins. It also gains `printer=native` (this Mac's default printer, which only Print
Setup and Print Preview use, as the driver's Print refuses it) and `print=save-pdf`.

9 new cases, one replaced (29 `PS`, 914 in all), cover:
- **The new dialog:** its fields, starting from the printer's paper and orientation;
  keeping the margins and the file.
- **The system's dialog** for this Mac's printer, and Print Preview after it.
- **Save as PDF** then Print Setup: the new dialog shows, and Print then uses what it
  set.

The 6 of them that don't choose Print fail on the previous commit; the 3 that do weren't
run there. Six new breaks each fail their own cases:
- **The earlier code,** which showed Qt's dialog for every printer: 14 fail.
- **Switching the printer to the system's** before Print Setup, so it stops making PDF
  files: 15 fail.
- **The new dialog:** OK not applied, 11 fail; only the paper, 10 fail; only the
  orientation, 4 fail; not starting from the printer's, 5 fail.

The 9 earlier breaks still fail their cases. The one that gives Print Setup's dialog a
printer of its own now fails 4, as only the system's dialog is given the printer.

**Checks:**
- **All 914 pass** with and without ASan/UBSan.
- **The app builds** with no new warnings.
- **cppcheck 2.22** found one new finding, a parameter named `printer` hiding the new
  `printer()`; it is renamed, and there is nothing else new.
- **Not checked:** the system's page layout and print panels themselves; the new
  dialog's look on the macOS platform; printing on paper. The shared printer was checked
  with PDF output, and with this Mac's printer for Print Setup and Print Preview only.

**Still open:**
- **Windows' red line** where the image ends inside its last MCUs (see View > Overlays >
  MCU Grid).
- **Menu items not yet ported:** File > Recent File, and the toolbar's New button.
- **For a decision:** Options > Check for Updates (Windows fetched a file over plain
  HTTP), Edit > Undo and View > Adjust (both greyed out; Windows' Adjust did nothing),
  and Options > Configuration's "Submit signature to Online DB", which does nothing.
- **Also:** Developer ID signing and notarization (M5). Latent code. Fill bytes before an
  RST marker in the report's marker list (see Tools > Lookup MCU Offset).

### File > Recent File

File > Recent File was only a greyed placeholder: the port kept no list of the files
opened. Now, as on Windows (MFC's list):
- **The four files opened last** are listed where the placeholder is, newest first, as
  "1 <path>" to "4 <path>". While there are none, the greyed "Recent File" placeholder
  shows. Each item's status tip is the whole path.
- **Choosing one** opens that file again and puts it first. One that can't be opened any
  more (moved, deleted, or on a volume that isn't mounted) gets the usual "Couldn't open
  file" message and is taken off the list, as MFC did.
- **A file joins the list,** first, when it is opened by File > Open or by one of the
  items, also an empty file, as on Windows. One that can't be opened isn't added.
  Reprocess and Batch Process don't change the list.
- **No file is listed twice:** opened again, by the same path, a path with "..", or a
  symbolic link, it moves first.
- **The names:** the home folder as "~". A path over 50 characters is shortened in the
  middle, as MFC's `AbbreviateName()` did at 30: its first part and as many of its last
  folders as fit, for example "~/…/Holiday in the mountains/Day 3/IMG_1234.jpg". If not
  even that fits, the file's name alone. A "&" in a name is doubled, or it would mark a
  shortcut and not show.
- **The list is saved** under MFC's names ("Recent File List\File1" to "File4") and read
  at the next start. A file gone since stays listed until it is chosen, as on Windows.

**Changes from Windows:**
- **Saved as it changes.** Windows saved the list at exit, so a crash lost the files
  opened since the start.
- **50 characters before a path is shortened** (MFC: 30), as macOS paths are longer, and
  "~" for the home folder.
- **The same file through a symbolic link** is one item. MFC compared the paths only.

**Unchanged from Windows:** four files, the place, the placeholder, the numbers, the
message and removal for a file that can't be opened, a file gone since kept until chosen.
As on Windows, two copies of JPEGsnoop each keep their own list, and the last to save it
wins (not tested).

**Tests:** 32 new `RF` cases (946 in all).
- **The driver:** `gui --recent STEP[,STEP...]` (`--recent-at N`) prints the
  placeholder's place, the items (each item's text and the file named in its tip), and
  the list as saved, after each step. Its steps open a file as File > Open does, open a
  copy of one or a link to one, choose an item, move a copy away and back, and reprocess.
  `name=PATH` prints how an item names a path, with no file needed. `--recent-stored`
  saves a list before the window opens, as a previous start would have.

The cases cover:
- **The placeholder:** its place, and that it is greyed while the list is empty.
- **The list:** one file, three in order, five (the oldest dropped), the same file again,
  by a path with "..", and by a link; the files given to the driver; an empty file; a
  file that can't be opened (not added); Reprocess.
- **Choosing:** an item opens that file and puts it first; one whose file was moved away
  gets the message and is taken off, but opens if the file is back before it is chosen;
  taking one off doesn't bring back one dropped before.
- **A list saved before:** shown at the start, with a file gone since kept until chosen.
- **The names:** short, under the home folder, long (also under the home folder), exactly
  50 and 51 characters, a long file name alone, and "&", in a name and in an item.

On the previous commit 29 of them fail. The other 3 check what was already so: the
placeholder, its place, and that nothing was saved. Two faults in the new cases were found
and fixed while checking them:
- **The case for a file that can't be opened** matched the list printed before its step,
  so it would have passed with the file added. A break showed it.
- **Moving a file away** renamed one of the shared test files, which other runs open.
  Breaks run side by side then failed each other's cases. The step now moves a copy in
  the run's own settings folder.

Fourteen deliberate breaks each fail their own cases:
- **The list:**
  - a file opened not added, 18 fail;
  - one that couldn't be opened added, 1 fails;
  - an empty file left out, 1 fails;
  - no check for the same file, 4 fail; the same file through a link not found, 1 fails;
  - no limit of four, 1 fails;
  - not saved, 9 fail; not read at the start, 2 fail.
- **Choosing:** one that can't be opened kept, 3 fail.
- **The menu:** the placeholder shown with the items, 7 fail; items not numbered, 13 fail.
- **The names:** no "~", 2 fail; not shortened, 4 fail; "&" not doubled, 2 fail.

**Checks:**
- **All 946 pass** with and without ASan/UBSan.
- **The app builds** with no new warnings.
- **cppcheck 2.22** finds nothing new in the changed files.
- **Not checked:** on the macOS platform, how the native menu shows the items; two copies
  at once; a volume that isn't mounted (simulated by moving the file away).

**Still open:**
- **Windows' red line** where the image ends inside its last MCUs (see View > Overlays >
  MCU Grid).
- **Not yet ported:** the toolbar's New button.
- **For a decision:** Options > Check for Updates (Windows fetched a file over plain
  HTTP), Edit > Undo and View > Adjust (both greyed out; Windows' Adjust did nothing),
  and Options > Configuration's "Submit signature to Online DB", which does nothing.
- **Also:** Developer ID signing and notarization (M5). Latent code. Fill bytes before an
  RST marker in the report's marker list (see Tools > Lookup MCU Offset).

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
- **User signature DB:** `DbSigs.cpp:756` writes with `strlen` over an uninitialised buffer; `589-600` has no `DBEX_ENTRIES_MAX` check (overflows `m_sSigListExtra[300]`); `185` reads 1 byte instead of 4; `689` uses `"[%s]"`. (Fixed: see "Tools > Add Camera/SW to DB".)
- **Export JPEG:** `JfifDecode.cpp:10321` skips writing the MJPEG DHT but logs that it did; `10232`, `10429` leak the `QFile` on failure. (Fixed: see "Export JPEG and Export TIFF".)
- **Signature submit:** `strFormData` is never built (`8846` commented out). (Not needed: this version doesn't post signatures. See "Tools > Add Camera/SW to DB".)
- `General.cpp:107-111` (`Uint2DotByte`): operator precedence makes every field the low byte; `93-98` (`Uint2Chars`): chained `.arg()` substitutes `%` in file bytes.
- `WindowBuf.cpp:584`, `196`, `347`: printf formats passed to `.arg()`; `716`: `OverlayGet` has no bounds check.
- `q_decodedetaildlg.cpp:38-46`: empty `accept()`/`reject()`, so the dialog can't be closed once wired up. (Fixed: see "Detailed Decode".)

## 8. Found earlier, during the macOS build work

- `WindowBuf.cpp:1113` (`BufReadUniStr2`): builds a `QString` from UTF-16 bytes as a C string, so EXIF `XPTitle`/`XPComment`/`XPAuthor`/`XPKeywords` show one letter, and FlashPix stream positions drift (`JfifDecode.cpp:4853`).
- `DbSigs.cpp:83`: writes `dbPath() + "Signatures.dat"` without a path separator, dumping the built-in list next to the app-data folder on every launch. (Fixed: see "Tools > Add Camera/SW to DB". The file was empty: the line that wrote the list was commented out.)
- `Viewer.cpp:29`, `56`: preview opens at 12.5% and `setScaledContents` stretches it, so it looks blocky.
- About 25 menu actions are created but never connected (batch, export, find, copy, detailed decode, DB, about, updates); no command-line mode; `FileTiff`, `JPEGsnoopCore`, `DecodeDicom` aren't built; user DB load/save disabled (fixed: see "Tools > Add Camera/SW to DB").
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
