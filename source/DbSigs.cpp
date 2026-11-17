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
#include <QSaveFile>
#include <QtEndian>

#include <utility>

#include "DocLog.h"
#include "General.h"
//#include "JPEGsnoop.h"
#include "snoop.h"
#include "SnoopConfig.h"

#include "DbSigs.h"

#include "Signatures.inl"


// TODO:
// - Convert m_sSigListExtra[] to use CObArray instead of managing it
//   directly. Otherwise we are inefficient with memory and could potentially
//   not allocate enough space.


// Initialize the signature database array sizes
//
// PRE:
// - m_sSigList[]
// - m_sExcMmNoMkrList[]
// - m_sExcMmIsEditList[]
// - m_sSwIjgList[]
// - m_sXComSwList[]
//
// POST:
// - m_nSigListNum
// - m_nSigListExtraNum
// - m_nExcMmNoMkrListNum
// - m_nExcMmIsEditListNum
// - m_nSwIjgListNum
// - m_nXcomSwListNum
// - m_strDbDir
//
CDbSigs::CDbSigs(CDocLog *pLog, CSnoopConfig *pAppConfig) : m_pDocLog(pLog), m_pAppConfig(pAppConfig)
{
	// Count the built-in database
	bool bDone;

	m_bFirstRun = false;

	bDone = false;
	m_nSigListNum = 0;

	while (!bDone)
	{
    if (m_sSigList[m_nSigListNum].strXMake == "*")
    {
      bDone = true;
    }
		else
    {
			m_nSigListNum++;
    }
	}

  // (The Qt port wrote an empty file here on every start, the user database
  // folder's path + "Signatures.dat": beside the folder, for want of a
  // separator)

	// Count number of exceptions in Signatures.inl
	bDone = false;
	m_nExcMmNoMkrListNum = 0;

	while (!bDone)
	{
    if (m_sExcMmNoMkrList[m_nExcMmNoMkrListNum].strXMake == "*")
      bDone = true;
		else
			m_nExcMmNoMkrListNum++;
	}

	bDone = false;
	m_nExcMmIsEditListNum = 0;

	while (!bDone)
	{
    if (m_sExcMmIsEditList[m_nExcMmIsEditListNum].strXMake == "*")
      bDone = true;
		else
			m_nExcMmIsEditListNum++;
	}

	bDone = false;
	m_nSwIjgListNum = 0;

  // Sample list of software programs that also use the IJG encoder

  m_sSwIjgList
    << "GIMP"
    << "IrfanView"
    << "idImager"
    << "FastStone Image Viewer"
    << "NeatImage"
    << "Paint.NET"
    << "Photomatix"
    << "XnView";

  m_nSwIjgListNum = m_sSwIjgList.count();

	bDone = false;
	m_nXcomSwListNum = 0;

  // Sample list of software programs marked by EXIF.COMMENT field
  //
  // NOTE: Have not included the following indicators as they also appear in some digicams in addition to software encoders.
  //   "LEAD Technologies"
  //   "U-Lead Systems"
  //   "Intel(R) JPEG Library" (unsure if ever in hardware)
  //

  m_sXComSwList
    << "gd-jpeg"
    << "Photoshop"
    << "ACD Systems"
    << "AppleMark"
    << "PICResize"
    << "NeatImage";

  m_nXcomSwListNum = m_sXComSwList.count();

	// Reset extra database
	m_nSigListExtraNum = 0;

	// Default to user database dir not set yet
	// This will cause a fail if database load/store
	// functions are called before SetDbDir()
  m_strDbDir = "";
}

CDbSigs::~CDbSigs()
{
}

// Is this the first time running the application?
// If so, we skip certain warning messages (such as lack of existing user DB file)
void CDbSigs::SetFirstRun(bool bFirstRun)
{
	m_bFirstRun = bFirstRun;
}

int32_t CDbSigs::GetNumSigsInternal()
{
	return m_nSigListNum;
}

int32_t CDbSigs::GetNumSigsExtra()
{
	return m_nSigListExtraNum;
}

// The user database file (JPEGsnoop_db.dat), as Windows wrote it:
//   "JPEGsnoop", the version ("03"), "*DB*", a 4-byte little-endian count,
//   then for each entry its make, model, editor (a number), quality,
//   signature, signature of the rotated tables, subsampling, software match
//   string and software name, then "*Z*".
// Each string ends in a NUL (a newline also ends one when read). Version 03
// is UTF-16LE (Windows' wchar_t); versions 01 and 02 used single bytes, and
// 01 has no rotated signature.
//
// Windows read only the low byte of the count, so a database of 256 or more
// entries lost its entries after the first (count mod 256); and of each
// UTF-16 character, only its low byte.

