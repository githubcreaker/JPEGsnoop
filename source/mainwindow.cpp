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

#include <algorithm>
#include <new>
#include <utility>

#include <QtWidgets>
#include <QtPrintSupport/qtprintsupportglobal.h>
#include <QPrintDialog>
#include <QPrinter>
#include <QPrintPreviewDialog>

#include "DbSigs.h"
#include "General.h"
#include "ImgDecode.h"
#include "JfifDecode.h"
#include "SnoopConfig.h"
#include "Viewer.h"
#include "WindowBuf.h"
#include "note.h"
#include "q_decodedetaildlg.h"
#include "snoop.h"
#include "snoopconfigdialog.h"

#include "mainwindow.h"

// Show a histogram across the label's width at the screen's own resolution.
// A plain pixmap is drawn at one pixel per point, which a Retina screen then
// doubles, so the bars came out soft and uneven. Fast (unsmoothed) scaling
// keeps the bars' edges sharp.
static void showHistogram(QLabel *pLabel, const QImage &imgHisto)
{
  const qreal dDpr = pLabel->devicePixelRatioF();
  QImage imgScaled = imgHisto.scaled(qRound(pLabel->width() * dDpr), qRound(imgHisto.height() * dDpr));

  imgScaled.setDevicePixelRatio(dDpr);
  pLabel->setPixmap(QPixmap::fromImage(imgScaled));
}

#ifdef Q_OS_MAC
const QString rsrcPath = ":/images/mac";
#else
const QString rsrcPath = ":/images/win";
#endif

//-----------------------------------------------------------------------------

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), textEdit(new QPlainTextEdit)
{
  if(DEBUG_EN)
    qDebug() << "MainWindow::MainWindow() Begin";

  m_pFile = nullptr;            // AnalyzeFile() replaces it; freed in the destructor

  QWidget *widget = new QWidget;
  setCentralWidget(widget);
  setGeometry(100, 100, 800, 700);

  m_pDocLog = new CDocLog();
  m_pDocLog->Enable();

  m_pAppConfig = new CSnoopConfig(this);
  m_pAppConfig->RegistryLoad();

  m_pDbSigs = new CDbSigs(m_pDocLog, m_pAppConfig);

  // Allocate the file window buffer
  m_pWBuf = new CwindowBuf(m_pDocLog);

  if(!m_pWBuf)
  {
    msgBox.setText("ERROR: Not enough memory for File Buffer");
    exit(1);
  }

  // Allocate the JPEG decoder
  m_pImgDec = new CimgDecode(m_pDocLog, m_pWBuf, m_pAppConfig, this);

  if(!m_pImgDec)
  {
    msgBox.setText("ERROR: Not enough memory for Image Decoder");
    exit(1);
  }

  m_pJfifDec = new CjfifDecode(m_pDocLog, m_pDbSigs, m_pWBuf, m_pImgDec, m_pAppConfig, this);

  if(!m_pJfifDec)
  {
    msgBox.setText("ERROR: Not enough memory for JFIF Decoder");
    exit(1);
  }

  imgWindow = new Q_Viewer(m_pAppConfig, m_pImgDec, this, Qt::Window);

#ifdef Q_OS_LINUX
  menuBar()->setNativeMenuBar(false);
#endif

  fileToolBar = addToolBar(tr("File"));

  createActions();
  createMenus();

  QHBoxLayout *hLayout = new QHBoxLayout;
  QVBoxLayout *vLayout = new QVBoxLayout;

  rgbHisto = new QLabel;
  rgbHisto->setObjectName("rgbHistogram");
  rgbHisto->setAlignment(Qt::AlignBottom | Qt::AlignLeft);
  rgbHisto->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  rgbHisto->setMinimumSize(width() / 2, 180);
  rgbHisto->setStyleSheet("background-color: white");
  hLayout->addWidget(rgbHisto);

  yHisto = new QLabel;
  yHisto->setObjectName("yHistogram");
  yHisto->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  yHisto->setMinimumSize(width() / 2, 180);
  yHisto->setStyleSheet("background-color: white");
  hLayout->addWidget(yHisto);

  vLayout->addWidget(textEdit);
  vLayout->addLayout(hLayout);

  QWidget *w = new QWidget;
  w->setLayout(vLayout);
  setCentralWidget(w);

  QFont f;
  f.setFamily("Courier");
  textEdit->setFont(f);
  textEdit->appendPlainText("");

  m_StatusBar = statusBar();
  QLabel *st1 = new QLabel("     ");
  st1->setAlignment(Qt::AlignHCenter);
  st1->setMinimumSize(st1->sizeHint());

  m_StatusBar->addPermanentWidget(st1);
  st1->setText("1234");
  m_pJfifDec->SetStatusBar(m_StatusBar);

  connect(m_pJfifDec, SIGNAL(updateStatus(QString, int)), m_StatusBar, SLOT(showMessage(QString,int)));
  connect(m_pImgDec, SIGNAL(updateStatus(QString, int)), m_StatusBar, SLOT(showMessage(QString,int)));
  connect(m_pAppConfig, SIGNAL(reprocess()), this, SLOT(reprocess()));

  m_pDocLog->setDoc(textEdit);

  // Re-colour the report when the light/dark appearance changes (the event
  // reaches the report view after its own palette has been updated)
  textEdit->installEventFilter(this);

  // Coach Messages
  strLoRes = "Currently only decoding low-res view (DC-only). Full-resolution image decode can be enabled in [Options->Scan Segment->Full IDCT], but it is slower.";
  strHiRes = "Currently decoding high-res view (AC+DC), which can be slow. For faster operation, low-resolution image decode can be enabled in [Options->Scan Segment->No IDCT].";

}

//-----------------------------------------------------------------------------

MainWindow::~MainWindow()
{
  // The preview window and the decoders are QObject children that QObject
  // would delete after this body; delete them first, since they use the
  // helpers below, then the helpers, which have no parent.
  delete imgWindow;
  imgWindow = nullptr;
  delete m_pJfifDec;
  m_pJfifDec = nullptr;
  delete m_pImgDec;
  m_pImgDec = nullptr;

  delete m_pDbSigs;
  m_pDbSigs = nullptr;
  delete m_pWBuf;
  m_pWBuf = nullptr;
  delete m_pDocLog;
  m_pDocLog = nullptr;
  delete m_pFile;
  m_pFile = nullptr;
}

//-----------------------------------------------------------------------------

