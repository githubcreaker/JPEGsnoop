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

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>

#include <array>
#include <future>
#include <limits>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

#include "General.h"
#include "filefacts.h"

namespace
{

constexpr qint64 kNoTime = std::numeric_limits<qint64>::min();
constexpr qint64 kChunk = 4 << 20;
constexpr qint64 kParallelMin = 1 << 20;        // less isn't worth the threads
// A change this soon before hashing might not show in the file's times, which
// some disks keep to a second or two: such hashes aren't kept (as git does).
constexpr qint64 kSettledMs = 3000;

//-----------------------------------------------------------------------------
// The file's times and identity, from the handle that is read

struct FileStamp
{
  QString strId;
  qint64 nSize = -1;
  qint64 nBornMs = kNoTime;
  qint64 nModifiedMs = kNoTime;
  qint64 nChangedMs = kNoTime;  // also for its name, permissions and attributes
};

#ifdef Q_OS_UNIX
// Cut short to the millisecond, not rounded into the next second or day
qint64 toMs(const struct timespec &ts)
{
  return static_cast<qint64>(ts.tv_sec) * 1000 + static_cast<qint64>(ts.tv_nsec / 1000000);
}
#endif

#ifndef Q_OS_MACOS
qint64 toMs(const QDateTime &dt)
{
  return dt.isValid() ? dt.toMSecsSinceEpoch() : kNoTime;
}
#endif

FileStamp stampOf(QFile &file)
{
  FileStamp sStamp;
#ifdef Q_OS_UNIX
  struct stat st;
  if(fstat(file.handle(), &st) != 0)
    return sStamp;
  sStamp.strId = QString("%1:%2").arg(static_cast<qulonglong>(st.st_dev)).arg(static_cast<qulonglong>(st.st_ino));
  sStamp.nSize = st.st_size;
#ifdef Q_OS_MACOS
  // 0 where the disk doesn't keep it
  if((st.st_birthtimespec.tv_sec != 0) || (st.st_birthtimespec.tv_nsec != 0))
    sStamp.nBornMs = toMs(st.st_birthtimespec);
  sStamp.nModifiedMs = toMs(st.st_mtimespec);
  sStamp.nChangedMs = toMs(st.st_ctimespec);
#else
  sStamp.nBornMs = toMs(QFileInfo(file).birthTime());
  sStamp.nModifiedMs = toMs(st.st_mtim);
  sStamp.nChangedMs = toMs(st.st_ctim);
#endif
#else
  QFileInfo info(file);
  info.refresh();
  sStamp.strId = info.absoluteFilePath();
  sStamp.nSize = info.size();
  sStamp.nBornMs = toMs(info.birthTime());
  sStamp.nModifiedMs = toMs(info.lastModified());
  sStamp.nChangedMs = toMs(info.metadataChangeTime());
#endif
  return sStamp;
}

QString utcTime(qint64 nMs)
{
  if(nMs == kNoTime)
    return QStringLiteral("not available");
  return QDateTime::fromMSecsSinceEpoch(nMs).toUTC().toString(Qt::ISODateWithMs);
}

//-----------------------------------------------------------------------------
// CRC-32 as in zip, gzip and PNG (polynomial 0xEDB88320), eight bytes at a
// time: Qt has none. At 1.75 GB/s on an M2 it doesn't hold up MD5's 0.68.

using CrcTables = std::array<std::array<quint32, 256>, 8>;

CrcTables makeCrcTables()
{
  CrcTables aTables{};
  for(quint32 nByte = 0; nByte < 256; nByte++)
  {
    quint32 nCrc = nByte;
    for(int nBit = 0; nBit < 8; nBit++)
      nCrc = (nCrc & 1u) ? (0xEDB88320u ^ (nCrc >> 1)) : (nCrc >> 1);
    aTables[0][nByte] = nCrc;
  }
  for(size_t nTable = 1; nTable < aTables.size(); nTable++)
  {
    for(size_t nByte = 0; nByte < 256; nByte++)
    {
      const quint32 nPrev = aTables[nTable - 1][nByte];
      aTables[nTable][nByte] = (nPrev >> 8) ^ aTables[0][nPrev & 0xFFu];
    }
  }
  return aTables;
}

quint32 crc32Update(quint32 nCrc, const char *pData, qint64 nLen)
{
  static const CrcTables aT = makeCrcTables();
  const uchar *p = reinterpret_cast<const uchar *>(pData);

  nCrc = ~nCrc;
  for(; nLen >= 8; nLen -= 8, p += 8)
  {
    nCrc ^= static_cast<quint32>(p[0]) | (static_cast<quint32>(p[1]) << 8) | (static_cast<quint32>(p[2]) << 16)
            | (static_cast<quint32>(p[3]) << 24);
    nCrc = aT[7][nCrc & 0xFFu] ^ aT[6][(nCrc >> 8) & 0xFFu] ^ aT[5][(nCrc >> 16) & 0xFFu] ^ aT[4][nCrc >> 24]
           ^ aT[3][p[4]] ^ aT[2][p[5]] ^ aT[1][p[6]] ^ aT[0][p[7]];
  }
  for(; nLen > 0; nLen--, p++)
    nCrc = aT[0][(nCrc ^ *p) & 0xFFu] ^ (nCrc >> 8);
  return ~nCrc;
}

//-----------------------------------------------------------------------------
// All five, each on a thread of its own for a large chunk: at once they go at
// MD5's speed, about twice as fast as in turn

class CHasher
{
public:
  void add(const char *pData, qint64 nLen)
  {
    const QByteArray abData = QByteArray::fromRawData(pData, static_cast<int>(nLen));
    if(nLen < kParallelMin)
    {
      for(QCryptographicHash *pHash : { &m_sha512, &m_sha256, &m_sha1, &m_md5 })
        pHash->addData(abData);
      m_nCrc = crc32Update(m_nCrc, pData, nLen);
      return;
    }

    // (The default policy: with no thread to be had, it's done when waited for)
    auto run512 = std::async([&]() { m_sha512.addData(abData); });
    auto run256 = std::async([&]() { m_sha256.addData(abData); });
    auto run1 = std::async([&]() { m_sha1.addData(abData); });
    auto runMd5 = std::async([&]() { m_md5.addData(abData); });
    m_nCrc = crc32Update(m_nCrc, pData, nLen);
    run512.get();
    run256.get();
    run1.get();
    runMd5.get();
  }

