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

#include <initializer_list>
#include <vector>

#include <QSaveFile>

#include "FileTiff.h"

namespace
{
  const quint16 TIFF_TYPE_SHORT = 3;
  const quint16 TIFF_TYPE_LONG = 4;
  const quint16 TIFF_TYPE_RATIONAL = 5;

  const quint16 TIFF_TAG_IMG_WIDTH = 0x0100;
  const quint16 TIFF_TAG_IMG_HEIGHT = 0x0101;
  const quint16 TIFF_TAG_BITS_PER_SAMP = 0x0102;
  const quint16 TIFF_TAG_COMPRESSION = 0x0103;
  const quint16 TIFF_TAG_PHOTOM_INTERP = 0x0106;
  const quint16 TIFF_TAG_STRIP_OFFSETS = 0x0111;
  const quint16 TIFF_TAG_ORIENTATION = 0x0112;
  const quint16 TIFF_TAG_SAMP_PER_PIX = 0x0115;
  const quint16 TIFF_TAG_ROWS_PER_STRIP = 0x0116;
  const quint16 TIFF_TAG_STRIP_BYTE_COUNTS = 0x0117;
  const quint16 TIFF_TAG_X_RESOLUTION = 0x011A;
  const quint16 TIFF_TAG_Y_RESOLUTION = 0x011B;
  const quint16 TIFF_TAG_PLANAR_CONFIG = 0x011C;
  const quint16 TIFF_TAG_RESOLUTION_UNIT = 0x0128;
  const quint16 TIFF_TAG_YCC_COEFFS = 0x0211;
  const quint16 TIFF_TAG_YCC_SUBSAMP = 0x0212;
  const quint16 TIFF_TAG_POSITIONING = 0x0213;
  const quint16 TIFF_TAG_REF_BLACK_WHITE = 0x0214;

  // Room left for the header, directory and tag values below 4 GB
  const quint64 MAX_DATA_SIZE = 0xFFFFFFFFULL - 0x1000;

  struct IfdEntry
  {
    quint16 nTag;
    quint16 nType;
    quint32 nCount;
    QByteArray abValue;         // Big-endian
  };

  void AppendBe16(QByteArray &abBuf, quint16 nVal)
  {
    abBuf.append(static_cast<char>(nVal >> 8));
    abBuf.append(static_cast<char>(nVal & 0xFF));
  }

  void AppendBe32(QByteArray &abBuf, quint32 nVal)
  {
    AppendBe16(abBuf, static_cast<quint16>(nVal >> 16));
    AppendBe16(abBuf, static_cast<quint16>(nVal & 0xFFFF));
  }

  IfdEntry Shorts(quint16 nTag, std::initializer_list<quint16> anVals)
  {
    IfdEntry sEntry = { nTag, TIFF_TYPE_SHORT, static_cast<quint32>(anVals.size()), QByteArray() };
    for(quint16 nVal : anVals)
      AppendBe16(sEntry.abValue, nVal);
    return sEntry;
  }

  IfdEntry Long(quint16 nTag, quint32 nVal)
  {
    IfdEntry sEntry = { nTag, TIFF_TYPE_LONG, 1, QByteArray() };
    AppendBe32(sEntry.abValue, nVal);
    return sEntry;
  }

  // Numerator, denominator pairs
  IfdEntry Rationals(quint16 nTag, std::initializer_list<quint32> anVals)
  {
    IfdEntry sEntry = { nTag, TIFF_TYPE_RATIONAL, static_cast<quint32>(anVals.size() / 2), QByteArray() };
    for(quint32 nVal : anVals)
      AppendBe32(sEntry.abValue, nVal);
    return sEntry;
  }
}

quint64 FileTiff::DataSize(uint32_t nSizeX, uint32_t nSizeY, teFormat eFormat)
{
  const quint64 nBytesPerPix = (eFormat == FMT_RGB16) ? 6 : 3;
  const quint64 nSize = static_cast<quint64>(nSizeX) * nSizeY * nBytesPerPix;

  return (nSize <= MAX_DATA_SIZE) ? nSize : 0;
}

