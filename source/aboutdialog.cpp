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
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QSet>
#include <QVBoxLayout>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

#include "aboutdialog.h"
#include "snoop.h"

// A label that opens its link in the browser
static QLabel *linkLabel(const QString &strText, const QString &strUrl, const QString &strName)
{
  QLabel *pLabel = new QLabel(QString("<a href=\"%1\">%2</a>").arg(strUrl.toHtmlEscaped(), strText.toHtmlEscaped()));
  pLabel->setObjectName(strName);
  pLabel->setTextFormat(Qt::RichText);
  pLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
  pLabel->setOpenExternalLinks(true);
  pLabel->setToolTip(strUrl);
  return pLabel;
}

// The same dialog as on Windows (IDD_ABOUTBOX): the icon, name, version and
// copyright, and the two links. Help > About did nothing.
AboutDialog::AboutDialog(QWidget *pParent) :
  QDialog(pParent)
{
  setWindowTitle(tr("About JPEGsnoop"));

  QLabel *pIcon = new QLabel;
  pIcon->setObjectName("lblIcon");
  pIcon->setPixmap(iconPixmap(devicePixelRatioF()));
  pIcon->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

  QLabel *pName = new QLabel(QStringLiteral("JPEGsnoop"));
  pName->setObjectName("lblName");
  QFont fontName = pName->font();
  fontName.setBold(true);
  fontName.setPointSizeF(fontName.pointSizeF() * 1.3);
  pName->setFont(fontName);

  QLabel *pVersion = new QLabel(tr("Version %1").arg(QStringLiteral(VERSION_STR)));
  pVersion->setObjectName("lblVersion");
  pVersion->setTextInteractionFlags(Qt::TextSelectableByMouse);

  QLabel *pCopyright = new QLabel(QString::fromUtf8("Copyright © 2017 Calvin Hass"));
  pCopyright->setObjectName("lblCopyright");

  // The Qt it runs on, which a bug report needs, in smaller type. (Not in the
  // placeholder colour: on macOS that is a quarter-strength grey, too faint.)
  QLabel *pQt = new QLabel(tr("Qt %1").arg(QString::fromLatin1(qVersion())));
  pQt->setObjectName("lblQt");
  pQt->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QFont fontQt = pQt->font();
  fontQt.setPointSizeF(fontQt.pointSizeF() * 0.85);
  pQt->setFont(fontQt);

  QVBoxLayout *pText = new QVBoxLayout;
  pText->addWidget(pName);
  pText->addWidget(pVersion);
  pText->addWidget(pCopyright);
  pText->addWidget(pQt);
  pText->addStretch();

  QHBoxLayout *pTop = new QHBoxLayout;
  pTop->addWidget(pIcon);
  pTop->addSpacing(12);
  pTop->addLayout(pText, 1);

  QGroupBox *pLinks = new QGroupBox;
  QVBoxLayout *pLinksLayout = new QVBoxLayout(pLinks);
  pLinksLayout->addWidget(linkLabel(tr("Documentation and Updates for JPEGsnoop"), docUrl(), "lblDocLink"));
  pLinksLayout->addWidget(linkLabel(QStringLiteral("www.impulseadventure.com/photo/"), siteUrl(), "lblSiteLink"));

  QDialogButtonBox *pButtons = new QDialogButtonBox(QDialogButtonBox::Ok);
  pButtons->setObjectName("buttonBox");
  connect(pButtons, &QDialogButtonBox::accepted, this, &QDialog::accept);

  QVBoxLayout *pLayout = new QVBoxLayout(this);
  pLayout->addLayout(pTop);
  pLayout->addWidget(pLinks);
  pLayout->addWidget(pButtons);
  pLayout->setSizeConstraint(QLayout::SetFixedSize);
}

QString AboutDialog::docUrl()
{
  return QStringLiteral("https://www.impulseadventure.com/photo/jpeg-snoop.html?ver=%1").arg(QStringLiteral(VERSION_STR));
}

QString AboutDialog::siteUrl()
{
  return QStringLiteral("https://www.impulseadventure.com/photo/");
}

QPixmap AboutDialog::iconPixmap(qreal dDevicePixelRatio)
{
  // images/jpegsnoop.ico has 16, 32 and 48-pixel art, each in 16 colours, 256
  // colours and 32 bits. Take the largest, and of those the one with the most
  // colours (the 32-bit one).
  QImageReader reader(QStringLiteral(":/images/jpegsnoop.ico"));
  QImage imgBest;
  qsizetype nBestColours = 0;

  for(int nInd = 0; nInd < std::max(1, reader.imageCount()); nInd++)
  {
    if((nInd > 0) && !reader.jumpToImage(nInd))
    {
      break;
    }

    const QImage img = reader.read().convertToFormat(QImage::Format_ARGB32);

    if(img.isNull())
    {
      continue;
    }

    QSet<QRgb> setColours;

    for(int nY = 0; nY < img.height(); nY++)
    {
      const QRgb *pLine = reinterpret_cast<const QRgb *>(img.constScanLine(nY));
      for(int nX = 0; nX < img.width(); nX++)
      {
        setColours.insert(pLine[nX]);
      }
    }

    if(imgBest.isNull() || (img.width() > imgBest.width()) ||
       ((img.width() == imgBest.width()) && (setColours.size() > nBestColours)))
    {
      imgBest = img;
      nBestColours = setColours.size();
    }
  }

  if(imgBest.isNull())
  {
    return QPixmap();
  }

  // Whole pixels, no smoothing: each art pixel a sharp square
  const int nScale = std::max(1, static_cast<int>(std::ceil(dDevicePixelRatio - 0.01)));
  QPixmap pix = QPixmap::fromImage(imgBest.scaled(imgBest.width() * nScale, imgBest.height() * nScale,
                                                  Qt::IgnoreAspectRatio, Qt::FastTransformation));
  pix.setDevicePixelRatio(nScale);
  return pix;
}
