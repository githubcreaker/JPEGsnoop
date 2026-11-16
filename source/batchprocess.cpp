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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTextStream>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <cstring>
#include <dirent.h>
#endif

#include "DocLog.h"
#include "ImgDecode.h"
#include "JfifDecode.h"
#include "SnoopConfig.h"
#include "WindowBuf.h"
#include "snoop.h"

#include "batchprocess.h"

//-----------------------------------------------------------------------------
// Extract All

QString numberedExportName(const QString &strOut, uint32_t nNum)
{
  const QFileInfo fiOut(strOut);
  return QDir(fiOut.path()).filePath(QString("%1.%2.jpg").arg(fiOut.completeBaseName()).arg(nNum, 6, 10, QChar('0')));
}

// (Without the case: macOS disks usually ignore it, so "x.000001.JPG" would
// be replaced too. QDir::System lists links that lead nowhere.)
QStringList existingNumberedExports(const QString &strOut)
{
  const QFileInfo fiOut(strOut);
  const QRegularExpression reNumbered(QString("^%1\\.[0-9]{6,}\\.jpg$").arg(QRegularExpression::escape(fiOut.completeBaseName())),
                                      QRegularExpression::CaseInsensitiveOption);
  QStringList astrExisting;
  const QStringList astrNames = QDir(fiOut.path()).entryList(QDir::Files | QDir::Hidden | QDir::System, QDir::Name);

  for(const QString &strName : astrNames)
  {
    if(reNumbered.match(strName).hasMatch())
      astrExisting << strName;
  }

  return astrExisting;
}

bool removeLinkAt(const QString &strPath, QString &strError)
{
  if(!QFileInfo(strPath).isSymLink())
    return true;

  // (QFile::remove() removes the link, not its target)
  QFile fileLink(strPath);
  if(fileLink.remove())
    return true;

  strError = fileLink.errorString();
  return false;
}

// Master left the report of the last place it looked and moved the start
// offset there. Each place is analyzed with the report and message boxes
// off, as a video can have thousands of frames.
ExtractAllResult extractAllJpegs(CwindowBuf *pWBuf, CjfifDecode *pJfifDec, CSnoopConfig *pAppConfig, CDocLog *pLog,
                                 QFile *pFile, const QString &strSrc, const QString &strOut,
                                 const ExportJpegSettings &sSettings, const std::function<void()> &fnSrcChanged,
                                 const std::function<void(uint32_t)> &fnProgress)
{
  const uint32_t nStartSaved = pAppConfig->startPos();
  const bool bInteractiveSaved = pAppConfig->interactive();
  const auto restore = qScopeGuard([&]() {
    pAppConfig->setInteractive(bInteractiveSaved);
    pAppConfig->setStartPos(nStartSaved);
    pLog->Enable();
  });

  ExtractAllResult sResult;
  uint32_t nPos = 0;
  const uint32_t nFileSize = static_cast<uint32_t>(pFile->size());

  pLog->Disable();
  pAppConfig->setInteractive(false);

  for(bool bMore = true; bMore;)
  {
    if(fnProgress)
      fnProgress(sResult.nExported);

    // Reload the buffer as AnalyzeOpen() does: reading past the end of the
    // file (as the last analysis or search may have) leaves it marked bad,
    // and the next scan then stops at once ("Ran out of buffer")
    pWBuf->BufLoadWindow(nPos);
    pAppConfig->setStartPos(nPos);
    fnSrcChanged();
    pJfifDec->ProcessFile(pFile);

    const QString strPos = QString("0x%1").arg(nPos, 8, 16, QChar('0'));

    if(!pJfifDec->ExportJpegPrepare(strSrc, sSettings.bForceSoi, sSettings.bForceEoi, sSettings.bIgnoreEoi))
    {
      sResult.astrResults << QString("  [%1] Not exported: %2").arg(strPos, pJfifDec->GetExportError());
      sResult.abResultOk << false;
    }
    else
    {
      const QString strName = numberedExportName(strOut, sResult.nExported + 1);

      // A link of that name is replaced, as a file of that name is: written
      // through, it replaced the file it led to, which may be one of a
      // batch's images (the names are made, not chosen in a save panel)
      QString strLinkError;
      if(!removeLinkAt(strName, strLinkError))
      {
        sResult.strWriteError = QString("ERROR: Couldn't replace the link [%1]: [%2]").arg(strName, strLinkError);
        sResult.astrResults << QString("  [%1] %2").arg(strPos, sResult.strWriteError);
        sResult.abResultOk << false;
        sResult.bWriteOk = false;
        break;
      }

      if(!pJfifDec->ExportJpegDo(strSrc, strName, nFileSize, sSettings.bOverlayEn, sSettings.bDhtAviInsert,
                                 sSettings.bForceSoi, sSettings.bForceEoi))
      {
        // The next would fail the same way
        sResult.strWriteError = pJfifDec->GetExportError();
        sResult.astrResults << QString("  [%1] %2").arg(strPos, sResult.strWriteError);
        sResult.abResultOk << false;
        sResult.bWriteOk = false;
        break;
      }

      sResult.nExported++;
      sResult.astrResults << QString("  [%1] Exported to [%2]").arg(strPos, strName);
      sResult.abResultOk << true;
    }

    // The next SOI after this one (the search starts after nPos)
    bMore = pWBuf->BufSearch(nPos, 0xFFD8FF, 3, true, nPos);
  }

  return sResult;
}

