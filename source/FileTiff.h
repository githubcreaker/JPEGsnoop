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
// - TIFF file export (Tools > Export TIFF)
//
// Writes an uncompressed, big-endian ("MM") baseline TIFF with one strip and
// three samples a pixel: 8-bit RGB, 16-bit RGB or 8-bit YCbCr. The tags are
// those of master's FileTiff, with these corrections:
// - ReferenceBlackWhite gives 128 as the black of Cb and Cr (the samples are
//   centred on 128), and 65535 as the white of 16-bit RGB. Master wrote 0 and
//   255 for every sample, so a reader that applies the tag shifted the
//   colours of a YCbCr file.
// - Width, height and the strip's offset and rows are LONGs, so sizes above
//   65535 aren't cut short.
// - Writes go through QSaveFile and are checked, so a failure leaves no
//   partial file.
// ==========================================================================

#ifndef FILETIFF_H
#define FILETIFF_H

#include <QByteArray>
#include <QString>

class FileTiff
{
public:
  enum teFormat
  {
    FMT_RGB8,                   // RGB, 8 bits a sample
    FMT_RGB16,                  // RGB, 16 bits a sample
    FMT_YCC8                    // YCbCr, 8 bits a sample, no subsampling
  };

  // Bytes of pixel data for an image of this size and format, or 0 if it
  // can't be written as a TIFF (offsets are 32-bit, so it must stay under
  // 4 GB with the header)
  static quint64 DataSize(uint32_t nSizeX, uint32_t nSizeY, teFormat eFormat);

  // Write the file. abSamples holds the pixels in file order: rows top to
  // bottom, three samples a pixel, 16-bit samples big-endian. On failure,
  // returns false with the reason in strError.
  static bool WriteFile(const QString &strFile, teFormat eFormat, const QByteArray &abSamples,
                        uint32_t nSizeX, uint32_t nSizeY, QString &strError);
};

#endif // FILETIFF_H