namespace
{

// A user database is a few hundred KB at most (DBEX_ENTRIES_MAX entries)
constexpr qint64 kDbMaxBytes = 4 * 1024 * 1024;

class CDbReader
{
public:
  CDbReader(const QByteArray &abData, bool bUni) : m_abData(abData), m_bUni(bUni) {}

  // False if the data ends before the string does
  bool str(QString &strOut)
  {
    strOut.clear();
    const qsizetype nCharSz = m_bUni ? 2 : 1;
    while(m_nPos + nCharSz <= m_abData.size())
    {
      const uint8_t nLo = static_cast<uint8_t>(m_abData.at(m_nPos));
      const char16_t ch =
        m_bUni ? static_cast<char16_t>(nLo | (static_cast<uint8_t>(m_abData.at(m_nPos + 1)) << 8)) : nLo;
      m_nPos += nCharSz;
      if((ch == u'\n') || (ch == 0))
        return true;
      strOut += QChar(ch);      // (single bytes as Latin-1)
    }
    m_nPos = m_abData.size();
    return false;
  }

  bool num(uint32_t &nOut)
  {
    if(m_nPos + 4 > m_abData.size())
    {
      m_nPos = m_abData.size();
      return false;
    }
    nOut = qFromLittleEndian<quint32>(m_abData.constData() + m_nPos);
    m_nPos += 4;
    return true;
  }

private:
  const QByteArray &m_abData;
  bool m_bUni;
  qsizetype m_nPos = 0;
};

// An entry's editor as written ("%u"); anything else is unknown, as on
// Windows (which cast whatever number it read)
teEditor editorFromText(const QString &strEditor)
{
  bool bOk = false;
  const uint32_t nEditor = strEditor.toUInt(&bOk);
  if(bOk && (nEditor >= ENUM_EDITOR_CAM) && (nEditor <= ENUM_EDITOR_UNSURE))
    return static_cast<teEditor>(nEditor);
  return ENUM_EDITOR_UNSET;
}

// Whether sOther is the same database entry as sEntry: software by its name
// and signature; anything else by its make, model, subsampling and signature.
// (Either of sOther's signatures can match: the user database keeps the
// lower of the two first.)
template <typename T>
bool sameEntry(const CompSig &sEntry, const T &sOther)
{
  if((sOther.strCSig != sEntry.strCSig) && (sOther.strCSigRot != sEntry.strCSig))
    return false;

  if(sEntry.eEditor == ENUM_EDITOR_SW)
    return (sOther.eEditor == ENUM_EDITOR_SW) && (sOther.strMSwDisp == sEntry.strMSwDisp);

  return (sOther.eEditor != ENUM_EDITOR_SW) && (sOther.strXMake == sEntry.strXMake) &&
         (sOther.strXModel == sEntry.strXModel) && (sOther.strXSubsamp == sEntry.strXSubsamp);
}

} // namespace

QString CDbSigs::DatabaseExtraPath() const
{
  // (The Qt port had no separator: "<folder>JPEGsnoop_db.dat", beside the
  // folder)
  return m_strDbDir.isEmpty() ? QString() : QDir(m_strDbDir).filePath(QStringLiteral(DAT_FILE));
}

