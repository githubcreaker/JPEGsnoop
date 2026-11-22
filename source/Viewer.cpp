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

PreviewCanvas::PreviewCanvas(QWidget *_parent) :
  QWidget(_parent), m_dZoom(1.0), m_bWhole(false), m_bQuick(false)
{
  setMouseTracking(true);
}

void PreviewCanvas::setImage(const QImage &img, double dZoom, bool bWhole)
{
  m_img = img;
  m_dZoom = (dZoom > 0.0) ? dZoom : 1.0;
  m_bWhole = bWhole;
  m_bQuick = false;
  rebuild();
}

void PreviewCanvas::setZoom(double dZoom, bool bWhole, bool bQuick)
{
  if(dZoom > 0.0)
  {
    m_dZoom = dZoom;
    m_bWhole = bWhole;
    m_bQuick = bQuick;
    rebuild();
  }
}

void PreviewCanvas::setMcuGrid(const QSize &sizeMcu)
{
  if(sizeMcu != m_sizeMcu)
  {
    m_sizeMcu = sizeMcu;
    update();
  }
}

void PreviewCanvas::setFrameSize(const QSize &sizeFrame)
{
  if(sizeFrame != m_sizeFrame)
  {
    m_sizeFrame = sizeFrame;
    update();
  }
}

// Widget (logical) pixels per image pixel. On a Retina screen there are two
// device pixels per logical pixel, so 100% is half an image pixel per point.
double PreviewCanvas::logicalPerImagePixel() const
{
  return m_dZoom / devicePixelRatioF();
}

// img as sharp blocks at dZoom, in nW x nH screen pixels: each has the colour
// of the image pixel under its centre, as widgetToImage() finds it. (QPainter's
// own enlarging can put an edge a screen pixel from there at an uneven zoom,
// such as 137%, off the MCU grid.)
static QImage enlarged(const QImage &img, int nW, int nH, double dZoom)
{
  const QImage imgSrc = img.convertToFormat(QImage::Format_RGB32);
  QImage imgDst(nW, nH, QImage::Format_RGB32);
  if(imgSrc.isNull() || imgDst.isNull())
  {
    return QImage();
  }

  QList<int> anSrcX(nW);
  for(int nX = 0; nX < nW; nX++)
  {
    anSrcX[nX] = qMin(qFloor((nX + 0.5) / dZoom), imgSrc.width() - 1);
  }
  for(int nY = 0; nY < nH; nY++)
  {
    const int nSrcY = qMin(qFloor((nY + 0.5) / dZoom), imgSrc.height() - 1);
    const QRgb *pSrc = reinterpret_cast<const QRgb *>(imgSrc.constScanLine(nSrcY));
    QRgb *pDst = reinterpret_cast<QRgb *>(imgDst.scanLine(nY));
    for(int nX = 0; nX < nW; nX++)
    {
      pDst[nX] = pSrc[anSrcX.at(nX)];
    }
  }
  return imgDst;
}

void PreviewCanvas::rebuild()
{
  m_imgScaled = QImage();

  if(m_img.isNull())
  {
    setFixedSize(0, 0);
    update();
    return;
  }

  const double dDpr = devicePixelRatioF();
  const int nW = qMax(1, qRound(m_img.width() * m_dZoom));
  const int nH = qMax(1, qRound(m_img.height() * m_dZoom));

  // Below 100%: one smoothly reduced copy at screen resolution, drawn 1:1. A
  // whole image has a copy enlarged too: it's no larger than the window.
  if(m_dZoom < 1.0)
  {
    m_imgScaled = m_img.scaled(nW, nH, Qt::IgnoreAspectRatio,
                               m_bQuick ? Qt::FastTransformation : Qt::SmoothTransformation);
  }
  else if(m_bWhole)
  {
    m_imgScaled = enlarged(m_img, nW, nH, m_dZoom);
  }
  if(!m_imgScaled.isNull())
  {
    m_imgScaled.setDevicePixelRatio(dDpr);
  }

  if(m_bWhole)
  {
    // Just the copy's size: rounding up the exact size made about one fitted
    // image in ten a point larger than the window
    setFixedSize(qMax(1, qCeil(nW / dDpr)), qMax(1, qCeil(nH / dDpr)));
  }
  else
  {
    const double dScale = logicalPerImagePixel();
    setFixedSize(qCeil(m_img.width() * dScale), qCeil(m_img.height() * dScale));
  }
  update();
}