//-----------------------------------------------------------------------------
// Batch Process

void addReportHeader(CDocLog *pLog, const QString &strPath, qint64 nSize)
{
  pLog->AddLine("");
  pLog->AddLine(QString("JPEGsnoop %1 by Calvin Hass").arg(QString(VERSION_STR)));
  pLog->AddLine("  http://www.impulseadventure.com/photo/");
  pLog->AddLine("  -------------------------------------");
  pLog->AddLine("");
  pLog->AddLine(QString("  Filename: [%1]").arg(strPath));
  pLog->AddLine(QString("  Filesize: [%1] bytes").arg(nSize));
}

QString emptyFileError(const QString &strPath)
{
  return QString("ERROR: File length is zero, no decoding done. [%1]").arg(strPath);
}

namespace
{

// A folder's path for comparing with another (its links resolved, if it exists)
QString comparablePath(const QString &strPath)
{
  const QFileInfo fi(strPath);
  const QString strCanonical = fi.canonicalFilePath();
  return strCanonical.isEmpty() ? QDir::cleanPath(fi.absoluteFilePath()) : strCanonical;
}

// Master processed .jpg and .jpeg files (in any case), or with Extract All,
// every file, as it may be a video or other file with JPEGs inside
void listFolder(const BatchSettings &sSettings, const QString &strSkipFolder, const QString &strRel, BatchList &list)
{
  const QDir dirIn(QDir(sSettings.strInput).filePath(strRel));

  // (Master took a folder it couldn't read for an empty one)
  const QString strError = folderReadError(dirIn.path());
  if(!strError.isEmpty())
  {
    list.folderErrors << BatchFolderError{ dirIn.path(), strError };
    return;
  }

  const QFileInfoList entries =
    dirIn.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);

  for(const QFileInfo &fi : entries)
  {
    const QString strEntryRel = strRel.isEmpty() ? fi.fileName() : strRel + "/" + fi.fileName();

    if(fi.isDir())
    {
      // A link to a folder can lead back to one already listed (master
      // followed them, so one to a parent folder listed its files again at
      // each level, until the paths got too long)
      if(!sSettings.bSubfolders || fi.isSymLink() || (comparablePath(fi.filePath()) == strSkipFolder))
        continue;
      listFolder(sSettings, strSkipFolder, strEntryRel, list);
    }
    else
    {
      const QString strSuffix = fi.suffix().toLower();
      if(!sSettings.bExtractAll && (strSuffix != "jpg") && (strSuffix != "jpeg"))
        continue;

      BatchFile sFile;
      sFile.strSrc = fi.filePath();
      const QString strDst = QDir(sSettings.strOutput).filePath(strEntryRel);
      sFile.strLog = strDst + ".txt";
      sFile.strExport = strDst + ".export.jpg";
      list.files << sFile;
    }
  }
}

} // namespace

QString folderReadError(const QString &strFolder)
{
#ifdef Q_OS_UNIX
  DIR *pDir = opendir(QFile::encodeName(strFolder).constData());
  if(!pDir)
  {
    const int nErr = errno;
    return QString::fromLocal8Bit(strerror(nErr));
  }
  closedir(pDir);
  return QString();
#else
  return QDir(strFolder).isReadable() ? QString() : QString("Can't be read");
#endif
}

BatchList batchFileList(const BatchSettings &sSettings)
{
  // The output folder, if it is a subfolder of the input folder (not the
  // input folder itself, where master put the logs by default)
  QString strSkipFolder = comparablePath(sSettings.strOutput);
  if(strSkipFolder == comparablePath(sSettings.strInput))
    strSkipFolder.clear();

  BatchList list;
  listFolder(sSettings, strSkipFolder, QString(), list);
  return list;
}

QStringList batchExistingOutputs(const BatchSettings &sSettings, const QList<BatchFile> &files)
{
  const QDir dirOut(sSettings.strOutput);
  QStringList astrExisting;

  for(const BatchFile &sFile : files)
  {
    const QFileInfo fiLog(sFile.strLog);
    if(fiLog.exists() || fiLog.isSymLink())
      astrExisting << dirOut.relativeFilePath(sFile.strLog);

    if(sSettings.bExtractAll)
    {
      const QDir dirExport(QFileInfo(sFile.strExport).path());
      for(const QString &strName : existingNumberedExports(sFile.strExport))
        astrExisting << dirOut.relativeFilePath(dirExport.filePath(strName));
    }
  }

  return astrExisting;
}

