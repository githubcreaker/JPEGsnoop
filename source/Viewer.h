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

#ifndef Q_VIEWER_H
#define Q_VIEWER_H

#include <QImage>
#include <QPainter>
#include <QPaintEvent>
#include <QString>
#include <QTimer>
#include <QWidget>

#include "snoop.h"

class CimgDecode;
class QLabel;
class QScrollArea;
class QStatusBar;
class CSnoopConfig;

// Draws the decoded image at the chosen zoom inside the preview's scroll
// area. 100% is one image pixel per screen (device) pixel, so a Retina screen
// shows the image 1:1. Below 100% it keeps one smoothly reduced copy; above
// 100% it enlarges only the area being painted, as sharp pixel blocks, so a
// high zoom never needs a full-size enlarged copy. A whole image (Zoom to Fit
// Window) can have any zoom: it is drawn from a copy at its size on screen,
// which is no larger than the window, so its blocks, the MCU grid and the red
// line fall on the same screen pixels. Mouse positions are reported in image
// pixels.
class PreviewCanvas : public QWidget
{
  Q_OBJECT
public:
  explicit PreviewCanvas(QWidget *parent = nullptr);

  // bWhole: fitted to the window. bQuick: reduced without smoothing, while
  // the window is being resized. The image is shared, not copied.
  void setImage(const QImage &img, double dZoom, bool bWhole = false);
  void setZoom(double dZoom, bool bWhole = false, bool bQuick = false);
  void setMcuGrid(const QSize &sizeMcu);            // empty: no grid
  void setFrameSize(const QSize &sizeFrame);        // a JPEG's own size; empty: none
  bool widgetToImage(const QPointF &ptWidget, QPoint &ptImg) const;

  const QImage &image() const { return m_img; }
  double zoom() const { return m_dZoom; }
  bool whole() const { return m_bWhole; }
  const QSize &frameSize() const { return m_sizeFrame; }

signals:
  void moved(QPoint ptImg, bool bInside);      // bInside: ptImg is in the image
  void released(QPoint ptImg, bool bInside);

protected:
  bool event(QEvent *pEvent) override;
  void paintEvent(QPaintEvent *pEvent) override;
  void mouseMoveEvent(QMouseEvent *pEvent) override;
  void mouseReleaseEvent(QMouseEvent *pEvent) override;
  void leaveEvent(QEvent *pEvent) override;

private:
  double logicalPerImagePixel() const;
  void rebuild();             // scaled copy and widget size for zoom and screen
  QSize drawnSize() const;    // in screen pixels
  int screenEdge(int nPixel) const;                 // where an image pixel starts
  int screenEnd(int nEnd, int nFull, int nDrawn) const;   // where the frame ends
  QRect screenRect(const QRect &rectDirty) const;   // in screen pixels, on the image
  void paintImageEnd(QPainter &painter, const QRect &rectScreen);
  void paintMcuGrid(QPainter &painter, const QRect &rectScreen);

  QImage m_img;
  QImage m_imgScaled;         // only below 100%, or of a whole image
  double m_dZoom;
  bool m_bWhole;
  bool m_bQuick;
  QSize m_sizeMcu;            // MCU grid, in image pixels (empty: none)
  QSize m_sizeFrame;          // where the image ends inside its MCUs (empty: none)
};

class Q_Viewer : public QWidget
{
  Q_OBJECT
public:
  explicit Q_Viewer(CSnoopConfig *pAppConfig, CimgDecode *pImgDecoder, QWidget *parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
  void drawImage();
  void clearImage();
  void showNoImage(const QString &strWhy);      // the file analyzed has none
  void updateMcuGrid();       // after View > Overlays > MCU Grid
  void setStatusBarVisible(bool bOn);     // View > Status Bar

public slots:
  void zoom(QAction *action);
  void setPreviewTitle(QAction *preview);

signals:
  void zoomModeChanged(int nMode);      // for the menu's check mark

protected:
  bool eventFilter(QObject *pObj, QEvent *pEvent) override;

private slots:
  void onCanvasMoved(QPoint ptImg, bool bInside);
  void onCanvasReleased(QPoint ptImg, bool bInside);

private:
  bool imageReady() const;
  bool scanReady() const;
  void showNoPosition();      // N/A in the MCU, file and YCC indicators
  void setZoomMode(int nMode);
  int nextZoomMode(bool bIn) const;
  double fitZoom(const QSize &sizeImg) const;
  void refit();               // Zoom to Fit Window, after a resize
  void updateTitle();

  CSnoopConfig *m_pAppConfig;
  CimgDecode *m_pImgDecoder;

  PreviewCanvas *canvas;
  QScrollArea *scrollArea;
  double scaleFactor;
  int32_t m_nZoomMode;

  QString m_strChannel;
  QTimer m_timerSmooth;       // after a resize's quick redraws

  QStatusBar *statusBar;
  QLabel *mcuLabel;
  QLabel *fileLabel;
  QLabel *yccLabel;
};

#endif // Q_VIEWER_H
