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
// - The settings dialogs of Tools > Export JPEG and Tools > Export TIFF
//   (master's CExportDlg and CExportTiffDlg)
// ==========================================================================

#ifndef EXPORTDIALOGS_H
#define EXPORTDIALOGS_H

#include <QDialog>

#include "FileTiff.h"

class QCheckBox;
class QRadioButton;

// Tools > Export JPEG options
struct ExportJpegSettings
{
  bool bOverlayEn = false;      // Copy with the file overlays applied
  bool bExtractAll = false;     // Export every JPEG in the file
  bool bDhtAviInsert = false;   // Insert the standard Motion JPEG DHT before SOS
  bool bForceSoi = false;       // Missing SOI: add one
  bool bForceEoi = false;       // Missing EOI: add one
  bool bIgnoreEoi = false;      // Missing EOI: export to the end of the file
};

class ExportJpegDialog : public QDialog
{
  Q_OBJECT

public:
  ExportJpegDialog(uint32_t nStartOffset, const ExportJpegSettings &sInit, QWidget *pParent = nullptr);

  ExportJpegSettings settings() const;

private:
  ExportJpegDialog(const ExportJpegDialog &);
  ExportJpegDialog &operator = (const ExportJpegDialog &);

  QCheckBox *m_pOverlay;
  QCheckBox *m_pExtractAll;
  QCheckBox *m_pDhtAvi;
  QCheckBox *m_pForceSoi;
  QCheckBox *m_pForceEoi;
  QCheckBox *m_pIgnoreEoi;
};

class ExportTiffDialog : public QDialog
{
  Q_OBJECT

public:
  // bYccAvailable: a JPEG scan was decoded (a Photoshop image has only RGB)
  ExportTiffDialog(const QString &strFile, bool bYccAvailable, QWidget *pParent = nullptr);

  FileTiff::teFormat format() const;

private:
  ExportTiffDialog(const ExportTiffDialog &);
  ExportTiffDialog &operator = (const ExportTiffDialog &);

  QRadioButton *m_pRgb8;
  QRadioButton *m_pRgb16;
  QRadioButton *m_pYcc8;
};

#endif // EXPORTDIALOGS_H
