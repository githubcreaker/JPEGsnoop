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

#include <QDebug>
#include <QtMath>
#include <QtWidgets>

#include "ImgDecode.h"
#include "SnoopConfig.h"

#include "Viewer.h"

//-----------------------------------------------------------------------------
// PreviewCanvas

PreviewCanvas::PreviewCanvas(QWidget *_parent) : QWidget(_parent), m_dZoom(1.0)
{
  setMouseTracking(true);
}

void PreviewCanvas::setImage(const QImage &img, double dZoom)
{
  m_img = img;
  m_dZoom = (dZoom > 0.0) ? dZoom : 1.0;
  rebuild();
}

void PreviewCanvas::setZoom(double dZoom)
{
  if(dZoom > 0.0)
  {
    m_dZoom = dZoom;
    rebuild();
  }
}

// Widget (logical) pixels per image pixel. On a Retina screen there are two
// device pixels per logical pixel, so 100% is half an image pixel per point.
double PreviewCanvas::logicalPerImagePixel() const
{
  return m_dZoom / devicePixelRatioF();
}

void PreviewCanvas::rebuild()
{
  m_imgReduced = QImage();

  if(m_img.isNull())
  {
    setFixedSize(0, 0);
    update();
    return;
  }

  // Below 100%: one smoothly reduced copy at screen resolution, drawn 1:1
  if(m_dZoom < 1.0)
  {
    const int nW = qMax(1, qRound(m_img.width() * m_dZoom));
    const int nH = qMax(1, qRound(m_img.height() * m_dZoom));

    m_imgReduced = m_img.scaled(nW, nH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    m_imgReduced.setDevicePixelRatio(devicePixelRatioF());
  }

  const double dScale = logicalPerImagePixel();
  setFixedSize(qCeil(m_img.width() * dScale), qCeil(m_img.height() * dScale));
  update();
}

bool PreviewCanvas::event(QEvent *pEvent)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
  // Moved to a screen with a different pixel density
  if(pEvent->type() == QEvent::DevicePixelRatioChange)
  {
    rebuild();
  }
#endif
  return QWidget::event(pEvent);
}

void PreviewCanvas::paintEvent(QPaintEvent *pEvent)
{
  if(m_img.isNull())
  {
    return;
  }

  QPainter painter(this);

  if(!m_imgReduced.isNull())
  {
    painter.drawImage(QPointF(0, 0), m_imgReduced);
    return;
  }

  // 100% and above: enlarge only the image area under the region being
  // painted, without smoothing, so each image pixel stays a sharp block
  const double dScale = logicalPerImagePixel();
  const QRect rectDirty = pEvent->rect();
  const QRect rectSrc = QRectF(rectDirty.x() / dScale, rectDirty.y() / dScale,
                               rectDirty.width() / dScale, rectDirty.height() / dScale)
                          .toAlignedRect().adjusted(-1, -1, 1, 1) & m_img.rect();
  const QRectF rectDst(rectSrc.x() * dScale, rectSrc.y() * dScale, rectSrc.width() * dScale, rectSrc.height() * dScale);

  painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
  painter.drawImage(rectDst, m_img, rectSrc);
}

bool PreviewCanvas::widgetToImage(const QPointF &ptWidget, QPoint &ptImg) const
{
  if(m_img.isNull())
  {
    return false;
  }

  const double dScale = logicalPerImagePixel();
  const int nX = qFloor(ptWidget.x() / dScale);
  const int nY = qFloor(ptWidget.y() / dScale);

  if((nX < 0) || (nY < 0) || (nX >= m_img.width()) || (nY >= m_img.height()))
  {
    return false;
  }

  ptImg = QPoint(nX, nY);
  return true;
}

static QPointF eventPos(QMouseEvent *pEvent)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return pEvent->position();
#else
  return pEvent->localPos();
#endif
}

void PreviewCanvas::mouseMoveEvent(QMouseEvent *pEvent)
{
  QPoint ptImg;
  const bool bInside = widgetToImage(eventPos(pEvent), ptImg);
  emit moved(ptImg, bInside);
}