void MainWindow::createActions(void)
{
  if(DEBUG_EN)
    m_pAppConfig->DebugLogAdd("MainWindow::createActions() Begin");

  const QIcon newIcon = QIcon::fromTheme("document-new", QIcon(rsrcPath + "/filenew.png"));
  newAct = new QAction(newIcon, tr("&New"), this);
  newAct->setShortcuts(QKeySequence::New);
  newAct->setStatusTip(tr("New"));
//  connect(newAct, &QAction::triggered, this, &MainWindow::newFile);
  fileToolBar->addAction(newAct);

  const QIcon openIcon = QIcon::fromTheme("document-open", QIcon(rsrcPath + "/fileopen.png"));
  openAct = new QAction(openIcon, tr("&Open Image..."), this);
  openAct->setShortcuts(QKeySequence::Open);
  openAct->setStatusTip(tr("Open"));
  fileToolBar->addAction(openAct);
  connect(openAct, &QAction::triggered, this, &MainWindow::open);

  const QIcon saveIcon = QIcon::fromTheme("document-save", QIcon(rsrcPath + "/filesave.png"));
  saveLogAct = new QAction(saveIcon, tr("&Save Log..."), this);
  saveLogAct->setShortcuts(QKeySequence::Save);
  saveLogAct->setEnabled(false);
  saveLogAct->setStatusTip(tr("Save Log"));
  fileToolBar->addAction(saveLogAct);
  connect(saveLogAct, &QAction::triggered, this, &MainWindow::saveLog);

  reprocessAct = new QAction(tr("Reprocess File"), this);
  reprocessAct->setShortcut(tr("Ctrl+R"));
  reprocessAct->setEnabled(false);
//   saveAct->setStatusTip(tr("Save the document to disk"));
   connect(reprocessAct, &QAction::triggered, this, &MainWindow::reprocess);

  batchProcessAct = new QAction(tr("Batch Process..."), this);
  OffsetAct = new QAction(tr("Offset"), this);

  const QIcon printIcon = QIcon::fromTheme("document-print", QIcon(rsrcPath + "/fileprint.png"));
  printAct = new QAction(printIcon, tr("&Print..."), this);
  printAct->setShortcuts(QKeySequence::Print);
  printAct->setStatusTip(tr("Print the document"));
  fileToolBar->addAction(printAct);
  connect(printAct, &QAction::triggered, this, &MainWindow::filePrint);

  fileToolBar->addSeparator();

  printPreviewAct = new QAction(tr("Print Preview"), this);
//   printPreviewAct->setStatusTip(tr("Print the document"));
  connect(printPreviewAct, &QAction::triggered, this, &MainWindow::filePrintPreview);

  printSetupAct = new QAction(tr("Print Setup..."), this);
//   printAct->setStatusTip(tr("Print the document"));
//   connect(printAct, &QAction::triggered, this, &MainWindow::print);

  recentFileAct = new QAction(tr("Recent File"), this);
  recentFileAct->setEnabled(false);

  exitAct = new QAction(tr("E&xit"), this);
  exitAct->setShortcuts(QKeySequence::Quit);
//   exitAct->setStatusTip(tr("Exit the application"));
  connect(exitAct, &QAction::triggered, this, &MainWindow::close);

  undoAct = new QAction(tr("Undo"), this);
  undoAct->setShortcuts(QKeySequence::Undo);
  undoAct->setEnabled(false);

  copyAct = new QAction(tr("&Copy"), this);
  copyAct->setShortcuts(QKeySequence::Copy);
  copyAct->setEnabled(false);

  selectAllAct = new QAction(tr("Select All"), this);
  selectAllAct->setShortcuts(QKeySequence::SelectAll);
  selectAllAct->setEnabled(false);

  findAct = new QAction(tr("Find..."), this);
  findAct->setShortcuts(QKeySequence::Find);
  findAct->setEnabled(false);

  findNextAct = new QAction(tr("Find Next"), this);
  findNextAct->setShortcuts(QKeySequence::FindNext);
  findNextAct->setEnabled(false);

  adjustAct = new QAction(tr("Adjust..."), this);
  adjustAct->setEnabled(false);

  // Image Channel
  rgbAct = new QAction(tr("RGB"), this);
  rgbAct->setShortcut(tr("Alt+1"));
  rgbAct->setData(PREVIEW_RGB);
  rgbAct->setCheckable(true);
  rgbAct->setChecked(true);

  yccAct = new QAction(tr("YCC"), this);
  yccAct->setShortcut(tr("Alt+2"));
  yccAct->setData(PREVIEW_YCC);
  yccAct->setCheckable(true);

  rAct = new QAction(tr("R"), this);
  rAct->setShortcut(tr("Alt+3"));
  rAct->setData(PREVIEW_R);
  rAct->setCheckable(true);

  gAct = new QAction(tr("G"), this);
  gAct->setShortcut(tr("Alt+4"));
  gAct->setData(PREVIEW_G);
  gAct->setCheckable(true);

  bAct = new QAction(tr("B"), this);
  bAct->setShortcut(tr("Alt+5"));
  bAct->setData(PREVIEW_B);
  bAct->setCheckable(true);

  yAct = new QAction(tr("Y (Greyscale)"), this);
  yAct->setShortcut(tr("Alt+6"));
  yAct->setData(PREVIEW_Y);
  yAct->setCheckable(true);

  cbAct = new QAction(tr("Cb"), this);
  cbAct->setShortcut(tr("Alt+7"));
  cbAct->setData(PREVIEW_CB);
  cbAct->setCheckable(true);

  crAct = new QAction(tr("Cr"), this);
  crAct->setShortcut(tr("Alt+8"));
  crAct->setData(PREVIEW_CR);
  crAct->setCheckable(true);

  channelGroup = new QActionGroup(this);
  channelGroup->addAction(rgbAct);
  channelGroup->addAction(yccAct);
  channelGroup->addAction(rAct);
  channelGroup->addAction(gAct);
  channelGroup->addAction(bAct);
  channelGroup->addAction(yAct);
  channelGroup->addAction(cbAct);
  channelGroup->addAction(crAct);
  connect(channelGroup, SIGNAL(triggered(QAction*)), imgWindow, SLOT(setPreviewTitle(QAction*)));
  connect(channelGroup, SIGNAL(triggered(QAction*)), m_pImgDec, SLOT(setPreviewMode(QAction*)));

  // Image Zoom
  // Theme names for zoom (these asked for "document-print", which the macOS
  // theme turns into a printer symbol)
  const QIcon zoomInIcon = QIcon::fromTheme("zoom-in", QIcon(rsrcPath + "/zoomin.png"));
  zoomInAct = new QAction(zoomInIcon, tr("Zoom In"), this);
  zoomInAct->setData(PRV_ZOOM_IN);
  zoomInAct->setShortcuts(QKeySequence::ZoomIn);
  fileToolBar->addAction(zoomInAct);

  const QIcon zoomOutIcon = QIcon::fromTheme("zoom-out", QIcon(rsrcPath + "/zoomout.png"));
  zoomOutAct = new QAction(zoomOutIcon, tr("Zoom Out"), this);
  zoomOutAct->setData(PRV_ZOOM_OUT);
  zoomOutAct->setShortcuts(QKeySequence::ZoomOut);
  fileToolBar->addAction(zoomOutAct);

  zoomInOutGroup = new QActionGroup(this);
  zoomInOutGroup->addAction(zoomInAct);
  zoomInOutGroup->addAction(zoomOutAct);
  connect(zoomInOutGroup, SIGNAL(triggered(QAction*)), imgWindow, SLOT(zoom(QAction*)));

  zoom12_5Act = new QAction(tr("12.5%"), this);
  zoom12_5Act->setData(PRV_ZOOM_12);
  zoom12_5Act->setCheckable(true);
  zoom12_5Act->setChecked(true);

  zoom25Act = new QAction(tr("25%"), this);
  zoom25Act->setData(PRV_ZOOM_25);
  zoom25Act->setCheckable(true);

  zoom50Act = new QAction(tr("50%"), this);
  zoom50Act->setData(PRV_ZOOM_50);
  zoom50Act->setCheckable(true);

  zoom100Act = new QAction(tr("100%"), this);
  zoom100Act->setData(PRV_ZOOM_100);
  zoom100Act->setCheckable(true);

  zoom150Act = new QAction(tr("150%"), this);
  zoom150Act->setData(PRV_ZOOM_150);
  zoom150Act->setCheckable(true);

  zoom200Act = new QAction(tr("200%"), this);
  zoom200Act->setData(PRV_ZOOM_200);
  zoom200Act->setCheckable(true);

  zoom300Act = new QAction(tr("300%"), this);
  zoom300Act->setData(PRV_ZOOM_300);
  zoom300Act->setCheckable(true);

  zoom400Act = new QAction(tr("400%"), this);
  zoom400Act->setData(PRV_ZOOM_400);
  zoom400Act->setCheckable(true);

  zoom500Act = new QAction(tr("800%"), this);
  zoom500Act->setData(PRV_ZOOM_800);
  zoom500Act->setCheckable(true);

  zoomGroup = new QActionGroup(this);
  zoomGroup->addAction(zoom12_5Act);
  zoomGroup->addAction(zoom25Act);
  zoomGroup->addAction(zoom50Act);
  zoomGroup->addAction(zoom100Act);
  zoomGroup->addAction(zoom150Act);
  zoomGroup->addAction(zoom200Act);
  zoomGroup->addAction(zoom300Act);
  zoomGroup->addAction(zoom400Act);
  zoomGroup->addAction(zoom500Act);
  connect(zoomGroup, SIGNAL(triggered(QAction*)), imgWindow, SLOT(zoom(QAction*)));

  mcuGridAct = new QAction(tr("MCU Grid"), this);
  mcuGridAct->setShortcut(tr("Ctrl+G"));

  toolbarAct = new QAction(tr("&Toolbar"), this);
  toolbarAct->setCheckable(true);
  toolbarAct->setChecked(true);

  statusBarAct = new QAction(tr("&Status Bar"), this);
  statusBarAct->setCheckable(true);
  statusBarAct->setChecked(true);

  // Tools
  imgSearchFwdAct = new QAction(tr("Image Search Fwd"), this);
  imgSearchFwdAct->setShortcut(tr("Ctrl+2"));
  imgSearchFwdAct->setEnabled(false);

  imgSearchRevAct = new QAction(tr("Image Search Rev"), this);
  imgSearchRevAct->setShortcut(tr("Ctrl+1"));
  imgSearchRevAct->setEnabled(false);

  lookupMcuOffAct = new QAction(tr("Lookup MCU Offset..."), this);
  lookupMcuOffAct->setEnabled(false);

  fileOverlayAct = new QAction(tr("File Overlay..."), this);
  fileOverlayAct->setEnabled(false);

  searchDqtAct = new QAction(tr("Search Executable for DQT..."), this);

  exportJpegAct = new QAction(tr("Export JPEG..."), this);
  exportJpegAct->setEnabled(false);
  connect(exportJpegAct, &QAction::triggered, this, &MainWindow::onExportJpeg);

  exportTiffAct = new QAction(tr("Export TIFF..."), this);
  exportTiffAct->setEnabled(false);
  connect(exportTiffAct, &QAction::triggered, this, &MainWindow::onExportTiff);

  addToDbAct = new QAction(tr("Add Camera/SW to DB..."), this);
  addToDbAct->setShortcut(tr("Alt+S"));
  addToDbAct->setEnabled(false);

  manageLocalDbAct = new QAction(tr("Manage Local DB..."), this);

  // Options
  dhtExpandAct = new QAction(tr("DHT Expand"), this);
  dhtExpandAct->setCheckable(true);
  dhtExpandAct->setChecked(m_pAppConfig->expandDht());
  connect(dhtExpandAct, SIGNAL(triggered()), m_pAppConfig, SLOT(onOptionsDhtexpand()));

  hideUnknownTagsAct = new QAction(tr("Hide Unknown EXIF Tags"), this);
  hideUnknownTagsAct->setCheckable(true);
  hideUnknownTagsAct->setChecked(m_pAppConfig->hideUnknownExif());
  connect(hideUnknownTagsAct, SIGNAL(triggered()), m_pAppConfig, SLOT(onOptionsHideuknownexiftags()));

  makerNotesAct = new QAction(tr("Maker Notes"), this);
  makerNotesAct->setCheckable(true);
  makerNotesAct->setChecked(m_pAppConfig->decodeMaker());
  connect(makerNotesAct, SIGNAL(triggered()), m_pAppConfig, SLOT(onOptionsMakernotes()));

  sigSearchAct = new QAction(tr("Signature Search"), this);
  sigSearchAct->setCheckable(true);
  sigSearchAct->setChecked(m_pAppConfig->searchSig());
  connect(sigSearchAct, SIGNAL(triggered()), m_pAppConfig, SLOT(onOptionsSignaturesearch()));

  relaxedParseAct = new QAction(tr("Relaxed Parsing"), this);
  relaxedParseAct->setCheckable(true);
  relaxedParseAct->setChecked(m_pAppConfig->relaxedParsing());
  connect(relaxedParseAct, SIGNAL(triggered()), m_pAppConfig, SLOT(onOptionsRelaxedparsing()));

  configAct = new QAction(tr("Configuration..."), this);
  // On macOS this is the app menu's Preferences item (Cmd+,), as Qt's text
  // heuristic would guess anyway; say so explicitly, with the standard shortcut
  configAct->setMenuRole(QAction::PreferencesRole);
  configAct->setShortcuts(QKeySequence::Preferences);
  connect(configAct, &QAction::triggered, this, &MainWindow::onConfig);

  checkUpdatesAct = new QAction(tr("Check for Updates..."), this);

  // Scan Segment menu
  decodeImageAct = new QAction(tr("Decode Image"), this);
  decodeImageAct->setCheckable(true);
  decodeImageAct->setChecked(m_pAppConfig->decodeImage());
  connect(decodeImageAct, SIGNAL(triggered()), m_pAppConfig, SLOT(onScansegmentDecodeImage()));

  fullIdtcAct = new QAction(tr("Full IDCT (AC+DC - slow)"), this);;
  fullIdtcAct->setCheckable(true);
  connect(fullIdtcAct, SIGNAL(triggered()), m_pAppConfig, SLOT(onScansegmentFullidct()));

  noIdtcAct = new QAction(tr("No IDCT (DC only - fast)"), this);;
  noIdtcAct->setCheckable(true);
  connect(noIdtcAct, SIGNAL(triggered()), m_pAppConfig, SLOT(onScansegmentNoidct()));

  if(m_pAppConfig->decodeAc())
  {
    fullIdtcAct->setChecked(true);
  }
  else
  {
    noIdtcAct->setChecked(true);
  }

  idtGroup = new QActionGroup(this);
  idtGroup->addAction(noIdtcAct);
  idtGroup->addAction(fullIdtcAct);

  histogramRgbAct = new QAction(tr("Histogram RGB"), this);;
  histogramRgbAct->setCheckable(true);
  histogramRgbAct->setChecked(m_pAppConfig->displayRgbHistogram());
  connect(histogramRgbAct, SIGNAL(triggered()), m_pAppConfig, SLOT(onScansegmentHistogram()));

  histogramYAct = new QAction(tr("Histogram Y"), this);;
  histogramYAct->setCheckable(true);
  histogramYAct->setChecked(m_pAppConfig->displayYHistogram());
  connect(histogramYAct, SIGNAL(triggered()), m_pAppConfig, SLOT(onScansegmentHistogramy()));

  dumpAct = new QAction(tr("Dump"), this);;
  dumpAct->setCheckable(true);
  dumpAct->setChecked(m_pAppConfig->scanDump());
  connect(dumpAct, SIGNAL(triggered()), m_pAppConfig, SLOT(onScansegmentDump()));

  detailedDecodeAct = new QAction(tr("Detailed Decode..."), this);
  connect(detailedDecodeAct, &QAction::triggered, this, &MainWindow::onDetailedDecode);

  // Help menu
  aboutAct = new QAction(tr("&About JPEGsnoop..."), this);

  if(DEBUG_EN)
    m_pAppConfig->DebugLogAdd("MainWindow::createActions() End");
}

