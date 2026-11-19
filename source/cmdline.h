// JPEGsnoop - JPEG Image Decoder & Analysis Utility
// Copyright (C) 2018 - Calvin Hass
// http://www.impulseadventure.com/photo/jpeg-snoop.html
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

// ==========================================================================
// CLASS DESCRIPTION:
// - Command-line mode (master's CMyCommandParser, CJPEGsnoopApp::DoCmdLineCore()
//   and CmdLineHelp()): started with any option ("-i", "-b", ...), the app
//   analyzes the files named without showing its window, writes their
//   reports to logs, and quits. Started with only file names, it opens the
//   last one in its window instead (MainWindow::openArguments()).
// - As on Windows:
//   - "-i FILE" analyzes FILE, and "-o LOG" saves its report to LOG.
//   - "-b DIR" (or "-br DIR", with its subfolders) analyzes each .jpg and
//     .jpeg file in DIR, saving each one's report next to it as NAME.txt
//     (-o isn't used).
//   - "-ext_all" also extracts every JPEG in the file (or in each file of
//     DIR, of any type), to LOG's name without its extension, or else to
//     FILE.export, numbered: "LOG.000001.jpg", ...
//   - -scan, -maker, -scandump, -histo_y, -dhtexp and -exif_hide_unk turn
//     those options on for this run (over the saved settings, which are
//     left as they were), and -offset_* say where to decode each file from.
//   - A file named without an option is the file to analyze, as with -i.
//   - Windows reported the help, its errors and "-done" in the console it
//     was started from; here they go to the standard output (the help and
//     "-done") and error (the errors).
// - Not as on Windows:
//   - Windows showed an unknown option in a message box, and showed the
//     license terms if they hadn't been accepted. Neither shows a window
//     here: the error says so, and nothing is done.
//   - Errors Windows ignored are reported, and the exit status says whether
//     everything worked (0) or not (1): an option missing its value, an
//     offset that isn't a number, no file to analyze, a log that would
//     replace the file analyzed, a file that couldn't be read, a log or
//     export that couldn't be written, and (errors in the report too) a
//     file that is empty or too large to decode, and an offset past a
//     file's end, which Windows decoded as if 0x00 bytes were there.
//   - In a batch, no log or export replaces one of the batch's files (with
//     -ext_all, every file in the folder): Windows replaced it, and then
//     analyzed what had replaced it.
//   - -ext_dht_avi adds the standard Huffman tables an exported frame
//     lacks (as Tools > Export JPEG can); on Windows it did nothing. Without
//     it, as Export JPEG does by default, they are added to a Motion JPEG
//     video's frames (Windows added none).
//   - An argument that starts with "-psn_" is ignored: older macOS versions
//     passed one to an app opened from Finder.
//   - The window and the Dock icon are never shown (on Windows, nor was the
//     window).
//   - Nothing is written but the logs and exports: not the settings (Windows
//     saved them when something had changed them, as at its first start,
//     options and all), nor the user database's folder, which the window
//     makes at its first start.
//
// ==========================================================================

#ifndef CMDLINE_H
#define CMDLINE_H

#include <cstdint>

#include <QString>
#include <QStringList>

#include "snoop.h"

class QTextStream;
class CDbSigs;
class CSnoopConfig;

// What the arguments ask for (master kept these in CSnoopConfig)
struct CmdLineOptions
{
  bool bCmdLine = false;        // An option was given: run without the window
  QString strError;             // What is wrong with the arguments (the help follows it)
  bool bHelp = false;           // -help, -h or -?

  QString strInput;             // -i, or a file named without an option
  QString strOutput;            // -o
  QString strBatchDir;          // -b or -br
  bool bBatchSubfolders = false; // -br

  bool bExtractAll = false;     // -ext_all
  bool bExtractDhtAvi = false;  // -ext_dht_avi

  bool bScan = false;           // -scan
  bool bMaker = false;          // -maker
  bool bScanDump = false;       // -scandump
  bool bHistoY = false;         // -histo_y
  bool bDhtExpand = false;      // -dhtexp
  bool bExifHideUnknown = false; // -exif_hide_unk

  teOffsetMode eOffset = DEC_OFFSET_START;      // -offset_start, _srch1, _srch2, _pos
  uint32_t nOffsetPos = 0;      // -offset_pos's value

  bool bDone = false;           // -done
};

// Reads the arguments (without the program's name), as master's parser did:
// an argument that starts with "-" is an option, and the one after -i, -o,
// -b, -br or -offset_pos is its value (even if it starts with "-"). An
// unknown option ends the reading. With options but no file or folder to
// analyze, strError says so.
CmdLineOptions parseCmdLine(const QStringList &astrArgs);

// For a command-line run, once the arguments are read and before
// QApplication is made: leaves Qt only the program's name, so it doesn't
// take the app's arguments for its own. (Given "-i -platform -o LOG", it
// took "-o" for the platform plugin to load, and stopped the app.)
void hideArgsFromQt(int &argc, char **argv);

// The command summary (master's CmdLineHelp())
QString cmdLineHelp();

// Does what sOptions ask, with the settings in pAppConfig (loaded) and the
// signatures in pDbSigs (both unused, and may be null, for the help or an
// argument error), writing the help and "-done" to out and errors to err.
// Returns the exit status: 0 if everything worked, 1 if not.
int runCmdLine(const CmdLineOptions &sOptions, CSnoopConfig *pAppConfig, CDbSigs *pDbSigs, QTextStream &out,
               QTextStream &err);

// The whole run, once QApplication exists: loads the settings (unless only
// the help or an argument error is to be shown) and the signature database,
// runs, and returns the exit status. If the license terms haven't been
// accepted, it says so and stops, without showing them. Qt's debug and info
// messages are dropped meanwhile.
int runCmdLineApp(const CmdLineOptions &sOptions, QTextStream &out, QTextStream &err);

#ifdef Q_OS_MACOS
// Before QApplication is made: keeps the app out of the Dock, the app
// switcher and the menu bar for a command-line run (cmdline_mac.mm)
void prepareCmdLineApp();
#endif

#endif // CMDLINE_H
