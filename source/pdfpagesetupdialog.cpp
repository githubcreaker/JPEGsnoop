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

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPageLayout>
#include <QPageSize>
#include <QPrinter>
#include <QRadioButton>
#include <QVBoxLayout>

#include "pdfpagesetupdialog.h"

// The paper sizes offered, as the system's page layout panel offers the
// common ones; the printer's own size is added if it isn't one of them. Its
// widgets have object names for the test driver.
CPdfPageSetupDialog::CPdfPageSetupDialog(QPrinter &printer, QWidget *pParent) :
  QDialog(pParent),
  m_printer(printer),
  m_pPaper(new QComboBox),
  m_pPortrait(new QRadioButton(tr("Portrait"))),
  m_pLandscape(new QRadioButton(tr("Landscape")))
{
  setObjectName(QStringLiteral("pdfPageSetupDialog"));
  setWindowTitle(tr("Print Setup"));

  const QPageLayout layout = m_printer.pageLayout();
  const QPageSize sizeNow = layout.pageSize();

  static const QPageSize::PageSizeId aeSizes[] = { QPageSize::A3,     QPageSize::A4,    QPageSize::A5,
                                                   QPageSize::B4,     QPageSize::B5,    QPageSize::Letter,
                                                   QPageSize::Legal,  QPageSize::Tabloid, QPageSize::Executive };
  bool bListed = false;
  for(const QPageSize::PageSizeId eSize : aeSizes)
  {
    const QPageSize size(eSize);
    m_pPaper->addItem(size.name(), QVariant::fromValue(size));
    if(size.isEquivalentTo(sizeNow))
    {
      m_pPaper->setCurrentIndex(m_pPaper->count() - 1);
      bListed = true;
    }
  }
  if(!bListed && sizeNow.isValid())
  {
    m_pPaper->insertItem(0, sizeNow.name(), QVariant::fromValue(sizeNow));
    m_pPaper->setCurrentIndex(0);
  }
  m_pPaper->setObjectName(QStringLiteral("paperSize"));

  m_pPortrait->setObjectName(QStringLiteral("portrait"));
  m_pLandscape->setObjectName(QStringLiteral("landscape"));
  if(layout.orientation() == QPageLayout::Landscape)
    m_pLandscape->setChecked(true);
  else
    m_pPortrait->setChecked(true);

  QHBoxLayout *pOrientation = new QHBoxLayout;
  pOrientation->addWidget(m_pPortrait);
  pOrientation->addWidget(m_pLandscape);
  pOrientation->addStretch(1);

  QFormLayout *pFields = new QFormLayout;
  pFields->addRow(tr("Paper size:"), m_pPaper);
  pFields->addRow(tr("Orientation:"), pOrientation);

  QLabel *pNote = new QLabel(tr("Printing makes a PDF file, which the system's page layout panel doesn't set up. "
                                "These set its pages."));
  pNote->setObjectName(QStringLiteral("note"));
  pNote->setWordWrap(true);

  QDialogButtonBox *pButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pButtons->setObjectName(QStringLiteral("buttonBox"));
  connect(pButtons, &QDialogButtonBox::accepted, this, &CPdfPageSetupDialog::accept);
  connect(pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  QVBoxLayout *pLayout = new QVBoxLayout(this);
  pLayout->addLayout(pFields);
  pLayout->addWidget(pNote);
  pLayout->addWidget(pButtons);
}

// The paper size and orientation chosen; the margins and the file stay
void CPdfPageSetupDialog::accept()
{
  m_printer.setPageSize(m_pPaper->currentData().value<QPageSize>());
  m_printer.setPageOrientation(m_pLandscape->isChecked() ? QPageLayout::Landscape : QPageLayout::Portrait);
  QDialog::accept();
}
