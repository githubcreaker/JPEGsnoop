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

#include "q_decodedetaildlg.h"
#include "ui_q_decodedetaildlg.h"

#include <QFontMetrics>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

// The dialog used to do nothing on OK or Cancel (its accept() and reject()
// were empty overrides, so it never closed) and nothing opened it.
Q_DecodeDetailDlg::Q_DecodeDetailDlg(bool bEn, uint32_t nMcuX, uint32_t nMcuY, uint32_t nMcuLen, QWidget *parent) :
  QDialog(parent),
  ui(new Ui::Q_DecodeDetailDlg),
  m_bEn(bEn),
  m_nMcuX(nMcuX),
  m_nMcuY(nMcuY),
  m_nMcuLen(nMcuLen),
  m_nLoadMcuX(0),
  m_nLoadMcuY(0),
  m_nLoadMcuLen(0)
{
  ui->setupUi(this);

  // Whole numbers only (up to 4294967295, checked on OK), wide enough for
  // ten digits
  const QRegularExpression reDigits(QStringLiteral("[0-9]{0,10}"));
  const int nWidth = fontMetrics().horizontalAdvance(QStringLiteral("4294967295")) + 16;

  for(QLineEdit *pEdit : { ui->leMcuX, ui->leMcuY, ui->leNumMcus })
  {
    pEdit->setValidator(new QRegularExpressionValidator(reDigits, pEdit));
    pEdit->setMinimumWidth(nWidth);
  }

  ui->leMcuX->setText(QString::number(nMcuX));
  ui->leMcuY->setText(QString::number(nMcuY));
  ui->leNumMcus->setText(QString::number(nMcuLen));
  ui->cbEnableDec->setChecked(bEn);

  ui->pbLoadCoords->setEnabled(false);
  ui->pbLoadCoords->setToolTip(tr("Click the image first to mark the MCUs"));
  connect(ui->pbLoadCoords, &QPushButton::clicked, this, &Q_DecodeDetailDlg::onLoadCoords);

  ui->lblNote->setVisible(false);

  // OK and Cancel are connected in the .ui file
  adjustSize();
}

Q_DecodeDetailDlg::~Q_DecodeDetailDlg()
{
  delete ui;
}

void Q_DecodeDetailDlg::setLoadCoords(uint32_t nMcuX, uint32_t nMcuY, uint32_t nMcuLen)
{
  m_nLoadMcuX = nMcuX;
  m_nLoadMcuY = nMcuY;
  m_nLoadMcuLen = nMcuLen;
  ui->pbLoadCoords->setEnabled(true);
  ui->pbLoadCoords->setToolTip(QString());
}

void Q_DecodeDetailDlg::setNote(const QString &strNote)
{
  ui->lblNote->setText(strNote);
  ui->lblNote->setVisible(!strNote.isEmpty());
  adjustSize();
}

// Load Coords: the range marked on the image, and turn the decode on
void Q_DecodeDetailDlg::onLoadCoords()
{
  ui->leMcuX->setText(QString::number(m_nLoadMcuX));
  ui->leMcuY->setText(QString::number(m_nLoadMcuY));
  ui->leNumMcus->setText(QString::number(m_nLoadMcuLen));
  ui->cbEnableDec->setChecked(true);
}

// Read a field as a whole number from 0 to 4294967295, or say what's wrong
bool Q_DecodeDetailDlg::readField(QLineEdit *pEdit, const QString &strName, uint32_t &nVal)
{
  bool bOk = false;
  const uint32_t nRead = pEdit->text().trimmed().toUInt(&bOk, 10);

  if(!bOk)
  {
    QMessageBox::warning(this, windowTitle(), tr("Enter a whole number from 0 to 4294967295 for %1.").arg(strName));
    pEdit->setFocus();
    pEdit->selectAll();
    return false;
  }

  nVal = nRead;
  return true;
}

void Q_DecodeDetailDlg::accept()
{
  uint32_t nMcuX = 0;
  uint32_t nMcuY = 0;
  uint32_t nMcuLen = 0;

  if(!readField(ui->leMcuX, tr("MCU X"), nMcuX) || !readField(ui->leMcuY, tr("MCU Y"), nMcuY) ||
     !readField(ui->leNumMcus, tr("# MCUs"), nMcuLen))
  {
    return;
  }

  const bool bEn = ui->cbEnableDec->isChecked();

  if(bEn && (nMcuLen == 0))
  {
    QMessageBox::warning(this, windowTitle(), tr("Enter at least 1 for # MCUs: no MCUs would be reported."));
    ui->leNumMcus->setFocus();
    ui->leNumMcus->selectAll();
    return;
  }

  m_bEn = bEn;
  m_nMcuX = nMcuX;
  m_nMcuY = nMcuY;
  m_nMcuLen = nMcuLen;

  QDialog::accept();
}
