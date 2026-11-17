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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QAction>
#include <QActionGroup>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPainter>

#include "exportdialogs.h"
#include "reportview.h"

class QPrinter;
class QProgressDialog;
struct BatchList;
struct BatchSettings;

class CFindDialog;
class SnoopConfigDialog;
class Q_Viewer;
class CjfifDecode;
class CimgDecode;
class CwindowBuf;
class CDocLog;
class CSnoopConfig;
class CDbSigs;

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = 0);
  ~MainWindow();

  CReportView *textEdit;

  // Tools > Export JPEG and Export TIFF without their dialogs (as the test
  // driver uses them). Both act on the file shown; Export JPEG analyzes it
  // again first, and adds what it did to the report.
  bool exportJpeg(const QString &strOut, const ExportJpegSettings &sSettings);
  bool exportTiff(const QString &strOut, FileTiff::teFormat eFormat, QString &strError);
  bool canExportTiff();
  bool canExportYcc();

protected:
  void closeEvent(QCloseEvent *e) override;
  bool eventFilter(QObject *pObj, QEvent *pEvent) override;

signals:
  void ImgSrcChanged();

public slots:
  void updateImage();
  void reprocess();
  void openFile(const QString &strPath);

private slots:
  void open();
  void saveLog();
  void onSelectAll();
  void onFind();
  void onFindNext();
  void findFromDialog(const QString &strFind, bool bDown, bool bCase, bool bWord);
  void onConfig();
  void filePrint();
  void filePrintPreview();
  void printPreview(QPrinter *);
  void onExportJpeg();
  void onExportTiff();
  void onDetailedDecode();
  void onFileOverlay();
  void onBatchProcess();
  void onFileOffset();
  void onImageSearchFwd();
  void onImageSearchRev();
  void onLookupMcuOffset();
  void onAbout();

private:
  MainWindow &operator = (const MainWindow&);
  MainWindow(MainWindow&);
  
  void createActions(void);
  void createMenus(void);
  void runBatch(const BatchSettings &sBatch, const BatchList &list);
  void enableMenus();
  void updateTextActions();
  bool findInReport();
  void moveFindDialogOffSelection();
  bool currentOverlay(QByteArray &abData, uint32_t &nStart);
  void setCurrentOverlay(const QByteArray &abData, uint32_t nStart);
  void imageSearch(bool bForward);
  void clearImageViews();
  void AnalyzeFile();
  bool AnalyzeOpen();
  void AnalyzeFileDo();
  void AnalyzeClose();
  bool exportAllJpegs(const QString &strOut, const ExportJpegSettings &sSettings);
  bool detailRangeFromMarks(uint32_t &nMcuX, uint32_t &nMcuY, uint32_t &nMcuLen);

  CDocLog *m_pDocLog;
  CDbSigs *m_pDbSigs;
  CSnoopConfig *m_pAppConfig;
  Q_Viewer *imgWindow;

  // Decoders and Buffers
  CjfifDecode *m_pJfifDec;
  CimgDecode *m_pImgDec;
  CwindowBuf *m_pWBuf;

  QToolBar *fileToolBar;

  QMenu *fileMenu;
  QMenu *editMenu;
  QMenu *viewMenu;
  QMenu *imageChanMenu;
  QMenu *imageZoomMenu;
  QMenu *overlayMenu;
  QMenu *toolsMenu;
  QMenu *optionsMenu;
  QMenu *scanSegMenu;
  QMenu *helpMenu;

  // File menu
  QAction *newAct;
  QAction *openAct;
  QAction *saveLogAct;
  QAction *reprocessAct;
  QAction *batchProcessAct;
  QAction *OffsetAct;
  QAction *printAct;
  QAction *printPreviewAct;
  QAction *printSetupAct;
  QAction *recentFileAct;
  QAction *exitAct;

  // Edit menu
  QAction *undoAct;
  QAction *copyAct;
  QAction *selectAllAct;
  QAction *findAct;
  QAction *findNextAct;

  // View menu
  QAction *adjustAct;
  QAction *toolbarAct;
  QAction *statusBarAct;

  // Image Chan menu
  QAction *rgbAct;
  QAction *yccAct;
  QAction *rAct;
  QAction *gAct;
  QAction *bAct;
  QAction *yAct;
  QAction *cbAct;
  QAction *crAct;
  QActionGroup *channelGroup;

  // Image Zoom menu
  QAction *zoomInAct;
  QAction *zoomOutAct;
  QAction *zoom12_5Act;
  QAction *zoom25Act;
  QAction *zoom50Act;
  QAction *zoom100Act;
  QAction *zoom150Act;
  QAction *zoom200Act;
  QAction *zoom300Act;
  QAction *zoom400Act;
  QAction *zoom500Act;
  QActionGroup *zoomGroup;
  QActionGroup *zoomInOutGroup;

  // Overlay menu
  QAction *mcuGridAct;

  // Tools menu
  QAction *imgSearchFwdAct;
  QAction *imgSearchRevAct;
  QAction *lookupMcuOffAct;
  QAction *fileOverlayAct;
  QAction *searchDqtAct;
  QAction *exportJpegAct;
  QAction *exportTiffAct;
  QAction *addToDbAct;
  QAction *manageLocalDbAct;

  // Options menu
  QAction *dhtExpandAct;
  QAction *hideUnknownTagsAct;
  QAction *makerNotesAct;
  QAction *sigSearchAct;
  QAction *relaxedParseAct;
  QAction *configAct;
  QAction *checkUpdatesAct;

  // Scan Segment menu
  QAction *decodeImageAct;
  QAction *fullIdtcAct;
  QAction *noIdtcAct;
  QAction *histogramRgbAct;
  QAction *histogramYAct;
  QAction *dumpAct;
  QAction *detailedDecodeAct;
  QActionGroup *idtGroup;

  // Help menu
  QAction *aboutAct;

  QStatusBar *m_StatusBar;

  QMessageBox msgBox;

  QString m_currentFile;

  // Input JPEG file
  QFile *m_pFile;
  qint64 m_lFileSize = 0;

  bool m_bFileAnalyzed = false; // Have we opened and analyzed a file? (These three were never initialised)
  bool m_bFileOpened = false;   // Is a file currently opened?

  QLabel *rgbHisto;
  QLabel *yHisto;

  // Coach Messages
  QString strLoRes;
  QString strHiRes;

  // Edit > Find's dialog (made when first shown) and the last search, which
  // Find Next repeats. As on Windows they last until the app quits.
  CFindDialog *m_pFindDlg = nullptr;
  QString m_strFind;
  bool m_bFindDown = true;
  bool m_bFindCase = false;
  bool m_bFindWord = false;
  QString m_strFindStatus;      // what the status bar shows after a search found nothing

  // File > Batch Process's progress, while a batch runs
  QProgressDialog *m_pBatchProgress = nullptr;
};

#endif // MAINWINDOW_H