void PreviewCanvas::mouseReleaseEvent(QMouseEvent *pEvent)
{
  QPoint ptImg;
  const bool bInside = widgetToImage(eventPos(pEvent), ptImg);
  emit released(ptImg, bInside);
}

void PreviewCanvas::leaveEvent(QEvent *pEvent)
{
  emit moved(QPoint(), false);
  QWidget::leaveEvent(pEvent);
}

//-----------------------------------------------------------------------------
// Q_Viewer

Q_Viewer::Q_Viewer(CSnoopConfig *pAppConfig, CimgDecode *pImgDecoder, QWidget *_parent, Qt::WindowFlags f) :
  QWidget(_parent, f), m_pAppConfig(pAppConfig), m_pImgDecoder(pImgDecoder), canvas(nullptr), scrollArea(nullptr), scaleFactor(0.125), m_nZoomMode(PRV_ZOOM_12)
{
  setGeometry(200, 50, 400, 400);

  QFont font("Courier", 12);

  statusBar = new QStatusBar(this);

  mcuLabel = new QLabel(statusBar);
  mcuLabel->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  mcuLabel->setFont(font);
  statusBar->addPermanentWidget(mcuLabel);

  fileLabel = new QLabel(statusBar);
  fileLabel->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  fileLabel->setFont(font);
  statusBar->addPermanentWidget(fileLabel);

  yccLabel = new QLabel(statusBar);
  yccLabel->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  yccLabel->setFont(font);
  statusBar->addPermanentWidget(yccLabel);

  // The canvas is the image's size at the current zoom (it used to be a label
  // stretched over the whole window, which also made 12.5% look blocky)
  canvas = new PreviewCanvas;

  scrollArea = new QScrollArea(this);
  scrollArea->setBackgroundRole(QPalette::Dark);
  scrollArea->setWidgetResizable(false);
  scrollArea->setWidget(canvas);
  scrollArea->setVisible(false);

  QGridLayout *loGrid = new QGridLayout(this);
  loGrid->addWidget(scrollArea, 0, 0);
  loGrid->addWidget(statusBar, 1, 0);
  loGrid->setContentsMargins(0, 0, 0, 0);
  setLayout(loGrid);

  m_strPreview = "Image (RGB)";
  setWindowTitle(m_strPreview + " " + m_strZoom);
  m_strZoom = QString("@ %1%").arg(scaleFactor * 100.0);

  setVisible(false);

  connect(canvas, &PreviewCanvas::released, this, &Q_Viewer::onCanvasReleased);
  connect(canvas, &PreviewCanvas::moved, this, &Q_Viewer::onCanvasMoved);
  connect(m_pAppConfig, SIGNAL(scanImageAc(bool)), this, SLOT(scanImageAc(bool)));
}

// Does the decoder hold a decoded image? (It may have been reset since the
// preview was drawn, e.g. by opening a file it can't decode.)
bool Q_Viewer::imageReady() const
{
  return m_pImgDecoder->IsImgDecoded() && m_pImgDecoder->m_bDibTempReady && m_pImgDecoder->m_pDibTemp;
}

void Q_Viewer::onCanvasReleased(QPoint ptImg, bool bInside)
{
  // SetMarkerBlk() takes 8x8 block coordinates, not pixels
  if(bInside && imageReady())
  {
    m_pImgDecoder->SetMarkerBlk(m_pImgDecoder->PixelToBlk(ptImg));
  }
}

void Q_Viewer::onCanvasMoved(QPoint ptImg, bool bInside)
{
  uint32_t nByte;
  uint32_t nBit;

  int32_t nY;
  int32_t nCb;
  int32_t nCr;

  if(!bInside || !imageReady())
  {
    mcuLabel->clear();
    fileLabel->clear();
    yccLabel->clear();
    return;
  }

  QPoint p;
  p = m_pImgDecoder->PixelToMcu(ptImg);
  mcuLabel->setText(QString("MCU [%1,%2]")
                    .arg(p.x(), 4, 10, QChar('0'))
                    .arg(p.y(), 4, 10, QChar('0')));
  m_pImgDecoder->LookupFilePosMcu(p, nByte, nBit);
  p = m_pImgDecoder->PixelToBlk(ptImg);
  m_pImgDecoder->LookupBlkYCC(p, nY, nCb, nCr);
  fileLabel->setText(QString("File 0x%1:%2").arg(nByte, 8, 16, QChar('0')).arg(nBit));
  yccLabel->setText(QString("YCC DC=[%1,%2,%3]")
                    .arg(nY, 5, 10, QChar('0'))
                    .arg(nCb, 5, 10, QChar('0'))
                    .arg(nCr, 5, 10, QChar('0')));
}