QSize PreviewCanvas::drawnSize() const
{
  if(!m_imgScaled.isNull())
  {
    return m_imgScaled.size();
  }
  return QSize(qCeil(m_img.width() * m_dZoom), qCeil(m_img.height() * m_dZoom));
}

// The first screen pixel of image pixel nPixel. Enlarged whole, that is the
// first whose centre is in it (see enlarged()); the menu's zooms put every
// MCU's edge on a whole screen pixel.
int PreviewCanvas::screenEdge(int nPixel) const
{
  const double dAt = nPixel * m_dZoom;
  return (m_bWhole && (m_dZoom >= 1.0)) ? qCeil(dAt - 0.5) : qRound(dAt);
}

// The screen pixel just past nEnd of the image's nFull: where the red line
// goes, cut short to a whole pixel as on Windows, or enlarged whole, on the
// padding's first. With no padding it is just off what is drawn (nDrawn).
int PreviewCanvas::screenEnd(int nEnd, int nFull, int nDrawn) const
{
  if(nEnd >= nFull)
  {
    return nDrawn;
  }
  return (m_bWhole && (m_dZoom >= 1.0)) ? screenEdge(nEnd) : static_cast<int>(nEnd * m_dZoom);
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

  if(!m_imgScaled.isNull())
  {
    painter.drawImage(QPointF(0, 0), m_imgScaled);
  }
  else
  {
    // 100% and above: enlarge only the image area under the region being
    // painted, without smoothing, so each image pixel stays a sharp block
    const double dScale = logicalPerImagePixel();
    const QRect rectDirty = pEvent->rect();
    const QRect rectSrc = QRectF(rectDirty.x() / dScale, rectDirty.y() / dScale,
                                 rectDirty.width() / dScale, rectDirty.height() / dScale)
                            .toAlignedRect().adjusted(-1, -1, 1, 1) & m_img.rect();
    const QRectF rectDst(rectSrc.x() * dScale, rectSrc.y() * dScale, rectSrc.width() * dScale,
                         rectSrc.height() * dScale);

    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(rectDst, m_img, rectSrc);
  }

  // As on Windows, the grid is drawn over the line
  const QRect rectScreen = screenRect(pEvent->rect());
  paintImageEnd(painter, rectScreen);
  paintMcuGrid(painter, rectScreen);
}

// The part of the area being painted that is on the image, in screen pixels:
// an image pixel is m_dZoom of them
QRect PreviewCanvas::screenRect(const QRect &rectDirty) const
{
  const double dDpr = devicePixelRatioF();
  return QRectF(rectDirty.x() * dDpr, rectDirty.y() * dDpr, rectDirty.width() * dDpr, rectDirty.height() * dDpr)
           .toAlignedRect() &
         QRect(QPoint(0, 0), drawnSize());
}

// The first pixel from nFrom on that a dotted line has a dot on: every third
// pixel counted from the canvas's top or left edge, so a dot is in the same
// place however much of the canvas is painted. (With a dotted pen, the dots
// started where the area being painted started: scrolling copies what is
// shown and paints only what comes into view, and the new dots fell between
// the copied ones.)
static int firstDot(int nFrom)
{
  return nFrom + ((3 - (nFrom % 3)) % 3);
}

