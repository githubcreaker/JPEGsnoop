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
// - Tools > Add Camera/SW to DB's dialog, as on Windows ("Submit Compression
//   Signature to Database"): the file's compression signature and EXIF
//   fields, and what the user says made the file. The entry goes to the user
//   database only: nothing is sent over the network.
//
// ==========================================================================

#ifndef DBSUBMITDIALOG_H
#define DBSUBMITDIALOG_H

#include <QDialog>

#include "snoop.h"

class QLineEdit;
class QRadioButton;

class CDbSubmitDialog : public QDialog
{
  Q_OBJECT

public:
  // eSource is the source chosen at first (the report's suggestion);
  // strDbPath is the user database file, which the dialog names
  CDbSubmitDialog(const QString &strMake, const QString &strModel, const QString &strSoftware, const QString &strSig,
                  const QString &strQual, teSource eSource, const QString &strDbPath, QWidget *pParent = nullptr);

  // As typed, without spaces around them
  QString quality() const;
  QString software() const;
  teSource source() const;

public slots:
  void accept() override;

private:
  QLineEdit *m_pQual;
  QLineEdit *m_pUserSoftware;
  QRadioButton *m_pCam;
  QRadioButton *m_pSw;
  QRadioButton *m_pUnsure;
};

#endif // DBSUBMITDIALOG_H
