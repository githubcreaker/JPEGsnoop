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
// - Tools > Lookup MCU Offset's dialog, as on Windows ("Lookup File Offset of
//   MCU"): a pixel of the decoded image, and where the scan data of the MCU
//   holding it starts in the file.
//
// ==========================================================================

#ifndef LOOKUPDIALOG_H
#define LOOKUPDIALOG_H

#include <QDialog>
#include <QPoint>

#include <functional>

class QLabel;
class QLineEdit;

class CLookupDialog : public QDialog
{
  Q_OBJECT

public:
  // The MCU holding a pixel, and where its scan data starts in the file:
  // false if the decode didn't reach that MCU
  using LookupFn = std::function<bool(QPoint ptPix, QPoint &ptMcu, uint32_t &nByte, uint32_t &nBit)>;

  // nSizeX and nSizeY are the decoded image's size (padded to whole MCUs, as
  // the preview is), which the pixel must be in
  CLookupDialog(uint32_t nSizeX, uint32_t nSizeY, LookupFn fnLookup, QWidget *pParent = nullptr);

private:
  void recalc();
  void clearResult();
  bool readField(QLineEdit *pField, const QString &strAxis, uint32_t nSize, uint32_t &nValue);

  uint32_t m_nSizeX;
  uint32_t m_nSizeY;
  LookupFn m_fnLookup;

  QLineEdit *m_pX;
  QLineEdit *m_pY;
  QLabel *m_pMcu;
  QLabel *m_pOffset;
};

#endif // LOOKUPDIALOG_H