void CDbSigs::DatabaseExtraLoad()
{
  // Nothing from the previous folder's file stays (on Windows, changing the
  // folder kept the old entries, and the next one added wrote them all over
  // the new folder's file)
  m_nSigListExtraNum = 0;
  m_strLoadError.clear();
  m_bKeepOldFile = false;

  const QString strPath = DatabaseExtraPath();
  if(strPath.isEmpty())
    return;

  // None yet: the first entry added makes it. (Windows wrote an empty one at
  // once, and on a later start said it was missing and wrote another.)
  QFile file(strPath);
  if(!file.exists())
    return;

  // From here on, a file that can't be read in full is kept: the next entry
  // added copies it to .bak before replacing it. (Windows copied it and wrote
  // an empty database at once.)
  m_bKeepOldFile = true;

  if(!file.open(QIODevice::ReadOnly))
  {
    m_strLoadError = QString("[%1] couldn't be opened: %2").arg(strPath, file.errorString());
    return;
  }

  // (Windows read 128 KB and asserted that the file was smaller)
  const QByteArray abData = file.read(kDbMaxBytes + 1);
  if(file.error() != QFileDevice::NoError)
  {
    m_strLoadError = QString("[%1] couldn't be read: %2").arg(strPath, file.errorString());
    return;
  }
  if(abData.size() > kDbMaxBytes)
  {
    m_strLoadError = QString("[%1] is too large to be a user database").arg(strPath);
    return;
  }
  file.close();

  // UTF-16LE (version 03) or single bytes (01 and 02)
  bool bUni = false;
  bool bHeader = false;
  for(const bool bTryUni : { true, false })
  {
    CDbReader reader(abData, bTryUni);
    QString strHeader;
    if(reader.str(strHeader) && (strHeader == QLatin1String("JPEGsnoop")))
    {
      bUni = bTryUni;
      bHeader = true;
      break;
    }
  }
  if(!bHeader)
  {
    m_strLoadError = QString("[%1] isn't a JPEGsnoop user database").arg(strPath);
    return;
  }

  CDbReader reader(abData, bUni);
  QString strVer;
  reader.str(strVer);           // "JPEGsnoop"
  if(!reader.str(strVer))
  {
    m_strLoadError = QString("[%1] is damaged: it ends after its first line").arg(strPath);
    return;
  }
  if((strVer != QLatin1String("01")) && (strVer != QLatin1String("02")) && (strVer != QLatin1String("03")))
  {
    m_strLoadError = QString("[%1] is version %2, which this version of JPEGsnoop can't read").arg(strPath, strVer);
    return;
  }

  uint32_t nListed = 0;         // entries the file says it has
  uint32_t nRead = 0;           // entries read in full
  bool bDamaged = false;
  bool bDone = false;

  while(!bDone && !bDamaged)
  {
    QString strSec;
    if(!reader.str(strSec))
    {
      bDamaged = true;
    }
    else if(strSec == QLatin1String("*Z*"))
    {
      bDone = true;
    }
    else if(strSec == QLatin1String("*DB*"))
    {
      uint32_t nNum = 0;
      if(!reader.num(nNum))
      {
        bDamaged = true;
        break;
      }
      nListed += nNum;

      for(uint32_t nInd = 0; (nInd < nNum) && !bDamaged; nInd++)
      {
        CompSig sEntry;
        QString strEditor;
        bool bOk = reader.str(sEntry.strXMake) && reader.str(sEntry.strXModel) && reader.str(strEditor) &&
                   reader.str(sEntry.strUmQual) && reader.str(sEntry.strCSig);
        if(bOk && (strVer != QLatin1String("01")))
          bOk = reader.str(sEntry.strCSigRot);
        bOk = bOk && reader.str(sEntry.strXSubsamp) && reader.str(sEntry.strMSwTrim) &&
              reader.str(sEntry.strMSwDisp);
        if(!bOk)
        {
          bDamaged = true;
          break;
        }
        nRead++;
        sEntry.eEditor = editorFromText(strEditor);
        sEntry.bValid = true;

        // One the built-in database now has isn't used, as on Windows (which
        // also wrote the file again without it)
        if(IsBuiltInEntry(sEntry))
          continue;

        // (Windows wrote past the end of the list)
        if(m_nSigListExtraNum < DBEX_ENTRIES_MAX)
          m_sSigListExtra[m_nSigListExtraNum++] = sEntry;
      }
    }
    else
    {
      bDamaged = true;
    }
  }

  if(bDamaged)
  {
    if(nRead < nListed)
      m_strLoadError = QString("[%1] is damaged: %2 of its %3 entries could be read").arg(strPath).arg(nRead).arg(nListed);
    else if(nRead == 0)
      m_strLoadError = QString("[%1] is damaged after its header").arg(strPath);
    else
      m_strLoadError = QString("[%1] is damaged after entry %2").arg(strPath).arg(nRead);
  }
  else if(nRead > DBEX_ENTRIES_MAX)
  {
    m_strLoadError =
      QString("[%1] has %2 entries: only the first %3 are used").arg(strPath).arg(nRead).arg(DBEX_ENTRIES_MAX);
  }
  else
  {
    // Read in full: an older version's file is still kept, as Windows kept
    // it when it wrote it again as version 03
    m_bKeepOldFile = (strVer != QLatin1String(DB_VER_STR));
  }
}

void CDbSigs::DatabaseExtraClean()
{
  m_nSigListExtraNum = 0;
  QString strError;
  DatabaseExtraStore(strError);
}