CBatchProcess::CBatchProcess(CSnoopConfig *pAppConfig, CDbSigs *pDbSigs, QObject *pParent) :
  QObject(pParent),
  m_pAppConfig(pAppConfig),
  m_pReport(new QPlainTextEdit),
  m_pLog(new CDocLog(m_pReport)),
  m_pWBuf(new CwindowBuf(m_pLog)),
  m_pImgDec(new CimgDecode(m_pLog, m_pWBuf, m_pAppConfig, this)),
  m_pJfifDec(new CjfifDecode(m_pLog, pDbSigs, m_pWBuf, m_pImgDec, m_pAppConfig, this))
{
  // As MainWindow's report
  m_pReport->appendPlainText("");
}

// The decoders first, as MainWindow does: they use the buffer and the log
CBatchProcess::~CBatchProcess()
{
  delete m_pJfifDec;
  delete m_pImgDec;
  delete m_pWBuf;
  delete m_pLog;
  delete m_pReport;
}

// The log is what File > Open would show for the file, which is also what
// File > Save Log would write: MainWindow::AnalyzeOpen() and AnalyzeFileDo()
// without the File Overlay, which this batch doesn't have. (Master wrote it
// in the Windows code page, with characters it lacks changed to "?". The log
// is UTF-8, as Save Log writes.)
CBatchProcess::Result CBatchProcess::process(const BatchFile &sFile, bool bExtractAll)
{
  const uint32_t nStartSaved = m_pAppConfig->startPos();
  const bool bInteractiveSaved = m_pAppConfig->interactive();
  const auto restore = qScopeGuard([&]() {
    m_pAppConfig->setInteractive(bInteractiveSaved);
    m_pAppConfig->setStartPos(nStartSaved);
  });

  // Decode from the start of each file (master's default command-line and
  // batch offset), and say nothing in message boxes
  m_pAppConfig->setStartPos(0);
  m_pAppConfig->setInteractive(false);

  Result sResult;

  // A new file: the decoders forget the last one, and its image
  emit ImgSrcChanged();
  m_pImgDec->ClearDecodedImage();
  m_pLog->Clear();

  QFile file(sFile.strSrc);
  if(!file.open(QIODevice::ReadOnly))
  {
    // Master skipped the file without saying so
    sResult.strError = QString("Couldn't open file: %1").arg(file.errorString());
    return sResult;
  }

  const qint64 nSize = file.size();
  bool bAnalyzed = false;

  if(nSize <= 0)
  {
    m_pLog->AddLineErr(emptyFileError(sFile.strSrc));
  }
  else
  {
    addReportHeader(m_pLog, sFile.strSrc, nSize);
    m_pLog->AddLine("");

    if(nSize > 0xFFFFFFFF)
    {
      m_pLog->AddLineErr(QString("ERROR: File too large for this version of JPEGsnoop. [Size=0x%1]").arg(nSize));
    }
    else
    {
      m_pWBuf->BufFileSet(&file);
      m_pWBuf->BufLoadWindow(0);
      m_pJfifDec->ProcessFile(&file);
      bAnalyzed = true;
    }
  }

  // The log, in the output folder's copy of the file's folder. It replaces
  // whatever has its name, and nothing else: opened to write, a log whose
  // name was a link (symbolic or hard) to one of the images wrote over the
  // image. A QSaveFile is renamed into place, so it replaces a hard link
  // rather than the file it shares; a symbolic link is removed first. A
  // failed log also leaves the old one, if there was one.
  QSaveFile fileLog(sFile.strLog);
  QString strLinkError;
  if(!QDir().mkpath(QFileInfo(sFile.strLog).path()))
  {
    sResult.strError = QString("Couldn't create the folder for the log [%1]").arg(QFileInfo(sFile.strLog).path());
  }
  else if(!removeLinkAt(sFile.strLog, strLinkError))
  {
    sResult.strError = QString("Couldn't replace the link [%1]: %2").arg(sFile.strLog, strLinkError);
  }
  else if(!fileLog.open(QIODevice::WriteOnly | QIODevice::Text))
  {
    sResult.strError = QString("Couldn't write the log [%1]: %2").arg(sFile.strLog, fileLog.errorString());
  }
  else
  {
    QTextStream out(&fileLog);
    out << m_pReport->toPlainText();
    out.flush();
    if(!fileLog.commit())
      sResult.strError = QString("Couldn't write the log [%1]: %2").arg(sFile.strLog, fileLog.errorString());
    else
      sResult.bLogWritten = true;
  }

  // Every JPEG in the file, with Tools > Export JPEG's defaults (master left
  // out the standard Huffman tables a Motion JPEG video's frames need, which
  // Export JPEG adds by default)
  if(bExtractAll && bAnalyzed)
  {
    bool bIsAvi = false;
    bool bIsMjpeg = false;
    m_pJfifDec->GetAviMode(bIsAvi, bIsMjpeg);

    ExportJpegSettings sExport;
    sExport.bExtractAll = true;
    sExport.bDhtAviInsert = bIsMjpeg;

    sResult.bExtracted = true;
    sResult.sExtract = extractAllJpegs(m_pWBuf, m_pJfifDec, m_pAppConfig, m_pLog, &file, sFile.strSrc,
                                       sFile.strExport, sExport, [this]() { emit ImgSrcChanged(); });
  }

  if(bAnalyzed)
    m_pWBuf->BufFileUnset();
  file.close();

  return sResult;
}