// Where the image ends inside its last, partial MCUs, as on Windows
// (CimgDecode::ViewOnDraw): a dotted red line just right of the image's own
// width, as tall as its own height, and one just below its own height, as
// wide as its own width, on the padding that fills the last MCUs. Windows
// drew it at every zoom, under the MCU grid, whether or not the grid was on,
// with its positions cut short to whole screen pixels. It didn't set a
// transparent background for it, as it did for the grid, so between the red
// dots the line is white. An image that fills its last MCUs has none: the
// line is just off the canvas (on Windows, the image's frame covered it).
//
// Each dot is one screen pixel, as the grid's are, on every third pixel.
void PreviewCanvas::paintImageEnd(QPainter &painter, const QRect &rectScreen)
{
  if(m_sizeFrame.isEmpty() || rectScreen.isEmpty())
  {
    return;
  }

  const QSize sizeDrawn = drawnSize();
  const int nEndX = screenEnd(m_sizeFrame.width(), m_img.width(), sizeDrawn.width());
  const int nEndY = screenEnd(m_sizeFrame.height(), m_img.height(), sizeDrawn.height());
  QList<QRect> aWhite;
  QList<QRect> aRed;

  // Down the right: column nEndX, rows 0 to nEndY - 1
  if((nEndX >= rectScreen.left()) && (nEndX <= rectScreen.right()))
  {
    const int nTop = rectScreen.top();
    const int nBottom = qMin(nEndY - 1, rectScreen.bottom());
    if(nTop <= nBottom)
    {
      aWhite.append(QRect(nEndX, nTop, 1, nBottom - nTop + 1));
      for(int nY = firstDot(nTop); nY <= nBottom; nY += 3)
        aRed.append(QRect(nEndX, nY, 1, 1));
    }
  }
  // Along the bottom: row nEndY, columns 0 to nEndX - 1
  if((nEndY >= rectScreen.top()) && (nEndY <= rectScreen.bottom()))
  {
    const int nLeft = rectScreen.left();
    const int nRight = qMin(nEndX - 1, rectScreen.right());
    if(nLeft <= nRight)
    {
      aWhite.append(QRect(nLeft, nEndY, nRight - nLeft + 1, 1));
      for(int nX = firstDot(nLeft); nX <= nRight; nX += 3)
        aRed.append(QRect(nX, nEndY, 1, 1));
    }
  }
  if(aWhite.isEmpty())
  {
    return;
  }

  painter.save();
  painter.scale(1.0 / devicePixelRatioF(), 1.0 / devicePixelRatioF());
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setPen(Qt::NoPen);
  painter.setBrush(Qt::white);
  painter.drawRects(aWhite);
  painter.setBrush(QColor(255, 0, 0));
  painter.drawRects(aRed);
  painter.restore();
}

// View > Overlays > MCU Grid, as on Windows (CimgDecode::ViewMcuOverlay): a
// dotted dark grey line along the top and the left of each MCU, with the
// image showing between the dots. Not at 12.5%, where an MCU of 8 pixels is
// one screen pixel and the lines would hide the image.
//
// Each dot is one screen pixel, on every third pixel (see firstDot()).
void PreviewCanvas::paintMcuGrid(QPainter &painter, const QRect &rectScreen)
{
  if(m_sizeMcu.isEmpty() || (m_dZoom < 0.25) || rectScreen.isEmpty())
  {
    return;
  }

  QList<QRect> aDots;
  for(int nX = 0; nX < m_img.width(); nX += m_sizeMcu.width())
  {
    const int nDevX = screenEdge(nX);
    if((nDevX < rectScreen.left()) || (nDevX > rectScreen.right()))
      continue;
    for(int nDevY = firstDot(rectScreen.top()); nDevY <= rectScreen.bottom(); nDevY += 3)
      aDots.append(QRect(nDevX, nDevY, 1, 1));
  }
  for(int nY = 0; nY < m_img.height(); nY += m_sizeMcu.height())
  {
    const int nDevY = screenEdge(nY);
    if((nDevY < rectScreen.top()) || (nDevY > rectScreen.bottom()))
      continue;
    for(int nDevX = firstDot(rectScreen.left()); nDevX <= rectScreen.right(); nDevX += 3)
      aDots.append(QRect(nDevX, nDevY, 1, 1));
  }

  painter.save();
  painter.scale(1.0 / devicePixelRatioF(), 1.0 / devicePixelRatioF());
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(32, 32, 32));
  painter.drawRects(aDots);
  painter.restore();
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

