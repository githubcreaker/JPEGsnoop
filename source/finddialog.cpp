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
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include "finddialog.h"

// The layout of the Windows dialog: the text and the options on the left,
// Find Next and Cancel on the right. The widgets have object names for the
// test driver.
CFindDialog::CFindDialog(QWidget *pParent) :
  QDialog(pParent),
  m_pFind(new QLineEdit),
  m_pWord(new QCheckBox(tr("Match &whole word only"))),
  m_pCase(new QCheckBox(tr("Match &case"))),
  m_pUp(new QRadioButton(tr("&Up"))),
  m_pDown(new QRadioButton(tr("&Down"))),
  m_pFindNext(new QPushButton(tr("&Find Next")))
{
  setObjectName(QStringLiteral("findDialog"));
  setWindowTitle(tr("Find"));
  m_pFind->setObjectName(QStringLiteral("findWhat"));
  m_pFind->setMinimumWidth(m_pFind->fontMetrics().averageCharWidth() * 30);
  m_pWord->setObjectName(QStringLiteral("matchWholeWord"));
  m_pCase->setObjectName(QStringLiteral("matchCase"));
  m_pUp->setObjectName(QStringLiteral("directionUp"));
  m_pDown->setObjectName(QStringLiteral("directionDown"));
  m_pFindNext->setObjectName(QStringLiteral("findNext"));

  QLabel *pLabel = new QLabel(tr("Fi&nd what:"));
  pLabel->setBuddy(m_pFind);
  QHBoxLayout *pFindRow = new QHBoxLayout;
  pFindRow->addWidget(pLabel);
  pFindRow->addWidget(m_pFind, 1);

  QVBoxLayout *pMatch = new QVBoxLayout;
  pMatch->addStretch();
  pMatch->addWidget(m_pWord);
  pMatch->addWidget(m_pCase);

  QGroupBox *pDirection = new QGroupBox(tr("Direction"));
  QHBoxLayout *pDirectionRow = new QHBoxLayout(pDirection);
  pDirectionRow->addWidget(m_pUp);
  pDirectionRow->addWidget(m_pDown);
  m_pDown->setChecked(true);

  QHBoxLayout *pOptionsRow = new QHBoxLayout;
  pOptionsRow->addLayout(pMatch);
  pOptionsRow->addStretch();
  pOptionsRow->addWidget(pDirection);

  QVBoxLayout *pLeft = new QVBoxLayout;
  pLeft->addLayout(pFindRow);
  pLeft->addLayout(pOptionsRow);

  QPushButton *pCancel = new QPushButton(tr("Cancel"));
  pCancel->setObjectName(QStringLiteral("cancel"));
  QVBoxLayout *pButtons = new QVBoxLayout;
  pButtons->addWidget(m_pFindNext);
  pButtons->addWidget(pCancel);
  pButtons->addStretch();

  QHBoxLayout *pLayout = new QHBoxLayout(this);
  pLayout->addLayout(pLeft, 1);
  pLayout->addLayout(pButtons);
  pLayout->setSizeConstraint(QLayout::SetFixedSize);

  // Find Next is the default button (Return), and is off while there is no
  // text to find
  m_pFindNext->setDefault(true);
  m_pFindNext->setEnabled(false);
  connect(m_pFind, &QLineEdit::textChanged, this,
          [this](const QString &strText) { m_pFindNext->setEnabled(!strText.isEmpty()); });
  connect(m_pFindNext, &QPushButton::clicked, this, [this]() {
    emit findNext(m_pFind->text(), m_pDown->isChecked(), m_pCase->isChecked(), m_pWord->isChecked());
  });
  connect(pCancel, &QPushButton::clicked, this, &QDialog::reject);
}

void CFindDialog::setOptions(const QString &strFind, bool bDown, bool bCase, bool bWord)
{
  m_pFind->setText(strFind);
  m_pFind->selectAll();
  m_pFind->setFocus(Qt::OtherFocusReason);
  (bDown ? m_pDown : m_pUp)->setChecked(true);
  m_pCase->setChecked(bCase);
  m_pWord->setChecked(bWord);
}
