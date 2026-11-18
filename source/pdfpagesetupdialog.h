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
// - File > Print Setup's dialog when printing makes PDF files (no printer is
//   set up, or Save as PDF was chosen in the print panel). Qt's macOS page
//   setup dialog, the system's page layout panel, does nothing for such a
//   printer. This one sets its paper size and orientation, as that panel
//   does; the printer's other settings (its margins, the file it writes)
//   stay as they are.
//
// ==========================================================================

#ifndef PDFPAGESETUPDIALOG_H
#define PDFPAGESETUPDIALOG_H

#include <QDialog>

class QComboBox;
class QPrinter;
class QRadioButton;

class CPdfPageSetupDialog : public QDialog
{
  Q_OBJECT

public:
  // Starts from the printer's paper size and orientation; OK sets them
  explicit CPdfPageSetupDialog(QPrinter &printer, QWidget *pParent = nullptr);

public slots:
  void accept() override;

private:
  QPrinter &m_printer;
  QComboBox *m_pPaper;
  QRadioButton *m_pPortrait;
  QRadioButton *m_pLandscape;
};

#endif // PDFPAGESETUPDIALOG_H