namespace
{

// The menu's zooms, PRV_ZOOM_12 to PRV_ZOOM_6400. The top is half what still
// fits in a widget (2^24-1 points) at one pixel per point, for the widest
// image decoded (65536 pixels).
constexpr double kZooms[] = { 0.125, 0.25, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 8.0, 16.0, 32.0, 64.0 };
static_assert(sizeof(kZooms) / sizeof(kZooms[0]) == PRV_ZOOMEND - PRV_ZOOMBEGIN - 1, "a zoom for each mode");

constexpr double kZoomMax = kZooms[PRV_ZOOMEND - PRV_ZOOMBEGIN - 2];
constexpr int kSmoothDelayMs = 150;     // a resize has stopped

double zoomOfMode(int nMode)
{
  return kZooms[qBound(0, nMode - (PRV_ZOOMBEGIN + 1), PRV_ZOOMEND - PRV_ZOOMBEGIN - 2)];
}

} // namespace

// Keep pLabel as wide as the widest of astrTexts, in its font and frame
static void reserveWidth(QLabel *pLabel, const QStringList &astrTexts)
{
  int nWidth = 0;
  for(const QString &strText : astrTexts)
  {
    pLabel->setText(strText);
    nWidth = qMax(nWidth, pLabel->sizeHint().width());
  }
  pLabel->setMinimumWidth(nWidth);
}

Q_Viewer::Q_Viewer(CSnoopConfig *pAppConfig, CimgDecode *pImgDecoder, QWidget *_parent, Qt::WindowFlags f) :
  QWidget(_parent, f), m_pAppConfig(pAppConfig), m_pImgDecoder(pImgDecoder), canvas(nullptr), scrollArea(nullptr), scaleFactor(1.0), m_nZoomMode(PRV_ZOOM_FIT)
{
  setGeometry(200, 50, 400, 400);

  QFont font("Courier", 12);

  statusBar = new QStatusBar(this);

  // (Object names for the test driver)
  mcuLabel = new QLabel(statusBar);
  mcuLabel->setObjectName(QStringLiteral("mcuLabel"));
  mcuLabel->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  mcuLabel->setFont(font);
  statusBar->addPermanentWidget(mcuLabel);

  fileLabel = new QLabel(statusBar);
  fileLabel->setObjectName(QStringLiteral("fileLabel"));
  fileLabel->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  fileLabel->setFont(font);
  statusBar->addPermanentWidget(fileLabel);

  yccLabel = new QLabel(statusBar);
  yccLabel->setObjectName(QStringLiteral("yccLabel"));
  yccLabel->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  yccLabel->setFont(font);
  statusBar->addPermanentWidget(yccLabel);

  // Room from the start for all they show, as Windows' panes had, or the
  // window widened when the mouse was first over the image: up to 8192 MCUs
  // each way, a 32-bit offset and 16-bit DC values (six characters with "-")
  reserveWidth(mcuLabel, { "MCU [0000,0000]", "MCU N/A" });
  reserveWidth(fileLabel, { "File 0x00000000:0", tr("File: not decoded"), tr("File N/A") });
  reserveWidth(yccLabel, { "YCC DC=[-00000,-00000,-00000]", tr("YCC N/A") });
  showNoPosition();

  // The canvas is the image's size at the current zoom (it used to be a label
  // stretched over the whole window, which also made 12.5% look blocky)
  canvas = new PreviewCanvas;

  scrollArea = new QScrollArea(this);
  scrollArea->setBackgroundRole(QPalette::Dark);
  scrollArea->setWidgetResizable(false);
  scrollArea->setWidget(canvas);
  scrollArea->setVisible(false);
  // As Zoom to Fit Window, the default, has it (see setZoomMode())
  scrollArea->setAlignment(Qt::AlignCenter);
  scrollArea->installEventFilter(this);

  m_timerSmooth.setSingleShot(true);
  connect(&m_timerSmooth, &QTimer::timeout, this, [this]() { canvas->setZoom(scaleFactor, true); });

  QGridLayout *loGrid = new QGridLayout(this);
  loGrid->addWidget(scrollArea, 0, 0);
  loGrid->addWidget(statusBar, 1, 0);
  loGrid->setContentsMargins(0, 0, 0, 0);
  setLayout(loGrid);

  m_strChannel = "RGB";
  setWindowTitle("Image");

  setVisible(false);

  connect(canvas, &PreviewCanvas::released, this, &Q_Viewer::onCanvasReleased);
  connect(canvas, &PreviewCanvas::moved, this, &Q_Viewer::onCanvasMoved);
}