uint32_t CDbSigs::DatabaseExtraGetNum()
{
  return static_cast<uint32_t>(m_nSigListExtraNum);
}

CompSig CDbSigs::DatabaseExtraGet(uint32_t nInd)
{
  Q_ASSERT(nInd < static_cast<uint32_t>(m_nSigListExtraNum));
  return m_sSigListExtra[nInd];
}

// Write the user database (version 03), in full or not at all. (The Qt port
// wrote nothing: its string writer was commented out, and the buffer was
// written up to its first zero byte.)
bool CDbSigs::DatabaseExtraStore(QString &strError)
{
  const QString strPath = DatabaseExtraPath();
  if(strPath.isEmpty())
  {
    strError = QStringLiteral("No folder is set for the user database (Options > Configuration)");
    return false;
  }

  QByteArray abData;
  auto putStr = [&abData](const QString &str) {
    for(const QChar ch : str)
    {
      const char16_t nCh = ch.unicode();
      abData.append(static_cast<char>(nCh & 0xFF));
      abData.append(static_cast<char>(nCh >> 8));
    }
    abData.append(2, '\0');
  };

  putStr(QStringLiteral("JPEGsnoop"));
  putStr(QStringLiteral(DB_VER_STR));
  putStr(QStringLiteral("*DB*"));

  // Entries marked for removal (Manage Local DB) are left out
  quint32 nValid = 0;
  for(int32_t nInd = 0; nInd < m_nSigListExtraNum; nInd++)
  {
    if(m_sSigListExtra[nInd].bValid)
      nValid++;
  }
  char acNum[4];
  qToLittleEndian<quint32>(nValid, acNum);
  abData.append(acNum, sizeof(acNum));

  for(int32_t nInd = 0; nInd < m_nSigListExtraNum; nInd++)
  {
    const CompSig &sEntry = m_sSigListExtra[nInd];
    if(!sEntry.bValid)
      continue;
    putStr(sEntry.strXMake);
    putStr(sEntry.strXModel);
    putStr(QString::number(static_cast<uint32_t>(sEntry.eEditor)));
    putStr(sEntry.strUmQual);
    putStr(sEntry.strCSig);
    putStr(sEntry.strCSigRot);
    putStr(sEntry.strXSubsamp);
    putStr(sEntry.strMSwTrim);
    putStr(sEntry.strMSwDisp);
  }

  putStr(QStringLiteral("*Z*"));

  QSaveFile file(strPath);
  if(!file.open(QIODevice::WriteOnly) || (file.write(abData) != abData.size()) || !file.commit())
  {
    strError = QString("[%1] couldn't be written: %2").arg(strPath, file.errorString());
    return false;
  }
  return true;
}