//-----------------------------------------------------------------------------

void MainWindow::createMenus(void)
{
  if(DEBUG_EN)
    m_pAppConfig->DebugLogAdd("MainWindow::createMenus() Begin");

  fileMenu = menuBar()->addMenu("&File");
  fileMenu->addAction(openAct);
  fileMenu->addAction(saveLogAct);
  fileMenu->addAction(reprocessAct);
  fileMenu->addSeparator();
  fileMenu->addAction(OffsetAct);
  fileMenu->addSeparator();
  fileMenu->addAction(printAct);
  fileMenu->addAction(printPreviewAct);
  fileMenu->addAction(printSetupAct);
  fileMenu->addSeparator();
  fileMenu->addAction(recentFileAct);
  fileMenu->addSeparator();
  fileMenu->addAction(exitAct);

  editMenu = menuBar()->addMenu("&Edit");
  editMenu->addAction(undoAct);
  editMenu->addSeparator();
  editMenu->addAction(copyAct);
  editMenu->addAction(selectAllAct);
  editMenu->addSeparator();
  editMenu->addAction(findAct);
  editMenu->addAction(findNextAct);

  viewMenu = menuBar()->addMenu("&View");
  imageChanMenu = viewMenu->addMenu(tr("Image Chan"));
  imageZoomMenu = viewMenu->addMenu(tr("Image Zoom"));
  overlayMenu = viewMenu->addMenu(tr("Overlays"));
  viewMenu->addAction(adjustAct);
  viewMenu->addSeparator();
  viewMenu->addAction(toolbarAct);
  viewMenu->addAction(statusBarAct);

  imageChanMenu->addAction(rgbAct);
  imageChanMenu->addAction(yccAct);
  imageChanMenu->addSeparator();
  imageChanMenu->addAction(rAct);
  imageChanMenu->addAction(gAct);
  imageChanMenu->addAction(bAct);
  imageChanMenu->addSeparator();
  imageChanMenu->addAction(yAct);
  imageChanMenu->addAction(cbAct);
  imageChanMenu->addAction(crAct);

  imageZoomMenu->addAction(zoomInAct);
  imageZoomMenu->addAction(zoomOutAct);
  imageZoomMenu->addSeparator();
  imageZoomMenu->addAction(zoom12_5Act);
  imageZoomMenu->addAction(zoom25Act);
  imageZoomMenu->addAction(zoom50Act);
  imageZoomMenu->addAction(zoom100Act);
  imageZoomMenu->addAction(zoom150Act);
  imageZoomMenu->addAction(zoom200Act);
  imageZoomMenu->addAction(zoom300Act);
  imageZoomMenu->addAction(zoom400Act);
  imageZoomMenu->addAction(zoom500Act);

  overlayMenu->addAction(mcuGridAct);

  toolsMenu = menuBar()->addMenu("&Tools");
  toolsMenu->addAction(imgSearchFwdAct);
  toolsMenu->addAction(imgSearchRevAct);
  toolsMenu->addSeparator();
  toolsMenu->addAction(lookupMcuOffAct);
  toolsMenu->addAction(fileOverlayAct);
  toolsMenu->addAction(searchDqtAct);
  toolsMenu->addAction(exportJpegAct);
  toolsMenu->addAction(exportTiffAct);
  toolsMenu->addSeparator();
  toolsMenu->addAction(addToDbAct);
  toolsMenu->addAction(manageLocalDbAct);

  optionsMenu = menuBar()->addMenu("&Options");
  optionsMenu->addAction(dhtExpandAct);
  optionsMenu->addAction(hideUnknownTagsAct);
  optionsMenu->addAction(makerNotesAct);
  optionsMenu->addAction(sigSearchAct);
  optionsMenu->addAction(relaxedParseAct);
  optionsMenu->addSeparator();
  scanSegMenu = optionsMenu->addMenu(tr("Scan Segment"));
  optionsMenu->addSeparator();
  optionsMenu->addAction(configAct);
  optionsMenu->addAction(checkUpdatesAct);

  // Scan Segment menu
  scanSegMenu->addAction(decodeImageAct);
  scanSegMenu->addSeparator();
  scanSegMenu->addAction(fullIdtcAct);
  scanSegMenu->addAction(noIdtcAct);
  scanSegMenu->addSeparator();
  scanSegMenu->addAction(histogramRgbAct);
  scanSegMenu->addAction(histogramYAct);
  scanSegMenu->addSeparator();
  scanSegMenu->addAction(dumpAct);
  scanSegMenu->addAction(detailedDecodeAct);

  // Help menu
  helpMenu = menuBar()->addMenu(tr("&Help"));
  helpMenu->addAction(aboutAct);
}