// Does the decoder hold an image to show? (It may have been reset since the
// preview was drawn, e.g. by opening a file it can't decode.) A Photoshop
// file's image is shown too, though no JPEG scan was decoded.
bool Q_Viewer::imageReady() const
{
  return m_pImgDecoder->m_bDibTempReady && m_pImgDecoder->m_pDibTemp;
}

// Is the image a decoded JPEG scan? Only then are there MCUs and blocks to
// look up under the mouse.
bool Q_Viewer::scanReady() const
{
  return imageReady() && m_pImgDecoder->IsImgDecoded() && m_pImgDecoder->IsPreviewReady();
}

void Q_Viewer::onCanvasReleased(QPoint ptImg, bool bInside)
{
  // SetMarkerBlk() takes 8x8 block coordinates, not pixels
  if(bInside && scanReady())
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

  if(!bInside || !scanReady())
  {
    showNoPosition();
    return;
  }

  QPoint p;
  p = m_pImgDecoder->PixelToMcu(ptImg);
  mcuLabel->setText(QString("MCU [%1,%2]")
                    .arg(p.x(), 4, 10, QChar('0'))
                    .arg(p.y(), 4, 10, QChar('0')));
  // An MCU after the scan stopped has no position (it showed 0x00000000:0)
  if(m_pImgDecoder->LookupFilePosMcu(p, nByte, nBit))
    fileLabel->setText(QString("File 0x%1:%2").arg(nByte, 8, 16, QChar('0')).arg(nBit));
  else
    fileLabel->setText(tr("File: not decoded"));
  p = m_pImgDecoder->PixelToBlk(ptImg);
  m_pImgDecoder->LookupBlkYCC(p, nY, nCb, nCr);
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
  canvas->setFrameSize(QSize());
  // Nor is the mouse on a pixel of it (they kept the previous image's until
  // the mouse next moved)
  showNoPosition();
  // The title kept it too, over an empty window
  setWindowTitle("Image");
}

void Q_Viewer::showNoImage(const QString &strWhy)
{
  setWindowTitle(strWhy.isEmpty() ? QString("Image not shown") : QString("Image not shown: %1").arg(strWhy));
}

// There is no image pixel under the mouse, or none of a JPEG scan. Windows
// left the three panes blank, but here they are framed, and empty frames look
// like buttons.
void Q_Viewer::showNoPosition()
{
  mcuLabel->setText(tr("MCU N/A"));
  fileLabel->setText(tr("File N/A"));
  yccLabel->setText(tr("YCC N/A"));
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

  // Shown first, which lays it out: fitting needs the room there is
  scrollArea->setVisible(true);
  setVisible(true);

  const QImage &img = *m_pImgDecoder->m_pDibTemp;
  const bool bFit = (m_nZoomMode == PRV_ZOOM_FIT);
  if(bFit)
  {
    m_timerSmooth.stop();
    scaleFactor = fitZoom(img.size());
  }
  canvas->setImage(img, scaleFactor, bFit);
  // A JPEG's own size, for the line where it ends inside its last MCUs (a
  // Photoshop file's image has no MCUs; on Windows its size was 0 for this)
  uint32_t nFrameX = 0;
  uint32_t nFrameY = 0;
  const bool bFrame = m_pImgDecoder->IsPreviewReady() && m_pImgDecoder->GetFrameSize(nFrameX, nFrameY);
  canvas->setFrameSize(bFrame ? QSize(static_cast<int>(nFrameX), static_cast<int>(nFrameY)) : QSize());
  updateMcuGrid();
  updateTitle();
}

