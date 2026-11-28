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
- **M5 Packaging:** bundle ID `com.impulseadventure.JPEGsnoopQt` (said here to be the
  name macOS gives the app's settings, which it wasn't until "One settings file, named
  as the bundle ID"), version 1.8.0 from `snoop.h`, and an app icon made from
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
  (Fixed since: see "Where the marker list ends".)

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
line. (Ported since: see "The red line where the image ends inside its MCUs".)

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

### The toolbar's New button

The toolbar's New button and its key, Cmd+N (Windows' Ctrl+N), did nothing: the handler
was commented out. Now, as on Windows (MFC's File New, which emptied the document):
- **No file is shown, as at the start.** The report is emptied, the image window is
  hidden with its preview, and the histograms are emptied. The items that need a file
  are off again: Save Log, Reprocess, Offset, Image Search Fwd and Rev, Lookup MCU
  Offset, File Overlay, Export JPEG and TIFF, and Add Camera/SW to DB. So are Select All
  and Find, which need text. Search Executable for DQT, which needs no file, stays on.
- **The settings stay,** with File > Recent File and the zoom. The next file opened is
  shown as any is, without the last one's start offset or File Overlay.
- **The button** is first on the toolbar, with Windows' tip "Create a new document". It
  isn't in the menus, as on Windows.
- **Its key works while the toolbar is hidden, and in the image window,** as Windows'
  Ctrl+N did (the image was part of Windows' main window). Qt honours an action's key
  only in a window it is added to, and only while a button for it can be seen there, so
  the action is also on the main window and the image window.

**Changes from Windows:**
- **The image window is hidden again,** as at the start. On Windows the image was part
  of the main window.
- **The title:** Windows' said "Untitled". The port's title never names the file, so
  nothing changes there.

**Tests:** 12 new `NB` cases (958 in all).
- **The driver:** `gui --new STEP[,STEP...]` (`--new-at N`) prints where the button is
  (toolbar, position, tips, shortcut, menus) and what is shown: the report, the image
  window, the preview, the histograms, which items that need a file are on, the Edit
  items that need text, Search Executable for DQT, and File > Recent File's items. Its
  steps click the button, press its key in the main window or the image window (made
  the active window first), hide the toolbar, reprocess, and open a file.

The cases cover:
- **The button:** its place, tips and key, and that it is in no menu.
- **After it:** as at the start, also when used before any file; the histograms
  emptied; Reprocess doing nothing; the next file shown as any is.
- **The key,** in the main window and in the image window, also with the toolbar hidden.

On the previous commit 8 of them fail. The other 4 check what was already so: the start,
the histograms before it, and opening a file after it. Eleven deliberate breaks each fail
their own cases:
- **The button:** no handler, 7 fail; Windows' tip missing, 1 fails; the key not on the
  main window, 1 fails; the key not on the image window, 2 fail.
- **What it empties:** the report kept, 6 fail; the preview and histograms kept, 7 fail;
  the image window left shown, 6 fail; the items left on, 6 fail; the file kept, so
  Reprocess shows it again, 1 fails.
- **What it keeps:** Search Executable for DQT turned off, 8 fail; File > Recent File
  emptied, 7 fail.

**Second check:** a review before the commit found one problem, reproduced and now fixed.
Cmd+N did nothing while the image window was the active window, also with the toolbar
hidden: the action was only on the main window and its toolbar, and Qt honours a key
added to a window only while that window is active. The key cases didn't see it, as the
driver made the main window active first. The action is now on the image window too.
The driver's new `key-image` step makes the image window active, then presses the key;
it prints no state if the window can't be made active. Its 2 cases (above) fail on the
previous commit and with the image window's key removed.

**Checks:**
- **All 958 pass** with and without ASan/UBSan.
- **The app builds** with no new warnings.
- **cppcheck 2.22** finds nothing new in the changed files (its one finding in the new
  driver code, a loop that could be `std::count_if`, is fixed).
- **Not checked:** on the macOS platform, the toolbar button's look and Cmd+N, in either
  window.

**Still open:**
- **Windows' red line** where the image ends inside its last MCUs (see View > Overlays >
  MCU Grid).
- **For a decision:** Options > Check for Updates (Windows fetched a file over plain
  HTTP), Edit > Undo and View > Adjust (both greyed out; Windows' Adjust did nothing),
  and Options > Configuration's "Submit signature to Online DB", which does nothing.
- **Also:** Developer ID signing and notarization (M5). Latent code. Fill bytes before an
  RST marker in the report's marker list (see Tools > Lookup MCU Offset).

### The red line where the image ends inside its MCUs

A JPEG's image is decoded in whole MCUs, so one whose width or height isn't a whole
number of MCUs is shown with padding on the right or at the bottom. Windows marked where
the image's own size ends with a dotted red line (`CimgDecode::ViewOnDraw`). The Qt port
drew nothing there. Now, as on Windows:
- **Where:** down the column just right of the image's own width, as tall as its own
  height, and along the row just below its own height, as wide as its own width. So the
  line is on the first column and row of the padding, and not on the corner pixel between
  them (Windows' `LineTo()` stopped short of its end point).
- **At each zoom,** 12.5% too, with its place cut short to whole screen pixels as
  Windows' was: a 61x45 image's line is at column 91 and row 67 at 150%, not 92 and 68.
- **Whether or not the grid is on,** and under it: the grid's dots are drawn over the
  line where they cross it.
- **Only where it is needed:** an image that fills its last MCUs one way has no line
  that way, and one that fills them both ways has none. (On Windows that line was drawn
  on the image's frame, which then covered it; here it falls just off the image.) A
  Photoshop file's image has no MCUs and no line (Windows' size was 0 for it).
- **Its look:** red dots, one screen pixel each, on every third pixel counted from the
  image's top or left edge, as the MCU grid's are (also on a Retina screen). Windows
  drew it with its default opaque background mode, where the grid set a transparent one,
  so the two pixels between the red dots are white.

**Changes from Windows:** none. As the grid is, the line is drawn by the image window
(`PreviewCanvas`) from the decoder's frame size (`CimgDecode::GetFrameSize()`), not by
the decoder.

**Tests:** 32 new `IE` cases (990 in all).
- **The driver:** `gui --mcu-grid ...` gains `end`, which prints where the line is, and
  prints the same after every file. The report finds the line from what the image window
  draws, against the decoded image: the screen column down the right and the row along
  the bottom, the rows and columns each spans, the red dots' spacing and first place, how
  many pixels are red and white, and how many differ elsewhere. `buffer` also prints how
  many of the window's pixels are the line's red.
- **The driver's other checks:** `--check-preview` and the MCU grid's report now expect
  the line where the image ends inside its MCUs, so it doesn't count as a stray pixel.
- **Test images:** black ones, so the line's red and white pixels and the grid's dark
  grey ones all differ from the image.

The cases cover:
- **Its place and look:** 61x45 images inside MCUs of 8x8, 16x16 and a grayscale
  image's 8x8; ones that end inside their MCUs one way only, and one that fills them.
- **Every zoom** from 12.5% to 800%, and a Retina screen at 50%, 100% and 200%.
- **With the grid:** drawn over the line, unchanged by it, and none at 12.5% while the
  line stays.
- **Another channel,** and back.
- **Later files:** gone for an image that fills its MCUs and a Photoshop file, and back
  after them.
- **Scrolling** a 1021x1019 image to its end at 50%, 100% and 150%, with and without
  the grid, and making the window larger: the line is painted as painting the whole
  window again paints it. Also scrolling back a little with the line in view, where
  copied dots meet newly painted ones.

On the previous commit 28 of them fail. The other 4 check what was already so: no line
for an image that fills its MCUs, first or as the next file, or for a Photoshop file,
and the grid unchanged. All the MCU grid's cases still pass there with the new driver. Eleven
deliberate breaks each fail their own cases:
- **Not drawn,** 30 fail; **drawn only with the grid on,** 24 fail; **not at 12.5%,**
  2 fail.
- **Its place:** rounded rather than cut short, 5 fail (12.5%, 50%, 150%); on the image's
  last pixels, 45 fail; as long as the padded image, 19 fail; including the corner pixel,
  19 fail.
- **Its look:** solid red, 25 fail; no white between the dots, 22 fail; drawn over the
  grid, 1 fails; the dots counted from where the painting starts rather than from the
  image's edge, 3 fail (the scroll-back cases).

**Checks:**
- **All 990 pass** with and without ASan/UBSan.
- **The app builds** with no new warnings.
- **cppcheck 2.22** finds nothing new in the changed files.
- **Not checked:** the line on the macOS platform, on a real screen.

**Still open:**
- **For a decision:** Options > Check for Updates (Windows fetched a file over plain
  HTTP), Edit > Undo and View > Adjust (both greyed out; Windows' Adjust did nothing),
  and Options > Configuration's "Submit signature to Online DB", which does nothing.
- **Also:** Developer ID signing and notarization (M5). Latent code. Fill bytes before an
  RST marker in the report's marker list (see Tools > Lookup MCU Offset).

### Opening a file from outside the app: a drop, Finder, the command line

A file could only be opened with File > Open or File > Recent File: the port took no
file dropped on its window, Finder didn't offer it for any file, and it ignored the file
it was started with. Now, as on Windows:
- **A file dropped on the report** is opened (`CJPEGsnoopView::OnDropFiles()`): the
  first file, if several are dropped. A drag of text or a web address isn't taken, nor a
  drop anywhere else (the image window, the histograms, the toolbar): on Windows only the
  report took files.
- **A file named when the app is started** (`JPEGsnoopQt.app/Contents/MacOS/JPEGsnoopQt
  photo.jpg`, as Windows' `JPEGsnoop.exe photo.jpg`) is opened once the window shows: the
  last one, if several are named, as on Windows. A name is taken from the current folder
  (Windows: `AfxFullPath()`), and a ".." in it is read as macOS reads it (below).
- **Each is opened as File > Open opens one:** from its start, without the last file's
  File Overlay, put first in File > Recent File, and with the usual "Couldn't open file"
  message for one that can't be opened (a folder, say), which isn't added.

**Added for macOS: Finder.** Finder doesn't start another copy of the app with the file's
name, as Windows' Explorer did, but sends the file to the app (Qt's `QFileOpenEvent`):
- **Open With and a drop on the Dock icon** open the file as a drop does: the first, if
  several are sent. Finder sends one event for each file, one after another with nothing
  between them (seen with a small Qt app and `open -a`).
- **The main window comes forward,** out of the Dock if it was minimized there. A drop
  and the file named at the start don't move it.
- **Finder offers JPEGsnoop** in Open With for the types Windows' File > Open listed:
  JPEG, Thumbnail (`.thm`), AVI, QuickTime, DNG, the RAW formats (CRW, CR2, NEF, ORF,
  PEF), PDF and Photoshop (`CFBundleDocumentTypes` in `macos/Info.plist`). Each is
  "Alternate", so JPEGsnoop never becomes the app that opens them by default. `.thm` has
  no type of macOS's own, so it is named by its extension. (Windows registered no file
  types: there it was chosen with "Open with" > "Choose another app".)

**When:** a file handed over waits until the app is free.
- **While a dialog is open** (a message, a batch's progress, the license terms at the
  first start, any of the app's dialogs): Finder's file waits and is opened once the
  dialog closes; a drop isn't taken (on Windows a dialog disabled the window behind it).
  A later file replaces one still waiting.
- **While a file is being decoded:** the decoder lets the window repaint as it goes, and
  the events handled then include Finder's (seen with the small Qt app, during
  `processEvents(ExcludeUserInputEvents)`), and timers. Opened then, the new file would
  start in the middle of the other one's decode: with that check removed, the test
  driver crashed (a segmentation fault). It now waits until the decode is done. The two
  places the decoders let the window repaint call `ProcessWorkEvents()` (`General.cpp`),
  which says while they are running. On Windows the decode handled no events, so a drop
  waited.
- **In File > Print and Print Setup:** Finder's file waits until the report is printed,
  or Print Setup is closed. Qt runs these two macOS panels without a modal Qt window
  (`qprintdialog_mac.mm`, `qpagesetupdialog_mac.mm`), so the check for a dialog doesn't
  see them. It also handles the app's events just before showing Print's panel, and
  timers fire while either panel is up. Opened then, the new file's report would be the
  one printed. File > Open's panel, the save panels and the messages are seen as
  dialogs: for those, Qt marks the dialog behind the panel as modal (checked with a
  small Qt app on the macOS platform, with a message, File > Open's panel and a save
  panel shown as a sheet).

**Changes from Windows:**
- **Finder (above):** new, as macOS hands files over differently.
- **Windows' options** (`-i`, `-o`, `-b`, `-br`, `-scan` and the rest), which ran it
  without its window and saved the report, aren't ported. With any argument that starts
  with "-", nothing is opened and the app starts as usual (Windows ran its command-line
  mode then, or showed its help). An empty argument is skipped. Windows also took "/" for
  an option; on macOS it starts a path.
- **".." in a name given at the start** is read as macOS reads it. When the folder
  before it is a symbolic link, macOS goes up from where the link leads, not from the
  folder holding the link. Windows' `AfxFullPath()` dropped "folder/.." from the name
  alone, which is how Windows reads it, and so do Qt's `QFileInfo::absoluteFilePath()`
  and `QDir::cleanPath()`. The part up to the last ".." is resolved first (`fullPath()`
  in `mainwindow.cpp`), so the report and File > Recent File name the file macOS opens.
  A name whose folder before ".." doesn't exist can't be opened, as macOS can't open
  it either; dropped from the name alone, it would have opened another file. "." is
  dropped, as on Windows.
- **A drag is taken only as a copy,** so the app it comes from never moves the file; a
  drag that only offers a move isn't taken. (Windows' drops were copies.)

**Unchanged from Windows:** the report the only place a file can be dropped; the first
file of a drop; the last file named at the start; the open itself, its message and File >
Recent File.

**Tests:** 57 new `OF` cases (1,047 in all).
- **The driver:** `gui --outside STEP[,STEP...]` (`--outside-at N`) prints, after each
  step, the file the report is of and its lines, File > Recent File's files, whether the
  main window is minimized, and which window is active.
  - **Drops** go through Qt's own entry points for a drag from another app
    (`QWindowSystemInterface::handleDrag()` and `handleDrop()`), so Qt finds the widget
    under the drop, and refuses a window behind a dialog, as for a real drag. The driver
    prints what the drag and the drop were answered.
  - **Finder's files** go through `handleFileOpenEvent()`, as Qt's macOS code sends them.
  - **The arguments** go through `MainWindow::openArguments()`, which `main()` calls.
  - **`in-dialog=`** hands a file over while a dialog is open. **`in-decode=`** does it
    while a file is being decoded, at the decoder's first repaint, with a 5 MB file whose
    decode repaints more than once. It prints whether that came during the decode, and
    whether the report just after is the same as the file's first one.
  - **`in-print=` and `in-setup=`** choose File > Print or Print Setup and hand a file
    over while the panel is up, as Qt runs it on macOS: the Qt dialog shown but never
    on the screen and not modal, and, for Print, the app's events handled just before
    the panel. No panel is shown and nothing goes to a printer: Print prints to a PDF,
    and Print Setup's panel is cancelled. The driver prints whether a dialog was modal
    then (none, as on macOS), whether the report is the same when the panel closes, and
    the PDF's pages.
  - **`paths`** prints the whole path of the report's file and its size, and File >
    Recent File's paths, from the cases' folder.

The cases cover:
- **A drop:** the file opened and listed, taken as a copy; the first of two; before any
  file; an empty file; a name with a space, an accent and "&"; a folder (the message, not
  listed). Not taken: a move only, text, a web address, on the image window, on the
  histograms, behind a dialog. The window not brought forward.
- **Finder:** the file opened and listed; the first of two; before any file; the name; a
  web address ignored. The main window brought forward from behind the image window, and
  out of the Dock. Behind a dialog: waiting, then opened; a later file replacing it.
- **During a decode,** from Finder and dropped: the decode finishes as it would have,
  then the file opens.
- **In File > Print and Print Setup,** from Finder: the report unchanged while the panel
  is up and when it closes, the PDF printed, then the file opened.
- **At the start:** the file; the last of two; a name from the current folder; the name
  with an accent; an option first or later (nothing opened); an empty argument, alone
  and after a file; a file that can't be opened (the message, not listed); the window
  not brought forward.
- **".." after a symbolic link** to another folder, with a different file of the same
  name beside the link: the one where the link leads opened and listed, also from the
  current folder, with "." after "..", and with a second ".." before the link; "." and
  "//" dropped; ".." after a folder that doesn't exist (the message). File > Recent File
  also lists a file opened with ".." in its name by where it is.

On the previous commit 35 of them fail. The other 22 check what was already so: the
drops that aren't taken and what they leave, a folder and a file that can't be opened
left out of File > Recent File, Finder's web address, nothing opened behind a dialog or
during File > Print and Print Setup (that commit took no file from Finder), the PDF
printed, nothing opened for an option or an empty argument, and the driver's own
minimize and image window steps. Two faults in the new cases were found and fixed while checking
them:
- **The case for a file named at the start not bringing the window forward** passed
  either way: the image window takes the focus when it first shows. The case now makes
  the image window active after a file is open, then names the file.
- **Skipping an empty argument** was tested only alone, where nothing opens either way;
  a break showed it. A new case puts it after a file, which must still open.

Twenty-one deliberate breaks each fail their own cases:
- **A drop:** not taken, 9 fail; the last file, not the first, 1 fails; a move taken,
  2 fail; a web address taken, 1 fails.
- **Finder:** not watched for, 9 fail; each of several files opened, 1 fails; not
  brought forward, 3 fail; not out of the Dock, 1 fails; a drop and the arguments
  brought forward too, 2 fail.
- **When:** opened at once, 9 fail; not waiting for a dialog, 2 fail; not waiting for a
  decode, 4 fail (the driver crashed); the decoders' repaints not counted, 4 fail; the
  image decoder's repaints not through `ProcessWorkEvents()`, 4 fail; a later file not
  replacing one waiting, 1 fails.
- **The open:** not as File > Open (left out of File > Recent File), 22 fail.
- **At the start:** nothing opened, 7 fail; the first file, 1 fails; options not
  checked, 2 fail; an empty argument kept, 1 fails.

**Second check:** a review before the commit found two problems, each reproduced and now
fixed:
- **Print could print another file's report.** A file from Finder could be opened while
  File > Print's panel was up, which the check for a dialog doesn't see (above), and the
  panel then printed the new file's report. Print and Print Setup now hold any file
  handed over until they are done. Before the fix, the driver's report changed to the
  new file during the panel, and the report printed was the new one.
- **".." after a symbolic link opened the wrong file.** A name given at the start was
  made whole with `QFileInfo::absoluteFilePath()`, which drops "alias/.." from the name
  alone. With `alias/../target.jpg`, where `alias` leads to another folder, it opened the
  `target.jpg` beside the link (or said it couldn't open one), not the one macOS opens.
  File > Recent File did the same with a file opened with ".." in its name. Both now go
  through `fullPath()` (above).

These added 13 of the cases above, and 9 of them fail without the fixes. The other 4
check what was already so: "." dropped, the file opened once Print and Print Setup are
done, and the PDF printed. Nine more deliberate breaks each fail their own cases:
- **Print:** the hold not checked, 4 fail; not set by Print, 2 fail; not set by Print
  Setup, 2 fail.
- **".."**: dropped from the name alone at the start, 4 fail; in File > Recent File,
  1 fails; resolved only up to the first "..", 1 fails; a name that can't be resolved
  dropped from the name alone, 1 fails; "." not dropped from a name without "..",
  1 fails; nor after "..", 1 fails.

**Checks:**
- **All 1,047 pass** with and without ASan/UBSan.
- **The app builds** with no new warnings, and the built app's `Info.plist` has the
  eight document types (`plutil -lint` passes). An existing build keeps its old
  `Info.plist`: qmake's rule for it doesn't depend on `macos/Info.plist`, so it needs
  `make -B JPEGsnoopQt.app/Contents/Info.plist` (or a fresh build).
- **cppcheck 2.22** finds nothing new in the changed files (its one finding in the new
  code, a loop that could be `std::find_if`, is fixed).
- **Not checked** on the macOS platform:
  - with the app itself: a real drag from Finder, Open With, and a drop on the Dock icon
    (only the small Qt app was tried, which got the files Finder sends);
  - Finder listing JPEGsnoop for each type, and the `.thm` entry (the app isn't
    registered with Launch Services here);
  - the real Print and Print Setup panels, which are never shown here (the driver runs
    them as Qt does, without the panel), and whether macOS sends Finder's file while
    one is up (the file waits either way).

**Still open:**
- **Not yet ported:** Windows' command-line mode (its options, above), and the toolbar's
  Copy and About buttons. File > Open's list of types also lacks Windows' "AVI Movie".
- **For a decision:** Options > Check for Updates (Windows fetched a file over plain
  HTTP), Edit > Undo and View > Adjust (both greyed out; Windows' Adjust did nothing),
  and Options > Configuration's "Submit signature to Online DB", which does nothing.
- **Also:** Developer ID signing and notarization (M5). Latent code. Fill bytes before an
  RST marker in the report's marker list (see Tools > Lookup MCU Offset).

### The toolbar's Copy and About buttons

Windows' toolbar had Copy and About buttons; the port's had neither. Now, as on Windows:
- **Copy** copies the part of the report that is selected, and is off while none is
  (MFC's `CRichEditView` command, on only with a selection: `OnUpdateNeedSel()`). It is
  Edit > Copy itself, so it puts the same plain text and HTML on the clipboard.
- **About** opens the About box: it is Help > About JPEGsnoop itself.
- **Their tips are Windows':** "Copy the selection and put it on the Clipboard" and
  "About JPEGsnoop" in the status bar, and "Copy" and "About" as their tooltips.
- **Their places are Windows':** New, Open, Save Log | Copy | Print, About, with the
  separators where Windows had them. The image's zoom, which this version adds, follows
  as before. (Before, Print followed Save Log with no separator.)

**Changes from Windows:**
- **No Cut or Paste buttons.** Windows had them beside Copy, but both would change the
  report, which is read-only there (`SetReadOnly(true)` in `CJPEGsnoopView`) and here.
- **The icons** are macOS's own symbols on macOS, as for the other buttons (Qt's theme
  names `edit-copy` and `help-about`). Without the theme's icon, Copy has the image set's
  `editcopy.png`, and About the app's own art, as the About box shows it.

**Unchanged from Windows:** what each button does, when Copy is on, and the other
buttons.

**Tests:** 10 new `TB` cases (1,057 in all).
- **The driver:** `gui --buttons` prints the toolbar's buttons in order, each by its
  tooltip, and for each its tip, key, menus, whether it is on and whether it has an
  icon. `--about-button` opens the About box with the button, as `--about` does with the
  menu item, and `--edit`'s `button:copy` step clicks Copy.
- **The cases:** the toolbar's order and separators; each button's tips, key, menu and
  icon, and Copy off with nothing selected; Copy clicked with the whole report or a part
  selected (the same clipboard text and formats as Edit > Copy), and with nothing
  selected, also before any file (nothing copied); About opening the About box, which
  then closes.

On the previous commit all 10 fail. Eleven deliberate breaks each fail their own cases:
- **Copy:** not on the toolbar, 7 fail; a button of its own that is always on, 6 fail;
  no tip, 1 fails; no icon without the theme's, 1 fails.
- **About:** not on the toolbar, 4 fail; before Print, 1 fails; no tip, 1 fails; the
  menu item's name as its tooltip, 2 fail; no icon without the theme's, 1 fails.
- **Both:** no separators, 1 fails.

**Checks:**
- **All 1,057 pass** with and without ASan/UBSan.
- **The app builds** with no new warnings.
- **cppcheck 2.22** finds nothing new in the changed files.
- **On the macOS platform,** Qt's theme gives both buttons an icon, as it does New's and
  Zoom In's (checked with a small Qt app, which showed no window).
- **Not checked:** the toolbar's look in the app, and its tooltips and status bar tips,
  on the macOS platform.

**Still open:**
- **Not yet ported:** Windows' command-line mode (its options; see "Opening a file from
  outside the app"). File > Open's list of types also lacks Windows' "AVI Movie".
- **For a decision:** Options > Check for Updates (Windows fetched a file over plain
  HTTP), Edit > Undo and View > Adjust (both greyed out; Windows' Adjust did nothing),
  and Options > Configuration's "Submit signature to Online DB", which does nothing.
- **Also:** Developer ID signing and notarization (M5). Latent code. Fill bytes before an
  RST marker in the report's marker list (see Tools > Lookup MCU Offset).

### Removed: Check for Updates, Edit > Undo, View > Adjust, Submit signature

Four items kept from Windows did nothing in the port, and what two of them were for is
no longer relevant: Windows checked impulseadventure.com for a new version, and could
post compression signatures to an online database there. All four are removed:
- **Options > Check for Updates...** had no handler. (Windows fetched a page over plain
  HTTP.)
- **Edit > Undo** was always greyed out: the report is read-only. Cmd+Z still does
  nothing in it.
- **View > Adjust...** was always greyed out. (Windows' did nothing.)
- **Options > Configuration's "Submit signature to Online DB when User submit"** sent
  nothing: Tools > Add Camera/SW to DB adds to the user database only.

The rest of the update check goes with them, since nothing used it:
- **Configuration's "Software Update" group,** "Automatically check for updates every N
  days".
- **The license terms' "Software Updates" group,** "Periodically check for updates",
  shown at the first start.
- **The settings** `UpdateAuto`, `UpdateAutoDays`, `UpdateLastChk` and `DbSubmitNet`
  (Windows' names), which are no longer read or written. Any already stored are left as
  they are, unused.
- **The decoder's code that built a signature entry and posted it** to the online
  database (`PrepareSendSubmit()` and `SendSubmit()`, about 370 lines, never called), and
  the web addresses and Windows Internet switches in `snoop.h` that only it used.

What remains closes up:
- **Edit:** Copy, Select All | Find..., Find Next.
- **View:** Image Chan, Image Zoom, Overlays | Toolbar, Status Bar.
- **Options:** ends with Configuration. On macOS, where Configuration is the app menu's
  Preferences item (M3), Options now ends with Scan Segment: Qt hides the separator left
  at the end (`QCocoaMenu::syncSeparatorsCollapsible()`).
- **Configuration and the license terms** are shorter by the groups removed; the other
  groups keep their sizes. Processing Options, now one checkbox, is as tall as Report
  Settings.

**Tests:** 7 new `RM` cases (1,063 in all; `S5_update_date_format`, which checked the
update check's stored date, goes with it).
- **The driver:** `gui --menus` prints each menu's items. The `settings` mode prints
  Configuration's groups, their heights, the room below them and its checkboxes; the
  license terms' groups, checkboxes and the room below them; and which of the four
  settings are stored. `--edit`'s state no longer lists Edit > Undo.
- **The cases:** the Edit, View and Options menus as above; none of the removed items in
  any menu; Configuration's groups, heights, room below and checkbox; the license
  terms'; none of the four settings stored. The case for View > Toolbar's place now has
  it after Overlays.

On the previous commit the 7 new cases and the View > Toolbar case fail. Eight
deliberate breaks each fail their own cases:
- **Menus:** Undo kept, 2 fail; the separator after it kept, 1 fails; Adjust kept, 3
  fail; Check for Updates kept, 2 fail.
- **Dialogs:** Processing Options left to stretch, 1 fails; Configuration left at its old
  height, 1 fails; the license terms left at theirs, 1 fails.
- **Settings:** one of the four still stored, 1 fails.

**Checks:**
- **All 1,063 pass** with and without ASan/UBSan.
- **The app builds** with no new warnings, compared with the previous commit's whole
  build (12 fewer, with the code removed).
- **cppcheck 2.22** finds nothing new in the changed files (10 fewer).
- **Both dialogs** were drawn offscreen before and after, to see them close up.
- **Not checked:** the two dialogs and the menus on the macOS platform.

**Still open:**
- **Not yet ported:** Windows' command-line mode (its options; see "Opening a file from
  outside the app"). File > Open's list of types also lacks Windows' "AVI Movie".
- **Also:** Developer ID signing and notarization (M5). Latent code. Fill bytes before an
  RST marker in the report's marker list (see Tools > Lookup MCU Offset).

### Command-line mode

Windows' options ran JPEGsnoop without its window: `JPEGsnoop.exe -i photo.jpg -o
photo.txt` analyzed the file, saved its report, and quit. The port ignored them: with
any argument starting with "-" it opened its window with nothing in it. Now started with
any option, it runs as Windows did (`CMyCommandParser`, `CJPEGsnoopApp::DoCmdLineCore()`
and `CmdLineHelp()`), from `JPEGsnoopQt.app/Contents/MacOS/JPEGsnoopQt`:
- **`-i FILE`** analyzes the file, and **`-o LOG`** saves its report to LOG: the report
  File > Open shows, the same as Save Log and Batch Process write. A file named without
  an option is taken as `-i`'s, as on Windows, and the last one named counts. With no
  option, the window opens the file, as before.
- **`-b DIR`** analyzes each `.jpg` and `.jpeg` file in the folder (**`-br`** also in its
  subfolders), saving each one's report next to it as `NAME.txt`, as File > Batch Process
  does with the folder as both input and output. `-o` isn't used. With both `-i` and
  `-b`, `-i` counts.
- **`-ext_all`** also extracts every JPEG in the file (with `-b`, in each file of any
  type), named from `-o`'s log without its extension, or else from `FILE.export`:
  `photo.000001.jpg`, ... (Windows' names.)
- **`-scan`, `-maker`, `-scandump`, `-histo_y`, `-dhtexp`, `-exif_hide_unk`** turn
  those options on for the run, over the saved settings, which are left as they were.
  The saved settings apply otherwise (Options and Scan Segment as last set in the
  window).
- **`-offset_start`, `-offset_srch1`, `-offset_srch2`, `-offset_pos N`** say where each
  file is decoded from: its start; the first SOI in it (the start if an image decodes
  there); the first SOI after the start; byte N. Extract All starts at the start.
- **`-help`, `-h`, `-?`** print Windows' command summary, and **`-done`** says "JPEGsnoop
  operations complete" at the end.
- **The value after `-i`, `-o`, `-b`, `-br`** is taken even if it starts with "-", as on
  Windows. Options are case-sensitive, as on Windows.

Windows wrote the summary, its errors and `-done` to the console it was started from;
here they go to the standard output (the summary and `-done`) and the standard error (the
errors). Otherwise nothing is written to either.

**Not as on Windows:**
- **No window, ever:** an unknown option is an error on the standard error, not a
  message box; and if the license terms haven't been accepted, the error says so ("Open
  JPEGsnoop and accept them first") and nothing is done, instead of showing them. The
  summary needs no license.
- **Errors Windows ignored are errors,** with the exit status 1 (0 if everything worked):
  an option missing its value (Windows ignored it), an offset that isn't a decimal number
  below 2^32 (Windows' `_ttoi()` made it 0), no file or folder named (Windows did
  nothing), a log that would replace the file analyzed (Windows only checked in its debug
  build; also through a link), a file or folder that can't be read (Windows skipped it
  silently in a batch), a file that isn't decoded because it is empty or over 4 GiB
  (Windows only said so in the report), and a log or export that can't be written. A
  file that can't be opened still gets its log with the error in it, as on Windows.
- **A batch's files are kept:** no log or export replaces one of them (with `-ext_all`,
  every file in the folder is one), before or after it is analyzed, nor by a name in
  another case, nor the file one of them is a link to, nor a link on the way to it (in
  its folders' names too). With `-i`, nor is the file analyzed. It is an error: that
  log isn't written, or the export stops there.
  Windows replaced it, and then analyzed what had replaced it. So a second `-b DIR
  -ext_all` run over the same folder reports each of the first run's files it would
  replace, which are now among the batch's.
- **An offset past the end of a file** is an error, in its report too (as File > Offset
  refuses it). Windows decoded it as if 0x00 bytes were there.
- **`-ext_dht_avi`** adds the standard Huffman tables a frame lacks, as Tools > Export
  JPEG's "Insert MJPEG DHT" does; on Windows the option was read but did nothing.
  Without it, Extract All adds them to a Motion JPEG AVI's frames, as Export JPEG does by
  default (Windows added none).
- **Nothing is written but the logs and exports:** not the settings (Windows saved them
  when something had changed them, as at its first start, options and all), nor the user
  database's folder, which the window makes at its first start. A missing folder in
  `-o`'s path is made, as for a batch's logs.
- **The Dock:** the report is a Qt text widget, so the run needs `QApplication`, which on
  macOS makes the app a foreground app with a Dock icon. Before `QApplication` is made,
  `prepareCmdLineApp()` (`cmdline_mac.mm`) sets the app's activation policy to
  "prohibited" and stops Qt making it foreground
  (`QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM`), so it never shows in the Dock, the
  app switcher or the menu bar. Qt's own debug messages (`qDebug()` in the decoders) are
  dropped; its warnings are kept.
- **Arguments are read before Qt starts,** for the Dock, and Qt is then given none of
  them (`hideArgsFromQt()`): it read them as its own options, so a file named
  `-platform` made it take `-o` for a platform plugin and stop the app. Qt's options
  (`-style` and the rest) are unknown options here. macOS's old `-psn_...` argument is
  ignored.

**Code:** `cmdline.h`/`.cpp` parse the arguments and run them. Each file goes through
`CBatchProcess::process()`, File > Batch Process's, which now takes the offset mode, the
forced Huffman tables, a log name that may be empty (no `-o`) and whether to log a file
that can't be opened. `CSnoopConfig` loses Windows' unused command-line fields, and
`RegistryLoad(false)` neither shows the license terms nor makes the database folder.
The report's "File too large" line now gives the size in hex, as Windows' did: the port
put the decimal size after "0x" (in the window's report too).

**Tests:** 120 new `CL` cases (1,183 in all).
- **The driver:** `cmdline` runs the command-line mode as `main()` does (from the raw
  arguments), in a temporary folder with copies of the files, and prints the exit status,
  the files there (each with its size, MD5, DHT markers and whether the run made or
  changed it), each line written to the standard output and error, each log's lines, and
  which saved settings changed. `--put`, `--link`, `--mkdir`, `--chmod`, `--sparse` (a
  file of any size that takes no room on the disk), `--setting` and `--no-eula` set the
  folder and settings up first. As `main()` does, it gives Qt none of the arguments.
- **The cases:**
  - **Arguments:** the summary (also with `-h` and `-?`, and before a file); each error
    above, with the summary after it and nothing done; only file names open the window,
    and `-psn_` is ignored.
  - **`-i`:** the log, with the file's name as given; a file and a log whose names start
    with "-"; a name with accents; the last file named; `-i` over `-b`; a log in a new
    folder; an empty file and one of 4 GiB (errors, also without `-o`, and the
    window's report of the latter); a file that is missing, unreadable or a folder (its
    log says so); a log that would replace the file, by its name or through a link (the
    file is kept); a log that can't be written.
  - **Options:** each of the six on and off, over saved settings that turn it off; a
    saved setting applying; no setting changed.
  - **Offsets:** each mode, the last one given counting, the report decoded once, an
    offset past the end and one at the last byte.
  - **Extract All:** the names from the file and from the log, the exports byte for
    byte, a Motion JPEG file's frames with and without `-ext_dht_avi`, an AVI's frames,
    `-ext_dht_avi` alone, a missing file, a folder that can't be written.
  - **Batches:** with and without subfolders and Extract All, the logs' names and
    contents, `-o` unused, an offset, a missing folder, an unreadable folder and file, a
    file of 4 GiB; a batch file kept from another's log, export, and export by a name in
    another case, while the rest is written.
  - **Qt:** a file named `-platform`, analyzed and with `-help`.
  - **`-done`,** also after an error, and **the license check.**

On the previous commit (with its `main()`: any option opens the window, with nothing in
it), 118 fail; the 2 that open the window pass, as nothing changed there. 24 deliberate
breaks each fail their own cases:
- **Arguments:** options not leading to command-line mode, 101 fail; `-psn_` taken for
  an option, 1; a value starting with "-" refused, 2; `-h` unknown, 1; an unknown option
  ignored, 4; a missing value ignored, 3; no file named ignored, 2; a junk offset read as
  0, 1.
- **`-i`:** the log allowed over the file, 3; no log for a file that can't be opened, 2;
  Extract All always named from the file, 2; `-b` over `-i`, 1; the exit status always 0,
  4.
- **Options:** not applied, 7; saved, 2; `-histo_y` without the RGB histogram, 2.
- **Offsets:** `-offset_srch1` searching past an image at the start, 1; no second decode
  after a search, 3; an offset past the end decoded, 3.
- **Extract All, batches and the rest:** `-ext_dht_avi` ignored, 1; `-br`'s subfolders
  not searched, 3; a batch's file errors left out of the exit status, 1; `-done` not
  said, 2; the license terms not checked, 2.

(A first version of the `-h` and `-?` cases didn't catch `-h` being unknown: an unknown
option shows the summary too. They now check for no error and the exit status 0.)

**Second check:** a review before the commit found three problems, each reproduced and
now fixed (above):
- **A batch could replace its own files.** With `-b . -ext_all`, the first file's export
  `a.jpg.export.000001.jpg` replaced another file of that name in the folder, which the
  batch then analyzed, and it exited 0. The batch's files are now kept from every log and
  export: their folder entries (device and inode, not following a link) are noted when
  the files are listed.
- **Qt read the app's arguments.** `-i -platform -o log.txt -help` stopped the app (exit
  134: "Could not find the Qt platform plugin "-o""). Qt now gets the program's name
  alone.
- **A file over 4 GiB was skipped quietly:** `-i FILE -ext_all -done` exited 0, said
  "operations complete", and wrote nothing. It is now an error on the standard error and
  in the exit status, as is an empty file.

These added 17 of the cases above, all of which fail on the previous commit (the two
Qt cases stop with exit status 134). Eight more deliberate breaks each fail their own
cases: the batch's files not kept, 6 fail; kept from the exports only, 2, or the log
only, 4; compared by name, 1 (the other case); Qt given the arguments, 2; a file not
decoded not an error, 7; an empty file not an error, 2; the size in decimal, 4.

**Third check:** a review of the commits through the previous one found two more ways
past that fix, both through links, and cases that hung without a printer. Each was
reproduced and is now fixed:
- **The file a link leads to wasn't kept.** With `a.jpg` a link to `a.jpg.txt`, which
  held the image, `-b .` wrote `a.jpg`'s report over `a.jpg.txt` and exited 0: the batch
  had noted the link's entry, not the file's. Each file's path is now followed as macOS
  follows it (`PathEntries()` in `General.cpp`), and every entry on the way is kept: the
  link, each link it leads through (also one to a folder, with `..` after it going up from
  where that led), and the file at the end. They are noted before anything is written.
- **An export took the place of the link the file was read through.**
  `-i frame.000001.jpg -o frame.txt -ext_all`, with `frame.000001.jpg` a link to the
  image, removed the link to write the first export there (as it removes a link of an
  export's name that leads elsewhere), and Export JPEG's check for the file analyzed then
  found nothing there to refuse. Before removing anything, Extract All now checks each name
  against the file analyzed's entries, and so does the log (`keepPaths()` in
  `batchprocess.cpp`). That also covers the window's Extract All, which opens a file by
  its link's name, and File > Batch Process: a batch's log isn't written over the file it
  reports on, even after Replace. (The other names a batch writes are still replaced when
  asked, links included.) An export by the file's own name now says "Couldn't write [NAME]:
  it would replace the file analyzed" too, in place of Export JPEG's "Can't overwrite
  source file". A hard link of the file is kept as well: its inode can't tell it from the
  file's own name in another case. (Before, a log or export by such a name replaced the
  hard link and left the file.)
- **Three Finder cases hung without a printer.** With none set up, Qt keeps the shared
  printer a PDF one, so File > Print Setup shows this version's own dialog, which is
  modal, instead of macOS's panel, which the driver answers. The cases waited out their time
  limit. The driver now checks the printer: without one, `in-setup=` prints `harness:
  skipped: ...`, which `run-tests.sh` lists as `SKIP` and counts apart, not as a failure.
  A new step, `in-pdf-setup=`, answers the PDF printer's dialog, on any Mac. Run with no
  printers, the whole suite also showed that the File > Print Setup cases that start
  with `printer=native` never had the system's printer: two failed and four passed by
  chance. That step now skips its case the same way.

These added 21 cases (1,204 in all):
- 14 `CL` cases, for links;
- the window's Extract All through such a link (2 `E`);
- a batch image that is a link to its own log's name (2 `BP`);
- Print Setup's own dialog with a file from Finder (3 `OF`).

On the previous commit, 18 of them fail. The 3 `OF` cases pass, as the app already
handled that dialog; only the driver didn't. With this Mac's two printers hidden from Qt
(`CUPS_SERVER` set to a socket that doesn't exist), the three Print Setup panel cases
time out on the previous commit, and two `PS` cases fail. Now those 3 and 6 `PS` cases
are skipped, and the rest pass. Eleven deliberate breaks each fail their own cases:
- the file at the end not kept, 13 fail;
- the links not kept, 11;
- following stopped at the first link, 10;
- `..` taken from the name (as `QFileInfo::symLinkTarget()` does), 1;
- each file kept by its own entry only (the previous commit's way), 12;
- the link removed before the check (the bug), 5;
- the file analyzed not checked by Extract All, 6, or by the log, 4;
- a file from Finder opened during any dialog, 10;
- the printer check left out, 3 (they time out with the printers hidden).

**Checks:**
- **All 1,204 pass** with and without ASan/UBSan. With the printers hidden, 1,195 pass
  and 9 are skipped.
- **The app builds** with no new warnings (the changed files built one at a time, with
  the previous commit's).
- **cppcheck 2.22** finds nothing new in the changed files (it can't read
  `cmdline_mac.mm`, which the driver doesn't run either: the next check covers it).
- **The app itself,** run from its bundle in Terminal: the summary (also after `-i
  -platform -o log.txt`), an unknown option, a bad offset and the license check give the
  right output and exit status, with nothing
  else on either stream, and nothing written to the settings or the Library. Polling
  `lsappinfo` caught 2 of 6 short runs, both as a background-only app. (A small Qt app in
  a bundle with only the variable set was a foreground one; with the policy set too,
  before or after `QApplication`, a background-only one.)
- **Not checked:** a run with accepted license terms in the app itself (this Mac's app
  has none, so only the driver ran files through it, the same code); a run from a login
  without the window server (over SSH, say).

**Still open:**
- **Not yet ported:** File > Open's list of types lacks Windows' "AVI Movie".
- **A link that leads nowhere** when a batch starts isn't kept: if one of the batch's
  logs or exports then gives it something to lead to, it is analyzed as it is found then.
  No file is lost.
- **Also:** Developer ID signing and notarization (M5). Latent code. Fill bytes before an
  RST marker in the report's marker list (see Tools > Lookup MCU Offset).

### The tests: cases that didn't test what they said

A review of the test suite found six places where a case could pass without checking
what it claimed. None was a fault in the app, but each would have hidden one:
- **The command line through the app itself.** The driver builds the app's sources
  without `source/main.cpp`, and hides the arguments from Qt itself. So the two `-i
  -platform` cases would still pass if `main()` stopped hiding them. `run-tests.sh` now
  builds the app too (in `app/` beside the driver), and `cmdline --app` starts it as one
  does in Terminal (not offscreen). The four new cases (`CL_app_*`) check the summary and
  an unknown option after `-i -platform -o log.txt`, with the exit status. They run only
  what reads no settings: a run with a file reads the license terms' setting, which would
  be your own. (`CFFIXED_USER_HOME` doesn't move them. That was first tried with the
  wrong name for them: see "Correction: the app's settings file" below.)
- **The native printer.** On a Mac with no printer set up, `printer=native` left Qt's
  printer a PDF one, so two cases failed and others passed with the wrong printer. That
  step now checks, as the Finder cases do, and skips the case (see "Third check" under
  Command-line mode).
  `PS_native` also took the printer's paper to be US Letter: it now chooses A4 first, and
  `PS_native_paper` checks that A4 took.
- **`--only`.** A misspelt pattern, or one that isn't a valid regex (`[`), ran no case
  and exited 0. Both are now errors (exit status 2). A selection whose cases are all
  skipped still runs and says so.
- **Two print jobs.** `PS_landscape_again` printed twice but checked only that one
  landscape PDF line was there, which the first job wrote even when the second printed
  nothing (`pdf=none`). A step may now end in `#LABEL`, which is printed with it, and
  `PS_landscape_again_1` and `_2` check `print#1` and `print#2`.
- **Progressive and CMYK files** were listed among the files that must decode cleanly,
  but their cases checked nothing (each would pass with no report at all). They now
  check what happens: the note that the scan isn't decoded ("Scan parsing doesn't support
  this SOF mode" and "... CMYK files yet"), no preview and no error, and for the
  progressive file no histogram.
- **The other-case export on a case-sensitive disk** printed its own "skipped" line,
  which counted as a pass. It now prints `harness: skipped: ...`, so it is counted as a
  skip. (Checked on a case-sensitive disk image.)

9 new cases (1,213 in all). They test the suite rather than the app, so they pass on the
previous commit too. Five deliberate breaks each fail their own cases:
- Qt given the arguments in `main()`, 4 fail (the app stops: "Could not find the Qt
  platform plugin "-o"", status `crash`), while the in-process `CL_qt_*` cases still
  pass;
- `main()` reading the arguments from the second, 2;
- only the first print job printed, 1 (`PS_landscape_again_2`; the old case passed);
- Print Setup's panel putting the paper back to US Letter, 2;
- the progressive and CMYK notes left out, 2.

**Checks:** all 1,213 pass with and without ASan/UBSan; with the printers hidden, 1,203
pass and 10 are skipped (the native printer's 7 and the Finder cases' 3). The changed `run-tests.sh` passes ShellCheck, and cppcheck 2.22
finds nothing new in the driver.

### File > Open's AVI Movie, and the status bar's "1234"

The last of Windows' features still to port, and a leftover found while looking for any
others. Now:
- **File > Open's types include "AVI Movie (*.avi)",** after Thumbnail, as on Windows.
  AVI files already decoded, but could be found only under All Files.
- **The main window's status bar shows only its messages.** It also had a label at its
  right that always said "1234", a placeholder from the first Qt port that nothing
  changed.

**Unchanged from Windows:** the other types and their order, and JPEG Image chosen at
first, so an AVI is offered after choosing AVI Movie or All Files. The MCU, file position
and YCC stay in the image window's status bar (see View > Toolbar and Status Bar).

**Tests:** 9 new cases, 5 `OD` and 4 `VB` (1,222 in all).
- **The driver:** `gui --open CHOICE[+CHOICE...]` chooses File > Open and answers its
  dialog, which no case had used. `--bars`' new step `widgets` lists what each status bar
  holds besides its messages.
- **The cases:** the types and their order; JPEG Image chosen at first, an existing file
  only, read-only; cancel keeping the file shown; an AVI opened through the dialog, also
  before any file; the main window's status bar with no widgets, also before any file and
  hidden and shown again; the image window's three labels.

On the previous commit 4 of them fail (the types, and the main window's 3). The other 5
check what was already so. Eleven deliberate breaks each fail their own cases:
- **The types:** AVI Movie left out, after QuickTime, or with another pattern, 1 each;
  chosen at first, 1.
- **The dialog:** any file name allowed, 1; not read-only, 1; the file chosen not opened,
  2; cancel clearing the report, 1.
- **The status bars:** an empty label kept in the main window's, 3; "1234" beside the
  messages, 3; the image window's file position left out, 1.

**Checks:**
- **All 1,222 pass** with and without ASan/UBSan. With the printers hidden, 1,212 pass
  and 10 are skipped, as before.
- **The app builds,** and the driver's whole build (the app's sources but the unchanged
  `main.cpp`) has no new warnings.
- **cppcheck 2.22** finds nothing new in `mainwindow.cpp`. In the driver it adds one
  false positive, as three others there: `!bShown` "always true", where a timer sets it
  while the dialog is up.
- **The status bar** was drawn offscreen before and after: "1234" showed at its right,
  and is gone.
- **Not checked:** the Open panel and the status bar on the macOS platform (the driver
  sees Qt's own dialog).

**Still open:**
- **No other feature found left to port,** apart from those removed or left out on
  purpose and F6, Windows' key for moving between its panes (the image has a window of
  its own here). Compared with `upstream/master`: the menus, toolbar, menu shortcuts,
  list of dialogs and Configuration's controls. Not compared again: the other dialogs'
  controls, and the decoders' output. (When Windows showed its notices wasn't compared
  either, and one differed: see "The low-resolution notice, and the image window's title
  and width".)
- **A link that leads nowhere** when a batch starts isn't kept (see Command-line mode).
- **Also:** Developer ID signing and notarization (M5). Latent code. Fill bytes before an
  RST marker in the report's marker list (see Tools > Lookup MCU Offset).

### The image window's indicators with no pixel under the mouse

Reported from the packaged app: with the mouse off the image, the MCU, file and YCC
indicators were empty, and their empty frames looked like buttons. Now:
- **They read "MCU N/A", "File N/A" and "YCC N/A"** when no pixel of a JPEG scan is under
  the mouse: at the start, off the image or the preview, and over a Photoshop file's
  image.
- **Also once their image is gone** (the next file, Reprocess, New). They kept the
  previous image's values until the mouse next moved.

**Change from Windows:** its three panes were blank.

**Unchanged:** the values over a pixel, and "File: not decoded" for an MCU after the scan
stopped.

**Tests:** 12 new `PI` cases (1,234 in all). `--hover` takes `leave`, the mouse leaving
the preview. The cases: the start, and a file shown with the mouse not moved; off each
side of the image; leaving the preview, and coming back; a Photoshop file's image; the
next file, with and without an image, and New, each after a pixel was under the mouse.

On the previous commit 11 of them fail (all but coming back). Ten deliberate breaks each
fail their own cases:
- **When:** not at the start, 1; blank off the image, 6; left as they were off it, 5;
  blank over a Photoshop image, 1; not once the image is gone, 3; not on leaving the
  preview, 1; also over a pixel, 5.
- **The text:** MCU's empty, File's with a colon, YCC's without its name, 11 each.

**Found on the way,** and fixed in the next section: the low-resolution notice missing
when a file opens, the image window's title without "DC", and the window widening under
the mouse.

### The low-resolution notice, and the image window's title and width

The report's note explains the blurred preview (No IDCT decodes one colour per 8x8
block), but three things that should have made it plain didn't. Now:
- **The notice shows when a file opens,** as on Windows, which opened files through
  `Reprocess()`: "Currently only decoding low-res view (DC-only)...", or the high-res one
  with Full IDCT. The port showed it only for File > Reprocess. The window is empty
  behind it, and a file from Finder waits until it is closed. Its "do not show" box is
  ticked at first, so as on Windows it shows once unless that is unticked.
- **Turning it off is always saved.** It was lost when the settings had been saved
  earlier in the session, as accepting the license terms does at a first start.
- **The title is "Image (RGB, DC) @ 12.5%",** or "DC+AC", as on Windows. It had neither
  until one was chosen under Scan Segment, and then followed the option rather than the
  image: it said DC+AC over a DC-only image before that was reprocessed.
- **The image window doesn't widen under the mouse.** Each indicator has room from the
  start for the widest it shows (four-digit MCUs, a 32-bit offset, 16-bit DC values), so
  the window starts about 494 points wide on macOS rather than 400.

**Changes from Windows:** no notice for an empty file, whose error says why; the title
has no "(1/8)" after the zoom, as before.

**Tests:** 34 new cases, 20 `RN`, 10 `IT` and 4 `IW` (1,268 in all).
- **The driver:** `--note` answers the notice; `--image-window` prints the title, and
  whether the window widened or cut an indicator's text.
- **The cases:** the notice for each way of opening a file and for Reprocess, its two
  texts, what is behind it, turned off, kept, saved, and when there is none; the title
  for each channel, zoom and IDCT change, and a Photoshop file's; the width with the
  mouse on a pixel, on one not decoded, and with the widest values.

On the previous commit 28 of them fail. 23 deliberate breaks each fail their own cases.
Four first got past them: a label given too little room cuts its text instead of
widening the window, and a file not waiting for the notice showed a second one. The
cases now check both.
- **The notice:** 13 breaks (when, the text, what is behind it, turning it off, saving,
  waiting).
- **The title:** 6 (each part, and following the option rather than the image).
- **The width:** 4 (none reserved, or too little for each indicator).

**17 older cases changed for the wider window.** The 9 MCU grid scrolling cases expected
a 400-point window, and 3 of them could no longer scroll sideways. 8 red line cases
still passed, but had less room to scroll sideways, or none. Both groups now give the
window its size and use an image twice as large, and the red line's also check where
they scrolled to. With the dots counted from the area painted (the bug they are for), the
same 11 of the 17 fail as before.

**Correction: the app's settings file.** macOS keeps the settings in
`com.impulseadventure.www.JPEGsnoopQt.plist`, as `main.cpp` sets the organisation domain
`www.impulseadventure.com`. It isn't the bundle ID's, as M5 and the project file's
comment said. Earlier checks here that the real settings were unchanged watched the bundle
ID's file, so they showed nothing; and that `CFFIXED_USER_HOME` doesn't move the settings
was concluded with that name too (tried again since with a right name, it doesn't). The driver doesn't use either file: it
has an organisation and INI files of its own.

**Still open:**
- **The settings' name** differed from the bundle ID (above): fixed in the next section.
- **When Windows showed its other notices and messages** hasn't been compared.

### One settings file, named as the bundle ID

The app and macOS each wrote a file of their own: the app's 21 settings in
`com.impulseadventure.www.JPEGsnoopQt.plist` (its domain had a "www.", from the first Qt
port), and macOS's 2 values for the Open panel in the bundle ID's,
`com.impulseadventure.JPEGsnoopQt.plist`. Now:
- **The domain is `impulseadventure.com`,** so both use the bundle ID's file. The names
  are in `snoop.h` (`APP_ORG_DOMAIN`, `APP_NAME`).
- **The old name isn't read.** There are no published builds that used it, so there is
  no code to move settings over: a Mac that ran an older build asks for the license
  again and starts with the default options. (On this Mac they were copied once by such
  code, checked, and the old ones deleted; the code was then removed, before any commit.)
- **Unchanged:** the user database's folder (named by the organisation, not its domain),
  and the settings' place on other systems.

**Tests:** 1 new case, `SM_name` (1,269 in all): the settings' name against the built
app's bundle ID, and that app's own domain. It fails on the previous commit, and with
"www." back in either `main()` or `snoop.h`.

**Checks,** for this section and the two before it:
- **The app and the driver build** with no new warnings, compared with the previous
  commit's whole builds.
- **cppcheck 2.22** finds nothing new in the changed files, and ruff nothing new in the
  generator.
- **The reserved width** was laid out with macOS's style and fonts, off screen: 494
  points, nothing cut.
- **In the packaged app's window on macOS,** the user saw that the indicators, the title
  and the window's width look right.

**Not checked:** the notice in the app's own window on macOS (it is turned off in the
settings there).

### The zoom menu: Zoom to Fit Window, and up to 6400%

View > Image Zoom had Windows' nine fixed zooms, up to 800%, and started at 12.5%. Now:
- **Zoom to Fit Window (Cmd+0) is the default.** It shows all of the image in the middle,
  as large as fits with no scroll bars, shrunk or enlarged (up to 6400%), and follows
  the window, the next file, Reprocess and New until another zoom is chosen. The title
  says what it comes to: "Image (RGB, DC+AC) @ Fit (37%)". The zoom isn't saved, as
  before.
- **1600%, 3200% and 6400%** are added. 12800% would still be drawn right for the widest
  image decoded (65536 pixels); at 25600% Qt's widget size limit cuts it off at one
  screen pixel a point.
- **Zoom In and Out** go to the next zoom from what is shown, which can be between two
  when fitted. Their shortcuts and Cmd+0 work in the image window too.

**Found and fixed:**
- **The check mark didn't follow Zoom In and Out** (the Qt port's: Windows' did).
- **Zooming moved the view.** The scroll position stayed, so from 400% to 800% the middle
  of a 6000x4000 image went from 2999,1999 to 1499,999. The middle now stays. (Qt shows
  scroll bars later, by a queued call, which is done first, or the middle moved by half a
  bar's width.)

**For uneven zooms,** which only fitting gives:
- **Enlarged,** the image is drawn from a copy at its size on screen, each screen pixel
  the colour of the image pixel under its centre. QPainter's own enlarging put edges up
  to a screen pixel from the MCU grid, such as 4 of 31 lines at 137%.
- **The red line** is on the padding's first screen pixel, and there is none without
  padding, however the size rounds.
- **The size** is whole screen pixels: rounded up, one image in ten was a point too big.
- **While the window is resized** a reduced image is reduced quickly, then smoothly 150
  ms after that stops (smoothly takes 70 to 100 ms for the largest images).

**Changes from Windows:** the default (its was 12.5%), fitting, the three zooms added,
and the middle staying.

**Tests:** 75 new `ZM` cases (1,344 in all), and the 7 title cases that named 12.5% now
expect Fit.
- **The driver:** `--image-window` gets steps for the zoom, the window's size, scrolling
  and the shortcuts, a `view` report, and `sweep`: Fit at 24 window sizes, each checked
  for scroll bars and, pixel by pixel, whole and painted in part.
- **The cases:** the menu; Fit shrinking, enlarging, at its limit and following the
  window, files, Reprocess and New; each zoom; stepping and the check mark; the
  shortcuts in both windows; the middle; and Retina and 1.5x screens.

On the previous tree 59 of them fail. 27 deliberate breaks each fail their own cases. Of
two more, which changed nothing, the code they broke was removed: Qt lays the window out
as it shows, and needs no scroll bar policy to keep a fitted image free of them. Three
got past the cases at first, which now use sizes that aren't powers of two, detailed
images and parts painted by themselves.

**Checks:**
- **All 1,344 pass.** The 75 `ZM` cases also pass with ASan/UBSan, as the enlarged copy
  is new pixel code; the other groups weren't rerun with it, or with the printers
  hidden, which this doesn't touch.
- **cppcheck 2.22** finds nothing new in the changed files.
- **With macOS's platform, style and scroll bars** at Retina density, laid out off
  screen: Fit through resizes, stepping and the middle, up to 6400%.
- **Not checked:** the menu, Cmd+0 and a live resize in the app's own window.

### Why an image isn't shown

Reported with a progressive JPEG, which didn't display. That is a limit of the decoder,
not a fault: it decodes only baseline and extended sequential scans (SOF0 and SOF1), as
on Windows. But it was hard to tell. Each scan had "NOTE: Scan parsing doesn't support
this SOF mode", the status bar said "Done", and the image window, if open, was empty
under the last image's title. A height of 0, or no frame, had no note at all. Now:
- **The report ends with "*** Image Not Shown ***"** and the reason: progressive,
  lossless, hierarchical or arithmetic-coded, CMYK, another number of components, an
  invalid frame header, a size of 0, no frame, scan or tables, too large, no JPEG at the
  offset, a Photoshop image that can't be drawn, or Decode Image off. For a progressive
  JPEG it adds that the file needn't be damaged. Logs have it too.
- **The report is scrolled to it,** once it is laid out (a long one stopped short).
- **The status bar says "Done. Image not shown: progressive JPEG",** and so on.
- **The image window's title says the same,** rather than keeping the last image's.

**Unchanged from Windows:** what is decoded, and the notes under each scan.

**Tests:** 47 new `NS` cases (1,391 in all), from made files: each reason in the report
and the status bar, none of it with an image (a cut-short scan too), the title, the
scrolling, logs, and Extract All. On the previous build 40 of them fail. 19 deliberate
breaks each fail their own cases; one more showed that keeping the reason out of the
status bar during Extract All did nothing (the file is analyzed again after), and that
was removed.

### The file's dates and hashes

Asked for: after Filename and Filesize, a report's intro now has Created and Modified,
and SHA-512, SHA-256, SHA-1, MD5 and CRC32 (`filefacts.cpp`). Batch and command-line logs
have them too.
- **The times** are the file system's, in UTC as "2026-11-06T09:54:46.123Z", cut short to
  the millisecond. Where the disk keeps no created time it says "not available".
- **The hashes** are lower case, as the standard tools print them, and of the file on the
  disk: a File Overlay doesn't change them. They are read through the handle that is
  analyzed, all five at once, at MD5's speed: 1 GB in 2.6 seconds on an M2. The status
  bar gives the percent meanwhile.
- **They are kept while the file is the same** (its identity, size, modified and change
  times), so Reprocess, Image Search and Extract All don't read a large video again.
  Not if it changed within 3 seconds of being hashed, which its times might not show.
- **No hash, and why, in each line** when the file changed while it was read, couldn't
  all be read, or is 4 GiB or larger (as on Windows it isn't analyzed, and at some 2.5
  seconds a GiB a large one would take minutes): an option now, see the next section.

**Tests:** 56 new `FF` cases (1,447 in all): times at the ends of seconds and days, before
1970 and not kept; published check values; hashes at each tail length and around the
thread and chunk sizes, against Python's; logs; and files changed before and while they
are read. 24 deliberate breaks each fail their own cases. A 1 GB file's hashes also match
`shasum`, `md5`, and zlib's and gzip's CRC32. Five Find cases name the line a match is
on, which is now 7 further down.

**Checks,** for this section and "Why an image isn't shown":
- **All 1,447 pass:** a plain run of all of them, in which those five failed, then the
  Find and `FF` groups again with the five moved (no app code changed between).
- **The `FF` and `NS` groups** also pass with ASan/UBSan. The other groups weren't rerun
  with it, or with the printers hidden, which neither change touches.
- **cppcheck 2.22** finds nothing new in the changed files.
- **Not checked:** the intro and the status bar's percent in the app's own window.

### Options > Skip Hashes on 4 GiB or Larger Files

Asked for, on by default. It is in Options after Relaxed Parsing and is saved
(`General\SkipLargeHashes`); settings saved before it existed have it on.
- **On,** a file of 4 GiB (2^32 bytes) or larger gets its dates, and "not calculated: the
  file is 4 GiB or larger (see Options)" for each hash.
- **Off,** it is hashed. A dialog counts the percent and has Cancel (also its close, and
  quitting the app), after which each line says "not calculated: canceled"; a file sent
  from Finder waits meanwhile. In a batch, the batch's progress counts it, and its
  Cancel stops the hashes too.
- **Either way** it isn't analyzed: that limit is Windows' (32-bit file positions).
- **The command line** goes by the saved setting, and has no Cancel.
- **A change** shows in the open file's report at the next Reprocess (at once with
  auto-reprocessing on).

**Tests:** 37 new `SF` cases (1,484 in all): the menu and its check mark, saving, the
boundary (a byte under 4 GiB, at it, 5 GiB), all of 4 GiB of zeros against `md5`'s and
zlib's results, the dialog and each way out of it, and batches and logs. The large
files are sparse. 19 deliberate breaks each fail the cases aimed at them.

**Checks:**
- **The groups it can affect pass** (280 cases: `SF`, `FF`, `RM`, the settings, batches
  and the command line's files, batches and options). The whole suite wasn't rerun.
- **35 of the `SF` cases pass with ASan/UBSan:** all but the two that read all 4 GiB.
- **cppcheck 2.22** finds nothing new in the files this changes.
- **Not checked:** the dialog and the menu item in the app's own window, and a file of
  4 GiB or more that isn't sparse.

### An endless loop at the end of the largest files

Found while looking into 64-bit file positions. A file of 4 GiB - 1 bytes (0xFFFFFFFF,
the largest analyzed, and FAT32's largest) has no position after its end in 32 bits:
moving past it wrapped around to 0, so the check for "Early EOF" never saw it. Parsing
went on with the start of the file, and for a scan with no EOI that was forever (with
no Cancel, as analysis has none). A byte less ended in some 20 seconds.

Parsing only moves forward, so a position before one it had has wrapped
(`CjfifDecode::WrappedSince()`). Now:
- **The marker loop ends** with "ERROR: Early EOF - file may be missing EOI", as for a
  smaller file, and the scan's error says @ 0x100000000 rather than 0x00000000.
- **The loops inside a segment end** there too, rather than read the start of the file as
  the rest of it: DHT, DQT, the search after a comment with a length of 0, IPTC, and an
  Exif thumbnail, which says "runs past the end of the file".
- **A scan header cut short** there doesn't decode the start of the file as its scan.
- **A segment reaching past the end** is said to be longer than expected, not shorter.

The scan's loop needs that exact size. A segment's length could also carry the position
past 2^32 in files some kilobytes smaller, which are covered too. **Windows** has the
same code for the marker loop and the scan (by reading it: it wasn't run).

**Tests:** 17 new `WR` cases: each of these, and two whole files of that size, one opened
in the window. 15 fail on the previous commit, the two whole ones by never finishing
(the other 2 are the file a byte less).

### The compression figures of large images

Both were worked out in 32 bits:
- **The compression ratio was wrong** over 89.5 megapixels in colour, and negative up to
  179: so for 108 MP phone photos (12000 x 9000 gave -126.03:1 for a plain test image).
  The port's signed sizes halved Windows' limit, above which it is wrong there too.
- **Both figures were wrong from 512 MiB of scan data,** as on Windows: 570 MiB gave 2.62
  bits a pixel rather than 25.84.

They are worked out in doubles, which hold both exactly.

**Tests:** 5 new `CS` cases (1,506 in all), each side of 89.5 megapixels, 108, 180, and
513 MiB of scan; 4 fail on the previous commit. (The 513 MiB case became two in "Three
fixes from a review of these changes".)

**Checks,** for this section and the one before it:
- **14 deliberate breaks** each fail the cases aimed at them.
- **Other files' reports are unchanged:** with the default options, byte for byte the
  previous commit's for the 252 case files under 512 MiB and for 3,298 damaged copies.
- **The groups closest to it pass** (720 cases: decoding, export, offsets, search, MCU
  lookup, batches and the command line). The whole suite wasn't rerun: its other groups
  open files in the window, so decode them by the same code.
- **With ASan/UBSan,** 19 of the new cases pass, and `H`'s 10. Not run with it: the two
  whole files and the 513 MiB scan, which take minutes there.
- **cppcheck 2.22** finds nothing new in the changed files.
- **Not checked:** either fix in the app's own window, or with a real file that large.

**Found meanwhile,** and fixed in the next section: an AVI that is zeros after its
headers made the report run away.

### AVIs that made the report run away

An AVI that is zeros after its headers (a recording cut off, or padded) was parsed as a
chunk every 8 bytes: 131,000 report lines and some 320 MB of memory for each MiB, so one
of 100 MiB would have needed some 32 GB. Empty and junk chunks did the same, and the
text of IDIT and ISFT ran on to the end of the file (530 MB for 8 MiB). Extract All does
it again for each frame. Now:
- **Zeros are passed in one line,** "(24 bytes of zeros at 0x000000e2)", 8 at a time as
  before, so the chunks after padding are still found.
- **Parsing stops after 1 MiB of them in all,** with "NOTE: Zeros in place of AVI chunks
  at ... (over 1 MiB in all). Stopping AVI parsing."
- **And after 4,096 chunks,** with an error. An AVI normally has some tens outside movi, which
  is skipped whole. What was found before stopping stays, such as Motion JPEG.
- **IDIT's and ISFT's text** is cut at 1 KB (and ISFT's at the end of its field too, since
  "Three fixes from a review of these changes").
- **A last 1 to 7 bytes of zeros** aren't read past the end of the file any more, which
  left Extract All without the frames near it (none of 20 in a small AVI).

**Changes from Windows,** whose code has none of these limits: all five.

**Tests:** 22 new `AV` cases (1,528 in all): zeros alone, up to 4 GiB - 1, and in the
window; padding on each side of the limit and in two runs; the last bytes; frames
extracted; chunks on each side of their limit; and the two texts. 17 fail on the code
before, and 12 deliberate breaks each fail the cases aimed at them.

**Checks:**
- **The same chunks are found:** the 252 case files' reports are byte for byte as before,
  and of 1,500 made AVIs (with padding, odd sizes, damage and cuts), decoded from the
  start and 450 from a frame too, only the lines of zeros differ, and 5 frames that are
  now read to their end.
- **The groups that parse AVIs pass** (417 cases, with export, batches, the command line,
  search and offsets). The whole suite wasn't rerun.
- **With ASan/UBSan,** the `AV`, `H` and `R19` cases pass (38).
- **cppcheck 2.22** finds nothing new in the changed file.
- **Not checked:** a real camera's AVI (all were made), or the app's own window.

**Found meanwhile:**
- **Frames weren't exported near the end of an AVI with other stray bytes there,**
  fixed in the next section.
- **A PSD's XMP is shown whole,** at some 55 MB of memory a MiB (480 MB for 8 MiB). Not
  fixed.

### Frames lost near the end of an AVI

Stray bytes after an AVI's last chunk (other than zeros), or a last chunk cut short,
were read past the end of the file. That marks the file buffer bad until it next loads a
window, and the JPEG decoded next was read from the same window, so its scan stopped at
once: "ERROR: Ran out of buffer before EOI during phase 1 of Scan decode". Extract All,
and decoding a frame found by Img Search, lost the frames in the file's last 16 KB (the
window reaches that far back): 5 of 20 were exported from a 21 KB AVI ending in "ab", none
from a 14 KB one, and 1,477 of 1,500 from one of 1 MiB. `CjfifDecode::DecodeAvi()`
reloaded the window when it had read past the end; the next section moved that reload to
the scan's start, which covers these frames too.

**Windows** has the same buffer and parser, so the same bug by its code (not tried there).

**Tests:** 4 new `AV` cases (1,532 in all): Extract All of 20 frames followed by "ab", by
an IDIT chunk cut short and by a LIST cut short, and the last frame decoded on its own.
All 4 fail with the reload taken out, which is the code before.

**Checks:**
- **Nothing else changes:** against the code before, the 255 case files' reports are byte
  for byte the same, and so are those of 1,500 made AVIs decoded from the start and from
  2,273 of their frames, and their Extract All exports.
- **The groups that parse AVIs pass** (421 cases). The whole suite wasn't rerun.
- **With ASan/UBSan,** the `AV`, `H` and `R19` cases pass (42).
- **cppcheck 2.22** finds nothing new in the changed file, and a bug planted beside the
  change shows it was checked.
- **Not checked:** a real camera's AVI, or the app's own window.

**Found meanwhile,** and fixed in the next section: the same buffer flag broke the
report and Export JPEG of a small JPEG whose EXIF points past the end of the file.

### An EXIF value past the end of a small JPEG

The flag of the section before hit ordinary JPEGs too. An EXIF value at or just past the
end of the file (ImageDescription's text, a RATIONAL such as XResolution, or the EXIF
IFD) was read past the end. In a file under about 16 KB everything read after it came
from the same window, so nothing cleared the flag. The image was still decoded and shown,
as its decode loads the window again, but the report said "ERROR: Ran out of buffer
before EOI during phase 1 of Scan decode" where the scan began. It then said "Expected
marker 0xFF" there, never reached EOI, and ended with "Data exists after EOF" for the
whole file. Export JPEG refused with "Missing marker: EOI". Now the scan reloads the
window at its start when the flag is set. That covers the AVI frames too, so
`DecodeAvi()`'s own reload is gone. A scan that does run past the end still reports it,
where it ends.

**Windows** has the same code, so the same bug (not tried there).

**Tests:** 6 new `PE` cases (1,538 in all): ImageDescription 10 bytes past the end and
across it, XResolution and the EXIF IFD past it, Export JPEG, and a scan cut short, which
still reports running out one past its last byte. All 6 fail on the code before. The 4
`AV_stray_*` cases pass without `DecodeAvi()`'s reload, and fail with neither reload.

**Checks:**
- **Only the reports that ran out change.** These were compared against the code before:
  - **Case files:** of 260 decoded and exported, the only changes are the 4 new files
    pointing past the end, now decoded to EOI and exported, and the cut scan, which runs
    out at its end instead of its start. The 5 other files with a scan cut short are
    unchanged.
  - **Made AVIs:** 1,500 of them, decoded from the start and from 2,273 of their frames,
    and their Extract All exports, are byte for byte the same.
  - **Made JPEGs:** 650 of them, from 800 bytes to 15 KB, each with one of five kinds of
    EXIF value placed from 40 bytes before the end to 1 MiB past it. The 350 that ran
    out now decode to EOI, with the same image, and export. The other 300 are unchanged.
    The bug never showed with a value 16,000 bytes or more past the end, or 4,000 past
    it in files of 13 KB or more.
- **The affected groups pass** (598 cases: those that parse AVIs, export, batches, the
  command line, search, offsets, MCU lookup, and files cut short). The whole suite wasn't
  rerun.
- **With ASan/UBSan,** the `PE`, `AV`, `H` and `R19` cases pass (48).
- **cppcheck 2.22** finds nothing new in the changed file, and a bug planted beside the
  change shows it was checked.
- **Not checked:** a camera's JPEG with such a value (all were made), or the app's own
  window.

### Three fixes from a review of these changes

A review of the sections above, before they were committed, found:
- **ISFT's text was read for as long as the list holding it,** counted from where the
  text starts. So it ran past its own field and 12 bytes past the list, as on Windows;
  the 1 KB cap kept that. A Software text that didn't end in a 0 took in the next field's
  name ("AAAAAAICRD"). Now it stops at the end of its field or the list, whichever comes
  first. It and IDIT's text also stop at the end of the file, which they read a byte past
  when cut short (marking the buffer bad, as in the sections before).
- **Fill bytes before a marker ran past the end of the file.** Their last byte was read
  past it, and the 0 there was taken for a marker. A file ending in 0xFF said "WARNING:
  Unknown marker [0xFF00]" and "Stopping decode" in place of "ERROR: Early EOF - file may
  be missing EOI". At the end of the largest file the position also passed 2^32, so with
  Options > Relaxed Parsing the marker's length was read from the file's start: 65,496
  for a JPEG there. The marker loop's check caught that, unless the fill bytes began in
  the file's first 64 KB; then parsing went on once more from near the start (by reading
  the code). Now the fill bytes stop at the end of the file, which is an Early EOF.
- **The 513 MiB case** of "The compression figures of large images" required its bits
  per pixel, but only forbade a wrong ratio, so it passed without the ratio line. It is
  now two cases, requiring 25.84 bits per pixel and 0.31:1.

**Windows** has the same code for both reads.

**Tests:** 9 new cases (1,547 in all), and one changed:
- **`AV_isft_long`** now has a 4 KB field, over the cap; it had 5 bytes, and needed the
  overrun to pass.
- **`AV_isft_field` and `AV_isft_list`:** a text without a 0 before the next field, and a
  field longer than its list.
- **`CS_scan_512mib_ratio`.**
- **6 `MP` cases:** one and four fill bytes at the end, the count of those, Relaxed
  Parsing, and both at the end of a file of 4 GiB - 1 bytes. The driver's `decode` takes
  a new `--relaxed` for them.

7 of the new cases fail on the code before: the 2 `AV` and 5 `MP`. `MP_eof_pads_count`
guards the count, which is the same as before. Both 513 MiB cases fail on the previous
commit (439.90:1 and 0.02 bits a pixel). The texts' and the fill bytes' old code, put back
one at a time, each fail just their own cases. With the scan's reload taken out, the IDIT
and LIST cut short (`AV_stray_*`) now pass anyway: their text no longer reads past the end.

**Checks:**
- **Only those reports change.** These were compared against the code before:
  - **Case files:** of 264 decoded and exported, only the 4 new ones with these faults.
  - **Cut JPEGs:** a 1.4 KB JPEG cut after each of its bytes, and each cut followed by two
    fill bytes. The 793 of 2,758 whose fill bytes reached the end now say Early EOF; the
    rest are unchanged.
  - **AVIs with ISFT:** 630 made with texts with and without a 0, fields and lists ending
    before, at and after the text, and the list before or after movi or at the end. 269
    now show the Software text cut to its field or list. The rest, and all their Extract
    All exports, are unchanged.
  - **The made AVIs from before:** of 1,500 decoded from the start and from 2,273 of
    their frames, and exported, only 2 frames ending in fill bytes change, to Early EOF.
- **The affected groups pass** (612 cases: those of the sections above, decoding, export,
  batches, the command line, search, offsets, MCU lookup, and files cut short). The whole
  suite wasn't rerun.
- **With ASan/UBSan,** the `AV`, `MP`, `PE`, `H` and `R19` cases pass (56).
- **cppcheck 2.22** finds nothing new, and catches two bugs planted beside the changes.
  Its "Variable 'nInfoLen' is assigned a value that is never used" is gone, and so is
  the compiler's warning about it (385 now): that was ISFT's field length.
- **Not checked:** fill bytes from the first 64 KB to the end of a file of 4 GiB - 1
  bytes (it needs 4 GiB of 0xFF written), a real camera's AVI, or the app's own window.

### The tests: cases that passed without their result

After the three fixes above, the suite was gone through for cases that would pass with
their result missing or wrong. Of the 1,547 cases, 17 checked only the exit status, 9
only `harness: decoded=1`, and 2 only that a line was absent; 5 GUI cases required only
`harness: done`. Run against the reports of a plain JPEG with the same flags, 25 decode
cases' required lines matched there too: 17 of them (some among the 9) are changed, and
the other 8 compare two decodes of their own file, or are about a label every report has.
The changed cases now require the line their file gives, worked out from the bytes the
generator writes:
- **Crashes and hangs (C1–C5, H1–H8):** the error each fix reports, with the values from
  the file: "Image too large to decode the scan (65535 x 65535 pixels)", the DHT selectors
  4 and 15, "Excessive DHT entries (258)", the IFD repeated at 0x00000008, the 8BIM length
  0xfffffff4 cut to 4 and the EOI after it at 0x24, the marker expected at 0x34 after a
  short XMP, the AVI size that points backwards, the thumbnail of 195,075 bytes, two
  layer groups (not a third) and no guide in a block that ends after the count.
- **H5:** its thumbnail claims 1,000 bytes but only 68 are left, so the file's end stopped
  the skip with or without the fix's check for it. A new case claims 0xFFFFFF00 bytes,
  which only that check stops (without it, 4 billion passes).
- **C3:** with Options > DHT Expand (`--expand-dht`, new in the driver), the 256th code
  of the 258-code table (16 bits, 0000000100000010) is shown, and the 257th isn't.
- **C4 and F5:** a white image's histogram is three full bars (`rgb=180`). With 255 one
  past each array, red's count went into green's first bin and green's into blue's: two
  bars at 0 (`rgb=120`).
- **R14:** the file whose table is defined twice decodes to the same pixels (MD5) as the
  file without the first definition.
- **F2:** the error naming the string and the offsets where it and its resource end.
- **PE and the last AVI frame:** the EOI is found at its offset, the file's (or frame's)
  last 2 bytes. Checking this against the old code showed that the scan's end ("Scan Data
  encountered marker 0xFFd9") isn't enough: the image decoder reads the scan apart from
  the marker parser and reaches the EOI even when the parser ran out early.
- **Valid files (`ok_*`):** each decodes at its SOF's size with no error or warning, the
  report gives the subsampling its SOF gives, and two pixels are close to libjpeg's
  (Pillow 13's) decode of the file. Over these images the two differ by up to 4 where
  nothing is subsampled (IDCT rounding) and up to 11 where chroma is (libjpeg smooths it
  as it upsamples; JPEGsnoop repeats it), so the windows are 6 and 13; at the points
  taken they agree within 2. The EXIF file's make, model and GPS values are checked.
- **GUI (U1–U3, F1):** each also requires its file's line, that it was decoded (or not)
  before the mouse went over it.
- **S5:** the driver prints "reprocessed with no file" either way; without the fix,
  opening "" showed "Couldn't open file: []", which the case now forbids.
- **Batches:** with hidden files left out, Extract All writes 16 files (9 logs, 7 JPEGs).

43 cases changed and 39 are new (1,586 in all). `tests/README.md` now says how to write a
case so that it fails when its result is missing or wrong.

**Checks:**
- **The new patterns aren't loose.** In each passing output, deleting the matched line,
  or adding 1 to any number in it, fails the case, except where the case allows it (the
  decode's `status=` count, and pixels within 4; a pixel 9 off fails).
- **They fail with the bugs back.** In a scratch copy, nine fixes could be switched off
  one at a time: C3's code limit, C4's bins, C5's zero factor, R14's table clear, F5's
  64-bit bar height, the PE scan reload, the batch's hidden files, H5's end-of-file stop
  and S5's check for no file. Each fails its own cases and passes with the switch off.
  That is how the PE cases' first new line was found wanting (see the PE item above).
- **An independent review** (an agent given only the diff) re-derived every value from
  the generator and found the H5 and S5 gaps, pixel windows with no margin at two points,
  and a wrong comment, all fixed as above. It also noted that some required lines were
  printed before their fix too (C2's selector error, C3's count, C5's validation error,
  H2's EOI and H3's marker error); there a crash, a hang or a sibling case is what fails.
- **All 251 decode cases pass,** and the 82 changed or new ones. The driver change only
  adds the option; cppcheck finds the same 58 things in it as before.

### Loops and reads past their bounds: an audit (not fixed)

The bugs fixed in the sections above were of two kinds: a loop that moves on toward the
end of the file without a limit, and a read whose length comes from a field other than
its own. `JfifDecode.cpp`, `DecodePs.cpp`, `WindowBuf.cpp` and the scan reader in
`ImgDecode.cpp` were read for both. Each finding below was reproduced with a made-up file
(made again independently, except where marked "agent's file"). All are inherited from
Windows unless marked. None is fixed yet.

**Runaway reports from a few bytes** (all six fixed since: see "Six runaway reports"
below). The report is a text widget that slows as it grows:
about 65,000 lines take 5 s, two such blocks in one file took over 10 s, and one 64 KB
segment can hold thousands of them.
- **Photoshop Layer Select ID** (`DecodePs.cpp` 1700): a 16-bit count with no limit. A
  2-byte resource gives 65,535 lines (4.3 MB, 5.6 s), read from past its end.
- **IPTC fields** (`DecodeIptc` 644, `DecodeIptcValue` 595): a field's 16-bit length isn't
  checked against its resource. A 6-byte resource reads a 65,535-byte caption, NULs and
  the next resource included. In the agent's run, 100 such resources didn't finish in
  10 s.
- **FlashPix contents list** (`DecodeApp2Flashpix` 5118): its 16-bit count isn't checked
  against the APP2 segment or the file. A 9-byte segment gives 196,954 lines (4.9 MB,
  7.8 s), nearly all from past the end of the file.
- **DHT codes over 256** (`DecodeDHT` 5404): C3 left one error line for every code
  beyond 256. One DHT segment of 15 tables gives 57,360 lines (3.2 s).
- **EXIF IFD entries** (`DecodeExifIfd` 3071): up to 65,535 entries, checked against
  neither the APP1 segment nor the file (65,558 lines, 5.1 s; agent's file).
- **An EXIF thumbnail inside the scan** (`DecodeEmbeddedThumb` 9370): every RST pair
  adds a blank line; a 400 KB file gives 200,033 (agent's file).

**Offsets that wrap past 2^32** (all fixed since: see "Offsets that wrapped, and Photoshop
resource names" below):
- **IFD0** at an offset of 0xFFFFFFF4 is read from the file's start (the SOI as an entry
  count of 0xD8FF): `DecodeMarker` 6144 adds the offsets in 32 bits (fixed with the IFD
  entries, below).
- **The EXIF thumbnail** at such an offset, with a length of 0xFFFFFFFF, is the main
  image: its markers and signature are reported as the thumbnail's (`DecodeEmbeddedThumb`).
- **An AVI chunk size of 0xFFFFFFF9–0xFFFFFFFF** lands 2–8 bytes past the chunk's start,
  which the port's "points backwards" check misses: a JUNK chunk of 0xFFFFFFFD lists a
  chunk "AB", and `movi` is never reached (port's check, inherited behaviour).

**Reads past their own field or segment** (wrong values in the report; all fixed since: see
"Reads past their own field" below):
- **DHT:** a table's counts and codes are read past the segment, and DHT isn't checked
  with `ExpectMarkerEnd`, so parsing goes on where the read stopped: 16 codes taken from
  a COM after it, then "Expected marker 0xFF, got 0x45" inside the COM, and no image.
- **DQT** reads 64 (or 128) values whatever its length: with a length of 3 the table is
  the next segments' bytes, given a quality factor. **SOF** reads 3 bytes per component
  whatever its length (agent's file).
- **ICC profile:** the 128-byte header is read from a 20-byte segment (its fields came
  from the next COM); `nPayloadLen` is computed but unused.
- **FlashPix stream names** are read up to 510 bytes, past the segment.
- **XPTitle and the other XP tags:** the byte count is used as a count of UTF-16 units, so
  "ABC" read on into the next tag ("ABCXYZ"); a value of 4 bytes or fewer, stored in the
  entry, is used as an offset (shown as "").
- **CFAPattern** takes its cells from the repeat sizes inside it, not its count; cells
  past its own come from the previous entry ("[ Yel Mgn ]" from FileSource; agent's file).
- **COM** text past the end of the file: a length of 0xFFFF with 3 bytes gives "abc" and
  65,530 dots.
- **EXIF IFDs after IFD0** and every value offset are never checked against the APP1
  segment: IFD entries were read from a later APP15 (agent's file). (The IFDs and their
  entries are, since the fix below; the values they point to still aren't.)
- **APPn identifiers** are read at a fixed length and the position moves by the printed
  string's length, which drops non-printing bytes (by reading only).
- **AVI:** after a stream list (strl) parsing goes on from its strn chunk's end, assuming
  there is one; without it, the next strl was skipped and a Motion JPEG AVI said "AVI is
  not MotionJPEG" (agent's file).
- **Photoshop:** a grid guide starting 1–4 bytes before its block's end reads past it
  (H7's fix checks only where a guide starts; port's fix); version 6 slices likewise
  (agent's file); JPEG Quality, Thumbnail and Version Info resources read their fixed
  fields from the next resource (a thumbnail 943,868,237 pixels wide, "8BIM"); a
  descriptor item of a type that isn't decoded (`doub`, `UntF`, `obj `, ...) leaves its
  value unread, which is then taken as the next key's length; in a PSD, `luni`, `lyid`
  and four other tagged blocks read past their length (agent's files), and section
  lengths are checked only against the file's end (agent's report only).
- **Photoshop resources lost:** after a read past an odd-length resource, parsing goes on
  one byte early and loses every resource after it; an unknown resource (0x03E8) leaves
  the position inside it, so an "8BIM" in its data was reported as a resource.

**Other things found:**
- **H5's message** says "runs past the thumbnail length (N bytes)" also when the file's
  end stopped it (port).
- **"Data exists after EOF"** counts from offset 0 when decoding stops before an EOI: the
  whole file is said to be after it. (Fixed since: see "Where the marker list ends".)
- **`Q_ASSERT`** in `CimgDecode::DecodeScanImg` (3523) asserts a nonzero minimum factor
  that the check after it handles (an SOS with more components than the SOF); a debug
  build would stop there. Release builds, the only ones made, are unaffected (port).
- **Past the end of the file,** `CwindowBuf` reloads its window for every byte read within
  16 KB of it, which slows the runaway reads above (2,000 FlashPix entries took 2.4 s
  longer past the end than inside the file).
- **`DecodeDicom.cpp`** isn't built (it isn't in `JPEGsnoopQt.pri`, and `SUPPORT_DICOM` is
  off), so DICOM files aren't decoded. It has the same kind of read (each value read as
  200 bytes, not its length).

**Checked and bounded:** the marker loop, DQT tables, XMP, the JFIF thumbnail, DAC, the
COM vulnerability search, SOF and SOS component loops, the scan's first pass, the AVI
chunk loop and its text fields, the export copies, `CwindowBuf`'s searches and string
reads, the scan reader in `ImgDecode.cpp` (it stops at the end of the file), Photoshop
descriptors and lists (depth 32), strings, layer records, channel data and RLE rows,
the APP13 resource loop, and the EXIF value loops (at most 4,000 values, 64 printed).

### Six runaway reports

The six "Runaway reports from a few bytes" in the audit above are fixed. Each count or
length from the file now stops at the end of what holds it, or of the file if that comes
first (past it every byte reads as 0, which is a valid small entry), and says so:
- **Photoshop Layer Select ID:** the count and the IDs stop at the end of their resource
  ("ERROR: Layer ID at [...] runs past the end of its 8BIM resource, at [...].
  Stopping."), as the other Photoshop parsers do. An ID that would start inside and end
  past it isn't read either.
- **IPTC fields:** a field's length is cut to what is left of its resource ("ERROR: IPTC
  field length [100] at [...] runs past the end of its 8BIM resource, at [...]; using
  [2]"), and a field whose 5-byte header doesn't fit isn't read. A tag other than 0x1C
  still ends the list, at the same offset, now without reading the rest of its header.
- **Photoshop resources** in a segment cut short by the end of the file end there too
  (the existing "8BIM length [...] runs past the end of its segment; using [...]").
- **FlashPix contents list:** `DecodeApp2Flashpix` is given the APP2 segment's end (or the
  file's), and an entry needs 7 bytes of it (size, default and a name's 0). It also stops
  if an entry takes the position past 2^32, to the file's start.
- **DHT:** codes past 256 in a table are reported in one line after the table, "Excessive
  DHT entries (258): only the first 256 are used", in place of one line for each (in the
  middle of the code listing). A DHT cut short by the end of the file ends there, and is
  still an Early EOF.
- **EXIF IFDs:** an IFD, IFD0 and the Exif, GPS, interoperability and maker note IFDs
  alike, is in its APP1 segment (as far as the file goes), and so is each entry: "ERROR:
  IFD0 entry #1 at [...] runs past the end of its APP1 segment, at [...]. Stopping." The
  IFD's position is worked out in 64 bits, so an offset past 2^32 is "outside its APP1
  segment" and no longer read 4 GiB before (the "IFD0" item under "Offsets that wrap past
  2^32", fixed too). A maker note outside the segment still counts as maker notes, which
  the assessment goes by, and an error in a maker note shows even with their decode off.
  After an IFD stops with an error, the chain no longer reads an "Offset to Next IFD" from
  where its entries stopped (which also drops that line after an entry of an unsupported
  format, where it was read from the middle of the entry).
- **Thumbnail RSTs:** an RST marker outside the thumbnail's scan isn't given a blank line;
  it reports nothing, as before.

**Windows** has all six.

**Tests:** 36 new `RA` cases and 2 changed (`C3_dht_258` and `C3_dht_4080`, now the one
line). The driver's `decode` takes a new `--count-lines`, which prints the report's
number of lines, and each runaway has a case that requires it under 1,000 (the DHT one,
whose tables are listed, under 10,000), also for files cut short and for FlashPix at the
end of a file of 4 GiB - 1 bytes. The others require each error with its offsets worked
out from the file, what is inside the limit (the IDs, the caption "Hi", the stream name,
the Orientation entry, the thumbnail's EOI), that parsing goes on after it (the next
resource, the image, Early EOF), the maker notes' summary, and, forbidden, what was read
past it ("EVIL" entries from a later segment, a third Layer ID of 65536). On the code
before, 19 of the first 22 fail (8 by running out of time) and both changed ones; the
other 3 check what was already right. Each of the 7 follow-ups from the review below,
switched off one at a time in a scratch copy, fails its own cases.

**Checks:**
- **Only these reports change.** Old against new, on the 269 case files under 64 MB and
  760 made files with these structures around their limits: 541 unchanged, 61 thumbnails
  with fewer blank lines and nothing else, and 426 with the new errors, where every other
  line that changes is the runaway output, entries or fields read past their segment, an
  old "Parsing exceeded expected length" (after which the next resource was lost, and is
  now read), or the rest of a report whose old run timed out. 300 of the made files cut
  short: 139 unchanged, 141 with the new errors, and 20 where only a cut resource has the
  8BIM length error or an empty DHT table read from past the end is gone; all still say
  Early EOF where they did. The first fixed build and the last give the same reports on
  the 1,029 files.
- **The affected groups pass:** all 624 cases that don't open the window (decoding,
  export, batches, the command line, file facts, settings), which also go through the
  decoder.
- **With ASan/UBSan,** the `RA`, `C3`, `H`, `F2`, `PE`, EXIF, Photoshop and `ok_` cases
  pass (146).
- **cppcheck 2.22** finds the same 250 things in the two files as before, and catches a
  bug planted in each of the 6 functions changed.
- **An independent review** (an agent given only the diff) re-derived every offset and
  found the gaps fixed above: the declared ends let files cut short still run away (a
  15-byte file gave 28,097 lines), FlashPix could wrap at 4 GiB, a maker note pointer
  outside the APP1 made the assessment say "processed/edited", a 1-byte Layer Select
  resource read its count past it, and a maker note error was hidden.
- **Not checked:** the cases that open the window (the reports of valid files don't
  change), a real camera's file.

**Found, not fixed:**
- **A file that ends right after a whole segment** says "Expected marker 0xFF, got 0x00 @
  [its end]" rather than Early EOF: the next marker is read at the end (inherited).
  (Fixed since: see "Where the marker list ends".)
- **An 8BIM header cut by the segment's end** gives "8BIM length [0x00000000] runs past the
  end of its segment", naming the length, not the header (port, now also at the file's end).
  (Fixed since: see "Reads past their own field".)
- **Empty DHT tables** (17 bytes each) give 21 lines each: a whole 64 KB DHT segment of
  them is about 81,000 lines. That grows with the file rather than from a few bytes.
  (Fixed since: see "The lines of a marker list".)

### Reads past their own field

The "Reads past their own field or segment" in the audit above are fixed, with the rest
of their class found in the same functions. A length or count from the file, or a field
of a fixed size, is now read only inside what holds it: its segment, resource, value,
list or section, or the file if that ends first. Where one doesn't fit, an error says
where it would have run past, as for the runaway reports ("ERROR: DQT table at [...] runs
past the end of its DQT segment, at [...]."). What happens then depends on the field:

- **Tables and headers the image needs** (DHT, DQT, SOF, SOS, DRI, DNL, EXP, DAC and
  APP0's JFIF header): decoding stops, as it already did for a length that doesn't match
  its fields ("Marker length shorter than expected"), and as libjpeg does. With Options >
  Relaxed Parsing they are read as before, after the error, so a file whose length is
  wrong but whose fields are right still decodes. Cut short by the end of the file, they
  end there, and the file is an Early EOF.
- **Metadata** (APPn identifiers, EXIF, ICC, FlashPix, Photoshop resources, comments):
  that field is skipped, or the list it is in stops, and parsing goes on after the segment
  or resource.

**The JPEG segments:**
- **DHT:** a table (its class, 16 counts and as many codes as they add up to) is checked
  whole before anything of it is read. Its counts and codes were read on into the next
  segment, and parsing went on where they stopped: inside it ("Expected marker 0xFF").
  DHT never had a length check after its tables, so a length a little short of whole
  tables decoded; it now stops, unless Relaxed Parsing is on.
- **DQT** read 64 (or 128) values whatever its length: with a length of 3 the table was
  the next segments' bytes, given a quality factor, and used.
- **SOF** read 6 bytes and 3 for each component whatever its length (the next segment's
  "FF C4 00" gave a sampling factor of 12 x 4). **SOS** likewise: a length short of its
  components was ignored, and the image decoded; it now stops, unless Relaxed Parsing is
  on. A length longer than its fields is still ignored, as before.
- **DRI, DNL, EXP and DAC:** their values were read from the next marker (an interval of
  65,499). EXP read 2 bytes for its 1-byte Eh and Ev (T.81 B.3.3: the segment is 3
  bytes), the second the next marker's 0xFF, which gave 15 and 15 and an error: a valid
  EXP stopped the decode.
- **APP0's JFIF header** (version, units, density, thumbnail size) was read past an APP0
  of only "JFIF".
- **APPn identifiers** were read 255 bytes on whatever the segment's length ("Duck" with
  no 0 ran on into the next segment's bytes), and the position moved on by the length of
  what was shown, which leaves out bytes that don't print: "Ex\x01if" was taken for
  "Exif", and its TIFF header read a byte early. They now end at the segment's end, and
  are compared as they are; one whose 0 isn't in the segment is an error, and matches
  nothing. A byte that doesn't print is shown as a dot ("Ex.if"), as in a comment.
- **A segment's length cut by the end of the file** (its first byte the file's last):
  the second byte was read as 0, and the segment parsed on. It is now an Early EOF.
- **APP12's Photoshop Save for Web quality**, 6 bytes after "Ducky", and **APP1's EXIF TIFF
  header** (8 bytes after "Exif\0\0") were read past short segments.
- **COM** text past the end of the file was shown as dots (65,530 of them).
- **At the end of a file of 4 GiB - 1 bytes,** a field past the end is past 2^32, where
  the 32-bit position wraps to the file's start, which was then read (a JFIF version of
  255.216, an interval of 65,496). Fields are checked in 64 bits.

**EXIF:**
- **Values at an offset** (those of more than 4 bytes) were never checked against the APP1
  segment. One that runs past it is now an error and its entry is skipped, and the next
  entry is read ("ERROR: IFD0 entry #0 (ImageDescription) value at [...] runs past the end
  of its APP1 segment, at [...]. Skipping."). A MakerNote's bytes aren't shown, so its value
  isn't checked: its IFD is, where it is decoded.
- **The offset to the next IFD** was read past an APP1 that ends with an IFD's entries,
  from the next segment, and that IFD looked for.
- **XPTitle and the other XP tags:** their count is of bytes, which was taken as a count
  of characters, so "ABC" read on into the next value ("ABCXYZ"); and a value of 4 bytes
  or fewer, in the entry itself, was read at those bytes taken as an offset (as ""). Both
  are fixed; **UserComment** had the second, and read 8 bytes of character code from a
  value of fewer.
- **Values from the entry before:** an entry's values weren't cleared, so decodes that
  use more values than an entry has took the previous entry's. **CFAPattern** took its
  cells from the sizes inside it, not its count ("[ Yel Mgn ]" from the entry before): a
  pattern needing more bytes than it has is now an error, and left in hex.
  **ComponentsConfiguration** shows 4 values whatever its count: with 2, after ExifVersion
  ("0230"), it showed "[Y Cb ? ?]", and now "[Y Cb . .]". A **Canon maker note** list of
  more than 150 SHORTs wasn't read at all, and listed the entry before's values.

**ICC and FlashPix:**
- **ICC:** the marker number and count, and in the first marker the profile's 128-byte
  header, were read past a short APP2 (a header from the next COM).
- **FlashPix:** a stream name was read up to 255 characters, past the segment, and the
  position moved on as if its 0 had been there; a storage's 16-byte class, the stream data
  header (32 bytes), and the version and count were read past it too.

**AVI:**
- **Stream lists:** after a stream header and format (strh, strf), parsing went on as if a
  name (strn) followed. Without one, the chunk after was taken for it and skipped: the
  next stream list, so a Motion JPEG AVI whose audio stream came first said "AVI is not
  MotionJPEG". Parsing now goes on after the list, whatever it holds (strn, strd, or an
  OpenDML index).
- **A stream header of fewer than 28 bytes** had its codec and rate read from the format
  chunk after it ("[strf]"): an error now says so, and what isn't in it isn't shown.

**Photoshop resources (APP13, and a PSD's image resources):**
- **Fixed-size resources** (Thumbnail, Version Info, JPEG Quality, Grid and Guides, Slices,
  Print Scale, Print Flags, Print Flags Information, Pixel Aspect Ratio, Global Angle and
  Altitude, Document-specific IDs seed, Resolution Info, Layer State) read their fields
  from the next resource (a thumbnail 943,868,237 pixels wide, "8BIM"). Each field group
  is now checked against its resource (as Layer Select ID's were), and a guide or slice
  field that would start inside and end past it isn't read either. JPEG Quality reads its
  fields as far as its resource goes (one of 2 bytes, the quality alone, has been seen).
- **Descriptor items** of a type that wasn't decoded (`doub`, `UntF`, `type`, `GlbC`,
  `alis`, `tdta`) left their value unread, and it was taken for the next key's length.
  They are now read (or, for data, skipped by its length). A reference (`obj `) or a type
  not known can't be skipped: "NOTE: Descriptor item type [obj ] at [...] isn't decoded,
  so the rest of its 8BIM resource isn't either", and parsing goes on after the resource.
- **Resources lost:** a resource read short (with bytes after its fields) left the position
  where its fields ended, inside it, so the resources after it were lost; one read past
  an odd length left it a byte early, with the same result; and a resource of a type not
  decoded (0x03E8) left it inside, so an "8BIM" in its data was reported as the next
  resource. Each is now skipped by its length. (Since every field is now checked, none is
  read past its length any more; that path is kept as it was, for safety.) Where nothing
  starts at the end its length gives, but a resource starts where its fields ended, the
  length is taken to be wrong, as it always was: "NOTE: No 8BIM resource where its length
  ends, at [...], but one where its fields do, at [...]: going on there".
- **A resource's header** (ID, name and length) was read past the end of its segment, and
  a cut one said "8BIM length [0x00000000] runs past the end of its segment" (listed as
  found, not fixed, above): now "ERROR: 8BIM header at [...] runs past the end of its
  segment, at [...]. Stopping."

**PSD layers:**
- **Tagged blocks:** `luni`'s name and the 4-byte values of `lnsr`, `lyid`, `clbl`, `infx` and
  `knko` were read past their block's length ("ERROR: [lyid] value at [...] runs past the
  end of its additional layer info, at [...]. Stopping.").
- **Section lengths** were only compared with the end of the file. The layer info (its
  length too), the global layer mask info and the tagged blocks are now in the layer and
  mask section; the layer count, a layer record's fields and its channels' data in the
  layer info (a channel 8 bytes too long was decoded from the image data after it); and a
  record's mask, blending ranges, name and tagged blocks in its extra data ("ERROR: Layer
  name at [...] runs past the end of the layer's extra data, at [...]. Stopping."). The
  name's pad to a multiple of 4 isn't required, for writers that don't pad.
- **What happens then:** a problem inside a record's extra data, whose length was
  checked, is reported and the record ends where its extra data does, so the other layers
  are still read (as they were, after wrong values). Past the layer info or the section,
  the rest of the layers can't be found: they are skipped, as for the other layer
  problems, and the image decoded.

**Windows** has all of these (by reading its code), but for the grid guide's: a guide's
start was checked by an earlier fix in this port, not its end.

**Tests:** 126 new `RP` cases (1,748 in all), and 2 changed: `RA_fpx_wrap` now stops at
the storage class it ends on, before the position can pass 2^32, and `PSD_addtl_wrap` at
the end of its section, before the end of the file. Each fix has a case with the error it
reports, its offsets worked out from the bytes the generator writes, and, forbidden, what
was read past it; the guards check what goes on after it (the next entry or resource, the
image, the layers), what Relaxed Parsing still decodes, and the Early EOFs. 5 are at the
end of a file of 4 GiB - 1 bytes. The driver's `decode` takes a new `--maker-notes` for the
Canon case. On the code before, 110 of the 128 fail; the other 18 are the guards. Each of
the 88 fixes, switched off on its own in a scratch copy, fails its own cases, and passes
with it on (one switch, the old path for a resource read past its length, has no case:
nothing reads past now). In each passing output, deleting the matched line or adding 1 to
a number in it fails the case, but for the decode's `status=` count.

**Checks:**
- **Only these reports change.** Old against new:
  - **Case files:** of the 400 under 64 MB, 307 are unchanged. The others are the new
    ones; the 4 `PE` files, whose EXIF values past the end of the file are now errors in
    place of empty values; `ra_dht_cut`, whose empty table read from past the end is
    gone; `psd_addtl_wrap`; and `h3_xmp_short`, whose XMP identifier is cut by its APP1's
    end (it showed "XMP =" with nothing after it).
  - **Made files:** 2,088 with these structures around their limits: each of a JPEG's
    segments from 6 bytes short to 6 long, small segments of each kind at each length,
    FlashPix, EXIF (values, XP, CFAPattern, counts), each Photoshop resource and descriptor
    item type at each length, AVI stream lists, and a file with most of them cut after
    each byte. 203 are unchanged. Every changed line is a new error, a value no longer
    read from past its field (or now read from its own), a resource or entry now read
    after one that used to lose it, or the image no longer decoded where a DHT or SOS is
    shorter than its fields (24 and 6 files; Relaxed Parsing decodes them as before).
    One change was a regression, and is the fallback above: a resource whose length says
    more than its fields had its next resource found where the fields ended.
- **The affected groups pass:** all 750 cases that don't open the window (decoding,
  export, batches, the command line, file facts, settings).
- **With ASan/UBSan,** the `RP`, `RA`, `C3`, `H`, `F2`, `PE`, `PSD` and `ok_` cases pass (251).
- **cppcheck 2.22,** at `--check-level=exhaustive`, finds nothing new in the two files or
  the driver (287 things before, 276 after: those gone were in code removed or
  rewritten). At the default level it skipped the value flow of `DecodeExifIfd`, about
  1,700 lines: a null pointer planted there was found only at the exhaustive level.
- **No new compiler warnings,** and one fewer (`nPayloadLen`, the ICC length not used).
- **The PE cases** now pass without the scan's window reload too, since EXIF values aren't
  read past the end of the file any more; `AV_stray_bytes` and `AV_stray_last_frame`
  still need it.
- **An independent review** (an agent given only the diff) re-derived the offsets of 40
  cases, and found what is fixed above: positions near the end of a file of 4 GiB - 1
  bytes that wrapped past 2^32 inside the new checks (a JFIF version of 255.216), a PSD
  layer's channel data bounded by the file only, the layer count and the layer info's
  length unchecked, a record problem that dropped the other layers, and identifiers shown
  without their bytes that don't print.
- **Not checked:** the cases that open the window, a real camera's or Photoshop's file.

**Found, not fixed:**
- **A resource's name** is read but not shown: every 8BIM line says Name="" (inherited).
  (Fixed since: see "Offsets that wrapped, and Photoshop resource names".)
- **The global layer mask info's** 17 fixed bytes are read whatever its length; they aren't
  shown, and the position goes by the length, so nothing changes in the report.
- **"8BIM header ... runs past the end of its segment"** says "segment" for a PSD's image
  resources section too, as the existing "8BIM length" error does.

### Offsets that wrapped, and Photoshop resource names

The two "Offsets that wrap past 2^32" left in the audit above are fixed, with the rest of
their class found in the same functions, and Photoshop resources show their names. An
offset or size from the file is now added in 64 bits, and where it lands past the end of
the file an error or note says so.

**The EXIF thumbnail:**
- **Its offset,** from the TIFF header, was added in 32 bits: one that took it to 2^32
  wrapped to the file's start, and the main image was reported as the thumbnail (its
  markers and signature). Now "ERROR: EXIF thumbnail at [0x100000000] is past the end of
  the file, at [...]", as for an offset past the end short of 2^32 (read as zeros there:
  "Expected marker 0xFF, got 0x00" past the end).
- **A compression of 6 with no offset** (JPEGInterchangeFormat) took the offset as 0: the
  main image again. Now "ERROR: EXIF thumbnail has no offset (JPEGInterchangeFormat)".
- **A thumbnail that runs past the end of the file** had its markers and DQT tables read
  past it as zeros (a DQT length of 0xFFFF gave 1,011 tables). At the end of the largest
  file its DQT's end wrapped, so its table wasn't read and an "EOI" was read from inside
  it. Its markers now stop at the end of the file ("ERROR: EXIF thumbnail runs past the
  end of the file"), and a DQT table or frame header is in its segment ("ERROR: Thumbnail
  DQT table at [...] runs past the end of its DQT segment, at [...]. Stopping.").
- **Its scan data's error** named the thumbnail's length also where the end of the file
  stopped it (listed above as found, not fixed): it now says which ("Thumbnail scan data
  runs past the end of the file").

**AVI:**
- **Chunk sizes:** a chunk's end was added in 32 bits, so a size of 0xFFFFFFF9 to
  0xFFFFFFFF wrapped to just past its start (the last 2 bytes of its size and "AB" were
  listed as a chunk "AB", and the movi list after it was never reached), and 0xFFFFFFF8
  to its start ("AVI chunk size points backwards"). The main header (avih) likewise. Now
  "NOTE: AVI chunk [JUNK] at 0x... (4294967293 bytes) runs past the end of the file.
  Stopping AVI parsing." The note is also given where a chunk is cut short by the end of
  the file, a recording cut short, which ended the parsing there without a word; what is
  found is the same. A last chunk without its pad byte is whole, and zeros at the end are
  zeros, as before.
- **A chunk's header and a list's fields** (its type, its first chunk's name and size, and
  the stream header's type, codec and rate) were read past the end of the file, as zeros
  (a rate of 0.00 frames a second, an IDIT's empty timestamp), and at the end of the
  largest one from its start (2.33): "NOTE: AVI chunk [strl] at 0x... is cut by the end
  of the file. Stopping AVI parsing." A stream header cut after its codec still shows its
  type and codec, so the AVI is still found Motion JPEG; one shorter than its fields
  keeps its own error; and a whole list that ends with the file before its first chunk
  (an empty INFO list) is passed, as before.

**Others of the class:**
- **APP14's Adobe fields** (version, flags and transform) were read past the end of the
  file (a transform of 0, taken as RGB), and at the end of the largest one from its start:
  "ERROR: Adobe fields at [...] runs past the end of its APP14 segment, at [...].
  Skipping."
- **With Relaxed Parsing,** a field past the end of the file was read on, as only the
  segment's end was compared with the file's: at the end of the largest file a JFIF
  header came from its start (a density of 55551 x 57344). It is an Early EOF whatever
  the options.
- **APP13's end,** passed to the Photoshop parser, was added in 32 bits: at the end of the
  largest file it wrapped, and no resource was read.
- **APP0's JFIF thumbnail** is in the file as well as its segment (it was shown as zeros
  past the end).

**Photoshop resource names:** every 8BIM line said Name="", a variable that was never set,
in place of the name read. The name is shown, with a byte that doesn't print as a dot,
as in an APPn identifier.

**Windows** has all of these (by reading its code).

**Tests:** 29 new cases (1,777 in all): 24 `OW`, 5 of them at the end of a file of 4 GiB
- 1 bytes, 4 `PN` and 1 `H5` (the length stopping a thumbnail's scan data). 4 changed:
`H4_avi_loop`, whose JUNK chunk of 0xFFFFFFF8 runs past the end of the file rather than
pointing backwards; `H5_thumb_sos` and `H5_thumb_sos_eof`, whose scan data the file's end
stops; and `WR_thumb_dqt`, whose DQT table, 3 bytes at the end of the largest file, is
now stopped at its segment's end. On the code before, 27 of the 33 fail; the other 6
check what was right (an AVI's Motion JPEG still found, twice; a whole last chunk and an
empty last list, with no note; a value after a named resource; the length's message).
Each of the 19 fixes, switched off on its own in a scratch copy, fails its own cases,
and passes with it on. In each passing output, deleting the matched line or adding 1 to
a number in it fails the case.

**Checks:**
- **Only these reports change.** Old against new:
  - **Case files:** of the 415 under 64 MB, 392 are unchanged. The others are 16 new ones;
    `h4_avi_loop` and `av_idit`, whose wrapped chunk sizes now run past the end of the
    file rather than pointing backwards; `av_stray_bytes`, `av_stray_idit_cut` and
    `av_stray_list_cut`, which end in a chunk cut short and now say so; and
    `h5_thumb_sos` and `h5_thumb_sos_eof`.
  - **Made files:** 656 with an EXIF thumbnail at offsets across and past the end of the
    file and past 2^32 (with its own length, 0 and 0xFFFFFFFF), each chunk size of a
    small Motion JPEG AVI from 8 short to 8 long and up to 2^32 - 1, the AVI cut after
    each byte, and resource names of each length from 0 to 6. 73 are unchanged. Every
    changed line is a new note or error, a value no longer read from past the end of the
    file or, after a wrap, from its start (a thumbnail's markers, a stream header's
    fields, a timestamp), or a resource's name. No AVI lost "AVI is MotionJPEG". Where a
    chunk size near 4 GiB wrapped the position into the chunk's own zeros, the chunks
    after it were found by that accident (an IDIT and the movi list, in 2 files); a size
    past the end of the file that doesn't wrap stopped the parsing there before, as both
    do now. A stream header cut 4 to 7 bytes into its fields no longer shows its type
    (its codec, cut, was shown in part).
  - **Last round's 2,088 made files** are all unchanged.
- **The affected groups pass:** all 780 cases that don't open the window (decoding,
  export, batches, the command line, file facts, settings), with the `RP`, `OW`, `PN`,
  `AV`, `H4`, `H5` and `WR_thumb` cases.
- **With ASan/UBSan,** the `OW`, `PN`, `RP`, `RA`, `PE`, `PSD`, `ok_`, `H`, `C3`, `F2`, `AV`
  and `WR_thumb` cases pass (310).
- **cppcheck 2.22,** at `--check-level=exhaustive`, finds nothing new in the two files (243
  things before, 241 after; the one added is an old finding under the variable's new
  name). One of those gone was the resource name's bug: "Variable 'strResName' is
  assigned a value that is never used".
- **No new compiler warnings** (66 in the two files before and after).
- **Two independent reviews** (agents given only the diff). The first found the rest of
  the class fixed above: the thumbnail's DQT end that wrapped, a thumbnail at an offset
  inside the file that ran past its end, a compression of 6 with no offset, the stream
  and header lists' fields, the JFIF header under Relaxed Parsing, APP14's fields, and
  the APP13 end with no case. The second, of the AVI changes, found an empty INFO list at
  the end of a file given a "cut" note (fixed), and the stream header's type above.
- **Not checked:** the cases that open the window, a real camera's AVI or Photoshop's file.

### Where the marker list ends

Three things found earlier and left are fixed, with the rest of their class: data said to
be after an EOI that was never found, an error at the end of a file that ends right after
a segment, and fill bytes before a restart marker. All are inherited from Windows.

**Data after EOF:** "NOTE: Data exists after EOF" counted from the EOI's position, which
is 0 when decoding stopped at an error before any EOI: the whole file was said to be after
it ("range: 0x00000000-0x00000286 (646 bytes)" for a file of 646 bytes). The note is now
given only after an EOI, as before.

**A file that ends right after a whole segment:** the next marker was read at the end of
the file, where the 0 read gave "ERROR: Expected marker 0xFF, got 0x00 @ offset [the
end]". It is now an Early EOF ("ERROR: Early EOF - file may be missing EOI"), as for a
segment cut short; also at the end of a file of 4 GiB - 1 bytes, where the position wraps
to 0. A byte that isn't 0xFF inside the file is still "Expected marker". A start offset at
or past the end of the file, which only the test driver can give (File > Offset and the
command line refuse one), is now an Early EOF too, where it said "Expected marker 0xFF,
got 0x00".

**Fill bytes before a restart marker** (0xFF bytes, which T.81 B.1.1.2 allows before any
marker):
- **The report's marker list** took the first fill byte for the end of the scan data, and
  the RST after it for a marker outside the scan: "WARNING: Restart marker [0xFFd0]
  detected outside scan", then "Stopping decode". No EOI was found, so Export JPEG
  exported nothing ("The image has no end-of-image marker"), and the whole file was said
  to be after the EOF (above). Fill bytes before an RST are now skipped with it. Before
  any other marker they are still the marker's own, skipped where it is read ("*** Skipped
  2 marker pad bytes ***"), and fill bytes that run to the end of the file are an Early
  EOF; none is read past the end.
- **The scan's decode** (found by the first review below; the earlier tests had 2 fill
  bytes at most): it took fill bytes for data, 1 bits. With 2 of them a restart could be
  missed ("ERROR: Restart marker not detected", once in a made file whose image was still
  right); with 3 or more restarts were missed, and the MCUs after them decoded wrongly
  ("Bad scan data in MCU(1,0)"), in the preview, Detailed Decode and Lookup MCU Offset. They are now skipped, to the RST's own 0xFF. A run of 0xFF found not
  to end in an RST is remembered until the next restart or decode, as that search goes to
  the run's end: without that, a scan the decoder reads through (its Huffman tables can
  make "1" a valid code) took over a minute for a run of 1 MB, which it reads in 1 s.
- **The EXIF thumbnail's marker list** didn't skip fill bytes at all: the second 0xFF was
  taken for the marker's code ("* Embedded Thumb Marker: (0xFFff)"), with a length from
  the bytes after it (53,500), so the rest of the thumbnail and its signature were lost
  ("ERROR: Read more than specified EXIF thumb nLength (1000 bytes) before EOI"). Its scan
  data, like the main image's, ended at a fill byte before an RST. Both now skip them.

**Windows** has all of these (by reading its code).

**Tests:** 29 new `ML` cases (1,806 in all), 3 of which open the window: Lookup MCU Offset
after an RST with 3 fill bytes before it, twice, and two files decoded one after the
other. On the code before, 23 fail; the other 6 check what was right (data after an EOI
still counted from it, a 0 inside the file still "Expected marker", fill bytes before an
EOI still skipped there, a thumbnail's scan without fill bytes counted as before, a Pillow
JPEG's image without fill bytes, and the 1 MB run read in time and as far as before). Each
of 10 switches, put in a scratch copy, fails its own cases when on and passes when off:
one for each fix, and one each for the search's bound at the end of the file (without it,
"Ran out of buffer"), the run remembered (without it, the 1 MB run times out) and
forgotten at the next decode (without that, a second file's fill bytes inside the first
file's run were taken for data). In each passing output, deleting the matched line or
adding 1 to a number in it fails the case.

**Checks:**
- **Only these reports change.** Old against new:
  - **Case files:** of the 431 under 64 MB, 414 are unchanged. The others are 11 new ones;
    `lm_rst_fill`, whose fill bytes before its RSTs stopped the marker list; `ns_nosos`
    and `r18_bufx_end`, which end after a segment; and `c5_second_sof_h0`, `ns_comps` and
    `rp_sos` (and `ns_nosos`), which lose the whole-file note.
  - **Made files:** 2,247 for this round: JPEGs with 0 to 3 fill bytes before each RST cut
    after each byte, Pillow's with restart markers and 0 to 2 fill bytes cut every 13
    bytes, fill bytes before the EOI and what may follow or replace it, fill bytes before
    each header segment and files ending after each, and an EXIF thumbnail with RSTs and
    fill bytes, cut after each byte, with fill bytes before each of its markers. 582 more
    have 3 to 8 fill bytes. 1,673 of the 2,829 are unchanged. Every changed line is one of
    these: the whole-file note gone; "Expected marker" at the end of the file replaced by
    Early EOF; the marker list going on past RSTs with fill bytes, to the EOI or to the
    end of a file cut short; the thumbnail's markers and signature; and the decode of
    files with fill bytes. Those now decode with no error, to the same image whatever the
    number of fill bytes, 0 to 8, and the restart interval, which is the image without
    them (Pillow's included). The only errors added are in 141 files cut short, all in
    their last 12 bytes, with no RST after them in the file.
  - **The last two rounds' made files:** the 656 are unchanged; of the 2,088, 60 lose the
    whole-file note, and 16 cut after a segment are now Early EOF (5 of them also lose the
    note).
  - **In all of them,** each of the 1,167 notes gone was from 0 to the file's size, and
    each of the 55 "Expected marker" errors replaced was at the file's end.
- **The affected groups pass:** 909 cases: all those that don't open the window (decoding,
  export, batches, the command line, file facts, settings), with the `ML`, `LM` (Lookup
  MCU Offset, which open it), `OW`, `PN`, `RP`, `AV`, `H4`, `H5`, `WR_thumb` and `NS`
  cases, and the `FF_size` and `RA_thumb_rst` ones, whose files have fill bytes before an
  RST. The 4 `ML` cases added after that run pass with the others (29).
- **With ASan/UBSan,** the `ML`, `LM_rst`, `OW`, `PN`, `RP`, `RA`, `PE`, `PSD`, `ok_`,
  `H`, `C1`–`C5`, `F`, `AV`, `WR_thumb`, `D`, `MP`, `R13`–`R18` and `NS` cases pass (496,
  and the 4 later `ML` ones).
- **cppcheck 2.22,** at `--check-level=exhaustive`, finds the same 235 things in the two
  files as before, and catches a bug planted in each function changed but the constructor
  (6 plants, two of them in `DecodeMarker()`).
- **No new compiler warnings** (202 in the two files before and after).
- **Two independent reviews** (agents given only the diff). The first found the scan's
  decode with 3 or more fill bytes, which a case of 3 hid by checking only the marker
  list; a case that couldn't fail without its fix (a thumbnail's fill bytes to the end of
  the file); the start offset's message (above); and Relaxed Parsing's RST (below). The
  second found no bug, and cases missing: the 1 MB run's result, the run forgotten at the
  next decode, Detailed Decode after an RST with fill bytes, and a Pillow JPEG with AC
  data. All are added.
- **Not checked:** a camera's JPEG with fill bytes before its restart markers, and
  Detailed Decode and Lookup MCU Offset in the app's own window.

**Found, not fixed:**
- **With Relaxed Parsing, an RST outside a scan** is checked for a marker 2 bytes after
  the one that follows it, and the parsing goes on 2 bytes too far: for
  `FF D8 FF D0 FF FE 00 04 61 62 FF D9`, "Header length = 65534" and Early EOF, not the
  COM (inherited).
- **Fill bytes before a stuffed 0xFF00** in scan data (`FF FF 00`, which isn't valid: a
  fill byte comes before a marker): the decoder takes it for data, but the marker list
  says "WARNING: Unknown marker [0xFF00]" and stops, so no EOI is found. (This said the
  decoder reads it as libjpeg does. It doesn't: it took each 0xFF for data, where
  libjpeg documents one 0xFF for the run.)
- **The thumbnail's "Skipped N bytes"** counts from the SOS's length, its header included,
  and leaves out the 0xFF of each RST (inherited).

(All three fixed since: see "A stray RST, fill bytes before a stuffed 0xFF, and the
thumbnail's count".)

### A stray RST, fill bytes before a stuffed 0xFF, and the thumbnail's count

The three things the section before found and left are fixed, with the rest of their
class that six reviews of the changes found. All are inherited from Windows (by reading
its code).

**An RST outside a scan, with Relaxed Parsing:**
- **It is passed over when a marker follows it.** That was looked for 2 bytes further on,
  past the byte after the RST: before a COM, the COM's marker (FF FE) was taken for the
  RST's length, 65534, so neither the COM nor the EOI was read ("Early EOF"); where a
  0xFF was found there, the parsing went on from it, 2 bytes too far, so a second RST in
  a row was passed over unseen. Now the byte right after the RST is checked, and one
  that ends the file is passed over too (its "length" was read past the end).
- **Otherwise it is taken to have a length only if that ends at a marker or the end of
  the file.** Any 2 bytes were taken for one (0x1234 ran past the end of the file). If
  not, the bytes after it aren't a marker, which reading the next one reports ("Expected
  marker 0xFF, got 0x12"). Two such bytes used to be passed over, by the 2 bytes too far.
- Without Relaxed Parsing it still stops the decode.

**TEM** (0xFF01) has no length (T.81 Table B.1, as RSTn, SOI and EOI), but its next 2
bytes were read as one, with or without Relaxed Parsing: before a COM it took a length of
65534, as the RST did. Likewise in the EXIF thumbnail's marker list.

**Fill bytes before a stuffed 0xFF** in scan data (`FF FF 00`), which T.81 doesn't allow,
as a fill byte comes before a marker:
- **The report's marker list** took them for a marker 0xFF00 and stopped ("WARNING:
  Unknown marker [0xFF00]"), so no EOI was found.
- **The decoder** took each 0xFF for data, 8 more 1 bits for each fill byte, so the MCUs
  after it were decoded wrongly.
- **The EXIF thumbnail's marker list** took them for its marker "(0xFF00)".

The run is now one 0xFF of data in all three, as IJG libjpeg documents ("multiple FF's
followed by a 0 as meaning a single FF data byte") and libjpeg-turbo's djpeg 3.2.1 read
7 of 10 tried (its fast path reads the others differently), and the report says so once
for the main image and once for the thumbnail: "WARNING: 0xFF fill bytes from 0x000000a2
before a stuffed 0xFF00 (2 runs in the scan data), which T.81 allows only before a marker:
each run is read as one 0xFF of data". (The section before said the decoder read it as
libjpeg does; it didn't.)

**Detailed Decode's "Data=" bytes** for a code are now the bytes the decoder read: they
showed fill bytes, before a stuffed 0xFF (in the code's bits: they contradicted its value)
or before an RST (after them), and past the end of a file of 4 GiB - 1 bytes they came
from its start (they are 0, as the decoder reads them).

**A run of 0xFF is searched to its end once.** The decoder and Detailed Decode remember
the last 8 runs, with whether each is skipped. Each version with less, made by these
changes and found by the reviews, was slow on crafted files: a run of 1 MB the decoder
reads through took over a minute (the section before); one before an EOI took 12 to 31 s
for 2 MB, as the byte after it, read again for each 0xFF, made the file's window load
twice each time (HEAD: 0.3 s); and Detailed Decode searched a run again for each code
near it (16 MB before an RST: 1.9 s, HEAD 0.4 s). On the files measured, each is now
within 0.1 s of HEAD where HEAD reads them to the end (it stops at fill bytes before a
stuffed 0xFF).

**The thumbnail's "Skipped N bytes"** counted from the SOS's length, so its header too,
and left out each RST's 0xFF and the fill bytes before it. It is now the scan data's
bytes: from the end of the header, where the main image's scan starts (6 + 2 Ns bytes from
its length, whatever the length says; more than 4 components stop it, as they stop the
main image's decode), to the marker that ends the scan, or to where the end of the file or
the thumbnail's length stopped it (not the byte after the thumbnail, which it took). Fill
bytes before that marker are the marker's. A marker's length cut by the end of the file
was read past it as 0 ("Length = 0"), and at the end of a file of 4 GiB - 1 bytes from its
start (a scan header of 216 components): it is "ERROR: EXIF thumbnail runs past the end of
the file".

**Tests:** 45 new `MR` cases (1,851 in all), and the 2 `ML` thumbnail counts changed. On
the code before, 36 of the 45 and the 2 fail; the other 9 check what was right (an RST
followed by a length, also one ending at the end of the file, still skipped with it; a
2 MB run before an EOI decoded in time, and shown byte by byte; a stuffed 0xFF without
fill bytes not warned about; a scan header whose length is wrong; the errors for a
thumbnail longer than its length and for a scan header cut by the end of the file; and
the end of a file of 4 GiB - 1 bytes in a scan header). The `ML` cases with fill bytes
before an RST now also check that it isn't warned about. Each of 28 switches, put in a
scratch copy, fails its own cases when on and passes when off: one for each fix, and one
each for the bounds at the end of the file and 2^32, the runs remembered (and forgotten
at the next decode), and the display's bytes. How many runs are remembered isn't tested:
it changes the time by a constant factor. In each passing output, deleting the matched
line or adding 1 to a number in it fails the case.

**Checks:**
- **Only these reports change.** Old against new:
  - **Case files:** of the 457 under 64 MB, 433 are unchanged: the others are 8 whose
    thumbnail's count changes (`h5_thumb_sos`, `h5_thumb_sos_eof`, `h5_thumb_sos_len`,
    `r13_be_thumb` and 4 `ml_thumb`) and 16 new ones. With Relaxed Parsing, also the 5
    new ones with a stray RST.
  - **Made files:** 978 for this round: 81 with 1 or 2 RSTs (0 to 2 fill bytes before)
    before each header segment, at the end of the file, before the EOI and followed by
    "lengths" of 0 to 65535; 607 with 1 to 4 fill bytes before every, the first or the
    last stuffed 0xFF of Pillow's and the test generator's scans, whole and cut; and
    thumbnails with scan headers of 1 and 3 components, RSTs and fill bytes, and `FF FF
    00`, whole and cut after each byte from the SOS on.
    - **With Relaxed Parsing,** 75 of the 81 change, none without it. Each of the 54
      with RSTs among the header segments now gives the report of the file without them
      but for the RSTs' own lines (27 didn't before); the 6 that end in an RST, or with
      one byte after it, pass over it; 12 followed by a "length" of 0, 1, 2 or 6 bytes
      that doesn't end at a marker report those bytes; and 3 followed by FF FF and two
      letters take the FF FF for fill bytes before a marker 0xFF61 ("a"), where it was a
      length of 65535 that ran past the end of the file; both stop there.
    - **Each of the 607** decodes as the same file without the fill bytes (the preview's
      MD5, the errors, and Detailed Decode's line for every code, its bytes shown
      included), and warns once with their number. So do the 69 made in the section
      before with 1 to 8 fill bytes before their RSTs, without a warning.
  - **The last three rounds' made files:** of 5,573, 141 change: 104 a thumbnail's count,
    36 a thumbnail marker's length cut by the end of the file (now the end-of-file error
    alone), and one `FF FF 00` before the EOI, which now reaches it.
  - **Every thumbnail count** in all of these (873) is the one worked out from the file's
    bytes, by a separate walk of the thumbnail.
- **The affected groups pass:** 957 cases: all those that don't open the window
  (decoding, export, batches, the command line, file facts, settings), with the `MR`,
  `ML`, `LM` (Lookup MCU Offset, which open it), `OW`, `PN`, `RP`, `AV`, `H4`, `H5`, `WR`,
  `RA_thumb`, `R13`, `NS` and `MP` cases; and the `MR` and `ML` ones (74) added after.
- **With ASan/UBSan,** the `MR`, `ML`, `LM_rst`, `OW`, `PN`, `RP`, `RA`, `PE`, `PSD`,
  `ok_`, `H`, `C1`–`C5`, `F`, `AV`, `WR_thumb`, `D`, `MP`, `R13`–`R18` and `NS` cases
  pass (543, and the `MR` and `ML` ones again after).
- **cppcheck 2.22,** at `--check-level=exhaustive`, finds the same 235 things in the two
  files as before, and catches a bug planted at each place changed.
- **No new compiler warnings** (202 in the two files before and after).
- **Six independent reviews** (agents given only the diff; the later ones of what was
  reworked after the one before). Between them they found: junk after a stray RST taken
  for a length; TEM's length; Detailed Decode's bytes across a run, and past 2^32; the
  thumbnail's header taken from its length, its count past its end, a field read from
  the file's start at 2^32, and Ns over 4; the warning's count and wording; and the four
  slow cases above. The last found no bug; its suggested cases are added.
- **Not checked:** a camera's or an editor's file with any of these.

**Found, not fixed:**
- **Two RSTs in a row** (an empty restart interval) make Detailed Decode report a code at
  [0x00000000.0] showing the file's first bytes: after the first restart, the buffer
  stops at the second RST, so the code's position is never set (inherited; the scan's
  decode and the preview are right). (Fixed since, and the decode was wrong too: see the
  next section.)

### Two RSTs in a row

The section before left one thing found, and it was worse than it said: the decode was
wrong too, not only Detailed Decode. Inherited from Windows (by reading its code).

**An RST right after another** in scan data (an empty restart interval, which T.81
doesn't allow: each interval but the last holds Ri MCUs). After the first restart the
decoder found the second before any data, and went on with no code read:
- **The block took that for its first coefficient,** a 0, so its real DC was decoded as
  AC coefficient 1: the block was grey (128; with the AC shown, a cosine), and the rest of
  its interval, whose DCs are differences from it, was off by as much (MCU [3,0] of
  `er_interval.jpg`: 165 for 151).
- **Detailed Decode showed that code at [0x00000000.0],** with the file's first bytes, and
  **Tools > Lookup MCU Offset** gave the MCU's offset as 0.
- **Each RST more in a row made another such code:** 1 MB of them (2^19) filled that
  block and each block after it with 64, so the scan's decode ended without reaching the
  EOI, 449 RSTs in ("Next position in scan buffer: Offset 0x00000000.0").
- A debug build stopped there, at a `Q_ASSERT` that two RSTs never come in a row (by its
  code).

Now each RST is passed over in turn until a code is read, in the decoder and in Detailed
Decode, so the file decodes as the same file without the extra RSTs. That is how
JPEGsnoop already takes an RST that comes early (a short interval): it decodes on from
the RST, with no message. libjpeg-turbo's djpeg 3.2.1 instead keeps each interval's MCUs
where Ri puts them: it warns ("Corrupt JPEG data: premature end of data segment"),
decodes the empty interval's MCU as grey, and the rest one interval later. Which is
right depends on whether an interval's data was lost or an RST was added.

**An RST with the wrong index is now reported up to the scan's error limit**
(`ErrMaxDecodeScan`, 20 by default), as its other errors are, on a count of its own (on
theirs, 20 of these hid the scan's later messages: its EOI, and the error for a file cut
short). It had no limit, and with the loop above a run of RSTs is read to its end: 1 MB
of RST0s, each with the wrong index, logged 524,288 errors in 88 s. (The code before
stopped after 449, when the codes it made had filled the image. A larger image already
logged them all: 2^15 in 7.5 s.) It now takes 0.3 s, as before.

**Tests:** 25 new `ER` cases (1,876 in all). On the code before, 19 fail; the other 6
check what was right: the file without the extra RST, one RST at the start of the scan
data, one inside an MCU, and three that its errors without a limit met (the 20th, a
second file's after a first with 24, and a cut file's after 20). Their values are worked
out from the generator's bytes:
- the code's position after the RSTs and its DC, and the pixel's level, with and without
  the AC shown;
- three in a row; fill bytes before the second; two at the start of the scan data; a
  stuffed 0xFF between the two (bits that never make a code); two between a block's DC
  and its EOB;
- two inside an MCU, before its Cb block, in an interval of 2, where the restart resets
  Cb's predictor (levels far from 128; the RGB worked out as JFIF converts it);
- an interval of 2, whose second MCU's DC follows the first's;
- a Pillow 4:2:0 file with AC data and three in a row, whose image is the same as without
  them; and Lookup MCU Offset;
- 1 MB of RSTs in a row, in order and all RST0, each decoded in 5 s; of the latter's
  errors, the 20th and not the 21st, and the EOI still reported; the error for a file cut
  after 20 still reported; and the count started again for a second file.

The test generator's `dc_only_jpeg()` can now put an RST inside an MCU; the files it made
before are byte for byte the same. Each of 6 switches, put in a scratch copy, fails its
own cases when on and passes when off: each decoder's loop run once (the `if` it was),
and the error without a limit, on the scan's count, with `<=` for `<`, and with its count
not started again at each decode. In each passing output, deleting the matched line or
adding 1 to a number in what it checks fails the case.

**Checks:**
- **Made files:** 261, from 8: 5 from the test generator (grey and colour; 4:4:4, 4:2:0
  and 2:1; intervals of 1 to 3 MCUs; one with fill bytes before its RSTs) and Pillow's 3
  with RSTs, with AC data. Each has 1 to 3 RSTs more, each after 0, 1 or 3 fill bytes, at
  the start of the scan data and after its first, middle and last RST, all renumbered in
  order. Each decodes as its file without them: the preview's MD5 with and without the
  AC, the errors, the RSTs counted (that many more) and Detailed Decode's every code
  (32,634), at its position moved by the bytes put before it. On the code before, 237
  didn't; the 24 that did have only one RST at the start of the scan, none in a row.
- **Only these reports change.** Old against new, with and without the AC shown (and the
  preview's MD5): of the 458 case files under 64 MB, none (two of 89 megapixels, which
  take 20 to 30 s, timed out under the load, and are the same when given the time); of
  the 6,551 made in the rounds before, none; of the 270 made here, the 237 with RSTs in a
  row; and of the first 9 `ER` files, the 7 with RSTs in a row. Of the 7,291 files, only
  4 have an RST with the wrong index: the 3 made before are the same, and
  `er_run_index.jpg` reports 20. (`er_index24.jpg` too, of 24; `er_index_cut.jpg`'s 20
  are followed by "Only reported first 20 instances of this message...".)
- **Last round's checks still hold:** the 607 files with fill bytes before a stuffed
  0xFF, and the 69 with fill bytes before their RSTs, decode as without them, Detailed
  Decode included.
- **Speed:** each RST in a row costs about 15 ns: 16 MB of them take 0.4 s of CPU, 0.12
  s more than the code before (0.27 s more with Detailed Decode), which took 64 for each
  block and never read the rest.
- **The affected groups pass:** 941 cases: all those that don't open the window, with the
  `LM`, `ML`, `MR` and `ER` cases.
- **With ASan/UBSan,** the `ER`, `ML`, `MR`, `LM_rst`, `D`, `ok_`, `H`, `C1`–`C5`, `F`,
  `R13`–`R18`, `NS`, `MP`, `RA` and `WR` cases pass with the loop (361), and the `ER`,
  `LM_rst`, `D`, `H`, `C1`–`C5`, `R13`–`R18`, `ML_fill`, `MR_stuffed` and `MR_fill` ones
  (153) with the final code.
- **cppcheck 2.22,** at `--check-level=exhaustive`, finds the same 73 things in
  `ImgDecode.cpp` as before, and catches a bug planted in each loop and in the error's
  limit and count.
- **No new compiler warnings.**
- **Two fresh reviews** (agents given only the diff; the second of what changed after the
  first) found no bug in the loop. The first tried RSTs in a row inside an MCU and between
  a DC and its EOB; followed by the end of the file, an EOI, fill bytes, a wrong index or
  bits that never make a code; and at the end of a file of 4 GiB - 1 bytes. It found the
  error without a limit, and cases missing (inside an MCU; bits between the RSTs). The
  second found that on the scan's count the error hid its later messages, and cases
  missing (a `<=` for `<`; between a DC and its EOB; a restart in an MCU that resets a
  predictor). All fixed above.
- **Not checked:** a camera's or an editor's file with RSTs in a row.

**Found, not fixed (all inherited):**
- **A code cut by its interval's RST stops the scan.** In an interval that ends inside a
  code (a single byte FE: a DC code whose 10 value bits the RST cuts), the decoder takes
  more bits than its buffer holds, so the restart is never seen: each MCU after it is
  grey, 2 of the 7 RSTs are decoded, and the errors are at 0x00000000.2. libjpeg-turbo's
  djpeg loses only that MCU. (Fixed since: see the next section.)
- **A short or empty restart interval isn't reported:** JPEGsnoop decodes on from an RST
  that comes before Ri MCUs, with no message, where libjpeg warns. (Fixed since: see
  "Restart intervals where the DRI puts them".)
- **A restart inside an MCU zeroes the DC the status bar shows for its luminance:** over
  MCU [1,0] of `er_mid_one.jpg` it shows "YCC DC=[00000,00416,-0384]", where Y is -144.
  The restart clears the MCU's DCs kept for it after its Y block was decoded. Its pixels
  are right. (Fixed since for files with a DRI: see "Restart intervals where the DRI puts
  them".)
- **A negative "Max # errors" in Options is no limit:** -1 is taken, and read as
  4,294,967,295 (by reading the code).

### A code cut short by an RST

The first of the section before's four, inherited from Windows (by reading its code).

**A restart interval whose data ends inside a code's value bits** (its data was lost or
damaged: an encoder ends each interval with whole codes, then 1 bits to the byte). The
decoder took the code whole, with zeros for the bits past the RST, so it took more bits
than its buffer held and never handled the restart:
- **The scan ended there,** with "Overread scan segment (after bitstring)": each MCU after
  it was grey (128), and only the RSTs before it were counted (2 of 7 in a file whose
  second interval was cut to one byte, FE: a DC code whose 10 value bits the RST cuts).
- **Its errors were at 0x00000000.2,** and so was "Next position in scan buffer".
  Detailed Decode stopped there too, and **Tools > Lookup MCU Offset** gave the cut code's
  offset for its MCU.

Now the code is dropped, as the bits before an RST that make no code already were, and
read again after the RST, in the decoder and in Detailed Decode. An error says where it
was: "\*\*\* ERROR: Huffman code @ 0x000000a0.0 in MCU(1,0) cut short by RST1 @
0x000000a1.0", reported up to the scan's error limit, as its other errors are, but on a
count of its own, as the RST index error's is (on theirs, 20 of these hid the error for a
file cut short after them; found by the review). Nor is the code counted in the code-length
histogram. So an interval that holds only a cut code is passed over: the file decodes as
without it, as with an empty interval (the section before). A cut after an MCU's first code
goes on as any restart there does: inside a block (an AC code), the block goes on after the
RST, and in a later block (Cb, say), that block is read from there.

This is how JPEGsnoop takes any short interval: it decodes on from the RST. libjpeg-turbo's
djpeg 3.2.1 instead decodes the cut code with zeros for its missing bits, warns ("Corrupt
JPEG data: premature end of data segment") and keeps each interval's MCUs where Ri puts
them. So for the file above, whose second interval was cut short, djpeg loses only that
MCU (it is 0), where JPEGsnoop now shows each MCU after it one MCU early, and the last MCU,
left without data, with errors. For an interval put in (the tests' files), JPEGsnoop's
image is the file's without it, and djpeg's is one MCU late from there.

**Tests:** 31 new `RC` cases (1,907 in all). On the code before, 30 fail; the other checks
that a code whose value bits end right at the RST is decoded, not dropped. Their values are
worked out from the generator's bytes, and from Pillow's tables read from its file:
- the error and its position, in the decoder and in Detailed Decode; the code's position
  after the RST and its level; the pixel, with and without the AC shown; the image's last
  MCU, and the RSTs counted (all 8; 2 were); the code histogram; and Lookup MCU Offset;
- the scan's first code cut at a byte, with none of its value bits; three in a row, each
  error where its code starts; with an empty interval before it and after it; Cb's DC cut
  inside an MCU; an AC code cut, in Pillow's `d_restart.jpg`; and one whose RST is read
  only after its code is (a 16-bit code at bit 7 of 4 bytes, the buffer's fill, with a
  stuffed 0xFF in it), when the buffer is filled again for its value bits;
- 768 KB of them (2^18), passed over in 5 s: the error's 20th and not its 21st; the error
  for a file cut short after 20 of them; theirs after the scan's other errors reach their
  limit, and after an RST index error each; and their count started again for a second
  file.

Each of 15 switches, put in a scratch copy, fails its own cases when on and passes when
off: the check off, with `>=` for `>`, and made before the buffer is filled again; the
histogram not put back; each decoder's loop without the cut, and without its report; the
report at the code's end; and its limit with `<=` for `<`, with no count, on the scan's
count or the RST index error's, also held by the scan's, and not started again at each
decode. In each passing output, deleting the matched line or adding 1 to a number in what
it checks fails the case.

**Checks:**
- **Made files:** 696, from the section before's 8 (5 from the test generator and Pillow's
  3 with RSTs), each with 1 to 3 intervals put in, each of a DC code cut short (for 9, 10,
  7 and 11, with 1, 0, 3 and 7 of their value bits: the last with a stuffed 0xFF) and 0 or
  2 fill bytes before its RST, at the start of the scan data and after its first, middle
  and last RST. Each decodes as its file without them: the preview's MD5 with and without
  the AC, the errors (and one more for each cut code, at its start, in the decoder and in
  Detailed Decode), the RSTs counted, the code histogram and Detailed Decode's every code
  (87,024), at its position moved by the bytes put before it. On the code before, none did.
- **Only these reports change.** Old against new, with and without the AC shown (and the
  preview's MD5): of the 483 case files under 64 MB, the 7 with a cut code (one of 89
  megapixels timed out under the load, and is the same when given the time); of the 6,821
  made in the rounds before, none; and of the 696 made here, all. The first fix, on the
  scan's count, against the last: only the 6 files where that count hid a line differ, each
  by that line (the EOI's, the error for a file cut short, or a cut after other errors).
- **Speed:** the 768 KB of cut intervals take 0.24 s of CPU (0.27 s with Detailed Decode).
  A 5 MB file without any takes as long as before (1.45 s against 1.43 s, within its
  spread), and so do 1 MB of RSTs in a row with Detailed Decode. (The review's 65,536 cut
  intervals of 64 fill bytes each, 4.3 MB, take 0.3 s.)
- **The affected groups pass:** 972 cases: the section before's 941 and the `RC` ones.
- **With ASan/UBSan,** the section before's 153 cases and the `RC` ones pass (184).
- **cppcheck 2.22,** at `--check-level=exhaustive`, finds the same 73 things in
  `ImgDecode.cpp` as before, and catches a bug planted in the check, in the loop's report
  and in the report.
- **No new compiler warnings** (143 in `ImgDecode.cpp`, as before).
- **Two fresh reviews** (agents given only the diff; the second of what changed after the
  first) found no bug in the change. The first tried stuffed 0xFF in the cut code and its
  value bits, fill bytes before the RST, 12-bit data, a cut before the end of the file or
  an EOI, a code whose RST is read at the second fill, valid files (each decodes as
  before), and long runs; it found the scan's count shared (20 cuts hid the error for a
  file cut short), cases missing (the second fill; a stuffed 0xFF; Cb; each of three
  errors), and the two inherited items below. The second found cases missing (the scan's
  other errors first; RST index errors with the cuts) and a comment naming the wrong
  coefficient. All fixed above.

**Found, not fixed (all inherited):**
- **After a cut past an MCU's first code, a colour image is read out of place:** the next
  interval starts with an MCU's first block, but is read as the cut block's next
  coefficients, or as the MCU's next component, and so on to the end (each restart after
  it takes the same rule). By the review: with Cb's DC cut in a 3-component file, the last
  pixel is 193,0,106, where djpeg gives 51,181,255. Before, the rest was grey. JPEGsnoop
  does the same after any restart inside an MCU: restarting at the next MCU (or where Ri
  puts it, as libjpeg does) would change that rule for all of them. (Fixed since: see the
  next section.)
- **Bits before an RST that make no code are dropped with no message:** a code the RST
  cuts within its Huffman bits, and bits that no code of the table starts. With a table of
  few codes, each interval whose data starts with none is dropped, one after another: after
  a block's DC, with the test generator's AC table (its EOB alone), every interval to the
  last (`rc_fit.jpg`). Before the section before's loop, each made a code of 0 instead.
  (Fixed since for files with a DRI: see the next section.)
- **A marker other than an RST in the scan data ends it for Lookup MCU Offset:** decoding
  goes on past it, but Lookup gives "Not decoded" for the MCUs after it (by the review: an
  FF C8 after an RST).
- The section before's other three still stand: a short or empty interval isn't reported,
  the status bar's luminance DC after a restart inside an MCU, and a negative "Max #
  errors".

### Restart intervals where the DRI puts them

The first of the section before's items, done as libjpeg does it.

A restart interval (T.81 B.2.4.4) holds the DRI's number of MCUs, and an RST ends each but
the last. **JPEGsnoop decoded on from wherever it found an RST,** going on with the block or
MCU it was in. So when an interval's data was damaged:
- **Data lost from an interval** (a short interval): the rest of the image was decoded one
  or more MCUs early, and its last MCUs had no data.
- **An RST inside an MCU:** the next interval's data, which starts with an MCU's first
  block, was read as the cut block's next coefficients, or as the MCU's next component, to
  the end of the image (in a colour file whose Cb DC was cut, the last pixel was 193,0,106,
  where djpeg gives 51,181,255).
- **Data left after an interval's last MCU** (a long interval, or an RST lost): it was
  decoded as the next MCUs, so the rest of the image was late, and an interval after a lost
  RST kept its DC predictor. "Restart marker not detected" was printed for each such
  interval, with no limit.
- **An empty interval, or one of a cut code,** was passed over (the two sections before),
  and bits before an RST that made no code were dropped with no message.

Now each interval's data is decoded at the MCUs the DRI sets, as libjpeg-turbo 3.2.1 does
(jdhuff.c `process_restart()`, jdmarker.c `read_restart_marker()` and
`jpeg_resync_to_restart()`):
- **An RST before the interval's last MCU** ends its data: the rest of the interval isn't
  decoded (it is left grey, DC 0 and no AC, as libjpeg leaves it), and the data after the
  RST is the next interval's. A block whose DC wasn't read is grey too, and a block cut in
  its AC keeps what was read. "\*\*\* ERROR: Restart interval ends early, at RST1 @
  0x000000a0.0 in MCU(1,0): the rest of it, to MCU(1,0), not decoded"; a code the RST cuts
  short is still reported where it starts.
- **Data after an interval's last MCU** (a byte or more before its RST, or no RST within
  the scan buffer's reach) is skipped to the next marker, searched from where the MCU's data
  ends, as libjpeg's `next_marker()` skips it: stuffed 0xFF and fill bytes are data, and
  markers below SOF0 (TEM, 0xFF01, ...) are passed over. "Expect Restart interval elapsed @
  0x000000a2.1" then "ERROR: Restart marker not detected: data skipped to RST1 @
  0x000000a5.0".
- **The RST found is taken by its number,** counted from RST0 for the scan's first
  interval, as `jpeg_resync_to_restart()` takes it: the one wanted is passed over; one of
  the next two is left for a later interval, the RSTs before it taken as lost with their
  intervals' data, so this interval isn't decoded; one of the two before is skipped, with
  the data after it, to the next marker, which is taken likewise; any other is taken for
  the one wanted. Each is reported: "\*\*\* ERROR: Expected RST1 before MCU(2,0), found
  RST2 @ 0x000000a5.0: taken as a later one, so MCU(2,0) not decoded", "..., an earlier
  one: skipped to RST2 @ ...", ": taken for it".
- **A marker that isn't an RST (from SOF0 on), or the end of the file,** where an RST
  should be: no MCU from there on is decoded, as libjpeg decodes none ("\*\*\* ERROR: No
  RST found before MCU(1,0), but marker 0xFFd9 @ 0x000000af.0: no MCU from there on
  decoded"). So a file with a DRI but no RSTs now shows its first interval only, as djpeg
  does (it was decoded whole).
- **Each message is reported up to the scan's error limit,** on a count of its own (one
  for intervals that end early, one for the others), as the RST index error's and the cut
  code's are, so as not to hide the scan's other errors; the counts start again at each
  decode.
- **Detailed Decode** shows where an interval's data ends ("RST1 @ 0x000000a0.0: the data
  of its restart interval ends here") and each block not decoded ("Not decoded: the data
  of its restart interval ended before it"). **Tools > Lookup MCU Offset** gives "Not
  decoded" for an MCU not decoded (it gave the position of the data that followed, or 0).
- **The image window's status bar** over an MCU whose interval ended inside it shows its
  decoded blocks' DCs (a restart inside the MCU cleared them: the "Two RSTs in a row"
  section's item).
- **"Expected RST marker index"** (each RST checked against the one read before it) is
  reported only without a DRI. With one, an RST out of order is reported where the
  restart rule takes it, by the interval count; reported where it was read too, each RST
  skipped was reported twice, and the right RST after a wrong one was called out of order.
- **A marker below SOF0 the scan buffer reads** no longer ends the scan's data for Lookup
  MCU Offset when there is a DRI, as the restart rule passes over it.
- **Data that runs out at the end of the file in a row** ends the decode there, as a
  missing RST does: the rest of the row was skipped without counting its MCUs, so the
  restart interval's count was off after it, and the first MCU of each row after it was
  decoded from zeros.
- **"Next position in scan buffer"** after a scan that ends right after an RST is that
  RST's next byte; it was 0x00000000.0 (the buffer's position before any byte was read).
- **Without a DRI,** an RST is passed over where it is found, as before.

This replaces the two sections before's rule for an empty interval and for one holding a cut
code (each was passed over, so the file decoded as without it): their MCUs are now not
decoded, and the data after them is one interval later, as djpeg shows it.

What still differs from djpeg 3.2.1:
- **The MCU where an interval's data ends:** djpeg decodes the rest of it, the cut code
  included, from zero bits (with "Corrupt JPEG data: premature end of data segment"), so
  its blocks are whatever those bits make (0 and 64 in the tests' files); JPEGsnoop leaves
  them grey. So does the first MCU of an interval taken as lost at its start.
- **A marker that isn't an RST inside an interval's data** is still decoded through
  (inherited, below): djpeg stops there, and leaves the rest grey.

**Tests:** 83 new `RI` cases, and the `ER` and `RC` sections reworked (45 of their cases
changed, 35 added, 1 left out): 173 in these three sections, 2,024 in all. On the code
before, 136 of the 173 fail; the 37 that pass check what didn't change (the cut code's
error and its limits, the RST index error without a DRI, valid files, files without a DRI
or with one of 0, three reference files, and Detailed Decode adding only its own lines).
Their values are worked out from the generator's bytes:
- an empty interval (one, two, with fill bytes before its RST, two at the scan's start,
  bits that make no code, in an interval of 2 MCUs, and 2^19 in a row); an RST inside an
  MCU (the status bar too); the MCU's offset; and 2^19 earlier RSTs skipped in time;
- a cut code (at the scan's start, three in a row, with empty intervals around it, Cb's
  DC, Pillow's AC codes, the code-length histogram); for Pillow's 4:2:0 `d_restart.jpg`
  with an empty interval or an AC code cut, the image's MD5 against a reference file made
  from its bytes with that MCU coded as decoded (grey blocks; the kept coefficients) and the
  intervals moved;
- data after an interval's last MCU (2 bytes, 2 MB, stuffed 0xFF, fill bytes and a TEM in
  it, and 1 MB runs of 0xFF ending in 0x00, 0x01, the RST, 0xC4 and the EOI, in time); an
  RST left out; numbered one or two after, or before, or three to five after, and RST
  numbers counted round past RST7; no RSTs, with an EOI or without; a non-RST marker or a
  TEM where the RST should be (and the MCU offsets after a TEM); an interval of 4 MCUs cut
  after its second; a colour interval cut after its Y; an interval larger than the image;
  an RST after the last MCU; an EOI whose 0xFF the last MCU took bits of; a scan ending at
  the end of a file of 4 GiB - 1 bytes; data running out in a row; the next position after
  a scan ending in an empty interval; and files without a DRI or with one of 0 (as before);
- each message's 20th and not its 21st; the error for a file cut short after 24 of each;
  theirs after the scan's own count is used up; the counts started again for a second file.

Each of 30 switches, put in a scratch copy, fails its own cases when on and passes when off
(each decoder's early end, its grey blocks and its grey DC; the interval's start; the
skip, searched from the buffer's position or the byte after the next bit's, with stuffed
0xFF, TEM or runs of 0xFF taken for markers; the number rule, and each of its three
outcomes; the end at a non-RST marker, and its not being remembered; the position before a
byte is read; each count on the scan's, and not started again; the RST the buffer stopped
at read again; Detailed Decode's line; and each fix after the review), but one: a non-RST
marker the buffer had read past, read again, is only reported twice, which no single-line
check sees (checked by hand). In each passing output, deleting the matched line or adding 1
to a number in what it checks fails the case, but in what it leaves unchecked (a pixel's
tolerance of 1; Detailed Decode's data bytes).

**Checks:**
- **The same image as djpeg 3.2.1** (`-dct float -nosmooth`, each block's centre within 4):
  the 696 made files of the section before and their 8 bases (10,512 blocks), every file
  with a DRI among the tests' (1,540 blocks) and every one whose report changed below
  (15,801 more). The only blocks that differ are in the MCUs djpeg decodes from zero bits
  (where an interval's data ends, or the first of one taken as lost at its start), in the
  MCU where a file cut short ends (as before), in an MCU with a bad Huffman code, and inside
  an interval holding a TEM, which JPEGsnoop decodes through.
- **Only these reports change,** old against new (before the review's fixes, which act only
  with a DRI), with and without the AC shown: of the tests' 518 files under 64 MB (before
  the review's cases), the 54 with broken restarts, and `d_restart_bad.jpg`, an interval
  of which starts with two stuffed 0xFF (now as djpeg's, but for the MCU with the bad
  code); of the 6,821 made in the rounds
  before, the 508 cut short where an RST should be (now as djpeg: no MCU after the cut), 2
  with a DRI but no RSTs (first interval only, as djpeg), and the 261 with RSTs in a row;
  and the 696 cut-code files. (One of 108 megapixels timed out under the load, and is the
  same when given the time.)
- **Speed:** the same as before, within its spread: 0.24 to 0.32 s of CPU for 1 MB of RSTs,
  2 MB of data skipped and 1 MB runs of 0xFF, and 1.24 s for a 3 MB file with an RST after
  each of its 23,500 MCUs. The review's 16 MB runs, 8 million earlier RSTs, and an 8192 x
  8192 file with a DRI but no RSTs (4.2 s, from 8.8 s) were in time too.
- **The affected groups pass:** the 203 cases of every file with a DRI (`ER`, `RC`, `RI`,
  and 30 of `D`, `LM`, `ML` and `MR`, unchanged).
- **With ASan/UBSan,** all but the last added pass (202), with no sanitizer reports.
- **cppcheck 2.22,** at `--check-level=exhaustive`, finds the same 73 things in
  `ImgDecode.cpp` as before (a redundant condition it found in the new search was taken
  out), and catches a bug planted in each new function. It missed one planted in a lambda
  (a known blind spot), so the lambda was made a plain string.
- **No new compiler warnings** (the same 143 in `ImgDecode.cpp`).
- **Two fresh reviews** (agents given only the diff; the second of the fixes after the
  first). The first crafted 30 files, and 300 damaged at random, and found no MCU out of
  place against djpeg, but four bugs, each fixed above with a case: the MCU map ended at a
  TEM the restart rule passes over ("Not decoded" for every MCU after it); the search for
  data after an interval started a byte late (an EOI whose 0xFF the last MCU took bits of
  was missed, and at the end of a file of 4 GiB - 1 bytes the position wrapped round to 0:
  20 s to search the file from its start); data running out in a row left the restart
  count off; and each RST out of order was reported twice. The second found no regression,
  and that the test driver took Detailed Decode's two new lines for a change in the report
  (fixed, with a case); its other findings are below.

**Found, not fixed (all inherited):**
- **A marker from SOF0 on inside an interval's data** is decoded through, and Lookup MCU
  Offset gives "Not decoded" for the MCUs after it; djpeg stops there. So is one whose
  0xFF the interval's last MCU takes whole as data (by the second review): even an EOI
  there isn't found then, and the decode goes on past it.
- **Without a DRI, after the scan data runs out in a row,** the first MCU of each row after
  it is decoded from zeros (by the second review; with a DRI, they are now grey).
- **An MCU whose data lies past the end of the file** is decoded from zeros with no error
  while it takes less than a byte of it (the end is noticed once a whole byte is used):
  in a scan that ends with an RST, the two MCUs after it (by the review). (Fixed since,
  with its real cause: see the next section.)
- **The file parser logs each TEM after the scan:** 16 MB of FF 01 after an interval took
  over 120 s (262,000 lines from 65,536 TEMs, in 25 s), before and after (by the review).
  (Fixed since: see the next section.)
- **Without a DRI, a restart inside an MCU** still zeroes the luminance DC the status bar
  shows for it (the section before's item, now only there).
- **A negative "Max # errors" in Options is no limit** (the sections before). (Fixed since:
  see the next section.)

### Zeros past the end of the file, runs of markers with no length, and the error limit

Three of the section before's items, all inherited from Windows (by reading its code).

**A byte past the end of the file, read into an empty scan buffer.** The scan buffer marks
each byte past the end of the file, and the 0xFF of a marker in the scan data, with an
error, which is taken when the bits read move into that byte. A byte added to an empty
buffer, after a restart or at the start of the scan, already holds the next bit, so its
error was never taken, and its 8 bits were decoded as data:
- **An RST that ends the file:** the MCUs after it were decoded from the zeros past the
  end, with no error, as many as 8 zero bits make (2 in the tests' files, where an MCU of
  zeros takes 3 bits). With a DRI, the search for the next RST then reported it missing
  ("No RST found before MCU(0,1), but the end of the file"). Without one, the error came a
  byte late, in a later MCU. (The section before put this down to the end being noticed
  only once a whole byte is used. This is the cause.)
- **A scan with no data** (its header ends the file): the same, from MCU [0,0]. Its
  compression ratio was also "inf:1", a division by zero that UBSan reported once these
  cases ran with ASan/UBSan (inherited, before and after the fix). It is now "Compression
  Ratio: - (no scan data)".
- **A marker as the first byte of a restart interval's data,** or of the scan's, wasn't
  reported as a bad marker, as one is anywhere else in the data. So `ri_eoi_mid.jpg`, an
  EOI right after RST5, now also gives "\*\*\* ERROR: Bad marker @ 0x000000c3.0".

Now the byte's error is taken as it is added: "\*\*\* ERROR: Scan data ends at the end of
the file in MCU(2,0) @ 0x000000a2.0 (file truncated?)", in the MCU that has no data, at
the end of the file, and no MCU after it is decoded. Without a DRI:
- **The loop that passes over an RST** (in the scan's decode and in Detailed Decode) read
  the code again after the RST, but kept the error taken before it, so the end of the
  file was reported a code late (at .2). It now takes the error of the byte the code
  starts in after the RST.
- **A bad marker in the bits before an RST that make no code** is reported where it is,
  before the restart. It was reported after it, at the position of the code read there
  (by the review: a marker at 0x9a was reported at 0x9f), or, once its error was taken in
  an empty buffer, not at all when the end of the file came next.

**Runs of markers with no length.** TEM (0xFF01), SOI and, with Relaxed Parsing, an RSTn
the parsing passes over have no length, so each takes 2 bytes. Each was listed with its
header, offset and warning (4 lines), and the report takes about 40 µs a line: 65,536 TEMs
took 11 s before the scan and 18 s after it, and 16 MB of them over 120 s. Now the first
20 in a row are listed as before, and the rest of the run in one line, with how many of
each kind and how many fill bytes: "Only the first 20 markers with no length in a row are
listed: 524268 more skipped (524268 TEM), to 0x00100098".
- **Any other marker ends the run.** An RSTn followed by anything but a marker or the end
  of the file isn't in it (it may have a length, which it is skipped with, as before), nor
  is one without Relaxed Parsing (it stops the decode, as before).
- **An SOI in the run is the file's SOI,** as one listed is. (By the review: with the SOI
  in the run, Export JPEG refused the file, saying it had no SOI.)
- **Speed:** 65,536 TEMs now take 0.17 s, and 16 MB of them 0.23 s.

The first version listed only the run's first marker. The old/new comparison below showed
that an SOI followed by a single TEM then lost the TEM's entry, hence the first 20.

Markers with a length still each get their entry. An empty COM or APPn takes 4 bytes for 5
lines, so 32,768 empty COMs (128 KB) take 6 s, before and after. By the review, a COM every
19 TEMs (so no run reaches 20) took 4.7 s for 64 KB, in both. That is the report growing
with the file, as with the empty DHT tables in "Six runaway reports".

**A negative or non-number "Max # errors"** (`Report\ErrMaxDecodeScan`) from the settings
file. Options takes 0 to 99999, and Windows stored it unsigned, but here -1 was no limit
(4,294,967,295 to the decoder), and text was 0, so no scan error was reported. Each now
keeps the default, 20; a number from 0 on is taken as before. (The sections before said
Options took -1. Its field doesn't; only the settings file can hold it. The review also
tried empty, `4294967295`, `2147483648`, `" 7"`, `7.5`, `+3`, `0x10` and `99999999`.)

**Tests:** 45 new cases (12 `EZ`, 28 `NL`, 5 `EM`), 2 new `RI` cases, and `RI_huge_end`
changed: 2,071 in all. `RI_huge_end`'s file ends in RST5 at the end of a file of 4 GiB - 1
bytes. Its next MCU now has the end-of-file error at 0xffffffff.0, and no search follows
it, so `RI_huge_end_search` cuts the file before RST5 to keep the search at the end of the
file in the tests. On the code before, 30 of the 48 fail. The 18 that pass check what
didn't change (a whole file; the image after a run; 20 TEMs, all listed; runs ended by
other markers; RSTs without Relaxed Parsing, or with a length; fill bytes before an EOI;
Early EOF; the search at the end of a 4 GiB file; the 20th index error, and limits of 5
and 0), or a bug made and fixed during this change (an SOI in a run; Detailed Decode's
report the same as without it). Their values are worked out from the generator's bytes:
- the end of the file after an RST, with and without a DRI, in the decoder and in Detailed
  Decode; a scan with no data, with and without a DRI, and its compression ratio; a bad
  marker at an interval's start, and before an RST without a DRI, in both decoders;
- 1 MB of TEMs after the scan and before it, in time, in few lines, with the 20th listed and
  not the 21st, and the EOI and the image after them; 20 and 21; TEMs and an SOI with fill
  bytes; 22 SOIs at the start; 22 RSTs; runs ended by a COM, by the scan and by an RSTn
  with a length; RSTs with and without Relaxed Parsing; to the end of the file, also of a
  file of 4 GiB - 1 bytes; 2 MB of fill bytes in the run and before the EOI that ends it, in
  time; and an SOI in the run, for Export JPEG;
- the limit at -1 and `abc` (20), and at 5 and 0, as before.

Each of 20 switches, put in a scratch copy, fails its own cases when on and passes when off:
the error taken in an empty buffer; each decoder's error after the RST, and its bad marker
reported before it; the run, its 20 (`>` for `>=`), its end at another marker, the SOI kept,
each kind taken into a run or counted in it, the RSTn's Relaxed Parsing and next marker, the
fill bytes skipped and counted; and the limit's two checks. A reset of the count at the top
of `DecodeMarker()` was taken out, as no switch could fail a case: every marker that
returns from its case ends the parsing. (A DQT with a table ID over 3 looked like one that
goes on, but `ValidateValue()` makes the ID 0 first.)

**Checks:**
- **Only these reports change.** Old against new, on the tests' 522 files under 64 MB, and
  with Detailed Decode over the whole image on the 391 under 1 MB: the new files and
  `ri_eoi_mid.jpg` (its bad marker). With Relaxed Parsing, on the 43 `ML` and `MR` files,
  before the review's fixes: none.
- **The affected groups pass:** 415 cases: the `EZ`, `NL`, `EM`, `ER`, `RC`, `RI`, `D`,
  `LM`, `ML`, `MR`, `S` and `SM` cases; and after the ratio's fix, the `EZ` and `CS` ones.
- **With ASan/UBSan,** the `EZ`, `NL`, `EM`, `ER`, `RC`, `RI`, `ML`, `MR` and `D` cases pass
  (344), with no sanitizer reports.
- **cppcheck 2.22,** at `--check-level=exhaustive`, finds the same 248 things in the three
  files as before, and catches a bug planted in each function changed.
- **No new compiler warnings** in the three files.
- **A fresh review** (an agent given the diff, then the reworked one) checked each case's
  values from the bytes, cut 6 files at 194 places, put markers at the start of intervals
  and scans, and tried runs and limit values. It found the SOI in the run, `RI_huge_end`'s
  expectation, the bad marker lost before an RST at the end of the file, and cases missing
  (Detailed Decode's half of the loop; the count after an RSTn with a length;
  `ri_eoi_mid.jpg`'s new line), all fixed above, and the first item below.

**Found, not fixed (inherited):**
- **A 0xFF as the last byte of the file, in the scan data,** is read as a stuffed 0xFF00, as
  the byte past the end reads as 0: four "Can't find huffman bitstring" errors, then "Scan
  data ends ... @ 0x0000009d.4", past the end of the file (by the review). (Fixed since: see
  the next section.)
- **Markers with a length are each listed,** however many in a row (above). (Fixed since for
  runs of one kind: see the next section; and for the rest, see "The lines of a marker
  list".)
- The section before's other three: a marker from SOF0 on inside an interval's data is
  decoded through; without a DRI, the first MCU of each row after the data runs out is
  decoded from zeros; and without a DRI, a restart inside an MCU zeroes the luminance DC the
  status bar shows.

### A 0xFF at the end of the scan data, and runs of one marker

The section before's first two items, both inherited from Windows (by reading its code).

**A 0xFF as the file's last byte, in the scan data.** The scan buffer read a 0xFF with the
byte after it, and past the end of the file that byte reads as 0, so the 0xFF was a stuffed
0xFF00: 8 bits of 1 decoded as data. djpeg (libjpeg-turbo 3.2.1) reads it as a marker that
the end of the file cuts off: at the end of the file its source supplies an EOI (FF D9) and
warns "Premature end of JPEG file", and its decoder takes a 0xFF before another 0xFF for a
fill byte, so the file's last 0xFF goes with that EOI, and the image is the one without it.
On a made-up 64x32 JPEG (`cjpeg -quality 95`, no DRI) cut just after each of its 7 stuffed
0xFF bytes (so each lost its 0x00):
- **djpeg:** all 7 images were the same as without the 0xFF ("Corrupt JPEG data: premature
  end of data segment" in both).
- **JPEGsnoop:** in all 7, "Scan data ends at the end of the file" was 1 or 2 bytes past the
  end of the file (0x00000728.4 in a file of 0x727 bytes), and the code-length statistics
  counted the codes the 8 bits made; in 2, the image changed; in the review's file, four
  "Can't find huffman bitstring" errors came first.

Now a 0xFF that the file ends in, or a run of them to its end, isn't data, and each of its
bytes is marked as one past the end of the file, so the scan's data ends before it. The 7
files decode as without the 0xFF: the same image, and the same report but for the scan
pass's line (below). As it is now:
- **The decode:** the error is at the 0xFF, in the MCU that has no data, as for the file cut
  there: "Scan data ends at the end of the file in MCU(3,1) @ 0x000000d3.0" for
  `lm_444.jpg` cut before MCU [3,1] with a 0xFF after it (it was 0x000000d6.4, and with 5
  of them, "Can't find huffman bitstring" first).
- **Tools > Lookup MCU Offset:** an MCU that starts at the 0xFF has no data, so "Not decoded"
  (it gave the 0xFF's offset).
- **Detailed Decode** shows the 0xFF as the 0 decoded in the bytes after a code
  ("Data=[0x 50 00 00 00", where it showed "50 ff 00 00"), as last round made it show the
  bytes decoded. So does a whole scan whose EOI is cut to its 0xFF: in Detailed Decode only
  the bytes after its last code changed (`lf_whole.jpg`, `ml_fill_eof.jpg`).
- **With a DRI,** a 0xFF as an interval's first byte is the end of the file likewise. And
  the search for the RST where an interval ends stops at a run of 0xFF to the end (by the
  review): with `ez_rst_end.jpg`'s RST0 cut to its 0xFF, "data skipped to the end of the
  file @ 0x000000a0.0" and a compression ratio of 102.40:1, as for the file cut before the
  0xFF (it gave 0x000000a1.0, past the 0xFF, and counted it as data: 85.33:1).
- **The marker list's pass over the scan data** (phase 1) ends the data at a lone 0xFF at the
  end, as it did at 2 or more (by the review). It read it with the 0 past the end, so Options
  > Scan Dump showed it as data, and it gave "Ran out of buffer before EOI during phase 1"
  one past that. Now the marker after the data is cut off by the end of the file, and the
  report gives "Early EOF", as with 2 or more.

djpeg decodes each `LF` file with 0xFF bytes at the end (one, 5, 2 MB, and one after an RST)
to the image of the file cut before them.

**Runs of one marker with a length.** Each marker with a length was listed, however many in
a row: an empty COM or DQT (4 bytes) makes 5 lines, an empty APP1 7. Each line takes about
35 µs and 3 KB of memory, in the report's Qt text widget, which the window, File > Batch
Process and the command line all use (`--count-lines` and `/usr/bin/time -l` on the harness,
and a scratch program on the app's own log class):

| Empty COMs (4 bytes each) | Report | Time | Peak memory |
|---|---|---|---|
| none | 348 lines | 0.26 s | 47 MB |
| 16,384 (64 KB) | 82,268 lines, 1.6 MB | 3.0 s | 302 MB |
| 65,536 (256 KB) | 328,028 lines, 6.2 MB | 11.3 s | 967 MB |

It grew with the file: 16 MB of them would have taken about 12 minutes and 60 GB (worked out
from these, not run). The parsing takes no input meanwhile (the only Cancel is for hashing a
large file), and djpeg reads the 65,536 in under 0.01 s. Undo history isn't the cost: the
window's report keeps none, and with it off the scratch program took the same (975 MB, not
1,003). With no lines written at all (a scratch build), 65,536 empty markers of each of 7
kinds took 0.18 to 0.36 s in all.

Now, after the first 20 of one kind in a row, each as before, the rest of the run is parsed
as usual, but its lines are held back and dropped, and the run gets one line where it ends:
"Only the first 20 COM markers in a row are listed: 65516 more not listed (parsed as usual),
to 0x00040002". It adds their fill bytes ("; 10 fill bytes"), and the error and warning lines
that weren't shown ("; 65516 error lines not shown"). It counts lines, not errors: a COM of
length 0 gives 3 error lines. (By the review: it said "errors", and left out fill bytes.)
- **Only the listing changes:** each marker is still parsed, so its tables and settings take
  effect (the 25th DQT's table is the one the scan uses). The reports of 65,536 empty COMs,
  APP1s, DQTs and JPG0s are each the old report with the run's lines taken out and the one
  line put in, and the image is the same. 65,536 of each of 7 kinds take 0.2 to 0.55 s (11
  to 23 s before).
- **A marker the parsing ends with** (an error, the end of the file, or a position that
  wrapped past 2^32) is shown in full, after the run's line. So is one after which no marker
  starts where its length ends, as when it takes the next marker's 0xFF: the last marker not
  listed is kept until the next one is read. (By the review: it was dropped, and the report
  gave "Expected marker 0xFF, got 0xda" after the run's line, with no sign of why.)
- **The run's markers are those of one code,** after any fill bytes, each with a length. Any
  other marker ends the run, as do the scan and the end of the file. SOS isn't in runs
  (each scan is listed), nor are markers with no length (SOI, EOI, TEM and RSTn, which have
  the rule of the section before), nor an RSTn taken to have a length.
- **The lines are held apart from `CDocLog::Enable()`/`Disable()`:** the EXIF parser calls
  `Enable()` at the end of each IFD, so disabling the log for the run's markers would have
  shown the lines after it. While the log is disabled, nothing is held.

**Not covered** (by the review, with the new build). Only a run of one kind is cut short, so
these are still listed in full, and their report still grows with the file:

| Input | Size | Time | Memory |
|---|---|---|---|
| COM and TEM in turn | 1 MB | 60 s | 5.1 GB |
| APP0 and APP1 in turn | 256 KB | 35 s | 1.7 GB |
| COM and DQT in turn | 256 KB | 14 s | 1 GB |
| 16,384 scan headers after the scan | 160 KB | 8.4 s | 663 MB |
| 16,384 RSTn taken to have a length (Relaxed Parsing) | 64 KB | 4.75 s | |
| 20 APP1s, each XMP of 32K short lines | 1.3 MB | 21 s | 1.7 GB |

Alternating kinds at 16 MB would take about 16 minutes and 80 GB, the cost this change
removed for runs of one kind. A single marker with many lines is the same class (an empty DHT
table's 21 lines, the earlier "Six runaway reports" item). The lines held back are still
built: 16 MB of DHTs take 10.9 s with Options > DHT Expand on, 3.2 s with it off. Closing
the rest needs a different rule: count each kind across the file, not in a row, or give the
marker list a budget of lines per file. That is left to decide. (Fixed since, with a budget:
see the next section.)

**Tests:** 47 new cases (14 `LF`, 33 `LR`): 2,118 in all. On the code before, 36 of the 47
fail. The 11 that pass check what didn't change: the file cut without a 0xFF, and its image;
a whole scan with its EOI cut to its 0xFF; 20 COMs, all listed; runs ended by a TEM, by
another kind and by the scan; the 20th APP1's error; the 25th DQT's table used; the error
after a COM whose length takes the scan's 0xFF; and 22 scan headers in a row, all listed.
Their values are worked out from the generator's bytes:
- a 0xFF, 5 and 2 MB of them (with Detailed Decode) after `lm_444.jpg` cut before MCU [3,1],
  the image there (the MCU decoded from the zero bits of the end has the levels of the MCU
  before), and Detailed Decode's bytes after one and after 5; Lookup MCU Offset; the scan
  pass's "Early EOF"; the 0xFF at 0xfffffffe, at the end of a file of 4 GiB - 1 bytes; and
  with a DRI, after an RST that ends an interval, and an RST cut to its 0xFF;
- 65,536 empty COMs before the scan, in time, in few lines, with the 20th listed and not the
  21st, the image after them, and in the command line's report; 20 and 21; a fill byte
  before each, and before only the 21st; two kinds in turn; the error lines of empty APP1s
  (and of one), and of COMs of length 0, the warning lines of JPG0s (and of one) and of
  reserved markers (0xFF02, with Relaxed Parsing); the 25th DQT; a DRI too short for its
  interval, a COM cut by the end of the file, and one whose length takes the scan's 0xFF,
  after 24 of them, each shown in full; a run to the end of a file of 4 GiB - 1 bytes, and
  one whose last COM wraps past 2^32 there.

Each of 23 switches, put in a scratch copy, fails its own cases when on, and with them off
all 47 pass:
- the 0xFF taken as the end of the file, a run of them, the data's end for Lookup MCU
  Offset, Detailed Decode's bytes, the scan pass's 0xFF, and the RST search's end;
- the lines held, the 20 (`>=` for `>`, and 21), the run's code, SOS kept out, each of the
  three ways a marker ends the parsing (and all), the marker kept until the next shows
  whether a marker follows it;
- the error and warning lines and fill bytes counted, the run's end position, the fill bytes
  before a code, the markers parsed (passed over instead, the 25th DQT isn't used), and the
  count reset.

**Checks:**
- **Only these reports change.** Old against new, on the tests' 578 files under 64 MB: the
  20 new `LF` and `LR` files with a 0xFF at the end or a run, and no other. With Detailed
  Decode over the whole image, on the 13 that end in a 0xFF: the `LF` files, and
  `ml_fill_eof.jpg`, in the bytes shown after the last code (above).
- **The tests pass:** all 2,105 cases before the review's fixes; after them, the 446 cases
  of the groups they reach (`LF`, `LR`, `ML`, `MP`, `RA`, `NL`, `EZ`, `RI`, `ER`, `RC`, `LM`,
  `D`), and the same 446 with ASan/UBSan, with no sanitizer reports.
- **cppcheck 2.22,** at `--check-level=exhaustive`, finds the same 245 things in the three
  files as before, and catches a bug planted in each of the 16 functions changed (13 before
  the review's fixes; `DecodeMarker()`, `RestartSkipToMarker()`, `LengthRunDrop()` and the
  reworked `LengthRunBefore()` after them).
- **No new compiler warnings** in the three files.
- **A fresh review** (an agent given the diff) checked each case's values from the bytes,
  timed crafted runs and cut files, and found the marker hidden after a wrong length, the RST
  search and the scan pass, the summary's counts, and the missing cases above, all fixed,
  and the cases not covered. A second review, of the reworked code, found no bug. It traced
  every way `ProcessFile()` ends, runs cut anywhere and near 4 GiB, and the log disabled. It
  timed 16 MB of empty COMs at 2.5 s and 49 MB, and of empty APP1s at 7 s. It found the
  first item below, and measured 65,536 RSTn taken to have a length at 16 s and 1.28 GB,
  before and after.

**Found, not fixed:**
- **A marker not listed whose length ends on a 0xFF that stops the parsing** (FF 00, or a
  reserved marker without Relaxed Parsing) is dropped, so the report shows the run's line
  and then the stop, without that marker (by the second review, from the code). Only the
  listing is affected.
- **Markers of different kinds in turn,** runs of scans, RSTn taken to have a length, and
  markers with many lines are still listed in full (above). (Fixed since: see the next
  section.)

### The lines of a marker list

The section before's other open cost, inherited from Windows: only a run of one kind was cut
short, so markers of different kinds in turn, runs of scan headers, RSTn taken to have a
length and markers with many lines were still listed in full. Each line costs about 35 µs
and 3 KB in the report.

**Now each marker list shows at most 20,000 lines or 2,000,000 characters**
(`MARKER_LIST_LINES`, `MARKER_LIST_CHARS`): the main image's (`ProcessFile()`) and the EXIF
thumbnail's (`DecodeEmbeddedThumb()`), each on its own. Past the limit the markers are
parsed as usual, but `CDocLog` doesn't show their lines: it counts them, with the error and
warning lines among them, and keeps the last marker's, at most 50. Where the list ends
(`MarkerListEnd()`), one warning line gives the counts, then the kept lines follow, so the
report still shows how the list ended:

> The marker list shows at most 20000 lines or 2000000 characters: 1028666 more lines not
> shown (64291 error lines and 64292 warning lines among them); the markers were parsed as
> usual, and the last 3 lines follow

- **Which lines are kept:** those of the last marker with lines not shown.
  - Where "Expected marker 0xFF" ends the list, the pass with no 0xFF goes with the marker
    before it, whose length led there. (By the review: the error was kept alone.)
  - A pass that shows nothing, such as a marker cut to its 0xFF at the end of the file,
    leaves the marker before it kept.
- **The scan's decode isn't limited, nor counted:** Detailed Decode of a 256x128 image gives
  22,500 lines. Whatever wasn't shown before the decode is reported just before it, with the
  scan header, and the limit goes on from the next marker. (By the review: the first version
  counted the decode, so markers before the scan hid all of it.)
- **Characters as well as lines** (by the review): 2,000 COMs of 64 KB each, in runs of 20,
  are one line each, so they stayed under 20,000 lines yet took 92 s and 1.2 GB.
- **"ERROR: Early EOF"** is now added after the list's last lines, so it is never cut. When
  nothing is cut it is where it was.
- **The run rule of the section before stays:** a run of one kind still gets one line, so
  those reports stay short well under the limit.
- **No other test file reaches the limit,** with the default options or with Relaxed
  Parsing and DHT Expand on. The longest report among them is 8,210 lines. When nothing is
  cut, the limit adds no line and moves none.

| Input | Size | Before | After |
|---|---|---|---|
| COM and TEM in turn | 1 MB | 60 s, 5.1 GB | 0.97 s, 110 MB |
| APP0 and APP1 in turn | 256 KB | 35 s, 1.7 GB | 1.1 s, 122 MB |
| COM and DQT in turn | 256 KB | 14 s, 1 GB | 0.79 s, 104 MB |
| 16,384 scan headers after the scan | 160 KB | 8.4 s, 663 MB | 0.89 s, 116 MB |
| 16,384 RSTn taken to have a length (Relaxed Parsing) | 64 KB | 4.75 s | 0.82 s, 107 MB |
| 20 APP1s, each XMP of 32K short lines | 1.3 MB | 21 s, 1.7 GB | 0.70 s, 99 MB |
| One DHT of 3,854 empty tables | 64 KB | 3.7 s, 412 MB | 1.0 s, 137 MB |
| A thumbnail whose offset leads to 1 MB of SOIs | 1 MB | 19 s, 1.7 GB | 0.67 s, 83 MB |
| 2,000 COMs of 64 KB, in runs of 20 | 131 MB | 92 s, 1.2 GB | 2.1 s, 70 MB |

The first six "Before" figures are the section before's. The others were measured with the
limit switched off. The "After" figures come from files of the same shape, made again.

**Tests:** 23 new cases (`LB`), 2,141 in all. With the limit switched off, as on the code
before, 16 of them fail. The 7 that pass check what didn't change: 20,000 lines with the
EOI's 3 after them, all shown; the Early EOF; the decode and the EOI after the markers
before the scan; a Detailed Decode of 22,500 lines, none cut; and the COM before a final
0xFF. Each count is worked out from the generator's bytes and the lines each marker gives,
from the code: an empty APP1 7 (1 an error line), an empty COM 5, a TEM 4 (1 a warning
line), an EOI 3, and an empty DHT table 21. nl's own markers give 81 lines before its scan
header, and its scan 9 before the decode (as listed before this change). The cases:
- APP1, COM and TEM in turn after the scan: the counts, the last offset shown and the first
  not, the EOI after, and the report's length (and in time);
- the edge: 20,000 lines then the EOI, all shown, and one line more;
- a last marker of 71 lines (a DHT), of which the last 50 are shown, its error, and the
  Early EOF after them;
- the thumbnail's list: 1 MB of SOIs, and 16,384 SOIs before "xy";
- a COM whose length leads to "xy", and one before a final 0xFF, each shown;
- 3,000 COM and TEM pairs before the scan: the line before the decode, the decode, and the
  EOI after;
- Detailed Decode of 512 MCUs, with no line cut;
- 40 COMs of 64 KB, cut by the characters.

Each of 18 switches, put in a scratch copy, fails its own cases when on, and with them off
all 23 pass:
- **The limit:** off; off for the thumbnail only; 19,999 and 20,001 lines; and the
  characters off.
- **The lines kept:** 49 and 51 of them; never restarted; restarted at once rather than at
  the next line cut; not counted when dropped; and a pass with no 0xFF kept apart, in each
  list.
- **The counts:** the error lines and the warning lines.
- **The rest:**
  - the Early EOF inside the limit;
  - the line given when nothing was cut;
  - no report before the decode;
  - no limit after it.

**Checks:**
- **The tests pass:**
  - all 2,129 cases with the first version, before the review's fixes;
  - after the fixes, the 23 `LB` cases, both plain and with ASan/UBSan, with no sanitizer
    reports;
  - after the fixes, every test file decoded with both sets of options, which showed that
    only the `LB` files reach the limit.
- **cppcheck 2.22,** at `--check-level=exhaustive`, finds the same 179 things in the two
  files as before. It adds two style notes for the new `qsizetype` member, which is
  assigned in the constructors as every member of the class is. It catches a bug planted in
  each of the 13 functions changed or added: 10 before the review's fixes, and after them
  the 2 added and the 3 changed most.
- **No new compiler warnings** in the two files.
- **A fresh review** (an agent given the diff) found the three problems above, all fixed. It
  also checked:
  - that every path through the two loops stops the limit, on all three callers, and with
    the log disabled;
  - the counts at the edges;
  - the run rule's held lines;
  - the moved Early EOF.

  There was no second review.

**Found, not fixed:**
- **Lines past the limit are still built, then dropped.** (Fixed since: see "Lines not
  shown aren't made", below.) The review timed these on the first version, whose cost of
  dropping them this round didn't change:
  - 16 MB of COMs and TEMs: 3.5 s and 109 MB;
  - 16 MB of 64 KB DHTs of empty tables, between TEMs: 8 s and 135 MB;
  - a thumbnail of 16 MB of SOIs: 1.1 s.
- **The limit is fixed.** No option shows the rest. A real file with a huge XMP (Photoshop's
  document-ancestors lists can reach tens of thousands of lines) is now cut, with the count.
- **The EXIF parser's `Enable()` at the end of each IFD** undoes Extract All's `Disable()`
  (inherited). By the review, from the code: after the first frame with EXIF, each later
  frame's report is added to the report. Not run, as it needs the GUI. The limit still
  starts and stops in pairs there.
- **The section before's hidden marker** (one not listed in a run, whose length ends on a
  0xFF that stops the parsing) is unchanged.

### Lines not shown aren't made

The section before's first open item: past the limit, each line's text was still made,
then dropped, as was the text of a run's lines held back (LR). Making it, mostly
`QString::arg()`, was most of the time left. Past the limit each MB of markers took about
0.06 to 0.7 s, by kind (a 14 MB photo takes 1.2 s in all), so the time grew with the file.

**Now a line's text is made only if it is shown.** `CDocLog::AddLineF()`, and its kinds
(`AddLineHdrF()`, `AddLineHdrDescF()`, `AddLineWarnF()`, `AddLineErrF()`), take a function
that makes the text. A line held back, or past the limit, keeps the function. It is called
only if the line is shown: one of the last marker's lines, or of a held marker shown in
full. Otherwise it is dropped, uncalled, and counted as before.
- **What a line shows is what the code read:** each function captures the values when the
  line is added, never `this` (the compiler refuses a member read without it). Arrays and
  byte strings are copied: a DHT line's codes, a DQT row, an APP0 thumbnail row.
- **Which lines:** each that `DecodeMarker()`, `DecodeDHT()` and `AddHeader()` add, 114
  changed by a script (the format and its arguments moved as they were), and about 10 by
  hand (lines made in parts: a DHT's codes, a DQT's rows, APP0's density and thumbnail,
  SOF's and SOS's components, APP14's transform, a COM's text). Left as they were:
  - lines that can't run: in `#if 0`, in the `#else` of an `#if 1`, and the dumps under
    `m_bVerbose`, which nothing sets;
  - EXIF's TIFF identifier, after which the EXIF parser's own lines follow;
  - the scan dump, an option, at most 20 lines a scan;
  - the lines of the helpers (`SegOverrun()`, `ValidateValue()` and others, errors), and of
    the EXIF, ICC and Photoshop parsers.
- **The report is the same:** every test file was decoded by the code before and after,
  with the default options, Relaxed Parsing, and DHT Expand: the 646 files' reports match,
  byte for byte. (10 entries are folders or links, which the decode mode doesn't take.)

The cost, in instructions retired (billions: `/usr/bin/time -l`), which the machine's load
doesn't change: it was swapping and busy for most of this round. A 14 MB photo's 13.4 take
1.2 s in all, so a billion is about 0.09 s. Peak memory is the same, within 6 MB.

| Input | Size | Before | After |
|---|---|---|---|
| 384 DHTs of 3,854 empty tables, a TEM after each (`LZ_time`) | 24 MB | 200 | 24 |
| The same DHTs in a row (`LZ_run`) | 24 MB | 200 | 22 |
| 256 such DHTs between TEMs (the review's, in the section before) | 16 MB | 138 | 20 |
| COM and TEM in turn | 16 MB | 55 | 19 |
| A thumbnail whose offset leads to SOIs (its list isn't changed) | 16 MB | 14.3 | 14.6 |
| 2,000 COMs of 64 KB, in runs of 20 | 131 MB | 32.2 | 32.0 |
| A photo (noise, quality 95; the limit isn't reached) | 14 MB | 13.4 | 13.4 |

In wall time, on the first version's 16 MB time files with the machine less busy: 7.6 s to
1.44 s with TEMs, 7.8 s to 1.38 s in a row.

Past the limit, each MB of one kind, in turn with an empty COM (the COMs alone, in a run),
from 1 MB and 5 MB files:

| Kind | Before | After |
|---|---|---|
| DHT (64 KB of empty tables) | 7.9 | 0.51 |
| APP1 (empty, which gives an error line; the rest of its cost wasn't looked into) | 5.1 | 2.75 |
| SOF0 (1 component) | 4.2 | 0.80 |
| DAC | 4.1 | 0.68 |
| DQT | 3.5 | 0.28 |
| COM | 2.6 | 0.56 |
| JPG0 | 2.5 | 0.50 |
| APP5 | 2.4 | 0.48 |
| DRI | 2.2 | 0.41 |
| APP0 (JFIF) | 1.9 | 0.33 |
| APP1 (XMP of 13,000 short lines) | 0.69 | 0.33 |

**Tests:** 13 new cases (`LZ`), 2,154 in all. Each expected line is worked out from the
generator's bytes.
- **Text made late** (10 cases): a marker past the limit, where the file ends, so its lines
  are the last shown and made then:
  - a DHT's line of 16 codes, then one of 4 after 41 spaces;
  - one of exactly 16, then an empty line, with no line of 41 spaces;
  - a DQT's first and last rows (zigzag order to row order, 3 wide);
  - an APP0's thumbnail row, and its density (72 x 72 DPI);
  - a SOF's first and last components (subsampled 2 x 1);
  - a COM's text.

  These pass on the code before too: they check that the text made late is the text the
  code made at once.
- **The time** (3 cases): 384 DHTs of 3,854 empty tables (24 MB) after the scan, in a 5 s
  limit, which the code before runs past (see the table):
  - with a TEM between them: the counts (31,061,818 more lines not shown, 384 warning
    lines among them), and the EOI's offset;
  - in a row: the run's line (364 more not listed).

  They pass `--timed`, a new flag of the test driver: built with the sanitizers, the driver
  skips the case. The sanitizers slow the lines kept about 35 times (62 s to 141 s, where
  the limit times 4 gives 16 s), so the time shows nothing there.

Each of 12 switches, put in a scratch copy, fails its own cases when on, and with them off
all 13 pass:
- **Made at once:** every line; those past the limit; those held back. Each fails the time
  cases it should: all 3; the two with TEMs; the run.
- **Shown without their text:** the lines kept (all 10 text cases); the held lines (4 `LR`
  cases, those of a held marker shown in full).
- **A DHT's codes:** lost when made late; a line of more codes without its spaces; the
  empty line given them.
- **The rest:** a DQT row's values reversed; a thumbnail row's last pixel lost; the
  density's units lost; a component's sampling factors swapped.

**Checks:**
- **The tests pass** with the new code, in a scratch build:
  - every case but the GUI's: 1,133 passed, and 5 were skipped, which need the app built
    beside the driver. (The GUI's 1,016 cases weren't run: their reports come from the same
    code, whose output is the same.)
  - with ASan/UBSan, through `tests/run-tests.sh --asan`: the `LZ`, `LB`, `LR` and `NL`
    cases, 94 passed and the 3 time cases skipped, with no sanitizer reports.
- **cppcheck 2.22,** at `--check-level=exhaustive`, finds nothing new in the two files. It
  had found two unused strings and a member with no initial value, both fixed. It catches a
  bug planted in `appendLine()`, `AddDhtCodesLine()`, `DhtCodesLine()`, and in a function
  made for a line.
- **No new compiler warnings** in the two files: the same 58 as before.
- **A fresh review** (an agent given the diff) found no bug in the code. It checked each
  converted line's function against the old text, the strings no longer set, and
  `appendLine()` against the old function. It also decoded its own made-up files with the
  code before and after: 80 segments of each converted kind, shown at once, as the last
  past the limit, held and shown, held and dropped, held past the limit, and before the
  scan. Their 1,796 reports match. Also clean with ASan/UBSan: 450 of those, the kept and
  held ones. It found two problems in the tests, both fixed:
  - the time cases couldn't pass with the sanitizers (the `--timed` flag above);
  - no case had a DHT length with exactly 16 codes (the case above).

  There was no second review.

**Found, not fixed:**
- **Lines still made past the limit:** those of the EXIF, ICC and Photoshop parsers, of the
  helpers, of the thumbnail's own list (`DecodeEmbeddedThumb()`; its markers give 2 lines
  each), and EXIF's TIFF identifier.
- **Showing the 20,000 lines** takes about 0.7 to 1 s of every report that reaches the
  limit.
- **A line's function is stored in the line,** and it allocates when it holds over 24 bytes:
  a DHT line with codes, a thumbnail row, a SOF component. They're made only where those
  are.

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
