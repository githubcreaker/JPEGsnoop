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
// - Tools > File Overlay's dialog, as on Windows ("File Overlay Test"): a
//   run of bytes at a file offset to decode in place of the file's own, to
//   try out a change without editing the file. It shows the file's bytes at
//   the offset in hex and binary.
//
// ==========================================================================

#ifndef OVERLAYDIALOG_H
#define OVERLAYDIALOG_H

#include <functional>

#include <QByteArray>
#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;

class COverlayDialog : public QDialog
{
  Q_OBJECT

public:
  // Reads up to nLen bytes of the file as it is on disk from nOffset (fewer
  // at its end)
  using ReadFile = std::function<QByteArray(quint32 nOffset, int nLen)>;

  // The overlay is at most this long: CwindowBuf's MAX_OVERLAY - 1
  static const int kMaxBytes = 499;

  COverlayDialog(const ReadFile &fnRead, quint64 nFileSize, QWidget *pParent = nullptr);

  // Fills in the fields from the overlay in place (or none)
  void setOverlay(bool bEnabled, quint32 nOffset, const QByteArray &abData);

  // What OK or Apply accepted: whether the overlay is on, and its offset and
  // bytes (Len of them)
  bool overlayEnabled() const { return m_bEnabled; }
  quint32 offset() const { return m_nOffset; }
  QByteArray data() const { return m_abData; }

  // Hex digits (spaces and a leading 0x allowed) as bytes; false if they
  // aren't whole bytes
  static bool parseHexBytes(const QString &strText, QByteArray &abBytes);

signals:
  // Apply accepted the fields: install the overlay and reprocess. The dialog
  // stays open.
  void applied();

private:
  bool acceptFields();
  void showOriginal();

  ReadFile m_fnRead;
  quint64 m_nFileSize;

  QCheckBox *m_pEnabled;
  QLineEdit *m_pOffset;
  QLabel *m_pOrigHex;
  QLabel *m_pOrigBin;
  QLineEdit *m_pValue;
  QLineEdit *m_pLen;

  bool m_bEnabled = false;
  quint32 m_nOffset = 0;
  QByteArray m_abData;
};

#endif // OVERLAYDIALOG_H
