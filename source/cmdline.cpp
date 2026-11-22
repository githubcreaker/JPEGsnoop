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

#include <algorithm>
#include <cstdio>
#include <iterator>

#include <QPlainTextEdit>
#include <QScopeGuard>
#include <QTextStream>

#include "DbSigs.h"
#include "DocLog.h"
#include "General.h"
#include "SnoopConfig.h"
#include "batchprocess.h"

#include "cmdline.h"

namespace
{

// The options that take a value: what the next argument is for
enum teNextArg
{
  ARG_OPTION,                   // An option or a file name
  ARG_INPUT,                    // -i's file
  ARG_OUTPUT,                   // -o's log
  ARG_BATCH_DIR,                // -b's or -br's folder
  ARG_OFFSET_POS,               // -offset_pos's offset
  ARG_IGNORED                   // after an unknown option
};

// -offset_pos's value: a decimal number of bytes (master read it with
// _ttoi(), which made anything else 0, or as much of it as was digits)
bool parseOffset(const QString &strValue, uint32_t &nOffset)
{
  const auto isDigit = [](QChar ch) { return (ch >= QLatin1Char('0')) && (ch <= QLatin1Char('9')); };
  if(strValue.isEmpty() || !std::all_of(strValue.cbegin(), strValue.cend(), isDigit))
    return false;

  bool bOk = false;
  const qulonglong nValue = strValue.toULongLong(&bOk);
  if(!bOk || (nValue > 0xFFFFFFFFu))
    return false;

  nOffset = static_cast<uint32_t>(nValue);
  return true;
}

// Qt's debug and info messages (the decoders' and the settings' qDebug())
// would fill the terminal; warnings and errors are kept
void cmdLineMessage(QtMsgType eType, const QMessageLogContext &context, const QString &strMsg)
{
  if((eType == QtDebugMsg) || (eType == QtInfoMsg))
    return;

  std::fprintf(stderr, "%s\n", qPrintable(qFormatLogMessage(eType, context, strMsg)));
  std::fflush(stderr);
}

// The settings for a file of the batch, or -i's
BatchFileOptions fileOptions(const CmdLineOptions &sOptions)
{
  BatchFileOptions sFileOptions;
  sFileOptions.bExtractAll = sOptions.bExtractAll;
  sFileOptions.bForceDhtAvi = sOptions.bExtractDhtAvi;
  sFileOptions.eOffset = sOptions.eOffset;
  sFileOptions.nOffsetPos = sOptions.nOffsetPos;
  return sFileOptions;
}

// A file's errors, one line each. Returns false if there were any.
bool reportFile(const QString &strFile, const CBatchProcess::Result &sResult, QTextStream &err)
{
  bool bOk = true;

  if(!sResult.strOpenError.isEmpty())
  {
    err << QString("ERROR: Couldn't open file [%1]: %2").arg(strFile, sResult.strOpenError) << "\n";
    bOk = false;
  }

  if(!sResult.strDecodeError.isEmpty())
  {
    err << QString("%1 [%2]").arg(sResult.strDecodeError, strFile) << "\n";
    bOk = false;
  }

  if(!sResult.strLogError.isEmpty())
  {
    err << "ERROR: " << sResult.strLogError << "\n";
    bOk = false;
  }

  if(sResult.bExtracted && !sResult.sExtract.bWriteOk)
  {
    err << sResult.sExtract.strWriteError << "\n";
    bOk = false;
  }

  return bOk;
}

// -i: master's CJPEGsnoopCore::DoAnalyzeOffset(), DoLogSave() and, with
// -ext_all, DoExtractEmbeddedJPEG()
int runFile(const CmdLineOptions &sOptions, CSnoopConfig *pAppConfig, CDbSigs *pDbSigs, QTextStream &err)
{
  // Master only checked this in its debug build: the log replaced the image
  if(!sOptions.strOutput.isEmpty() && SameFile(sOptions.strInput, sOptions.strOutput))
  {
    err << QString("ERROR: The log [%1] would replace the file analyzed [%2]").arg(sOptions.strOutput, sOptions.strInput)
        << "\n";
    return 1;
  }

  // Extract All's names come from the log's, as master's did (or else from
  // the file's)
  BatchFile sFile;
  sFile.strSrc = sOptions.strInput;
  sFile.strLog = sOptions.strOutput;
  sFile.strExport = sOptions.strOutput.isEmpty() ? (sOptions.strInput + ".export.jpg") : sOptions.strOutput;

  BatchFileOptions sFileOptions = fileOptions(sOptions);
  sFileOptions.bLogOpenError = true;

  CBatchProcess batch(pAppConfig, pDbSigs);
  const CBatchProcess::Result sResult = batch.process(sFile, sFileOptions);
  return reportFile(sOptions.strInput, sResult, err) ? 0 : 1;
}

// -b and -br: master's GenBatchFileList() and DoBatchFileProcess(), with the
// logs (and exports) next to the files
int runBatch(const CmdLineOptions &sOptions, CSnoopConfig *pAppConfig, CDbSigs *pDbSigs, QTextStream &err)
{
  BatchSettings sBatch;
  sBatch.strInput = sOptions.strBatchDir;
  sBatch.strOutput = sOptions.strBatchDir;
  sBatch.bSubfolders = sOptions.bBatchSubfolders;
  sBatch.bExtractAll = sOptions.bExtractAll;

  const BatchList list = batchFileList(sBatch);
  int nStatus = 0;

  // (Master did nothing for a folder it couldn't read)
  for(const BatchFolderError &sError : list.folderErrors)
  {
    err << QString("ERROR: Couldn't read the folder [%1]: %2").arg(sError.strFolder, sError.strError) << "\n";
    nStatus = 1;
  }

  // The logs and exports are written next to the files, so (with -ext_all,
  // which analyzes every file) one could be another of the batch's files,
  // the file one is a link to, or a link on the way to it: none of them is
  // replaced, before or after it is analyzed. (Master replaced it, and then
  // analyzed what had replaced it.)
  QStringList astrInputs;
  std::transform(list.files.cbegin(), list.files.cend(), std::back_inserter(astrInputs),
                 [](const BatchFile &sFile) { return sFile.strSrc; });

  BatchFileOptions sFileOptions = fileOptions(sOptions);
  sFileOptions.fnKeep = keepPaths(astrInputs);
  CBatchProcess batch(pAppConfig, pDbSigs);

  for(const BatchFile &sFile : list.files)
  {
    if(!reportFile(sFile.strSrc, batch.process(sFile, sFileOptions), err))
      nStatus = 1;
    err.flush();
  }

  return nStatus;
}

} // namespace

