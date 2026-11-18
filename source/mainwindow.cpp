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
#include <QScopeGuard>

#include "DbSigs.h"
#include "General.h"
#include "aboutdialog.h"
#include "batchdialog.h"
#include "batchprocess.h"
#include "dbmanagedialog.h"
#include "dbsubmitdialog.h"
#include "finddialog.h"
#include "lookupdialog.h"
#include "offsetdialog.h"
#include "overlaydialog.h"
#include "ImgDecode.h"
#include "JfifDecode.h"
#include "SnoopConfig.h"
#include "Viewer.h"
#include "WindowBuf.h"
#include "note.h"
#include "q_decodedetaildlg.h"
#include "reportview.h"
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

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), textEdit(new CReportView)
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

  // The signatures the user added (Tools > Add Camera/SW to DB), which every
  // signature search uses. (The Qt port never read them.)
  m_pDbSigs->SetDbDir(m_pAppConfig->dbPath());
  m_pDbSigs->DatabaseExtraLoad();

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

  // Always on: it needs no file open (as on Windows)
  batchProcessAct = new QAction(tr("Batch Process..."), this);
  batchProcessAct->setStatusTip(tr("Start batch processing"));
  connect(batchProcessAct, &QAction::triggered, this, &MainWindow::onBatchProcess);
  OffsetAct = new QAction(tr("Offset..."), this);
  OffsetAct->setStatusTip(tr("Start processing at an offset"));
  OffsetAct->setEnabled(false);
  connect(OffsetAct, &QAction::triggered, this, &MainWindow::onFileOffset);

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

  // Edit > Copy, Select All, Find and Find Next act on the report, as on
  // Windows. Copy is enabled while part of the report is selected; Select All
  // and Find while the report has any text (also when it only holds the error
  // for a file that couldn't be opened or was empty); and Find Next while it
  // has text and there has been a search. See updateTextActions().
  copyAct = new QAction(tr("&Copy"), this);
  copyAct->setShortcuts(QKeySequence::Copy);
  copyAct->setEnabled(false);
  connect(copyAct, &QAction::triggered, textEdit, &QPlainTextEdit::copy);
  connect(textEdit, &QPlainTextEdit::copyAvailable, copyAct, &QAction::setEnabled);

  selectAllAct = new QAction(tr("Select All"), this);
  selectAllAct->setShortcuts(QKeySequence::SelectAll);
  selectAllAct->setEnabled(false);
  connect(selectAllAct, &QAction::triggered, this, &MainWindow::onSelectAll);

  findAct = new QAction(tr("Find..."), this);
  findAct->setShortcuts(QKeySequence::Find);
  findAct->setEnabled(false);
  connect(findAct, &QAction::triggered, this, &MainWindow::onFind);

  findNextAct = new QAction(tr("Find Next"), this);
  findNextAct->setShortcuts(QKeySequence::FindNext);
  findNextAct->setEnabled(false);
  connect(findNextAct, &QAction::triggered, this, &MainWindow::onFindNext);

  connect(textEdit, &QPlainTextEdit::textChanged, this, &MainWindow::updateTextActions);

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
  imgSearchFwdAct->setStatusTip(tr("Search for next Image"));
  imgSearchFwdAct->setEnabled(false);
  connect(imgSearchFwdAct, &QAction::triggered, this, &MainWindow::onImageSearchFwd);

  imgSearchRevAct = new QAction(tr("Image Search Rev"), this);
  imgSearchRevAct->setShortcut(tr("Ctrl+1"));
  imgSearchRevAct->setStatusTip(tr("Search for previous Image"));
  imgSearchRevAct->setEnabled(false);
  connect(imgSearchRevAct, &QAction::triggered, this, &MainWindow::onImageSearchRev);

  lookupMcuOffAct = new QAction(tr("Lookup MCU Offset..."), this);
  lookupMcuOffAct->setStatusTip(tr("Calculate file offset for an MCU"));
  lookupMcuOffAct->setEnabled(false);
  connect(lookupMcuOffAct, &QAction::triggered, this, &MainWindow::onLookupMcuOffset);

  fileOverlayAct = new QAction(tr("File Overlay..."), this);
  fileOverlayAct->setEnabled(false);
  connect(fileOverlayAct, &QAction::triggered, this, &MainWindow::onFileOverlay);

  searchDqtAct = new QAction(tr("Search Executable for DQT..."), this);
  searchDqtAct->setStatusTip(tr("Find DQT Table in Executable"));
  connect(searchDqtAct, &QAction::triggered, this, &MainWindow::onSearchDqt);

  exportJpegAct = new QAction(tr("Export JPEG..."), this);
  exportJpegAct->setEnabled(false);
  connect(exportJpegAct, &QAction::triggered, this, &MainWindow::onExportJpeg);

  exportTiffAct = new QAction(tr("Export TIFF..."), this);
  exportTiffAct->setEnabled(false);
  connect(exportTiffAct, &QAction::triggered, this, &MainWindow::onExportTiff);

  addToDbAct = new QAction(tr("Add Camera/SW to DB..."), this);
  addToDbAct->setShortcut(tr("Alt+S"));
  addToDbAct->setEnabled(false);
  connect(addToDbAct, &QAction::triggered, this, &MainWindow::onAddToDb);

  // (Needs no file open)
  manageLocalDbAct = new QAction(tr("Manage Local DB..."), this);
  manageLocalDbAct->setStatusTip(tr("Manage the local User signatures"));
  connect(manageLocalDbAct, &QAction::triggered, this, &MainWindow::onManageLocalDb);

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
  // On macOS: JPEGsnoop > About JPEGsnoop, where Mac apps have it
  aboutAct->setMenuRole(QAction::AboutRole);
  connect(aboutAct, &QAction::triggered, this, &MainWindow::onAbout);

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
  fileMenu->addAction(batchProcessAct);
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

  // A new file is decoded from its start: File > Offset applies to the file
  // it was chosen for, as on Windows (Reprocess and Export JPEG keep it)
  m_pAppConfig->setStartPos(0);

  // A File Overlay is a change to try on the file it was made for (on
  // Windows it stayed in place for the next file opened, which was then
  // decoded with those bytes replaced). Reprocess keeps it.
  m_pWBuf->OverlayRemoveAll();

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
    m_pDocLog->AddLineErr(emptyFileError(m_currentFile));
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

  // (Shared with File > Batch Process, whose logs are this report)
  addReportHeader(m_pDocLog, m_currentFile, m_lFileSize);

  // A File Overlay changes what is decoded, so say so first. (Windows only
  // listed it in the scan section, which isn't there with Decode Image off.)
  unsigned char *pOverlay = nullptr;
  uint32_t nOverlayLen = 0;
  uint32_t nOverlayStart = 0;
  if(m_pWBuf->OverlayGet(0, pOverlay, nOverlayLen, nOverlayStart))
  {
    m_pDocLog->AddLineWarn(QString("  Overlay:  [%1] bytes replaced at offset [0x%2] (Tools > File Overlay)")
                             .arg(nOverlayLen)
                             .arg(QString("%1").arg(nOverlayStart, 8, 16, QChar('0')).toUpper()));
  }
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
  OffsetAct->setEnabled(true);
  imgSearchFwdAct->setEnabled(true);
  imgSearchRevAct->setEnabled(true);
  lookupMcuOffAct->setEnabled(true);
  fileOverlayAct->setEnabled(true);
  searchDqtAct->setEnabled(true);
  exportJpegAct->setEnabled(true);
  exportTiffAct->setEnabled(true);
  addToDbAct->setEnabled(true);
}

