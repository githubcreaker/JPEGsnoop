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
// - File > Batch Process: analyze each image in a folder (and optionally
//   its subfolders), saving each one's report as a log in an output folder,
//   and optionally extracting every JPEG in each file (master's batch code
//   in CJPEGsnoopCore).
// - CBatchProcess has its own decoders, as master's batch had its own
//   CJPEGsnoopCore, so the file shown in the main window (its report state,
//   preview and File Overlay) is left as it was.
// - Also Extract All's loop, shared with Tools > Export JPEG.
//
// ==========================================================================

#ifndef BATCHPROCESS_H
#define BATCHPROCESS_H

#include <cstdint>
#include <functional>

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "exportdialogs.h"

class QFile;
class QPlainTextEdit;
class CDbSigs;
class CDocLog;
class CimgDecode;
class CjfifDecode;
class CSnoopConfig;
class CwindowBuf;

//-----------------------------------------------------------------------------
// Extract All

// Extract All's names: strOut without its extension, then the JPEG's number
// (from 1, in 6 digits), then ".jpg"
QString numberedExportName(const QString &strOut, uint32_t nNum);

// The files already named as Extract All would name its exports from strOut
// (links that lead nowhere included)
QStringList existingNumberedExports(const QString &strOut);

// Make way for a file written at strPath that replaces whatever has that
// name: if the name is a symbolic link, remove the link, so the file it
// leads to isn't written. (QSaveFile writes to a link's target; a hard
// link's other names keep the old file, as QSaveFile renames the new one
// into place.) Returns false, saying why in strError, if the link stays.
bool removeLinkAt(const QString &strPath, QString &strError);

// What Extract All did at each place in a file where a JPEG starts
struct ExtractAllResult
{
  QStringList astrResults;      // One report line for each place
  QList<bool> abResultOk;       // Whether each place was exported
  uint32_t nExported = 0;
  bool bWriteOk = true;         // False if a file couldn't be written (it stops there)
  QString strWriteError;
};

// Export the JPEG at each SOI (FF D8 FF) in pFile, from its start, to
// numberedExportName(strOut, 1), (strOut, 2), ..., replacing any files of
// those names (a link of one of those names is replaced, not written
// through). Each place is analyzed with pLog and message boxes off.
// fnSrcChanged() must tell the decoders the image source changed, and
// fnProgress (if given) is called before each place with the number
// exported so far. The start offset is put back afterwards; the buffer is
// left at the last place looked at.
//
// PRE:
// - pWBuf is reading pFile
//
ExtractAllResult extractAllJpegs(CwindowBuf *pWBuf, CjfifDecode *pJfifDec, CSnoopConfig *pAppConfig, CDocLog *pLog,
                                 QFile *pFile, const QString &strSrc, const QString &strOut,
                                 const ExportJpegSettings &sSettings, const std::function<void()> &fnSrcChanged,
                                 const std::function<void(uint32_t)> &fnProgress = nullptr);

//-----------------------------------------------------------------------------
// Batch Process

// The report's first lines for a file: the program, and the file's name and
// size (also File > Open's)
void addReportHeader(CDocLog *pLog, const QString &strPath, qint64 nSize);

// The report's one line for a file of no length (also File > Open's)
QString emptyFileError(const QString &strPath);

struct BatchSettings
{
  QString strInput;             // The folder to process
  QString strOutput;            // The folder for the logs (and exports)
  bool bSubfolders = false;     // Also process the input folder's subfolders
  bool bExtractAll = false;     // Also extract every JPEG; every file is processed
};

// A file to process, and what it writes. The output paths are the file's
// path below the input folder, in the output folder, with ".txt" or
// ".export.jpg" added (as master).
struct BatchFile
{
  QString strSrc;               // The file
  QString strLog;               // Its report
  QString strExport;            // With Extract All, the name its exports are numbered from
};

// A folder whose files couldn't be listed
struct BatchFolderError
{
  QString strFolder;
  QString strError;             // Why, as the system says it
};

struct BatchList
{
  QList<BatchFile> files;
  QList<BatchFolderError> folderErrors;
};

// Why the files in a folder can't be listed, or nothing if they can. (QDir
// lists nothing for a folder it can't read, the same as for an empty one.)
QString folderReadError(const QString &strFolder);

// The files a batch processes, in name order with each folder's files and
// subfolders together (as master listed them on Windows), and the folders
// whose files couldn't be listed. Without Extract All, only .jpg and .jpeg
// files. Hidden files and folders are left out, and links to folders aren't
// followed. With subfolders, the output folder isn't searched when it is
// inside the input folder, so the logs of an earlier batch aren't processed.
BatchList batchFileList(const BatchSettings &sSettings);

// The files the batch would write that already exist (their names, from the
// output folder), links that lead nowhere included
QStringList batchExistingOutputs(const BatchSettings &sSettings, const QList<BatchFile> &files);

class CBatchProcess : public QObject
{
  Q_OBJECT

public:
  CBatchProcess(CSnoopConfig *pAppConfig, CDbSigs *pDbSigs, QObject *pParent = nullptr);
  ~CBatchProcess() override;

  struct Result
  {
    bool bLogWritten = false;
    QString strError;           // Why the file couldn't be read or its log written
    bool bExtracted = false;    // Extract All ran
    ExtractAllResult sExtract;
  };

  // Analyze the file from its start, write its report to its log, and with
  // bExtractAll, export every JPEG in it. Message boxes are off meanwhile.
  Result process(const BatchFile &sFile, bool bExtractAll);

signals:
  void ImgSrcChanged();

private slots:
  // CimgDecode connects its updateImage() signal to its parent's slot of this
  // name; there is no image to show here
  void updateImage() {}

private:
  CBatchProcess &operator = (const CBatchProcess&);
  CBatchProcess(CBatchProcess&);

  CSnoopConfig *m_pAppConfig;
  QPlainTextEdit *m_pReport;    // The file's report, as the main window shows it (never shown)
  CDocLog *m_pLog;
  CwindowBuf *m_pWBuf;
  CimgDecode *m_pImgDec;
  CjfifDecode *m_pJfifDec;
};

#endif // BATCHPROCESS_H