CmdLineOptions parseCmdLine(const QStringList &astrArgs)
{
  CmdLineOptions sOptions;
  teNextArg eNext = ARG_OPTION;
  QString strOption;            // The option whose value is next

  for(const QString &strArg : astrArgs)
  {
    switch(eNext)
    {
    case ARG_OPTION:
      if(strArg.startsWith(QLatin1String("-psn_")))
      {
        // (macOS: the process serial number)
      }
      else if(!strArg.startsWith(QLatin1Char('-')))
      {
        // A file to analyze (or with no option, to open in the window)
        if(!strArg.isEmpty())
          sOptions.strInput = strArg;
      }
      else
      {
        sOptions.bCmdLine = true;
        strOption = strArg;
        const QString strName = strArg.mid(1);

        if(strName == QLatin1String("i"))
          eNext = ARG_INPUT;
        else if(strName == QLatin1String("o"))
          eNext = ARG_OUTPUT;
        else if(strName == QLatin1String("b"))
          eNext = ARG_BATCH_DIR;
        else if(strName == QLatin1String("br"))
        {
          sOptions.bBatchSubfolders = true;
          eNext = ARG_BATCH_DIR;
        }
        else if((strName == QLatin1String("help")) || (strName == QLatin1String("h")) ||
                (strName == QLatin1String("?")))
          sOptions.bHelp = true;
        else if(strName == QLatin1String("offset_start"))
          sOptions.eOffset = DEC_OFFSET_START;
        else if(strName == QLatin1String("offset_srch1"))
          sOptions.eOffset = DEC_OFFSET_SRCH1;
        else if(strName == QLatin1String("offset_srch2"))
          sOptions.eOffset = DEC_OFFSET_SRCH2;
        else if(strName == QLatin1String("offset_pos"))
        {
          sOptions.eOffset = DEC_OFFSET_POS;
          sOptions.nOffsetPos = 0;
          eNext = ARG_OFFSET_POS;
        }
        else if(strName == QLatin1String("ext_all"))
          sOptions.bExtractAll = true;
        else if(strName == QLatin1String("ext_dht_avi"))
          sOptions.bExtractDhtAvi = true;
        else if(strName == QLatin1String("scan"))
          sOptions.bScan = true;
        else if(strName == QLatin1String("maker"))
          sOptions.bMaker = true;
        else if(strName == QLatin1String("scandump"))
          sOptions.bScanDump = true;
        else if(strName == QLatin1String("histo_y"))
          sOptions.bHistoY = true;
        else if(strName == QLatin1String("dhtexp"))
          sOptions.bDhtExpand = true;
        else if(strName == QLatin1String("exif_hide_unk"))
          sOptions.bExifHideUnknown = true;
        else if(strName == QLatin1String("done"))
          sOptions.bDone = true;
        else
        {
          // The rest is ignored, and the help shown
          sOptions.strError = QString("ERROR: Unknown command-line flag [%1]").arg(strArg);
          eNext = ARG_IGNORED;
        }
      }
      break;

    // Master's parser took the argument after one of these as its value,
    // even one that starts with "-". (An empty one it took for none.)
    case ARG_INPUT:
    case ARG_OUTPUT:
    case ARG_BATCH_DIR:
      if(strArg.isEmpty())
      {
        sOptions.strError = QString("ERROR: Command-line flag [%1] needs a value after it").arg(strOption);
        eNext = ARG_IGNORED;
        break;
      }
      if(eNext == ARG_INPUT)
        sOptions.strInput = strArg;
      else if(eNext == ARG_OUTPUT)
        sOptions.strOutput = strArg;
      else
        sOptions.strBatchDir = strArg;
      eNext = ARG_OPTION;
      break;

    case ARG_OFFSET_POS:
      if(!parseOffset(strArg, sOptions.nOffsetPos))
      {
        sOptions.strError = QString("ERROR: Offset for command-line flag [%1] isn't a decimal number below 4294967296: [%2]")
                              .arg(strOption, strArg);
        eNext = ARG_IGNORED;
      }
      else
        eNext = ARG_OPTION;
      break;

    case ARG_IGNORED:
    default:
      break;
    }
  }

  // (Master ignored an option with no value after it, and did nothing when
  // no file was named)
  if((eNext != ARG_OPTION) && (eNext != ARG_IGNORED))
    sOptions.strError = QString("ERROR: Command-line flag [%1] needs a value after it").arg(strOption);
  else if(sOptions.bCmdLine && sOptions.strError.isEmpty() && !sOptions.bHelp && sOptions.strInput.isEmpty() &&
          sOptions.strBatchDir.isEmpty())
    sOptions.strError = "ERROR: No file to analyze: give -i <fname_in>, -b <dir> or -br <dir>";

  return sOptions;
}