//-----------------------------------------------------------------------------

void MainWindow::onSelectAll()
{
  // The report, wherever the focus was, and focused so the selection shows
  // as active and the keyboard can extend it
  textEdit->setFocus(Qt::OtherFocusReason);
  textEdit->selectAll();
}

//-----------------------------------------------------------------------------

// The Edit items that depend on the report's text (isEmpty() takes constant
// time; this runs for each line the decoder adds)
void MainWindow::updateTextActions()
{
  const bool bText = !textEdit->document()->isEmpty();
  selectAllAct->setEnabled(bText);
  findAct->setEnabled(bText);
  findNextAct->setEnabled(bText && !m_strFind.isEmpty());
}

//-----------------------------------------------------------------------------

// Edit > Find: shows the Find dialog, which stays open while searching, as on
// Windows. Shown afresh, it holds the selected text if that is on one line,
// otherwise the last text searched for, and the last search's options; if it
// is already open it is only brought to the front. Either way the next search
// starts from the selection.
void MainWindow::onFind()
{
  textEdit->startNewSearch();

  if(!m_pFindDlg)
  {
    m_pFindDlg = new CFindDialog(this);
    connect(m_pFindDlg, &CFindDialog::findNext, this, &MainWindow::findFromDialog);
  }

  if(!m_pFindDlg->isVisible())
  {
    QString strFind = m_strFind;
    const QTextCursor cursor = textEdit->textCursor();
    const QTextDocument *pDoc = textEdit->document();
    if(cursor.hasSelection() &&
       (pDoc->findBlock(cursor.selectionStart()) == pDoc->findBlock(cursor.selectionEnd())))
    {
      const QString strSel = cursor.selectedText();
      if(!strSel.contains(QLatin1Char('\n')) && !strSel.contains(QLatin1Char('\r')) &&
         !strSel.contains(QChar::LineSeparator))
        strFind = strSel;
    }
    m_pFindDlg->setOptions(strFind, m_bFindDown, m_bFindCase, m_bFindWord);
  }

  m_pFindDlg->show();
  m_pFindDlg->raise();
  m_pFindDlg->activateWindow();
}

//-----------------------------------------------------------------------------

// Edit > Find Next: the last search again
void MainWindow::onFindNext()
{
  findInReport();
}

//-----------------------------------------------------------------------------

// The Find dialog's Find Next: becomes the search Find Next repeats. If the
// text found is under the dialog, the dialog moves out of its way.
void MainWindow::findFromDialog(const QString &strFind, bool bDown, bool bCase, bool bWord)
{
  m_strFind = strFind;
  m_bFindDown = bDown;
  m_bFindCase = bCase;
  m_bFindWord = bWord;
  updateTextActions();

  if(findInReport())
    moveFindDialogOffSelection();
}

//-----------------------------------------------------------------------------

// Searches the report for the last text searched for. When it finds nothing,
// Windows only beeps; this also says why in the status bar, which a search
// that finds something clears.
bool MainWindow::findInReport()
{
  if(textEdit->findText(m_strFind, m_bFindDown, m_bFindCase, m_bFindWord))
  {
    if(!m_strFindStatus.isEmpty() && (m_StatusBar->currentMessage() == m_strFindStatus))
      m_StatusBar->clearMessage();
    m_strFindStatus.clear();
    return true;
  }

  QApplication::beep();

  QTextDocument::FindFlags eFlags;
  if(m_bFindCase)
    eFlags |= QTextDocument::FindCaseSensitively;
  if(m_bFindWord)
    eFlags |= QTextDocument::FindWholeWords;
  if(m_strFind.isEmpty() || textEdit->document()->find(m_strFind, 0, eFlags).isNull())
    m_strFindStatus = tr("Cannot find \"%1\"").arg(m_strFind);
  else if(m_bFindDown)
    m_strFindStatus = tr("Reached the end of the report: Find Next goes on from the top");
  else
    m_strFindStatus = tr("Reached the start of the report: Find Next goes on from the bottom");
  m_StatusBar->showMessage(m_strFindStatus);
  return false;
}

//-----------------------------------------------------------------------------

// As on Windows: when the Find dialog covers the text found, it moves above
// it, or below it if there isn't room above
void MainWindow::moveFindDialogOffSelection()
{
  if(!m_pFindDlg || !m_pFindDlg->isVisible())
    return;

  const QTextCursor cursor = textEdit->textCursor();
  QTextCursor start(cursor);
  start.setPosition(cursor.selectionStart());
  QTextCursor end(cursor);
  end.setPosition(cursor.selectionEnd());
  const QRect rectSel = textEdit->cursorRect(start).united(textEdit->cursorRect(end));
  const QRect rectFound(textEdit->viewport()->mapToGlobal(rectSel.topLeft()), rectSel.size());
  const QRect rectDlg = m_pFindDlg->frameGeometry();
  if(!rectDlg.intersects(rectFound))
    return;

  const QScreen *pScreen = m_pFindDlg->screen();
  const QRect rectScreen = pScreen ? pScreen->availableGeometry() : QRect();
  const int nGap = 20;
  const int nAbove = rectFound.top() - nGap - rectDlg.height();
  const int nBelow = rectFound.bottom() + 1 + nGap;
  if(rectScreen.isNull() || (nAbove >= rectScreen.top()))
    m_pFindDlg->move(rectDlg.left(), nAbove);
  else if(nBelow + rectDlg.height() <= rectScreen.bottom() + 1)
    m_pFindDlg->move(rectDlg.left(), nBelow);
}

