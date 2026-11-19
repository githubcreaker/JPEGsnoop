# JPEGsnoop v1.8.0

<img align="right" src="http://www.impulseadventure.com/photo/images/jpegsnoop_prog.png">

JPEGsnoop is a detailed JPEG image decoder and analysis tool.
It reports all image metadata and can even help identify if an image has been edited.

Official repository for JPEGsnoop: [JPEGsnoop @ GitHub](https://github.com/ImpulseAdventure/JPEGsnoop/)

## Documentation & Downloads
- More details and support are available: [JPEGsnoop - ImpulseAdventure.com](http://www.impulseadventure.com/photo/jpeg-snoop.html)
- Windows binary can be downloaded at: [JPEGsnoop latest releases](https://github.com/ImpulseAdventure/JPEGsnoop/releases)

<img align="right" src="http://www.impulseadventure.com/photo/images/jpegsnoop-04.jpg">

## Features
- Decode JPEG, AVI (MJPG), PSD images
- MCU analysis with detailed decode
- Extract embedded JPEG images
- Detect edited images through compression signature analysis
- Report all image metadata (EXIF)
- Batch file processing
- Compression signature detection
- Recover corrupt JPEG image data
- No installation required

## Target System
- Windows (MFC)
- Compilation under Visual Studio (Visual C++)

## License
- JPEGsnoop is free for personal and commercial use per GNU General Public License version 2.0 (GPLv2)
## Build  
 - project build  
req: ms visual studio(c++) 2012  
simple stuff
 - nmake  
 req: ms visual studio(c++) 2005  
 http://imageshack.com/i/pmym0PEnj

## Building on macOS (Qt 6)
The Qt version of JPEGsnoop (this branch) builds on macOS with Qt 6.

- Requirements: Qt 6 and the Xcode command line tools, for example
  `brew install qtbase` and `xcode-select --install`
- Build and package: `scripts/package-macos.sh`
  - Output: `build-macos/JPEGsnoopQt-<version>-macos-<arch>.zip`, a self-contained app with Qt bundled
  - `--no-build` packages an existing build; `--build-dir DIR` uses another build folder;
    set `QMAKE` to choose a Qt installation
- The app is ad-hoc signed and not notarized, so on other Macs Gatekeeper blocks it until
  it is allowed in System Settings > Privacy & Security.
- Command line: run the app's program with the Windows version's options, for example
  `JPEGsnoopQt.app/Contents/MacOS/JPEGsnoopQt -i photo.jpg -o photo.txt` (`-help` lists them).
  It runs without its window or a Dock icon, writes errors to the standard error, and exits
  with 0 if everything worked, 1 if not. The license terms must have been accepted in the app
  first.
- The app icon `images/jpegsnoop.icns` is made from `images/jpegsnoop.ico` by
  `scripts/make-macos-icon.py`; run it again only if the `.ico` changes.
- With Homebrew's Qt, the app needs the macOS version Homebrew built its libraries for
  (the script sets `LSMinimumSystemVersion` to match). Qt's official installer targets
  older macOS versions.
- Regression tests: `tests/run-tests.sh` (add `--asan` for a sanitizer build); see
  [tests/README.md](tests/README.md).
