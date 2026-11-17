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

#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRadioButton>
#include <QVBoxLayout>

#include "dbsubmitdialog.h"

// The Windows dialog's fields and wording, apart from its Notes field and its
// note: both were for the web submission, which this version doesn't make.
// (The note said that only the signature was sent; the make, model, software,
// comment and notes were sent too.) Its widgets have object names for the
// test driver.
CDbSubmitDialog::CDbSubmitDialog(const QString &strMake, const QString &strModel, const QString &strSoftware,
                                 const QString &strSig, const QString &strQual, teSource eSource,
                                 const QString &strDbPath, QWidget *pParent) :
  QDialog(pParent),
  m_pQual(new QLineEdit(strQual)),
  m_pUserSoftware(new QLineEdit(strSoftware)),
  m_pCam(new QRadioButton(tr("Direct from Camera"))),
  m_pSw(new QRadioButton(tr("Edited / Processed by Software"))),
  m_pUnsure(new QRadioButton(tr("I really don't know where it came from")))
{
  setObjectName(QStringLiteral("addDbDialog"));
  setWindowTitle(tr("Add Compression Signature to Database"));

  // What the file says, which can be selected and copied but not changed
  auto shown = [](const QString &strText, const QString &strName) {
    QLineEdit *pEdit = new QLineEdit(strText);
    pEdit->setReadOnly(true);
    pEdit->setObjectName(strName);
    return pEdit;
  };
  QLineEdit *pSig = shown(strSig, QStringLiteral("signature"));
  pSig->setMinimumWidth(pSig->fontMetrics().horizontalAdvance(strSig + QStringLiteral("00")));

  m_pQual->setObjectName(QStringLiteral("quality"));
  QLabel *pQualHint = new QLabel(tr("e.g. superfine, normal, 70, 10, high, etc. For software, the save quality "
                                    "setting."));
  pQualHint->setWordWrap(true);

  QFormLayout *pFields = new QFormLayout;
  pFields->addRow(tr("EXIF Make:"), shown(strMake, QStringLiteral("exifMake")));
  pFields->addRow(tr("EXIF Model:"), shown(strModel, QStringLiteral("exifModel")));
  pFields->addRow(tr("EXIF Software:"), shown(strSoftware, QStringLiteral("exifSoftware")));
  pFields->addRow(tr("Signature:"), pSig);
  pFields->addRow(tr("Camera / Software Quality Setting:"), m_pQual);
  pFields->addRow(QString(), pQualHint);

  m_pCam->setObjectName(QStringLiteral("sourceCam"));
  m_pSw->setObjectName(QStringLiteral("sourceSw"));
  m_pUnsure->setObjectName(QStringLiteral("sourceUnsure"));
  m_pUserSoftware->setObjectName(QStringLiteral("userSoftware"));

  QHBoxLayout *pSoftwareRow = new QHBoxLayout;
  pSoftwareRow->addSpacing(m_pSw->sizeHint().height());
  pSoftwareRow->addWidget(new QLabel(tr("Software:")));
  pSoftwareRow->addWidget(m_pUserSoftware);

  QGroupBox *pSource = new QGroupBox(tr("This file is:"));
  QVBoxLayout *pSourceLayout = new QVBoxLayout(pSource);
  pSourceLayout->addWidget(m_pCam);
  pSourceLayout->addWidget(m_pSw);
  pSourceLayout->addLayout(pSoftwareRow);
  pSourceLayout->addWidget(m_pUnsure);

  if(eSource == ENUM_SOURCE_CAM)
    m_pCam->setChecked(true);
  else if(eSource == ENUM_SOURCE_SW)
    m_pSw->setChecked(true);
  else
    m_pUnsure->setChecked(true);

  QLabel *pNote = new QLabel(tr("OK adds this compression signature (not the photo) to your user database, with the "
                                "camera's make and model or the software's name, and the quality setting. JPEGsnoop "
                                "uses it when it searches for signatures. Nothing is sent over the network.\n\n"
                                "User database: %1")
                               .arg(QDir::toNativeSeparators(strDbPath)));
  pNote->setObjectName(QStringLiteral("dbNote"));
  pNote->setWordWrap(true);
  pNote->setTextInteractionFlags(Qt::TextSelectableByMouse);

  QDialogButtonBox *pButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pButtons->setObjectName(QStringLiteral("buttonBox"));
  connect(pButtons, &QDialogButtonBox::accepted, this, &CDbSubmitDialog::accept);
  connect(pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  QVBoxLayout *pLayout = new QVBoxLayout(this);
  pLayout->addLayout(pFields);
  pLayout->addWidget(pSource);
  pLayout->addWidget(pNote);
  pLayout->addWidget(pButtons);

  m_pQual->setFocus();
}

QString CDbSubmitDialog::quality() const
{
  return m_pQual->text().trimmed();
}

QString CDbSubmitDialog::software() const
{
  return m_pUserSoftware->text().trimmed();
}

teSource CDbSubmitDialog::source() const
{
  if(m_pCam->isChecked())
    return ENUM_SOURCE_CAM;
  if(m_pSw->isChecked())
    return ENUM_SOURCE_SW;
  return ENUM_SOURCE_UNSURE;
}

// OK: software needs its name, as on Windows (where a name of spaces passed)
void CDbSubmitDialog::accept()
{
  if(m_pSw->isChecked() && software().isEmpty())
  {
    QMessageBox::warning(this, windowTitle(),
                         tr("You indicated that this file is processed. Please enter software name."));
    m_pUserSoftware->setFocus();
    m_pUserSoftware->selectAll();
    return;
  }

  QDialog::accept();
}