//-----------------------------------------------------------------------------

void MainWindow::onConfig()
{
  // Modal and on the stack: one dialog at a time, nothing left behind.
  // Save applies the settings (SnoopConfigDialog::onSave).
  const QString strDbDirBefore = m_pAppConfig->dbPath();
  SnoopConfigDialog dlg(m_pAppConfig, this);
  dlg.exec();

  // A new user database folder: its database is the one searched and added
  // to. (Windows only noted the folder, so the next entry added wrote the old
  // folder's entries over the new folder's file.)
  if(m_pAppConfig->dbPath() != strDbDirBefore)
  {
    m_pDbSigs->SetDbDir(m_pAppConfig->dbPath());
    m_pDbSigs->DatabaseExtraLoad();
  }
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

// Extract All's names (numberedExportName() and existingNumberedExports(),
// in batchprocess.cpp) are the name chosen, without its extension, then the
// JPEG's number (from 1, in 6 digits as master: a 60 fps video's frames for
// over 4.5 hours), then ".jpg".

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

  // Without Enable Overlays the export copies the file's own bytes, so it
  // must also find the JPEG (its markers, and with Extract All each SOI) in
  // them. With the File Overlay in place it found what the overlay put there:
  // an FFD9 overlaid in the scan ended the export at it, without an EOI. So
  // the overlay is set aside for the export and put back after it. (Windows
  // had the same problem.)
  QByteArray abOverlay;
  uint32_t nOverlayStart = 0;
  const bool bSetAside = !sSettings.bOverlayEn && currentOverlay(abOverlay, nOverlayStart);
  if(bSetAside)
  {
    setCurrentOverlay(QByteArray(), 0);
  }
  const auto restoreOverlay = qScopeGuard([&]() {
    if(bSetAside)
      setCurrentOverlay(abOverlay, nOverlayStart);
  });

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

  // The report above is of the file without the overlay, which stays in
  // place. It says "exported" only if the export succeeded: after a write
  // error, nothing (or with Extract All, not everything) was written.
  if(bSetAside)
  {
    m_pDocLog->AddLine(bOk ? QString("  NOTE: File Overlay left out (Enable Overlays is off): the file was analyzed "
                                     "and exported as it is on disk")
                           : QString("  NOTE: File Overlay left out (Enable Overlays is off): the file was analyzed "
                                     "as it is on disk"));
  }

  AnalyzeClose();
  return bOk;
}

