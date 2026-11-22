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
// - Application configuration structures
// - Registry management
// - Note that the majority of this class is defined with public members
//
// ==========================================================================

#ifndef SNOOPCONFIG_H
#define SNOOPCONFIG_H

#include "snoop.h"              // For DEBUG_LOG_OUT

#include <QSettings>
#include <QTextStream>

class CjfifDecode;

class CSnoopConfig : public QObject
{
  Q_OBJECT

public:
  CSnoopConfig(QObject *_parent = 0);
  ~CSnoopConfig(void);

  void UseDefaults();
  void printSettings();

  // Loads the settings. For the window (bForWindow), also shows the license
  // terms if they haven't been accepted (and quits if they are declined),
  // and makes the user database's folder if no other is set. Command-line
  // mode does neither: it checks eulaAccepted() and says so instead of
  // showing a window, and writes nothing but the files asked for.
  void RegistryLoad(bool bForWindow = true);
  void RegistryStore();
  void Dirty(bool mode = true);

  void CoachReset();            // Reset all coach messages

//  QString GetDefaultDbDir();    // Public use by CSettingsDlg

  // Getter functions
  QString dbPath() { return strDbDir; }

  int32_t maxDecodeError() { return nErrMaxDecodeScan; }

  uint32_t startPos() { return nPosStart; }

  QString batchLastInput() { return strBatchLastInput; }
  QString batchLastOutput() { return strBatchLastOutput; }

  // File > Recent File: the files opened last, newest first, at most
  // RECENT_FILES_MAX (MFC's four). Saved at once, under the names MFC used
  // ("Recent File List\File1" and on); Windows saved them at exit.
  static constexpr int RECENT_FILES_MAX = 4;
  QStringList recentFiles() const;
  void setRecentFiles(const QStringList &astrFiles);

  bool autoReprocess() { return bReprocessAuto; }
  bool clipStats() { return bStatClipEn; }
  bool coachIdct() { return bCoachDecodeIdct; }
  bool decodeAc() { return bDecodeScanImgAc; }
  bool decodeImage() { return bDecodeScanImg; }
  bool decodeMaker() { return bDecodeMaker; }
  bool eulaAccepted() { return bEulaAccepted; }
  bool displayRgbHistogram() { return bHistoEn; }
  bool displayYHistogram() { return bDumpHistoY; }
  bool expandDht() { return bOutputDHTexpand; }
  bool hideUnknownExif() { return bExifHideUnknown; }
  bool interactive() { return bInteractive; }
  bool relaxedParsing() { return bRelaxedParsing; }
  bool scanDump() { return bOutputScanDump; }
  bool searchSig() { return bSigSearch; }
  bool skipLargeHashes() { return bSkipLargeHashes; }

  // Setter functions
  void setDbPath(QString path) {strDbDir = path; }
  void setAutoReprocess(bool b) { bReprocessAuto = b; }
  void setInteractive(bool b) { bInteractive = b; }
  void setMaxErrors(int32_t errors) { nErrMaxDecodeScan = errors; }
  void setStartPos(uint32_t start) { nPosStart = start; }
  void setCoachIdct(bool b) { bCoachDecodeIdct = b; }
  // Command-line mode's -scan, -maker, -scandump, -histo_y, -dhtexp and
  // -exif_hide_unk (see cmdline.h): for that run only, as on Windows. They
  // don't mark the settings changed, and command-line mode never saves them.
  void setDecodeImage(bool b) { bDecodeScanImg = b; }
  void setDecodeMaker(bool b) { bDecodeMaker = b; }
  void setScanDump(bool b) { bOutputScanDump = b; }
  void setDisplayRgbHistogram(bool b) { bHistoEn = b; }
  void setDisplayYHistogram(bool b) { bDumpHistoY = b; }
  void setExpandDht(bool b) { bOutputDHTexpand = b; }
  void setHideUnknownExif(bool b) { bExifHideUnknown = b; }
  // The folders File > Batch Process used last (saved with the settings)
  void setBatchFolders(const QString &strInput, const QString &strOutput)
  {
    strBatchLastInput = strInput;
    strBatchLastOutput = strOutput;
    bDirty = true;
  }