void hideArgsFromQt(int &argc, char **argv)
{
  if(argc > 1)
  {
    argc = 1;
    argv[1] = nullptr;
  }
}

QString cmdLineHelp()
{
#ifdef Q_OS_MACOS
  const QString strProgram = "JPEGsnoopQt.app/Contents/MacOS/JPEGsnoopQt";
#else
  const QString strProgram = "JPEGsnoopQt";
#endif

  QString strMsg;
  strMsg += "\n";
  strMsg += QString("JPEGsnoop v%1\n").arg(QString(VERSION_STR));
  strMsg += "\n";
  strMsg += QString("%1 <parameters>\n").arg(strProgram);
  strMsg += "\n";
  strMsg += " One of the following input parameters:\n";
  strMsg += "   -help              : Show command summary\n";
  strMsg += "   -i <fname_in>      : Defines input JPEG filename\n";
  strMsg += "   -b <dir>           : Batch process directory\n";
  strMsg += "   -br <dir>          : Batch process directory (recursive)\n";
  strMsg += " Zero or more of the following input parameters:\n";
  strMsg += "   -o <fname_log>     : Defines output log filename\n";
  strMsg += "   -ext_all           : Extract all from file\n";
  strMsg += "   -ext_dht_avi       : Force insert DHT for AVI (-ext_all mode)\n";
  strMsg += "   -scan              : Enables Scan Segment decode\n";
  strMsg += "   -maker             : Enables Makernote decode\n";
  strMsg += "   -scandump          : Enables Scan Segment dumping\n";
  strMsg += "   -histo_y           : Enables luminance histogram\n";
  strMsg += "   -dhtexp            : Enables DHT table expansion into huffman bitstrings\n";
  strMsg += "   -exif_hide_unk     : Disables decoding of unknown makernotes\n";
  strMsg += "   -offset_start      : Decode at start of file\n";
  strMsg += "   -offset_srch1      : Decode at 1st SOI found in file\n";
  strMsg += "   -offset_srch2      : Decode at 1st SOI found after start of file\n";
  strMsg += "   -offset_pos <###>  : Decode from byte ### (decimal) in file\n";
  strMsg += "   -done              : Indicate when operations complete\n";
  strMsg += "\n";
  return strMsg;
}