//-----------------------------------------------------------------------------

void MainWindow::open()
{
  QFileDialog dialog(this);

  QStringList filters;
  filters << "JPEG Image (*.jpg *.jpeg)"
          << "Thumbnail (*.thm)"
          << "QuickTime Movie (*.mov)"
          << "Digital Negative (*.dng)"
          << "RAW Image (*.crw *.cr2 *.nef *.orf *.pef)"
          << "PDF Files (*.pdf)"
          << "Photoshop Files (*.psd)"
          << "All Files (*)";
  dialog.setNameFilters(filters);
  dialog.setFileMode(QFileDialog::ExistingFile);
  dialog.setOption(QFileDialog::ReadOnly);
  //dialog.setDirectory(...); // TODO: Preserve last directory

  if(dialog.exec())
  {
    openFile(dialog.selectedFiles().at(0));
  }
}

//-----------------------------------------------------------------------------
// Open and analyze a file (File > Open, and the test driver)

void MainWindow::openFile(const QString &strPath)
{
  m_currentFile = strPath;

  // A new file means a new image source: without this the scan decoder keeps
  // the previous file's state and skips decoding (master did this in
  // CJPEGsnoopDoc::Reset() on every open).
  emit ImgSrcChanged();

  AnalyzeFile();
}

//-----------------------------------------------------------------------------

// Clear the preview and histograms before showing a file's. They were only
// replaced when the new file decoded, so after an undecodable file (such as a
// progressive JPEG), an empty file or a failed open the previous file's stayed.
// The decoder forgets its image too, or a zoom or channel change drew it
// again. (AnalyzeFile()'s callers mark the image source changed first, so the
// next file is always decoded afresh.)
void MainWindow::clearImageViews()
{
  m_pImgDec->ClearDecodedImage();
  imgWindow->clearImage();
  rgbHisto->clear();
  yHisto->clear();
}

void MainWindow::AnalyzeFile()
{
  clearImageViews();

  // Nothing is analyzed until this file is (a failed open left the previous
  // file's flag set)
  m_bFileAnalyzed = false;

  if(AnalyzeOpen())
  {
    enableMenus();
    AnalyzeFileDo();
    AnalyzeClose();
  }
}