// Show no image: the source changed and has no decoded image (yet). The
// canvas keeps its own copy of the image, so it showed the previous file's
// until something else was drawn.
void Q_Viewer::clearImage()
{
  canvas->setImage(QImage(), scaleFactor);
}

void Q_Viewer::drawImage()
{
  qDebug() << "Q_Viewer::drawImage";

  // Ensure that image has been decoded already
  if(!imageReady())
  {
      qDebug() << "Q_Viewer::drawImage exit as image not decoded";
      return;
  }

  setWindowTitle(m_strPreview + " " + m_strIdct + " " + m_strZoom);
  canvas->setImage(*m_pImgDecoder->m_pDibTemp, scaleFactor);
  scrollArea->setVisible(true);
  setVisible(true);
}

void Q_Viewer::zoom(QAction* action)
{
  if(action->data().toInt() == PRV_ZOOM_IN)
  {
    if(m_nZoomMode + 1 < PRV_ZOOMEND)
    {
      m_nZoomMode++;

    }
  }
  else if(action->data().toInt() == PRV_ZOOM_OUT)
  {
    if(m_nZoomMode - 1 > PRV_ZOOMBEGIN)
    {
      m_nZoomMode--;
    }
  }
  else
  {
    m_nZoomMode = action->data().toInt();
  }

  switch(m_nZoomMode)
  {
    case PRV_ZOOM_12:
      scaleFactor = 0.125;
      break;

    case PRV_ZOOM_25:
      scaleFactor = 0.25;
      break;

    case PRV_ZOOM_50:
      scaleFactor = 0.5;
      break;

    case PRV_ZOOM_100:
      scaleFactor = 1.0;
      break;

    case PRV_ZOOM_150:
      scaleFactor = 1.5;
      break;

    case PRV_ZOOM_200:
      scaleFactor = 2.0;
      break;

    case PRV_ZOOM_300:
      scaleFactor = 3.0;
      break;

    case PRV_ZOOM_400:
      scaleFactor = 4.0;
      break;

    case PRV_ZOOM_800:
      scaleFactor = 8.0;
      break;

    default:
      scaleFactor = 1.0;
      break;
  }

  m_strZoom = QString("@ %1%").arg(scaleFactor * 100.0);
  drawImage();
}

void Q_Viewer::setPreviewTitle(QAction *action)
{
    // TODO: Alter title depending on whether non-JPEG image displayed (eg. PSD)
    //       and indicate whether DC-only or DC+AC. See original CimgDecode::ViewOnDraw().

    m_strPreview = "Image (";
  switch(action->data().toInt())
  {
    case PREVIEW_RGB:
      m_strPreview += "RGB";
      break;

    case PREVIEW_YCC:
      m_strPreview += "YCC";
      break;

    case PREVIEW_R:
      m_strPreview += "R";
      break;

    case PREVIEW_G:
      m_strPreview += "G";
      break;

    case PREVIEW_B:
      m_strPreview += "B";
      break;

    case PREVIEW_Y:
      m_strPreview += "Y";
      break;

    case PREVIEW_CB:
      m_strPreview += "Cb";
      break;

    case PREVIEW_CR:
      m_strPreview += "Cr";
      break;

    default:
      m_strPreview += "???";
      break;
  }
  m_strPreview += ")";
}

void Q_Viewer::scanImageAc(bool b)
{
  qDebug() << "scanImageAc" << b;

  if(b)
  {
    m_strIdct = "DC+AC";
  }
  else
  {
    m_strIdct = "DC";
  }
}
