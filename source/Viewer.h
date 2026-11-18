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
// high zoom never needs a full-size enlarged copy. Mouse positions are
// reported in image pixels.
class PreviewCanvas : public QWidget
{
  Q_OBJECT
public:
  explicit PreviewCanvas(QWidget *parent = nullptr);

  void setImage(const QImage &img, double dZoom);   // image is shared, not copied
  void setZoom(double dZoom);
  void setMcuGrid(const QSize &sizeMcu);            // empty: no grid
  bool widgetToImage(const QPointF &ptWidget, QPoint &ptImg) const;

  const QImage &image() const { return m_img; }
  double zoom() const { return m_dZoom; }

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
  void rebuild();             // reduced copy and widget size for zoom and screen
  void paintMcuGrid(QPainter &painter, const QRect &rectDirty);

  QImage m_img;
  QImage m_imgReduced;        // only below 100%
  double m_dZoom;
  QSize m_sizeMcu;            // MCU grid, in image pixels (empty: none)
};

class Q_Viewer : public QWidget
{
  Q_OBJECT
public:
  explicit Q_Viewer(CSnoopConfig *pAppConfig, CimgDecode *pImgDecoder, QWidget *parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
  void drawImage();
  void clearImage();
  void updateMcuGrid();       // after View > Overlays > MCU Grid
  void setStatusBarVisible(bool bOn);     // View > Status Bar

public slots:
  void zoom(QAction *action);
  void setPreviewTitle(QAction *preview);
  void scanImageAc(bool);

private slots:
  void onCanvasMoved(QPoint ptImg, bool bInside);
  void onCanvasReleased(QPoint ptImg, bool bInside);

private:
  bool imageReady() const;
  bool scanReady() const;

  CSnoopConfig *m_pAppConfig;
  CimgDecode *m_pImgDecoder;

  PreviewCanvas *canvas;
  QScrollArea *scrollArea;
  double scaleFactor;
  int32_t m_nZoomMode;

  QString m_strPreview;
  QString m_strZoom;
  QString m_strIdct;

  QStatusBar *statusBar;
  QLabel *mcuLabel;
  QLabel *fileLabel;
  QLabel *yccLabel;
};

#endif // Q_VIEWER_H
