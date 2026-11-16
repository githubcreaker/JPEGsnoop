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
// - File > Offset's dialog, as on Windows ("Starting File Offset"): the file
//   offset to decode from, typed in hex or decimal.
//
// ==========================================================================

#ifndef OFFSETDIALOG_H
#define OFFSETDIALOG_H

#include <QDialog>

class QLineEdit;
class QRadioButton;

class COffsetDialog : public QDialog
{
  Q_OBJECT

public:
  // nOffset is the offset shown at first, and nFileSize the size of the file
  // (an offset must be less)
  COffsetDialog(quint32 nOffset, quint64 nFileSize, QWidget *pParent = nullptr);

  // The offset OK accepted
  quint32 offset() const { return m_nOffset; }

  // The field's text as a number in hex (with or without 0x) or decimal.
  // False if it isn't one; true with bTooLarge if it is, but has more than
  // 64 bits.
  static bool parseOffset(const QString &strText, bool bHex, quint64 &nValue, bool &bTooLarge);

  // The number as the field shows it: 0x0000ABCD in hex, as on Windows
  static QString offsetText(quint64 nValue, bool bHex);

private:
  void baseClicked();
  QString notNumberError(const QString &strText) const;
  bool acceptFields();

  quint64 m_nFileSize;
  quint32 m_nOffset;
  bool m_bHex = true;             // the base the field's text is in

  QLineEdit *m_pValue;
  QRadioButton *m_pHex;
  QRadioButton *m_pDecimal;
};

#endif // OFFSETDIALOG_H
