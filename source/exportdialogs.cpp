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
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QVBoxLayout>

#include "exportdialogs.h"

//-----------------------------------------------------------------------------
// Tools > Export JPEG settings ("Export JPEG Settings")

ExportJpegDialog::ExportJpegDialog(uint32_t nStartOffset, const ExportJpegSettings &sInit, QWidget *pParent) :
  QDialog(pParent),
  m_pOverlay(new QCheckBox(tr("Enable Overlays (Slower)"))),
  m_pExtractAll(new QCheckBox(tr("Extract All JPEGs"))),
  m_pDhtAvi(new QCheckBox(tr("Insert MJPEG DHT (for AVI Frame export)"))),
  m_pForceSoi(new QCheckBox(tr("Missing SOI: Force SOI"))),
  m_pForceEoi(new QCheckBox(tr("Missing EOI: Force EOI"))),
  m_pIgnoreEoi(new QCheckBox(tr("Missing EOI: Ignore")))
{
  setWindowTitle(tr("Export JPEG Settings"));

  m_pOverlay->setObjectName("cbOverlay");
  m_pExtractAll->setObjectName("cbExtractAll");
  m_pDhtAvi->setObjectName("cbDhtAvi");
  m_pForceSoi->setObjectName("cbForceSoi");
  m_pForceEoi->setObjectName("cbForceEoi");
  m_pIgnoreEoi->setObjectName("cbIgnoreEoi");

  m_pOverlay->setChecked(sInit.bOverlayEn);
  m_pExtractAll->setChecked(sInit.bExtractAll);
  m_pDhtAvi->setChecked(sInit.bDhtAviInsert);
  m_pForceSoi->setChecked(sInit.bForceSoi);
  m_pForceEoi->setChecked(sInit.bForceEoi);
  m_pIgnoreEoi->setChecked(sInit.bIgnoreEoi && !sInit.bForceEoi);

  m_pOverlay->setToolTip(tr("Exports the file with the bytes set in Tools > File Overlay in place of its own."));
  m_pExtractAll->setToolTip(tr("Exports every JPEG in the file, such as a video's frames and embedded previews, "
                               "as name.000001.jpg, name.000002.jpg and so on."));
  m_pDhtAvi->setToolTip(tr("Adds the standard Huffman tables that Motion JPEG frames leave out."));
  m_pForceSoi->setToolTip(tr("Adds a start-of-image marker if the image has none."));
  m_pForceEoi->setToolTip(tr("Adds an end-of-image marker if the image has none."));
  m_pIgnoreEoi->setToolTip(tr("Exports to the end of the file if the image has no end-of-image marker."));

  // An image missing its EOI either gets one or doesn't
  connect(m_pForceEoi, &QCheckBox::toggled, this, [this](bool bOn) {
    if(bOn)
      m_pIgnoreEoi->setChecked(false);
  });
  connect(m_pIgnoreEoi, &QCheckBox::toggled, this, [this](bool bOn) {
    if(bOn)
      m_pForceEoi->setChecked(false);
  });

  QLabel *pOffset = new QLabel(tr("Source file start offset: 0x%1").arg(nStartOffset, 8, 16, QChar('0')));
  pOffset->setTextInteractionFlags(Qt::TextSelectableByMouse);

  QGroupBox *pGroup = new QGroupBox(tr("Settings"));
  QVBoxLayout *pGroupLayout = new QVBoxLayout(pGroup);
  pGroupLayout->addWidget(m_pOverlay);
  pGroupLayout->addWidget(m_pExtractAll);
  pGroupLayout->addWidget(m_pDhtAvi);
  pGroupLayout->addWidget(m_pForceSoi);
  pGroupLayout->addWidget(m_pForceEoi);
  pGroupLayout->addWidget(m_pIgnoreEoi);

  QDialogButtonBox *pButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pButtons->setObjectName("buttonBox");
  connect(pButtons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  QVBoxLayout *pLayout = new QVBoxLayout(this);
  pLayout->addWidget(pOffset);
  pLayout->addWidget(pGroup);
  pLayout->addWidget(pButtons);
  pLayout->setSizeConstraint(QLayout::SetFixedSize);
}

ExportJpegSettings ExportJpegDialog::settings() const
{
  ExportJpegSettings sSettings;
  sSettings.bOverlayEn = m_pOverlay->isChecked();
  sSettings.bExtractAll = m_pExtractAll->isChecked();
  sSettings.bDhtAviInsert = m_pDhtAvi->isChecked();
  sSettings.bForceSoi = m_pForceSoi->isChecked();
  sSettings.bForceEoi = m_pForceEoi->isChecked();
  sSettings.bIgnoreEoi = m_pIgnoreEoi->isChecked();
  return sSettings;
}

//-----------------------------------------------------------------------------
// Tools > Export TIFF format ("Export as TIFF")

ExportTiffDialog::ExportTiffDialog(const QString &strFile, bool bYccAvailable, QWidget *pParent) :
  QDialog(pParent),
  m_pRgb8(new QRadioButton(tr("RGB (8 bit)"))),
  m_pRgb16(new QRadioButton(tr("RGB (16 bit)"))),
  m_pYcc8(new QRadioButton(tr("YCC (8 bit)")))
{
  setWindowTitle(tr("Export as TIFF"));

  m_pRgb8->setObjectName("rbRgb8");
  m_pRgb16->setObjectName("rbRgb16");
  m_pYcc8->setObjectName("rbYcc8");
  m_pRgb8->setChecked(true);

  m_pRgb8->setToolTip(tr("The image as the preview shows it, including the View > Image Chan choice."));
  m_pRgb16->setToolTip(tr("As RGB (8 bit), with each value scaled to 16 bits."));
  if(bYccAvailable)
  {
    m_pYcc8->setToolTip(tr("The decoded Y, Cb and Cr values, without colour conversion."));
  }
  else
  {
    m_pYcc8->setEnabled(false);
    m_pYcc8->setToolTip(tr("Only a decoded JPEG has YCC values."));
  }

  QLineEdit *pFile = new QLineEdit(strFile);
  pFile->setReadOnly(true);
  pFile->setMinimumWidth(320);

  QFormLayout *pFileLayout = new QFormLayout;
  pFileLayout->addRow(tr("Save Filename:"), pFile);

  QGroupBox *pGroup = new QGroupBox(tr("Output Format"));
  QVBoxLayout *pGroupLayout = new QVBoxLayout(pGroup);
  pGroupLayout->addWidget(m_pRgb8);
  pGroupLayout->addWidget(m_pRgb16);
  pGroupLayout->addWidget(m_pYcc8);

  QDialogButtonBox *pButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pButtons->setObjectName("buttonBox");
  connect(pButtons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  QVBoxLayout *pLayout = new QVBoxLayout(this);
  pLayout->addLayout(pFileLayout);
  pLayout->addWidget(pGroup);
  pLayout->addWidget(pButtons);
  pLayout->setSizeConstraint(QLayout::SetFixedSize);
}

FileTiff::teFormat ExportTiffDialog::format() const
{
  if(m_pRgb16->isChecked())
    return FileTiff::FMT_RGB16;
  if(m_pYcc8->isChecked())
    return FileTiff::FMT_YCC8;
  return FileTiff::FMT_RGB8;
}