// As on Windows, "Image (RGB, DC)": the channel, and how the image shown was
// decoded. A non-JPEG image (from a Photoshop file) is just "Image": it has
// no channel view or IDCT mode. Then the zoom, or "Fit" and what that comes to.
void Q_Viewer::updateTitle()
{
  const double dPercent = scaleFactor * 100.0;
  QString strZoom;
  if(m_nZoomMode != PRV_ZOOM_FIT)
    strZoom = QString("@ %1%").arg(dPercent);
  else if(dPercent < 9.95)
    strZoom = QString("@ Fit (%1%)").arg(dPercent, 0, 'f', 1);
  else
    strZoom = QString("@ Fit (%1%)").arg(qRound(dPercent));

  if(m_pImgDecoder->IsPreviewReady())
    setWindowTitle(QString("Image (%1, %2) %3")
                   .arg(m_strChannel, m_pImgDecoder->IsPreviewAc() ? "DC+AC" : "DC", strZoom));
  else
    setWindowTitle("Image " + strZoom);
}

// The zoom that shows all of an image of sizeImg, as large as fits in the
// window with no scroll bars, up to the menu's highest. Whole screen pixels:
// a part of one would round up to a point too many.
double Q_Viewer::fitZoom(const QSize &sizeImg) const
{
  if(sizeImg.isEmpty())
  {
    return 1.0;
  }
  const QSize sizeView = scrollArea->maximumViewportSize();
  const double dDpr = canvas->devicePixelRatioF();
  const int nRoomW = qMax(1, qFloor(sizeView.width() * dDpr));
  const int nRoomH = qMax(1, qFloor(sizeView.height() * dDpr));
  return qMin(qMin(static_cast<double>(nRoomW) / sizeImg.width(), static_cast<double>(nRoomH) / sizeImg.height()),
              kZoomMax);
}

// Zoom to Fit Window follows the window's size. A large image is slow to
// reduce smoothly (70 to 100 ms for the largest), so while the size changes it
// is reduced quickly, then smoothly once that stops.
void Q_Viewer::refit()
{
  if((m_nZoomMode != PRV_ZOOM_FIT) || canvas->image().isNull())
  {
    return;
  }
  const double dZoom = fitZoom(canvas->image().size());
  if(qFuzzyCompare(dZoom, scaleFactor))
  {
    return;
  }
  scaleFactor = dZoom;
  const bool bQuick = (dZoom < 1.0);
  canvas->setZoom(dZoom, true, bQuick);
  if(bQuick)
    m_timerSmooth.start(kSmoothDelayMs);
  else
    m_timerSmooth.stop();
  updateTitle();
}

bool Q_Viewer::eventFilter(QObject *pObj, QEvent *pEvent)
{
  if(pObj == scrollArea)
  {
    bool bRefit = (pEvent->type() == QEvent::Resize);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    bRefit = bRefit || (pEvent->type() == QEvent::DevicePixelRatioChange);
#endif
    if(bRefit)
    {
      refit();
    }
  }
  return QWidget::eventFilter(pObj, pEvent);
}

// View > Status Bar: the MCU, file position and YCC under the mouse, which
// on Windows were in the main window's one status bar
void Q_Viewer::setStatusBarVisible(bool bOn)
{
  statusBar->setVisible(bOn);
}

// The MCU grid is drawn over a JPEG's image only: a Photoshop file's has no
// MCUs. A JPEG's image is a whole number of MCUs.
void Q_Viewer::updateMcuGrid()
{
  const bool bGrid = m_pImgDecoder->GetPreviewOverlayMcuGrid() && imageReady() && m_pImgDecoder->IsPreviewReady();
  canvas->setMcuGrid(bGrid ? m_pImgDecoder->GetMcuSize() : QSize());
}

void Q_Viewer::zoom(QAction* action)
{
  int nMode = action->data().toInt();
  if((nMode == PRV_ZOOM_IN) || (nMode == PRV_ZOOM_OUT))
  {
    nMode = nextZoomMode(nMode == PRV_ZOOM_IN);
  }
  setZoomMode(nMode);
}