  QStringList lines() const
  {
    const auto fnHex = [](const QCryptographicHash &hash) { return QString::fromLatin1(hash.result().toHex()); };
    return { QString("  SHA-512: [%1]").arg(fnHex(m_sha512)),
             QString("  SHA-256: [%1]").arg(fnHex(m_sha256)),
             QString("  SHA-1: [%1]").arg(fnHex(m_sha1)),
             QString("  MD5: [%1]").arg(fnHex(m_md5)),
             QString("  CRC32: [%1]").arg(m_nCrc, 8, 16, QChar('0')) };
  }

private:
  QCryptographicHash m_sha512{ QCryptographicHash::Sha512 };
  QCryptographicHash m_sha256{ QCryptographicHash::Sha256 };
  QCryptographicHash m_sha1{ QCryptographicHash::Sha1 };
  QCryptographicHash m_md5{ QCryptographicHash::Md5 };
  quint32 m_nCrc = 0;
};

// The five lines with why there's no hash in each, so a report always has them
QStringList noHashes(const QString &strWhy)
{
  QStringList astrLines;
  for(const char *pszName : { "SHA-512", "SHA-256", "SHA-1", "MD5", "CRC32" })
    astrLines << QString("  %1: [not calculated: %2]").arg(QLatin1String(pszName), strWhy);
  return astrLines;
}

} // namespace

//-----------------------------------------------------------------------------
// See filefacts.h

QStringList CFileFacts::lines(QFile &file, bool bSkipLarge, const std::function<bool(int)> &fnProgress)
{
  const FileStamp sBefore = stampOf(file);
  QStringList astrLines = { QString("  Created: [%1]").arg(utcTime(sBefore.nBornMs)),
                            QString("  Modified: [%1]").arg(utcTime(sBefore.nModifiedMs)) };

  if(sBefore.nSize < 0)
    return astrLines + noHashes("couldn't read the file");
  if(bSkipLarge && (sBefore.nSize >= kLargeFile))
    return astrLines + noHashes("the file is 4 GiB or larger (see Options)");

  // The change time can't be set back, so a write that restores the modified
  // time still shows
  const QString strKey = QString("%1:%2:%3:%4")
                           .arg(sBefore.strId)
                           .arg(sBefore.nSize)
                           .arg(sBefore.nModifiedMs)
                           .arg(sBefore.nChangedMs);
  if(!m_strKey.isEmpty() && (strKey == m_strKey))
    return astrLines + m_astrHashes;

  m_strKey.clear();
  m_nTimesHashed++;
  const qint64 nBeganMs = QDateTime::currentMSecsSinceEpoch();

  CHasher hasher;
  QByteArray abBuf(static_cast<int>(qMin(sBefore.nSize, kChunk)), Qt::Uninitialized);
  QElapsedTimer tmEvents;
  tmEvents.start();
  qint64 nDone = 0;
  bool bRead = file.seek(0);
  while(bRead && (nDone < sBefore.nSize))
  {
    const qint64 nGot = file.read(abBuf.data(), qMin(sBefore.nSize - nDone, kChunk));
    if(nGot <= 0)
    {
      bRead = false;
      break;
    }
    hasher.add(abBuf.constData(), nGot);
    nDone += nGot;
    if(nDone < sBefore.nSize)
    {
      if(fnProgress && !fnProgress(static_cast<int>(nDone * 100 / sBefore.nSize)))
        return astrLines + noHashes("canceled");
      if(tmEvents.elapsed() >= 100)
      {
        ProcessWorkEvents();
        tmEvents.start();
      }
    }
  }

  const FileStamp sAfter = stampOf(file);
  // Not by the change time: opening from Finder notes the last use in the
  // file's attributes, which changes that
  if((sAfter.nSize >= 0) && ((sAfter.nSize != sBefore.nSize) || (sAfter.nModifiedMs != sBefore.nModifiedMs)))
    return astrLines + noHashes("the file changed while it was read");
  if(!bRead)
    return astrLines + noHashes("couldn't read all of the file");

  m_astrHashes = hasher.lines();
  const qint64 nLastMs = (sBefore.nChangedMs != kNoTime) ? sBefore.nChangedMs : sBefore.nModifiedMs;
  if((nLastMs != kNoTime) && (nBeganMs - nLastMs > kSettledMs))
    m_strKey = strKey;
  return astrLines + m_astrHashes;
}