//-----------------------------------------------------------------------------
// Open m_currentFile and point the file buffer at it
//
// RETURN:
// - False, with the reason in the report, if the file can't be opened or is
//   empty
//
// POST:
// - m_pFile, m_lFileSize, m_bFileOpened
//
bool MainWindow::AnalyzeOpen()
{
  // One QFile per analysis; free the previous one (this used to leak per open)
  delete m_pFile;
  m_pFile = new QFile(m_currentFile);

  if(m_pFile->open(QIODevice::ReadOnly) == false)
  {
    QString strError;

    // Note: msg includes m_strPathName. Clear the previous file's report
    // first, as for an empty file: the error was added to the end of it.
    strError = QString("ERROR: Couldn't open file: [%1]").arg(m_currentFile);
    m_pDocLog->Clear();
    m_pDocLog->AddLineErr(strError);

    if(m_pAppConfig->interactive())
    {
      msgBox.setText(strError);
      msgBox.exec();
    }

    delete m_pFile;
    m_pFile = nullptr;
    return false;
  }

  // Set the file size variable
  m_lFileSize = m_pFile->size();

  // Don't attempt to load buffer with zero length file!
  if(m_lFileSize <= 0)
  {
    // Say so, as master did, rather than leaving the previous file's report
    m_pDocLog->Clear();
    m_pDocLog->AddLineErr(QString("ERROR: File length is zero, no decoding done. [%1]").arg(m_currentFile));
    m_pFile->close();
    return false;
  }

  // Open up the buffer
  m_pWBuf->BufFileSet(m_pFile);
  m_pWBuf->BufLoadWindow(0);

  // Mark file as opened
  m_bFileOpened = true;
  return true;
}

//-----------------------------------------------------------------------------

void MainWindow::AnalyzeFileDo()
{
  if(DEBUG_EN)
    m_pAppConfig->DebugLogAdd("MainWindow::AnalyzeFileDo() Start");

  // Ensure file was already opened
  Q_ASSERT(m_pFile);
  Q_ASSERT(m_bFileOpened);

  // Reset the analyzed state in case this file is invalid (eg. zero length)
  m_bFileAnalyzed = false;

  // Also reached by Reprocess, without AnalyzeFile()
  clearImageViews();

  // Clear the document log
  m_pDocLog->Clear();

  m_pDocLog->AddLine("");
  m_pDocLog->AddLine(QString("JPEGsnoop %1 by Calvin Hass").arg(QString(VERSION_STR)));
  m_pDocLog->AddLine("  http://www.impulseadventure.com/photo/");
  m_pDocLog->AddLine("  -------------------------------------");
  m_pDocLog->AddLine("");
  m_pDocLog->AddLine(QString("  Filename: [%1]").arg(m_currentFile));
  m_pDocLog->AddLine(QString("  Filesize: [%1] bytes").arg(m_lFileSize));
  m_pDocLog->AddLine("");

  // Perform the actual decoding
  if(m_lFileSize > 0xFFFFFFFF)
  {
    m_pDocLog->AddLineErr(QString("ERROR: File too large for this version of JPEGsnoop. [Size=0x%1]").arg(m_lFileSize));
  }
  else if(m_lFileSize == 0)
  {
    m_pDocLog->AddLineErr("ERROR: File length is zero, no decoding done.");
  }
  else
  {
    if(DEBUG_EN)
      m_pAppConfig->DebugLogAdd("CJPEGsnoopCore::AnalyzeFileDo() Checkpoint 2");

    m_pJfifDec->ProcessFile(m_pFile);

    if(m_pAppConfig->displayRgbHistogram() && m_pImgDec->isRgbHistogramReady())
    {
      showHistogram(rgbHisto, *m_pImgDec->rgbHistogram());
    }

    if(m_pAppConfig->displayYHistogram() && m_pImgDec->isRgbHistogramReady())
    {
      showHistogram(yHisto, *m_pImgDec->yHistogram());
    }

    qDebug() << QString("MainWindow::AnalyzeFileDo() DibTempReady=%1 PreviewIsJpeg=%2")
                .arg(m_pImgDec->m_bDibTempReady)
                .arg(m_pImgDec->m_bPreviewIsJpeg);
    // A JPEG scan or a Photoshop file's image (m_bPreviewIsJpeg is false
    // for the latter, which used to keep it from being shown)
    if(m_pImgDec->m_bDibTempReady)
    {
      imgWindow->drawImage();
    }

    // Now indicate that the file has been processed
    m_bFileAnalyzed = true;
  }
}

//-----------------------------------------------------------------------------

void MainWindow::updateImage()
{
  imgWindow->drawImage();
}

//-----------------------------------------------------------------------------
// Close the current file
// - Invalidate the buffer
//
// POST:
// - m_bFileOpened
//
void MainWindow::AnalyzeClose()
{
  // Indicate no file is currently opened
  // Note that we don't clear m_bFileAnalyzed as the last
  // decoder state is still available
  m_bFileOpened = false;

  // Close the buffer window
  m_pWBuf->BufFileUnset();

  m_pFile->close();
}

void MainWindow::saveLog()
{
  QFileDialog dialog(this);

  QStringList filters;
  filters << "Text File (*.txt)";

  QFileInfo fileInfo(m_currentFile);

  dialog.setWindowModality(Qt::WindowModal);
  dialog.setAcceptMode(QFileDialog::AcceptSave);
  dialog.setNameFilters(filters);
  // Default: <image folder>/<image name>.txt (absoluteFilePath() made it .../photo.jpg/photo.txt)
  dialog.selectFile(fileInfo.absolutePath() + "/" + fileInfo.completeBaseName() + ".txt");

  if (dialog.exec() != QDialog::Accepted)
      return;

//  return saveFile(dialog.selectedFiles().first());
  QFile file(dialog.selectedFiles().first());

     if (!file.open(QFile::WriteOnly | QFile::Text))
     {
         QMessageBox::warning(this, tr("Application"),
                              tr("Cannot write file %1:\n%2.")
                              .arg(QDir::toNativeSeparators(dialog.selectedFiles().first()),
                                   file.errorString()));
         return;
     }

     QTextStream out(&file);
     out << textEdit->toPlainText();

  file.close();
}

//-----------------------------------------------------------------------------

void MainWindow::enableMenus()
{
  saveLogAct->setEnabled(true);
  reprocessAct->setEnabled(true);
  selectAllAct->setEnabled(true);
  findAct->setEnabled(true);
  imgSearchFwdAct->setEnabled(true);
  imgSearchRevAct->setEnabled(true);
  lookupMcuOffAct->setEnabled(true);
  fileOverlayAct->setEnabled(true);
  searchDqtAct->setEnabled(true);
  exportJpegAct->setEnabled(true);
  exportTiffAct->setEnabled(true);
  addToDbAct->setEnabled(true);
  manageLocalDbAct->setEnabled(true);
}

//-----------------------------------------------------------------------------

void MainWindow::onConfig()
{
  // Modal and on the stack: one dialog at a time, nothing left behind.
  // Save applies the settings (SnoopConfigDialog::onSave).
  SnoopConfigDialog dlg(m_pAppConfig, this);
  dlg.exec();
}

//-----------------------------------------------------------------------------

void MainWindow::filePrint()
{
  QPrinter printer(QPrinter::HighResolution);
  QPrintDialog *dlg = new QPrintDialog(&printer, this);

  if (textEdit->textCursor().hasSelection())
  {
    dlg->setOption(QAbstractPrintDialog::PrintSelection);
  }

  dlg->setWindowTitle(tr("Print Document"));

  if (dlg->exec() == QDialog::Accepted)
  {
    textEdit->print(&printer);
  }

  delete dlg;
}

//-----------------------------------------------------------------------------