//-----------------------------------------------------------------------------
// Extract All: export the JPEG at each SOI (FF D8 FF) in the file, from its
// start, to <strOut without extension>.000001.jpg, .000002.jpg, ...,
// replacing any files of those names (onExportJpeg() asks first). Then
// show the file's report, from the start offset as before, followed by a
// list of what was exported. (The loop is extractAllJpegs(), shared with
// File > Batch Process.)
//
// PRE:
// - AnalyzeOpen()
//
bool MainWindow::exportAllJpegs(const QString &strOut, const ExportJpegSettings &sSettings)
{
  const ExtractAllResult sResult = extractAllJpegs(
    m_pWBuf, m_pJfifDec, m_pAppConfig, m_pDocLog, m_pFile, m_currentFile, strOut, sSettings,
    [this]() { emit ImgSrcChanged(); },
    [this](uint32_t nExported) { m_StatusBar->showMessage(tr("Exporting all JPEGs: %1 exported...").arg(nExported)); });
  const QStringList &astrResults = sResult.astrResults;
  const QList<bool> &abResultOk = sResult.abResultOk;
  const uint32_t nExported = sResult.nExported;
  const bool bWriteOk = sResult.bWriteOk;
  const QString &strWriteError = sResult.strWriteError;
  const bool bInteractiveSaved = m_pAppConfig->interactive();

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
void MainWindow::onAbout()
{
  // Modal and on the stack, like the other dialogs (it did nothing)
  AboutDialog dlg(this);
  dlg.exec();
}

//-----------------------------------------------------------------------------

// Tools > File Overlay: a run of bytes to decode in place of the file's own
// at an offset, to try out a change without editing the file. As on Windows,
// OK or Apply puts it in place (or removes it, with Overlay Enabled off) and
// reprocesses the file; Apply leaves the dialog open. There is one overlay:
// on Windows each OK or Apply with it on added another, so an earlier one at
// another offset stayed in place, and the dialog showed the first.
void MainWindow::onFileOverlay()
{
  if(m_currentFile.isEmpty())
  {
    return;
  }

  // The menu stays enabled after a file that couldn't be opened
  if(!m_bFileAnalyzed)
  {
    QMessageBox::warning(this, "JPEGsnoop", tr("The file shown couldn't be analyzed, so there is nothing to overlay."));
    return;
  }

  // The file's own bytes, for the dialog to show
  const QString strFile = m_currentFile;
  auto readFile = [strFile](quint32 nOffset, int nLen) {
    QFile file(strFile);
    if(!file.open(QIODevice::ReadOnly) || !file.seek(nOffset))
      return QByteArray();
    return file.read(nLen);
  };

  COverlayDialog dlg(readFile, static_cast<quint64>(m_lFileSize), this);
  auto showOverlay = [this, &dlg]() {
    QByteArray abData;
    uint32_t nStart = 0;
    const bool bEn = currentOverlay(abData, nStart);
    dlg.setOverlay(bEn, nStart, abData);
  };
  auto applyOverlay = [this, &dlg]() {
    setCurrentOverlay(dlg.overlayEnabled() ? dlg.data() : QByteArray(), dlg.offset());
    reprocess();
  };

  showOverlay();
  connect(&dlg, &COverlayDialog::applied, this, [&]() {
    applyOverlay();
    showOverlay();
  });

  if(dlg.exec() == QDialog::Accepted)
  {
    applyOverlay();
  }
}

//-----------------------------------------------------------------------------

// The File Overlay in place: false if there is none
bool MainWindow::currentOverlay(QByteArray &abData, uint32_t &nStart)
{
  unsigned char *pData = nullptr;
  uint32_t nLen = 0;
  nStart = 0;
  abData.clear();
  if(!m_pWBuf->OverlayGet(0, pData, nLen, nStart))
  {
    return false;
  }
  abData = QByteArray(reinterpret_cast<const char *>(pData), static_cast<int>(nLen));
  return true;
}

// Puts abData in place at nStart as the File Overlay (there is one), or with
// abData empty removes it. Reprocess to see the change.
void MainWindow::setCurrentOverlay(const QByteArray &abData, uint32_t nStart)
{
  m_pWBuf->OverlayRemoveAll();
  if(!abData.isEmpty())
  {
    QByteArray abCopy = abData;
    m_pWBuf->OverlayInstall(0, reinterpret_cast<unsigned char *>(abCopy.data()), static_cast<uint32_t>(abCopy.size()),
                            nStart, 0, 0, 0, 0, 0, 0, 0);
  }
}

//-----------------------------------------------------------------------------
// File > Offset: decode the file from an offset, such as where a JPEG
// embedded in it starts (master's CJPEGsnoopDoc::OnFileOffset). The offset
// applies to the file shown: Reprocess and Export JPEG keep it, and opening
// a file starts from 0 again. A File Overlay stays in place.
//
// Master marked the image source changed only after decoding the file again,
// so once a scan had been decoded (from a file that starts with a JPEG, say)
// the decode from the new offset skipped its scan: the report lacked it and
// the preview kept the old image (from the code). reprocess() marks it first.
void MainWindow::onFileOffset()
{
  if(m_currentFile.isEmpty())
  {
    return;
  }

  // The menu stays enabled after a file that couldn't be opened
  if(!m_bFileAnalyzed)
  {
    QMessageBox::warning(this, "JPEGsnoop",
                         tr("The file shown couldn't be analyzed, so it can't be decoded from an offset."));
    return;
  }

  COffsetDialog dlg(m_pAppConfig->startPos(), static_cast<quint64>(m_lFileSize), this);
  if(dlg.exec() != QDialog::Accepted)
  {
    return;
  }

  m_pAppConfig->setStartPos(dlg.offset());
  reprocess();
}

//-----------------------------------------------------------------------------
// Tools > Image Search Fwd and Rev: decode the file from the next JPEG after
// the start offset, or the one before it (master's
// CJPEGsnoopDoc::OnToolsSearchforward and OnToolsSearchreverse). A JPEG is
// found by its SOI marker, FF D8 FF. The offset found becomes the start
// offset, as File > Offset sets it. As on Windows, the search sees the File
// Overlay, as the decoder does, and when Rev finds no JPEG it goes back to
// the start of the file, where the file was decoded from when opened.
void MainWindow::onImageSearchFwd()
{
  imageSearch(true);
}

void MainWindow::onImageSearchRev()
{
  imageSearch(false);
}

void MainWindow::imageSearch(bool bForward)
{
  if(m_currentFile.isEmpty())
  {
    return;
  }

  // The menu stays enabled after a file that couldn't be opened
  if(!m_bFileAnalyzed)
  {
    QMessageBox::warning(this, "JPEGsnoop", tr("The file shown couldn't be analyzed, so it can't be searched."));
    return;
  }

  const uint32_t nStart = m_pAppConfig->startPos();
  const QString strStart = COffsetDialog::offsetText(nStart, true);

  // (Windows did nothing here)
  if(!bForward && (nStart == 0))
  {
    QMessageBox::information(this, "JPEGsnoop",
                             tr("The file is decoded from its start, so there is nothing before it to search."));
    return;
  }

  // The file as it is now, as decoding it again reads it
  if(!AnalyzeOpen())
  {
    // Moved, removed or emptied: the report says so, as after Reprocess,
    // and the preview of what was there goes
    clearImageViews();
    m_bFileAnalyzed = false;
    return;
  }

  m_StatusBar->showMessage(bForward ? tr("Searching forward...") : tr("Searching reverse..."));
  QApplication::setOverrideCursor(Qt::WaitCursor);
  uint32_t nFound = 0;
  const bool bFound = m_pWBuf->BufSearch(nStart, 0xFFD8FF, 3, bForward, nFound);
  QApplication::restoreOverrideCursor();
  m_StatusBar->clearMessage();
  AnalyzeClose();

  if(bFound)
  {
    m_pAppConfig->setStartPos(nFound);
    reprocess();
  }
  else if(bForward)
  {
    QMessageBox::information(this, "JPEGsnoop",
                             tr("No JPEG found after offset %1 (no SOI marker, FF D8 FF).").arg(strStart));
  }
  else
  {
    m_pAppConfig->setStartPos(0);
    reprocess();
    QMessageBox::information(this, "JPEGsnoop",
                             tr("No JPEG found before offset %1 (no SOI marker, FF D8 FF), so the file is decoded "
                                "from its start.")
                               .arg(strStart));
  }
}

//-----------------------------------------------------------------------------
// Tools > Lookup MCU Offset: where the MCU holding a pixel of the decoded
// image starts in the file (master's CJPEGsnoopDoc::OnToolsLookupmcuoffset).
// As there, it reads the decoder's map of MCU positions, filled when the scan
// was decoded, so the file isn't read again. The dialog is modal, so the image
// can't change while it is open.
void MainWindow::onLookupMcuOffset()
{
  if(m_currentFile.isEmpty())
  {
    return;
  }

  // The menu stays enabled after a file that couldn't be opened
  if(!m_bFileAnalyzed)
  {
    QMessageBox::warning(this, "JPEGsnoop",
                         tr("The file shown couldn't be analyzed, so it has no MCUs to look up."));
    return;
  }

  // Windows offered the dialog with no scan decoded (with Decode Image off, or
  // for a progressive JPEG), with a range of 0 to 4294967295, and read a null
  // map
  if(!m_pImgDec->IsImgDecoded() || !m_pImgDec->IsPreviewReady())
  {
    if(!m_pAppConfig->decodeImage())
      QMessageBox::information(this, "JPEGsnoop",
                               tr("No image was decoded, so there are no MCUs to look up: Options > Scan Segment > "
                                  "Decode Image is off."));
    else
      QMessageBox::information(this, "JPEGsnoop",
                               tr("No JPEG image was decoded in the file shown, so there are no MCUs to look up. "
                                  "The report says why."));
    return;
  }

  uint32_t nSizeX = 0;
  uint32_t nSizeY = 0;
  m_pImgDec->GetImageSize(nSizeX, nSizeY);
  CLookupDialog dlg(nSizeX, nSizeY,
                    [this](QPoint ptPix, QPoint &ptMcu, uint32_t &nByte, uint32_t &nBit) {
                      ptMcu = m_pImgDec->PixelToMcu(ptPix);
                      return m_pImgDec->LookupFilePosPix(ptPix, nByte, nBit);
                    },
                    this);
  dlg.exec();
}

//-----------------------------------------------------------------------------
// Tools > Search Executable for DQT: look in another file, such as a camera
// maker's program, for the JPEG standard luminance quantization table and the
// luminance table of the file shown (master's
// CJPEGsnoopDoc::OnToolsSearchexecutablefordqt). A program that writes JPEGs
// often keeps its tables in its code, so finding one there suggests which
// program wrote a JPEG. As on Windows, the item needs no file open, and the
// results go at the end of the report.
void MainWindow::onSearchDqt()
{
  QFileDialog dlg(this, tr("Open Executable file for Search"));
  dlg.setObjectName(QStringLiteral("dqtExeDialog"));
  dlg.setFileMode(QFileDialog::ExistingFile);
  dlg.setOption(QFileDialog::ReadOnly);
  // Windows offered .exe files first; macOS programs have no extension
  dlg.setNameFilters({ tr("All Files (*)"), tr("Executable (*.exe)"), tr("DLL Library (*.dll)"),
                       tr("Dynamic Library (*.dylib)") });

  if((dlg.exec() != QDialog::Accepted) || dlg.selectedFiles().isEmpty())
  {
    return;
  }

  searchDqt(dlg.selectedFiles().first());
}

// Search the file strChosen for the tables (see onSearchDqt()): each in zigzag
// order (as a DQT segment has it) and in row order, with 1-, 2- and 4-byte
// entries in either byte order. Each place found is reported.
void MainWindow::searchDqt(const QString &strChosen)
{
  QString strPath = strChosen;
  QFileInfo fileInfo(strPath);

#ifdef Q_OS_MACOS
  // An app chosen as a whole (the macOS open panel shows it as one file): its
  // program, as its Info.plist names it
  if(fileInfo.isBundle())
  {
    const QSettings plist(fileInfo.absoluteFilePath() + "/Contents/Info.plist", QSettings::NativeFormat);
    const QString strExe = plist.value("CFBundleExecutable").toString();

    if(!strExe.isEmpty())
    {
      strPath = fileInfo.absoluteFilePath() + "/Contents/MacOS/" + strExe;
      fileInfo = QFileInfo(strPath);
    }
  }
#endif

  if(fileInfo.isDir())
  {
    QMessageBox::warning(this, "JPEGsnoop", tr("[%1] is a folder: choose a file to search.").arg(strPath));
    return;
  }

  QFile fileExe(strPath);
  if(!fileExe.open(QIODevice::ReadOnly))
  {
    QMessageBox::warning(this, "JPEGsnoop",
                         tr("ERROR: Couldn't open file [%1]: [%2]").arg(strPath, fileExe.errorString()));
    return;
  }

  // The file is read with 32-bit offsets, as an image is. (For an empty file
  // the file buffer showed two errors of its own.)
  const qint64 nSize = fileExe.size();
  if(nSize > 0xFFFFFFFF)
  {
    QMessageBox::warning(this, "JPEGsnoop",
                         tr("[%1] is too large to search: %2 bytes (4 GB or more).").arg(strPath).arg(nSize));
    return;
  }
  if(nSize == 0)
  {
    QMessageBox::warning(this, "JPEGsnoop", tr("[%1] is empty: there is nothing to search.").arg(strPath));
    return;
  }

  m_pDocLog->AddLineHdr("*** Searching Executable for DQT ***");
  m_pDocLog->AddLine(QString("  Filename: [%1]").arg(strPath));
  m_pDocLog->AddLine(QString("  Size:     [%1]").arg(nSize));

  // The tables: the JPEG standard's, and the file shown's luminance table
  struct DqtTable
  {
    QString strName;
    uint32_t anVal[64];
    bool bBaseline;               // every entry fits in a byte
  };
  QList<DqtTable> aTables;

  DqtTable sStd;
  sStd.strName = "JPEG Standard";
  sStd.bBaseline = true;
  for(uint32_t nInd = 0; nInd < 64; nInd++)
  {
    sStd.anVal[nInd] = glb_anStdQuantLum[nInd];
  }
  aTables.append(sStd);

  // (Windows used whatever table 0 held: for a file without one, all 0s,
  // which it reported as a constant table)
  if(m_bFileAnalyzed && !m_pImgDec->m_abDqtDefined[0])
  {
    m_pDocLog->AddLine("  NOTE: The file shown has no DQT Luminance table (table 0): only the JPEG Standard table");
    m_pDocLog->AddLine("        is searched for.");
  }
  else if(m_bFileAnalyzed)
  {
    DqtTable sFile;
    sFile.strName = m_currentFile;
    sFile.bBaseline = true;
    bool bAllSame = true;
    for(uint32_t nInd = 0; nInd < 64; nInd++)
    {
      sFile.anVal[nInd] = m_pImgDec->GetDqtEntry(0, nInd);
      bAllSame = bAllSame && (sFile.anVal[nInd] == sFile.anVal[0]);
      sFile.bBaseline = sFile.bBaseline && (sFile.anVal[nInd] <= 0xFF);
    }

    if(bAllSame)
    {
      m_pDocLog->AddLineWarn(QString("  NOTE: Because the JPEG's DQT Luminance table is constant value (0x%1),")
                               .arg(QString("%1").arg(sFile.anVal[0], 2, 16, QChar('0')).toUpper()));
      m_pDocLog->AddLineWarn("        matching for this table has been disabled.");
      m_pDocLog->AddLineWarn("        Please select a different reference image.");
    }
    else
    {
      aTables.append(sFile);
    }
  }

  CwindowBuf exeBuf(m_pDocLog);
  exeBuf.SetStatusBar(m_StatusBar);
  exeBuf.BufFileSet(&fileExe);
  exeBuf.BufLoadWindow(0);
  QApplication::setOverrideCursor(Qt::WaitCursor);

  uint8_t anPattern[64 * 4];
  m_pDocLog->AddLine("  Searching for DQT Luminance tables:");
  for(bool bRowOrder : { false, true })
  {
    m_pDocLog->AddLine(QString("    DQT Ordering: %1").arg(bRowOrder ? "post-zigzag" : "pre-zigzag"));
    for(const DqtTable &sTable : aTables)
    {
      m_pDocLog->AddLine(QString("      Matching [%1]").arg(sTable.strName));

      // Entries of 1, 2 and 4 bytes, the 2- and 4-byte ones in big-endian and
      // little-endian order ("endian byteswap")
      static const struct { uint32_t nWidth; bool bSwap; } asPatterns[] = {
        { 1, false }, { 2, false }, { 2, true }, { 4, false }, { 4, true } };
      for(const auto &sPattern : asPatterns)
      {
        m_pDocLog->AddLine(QString("        Searching patterns with %1-byte DQT entries%2")
                             .arg(sPattern.nWidth)
                             .arg(sPattern.bSwap ? ", endian byteswap" : ""));
        if((sPattern.nWidth == 1) && !sTable.bBaseline)
        {
          m_pDocLog->AddLine("          DQT Table is not baseline, skipping 1-byte search");
          continue;
        }

        const uint32_t nLen = 64 * sPattern.nWidth;
        memset(anPattern, 0, sizeof(anPattern));
        for(uint32_t nInd = 0; nInd < 64; nInd++)
        {
          const uint32_t nVal = sTable.anVal[bRowOrder ? nInd : glb_anZigZag[nInd]];
          uint8_t *pEntry = &anPattern[nInd * sPattern.nWidth];
          if(sPattern.nWidth == 1)
          {
            pEntry[0] = static_cast<uint8_t>(nVal);
          }
          else if(sPattern.bSwap)
          {
            pEntry[0] = static_cast<uint8_t>(nVal & 0xFF);
            pEntry[1] = static_cast<uint8_t>((nVal >> 8) & 0xFF);
          }
          else
          {
            pEntry[sPattern.nWidth - 1] = static_cast<uint8_t>(nVal & 0xFF);
            pEntry[sPattern.nWidth - 2] = static_cast<uint8_t>((nVal >> 8) & 0xFF);
          }
        }

        uint32_t nStart = 0;
        uint32_t nFound = 0;
        while(exeBuf.BufSearchX(nStart, anPattern, nLen, nFound))
        {
          m_pDocLog->AddLineGood(
            QString("          Found @ 0x%1").arg(QString("%1").arg(nFound, 8, 16, QChar('0')).toUpper()));
          nStart = nFound + 1;
        }
      }
    }
  }

  m_pDocLog->AddLine("  Done Search");
  QApplication::restoreOverrideCursor();
  m_StatusBar->clearMessage();
  exeBuf.BufFileUnset();
}

//-----------------------------------------------------------------------------
// Tools > Add Camera/SW to DB: add the shown file's compression signature to
// the user database, with what the user says made the file (master's
// CJPEGsnoopDoc::OnToolsAddcameratodb). Every signature search uses it from
// then on. Windows also posted the entry to impulseadventure.com when
// "Submit signature to Online DB" was on; this version doesn't.
void MainWindow::onAddToDb()
{
  if(m_currentFile.isEmpty())
  {
    return;
  }

  // The menu stays enabled after a file that couldn't be opened, and the
  // decoder still has the previous file's signature
  if(!m_bFileAnalyzed)
  {
    QMessageBox::warning(this, "JPEGsnoop",
                         tr("The file shown couldn't be analyzed, so it has no compression signature to add."));
    return;
  }

  QString strHash;
  QString strHashRot;
  QString strMake;
  QString strModel;
  QString strQual;
  QString strSoftware;
  teDbAdd eSuggest = DB_ADD_SUGGEST_UNSET;
  m_pJfifDec->GetDecodeSummary(strHash, strHashRot, strMake, strModel, strQual, strSoftware, eSuggest);

  // (Windows: "No valid signature could be created, so DB submit is
  // temporarily disabled", also added to the report)
  if(strHash == "NONE")
  {
    QMessageBox::information(this, "JPEGsnoop",
                             tr("No compression signature could be made for the file shown: that needs a frame "
                                "header (SOF) and quantization tables (DQT). There is nothing to add to the "
                                "database."));
    return;
  }

  // The source the report suggested
  teSource eSource = ENUM_SOURCE_UNSURE;
  if(eSuggest == DB_ADD_SUGGEST_CAM)
    eSource = ENUM_SOURCE_CAM;
  else if(eSuggest == DB_ADD_SUGGEST_SW)
    eSource = ENUM_SOURCE_SW;

  // (Windows also filled the software field with the EXIF Software)
  CDbSubmitDialog dlg(strMake, strModel, strSoftware, strHash, strQual, eSource, m_pDbSigs->DatabaseExtraPath(),
                      this);
  if(dlg.exec() != QDialog::Accepted)
  {
    return;
  }

  QString strDetail;
  const CDbSigs::AddResult eResult =
    m_pDbSigs->DatabaseExtraAdd(strMake, strModel, dlg.quality(), strHash, strHashRot, m_pJfifDec->GetImgQuantCss(),
                                dlg.source(), dlg.software(), strDetail);
  if(eResult == CDbSigs::AddResult::Added)
  {
    QMessageBox::information(this, "JPEGsnoop",
                             strDetail.isEmpty() ? tr("Added Compression signature to database")
                                                 : tr("Added Compression signature to database. %1").arg(strDetail));
  }
  else if(eResult == CDbSigs::AddResult::AlreadyIn)
  {
    QMessageBox::information(this, "JPEGsnoop", tr("Compression signature already in database"));
  }
  else if(eResult == CDbSigs::AddResult::Full)
  {
    QMessageBox::warning(this, "JPEGsnoop",
                         tr("ERROR: Can only store maximum of %1 extra signatures in local DB").arg(DBEX_ENTRIES_MAX));
  }
  else
  {
    QMessageBox::warning(this, "JPEGsnoop", tr("ERROR: The signature wasn't added: %1").arg(strDetail));
  }
}

//-----------------------------------------------------------------------------
// Tools > Manage Local DB: list the user database's entries, and take out of
// it the ones the user removes (master's CJPEGsnoopApp::OnToolsManagelocaldb).
// It needs no file open. The report shows the change after File > Reprocess,
// as with Add Camera/SW to DB.
void MainWindow::onManageLocalDb()
{
  // As the file is now: another copy of JPEGsnoop may have changed it
  // (Windows listed what it read at the start)
  m_pDbSigs->DatabaseExtraLoad();
  QList<CompSig> aEntries;
  for(uint32_t nInd = 0; nInd < m_pDbSigs->DatabaseExtraGetNum(); nInd++)
    aEntries.append(m_pDbSigs->DatabaseExtraGet(nInd));

  CDbManageDialog dlg(aEntries, m_pDbSigs->DatabaseExtraPath(), m_pDbSigs->DatabaseExtraLoadError(), this);
  if(dlg.exec() != QDialog::Accepted)
  {
    return;
  }

  // (Windows wrote the file again with none removed)
  const QList<CompSig> aRemoved = dlg.removed();
  if(aRemoved.isEmpty())
  {
    return;
  }

  // Nothing is said when it simply worked, as on Windows, or when another
  // copy has already taken them out
  QString strDetail;
  const CDbSigs::RemoveResult eResult = m_pDbSigs->DatabaseExtraRemove(aRemoved, strDetail);
  if(eResult == CDbSigs::RemoveResult::SaveFailed)
  {
    QMessageBox::warning(this, "JPEGsnoop",
                         ((aRemoved.size() == 1) ? tr("ERROR: The entry wasn't removed: %1")
                                                 : tr("ERROR: The entries weren't removed: %1"))
                           .arg(strDetail));
  }
  else if((eResult == CDbSigs::RemoveResult::Removed) && !strDetail.isEmpty())
  {
    QMessageBox::information(this, "JPEGsnoop", tr("Removed from the user database. %1").arg(strDetail));
  }
}

//-----------------------------------------------------------------------------
// File > Batch Process: analyze each image in a folder (and optionally its
// subfolders), saving each one's report as a log in an output folder, and
// optionally extracting every JPEG in each file (master's
// CJPEGsnoopDoc::DoBatchProcess). It needs no file open. The batch has its
// own decoders, as on Windows, so the file shown stays as it was: the report
// then lists what the batch did, and File > Reprocess shows the file's again.
void MainWindow::onBatchProcess()
{
  CBatchDialog dlg(m_pAppConfig->batchLastInput(), m_pAppConfig->batchLastOutput(), this);

  if(dlg.exec() != QDialog::Accepted)
  {
    return;
  }

  const BatchSettings sBatch = dlg.settings();
  m_pAppConfig->setBatchFolders(sBatch.strInput, sBatch.strOutput);

  const BatchList list = batchFileList(sBatch);
  const QList<BatchFile> &files = list.files;

  // Master said it was done, having done nothing (also when it couldn't
  // read the folders)
  if(files.isEmpty())
  {
    if(!list.folderErrors.isEmpty())
    {
      const BatchFolderError &sFirst = list.folderErrors.first();
      const QString strFirst = QDir::toNativeSeparators(sFirst.strFolder);
      QMessageBox msgNone(QMessageBox::Warning, "JPEGsnoop",
                          sBatch.bExtractAll ? tr("There are no files to process in the folders that could be read.")
                                             : tr("There are no .jpg or .jpeg files in the folders that could be read."),
                          QMessageBox::Ok, this);
      msgNone.setInformativeText(
        (list.folderErrors.size() == 1)
          ? tr("Couldn't read \"%1\": %2.").arg(strFirst, sFirst.strError)
          : tr("Couldn't read %1 folders, such as \"%2\": %3.").arg(list.folderErrors.size()).arg(strFirst, sFirst.strError));
      msgNone.exec();
      return;
    }

    const QString strIn = QDir::toNativeSeparators(sBatch.strInput);
    QString strNone = sBatch.bExtractAll ? tr("There are no files to process in \"%1\"").arg(strIn)
                                         : tr("There are no .jpg or .jpeg files in \"%1\"").arg(strIn);
    strNone += sBatch.bSubfolders ? tr(" or its subfolders.") : tr(".");
    QMessageBox::information(this, "JPEGsnoop", strNone);
    return;
  }

  // Master replaced them without asking
  const QStringList astrExisting = batchExistingOutputs(sBatch, files);

  if(!astrExisting.isEmpty())
  {
    const QString strFound =
      (astrExisting.size() == 1)
        ? tr("A file the batch writes already exists: \"%1\".").arg(astrExisting.first())
        : tr("%1 files the batch writes already exist, such as \"%2\".").arg(astrExisting.size()).arg(astrExisting.first());
    QMessageBox msgReplace(QMessageBox::Warning, "JPEGsnoop",
                           strFound + " " + tr("Each one the batch writes again is replaced."), QMessageBox::Cancel, this);
    msgReplace.setInformativeText(tr("Replace them?"));
    QPushButton *pReplace = msgReplace.addButton(tr("Replace"), QMessageBox::DestructiveRole);
    msgReplace.setDefaultButton(QMessageBox::Cancel);
    msgReplace.exec();

    if(msgReplace.clickedButton() != pReplace)
    {
      return;
    }
  }

  if(!QDir().mkpath(sBatch.strOutput))
  {
    QMessageBox::warning(this, "JPEGsnoop",
                         tr("Couldn't create the output folder \"%1\".").arg(QDir::toNativeSeparators(sBatch.strOutput)));
    return;
  }

  runBatch(sBatch, list);
}

// "1 file", "2 files"
static QString countOf(qsizetype nCount, const QString &strThing)
{
  return QString("%1 %2%3").arg(nCount).arg(strThing, (nCount == 1) ? "" : "s");
}

// Process the files with the progress shown, then list what was done in the
// report (in place of the file's, as on Windows) and say so. Cancel, or
// closing the window, stops it after the file it is on. (Master showed only
// a wait cursor, and couldn't be stopped.)
void MainWindow::runBatch(const BatchSettings &sBatch, const BatchList &list)
{
  const QList<BatchFile> &files = list.files;
  const qsizetype nFiles = files.size();
  const qsizetype nFolderErrors = list.folderErrors.size();
  CBatchProcess batch(m_pAppConfig, m_pDbSigs);
  QList<CBatchProcess::Result> results;

  {
    QProgressDialog progress(this);
    progress.setObjectName("batchProgress");
    progress.setWindowTitle(tr("Batch Processing"));
    progress.setCancelButtonText(tr("Cancel"));
    progress.setRange(0, static_cast<int>(nFiles));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setAutoReset(false);
    progress.setAutoClose(false);
    progress.setMinimumDuration(0);
    progress.setMinimumWidth(fontMetrics().horizontalAdvance(QLatin1Char('x')) * 50);
    progress.show();
    m_pBatchProgress = &progress;
    const auto clearProgress = qScopeGuard([this]() { m_pBatchProgress = nullptr; });

    for(qsizetype nInd = 0; nInd < nFiles; nInd++)
    {
      const QString strName = QFileInfo(files.at(nInd).strSrc).fileName();
      progress.setLabelText(tr("Batch processing file %1 of %2:\n%3").arg(nInd + 1).arg(nFiles).arg(strName));
      progress.setValue(static_cast<int>(nInd));
      QCoreApplication::processEvents();

      if(progress.wasCanceled())
      {
        break;
      }

      results << batch.process(files.at(nInd), sBatch.bExtractAll);
    }

    if(!progress.wasCanceled())
    {
      progress.setValue(static_cast<int>(nFiles));
    }
  }

  const bool bStopped = (results.size() < nFiles);
  qsizetype nLogs = 0;
  qsizetype nErrors = 0;
  uint32_t nExported = 0;

  m_pDocLog->Clear();
  m_pDocLog->AddLine("");
  m_pDocLog->AddLineHdr("*** Batch Processing ***");
  m_pDocLog->AddLine(QString("  Input folder:       [%1]").arg(QDir::toNativeSeparators(sBatch.strInput)));
  m_pDocLog->AddLine(QString("  Output folder:      [%1]").arg(QDir::toNativeSeparators(sBatch.strOutput)));
  m_pDocLog->AddLine(QString("  Process subfolders: [%1]").arg(sBatch.bSubfolders ? "Yes" : "No"));
  m_pDocLog->AddLine(QString("  Extract all JPEGs:  [%1]").arg(sBatch.bExtractAll ? "Yes" : "No"));
  m_pDocLog->AddLine("");

  // The files in these weren't processed
  if(nFolderErrors > 0)
  {
    for(const BatchFolderError &sError : list.folderErrors)
    {
      m_pDocLog->AddLineErr(QString("  ERROR: Couldn't read the folder [%1]: %2")
                              .arg(QDir::toNativeSeparators(sError.strFolder), sError.strError));
    }
    m_pDocLog->AddLine("");
  }

  for(qsizetype nInd = 0; nInd < results.size(); nInd++)
  {
    const BatchFile &sFile = files.at(nInd);
    const CBatchProcess::Result &sResult = results.at(nInd);

    // Master's progress line for each file
    m_pDocLog->AddLine(QString("  Batch processing file %1 of %2 (%3 %): [%4]")
                         .arg(nInd + 1, 6)
                         .arg(nFiles, 6)
                         .arg(100.0 * static_cast<double>(nInd + 1) / static_cast<double>(nFiles), 6, 'f', 2)
                         .arg(QDir::toNativeSeparators(sFile.strSrc)));

    if(sResult.bLogWritten)
    {
      nLogs++;
      m_pDocLog->AddLine(QString("    Log written to [%1]").arg(QDir::toNativeSeparators(sFile.strLog)));
    }

    if(!sResult.strError.isEmpty())
    {
      m_pDocLog->AddLineErr("    ERROR: " + sResult.strError);
    }

    if(sResult.bExtracted)
    {
      const ExtractAllResult &sExtract = sResult.sExtract;

      for(qsizetype nPlace = 0; nPlace < sExtract.astrResults.size(); nPlace++)
      {
        if(sExtract.abResultOk.at(nPlace))
          m_pDocLog->AddLine("  " + sExtract.astrResults.at(nPlace));
        else
          m_pDocLog->AddLineWarn("  " + sExtract.astrResults.at(nPlace));
      }

      m_pDocLog->AddLine("    Exported " + countOf(sExtract.nExported, "JPEG"));
      nExported += sExtract.nExported;
    }

    if(!sResult.strError.isEmpty() || !sResult.sExtract.bWriteOk)
    {
      nErrors++;
    }
  }

  m_pDocLog->AddLine("");

  if(bStopped)
  {
    m_pDocLog->AddLineWarn(QString("  Batch processing stopped after %1 of %2").arg(results.size()).arg(countOf(nFiles, "file")));
  }
  else
  {
    m_pDocLog->AddLineGood(QString("  Batch processing complete: %1").arg(countOf(nFiles, "file")));
  }

  m_pDocLog->AddLine(QString("  Logs written:      %1").arg(nLogs));

  if(sBatch.bExtractAll)
  {
    m_pDocLog->AddLine(QString("  JPEGs exported:    %1").arg(nExported));
  }

  if(nErrors > 0)
  {
    m_pDocLog->AddLineWarn(QString("  Files with errors: %1").arg(nErrors));
  }
  else
  {
    m_pDocLog->AddLine(QString("  Files with errors: %1").arg(nErrors));
  }

  if(nFolderErrors > 0)
  {
    m_pDocLog->AddLineWarn(QString("  Folders not read:  %1").arg(nFolderErrors));
  }

  if(!m_currentFile.isEmpty())
  {
    m_pDocLog->AddLine("");
    m_pDocLog->AddLine(QString("  File > Reprocess shows the report of [%1] again").arg(m_currentFile));
  }

  QString strDone = bStopped ? tr("Batch processing stopped.") : tr("Batch processing complete.");
  QString strCounts = tr("%1 of %2 processed: %3 written").arg(results.size()).arg(countOf(nFiles, tr("file")),
                                                                                    countOf(nLogs, tr("log")));
  if(sBatch.bExtractAll)
  {
    strCounts += tr(", %1 exported").arg(countOf(nExported, tr("JPEG")));
  }
  strCounts += ".";
  if((nErrors > 0) && (nFolderErrors > 0))
  {
    strCounts += " " + tr("%1 had errors and %2 couldn't be read; the report lists them.")
                         .arg(countOf(nErrors, tr("file")), countOf(nFolderErrors, tr("folder")));
  }
  else if(nErrors > 0)
  {
    strCounts += " " + tr("%1 had errors; the report lists them.").arg(countOf(nErrors, tr("file")));
  }
  else if(nFolderErrors > 0)
  {
    strCounts += " " + ((nFolderErrors == 1) ? tr("1 folder couldn't be read; the report lists it.")
                                             : tr("%1 folders couldn't be read; the report lists them.").arg(nFolderErrors));
  }

  m_StatusBar->showMessage(strDone);

  // Master said "Batch Processing Complete!"
  if(m_pAppConfig->interactive())
  {
    QMessageBox msgDone(((nErrors > 0) || (nFolderErrors > 0)) ? QMessageBox::Warning : QMessageBox::Information,
                        "JPEGsnoop", strDone, QMessageBox::Ok, this);
    msgDone.setInformativeText(strCounts);
    msgDone.exec();
  }
}

//-----------------------------------------------------------------------------

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

void MainWindow::closeEvent(QCloseEvent *pEvent)
{
  // A batch in progress stops after the file it is on; the window stays open
  // to show what was done
  if(m_pBatchProgress)
  {
    m_pBatchProgress->cancel();
    pEvent->ignore();
    return;
  }

  m_pAppConfig->RegistryStore();
}