bool FileTiff::WriteFile(const QString &strFile, teFormat eFormat, const QByteArray &abSamples,
                         uint32_t nSizeX, uint32_t nSizeY, QString &strError)
{
  const quint64 nDataSize = DataSize(nSizeX, nSizeY, eFormat);

  if((nDataSize == 0) || (static_cast<quint64>(abSamples.size()) != nDataSize))
  {
    strError = QString("An image of %1 x %2 pixels can't be written as a TIFF file").arg(nSizeX).arg(nSizeY);
    return false;
  }

  const bool bYcc = (eFormat == FMT_YCC8);
  const quint16 nBits = (eFormat == FMT_RGB16) ? 16 : 8;
  const quint32 nWhite = (eFormat == FMT_RGB16) ? 65535 : 255;

  // In ascending tag order, as TIFF requires
  std::vector<IfdEntry> asEntries;
  asEntries.push_back(Long(TIFF_TAG_IMG_WIDTH, nSizeX));
  asEntries.push_back(Long(TIFF_TAG_IMG_HEIGHT, nSizeY));
  asEntries.push_back(Shorts(TIFF_TAG_BITS_PER_SAMP, { nBits, nBits, nBits }));
  asEntries.push_back(Shorts(TIFF_TAG_COMPRESSION, { 1 }));                     // None
  asEntries.push_back(Shorts(TIFF_TAG_PHOTOM_INTERP, { static_cast<quint16>(bYcc ? 6 : 2) }));
  const size_t nStripOffsetsInd = asEntries.size();
  asEntries.push_back(Long(TIFF_TAG_STRIP_OFFSETS, 0));                         // Set below
  asEntries.push_back(Shorts(TIFF_TAG_ORIENTATION, { 1 }));                     // Top-left
  asEntries.push_back(Shorts(TIFF_TAG_SAMP_PER_PIX, { 3 }));
  asEntries.push_back(Long(TIFF_TAG_ROWS_PER_STRIP, nSizeY));
  asEntries.push_back(Long(TIFF_TAG_STRIP_BYTE_COUNTS, static_cast<quint32>(nDataSize)));
  asEntries.push_back(Rationals(TIFF_TAG_X_RESOLUTION, { 72, 1 }));             // 72 DPI
  asEntries.push_back(Rationals(TIFF_TAG_Y_RESOLUTION, { 72, 1 }));
  asEntries.push_back(Shorts(TIFF_TAG_PLANAR_CONFIG, { 1 }));                   // Chunky
  asEntries.push_back(Shorts(TIFF_TAG_RESOLUTION_UNIT, { 2 }));                 // Inches

  if(bYcc)
  {
    asEntries.push_back(Rationals(TIFF_TAG_YCC_COEFFS, { 299, 1000, 587, 1000, 114, 1000 }));
    asEntries.push_back(Shorts(TIFF_TAG_YCC_SUBSAMP, { 1, 1 }));
    asEntries.push_back(Shorts(TIFF_TAG_POSITIONING, { 1 }));                  // Centred
    asEntries.push_back(Rationals(TIFF_TAG_REF_BLACK_WHITE, { 0, 1, 255, 1, 128, 1, 255, 1, 128, 1, 255, 1 }));
  }
  else
  {
    asEntries.push_back(Rationals(TIFF_TAG_REF_BLACK_WHITE,
                                  { 0, 1, nWhite, 1, 0, 1, nWhite, 1, 0, 1, nWhite, 1 }));
  }

  // Header (8 bytes), then the directory, then the values that don't fit
  // in an entry, then the pixels. Every offset is even.
  const quint32 nIfdSize = static_cast<quint32>(2 + 12 * asEntries.size() + 4);
  const quint32 nExtraStart = 8 + nIfdSize;

  QByteArray abExtra;
  std::vector<quint32> anValueOffset(asEntries.size(), 0);

  for(size_t nInd = 0; nInd < asEntries.size(); nInd++)
  {
    if(asEntries[nInd].abValue.size() > 4)
    {
      anValueOffset[nInd] = nExtraStart + static_cast<quint32>(abExtra.size());
      abExtra.append(asEntries[nInd].abValue);
      if(abExtra.size() % 2)
        abExtra.append('\0');
    }
  }

  const quint32 nDataStart = nExtraStart + static_cast<quint32>(abExtra.size());
  asEntries[nStripOffsetsInd].abValue.clear();
  AppendBe32(asEntries[nStripOffsetsInd].abValue, nDataStart);

  QByteArray abHead("MM", 2);
  AppendBe16(abHead, 42);
  AppendBe32(abHead, 8);                                  // The directory follows
  AppendBe16(abHead, static_cast<quint16>(asEntries.size()));

  for(size_t nInd = 0; nInd < asEntries.size(); nInd++)
  {
    const IfdEntry &sEntry = asEntries[nInd];
    AppendBe16(abHead, sEntry.nTag);
    AppendBe16(abHead, sEntry.nType);
    AppendBe32(abHead, sEntry.nCount);
    if(sEntry.abValue.size() > 4)
    {
      AppendBe32(abHead, anValueOffset[nInd]);
    }
    else
    {
      // Values that fit are left-justified in the entry
      abHead.append(sEntry.abValue);
      abHead.append(QByteArray(4 - sEntry.abValue.size(), '\0'));
    }
  }

  AppendBe32(abHead, 0);                                  // No next directory
  abHead.append(abExtra);
  Q_ASSERT(static_cast<quint32>(abHead.size()) == nDataStart);

  // Written to a temporary file that replaces strFile once complete
  QSaveFile fileOut(strFile);

  if(!fileOut.open(QIODevice::WriteOnly))
  {
    strError = QString("Couldn't open file for write [%1]: [%2]").arg(strFile).arg(fileOut.errorString());
    return false;
  }

  if((fileOut.write(abHead) != abHead.size()) || (fileOut.write(abSamples) != abSamples.size()) || !fileOut.commit())
  {
    strError = QString("Couldn't write file [%1]: [%2]").arg(strFile).arg(fileOut.errorString());
    return false;
  }

  return true;
}