void MainWindow::reprocess()
{
  // Auto-reprocess fires on option changes even before any file is opened
  if(m_currentFile.isEmpty())
  {
    return;
  }

  // Show coach message once
  if(m_pAppConfig->decodeImage() &&	m_pAppConfig->coachIdct())
  {
    // Show the coaching dialog
    QString msg;

    if(m_pAppConfig->decodeAc())
    {
      msg = strHiRes;
    }
    else
    {
      msg = strLoRes;
    }

    // On the stack: a new dialog per tip used to leak
    Q_Note note(msg);
    note.exec();
    m_pAppConfig->setCoachIdct(!note.hideStatus());
  }

  emit ImgSrcChanged();

  textEdit->clear();
  AnalyzeFile();

  // TODO: Handle AVI
}

//-----------------------------------------------------------------------------

void MainWindow::filePrintPreview()
{
  QPrinter printer(QPrinter::HighResolution);
  QPrintPreviewDialog preview(&printer, this);
  connect(&preview, &QPrintPreviewDialog::paintRequested, this, &MainWindow::printPreview);
  preview.exec();
}

//-----------------------------------------------------------------------------

void MainWindow::printPreview(QPrinter *printer)
{
  textEdit->print(printer);
}

//-----------------------------------------------------------------------------
// Tools > Export JPEG: copy the JPEG at the start offset to its own file, or
// with Extract All, every JPEG in the file (master's DoGuiExtractEmbeddedJPEG)

// Extract All's names: the name chosen, without its extension, then the
// JPEG's number (from 1, in 6 digits as master: a 60 fps video's frames for
// over 4.5 hours), then ".jpg"
static QString numberedExportName(const QString &strOut, uint32_t nNum)
{
  const QFileInfo fiOut(strOut);
  return QDir(fiOut.path()).filePath(QString("%1.%2.jpg").arg(fiOut.completeBaseName()).arg(nNum, 6, 10, QChar('0')));
}

// The files already named as Extract All would name its exports. (Without
// the case: macOS disks usually ignore it, so "x.000001.JPG" would be
// replaced too.)
static QStringList existingNumberedExports(const QString &strOut)
{
  const QFileInfo fiOut(strOut);
  const QRegularExpression reNumbered(QString("^%1\\.[0-9]{6,}\\.jpg$").arg(QRegularExpression::escape(fiOut.completeBaseName())),
                                      QRegularExpression::CaseInsensitiveOption);
  QStringList astrExisting;
  const QStringList astrNames = QDir(fiOut.path()).entryList(QDir::Files | QDir::Hidden, QDir::Name);

  for(const QString &strName : astrNames)
  {
    if(reNumbered.match(strName).hasMatch())
      astrExisting << strName;
  }

  return astrExisting;
}

void MainWindow::onExportJpeg()
{
  if(m_currentFile.isEmpty())
  {
    return;
  }

  // The menu stays enabled after a file that couldn't be opened
  if(!m_bFileAnalyzed)
  {
    QMessageBox::warning(this, "JPEGsnoop", tr("The file shown couldn't be analyzed, so there is nothing to export."));
    return;
  }

  // Motion JPEG frames usually leave out their Huffman tables, so add the
  // standard ones for them (only those a frame lacks)
  bool bIsAvi = false;
  bool bIsMjpeg = false;
  m_pJfifDec->GetAviMode(bIsAvi, bIsMjpeg);

  ExportJpegSettings sInit;
  sInit.bDhtAviInsert = bIsMjpeg;

  // Master passed an empty string here, so the dialog showed no offset
  ExportJpegDialog dlgSettings(m_pAppConfig->startPos(), sInit, this);

  if(dlgSettings.exec() != QDialog::Accepted)
  {
    return;
  }

  const ExportJpegSettings sSettings = dlgSettings.settings();

  QFileDialog dialog(this, sSettings.bExtractAll ? tr("Save Exported JPEG files as") : tr("Save Exported JPEG file as"));
  dialog.setAcceptMode(QFileDialog::AcceptSave);
  dialog.setNameFilters(QStringList() << tr("JPEG Image (*.jpg)") << tr("All Files (*)"));
  dialog.setDefaultSuffix("jpg");
  dialog.selectFile(QFileInfo(m_currentFile).absoluteFilePath() + ".export.jpg");

  // Extract All doesn't write the name chosen, only numbered names made from
  // it, so asking about that one would be asking about the wrong file. The
  // numbered names are checked below instead.
  if(sSettings.bExtractAll)
  {
    dialog.setOption(QFileDialog::DontConfirmOverwrite);
  }

  if((dialog.exec() != QDialog::Accepted) || dialog.selectedFiles().isEmpty())
  {
    return;
  }

  const QString strOut = dialog.selectedFiles().first();

  // They were replaced without asking, even when the name chosen was new
  if(sSettings.bExtractAll)
  {
    const QStringList astrExisting = existingNumberedExports(strOut);

    if(!astrExisting.isEmpty())
    {
      const QString strFound =
        (astrExisting.size() == 1)
          ? tr("A file with a name Extract All uses already exists: \"%1\".").arg(astrExisting.first())
          : tr("%1 files with names Extract All uses already exist, such as \"%2\".")
              .arg(astrExisting.size()).arg(astrExisting.first());
      QMessageBox msgReplace(QMessageBox::Warning, "JPEGsnoop",
                             strFound + " " + tr("Each one whose number it uses is replaced, and the others are left "
                                                 "as they are."),
                             QMessageBox::Cancel, this);
      msgReplace.setInformativeText(tr("Replace them?"));
      QPushButton *pReplace = msgReplace.addButton(tr("Replace"), QMessageBox::DestructiveRole);
      msgReplace.setDefaultButton(QMessageBox::Cancel);
      msgReplace.exec();

      if(msgReplace.clickedButton() != pReplace)
      {
        return;
      }
    }
  }

  QApplication::setOverrideCursor(Qt::WaitCursor);
  exportJpeg(strOut, sSettings);
  QApplication::restoreOverrideCursor();
}

//-----------------------------------------------------------------------------
// Export the JPEG at the start offset (or with bExtractAll, every JPEG) to
// strOut. The report then shows the file, analyzed again, and the export.
//
// RETURN:
// - True if a file was written
//
bool MainWindow::exportJpeg(const QString &strOut, const ExportJpegSettings &sSettings)
{
  if(m_currentFile.isEmpty() || !m_bFileAnalyzed)
  {
    return false;
  }

  // Analyze the file again first, as master did: the export copies the
  // marker positions this finds, and the file may have changed since it was
  // opened. (AnalyzeFileDo() clears the preview, so the image is decoded
  // again too, as for Reprocess.)
  emit ImgSrcChanged();
  clearImageViews();
  m_bFileAnalyzed = false;

  if(!AnalyzeOpen())
  {
    return false;
  }

  bool bOk = false;

  if(sSettings.bExtractAll)
  {
    bOk = exportAllJpegs(strOut, sSettings);
  }
  else
  {
    AnalyzeFileDo();

    bOk = m_bFileAnalyzed &&
          m_pJfifDec->ExportJpegPrepare(m_currentFile, sSettings.bForceSoi, sSettings.bForceEoi, sSettings.bIgnoreEoi) &&
          m_pJfifDec->ExportJpegDo(m_currentFile, strOut, static_cast<uint32_t>(m_lFileSize), sSettings.bOverlayEn,
                                   sSettings.bDhtAviInsert, sSettings.bForceSoi, sSettings.bForceEoi);
  }

  AnalyzeClose();
  return bOk;
}

