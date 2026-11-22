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

#ifndef FILEFACTS_H
#define FILEFACTS_H

#include <QString>
#include <QStringList>

#include <functional>

class QFile;

// The report's lines about the file itself, after its name and size:
//
//   Created: [2026-11-06T09:54:46.123Z]     in UTC (ISO 8601 and RFC 3339)
//   Modified: [2026-11-06T09:54:46.123Z]
//   SHA-512: [...]                          in lower case, as shasum prints
//   SHA-256: [...]
//   SHA-1: [...]
//   MD5: [...]
//   CRC32: [...]                            as zip's and crc32's
//
// The hashes are of the file as it is on the disk, without a File Overlay.
// They are kept until the file changes, so Reprocess, Image Search and Extract
// All don't read all of a large one again.
//
// A file of 4 GiB (2^32 bytes) or more has none while Options > Skip Hashes on
// 4 GiB or Larger Files is on, as it is at first: at some 2.5 seconds a GiB
// that can take minutes, and such a file isn't analyzed.
class CFileFacts
{
public:
  static constexpr qint64 kLargeFile = Q_INT64_C(0x100000000);

  // file is open for reading; its position is moved. fnProgress is given the
  // percent read while more is left (a file of over 4 MB), and returns false
  // to cancel.
  QStringList lines(QFile &file, bool bSkipLarge, const std::function<bool(int)> &fnProgress = nullptr);

  // How often a file has been hashed (for the test driver)
  int timesHashed() const { return m_nTimesHashed; }

private:
  QString m_strKey;             // the file m_astrHashes is of, "" if none is kept
  QStringList m_astrHashes;
  int m_nTimesHashed = 0;
};

#endif // FILEFACTS_H