// The menu's next zoom above or below what is shown, which Zoom to Fit Window
// can put between two of them, or the mode as it is if there is none.
int Q_Viewer::nextZoomMode(bool bIn) const
{
  constexpr double kSame = 1e-6;        // (a fitted zoom that is one of them)
  if(bIn)
  {
    for(int nMode = PRV_ZOOMBEGIN + 1; nMode < PRV_ZOOMEND; nMode++)
    {
      if(zoomOfMode(nMode) > scaleFactor * (1.0 + kSame))
        return nMode;
    }
  }
  else
  {
    for(int nMode = PRV_ZOOMEND - 1; nMode > PRV_ZOOMBEGIN; nMode--)
    {
      if(zoomOfMode(nMode) < scaleFactor * (1.0 - kSame))
        return nMode;
    }
  }
  return m_nZoomMode;
}

void Q_Viewer::setZoomMode(int nMode)
{
  m_timerSmooth.stop();

  // What is in the middle of the view, to keep it there (the middle of the
  // image, across or down, where all of it shows). It stayed put at the top
  // left, so the middle moved further away the higher the zoom.
  const QSize sizeView = scrollArea->viewport()->size();
  const double dDpr = canvas->devicePixelRatioF();
  const double dPerPixel = canvas->zoom() / dDpr;
  const QSize sizeImg = canvas->image().size();
  const double dMidX = (canvas->width() <= sizeView.width())
                         ? (sizeImg.width() / 2.0)
                         : ((sizeView.width() / 2.0 - canvas->x()) / dPerPixel);
  const double dMidY = (canvas->height() <= sizeView.height())
                         ? (sizeImg.height() / 2.0)
                         : ((sizeView.height() / 2.0 - canvas->y()) / dPerPixel);

  m_nZoomMode = nMode;
  emit zoomModeChanged(nMode);

  // Fitted, the image is in the middle. (It needs no scroll bars: it is
  // never larger than the view without them, which is what Qt goes by.)
  const bool bFit = (nMode == PRV_ZOOM_FIT);
  scrollArea->setAlignment(bFit ? Qt::AlignCenter : (Qt::AlignLeft | Qt::AlignTop));

  if(!bFit)
    scaleFactor = zoomOfMode(nMode);
  if(canvas->image().isNull())
  {
    return;
  }
  if(bFit)
    scaleFactor = fitZoom(sizeImg);
  canvas->setZoom(scaleFactor, bFit);

  if(!bFit)
  {
    const double dNewPerPixel = scaleFactor / dDpr;
    // Qt shows or hides the scroll bars later, by a queued call, and their
    // room is then the view's no longer: have that done first, twice, as one
    // bar appearing can make the other needed
    for(int nRound = 0; nRound < 2; nRound++)
      QCoreApplication::sendPostedEvents(scrollArea, QEvent::MetaCall);
    const QSize sizeNow = scrollArea->viewport()->size();
    scrollArea->horizontalScrollBar()->setValue(qRound(dMidX * dNewPerPixel - sizeNow.width() / 2.0));
    scrollArea->verticalScrollBar()->setValue(qRound(dMidY * dNewPerPixel - sizeNow.height() / 2.0));
  }
  updateTitle();
}

void Q_Viewer::setPreviewTitle(QAction *action)
{
  switch(action->data().toInt())
  {
    case PREVIEW_RGB:
      m_strChannel = "RGB";
      break;

    case PREVIEW_YCC:
      m_strChannel = "YCC";
      break;

    case PREVIEW_R:
      m_strChannel = "R";
      break;

    case PREVIEW_G:
      m_strChannel = "G";
      break;

    case PREVIEW_B:
      m_strChannel = "B";
      break;

    case PREVIEW_Y:
      m_strChannel = "Y";
      break;

    case PREVIEW_CB:
      m_strChannel = "Cb";
      break;

    case PREVIEW_CR:
      m_strChannel = "Cr";
      break;

    default:
      m_strChannel = "???";
      break;
  }
}
