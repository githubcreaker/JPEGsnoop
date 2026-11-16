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
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRadioButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include "offsetdialog.h"

// The Windows dialog's fields and wording. As there, it opens in hex with
// the offset the file is decoded from now. Its widgets have object names
// for the test driver.
COffsetDialog::COffsetDialog(quint32 nOffset, quint64 nFileSize, QWidget *pParent) :
  QDialog(pParent),
  m_nFileSize(nFileSize),
  m_nOffset(nOffset),
  m_pValue(new QLineEdit(offsetText(nOffset, true))),
  m_pHex(new QRadioButton(tr("Hex"))),
  m_pDecimal(new QRadioButton(tr("Decimal")))
{
  setObjectName(QStringLiteral("fileOffsetDialog"));
  setWindowTitle(tr("Starting File Offset"));
  m_pValue->setObjectName(QStringLiteral("offsetValue"));
  m_pHex->setObjectName(QStringLiteral("baseHex"));
  m_pDecimal->setObjectName(QStringLiteral("baseDecimal"));
  m_pHex->setChecked(true);
  m_pValue->setMinimumWidth(m_pValue->fontMetrics().horizontalAdvance(QLatin1Char('0')) * 16);

  QGridLayout *pFields = new QGridLayout;
  pFields->addWidget(new QLabel(tr("Offset (Bytes):")), 0, 0);
  pFields->addWidget(m_pHex, 0, 1);
  pFields->addWidget(m_pDecimal, 1, 1);
  pFields->addWidget(m_pValue, 0, 2);

  QDialogButtonBox *pButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pButtons->setObjectName(QStringLiteral("buttonBox"));

  QVBoxLayout *pLayout = new QVBoxLayout(this);
  pLayout->addLayout(pFields);
  pLayout->addWidget(pButtons);
  pLayout->setSizeConstraint(QLayout::SetFixedSize);

  // Clicked, not toggled: by then the click is done, so baseClicked() can
  // put the choice back. (Arrow keys between the two click too.)
  connect(m_pHex, &QRadioButton::clicked, this, &COffsetDialog::baseClicked);
  connect(m_pDecimal, &QRadioButton::clicked, this, &COffsetDialog::baseClicked);
  connect(pButtons, &QDialogButtonBox::accepted, this, [this]() {
    if(acceptFields())
      accept();
  });
  connect(pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  // Typing replaces the offset: a dialog shown selects its focus field's
  // text, as tabbing into it does
  m_pValue->setFocus();
}

bool COffsetDialog::parseOffset(const QString &strText, bool bHex, quint64 &nValue, bool &bTooLarge)
{
  nValue = 0;
  bTooLarge = false;

  QString strDigits = strText.trimmed();
  if(bHex && strDigits.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
    strDigits = strDigits.mid(2);

  static const QRegularExpression reHex(QStringLiteral("^[0-9A-Fa-f]+$"));
  static const QRegularExpression reDecimal(QStringLiteral("^[0-9]+$"));
  if(!(bHex ? reHex : reDecimal).match(strDigits).hasMatch())
    return false;

  bool bOk = false;
  nValue = strDigits.toULongLong(&bOk, bHex ? 16 : 10);
  bTooLarge = !bOk;
  return true;
}

QString COffsetDialog::offsetText(quint64 nValue, bool bHex)
{
  if(!bHex)
    return QString::number(nValue);
  return QStringLiteral("0x") + QString("%1").arg(nValue, 8, 16, QLatin1Char('0')).toUpper();
}

QString COffsetDialog::notNumberError(const QString &strText) const
{
  if(m_bHex)
    return tr("\"%1\" isn't a hex number. Type digits 0-9 and A-F, with or without 0x.").arg(strText);
  return tr("\"%1\" isn't a decimal number. Type digits 0-9 only.").arg(strText);
}

// Hex or Decimal clicked: as on Windows, the offset typed changes to that
// base. If it isn't a number in the base it was typed in, but is one in the
// base clicked (0x100 typed with Decimal chosen, say), it is kept as it is.
// Otherwise the choice goes back, with a message saying why (Windows said
// "Invalid hex string" or "Invalid decimal string", but changed the offset
// to 0, and its code noted that it should keep the choice instead).
void COffsetDialog::baseClicked()
{
  const bool bHex = m_pHex->isChecked();
  if(bHex == m_bHex)
    return;

  const QString strText = m_pValue->text().trimmed();
  quint64 nValue = 0;
  bool bTooLarge = false;
  const bool bNumber = !strText.isEmpty() && parseOffset(strText, m_bHex, nValue, bTooLarge);
  quint64 nOther = 0;
  bool bOtherTooLarge = false;

  if(strText.isEmpty())
  {
    // Nothing to change
  }
  else if(bNumber && !bTooLarge)
  {
    m_pValue->setText(offsetText(nValue, bHex));
  }
  else if(bNumber || !parseOffset(strText, bHex, nOther, bOtherTooLarge))
  {
    (m_bHex ? m_pHex : m_pDecimal)->setChecked(true);
    QMessageBox::warning(this, windowTitle(),
                         bNumber ? tr("\"%1\" is too large for an offset.").arg(strText)
                                 : tr("\"%1\" isn't a hex or decimal number.").arg(strText));
    return;
  }
  m_bHex = bHex;
}

// Checks the offset typed and keeps it for offset(); says what is wrong and
// returns false if it can't be used. (Windows took any 32-bit number, and
// one past the end of the file was decoded as if 0x00 bytes were there.)
bool COffsetDialog::acceptFields()
{
  const QString strText = m_pValue->text().trimmed();
  quint64 nValue = 0;
  bool bTooLarge = false;
  QString strError;

  if(strText.isEmpty())
    strError = tr("Type the offset to decode the file from.");
  else if(!parseOffset(strText, m_bHex, nValue, bTooLarge))
    strError = notNumberError(strText);
  else if(bTooLarge || (nValue >= m_nFileSize) || (nValue > 0xFFFFFFFFu))
    strError = tr("The offset must be less than the file's size, %1 bytes (%2).")
                 .arg(m_nFileSize)
                 .arg(offsetText(m_nFileSize, true));

  if(!strError.isEmpty())
  {
    QMessageBox::warning(this, windowTitle(), strError);
    return false;
  }

  m_nOffset = static_cast<quint32>(nValue);
  return true;
}