CDbSigs::AddResult CDbSigs::DatabaseExtraAdd(QString strExifMake, QString strExifModel,
                                             QString strQual, QString strSig, QString strSigRot, QString strCss,
                                             teSource eUserSource, QString strUserSoftware, QString &strDetail)
{
  strDetail.clear();

  // The lower signature first, so the same tables make the same entry
  // whichever way the image is turned (as on Windows)
  if(strSigRot < strSig)
    std::swap(strSig, strSigRot);

  CompSig sEntry;
  // (Windows never set this, so whether the new entry was written depended
  // on what was in memory there before)
  sEntry.bValid = true;
  sEntry.strXMake = strExifMake;
  sEntry.strXModel = strExifModel;
  sEntry.strUmQual = strQual;
  sEntry.strCSig = strSig;
  sEntry.strCSigRot = strSigRot;
  sEntry.strXSubsamp = strCss;

  if(eUserSource == ENUM_SOURCE_CAM)
  {
    sEntry.eEditor = ENUM_EDITOR_CAM;
  }
  else if(eUserSource == ENUM_SOURCE_SW)
  {
    sEntry.eEditor = ENUM_EDITOR_SW;
    sEntry.strMSwDisp = strUserSoftware;
    sEntry.strXMake.clear();
    sEntry.strXModel.clear();
  }
  else
  {
    // The user didn't know
    sEntry.eEditor = ENUM_EDITOR_UNSURE;
    sEntry.strMSwDisp = strUserSoftware;
  }

  // A newline or NUL would end the field early when the file is read back,
  // and every field after it would be out of step
  for(QString *pStr : { &sEntry.strXMake, &sEntry.strXModel, &sEntry.strUmQual, &sEntry.strXSubsamp,
                        &sEntry.strMSwDisp })
  {
    pStr->replace(QChar(u'\n'), QChar(u' ')).replace(QChar(u'\0'), QChar(u' '));
  }

  // (Windows added it again, or added one the next start would drop as built
  // in, and said "Added")
  if(IsBuiltInEntry(sEntry) || IsUserEntry(sEntry))
    return AddResult::AlreadyIn;

  if(m_nSigListExtraNum >= DBEX_ENTRIES_MAX)
    return AddResult::Full;

  // Keep a file that couldn't be read in full, or an older version's
  const QString strPath = DatabaseExtraPath();
  QString strBak;
  if(m_bKeepOldFile && !strPath.isEmpty() && QFile::exists(strPath))
  {
    strBak = strPath + QStringLiteral(".bak");
    QFile fileOld(strPath);
    if((QFile::exists(strBak) && !QFile::remove(strBak)) || !fileOld.copy(strBak))
    {
      strDetail = QString("[%1] couldn't be kept as [%2], so it wasn't replaced: %3")
                    .arg(strPath, strBak, fileOld.errorString());
      return AddResult::SaveFailed;
    }
  }

  m_sSigListExtra[m_nSigListExtraNum++] = sEntry;

  QString strError;
  if(!DatabaseExtraStore(strError))
  {
    // Not kept: it would be gone at the next start
    m_sSigListExtra[--m_nSigListExtraNum] = CompSig();
    strDetail = strError;
    return AddResult::SaveFailed;
  }

  if(!strBak.isEmpty())
    strDetail = QString("The file it replaced was kept as [%1].").arg(strBak);
  m_bKeepOldFile = false;
  m_strLoadError.clear();
  return AddResult::Added;
}

// Whether the built-in database has this entry (see sameEntry()). Windows
// compared only the make, model and signature, so it also dropped a user's
// entry for the same camera with other subsampling, which the signature
// search doesn't count as a match; and it never matched software, whose make
// and model the user database leaves empty.
bool CDbSigs::IsBuiltInEntry(const CompSig &sEntry) const
{
  for(int32_t nInd = 0; nInd < m_nSigListNum; nInd++)
  {
    if(sameEntry(sEntry, m_sSigList[nInd]))
      return true;
  }
  return false;
}

bool CDbSigs::IsUserEntry(const CompSig &sEntry) const
{
  for(int32_t nInd = 0; nInd < m_nSigListExtraNum; nInd++)
  {
    if(m_sSigListExtra[nInd].bValid && sameEntry(sEntry, m_sSigListExtra[nInd]))
      return true;
  }
  return false;
}

bool CDbSigs::SearchCom(QString strCom)
{
	bool bFound = false;

  if (strCom.size() > 0)
  {
    for(int i = 0; i < m_sXComSwList.size(); i++)
    {
      // Does the comment mention this software? (master: strCom.Find(entry))
      if(strCom.contains(m_sXComSwList.at(i)))
      {
        bFound = true;
        break;
      }
    }
  }

	return bFound;
}

// Returns total of built-in plus local DB
int32_t CDbSigs::GetDBNumEntries()
{
	return (m_nSigListNum + m_nSigListExtraNum);
}

// Returns total of built-in plus local DB
uint32_t CDbSigs::IsDBEntryUser(uint32_t nInd)
{
  if (nInd < m_nSigListNum)
  {
		return false;
  }
  else
  {
		return true;
	}
}

