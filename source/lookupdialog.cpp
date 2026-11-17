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
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include "lookupdialog.h"

// The Windows dialog's fields and wording, with the MCU the pixel is in
// added. Recalc is the default button, so Return calculates (on Windows it
// closed the dialog); Windows' OK and Cancel both only closed it, so there is
// a Close button instead. Its widgets have object names for the test driver.
CLookupDialog::CLookupDialog(uint32_t nSizeX, uint32_t nSizeY, LookupFn fnLookup, QWidget *pParent) :
  QDialog(pParent),
  m_nSizeX(nSizeX),
  m_nSizeY(nSizeY),
  m_fnLookup(std::move(fnLookup)),
  m_pX(new QLineEdit(QStringLiteral("0"))),
  m_pY(new QLineEdit(QStringLiteral("0"))),
  m_pMcu(new QLabel),
  m_pOffset(new QLabel)
{
  setObjectName(QStringLiteral("lookupMcuDialog"));
  setWindowTitle(tr("Lookup File Offset of MCU"));
  m_pX->setObjectName(QStringLiteral("pixelX"));
  m_pY->setObjectName(QStringLiteral("pixelY"));
  m_pMcu->setObjectName(QStringLiteral("mcuValue"));
  m_pOffset->setObjectName(QStringLiteral("offsetValue"));

  const int nDigit = m_pX->fontMetrics().horizontalAdvance(QLatin1Char('0'));
  m_pX->setMinimumWidth(nDigit * 8);
  m_pY->setMinimumWidth(nDigit * 8);

  QLabel *pRangeX = new QLabel(QString("(0..%1)").arg(m_nSizeX - 1));
  QLabel *pRangeY = new QLabel(QString("(0..%1)").arg(m_nSizeY - 1));
  pRangeX->setObjectName(QStringLiteral("rangeX"));
  pRangeY->setObjectName(QStringLiteral("rangeY"));

  // The results can be selected and copied. The offset's box is wide enough
  // for any of its texts, so the dialog doesn't change size.
  for(QLabel *pResult : { m_pMcu, m_pOffset })
  {
    pResult->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    pResult->setTextInteractionFlags(Qt::TextSelectableByMouse);
  }
  const QFontMetrics fm = m_pOffset->fontMetrics();
  int nWidest = 0;
  for(const QString &strText : { QStringLiteral("0x00000000 : 0"), tr("Not decoded"), tr("Not Calculated") })
    nWidest = qMax(nWidest, fm.horizontalAdvance(strText));
  m_pOffset->setMinimumWidth(nWidest + nDigit * 2);

  QPushButton *pRecalc = new QPushButton(tr("Recalc"));
  pRecalc->setObjectName(QStringLiteral("recalcButton"));

  QGridLayout *pFields = new QGridLayout;
  pFields->addWidget(pRangeX, 0, 1);
  pFields->addWidget(pRangeY, 0, 3);
  pFields->addWidget(new QLabel(tr("X=")), 1, 0);
  pFields->addWidget(m_pX, 1, 1);
  pFields->addWidget(new QLabel(tr("Y=")), 1, 2);
  pFields->addWidget(m_pY, 1, 3);
  pFields->addWidget(new QLabel(tr("Pixels")), 1, 4);
  pFields->addWidget(new QLabel(tr("MCU:")), 2, 0);
  pFields->addWidget(m_pMcu, 2, 1, 1, 3);
  pFields->addWidget(new QLabel(tr("File Offset:")), 3, 0);
  pFields->addWidget(m_pOffset, 3, 1, 1, 3);
  pFields->addWidget(pRecalc, 3, 4);

  QDialogButtonBox *pButtons = new QDialogButtonBox(QDialogButtonBox::Close);
  pButtons->setObjectName(QStringLiteral("buttonBox"));

  QVBoxLayout *pLayout = new QVBoxLayout(this);
  pLayout->addLayout(pFields);
  pLayout->addWidget(pButtons);
  pLayout->setSizeConstraint(QLayout::SetFixedSize);

  // Now that it is in the dialog, so the dialog makes it its default (made
  // before, the dialog chose one when shown)
  pRecalc->setDefault(true);

  clearResult();

  connect(pRecalc, &QPushButton::clicked, this, &CLookupDialog::recalc);
  connect(pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  // A result shown is always for the pixel in the fields: on Windows, a
  // changed field kept the last result until Recalc
  connect(m_pX, &QLineEdit::textEdited, this, &CLookupDialog::clearResult);
  connect(m_pY, &QLineEdit::textEdited, this, &CLookupDialog::clearResult);

  // Typing replaces X: a dialog shown selects its focus field's text, as
  // tabbing into it does
  m_pX->setFocus();
}

void CLookupDialog::clearResult()
{
  m_pMcu->setText(tr("Not Calculated"));
  m_pOffset->setText(tr("Not Calculated"));
}

// A field's text as a pixel coordinate less than nSize: says what is wrong
// and returns false if it isn't one. (Windows said "Pixel X coord out of
// range", and for text that wasn't a number, calculated from the last
// number.)
bool CLookupDialog::readField(QLineEdit *pField, const QString &strAxis, uint32_t nSize, uint32_t &nValue)
{
  static const QRegularExpression reDigits(QStringLiteral("^[0-9]+$"));
  const QString strText = pField->text().trimmed();
  bool bOk = false;
  const qulonglong nNum = reDigits.match(strText).hasMatch() ? strText.toULongLong(&bOk) : 0;

  if(!bOk || (nNum >= nSize))
  {
    QMessageBox::warning(this, windowTitle(),
                         tr("%1 must be a whole number from 0 to %2.").arg(strAxis).arg(nSize - 1));
    pField->setFocus();
    pField->selectAll();
    return false;
  }

  nValue = static_cast<uint32_t>(nNum);
  return true;
}

// Recalc: the MCU holding the pixel, and where its scan data starts in the
// file, as Windows showed it (hex byte, then bit)
void CLookupDialog::recalc()
{
  uint32_t nX = 0;
  uint32_t nY = 0;
  if(!readField(m_pX, tr("X"), m_nSizeX, nX) || !readField(m_pY, tr("Y"), m_nSizeY, nY))
  {
    return;
  }

  QPoint ptMcu;
  uint32_t nByte = 0;
  uint32_t nBit = 0;
  const bool bFound = m_fnLookup(QPoint(static_cast<int>(nX), static_cast<int>(nY)), ptMcu, nByte, nBit);

  m_pMcu->setText(QString("[%1,%2]").arg(ptMcu.x()).arg(ptMcu.y()));
  if(bFound)
    m_pOffset->setText(QString("0x%1 : %2").arg(QString("%1").arg(nByte, 8, 16, QLatin1Char('0')).toUpper()).arg(nBit));
  else
    m_pOffset->setText(tr("Not decoded"));
}
