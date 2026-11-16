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

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include "overlaydialog.h"

namespace
{

// How many of the file's bytes the dialog shows, as on Windows
const int kShownBytes = 8;

// An offset as the dialog shows it: 0x0000ABCD
QString offsetText(quint32 nOffset)
{
  return QStringLiteral("0x") + QString("%1").arg(nOffset, 8, 16, QLatin1Char('0')).toUpper();
}

// The offset field's text as a number: hex, with or without 0x
bool parseOffset(const QString &strText, quint32 &nOffset)
{
  QString strHex = strText.trimmed();
  if(strHex.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
    strHex = strHex.mid(2);
  static const QRegularExpression reHex(QStringLiteral("^[0-9A-Fa-f]{1,8}$"));
  if(!reHex.match(strHex).hasMatch())
    return false;
  nOffset = strHex.toUInt(nullptr, 16);
  return true;
}

} // namespace

bool COverlayDialog::parseHexBytes(const QString &strText, QByteArray &abBytes)
{
  QString strHex = strText.trimmed();
  if(strHex.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
    strHex = strHex.mid(2);
  strHex.remove(QLatin1Char(' '));
  static const QRegularExpression reHex(QStringLiteral("^([0-9A-Fa-f]{2})*$"));
  if(!reHex.match(strHex).hasMatch())
    return false;
  abBytes = QByteArray::fromHex(strHex.toLatin1());
  return true;
}

// The Windows dialog's fields and wording. Its widgets have object names for
// the test driver.
COverlayDialog::COverlayDialog(const ReadFile &fnRead, quint64 nFileSize, QWidget *pParent) :
  QDialog(pParent),
  m_fnRead(fnRead),
  m_nFileSize(nFileSize),
  m_pEnabled(new QCheckBox(tr("Overlay Enabled?"))),
  m_pOffset(new QLineEdit),
  m_pOrigHex(new QLabel),
  m_pOrigBin(new QLabel),
  m_pValue(new QLineEdit),
  m_pLen(new QLineEdit)
{
  setObjectName(QStringLiteral("fileOverlayDialog"));
  setWindowTitle(tr("File Overlay Test"));
  m_pEnabled->setObjectName(QStringLiteral("overlayEnabled"));
  m_pOffset->setObjectName(QStringLiteral("offset"));
  m_pOrigHex->setObjectName(QStringLiteral("originalHex"));
  m_pOrigBin->setObjectName(QStringLiteral("originalBin"));
  m_pValue->setObjectName(QStringLiteral("newValue"));
  m_pLen->setObjectName(QStringLiteral("length"));

  // The bytes line up in a fixed-width font, and can be selected to copy
  const QFont fontFixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  for(QLabel *pLabel : { m_pOrigHex, m_pOrigBin })
  {
    pLabel->setFont(fontFixed);
    pLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  }
  m_pValue->setFont(fontFixed);
  m_pValue->setMinimumWidth(m_pValue->fontMetrics().horizontalAdvance(QLatin1Char('0')) * 24);
  m_pOffset->setFont(fontFixed);
  m_pLen->setValidator(new QIntValidator(0, kMaxBytes, m_pLen));
  m_pLen->setMaximumWidth(m_pLen->fontMetrics().horizontalAdvance(QLatin1Char('0')) * 6);

  QPushButton *pLoad = new QPushButton(tr("Load Original"));
  pLoad->setObjectName(QStringLiteral("loadOriginal"));
  pLoad->setAutoDefault(false);
  QHBoxLayout *pOffsetRow = new QHBoxLayout;
  pOffsetRow->addWidget(new QLabel(tr("File Offset:")));
  pOffsetRow->addWidget(m_pOffset);
  pOffsetRow->addWidget(pLoad);
  pOffsetRow->addStretch();

  QGroupBox *pOriginal = new QGroupBox(tr("Original Data in Overlay Region"));
  QFormLayout *pOriginalForm = new QFormLayout(pOriginal);
  pOriginalForm->addRow(tr("(Hex):"), m_pOrigHex);
  pOriginalForm->addRow(tr("(Bin):"), m_pOrigBin);

  QGroupBox *pNew = new QGroupBox(tr("New Values for Overlay Region"));
  QHBoxLayout *pNewRow = new QHBoxLayout(pNew);
  pNewRow->addWidget(new QLabel(tr("(Hex):")));
  pNewRow->addWidget(m_pValue, 1);
  pNewRow->addWidget(new QLabel(tr("Len (bytes)")));
  pNewRow->addWidget(m_pLen);

  QDialogButtonBox *pButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                                    QDialogButtonBox::Apply);
  pButtons->setObjectName(QStringLiteral("buttonBox"));

  QVBoxLayout *pLayout = new QVBoxLayout(this);
  pLayout->addWidget(m_pEnabled);
  pLayout->addLayout(pOffsetRow);
  pLayout->addWidget(pOriginal);
  pLayout->addWidget(pNew);
  pLayout->addWidget(pButtons);
  pLayout->setSizeConstraint(QLayout::SetFixedSize);

  // Load Original shows the file's bytes at the offset typed. (They are also
  // shown whenever the offset changes: on Windows they stayed those of the
  // last offset loaded until Load Original was pressed.)
  connect(pLoad, &QPushButton::clicked, this, &COverlayDialog::showOriginal);
  connect(m_pOffset, &QLineEdit::textChanged, this, &COverlayDialog::showOriginal);

  // Len follows the number of bytes typed, and can then be made shorter. (On
  // Windows it had to be typed as well, and a Len longer than the bytes given
  // overlaid whatever followed them in memory.)
  connect(m_pValue, &QLineEdit::textEdited, this, [this](const QString &strText) {
    QByteArray abBytes;
    if(parseHexBytes(strText, abBytes))
      m_pLen->setText(QString::number(abBytes.size()));
  });

  connect(pButtons, &QDialogButtonBox::accepted, this, [this]() {
    if(acceptFields())
      accept();
  });
  connect(pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(pButtons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this]() {
    if(acceptFields())
      emit applied();
  });

  setOverlay(false, 0, QByteArray());
}

void COverlayDialog::setOverlay(bool bEnabled, quint32 nOffset, const QByteArray &abData)
{
  m_bEnabled = bEnabled;
  m_nOffset = nOffset;
  m_abData = abData;

  m_pEnabled->setChecked(bEnabled);
  m_pOffset->setText(offsetText(nOffset));
  m_pValue->setText(QString::fromLatin1(abData.toHex()).toUpper());
  m_pLen->setText(QString::number(abData.size()));
  showOriginal();
}

// The file's first bytes at the offset typed, in hex and binary
void COverlayDialog::showOriginal()
{
  quint32 nOffset = 0;
  if(!parseOffset(m_pOffset->text(), nOffset))
  {
    m_pOrigHex->setText(tr("(not a hexadecimal offset)"));
    m_pOrigBin->clear();
    return;
  }
  if(nOffset >= m_nFileSize)
  {
    m_pOrigHex->setText(tr("(past the end of the file)"));
    m_pOrigBin->clear();
    return;
  }

  // One more than shown, to know whether to add "..."
  const QByteArray abBytes = m_fnRead ? m_fnRead(nOffset, kShownBytes + 1) : QByteArray();
  if(abBytes.isEmpty())
  {
    m_pOrigHex->setText(tr("(the file can't be read)"));
    m_pOrigBin->clear();
    return;
  }

  QString strHex = QStringLiteral("0x");
  QString strBin = QStringLiteral("0b");
  for(int nInd = 0; (nInd < abBytes.size()) && (nInd < kShownBytes); nInd++)
  {
    const uint nByte = static_cast<uint8_t>(abBytes.at(nInd));
    strHex += QString(" %1").arg(nByte, 2, 16, QLatin1Char('0')).toUpper();
    strBin += QString(" %1").arg(nByte, 8, 2, QLatin1Char('0'));
  }
  if(abBytes.size() > kShownBytes)
  {
    strHex += QStringLiteral(" ...");
    strBin += QStringLiteral(" ...");
  }
  m_pOrigHex->setText(strHex);
  m_pOrigBin->setText(strBin);
}

// Checks the fields and keeps them for overlayEnabled(), offset() and data();
// says what is wrong and returns false if they can't be used. With the
// overlay off, the rest doesn't matter (OK removes it).
bool COverlayDialog::acceptFields()
{
  if(!m_pEnabled->isChecked())
  {
    m_bEnabled = false;
    return true;
  }

  QString strError;
  quint32 nOffset = 0;
  QByteArray abBytes;
  bool bLenOk = false;
  const int nLen = m_pLen->text().toInt(&bLenOk);

  if(!parseOffset(m_pOffset->text(), nOffset))
    strError = tr("File Offset must be a hexadecimal number, such as 0x0000ABCD.");
  else if(nOffset >= m_nFileSize)
    strError = tr("File Offset %1 is past the end of the file (%2 bytes).").arg(offsetText(nOffset)).arg(m_nFileSize);
  else if(!parseHexBytes(m_pValue->text(), abBytes) || abBytes.isEmpty())
    strError = tr("The new value must be hexadecimal bytes, two digits each, such as FFD8.");
  else if(abBytes.size() > kMaxBytes)
    strError = tr("The new value can be at most %1 bytes.").arg(kMaxBytes);
  else if(!bLenOk || (nLen < 1) || (nLen > abBytes.size()))
    strError = tr("Len must be from 1 to the number of bytes in the new value (%1).").arg(abBytes.size());
  else if(nOffset + static_cast<quint64>(nLen) > m_nFileSize)
    strError = tr("The overlay runs past the end of the file (%1 bytes).").arg(m_nFileSize);

  if(!strError.isEmpty())
  {
    QMessageBox::warning(this, windowTitle(), strError);
    return false;
  }

  m_bEnabled = true;
  m_nOffset = nOffset;
  m_abData = abBytes.left(nLen);
  return true;
}