//-----------------------------------------------------------------------------
// Extract All: export the JPEG at each SOI (FF D8 FF) in the file, from its
// start, to <strOut without extension>.000001.jpg, .000002.jpg, ...,
// replacing any files of those names (onExportJpeg() asks first). Then
// show the file's report, from the start offset as before, followed by a
// list of what was exported.
//
// Master left the report of the last place it looked and moved the start
// offset there. Each place is analyzed with the report and message boxes
// off, as a video can have thousands of frames.
//
// PRE:
// - AnalyzeOpen()
//
bool MainWindow::exportAllJpegs(const QString &strOut, const ExportJpegSettings &sSettings)
{
  const uint32_t nStartSaved = m_pAppConfig->startPos();
  const bool bInteractiveSaved = m_pAppConfig->interactive();

  QStringList astrResults;
  QList<bool> abResultOk;
  uint32_t nPos = 0;
  uint32_t nExported = 0;
  bool bWriteOk = true;
  QString strWriteError;

  m_pDocLog->Disable();
  m_pAppConfig->setInteractive(false);

  for(bool bMore = true; bMore;)
  {
    m_StatusBar->showMessage(tr("Exporting all JPEGs: %1 exported...").arg(nExported));

    // Reload the buffer as AnalyzeOpen() does: reading past the end of the
    // file (as the last analysis or search may have) leaves it marked bad,
    // and the next scan then stops at once ("Ran out of buffer")
    m_pWBuf->BufLoadWindow(nPos);
    m_pAppConfig->setStartPos(nPos);
    emit ImgSrcChanged();
    m_pJfifDec->ProcessFile(m_pFile);

    const QString strPos = QString("0x%1").arg(nPos, 8, 16, QChar('0'));

    if(!m_pJfifDec->ExportJpegPrepare(m_currentFile, sSettings.bForceSoi, sSettings.bForceEoi, sSettings.bIgnoreEoi))
    {
      astrResults << QString("  [%1] Not exported: %2").arg(strPos, m_pJfifDec->GetExportError());
      abResultOk << false;
    }
    else
    {
      const QString strName = numberedExportName(strOut, nExported + 1);

      if(!m_pJfifDec->ExportJpegDo(m_currentFile, strName, static_cast<uint32_t>(m_lFileSize), sSettings.bOverlayEn,
                                   sSettings.bDhtAviInsert, sSettings.bForceSoi, sSettings.bForceEoi))
      {
        // The next would fail the same way
        strWriteError = m_pJfifDec->GetExportError();
        astrResults << QString("  [%1] %2").arg(strPos, strWriteError);
        abResultOk << false;
        bWriteOk = false;
        break;
      }

      nExported++;
      astrResults << QString("  [%1] Exported to [%2]").arg(strPos, strName);
      abResultOk << true;
    }

    // The next SOI after this one (the search starts after nPos)
    bMore = m_pWBuf->BufSearch(nPos, 0xFFD8FF, 3, true, nPos);
  }

  m_pAppConfig->setInteractive(bInteractiveSaved);
  m_pAppConfig->setStartPos(nStartSaved);
  m_pDocLog->Enable();

  // The file as it was shown before, then what was exported
  m_pWBuf->BufLoadWindow(0);
  emit ImgSrcChanged();
  AnalyzeFileDo();

  m_pDocLog->AddLine("");
  m_pDocLog->AddLineHdr("*** Exporting All JPEGs ***");
  m_pDocLog->AddLine(QString("  Exporting from: [%1]").arg(m_currentFile));

  for(qsizetype nInd = 0; nInd < astrResults.size(); nInd++)
  {
    if(abResultOk.at(nInd))
      m_pDocLog->AddLine(astrResults.at(nInd));
    else
      m_pDocLog->AddLineWarn(astrResults.at(nInd));
  }

  m_pDocLog->AddLine(QString("  Exported %1 JPEG%2").arg(nExported).arg((nExported == 1) ? "" : "s"));

  if(!bWriteOk && bInteractiveSaved)
  {
    QMessageBox::warning(this, "JPEGsnoop", strWriteError);
  }

  m_StatusBar->showMessage(tr("Exported %1 JPEG%2").arg(nExported).arg((nExported == 1) ? "" : "s"));
  return bWriteOk && (nExported > 0);
}

//-----------------------------------------------------------------------------
// Tools > Export TIFF: save the decoded image as a TIFF file

// Is there a decoded image (a JPEG scan or a Photoshop file's) to export?
bool MainWindow::canExportTiff()
{
  return m_pImgDec->m_bDibTempReady && m_pImgDec->m_pDibTemp && !m_pImgDec->m_pDibTemp->isNull();
}

// Are there decoded Y, Cb and Cr values (a JPEG scan's) to export?
bool MainWindow::canExportYcc()
{
  uint32_t nSizeX = 0;
  uint32_t nSizeY = 0;
  return canExportTiff() && m_pImgDec->GetFrameSize(nSizeX, nSizeY);
}

void MainWindow::onExportTiff()
{
  // Master exported whatever its bitmap pointer held: nothing, or the
  // previous file's image
  if(!canExportTiff())
  {
    QMessageBox::warning(this, "JPEGsnoop",
                         tr("There is no decoded image to export. (Images are decoded when Options > Scan Segment > "
                            "Decode Image is on.)"));
    return;
  }

  QFileDialog dialog(this, tr("Output TIFF Filename"));
  dialog.setAcceptMode(QFileDialog::AcceptSave);
  dialog.setNameFilters(QStringList() << tr("TIFF (*.tif)") << tr("All Files (*)"));
  dialog.setDefaultSuffix("tif");
  dialog.selectFile(QFileInfo(m_currentFile).absoluteFilePath() + ".tif");

  if((dialog.exec() != QDialog::Accepted) || dialog.selectedFiles().isEmpty())
  {
    return;
  }

  const QString strOut = dialog.selectedFiles().first();

  // Master wrote the file even if this dialog was cancelled
  ExportTiffDialog dlgFormat(strOut, canExportYcc(), this);

  if(dlgFormat.exec() != QDialog::Accepted)
  {
    return;
  }

  QString strError;
  QApplication::setOverrideCursor(Qt::WaitCursor);
  const bool bOk = exportTiff(strOut, dlgFormat.format(), strError);
  QApplication::restoreOverrideCursor();

  if(!bOk)
  {
    QMessageBox::warning(this, "JPEGsnoop", QString("ERROR: %1").arg(strError));
  }
}

