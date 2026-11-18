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
// - Class provides management of the signatures database
// - Supports both built-in and user database entries
//
// ==========================================================================

#pragma once

#include <QList>
#include <QString>

#include <memory>

#define DBEX_ENTRIES_MAX 300
#define DB_VER_STR "03"

#include "snoop.h"

class CDocLog;
class CSnoopConfig;
class QLockFile;

// Signature exception structure with metadata fields
struct CompExcMm
{
  QString strXMake;             // EXIF Make
  QString strXModel;            // EXIF Model
};

// Signature structure for hardcoded table
struct CompSigConst
{
  teEditor eEditor;             // Digicam vs software/editor
  QString strXMake;             // Blank for editors (set to strMSwDisp)
  QString strXModel;            // Blank for editors
  QString strUmQual;
  QString strCSig;              // Signature
  QString strCSigRot;           // Signature of rotated DQTs
  QString strXSubsamp;          // Blank for editors
  QString strMSwTrim;           // Blank for digicam
  QString strMSwDisp;           // Blank for digicam
};

// Signature structure for runtime table (can use QStrings)
struct CompSig
{
  bool bValid = false;          // Set to FALSE for removal
  teEditor eEditor = ENUM_EDITOR_UNSET;
  QString strXMake;             // Blank for editors
  QString strXModel;            // Blank for editors
  QString strUmQual;
  QString strCSig;
  QString strCSigRot;
  QString strXSubsamp;          // Blank for editors
  QString strMSwTrim;           // Blank for digicam
  QString strMSwDisp;           // Blank for digicam
};

class CDbSigs
{
public:
  CDbSigs(CDocLog * pLog, CSnoopConfig *pAppConfig);
  ~CDbSigs();

  int32_t GetNumSigsInternal();
  int32_t GetNumSigsExtra();
  int32_t GetDBNumEntries();

  bool GetDBEntry(int32_t nInd, CompSig * pEntry);
  uint32_t IsDBEntryUser(uint32_t nInd);

  uint32_t DatabaseExtraGetNum();
  CompSig DatabaseExtraGet(uint32_t nInd);

  // The user database: JPEGsnoop_db.dat in the folder SetDbDir() gave (empty
  // if none was)
  QString DatabaseExtraPath() const;

  // Read the user database again (at startup, when its folder changes, when
  // Manage Local DB opens, and before the file is changed). A missing file is
  // an empty database.
  void DatabaseExtraLoad();

  // Why the user database file couldn't be read in full, or empty if it
  // could (or there is none). Its entries before the problem are used.
  QString DatabaseExtraLoadError() const { return m_strLoadError; }

  enum class AddResult
  {
    Added,                      // strDetail: a note about the old file, if any
    AlreadyIn,                  // built in, or the same user entry
    Full,                       // DBEX_ENTRIES_MAX user entries already
    SaveFailed                  // strDetail: why; the entry isn't kept
  };

  // Add an entry to the user database and save it (Tools > Add Camera/SW to
  // DB). For software, the make and model aren't kept. The file is read
  // again first, so what another copy of JPEGsnoop added is kept.
  AddResult DatabaseExtraAdd(QString strExifMake, QString strExifModel,
                             QString strQual, QString strSig, QString strSigRot, QString strCss,
                             teSource eUserSource, QString strUserSoftware, QString &strDetail);

  enum class RemoveResult
  {
    Removed,                    // strDetail: a note about the old file, if any
    NotThere,                   // none of them is in the file, read in full: it isn't written
    SaveFailed                  // strDetail: why, also when none was found in a file
                                // that couldn't be read in full; the list is as the
                                // file is
  };

  // Take entries out of the user database and save it (Tools > Manage Local
  // DB). The file is read again first, so what another copy of JPEGsnoop
  // added or took out since is left as it is. Each entry given is taken out
  // once, if the file still has one with the same fields.
  RemoveResult DatabaseExtraRemove(const QList<CompSig> &aEntries, QString &strDetail);

  bool SearchCom(QString strCom);

  uint32_t GetIjgNum();
  QString GetIjgEntry(uint32_t nInd);

  void SetDbDir(QString strDbDir);
  void SetFirstRun(bool bFirstRun);

  bool LookupExcMmNoMkr(QString strMake, QString strModel);
  bool LookupExcMmIsEdit(QString strMake, QString strModel);

private:
  CDocLog *m_pDocLog;

  void SetEntryValid(uint32_t nInd, bool bValid);

  std::unique_ptr<QLockFile> DatabaseExtraLock(QString &strWhy) const;
  QByteArray DatabaseExtraBytes() const;
  bool DatabaseExtraReplace(const QByteArray &abData, QString &strNote);

  bool IsBuiltInEntry(const CompSig &sEntry) const;
  bool IsUserEntry(const CompSig &sEntry) const;

  CSnoopConfig *m_pAppConfig;

  CompSig m_sSigListExtra[DBEX_ENTRIES_MAX];  // Extra entries
  int32_t m_nSigListExtraNum;

  int32_t m_nSigListNum;
  static const CompSigConst m_sSigList[];       // Built-in entries

  int32_t m_nExcMmNoMkrListNum;
  static const CompExcMm m_sExcMmNoMkrList[];

  int32_t m_nExcMmIsEditListNum;
  static const CompExcMm m_sExcMmIsEditList[];

  int32_t m_nSwIjgListNum;
  int32_t m_nXcomSwListNum;

  QString m_strDbDir;           // Database directory
  QStringList m_sXComSwList;
  QStringList m_sSwIjgList;

  bool m_bFirstRun;             // First time running app?

  QString m_strLoadError;       // See DatabaseExtraLoadError()
  bool m_bKeepOldFile = false;  // Copy the file to .bak before replacing it
};