int runCmdLine(const CmdLineOptions &sOptions, CSnoopConfig *pAppConfig, CDbSigs *pDbSigs, QTextStream &out,
               QTextStream &err)
{
  int nStatus = 0;

  if(!sOptions.strError.isEmpty())
  {
    // Master showed the error in a message box, then the command summary
    err << sOptions.strError << "\n";
    err.flush();
    out << cmdLineHelp();
    nStatus = 1;
  }
  else if(sOptions.bHelp)
  {
    out << cmdLineHelp();
  }
  else
  {
    // The options asked for, over the saved settings (which aren't changed)
    if(sOptions.bScan)
      pAppConfig->setDecodeImage(true);
    if(sOptions.bMaker)
      pAppConfig->setDecodeMaker(true);
    if(sOptions.bScanDump)
      pAppConfig->setScanDump(true);
    if(sOptions.bHistoY)
    {
      pAppConfig->setDisplayRgbHistogram(true);
      pAppConfig->setDisplayYHistogram(true);
    }
    if(sOptions.bDhtExpand)
      pAppConfig->setExpandDht(true);
    if(sOptions.bExifHideUnknown)
      pAppConfig->setHideUnknownExif(true);

    // A file named takes the place of a folder, as on Windows
    if(!sOptions.strInput.isEmpty())
      nStatus = runFile(sOptions, pAppConfig, pDbSigs, err);
    else
      nStatus = runBatch(sOptions, pAppConfig, pDbSigs, err);
  }

  err.flush();

  if(sOptions.bDone)
    out << "\nJPEGsnoop operations complete\n\n";
  out.flush();

  return nStatus;
}

int runCmdLineApp(const CmdLineOptions &sOptions, QTextStream &out, QTextStream &err)
{
  const QtMessageHandler fnHandlerSaved = qInstallMessageHandler(cmdLineMessage);
  const auto restore = qScopeGuard([fnHandlerSaved]() { qInstallMessageHandler(fnHandlerSaved); });

  // The summary, and an error in the arguments, need no settings
  if(!sOptions.strError.isEmpty() || sOptions.bHelp)
    return runCmdLine(sOptions, nullptr, nullptr, out, err);

  CSnoopConfig config;
  config.RegistryLoad(false);

  // Nothing is analyzed before the license terms are accepted, and they
  // aren't shown here (Windows showed them)
  if(!config.eulaAccepted())
  {
    err << "ERROR: The license terms haven't been accepted. Open JPEGsnoop and accept them first.\n";
    err.flush();
    return 1;
  }

  // The signatures the user added (Tools > Add Camera/SW to DB), as the window uses them
  QPlainTextEdit report;
  CDocLog log(&report);
  CDbSigs dbSigs(&log, &config);
  dbSigs.SetDbDir(config.dbPath());
  dbSigs.DatabaseExtraLoad();

  return runCmdLine(sOptions, &config, &dbSigs, out, err);
}