//-----------------------------------------------------------------------------
// Write the decoded image to strOut as a TIFF file, and say so in the report.
//
// RGB is the image as the preview shows it (so with the View > Image Chan
// choice, as in master); YCC is the decoded Y, Cb and Cr. A JPEG's image is
// its own size: the preview is padded to whole MCUs, and master exported the
// padding too. 16-bit RGB scales each value by 257, so white is 65535 (master
// shifted by 8, giving 65280).
//
// RETURN:
// - False, with the reason in strError, if nothing was written
//
bool MainWindow::exportTiff(const QString &strOut, FileTiff::teFormat eFormat, QString &strError)
{
  if(!canExportTiff())
  {
    strError = "There is no decoded image to export.";
    return false;
  }

  if((eFormat == FileTiff::FMT_YCC8) && !canExportYcc())
  {
    strError = "Only a decoded JPEG image can be exported as YCC.";
    return false;
  }

  if(SameFile(m_currentFile, strOut))
  {
    strError = "Can't overwrite source file.";
    return false;
  }

  const QImage img = m_pImgDec->m_pDibTemp->convertToFormat(QImage::Format_RGB32);
  uint32_t nSizeX = static_cast<uint32_t>(img.width());
  uint32_t nSizeY = static_cast<uint32_t>(img.height());
  uint32_t nFrameX = 0;
  uint32_t nFrameY = 0;

  if(m_pImgDec->GetFrameSize(nFrameX, nFrameY))
  {
    nSizeX = std::min(nSizeX, nFrameX);
    nSizeY = std::min(nSizeY, nFrameY);
  }

  const quint64 nDataSize = FileTiff::DataSize(nSizeX, nSizeY, eFormat);

  if(nDataSize == 0)
  {
    strError = QString("An image of %1 x %2 pixels is too large for a TIFF file.").arg(nSizeX).arg(nSizeY);
    return false;
  }

  QByteArray abSamples;

  try
  {
    abSamples.resize(static_cast<qsizetype>(nDataSize));
  }
  catch(const std::bad_alloc &)
  {
    strError = "Not enough memory for the export.";
    return false;
  }

  char *pOut = abSamples.data();

  for(uint32_t nY = 0; nY < nSizeY; nY++)
  {
    const QRgb *pLine = reinterpret_cast<const QRgb *>(img.constScanLine(static_cast<int>(nY)));

    for(uint32_t nX = 0; nX < nSizeX; nX++)
    {
      if(eFormat == FileTiff::FMT_YCC8)
      {
        int32_t anVal[3] = { 0, 0, 0 };
        m_pImgDec->GetPixelYcc(nX, nY, anVal[0], anVal[1], anVal[2]);

        // -1024..1023 (level - 128, times 8) to 0..255, as master did
        for(int32_t nVal : anVal)
        {
          *pOut++ = static_cast<char>((std::clamp(nVal, -1024, 1023) + 1024) >> 3);
        }
      }
      else
      {
        const int anVal[3] = { qRed(pLine[nX]), qGreen(pLine[nX]), qBlue(pLine[nX]) };

        for(int nVal : anVal)
        {
          if(eFormat == FileTiff::FMT_RGB16)
          {
            // Big-endian, as the file is
            const int nVal16 = nVal * 257;
            *pOut++ = static_cast<char>(nVal16 >> 8);
            *pOut++ = static_cast<char>(nVal16 & 0xFF);
          }
          else
          {
            *pOut++ = static_cast<char>(nVal);
          }
        }
      }
    }
  }

  Q_ASSERT(pOut == abSamples.data() + abSamples.size());

  m_pDocLog->AddLine("");
  m_pDocLog->AddLineHdr("*** Exporting TIFF ***");
  m_pDocLog->AddLine(QString("  Exporting to:   [%1]").arg(strOut));

  if(!FileTiff::WriteFile(strOut, eFormat, abSamples, nSizeX, nSizeY, strError))
  {
    m_pDocLog->AddLineErr(QString("  ERROR: %1").arg(strError));
    return false;
  }

  const char *pszFormat = (eFormat == FileTiff::FMT_RGB16) ? "RGB (16 bit)" :
                          (eFormat == FileTiff::FMT_YCC8) ? "YCC (8 bit)" : "RGB (8 bit)";
  m_pDocLog->AddLine(QString("  Format:         %1, %2 x %3 pixels").arg(pszFormat).arg(nSizeX).arg(nSizeY));
  m_pDocLog->AddLine("  Export done");
  m_StatusBar->showMessage(tr("Exported TIFF"), 5000);
  return true;
}

//-----------------------------------------------------------------------------

// The MCUs between the last two positions clicked on the image, first to
// last in decode order (or the one MCU clicked, if only one was). False if
// nothing has been clicked on this image.
bool MainWindow::detailRangeFromMarks(uint32_t &nMcuX, uint32_t &nMcuY, uint32_t &nMcuLen)
{
  const uint32_t nMarks = m_pImgDec->GetMarkerCount();

  if(!m_pImgDec->IsImgDecoded() || (nMarks == 0))
  {
    return false;
  }

  // Marks are 8x8 blocks: the MCU holding a block's top-left pixel
  auto markMcu = [this](uint32_t nInd) {
    const QPoint ptBlk = m_pImgDec->GetMarkerBlk(nInd);
    return m_pImgDec->PixelToMcu(QPoint(ptBlk.x() * BLK_SZ_X, ptBlk.y() * BLK_SZ_Y));
  };

  QPoint ptStart = markMcu(nMarks - 1);
  nMcuLen = 1;

  if(nMarks >= 2)
  {
    QPoint ptEnd = ptStart;
    ptStart = markMcu(nMarks - 2);

    // Clicked last to first: swap them. (Master swapped the positions for
    // the length but kept the first click as the start, so the range began
    // at the later MCU.)
    if(m_pImgDec->McuXyToLinear(ptStart) > m_pImgDec->McuXyToLinear(ptEnd))
    {
      std::swap(ptStart, ptEnd);
    }

    nMcuLen = m_pImgDec->McuXyToLinear(ptEnd) - m_pImgDec->McuXyToLinear(ptStart) + 1;
  }

  nMcuX = static_cast<uint32_t>(ptStart.x());
  nMcuY = static_cast<uint32_t>(ptStart.y());
  return true;
}

// Options > Scan Segment > Detailed Decode: choose the MCUs whose scan decode
// the report shows in detail (each variable-length code, the coefficients of
// each block and the first MCU's pixels). It applies from the next decode,
// which Automatic Reprocessing starts at once. (The menu item did nothing.)
void MainWindow::onDetailedDecode()
{
  bool bEn = false;
  uint32_t nMcuX = 0;
  uint32_t nMcuY = 0;
  uint32_t nMcuLen = 0;
  m_pImgDec->GetDetailVlc(bEn, nMcuX, nMcuY, nMcuLen);

  Q_DecodeDetailDlg dlg(bEn, nMcuX, nMcuY, nMcuLen, this);

  uint32_t nLoadX = 0;
  uint32_t nLoadY = 0;
  uint32_t nLoadLen = 0;

  if(detailRangeFromMarks(nLoadX, nLoadY, nLoadLen))
  {
    dlg.setLoadCoords(nLoadX, nLoadY, nLoadLen);
  }

  if(!m_pAppConfig->decodeImage())
  {
    dlg.setNote(tr("Options > Scan Segment > Decode Image is off, so the scan isn't decoded and there is "
                   "nothing to report."));
  }

  if(dlg.exec() != QDialog::Accepted)
  {
    return;
  }

  m_pImgDec->SetDetailVlc(dlg.detailEnabled(), dlg.mcuX(), dlg.mcuY(), dlg.mcuLen());

  // Only a change to what the report shows needs a new decode
  const bool bChanged = (dlg.detailEnabled() != bEn) ||
                        (bEn && ((dlg.mcuX() != nMcuX) || (dlg.mcuY() != nMcuY) || (dlg.mcuLen() != nMcuLen)));

  if(bChanged && !m_currentFile.isEmpty())
  {
    m_pAppConfig->HandleAutoReprocess();
  }
}

//-----------------------------------------------------------------------------

bool MainWindow::eventFilter(QObject *pObj, QEvent *pEvent)
{
  if((pObj == textEdit) && (pEvent->type() == QEvent::PaletteChange) && m_pDocLog)
  {
    m_pDocLog->RecolorForPalette();
  }

  return QMainWindow::eventFilter(pObj, pEvent);
}

//-----------------------------------------------------------------------------

void MainWindow::closeEvent(QCloseEvent *)
{
  m_pAppConfig->RegistryStore();
}