  // Debug Log
  // - Used if DEBUG_LOG_OUT
  bool DebugLogCreate();
  bool DebugLogAdd(QString strText);

  // Temporary status (not saved)
  QString strCurFname;          // Current filename (Debug use only)

signals:
  void ImgSrcChanged();
  void reprocess();

public slots:
  void onOptionsDhtexpand();
  void onOptionsMakernotes();
  void onScansegmentDecodeImage();
  void onOptionsHideuknownexiftags();
  void onOptionsRelaxedparsing();
  void onOptionsSkipLargeHashes();
  void onOptionsSignaturesearch();
  void onScansegmentNoidct();
  void onScansegmentFullidct();
  void onScansegmentHistogram();
  void onScansegmentHistogramy();
  void onScansegmentDump();

public:
  // Reprocess the file after an option change if Automatic Reprocessing is
  // on (otherwise offer it, once)
  void HandleAutoReprocess();

private:
  CSnoopConfig &operator = (const CSnoopConfig&);
  CSnoopConfig(CSnoopConfig&);

  QString GetExeDir();
  void CreateDir(QString Path);
  void docImageDirty();

  CjfifDecode *m_pJfifDec;

  // Interactive mode: shows message dialog box alerts
  // In non-interactive mode we suppress most alert dialogs but still
  // show them in certain circumstances:
  // - They involve an interactive user function (eg. another dialog box)
  // - They indicate a critical error
  bool bInteractive;          // Do we want user input (interactive mode)? (ie. show message boxes)

  // (Master's command-line options were kept here; see CmdLineOptions in
  // cmdline.h)

  unsigned nPosStart;           // Starting decode file offset

  // Operating system
  bool bIsWindowsNTorLater;
  bool bIsWindowsXPorLater;

  // Registry Configuration options
  QString strDbDir;             // Directory for User DB
  QString strBatchLastInput;    // Last batch process input directory
  QString strBatchLastOutput;   // Last batch process output directory
  QString strBatchExtensions;   // Extension list for batch processing (eg. ".jpg,.jpeg")

  int nErrMaxDecodeScan;        // Max # errs to show in scan decode

  bool bDirty;                  // Registry entry options dirtied?
  bool bEulaAccepted;           // Accepted the EULA?
  bool bReprocessAuto;          // Auto reprocess file when option change
  bool bSigSearch;              // Automatically search for comp signatures
  bool bDecodeScanImg;          // Scan image decode enabled
  bool bDecodeScanImgAc;        // When scan image decode, do full AC
  bool bOutputScanDump;         // Do we dump a portion of scan data?
  bool bOutputDHTexpand;
  bool bDecodeMaker;
  bool bHistoEn;                // Histogram calcs enabled?
  bool bStatClipEn;             // Enable scan decode clip stats?
  bool bDumpHistoY;             // Dump full Y DC Histogram
  bool bExifHideUnknown;        // Hide unknown exif tags?
  bool bRelaxedParsing;         // Proceed despite bad marker / format?
  bool bSkipLargeHashes;        // No hashes in the intro for files of 4 GiB or more?
  bool bCoachReprocessAuto;     // Coach msg: Need to reprocess or change to auto
  bool bCoachDecodeIdct;        // Coach msg: Warn about slow AC decode / lowres DC

  // Coach Message
  QString strReprocess;

  // Extra config (not in registry)
  bool bDecodeColorConvert;     // Do we do color convert after scan decode?

  // Debug log
  // - Used if DEBUG_LOG_OUT
  bool bDebugLogEnable;
  QString strDebugLogFname;
  QTextStream *fpDebugLog;
};

#endif