// Return a ptr to the struct containing the indexed entry
bool CDbSigs::GetDBEntry(int32_t nInd,CompSig* pEntry)
{
  uint32_t nIndOffset;
  int32_t nIndMax = GetDBNumEntries();
  Q_ASSERT(pEntry);
  Q_ASSERT(nInd<nIndMax);

  if (nInd < m_nSigListNum)
  {
		pEntry->eEditor  = m_sSigList[nInd].eEditor;
		pEntry->strXMake    = m_sSigList[nInd].strXMake;
		pEntry->strXModel   = m_sSigList[nInd].strXModel;
		pEntry->strUmQual   = m_sSigList[nInd].strUmQual;
		pEntry->strCSig     = m_sSigList[nInd].strCSig;
		pEntry->strCSigRot  = m_sSigList[nInd].strCSigRot;
		pEntry->strXSubsamp = m_sSigList[nInd].strXSubsamp;
		pEntry->strMSwTrim  = m_sSigList[nInd].strMSwTrim;
		pEntry->strMSwDisp  = m_sSigList[nInd].strMSwDisp;
		return true;
  }
  else
  {
		nIndOffset = nInd-m_nSigListNum;
		pEntry->eEditor  = m_sSigListExtra[nIndOffset].eEditor;
		pEntry->strXMake    = m_sSigListExtra[nIndOffset].strXMake;
		pEntry->strXModel   = m_sSigListExtra[nIndOffset].strXModel;
		pEntry->strUmQual   = m_sSigListExtra[nIndOffset].strUmQual;
		pEntry->strCSig     = m_sSigListExtra[nIndOffset].strCSig;
		pEntry->strCSigRot  = m_sSigListExtra[nIndOffset].strCSigRot;
		pEntry->strXSubsamp = m_sSigListExtra[nIndOffset].strXSubsamp;
		pEntry->strMSwTrim  = m_sSigListExtra[nIndOffset].strMSwTrim;
		pEntry->strMSwDisp  = m_sSigListExtra[nIndOffset].strMSwDisp;
		return true;
	}
}

void CDbSigs::SetEntryValid(uint32_t nInd,bool bValid)
{
	// TODO: Bounds check
  Q_ASSERT(nInd < m_nSigListExtraNum);
	m_sSigListExtra[nInd].bValid = bValid;
}


uint32_t CDbSigs::GetIjgNum()
{
	return m_nSwIjgListNum;
}

QString CDbSigs::GetIjgEntry(uint32_t nInd)
{
	return m_sSwIjgList[nInd];
}

// Update the directory used for user database
void CDbSigs::SetDbDir(QString strDbDir)
{
	m_strDbDir = strDbDir;
}


// Search exceptions for Make/Model in list of ones that don't have Makernotes
bool CDbSigs::LookupExcMmNoMkr(QString strMake,QString strModel)
{
	bool bFound = false;
	bool bDone = false;
  uint32_t nInd = 0;

  if (strMake.size() == 0)
  {
		bDone = true;
	}

  while (!bDone)
  {
    if (nInd >= m_nExcMmNoMkrListNum)
    {
			bDone = true;
    }
    else
    {
			// Perform exact match on Make, case sensitive
			// Check Make field and possibly Model field (if non-empty)
      if (m_sExcMmNoMkrList[nInd].strXMake == strMake)
      {
        // Make matched, now check to see if we need to compare the Model string
        const QString &strXModel = m_sExcMmNoMkrList[nInd].strXModel;

        if (strXModel.isEmpty())
        {
					// No model in the entry: any model of this make matches
					bFound = true;
					bDone = true;
        }
        else
        {
          // Need to check model as well. Since we may like to do a substring match, support wildcards.
          // As in master (_tcsnccmp): compare the entry up to its "*" (or all
          // of it) with the same leading part of the model.
          const qsizetype nWildcard = strXModel.indexOf(QChar('*'));
          const qsizetype nCompareLen = (nWildcard != -1) ? nWildcard : strXModel.size();

          if (strXModel.left(nCompareLen) == strModel.left(nCompareLen))
          {
						// Matched as well, we're bDone
						bFound = true;
						bDone = true;
					}
				}
			}

			nInd++;
		}
	}

	return bFound;
}

// Search exceptions for Make/Model in list of ones that are always edited
bool CDbSigs::LookupExcMmIsEdit(QString strMake,QString strModel)
{
	bool bFound = false;
	bool bDone = false;
  uint32_t nInd = 0;

  if (strMake.size() == 0)
  {
		bDone = true;
	}

  while (!bDone)
  {
    if (nInd >= m_nExcMmIsEditListNum)
    {
			bDone = true;
    }
    else
    {
			// Perform exact match, case sensitive
			// Check Make field and possibly Model field (if non-empty)
      if (m_sExcMmIsEditList[nInd].strXMake == strMake)
      {
				// Make matched, now check to see if we need
				// to compare the Model string
        if (m_sExcMmIsEditList[nInd].strXModel.size() == 0)
        {
					// No need to compare, we're bDone
					bFound = true;
					bDone = true;
        }
        else
        {
					// Need to check model as well
          if (m_sExcMmIsEditList[nInd].strXModel == strModel)
          {
						// Matched as well, we're bDone
						bFound = true;
						bDone = true;
					}
				}
			}

			nInd++;
		}
	}
	
	return bFound;
}

// Software signature list (m_sSigList) is located in "Signatures.inl"

