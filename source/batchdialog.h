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
// - File > Batch Process's dialog, as on Windows ("Batch Processing"): the
//   folder of images to process, the folder for their logs, whether to
//   process subfolders too, and whether to extract every JPEG.
//
// ==========================================================================

#ifndef BATCHDIALOG_H
#define BATCHDIALOG_H

#include <QDialog>

#include "batchprocess.h"

class QCheckBox;
class QLineEdit;

class CBatchDialog : public QDialog
{
  Q_OBJECT

public:
  // The folders shown at first (the last ones used)
  CBatchDialog(const QString &strInput, const QString &strOutput, QWidget *pParent = nullptr);

  // The settings accepted (after OK)
  BatchSettings settings() const { return m_sSettings; }

private:
  CBatchDialog &operator = (const CBatchDialog&);
  CBatchDialog(CBatchDialog&);

  void browse(bool bInput);
  bool acceptFields();

  QLineEdit *m_pInput;
  QLineEdit *m_pOutput;
  QCheckBox *m_pSubfolders;
  QCheckBox *m_pExtractAll;
  BatchSettings m_sSettings;
};

#endif // BATCHDIALOG_H
