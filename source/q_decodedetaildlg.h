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
// - Dialog box for Options > Scan Segment > Detailed Decode: the MCUs whose
//   scan decode is reported in detail
//
// ==========================================================================

#ifndef Q_DECODEDETAILDLG_H
#define Q_DECODEDETAILDLG_H

#include <QDialog>
#include <cstdint>

namespace Ui {
class Q_DecodeDetailDlg;
}

class QLineEdit;

class Q_DecodeDetailDlg : public QDialog
{
  Q_OBJECT

public:
  Q_DecodeDetailDlg(bool bEn, uint32_t nMcuX, uint32_t nMcuY, uint32_t nMcuLen, QWidget *parent = nullptr);
  ~Q_DecodeDetailDlg() override;

  // The range Load Coords fills in, from the positions last marked on the
  // image. Until this is called there is nothing to load.
  void setLoadCoords(uint32_t nMcuX, uint32_t nMcuY, uint32_t nMcuLen);

  // A note shown below the settings (such as why nothing will be reported)
  void setNote(const QString &strNote);

  // The settings chosen (after OK)
  bool detailEnabled() const { return m_bEn; }
  uint32_t mcuX() const { return m_nMcuX; }
  uint32_t mcuY() const { return m_nMcuY; }
  uint32_t mcuLen() const { return m_nMcuLen; }

public slots:
  void accept() override;

private slots:
  void onLoadCoords();

private:
  bool readField(QLineEdit *pEdit, const QString &strName, uint32_t &nVal);

  Ui::Q_DecodeDetailDlg *ui;

  bool m_bEn;
  uint32_t m_nMcuX;
  uint32_t m_nMcuY;
  uint32_t m_nMcuLen;

  uint32_t m_nLoadMcuX;
  uint32_t m_nLoadMcuY;
  uint32_t m_nLoadMcuLen;
};

#endif // Q_DECODEDETAILDLG_H
