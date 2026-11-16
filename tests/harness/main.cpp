// Headless test driver for JPEGsnoopQt.
//
// decode <file> [--ac] [--histo] [--unknown-exif] [--pixels X,Y[/X,Y...]] [--start N]
//     [--detail X,Y,LEN] [--preview-md5]
//     Decode one file with the app's decoder classes (CjfifDecode, CimgDecode,
//     CwindowBuf, ...) in non-interactive mode and print the report to stdout,
//     followed by a "harness:" summary line. --ac: full IDCT; --histo:
//     histograms, and a line counting the pixels drawn in each histogram
//     image; --unknown-exif: also report unknown EXIF tags; --pixels
//     prints the decoded preview's colour at each point; --start decodes
//     from file offset N; --detail turns on the detailed scan decode
//     (Options > Scan Segment > Detailed Decode) for LEN MCUs from MCU X,Y,
//     prints a "harness: detail" summary of what it reported, and decodes
//     the file again without it to check that only the report's detail
//     lines differ ("harness: detail changes report=0 image=0");
//     --preview-md5 prints an MD5 of the decoded preview's pixels.
//
// gui <file> [<file> ...] [--mouse] [--log] [--reprocess-first] [--histo]
//     [--cycle-channels] [--repeat N] [--max-growth-mb N] [--zoom PCT]
//     [--max-zoom-mb N] [--check-preview] [--check-histo] [--dark]
//     [--check-theme]
//     Build the real MainWindow, open each file in turn as File > Open does,
//     and print a "harness:" line per file. With --mouse, move the mouse over
//     the preview window and click at points inside, at the edges of and
//     outside the image after each file; --log prints the report after each
//     file; --reprocess-first runs Reprocess before opening anything;
//     --histo turns the histograms on; --cycle-channels shows each
//     View > Image Chan channel after each file; --repeat opens the files N
//     times; --max-growth-mb reports whether memory grew by more than N MB
//     after the first round (a leak check). A message box (such as a failed
//     open's) is closed and its text printed as "harness: message box:".
//     After each file in the first round: --zoom picks that View > Zoom
//     level ("800" for 800%), and --max-zoom-mb reports whether showing it
//     took more than N MB; --check-preview compares the preview's size, mouse
//     mapping and drawn pixels with the decoded image (and again, as "preview
//     after channels", after --cycle-channels); --check-histo checks
//     the histograms are drawn at the screen's resolution; --check-theme
//     reports the report's lowest text contrast, then switches between light
//     and dark palettes and reports it again. --dark starts with a dark
//     palette, as macOS Dark Mode does.
//
// --scale F (any mode) runs as if on a screen with F device pixels per point
// (QT_SCALE_FACTOR), e.g. 2 for a Retina display.
//
// settings
//     Exercise settings load/store, option toggles and the Configuration
//     dialog against a throwaway settings store; prints "harness:" lines.
//
// export-jpeg <file> [--all] [--force-soi] [--force-eoi] [--ignore-eoi]
//     [--dht-avi] [--out PATH] [--out-same] [--out-same-case]
//     Open the file in the real MainWindow and run Tools > Export JPEG with
//     these settings (--all: Extract All), to a temporary out.jpg unless
//     --out says where. --out-same / --out-same-case export over the file
//     itself, by the same name or by the name in another case. Prints the
//     size and MD5 of each file written, as "harness: exported N md5s=...",
//     whether the source is unchanged, and the report from the export on.
//
// export-tiff <file> --format rgb8|rgb16|ycc8 [--out PATH] [--out-same]
//     Open the file in the real MainWindow and run Tools > Export TIFF, then
//     read the TIFF back and check its tags and every sample against a
//     separate decode of the file ("harness: tiff ... check=ok").
//     --out-same exports over the file itself.
//
// gui ... [--export-menus | --export-menus-cancel | --export-menus-all]
//     [--old-numbered] [--replace]
//     After each file, choose Tools > Export JPEG and Tools > Export TIFF,
//     answering their dialogs (OK, or Cancel for the settings and format
//     dialogs), and print each dialog's title and what was written.
//     --export-menus-all ticks Extract All and prints the numbered files;
//     --old-numbered leaves files named as the first of them and as the
//     name chosen beforehand;
//     --replace answers Replace when asked (otherwise Cancel).
//
// gui ... --detail-dlg STEPS [--mark X,Y[/X,Y...]] [--detail-dlg-at N]
//     [--auto-reprocess] [--no-decode-image]
//     After each file, click the image at each --mark pixel. After the first
//     file (or file N), choose
//     Options > Scan Segment > Detailed Decode and answer its dialog with
//     STEPS, a comma-separated list of: load (click Load Coords), x=N, y=N,
//     len=N, on, off (the Enable box), ok and cancel ("+" chooses the menu
//     item again, for the steps after it). Each time the dialog
//     shows, its fields are printed; if OK is refused (a message box), the
//     steps after it carry on. Then print the settings chosen and a summary
//     of the report's detailed decode, as it is and after File > Reprocess
//     (and after --cycle-channels). For the other files, print the summary.
//     --auto-reprocess turns on Automatic Reprocessing; --no-decode-image
//     turns off Options > Scan Segment > Decode Image.
//
// gui ... --about
//     After the first file, choose Help > About JPEGsnoop and print its menu
//     role, what the dialog shows (texts, links, the icon's size, pixel ratio
//     and number of colours), then click OK.
//
// Always runs offscreen and keeps any settings in a temporary folder, so it
// never shows a window or touches the user's JPEGsnoop settings.

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFileDialog>
#include <QCoreApplication>
#include <QDate>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QPointingDevice>
#include <QSet>
#include <QSettings>
#include <QStyle>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextStream>
#include <QTimer>
#include <QtMath>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifdef Q_OS_MACOS
#include <mach/mach.h>
#else
#include <sys/resource.h>
#endif

#include "DbSigs.h"
#include "DocLog.h"
#include "FileTiff.h"
#include "exportdialogs.h"
#include "ImgDecode.h"
#include "JfifDecode.h"
#include "SnoopConfig.h"
#include "Viewer.h"
#include "WindowBuf.h"
#include "aboutdialog.h"
#include "mainwindow.h"
#include "q_decodedetaildlg.h"
#include "snoopconfigdialog.h"

static void usage()
{
  fprintf(stderr,
          "usage: jpegsnoop-harness decode <file> [--ac] [--histo] [--unknown-exif]\n"
          "                         [--pixels X,Y[/X,Y...]] [--start N] [--detail X,Y,LEN] [--preview-md5]\n"
          "       jpegsnoop-harness gui <file> [<file> ...] [--mouse] [--log] [--reprocess-first]\n"
          "                         [--histo] [--cycle-channels] [--repeat N] [--max-growth-mb N]\n"
          "                         [--zoom PCT] [--max-zoom-mb N] [--check-preview] [--check-histo]\n"
          "                         [--dark] [--check-theme]\n"
          "                         [--export-menus | --export-menus-cancel | --export-menus-all]\n"
          "                         [--old-numbered] [--replace]\n"
          "                         [--detail-dlg STEPS [--mark X,Y[/X,Y...]] [--detail-dlg-at N]\n"
          "                          [--auto-reprocess] [--no-decode-image]] [--about]\n"
          "       jpegsnoop-harness settings\n"
          "       jpegsnoop-harness export-jpeg <file> [--all] [--force-soi] [--force-eoi] [--ignore-eoi]\n"
          "                         [--dht-avi] [--out PATH] [--out-same] [--out-same-case]\n"
          "       jpegsnoop-harness export-tiff <file> --format rgb8|rgb16|ycc8 [--out PATH] [--out-same]\n"
          "       any mode: [--scale F]\n");
}

// What the report's detailed scan decode shows: how many blocks (one
// "Lum (Tbl #0), MCU=[x,y]" line each), the first and last MCU, the block
// labels in order of appearance, how many RGB dumps and rows, the header, and
// the DC of the first 8 blocks' matrices
static QString detailSummary(const QString &strLog)
{
  static const QRegularExpression reBlock(QStringLiteral("^    (\\S+) \\(Tbl #\\d+\\), MCU=\\[(\\d+),(\\d+)\\]$"),
                                          QRegularExpression::MultilineOption);
  static const QRegularExpression reRow(QStringLiteral("^      \\[ x"), QRegularExpression::MultilineOption);
  static const QRegularExpression reHeader(QStringLiteral("^  (Detailed Decode: .*|NOTE: Detailed Decode .*)$"),
                                           QRegularExpression::MultilineOption);

  int nBlocks = 0;
  QString strFirst = QStringLiteral("-");
  QString strLast = QStringLiteral("-");
  QStringList astrLabels;
  QRegularExpressionMatchIterator it = reBlock.globalMatch(strLog);
  while(it.hasNext())
  {
    const QRegularExpressionMatch match = it.next();
    const QString strMcu = match.captured(2) + "," + match.captured(3);
    if(nBlocks == 0)
      strFirst = strMcu;
    strLast = strMcu;
    if(!astrLabels.contains(match.captured(1)))
      astrLabels << match.captured(1);
    nBlocks++;
  }

  int nRows = 0;
  QRegularExpressionMatchIterator itRow = reRow.globalMatch(strLog);
  while(itRow.hasNext())
  {
    itRow.next();
    nRows++;
  }

  static const QRegularExpression reDc(QStringLiteral("DCT Matrix=\\[ *(-?\\d+)"));
  QStringList astrDc;
  QRegularExpressionMatchIterator itDc = reDc.globalMatch(strLog);
  while(itDc.hasNext() && (astrDc.size() < 8))
    astrDc << itDc.next().captured(1);

  const QRegularExpressionMatch matchHeader = reHeader.match(strLog);
  return QString("blocks=%1 first=%2 last=%3 labels=%4 rgb dumps=%5 rows=%6 header=%7 dc=%8")
    .arg(nBlocks)
    .arg(strFirst, strLast)
    .arg(astrLabels.isEmpty() ? QStringLiteral("-") : astrLabels.join('/'))
    .arg(strLog.count(QStringLiteral("Detailed IDCT Dump (RGB):")))
    .arg(nRows)
    .arg(matchHeader.hasMatch() ? matchHeader.captured(1) : QStringLiteral("-"))
    .arg(astrDc.isEmpty() ? QStringLiteral("-") : astrDc.join('/'));
}

// The report without the detailed decode's lines (and blank lines)
static QString withoutDetail(const QString &strLog)
{
  static const QRegularExpression reDetail(
    QStringLiteral("^(    \\S+ \\(Tbl #\\d+\\), MCU=\\[.*|      \\[0x.*|.*DCT Matrix=.*| {20,}\\[.*"
                   "|  Detailed IDCT Dump \\(RGB\\):|    MCU \\[.*|      \\[ x.*|    \\(No IDCT mode: .*"
                   "|  Detailed Decode: .*|  NOTE: Detailed Decode .*|\\s*)$"));
  QStringList astrKept;
  const QStringList astrLines = strLog.split('\n');
  for(const QString &strLine : astrLines)
  {
    if(!reDetail.match(strLine).hasMatch())
      astrKept << strLine;
  }
  return astrKept.join('\n');
}

static QString previewMd5(CimgDecode &img)
{
  QCryptographicHash hash(QCryptographicHash::Md5);
  if(img.m_bDibTempReady && img.m_pDibTemp)
  {
    const QImage imgRgb = img.m_pDibTemp->convertToFormat(QImage::Format_RGB32);
    for(int nY = 0; nY < imgRgb.height(); nY++)
      hash.addData(QByteArrayView(reinterpret_cast<const char *>(imgRgb.constScanLine(nY)), imgRgb.width() * 4));
  }
  return QString::fromLatin1(hash.result().toHex());
}

// Decode strFile in non-interactive mode with the detailed decode as given
// (an empty strDetail: off), returning the report and the preview's MD5
static bool decodeForDetail(const QString &strFile, bool bAc, uint32_t nStart, const QString &strDetail,
                            const QString &strDbDir, QString &strLog, QString &strMd5)
{
  QPlainTextEdit edit;
  CDocLog log(&edit);
  log.Enable();

  CSnoopConfig cfg;
  cfg.setDbPath(strDbDir + "/");
  cfg.setInteractive(false);
  cfg.setAutoReprocess(true);
  cfg.setStartPos(nStart);
  if(bAc)
    cfg.onScansegmentFullidct();

  CDbSigs sigs(&log, &cfg);
  CwindowBuf wbuf(&log);
  CimgDecode img(&log, &wbuf, &cfg, nullptr);
  CjfifDecode jfif(&log, &sigs, &wbuf, &img, &cfg, nullptr);

  if(!strDetail.isEmpty())
  {
    const QStringList astrDetail = strDetail.split(',');
    bool abOk[3] = { false, false, false };
    uint32_t anDetail[3] = { 0, 0, 0 };
    for(int nInd = 0; (nInd < 3) && (astrDetail.size() == 3); nInd++)
      anDetail[nInd] = astrDetail.at(nInd).toUInt(&abOk[nInd]);
    if(!abOk[0] || !abOk[1] || !abOk[2])
      return false;
    img.SetDetailVlc(true, anDetail[0], anDetail[1], anDetail[2]);
  }

  QFile file(strFile);
  if(!file.open(QIODevice::ReadOnly))
    return false;
  if(file.size() > 0)
  {
    wbuf.BufFileSet(&file);
    wbuf.BufLoadWindow(0);
    jfif.ProcessFile(&file);
    wbuf.BufFileUnset();
  }
  file.close();

  strLog = edit.toPlainText();
  strMd5 = previewMd5(img);
  return true;
}

static int runDecode(const QString &strFile, bool bAc, bool bHisto, bool bUnknownExif, const QString &strPixels,
                     uint32_t nStart, const QString &strDetail, bool bPreviewMd5, const QString &strDbDir)
{
  QPlainTextEdit edit;
  CDocLog log(&edit);
  log.Enable();

  CSnoopConfig cfg;                     // defaults; settings are never loaded
  cfg.setDbPath(strDbDir + "/");
  cfg.setInteractive(false);            // no modal error dialogs
  cfg.setAutoReprocess(true);           // option slots would otherwise show a coach note
  cfg.setStartPos(nStart);
  if(bAc)
    cfg.onScansegmentFullidct();
  if(bHisto)
    cfg.onScansegmentHistogram();
  if(bUnknownExif)
    cfg.onOptionsHideuknownexiftags();  // default hides them; this toggles

  CDbSigs sigs(&log, &cfg);
  CwindowBuf wbuf(&log);
  CimgDecode img(&log, &wbuf, &cfg, nullptr);
  CjfifDecode jfif(&log, &sigs, &wbuf, &img, &cfg, nullptr);

  if(!strDetail.isEmpty())
  {
    const QStringList astrDetail = strDetail.split(',');
    bool abOk[3] = { false, false, false };
    uint32_t anDetail[3] = { 0, 0, 0 };
    for(int nInd = 0; (nInd < 3) && (astrDetail.size() == 3); nInd++)
      anDetail[nInd] = astrDetail.at(nInd).toUInt(&abOk[nInd]);
    if(!abOk[0] || !abOk[1] || !abOk[2])
    {
      fprintf(stderr, "--detail needs X,Y,LEN\n");
      return 2;
    }
    img.SetDetailVlc(true, anDetail[0], anDetail[1], anDetail[2]);
  }

  // Count status-bar messages (MainWindow shows these in its status bar)
  int nStatusMsgs = 0;
  auto countStatus = [&nStatusMsgs](QString strMsg, int) {
    if(!strMsg.isEmpty())
      nStatusMsgs++;
  };
  QObject::connect(&jfif, &CjfifDecode::updateStatus, countStatus);
  QObject::connect(&img, &CimgDecode::updateStatus, countStatus);

  QFile file(strFile);
  if(!file.open(QIODevice::ReadOnly))
  {
    fprintf(stderr, "cannot open %s\n", qPrintable(strFile));
    return 2;
  }
  if(file.size() > 0)
  {
    wbuf.BufFileSet(&file);
    wbuf.BufLoadWindow(0);
    jfif.ProcessFile(&file);
    wbuf.BufFileUnset();
  }
  file.close();

  QTextStream out(stdout);
  out << edit.toPlainText() << "\n";
  out << QString("harness: decoded=%1 preview=%2x%3 status=%4\n")
           .arg(img.IsImgDecoded() ? 1 : 0)
           .arg(img.m_bDibTempReady && img.m_pDibTemp ? img.m_pDibTemp->width() : 0)
           .arg(img.m_bDibTempReady && img.m_pDibTemp ? img.m_pDibTemp->height() : 0)
           .arg(nStatusMsgs);

  if(bPreviewMd5)
    out << QString("harness: preview md5=%1\n").arg(previewMd5(img));

  // The detailed decode adds lines to the report and changes nothing else
  if(!strDetail.isEmpty())
  {
    const QString strLog = edit.toPlainText();
    out << "harness: detail " << detailSummary(strLog) << "\n";
    QString strRefLog;
    QString strRefMd5;
    if(decodeForDetail(strFile, bAc, nStart, QString(), strDbDir, strRefLog, strRefMd5))
    {
      out << QString("harness: detail changes report=%1 image=%2\n")
               .arg((withoutDetail(strLog) != withoutDetail(strRefLog)) ? 1 : 0)
               .arg((previewMd5(img) != strRefMd5) ? 1 : 0);
    }
  }

  // Histogram images: how many pixels differ from the background (the top-left
  // pixel; bars grow up from the bottom). -1 if there is no image.
  if(bHisto)
  {
    auto barPixels = [](const QImage *pImg) -> qsizetype {
      if(!pImg || pImg->isNull())
        return -1;
      const QRgb rgbBack = pImg->pixel(0, 0);
      qsizetype nCount = 0;
      for(int nY = 0; nY < pImg->height(); nY++)
        for(int nX = 0; nX < pImg->width(); nX++)
          nCount += (pImg->pixel(nX, nY) != rgbBack) ? 1 : 0;
      return nCount;
    };
    out << QString("harness: histogram bars rgb=%1 y=%2\n")
             .arg(img.isRgbHistogramReady() ? barPixels(img.rgbHistogram()) : -1)
             .arg(img.isYHistogramReady() ? barPixels(img.yHistogram()) : -1);
  }

  // Decoded colours at the requested points ("x,y/x,y")
  const QStringList astrPoints = strPixels.split('/', Qt::SkipEmptyParts);
  for(const QString &strPoint : astrPoints)
  {
    const QStringList astrXy = strPoint.split(',');
    bool bOkX = false;
    bool bOkY = false;
    const int nX = (astrXy.size() == 2) ? astrXy.at(0).toInt(&bOkX) : 0;
    const int nY = (astrXy.size() == 2) ? astrXy.at(1).toInt(&bOkY) : 0;
    if(!bOkX || !bOkY || !img.m_bDibTempReady || !img.m_pDibTemp || !img.m_pDibTemp->valid(nX, nY))
    {
      out << QString("harness: pixel %1 unavailable\n").arg(strPoint);
      continue;
    }
    const QColor col = img.m_pDibTemp->pixelColor(nX, nY);
    out << QString("harness: pixel %1,%2 = %3,%4,%5\n").arg(nX).arg(nY).arg(col.red()).arg(col.green()).arg(col.blue());
  }
  return 0;
}

// Use a throwaway INI settings store in strDir, holding only the answers a
// test needs to get past first-run prompts.
static void useTestSettings(const QString &strDir)
{
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, strDir);

  QSettings settings;
  settings.clear();
  settings.setValue("General\\EulaAccepted", true);   // first-run notice
  settings.setValue("General\\UserDbPath", strDir);   // keep out of ~/Library
}

static int runSettings(const QString &strSettingsDir)
{
  useTestSettings(strSettingsDir);
  QTextStream out(stdout);

  // Fresh install: nothing stored apart from the first-run answer
  {
    CSnoopConfig cfg;
    cfg.RegistryLoad();
    out << "harness: fresh decodeImage=" << cfg.decodeImage() << " coachIdct=" << cfg.coachIdct()
        << " maxErrors=" << cfg.maxDecodeError() << "\n";
  }

  // Options > Maker Notes toggles the maker-note setting
  {
    CSnoopConfig cfg;
    cfg.RegistryLoad();
    cfg.setAutoReprocess(true);      // otherwise each toggle shows a coach note
    out << "harness: makernotes";
    for(int i = 0; i < 3; i++)
    {
      cfg.onOptionsMakernotes();
      out << " " << cfg.decodeMaker();
    }
    out << "\n";
  }

  // A stored setting survives a restart
  {
    CSnoopConfig cfg;
    cfg.RegistryLoad();
    cfg.setMaxErrors(5);
    cfg.Dirty();
    cfg.RegistryStore();
  }
  {
    CSnoopConfig cfg;
    cfg.RegistryLoad();
    out << "harness: maxErrors after restart=" << cfg.maxDecodeError() << "\n";
  }

  // The update-check date is stored as today's yyyyMMdd
  {
    QSettings settings;
    const QString strStored = settings.value("General\\UpdateLastChk").toString();
    out << "harness: lastchk=" << strStored
        << " is_today=" << (strStored == QDate::currentDate().toString("yyyyMMdd")) << "\n";
  }

  // Options > Configuration: Save applies and stores, Reset re-enables tips
  {
    CSnoopConfig cfg;
    cfg.RegistryLoad();
    cfg.setCoachIdct(false);

    SnoopConfigDialog dlg(&cfg);
    QLineEdit *pMaxErrors = dlg.findChild<QLineEdit *>("leMaxErrors");
    QCheckBox *pAutoReprocess = dlg.findChild<QCheckBox *>("cbAutoReprocess");
    QPushButton *pReset = dlg.findChild<QPushButton *>("btnReset");
    QDialogButtonBox *pButtons = dlg.findChild<QDialogButtonBox *>("buttonBox");
    if(!pMaxErrors || !pAutoReprocess || !pReset || !pButtons || !pButtons->button(QDialogButtonBox::Save))
    {
      fprintf(stderr, "configuration dialog widgets not found\n");
      return 1;
    }

    pMaxErrors->setText("7");
    pAutoReprocess->setChecked(true);
    pReset->click();
    pButtons->button(QDialogButtonBox::Save)->click();

    QSettings settings;
    out << "harness: dialog result=" << dlg.result() << " maxErrors=" << cfg.maxDecodeError()
        << " autoReprocess=" << cfg.autoReprocess() << " coachIdct=" << cfg.coachIdct()
        << " stored=" << settings.value("Report\\ErrMaxDecodeScan").toInt() << "\n";
  }

  out << "harness: done\n";
  return 0;
}

static double memoryMb();

static void sendMouse(QWidget *pWidget, QEvent::Type eType, const QPointF &pt)
{
  const Qt::MouseButton eButton = (eType == QEvent::MouseMove) ? Qt::NoButton : Qt::LeftButton;
  const Qt::MouseButtons nButtons = (eType == QEvent::MouseButtonPress) ? Qt::LeftButton : Qt::NoButton;
  QMouseEvent ev(eType, pt, pWidget->mapToGlobal(pt), eButton, nButtons, Qt::NoModifier,
                 QPointingDevice::primaryPointingDevice());
  QCoreApplication::sendEvent(pWidget, &ev);
}

static Q_Viewer *findViewer()
{
  const QWidgetList widgets = QApplication::topLevelWidgets();
  for(QWidget *pWidget : widgets)
  {
    if(Q_Viewer *pViewer = qobject_cast<Q_Viewer *>(pWidget))
      return pViewer;
  }
  return nullptr;
}

static void exerciseMouse()
{
  Q_Viewer *pViewer = findViewer();
  if(!pViewer)
    return;
  PreviewCanvas *pCanvas = pViewer->findChild<PreviewCanvas *>();
  if(!pCanvas)
    return;

  const int w = qMax(1, pCanvas->width());
  const int h = qMax(1, pCanvas->height());
  const QPoint aPts[] = {
    QPoint(0, 0), QPoint(w / 2, h / 2), QPoint(w - 1, h - 1), QPoint(w - 1, 0), QPoint(0, h - 1),
    QPoint(w * 3, h * 3), QPoint(-40, -40), QPoint(-1, h / 2), QPoint(w / 2, -1),
  };
  for(const QPoint &pt : aPts)
  {
    sendMouse(pCanvas, QEvent::MouseMove, pt);
    sendMouse(pCanvas, QEvent::MouseButtonPress, pt);
    sendMouse(pCanvas, QEvent::MouseButtonRelease, pt);
  }
  QEvent evLeave(QEvent::Leave);
  QCoreApplication::sendEvent(pCanvas, &evLeave);
  QCoreApplication::processEvents();
}

// Compare the preview with the decoded image at the current zoom and screen
// scale: the canvas size, where mouse positions land in the image, and the
// pixels drawn, in screen (device) pixels. Below 100% each screen pixel must
// be one pixel of the smoothly reduced image; from 100% each image pixel must
// be a sharp block of zoom x zoom screen pixels.
static void checkPreview(QTextStream &out, const QString &strWhat = QStringLiteral("preview"))
{
  Q_Viewer *pViewer = findViewer();
  PreviewCanvas *pCanvas = pViewer ? pViewer->findChild<PreviewCanvas *>() : nullptr;
  if(!pCanvas || pCanvas->image().isNull())
  {
    out << "harness: " << strWhat << " missing\n";
    return;
  }

  const QImage &img = pCanvas->image();
  const double dZoom = pCanvas->zoom();
  const double dDpr = pCanvas->devicePixelRatioF();
  const double dScale = dZoom / dDpr;               // points per image pixel
  QStringList errors;

  const QSize sizeWant(qCeil(img.width() * dScale), qCeil(img.height() * dScale));
  if(pCanvas->size() != sizeWant)
  {
    errors << QString("canvas %1x%2, want %3x%4")
                .arg(pCanvas->width()).arg(pCanvas->height()).arg(sizeWant.width()).arg(sizeWant.height());
  }

  // Mouse: the centre of an image pixel must map back to that pixel, and
  // points off the image must say so
  QPoint ptGot;
  bool bGotInside = false;
  const QMetaObject::Connection conn =
    QObject::connect(pCanvas, &PreviewCanvas::moved, [&](QPoint pt, bool bInside) {
      ptGot = pt;
      bGotInside = bInside;
    });
  const QPoint aPix[] = { QPoint(0, 0), QPoint(img.width() - 1, img.height() - 1),
                          QPoint(img.width() / 2, img.height() / 3), QPoint(img.width() - 1, 0) };
  for(const QPoint &ptPix : aPix)
  {
    bGotInside = false;
    sendMouse(pCanvas, QEvent::MouseMove, QPointF((ptPix.x() + 0.5) * dScale, (ptPix.y() + 0.5) * dScale));
    if(!bGotInside || (ptGot != ptPix))
    {
      errors << QString("mouse over pixel %1,%2 gave %3,%4%5")
                  .arg(ptPix.x()).arg(ptPix.y()).arg(ptGot.x()).arg(ptGot.y()).arg(bGotInside ? "" : " (outside)");
    }
  }
  const QPointF aOff[] = { QPointF(-1, -1), QPointF(img.width() * dScale + 1, 0), QPointF(0, img.height() * dScale + 1) };
  for(const QPointF &ptOff : aOff)
  {
    bGotInside = true;
    sendMouse(pCanvas, QEvent::MouseMove, ptOff);
    if(bGotInside)
      errors << QString("mouse at %1,%2 (off the image) reported inside").arg(ptOff.x()).arg(ptOff.y());
  }
  QObject::disconnect(conn);

  // Pixels: all of a reduced preview, or 48x48 points from the middle
  QImage imgRef;
  if(dZoom < 1.0)
  {
    imgRef = img.scaled(qMax(1, qRound(img.width() * dZoom)), qMax(1, qRound(img.height() * dZoom)),
                        Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  }
  QRect rectGrab = (dZoom < 1.0) ? pCanvas->rect()
                                 : QRect(sizeWant.width() / 2 - 24, sizeWant.height() / 2 - 24, 48, 48);
  rectGrab &= pCanvas->rect();
  const QImage imgGrab = pCanvas->grab(rectGrab).toImage();
  const int nOffX = qRound(rectGrab.x() * dDpr);
  const int nOffY = qRound(rectGrab.y() * dDpr);
  int nChecked = 0;
  int nBad = 0;
  for(int y = 0; y < imgGrab.height(); y++)
  {
    for(int x = 0; x < imgGrab.width(); x++)
    {
      const int nDevX = nOffX + x;
      const int nDevY = nOffY + y;
      QRgb nWant;
      if(dZoom < 1.0)
      {
        if((nDevX >= imgRef.width()) || (nDevY >= imgRef.height()))
          continue;
        nWant = imgRef.pixel(nDevX, nDevY);
      }
      else
      {
        const int nPixX = qFloor((nDevX + 0.5) / dZoom);
        const int nPixY = qFloor((nDevY + 0.5) / dZoom);
        if((nPixX >= img.width()) || (nPixY >= img.height()))
          continue;
        nWant = img.pixel(nPixX, nPixY);
      }
      nChecked++;
      if((imgGrab.pixel(x, y) & 0xFFFFFF) != (nWant & 0xFFFFFF))
        nBad++;
    }
  }
  if((nChecked == 0) || (nBad > 0))
    errors << QString("%1 of %2 screen pixels differ").arg(nBad).arg(nChecked);

  out << QString("harness: %1 zoom=%2 scale=%3 canvas=%4x%5 image=%6x%7 pixels=%8: %9\n")
           .arg(strWhat).arg(dZoom).arg(dDpr).arg(pCanvas->width()).arg(pCanvas->height())
           .arg(img.width()).arg(img.height()).arg(nChecked)
           .arg(errors.isEmpty() ? QString("ok") : errors.join("; "));
}

// View > Zoom: pick a zoom ("800" for 800%) and draw the preview at it. With
// nMaxMb >= 0, report whether that took more than nMaxMb of memory.
static void zoomPreview(MainWindow &window, const QString &strZoom, int nMaxMb, QTextStream &out)
{
  QAction *pZoom = nullptr;
  const QList<QAction *> actions = window.findChildren<QAction *>();
  for(QAction *pAction : actions)
  {
    if(pAction->text() == strZoom + "%")
    {
      pZoom = pAction;
      break;
    }
  }
  if(!pZoom)
  {
    out << "harness: no zoom " << strZoom << "%\n";
    return;
  }

  const double dBefore = memoryMb();
  pZoom->trigger();
  QCoreApplication::processEvents();
  if(Q_Viewer *pViewer = findViewer())
    pViewer->grab();                                // paints the visible part
  const double dUsed = memoryMb() - dBefore;

  if(nMaxMb < 0)
    return;
#if defined(__SANITIZE_ADDRESS__) || (defined(__has_feature) && __has_feature(address_sanitizer))
  Q_UNUSED(dUsed);
  out << "harness: zoom memory not measured under AddressSanitizer: ok\n";
#else
  out << QString("harness: zoom %1% used %2 MB (limit %3 MB): %4\n")
           .arg(strZoom)
           .arg(dUsed, 0, 'f', 1)
           .arg(nMaxMb)
           .arg(dUsed <= nMaxMb ? "ok" : "too much");
#endif
}

// Both histograms must fill their label at the screen's resolution
static void checkHistograms(MainWindow &window, QTextStream &out)
{
  for(const char *pszName : { "rgbHistogram", "yHistogram" })
  {
    QLabel *pLabel = window.findChild<QLabel *>(pszName);
    const QPixmap pm = pLabel ? pLabel->pixmap() : QPixmap();
    if(pm.isNull())
    {
      out << "harness: histogram " << pszName << " missing\n";
      continue;
    }
    const qreal dDpr = pLabel->devicePixelRatioF();
    const bool bOk = qFuzzyCompare(pm.devicePixelRatio(), dDpr) && (pm.width() == qRound(pLabel->width() * dDpr));
    out << QString("harness: histogram %1 pixmap=%2x%3 ratio=%4 label width=%5 scale=%6: %7\n")
             .arg(pszName).arg(pm.width()).arg(pm.height()).arg(pm.devicePixelRatio())
             .arg(pLabel->width()).arg(dDpr).arg(bOk ? "ok" : "wrong");
  }
}

// WCAG contrast ratio of two colours, from 1 (same) to 21 (black on white)
static double contrastRatio(const QColor &col1, const QColor &col2)
{
  auto luminance = [](const QColor &col) {
    auto linear = [](double v) { return (v <= 0.04045) ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * linear(col.redF()) + 0.7152 * linear(col.greenF()) + 0.0722 * linear(col.blueF());
  };
  const double d1 = luminance(col1);
  const double d2 = luminance(col2);
  return (qMax(d1, d2) + 0.05) / (qMin(d1, d2) + 0.05);
}

// Roughly the palette macOS gives Qt in Dark Mode
static QPalette darkPalette()
{
  QPalette pal;
  pal.setColor(QPalette::Window, QColor(50, 50, 50));
  pal.setColor(QPalette::WindowText, QColor(223, 223, 223));
  pal.setColor(QPalette::Base, QColor(30, 30, 30));
  pal.setColor(QPalette::AlternateBase, QColor(40, 40, 40));
  pal.setColor(QPalette::Text, QColor(223, 223, 223));
  pal.setColor(QPalette::Button, QColor(70, 70, 70));
  pal.setColor(QPalette::ButtonText, QColor(223, 223, 223));
  pal.setColor(QPalette::Highlight, QColor(0, 88, 208));
  pal.setColor(QPalette::HighlightedText, Qt::white);
  return pal;
}

// Lowest contrast between any run of report text and the report background
static double lowestContrast(QPlainTextEdit *pEdit, int &nRuns, int &nColours)
{
  QSet<QRgb> colours;
  const QColor colBase = pEdit->palette().color(QPalette::Base);
  const QColor colText = pEdit->palette().color(QPalette::Text);
  double dLowest = 21.0;
  nRuns = 0;
  for(QTextBlock block = pEdit->document()->begin(); block.isValid(); block = block.next())
  {
    for(QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
    {
      const QTextFragment frag = it.fragment();
      if(!frag.isValid() || frag.text().trimmed().isEmpty())
        continue;
      const QBrush brush = frag.charFormat().foreground();
      const QColor col = (brush.style() == Qt::NoBrush) ? colText : brush.color();
      dLowest = qMin(dLowest, contrastRatio(col, colBase));
      colours.insert(col.rgb());
      nRuns++;
    }
  }
  nColours = static_cast<int>(colours.size());
  return dLowest;
}

static void reportContrast(MainWindow &window, const QString &strWhen, QTextStream &out)
{
  int nRuns = 0;
  int nColours = 0;
  const double dLowest = lowestContrast(window.textEdit, nRuns, nColours);
  const bool bDark = window.textEdit->palette().color(QPalette::Base).lightness() < 128;
  out << QString("harness: theme %1 %2: lowest contrast %3 over %4 runs in %5 colours: %6\n")
           .arg(bDark ? "dark" : "light").arg(strWhen).arg(dLowest, 0, 'f', 2).arg(nRuns).arg(nColours)
           .arg(((nRuns > 0) && (dLowest >= 4.5)) ? "ok" : "low");
}

// The report as written, then after switching to the other scheme and back
// (as when the user switches macOS between Light and Dark while it is open)
static void checkTheme(MainWindow &window, QTextStream &out)
{
  const bool bStartDark = window.textEdit->palette().color(QPalette::Base).lightness() < 128;
  const QPalette palStart = QApplication::palette();
  const QPalette palOther = bStartDark ? QApplication::style()->standardPalette() : darkPalette();

  reportContrast(window, "as written", out);
  QApplication::setPalette(palOther);
  QCoreApplication::processEvents();
  reportContrast(window, "after switching", out);
  QApplication::setPalette(palStart);
  QCoreApplication::processEvents();
  reportContrast(window, "after switching back", out);
}

struct GuiOptions
{
  bool bMouse = false;            // --mouse
  bool bLog = false;              // --log
  bool bReprocessFirst = false;   // --reprocess-first
  bool bHisto = false;            // --histo: RGB and Y histograms on
  bool bCycleChannels = false;    // --cycle-channels
  int nRepeat = 1;                // --repeat N
  int nMaxGrowthMb = -1;          // --max-growth-mb N (-1: don't measure)
  QString strZoom;                // --zoom PCT
  int nMaxZoomMb = -1;            // --max-zoom-mb N (-1: don't measure)
  bool bCheckPreview = false;     // --check-preview
  bool bCheckHisto = false;       // --check-histo
  bool bDark = false;             // --dark
  bool bCheckTheme = false;       // --check-theme
  int nExportMenus = 0;           // --export-menus (1), --export-menus-cancel (2) or --export-menus-all (3)
  bool bOldNumbered = false;      // --old-numbered
  bool bReplace = false;          // --replace
  QString strDetailDlg;           // --detail-dlg STEPS
  QString strMarks;               // --mark X,Y[/X,Y...]
  int nDetailDlgAt = 1;           // --detail-dlg-at N
  bool bAutoReprocess = false;    // --auto-reprocess
  bool bNoDecodeImage = false;    // --no-decode-image
  bool bAbout = false;            // --about
};

// Memory the process is using, as Activity Monitor shows it ("Memory")
static double memoryMb()
{
#ifdef Q_OS_MACOS
  task_vm_info_data_t info;
  mach_msg_type_number_t nCount = TASK_VM_INFO_COUNT;
  if(task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &nCount) != KERN_SUCCESS)
    return 0.0;
  return static_cast<double>(info.phys_footprint) / 1048576.0;
#else
  struct rusage sUsage;
  getrusage(RUSAGE_SELF, &sUsage);
  return static_cast<double>(sUsage.ru_maxrss) / 1024.0;    // KB on Linux
#endif
}

// View > Image Chan: show each channel in turn (each re-renders the preview)
static void cycleChannels(MainWindow &window)
{
  static const char *apszChannels[] = { "YCC", "R", "G", "B", "Y (Greyscale)", "Cb", "Cr", "RGB" };
  const QList<QAction *> actions = window.findChildren<QAction *>();
  for(const char *pszName : apszChannels)
  {
    for(QAction *pAction : actions)
    {
      if(pAction->text() == QLatin1String(pszName))
      {
        pAction->trigger();
        QCoreApplication::processEvents();
        break;
      }
    }
  }
}

// Settings for a MainWindow: a fresh install (so "Decode Image" etc. take
// their defaults), minus the coach tips, which are modal and can't be clicked
// away here
static void useGuiSettings(const QString &strSettingsDir, const GuiOptions &opts)
{
  useTestSettings(strSettingsDir);
  QSettings settings;
  settings.setValue("Coach\\ReprocessAuto", false);
  settings.setValue("Coach\\DecodeIdct", false);
  if(opts.bHisto)
  {
    settings.setValue("General\\DecScanHisto", true);
    settings.setValue("General\\DumpHistoY", true);
  }
  if(opts.bAutoReprocess)
    settings.setValue("General\\ReprocessAuto", true);
  if(opts.bNoDecodeImage)
    settings.setValue("General\\DecScanImg", false);
}

// A message box (such as "Couldn't open file") waits for a click that never
// comes here: print its text and close it. A question is cancelled, or with
// bReplace, a "Replace" button is clicked.
static void closeMessageBoxes(QTimer &timer, QTextStream &out, bool bReplace = false)
{
  QObject::connect(&timer, &QTimer::timeout, [&out, bReplace]() {
    if(QMessageBox *pBox = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
    {
      out << "harness: message box: " << pBox->text();
      if(!pBox->informativeText().isEmpty())
        out << " " << pBox->informativeText();
      const QList<QAbstractButton *> buttons = pBox->buttons();
      for(QAbstractButton *pButton : buttons)
      {
        if(bReplace && (pButton->text() == "Replace"))
        {
          out << " [Replace]\n";
          pButton->click();
          return;
        }
      }
      out << "\n";
      pBox->done(QMessageBox::Ok);
    }
  });
  timer.start(100);
}

static QString fileMd5(const QString &strPath)
{
  QFile file(strPath);
  if(!file.open(QIODevice::ReadOnly))
    return QStringLiteral("-");
  QCryptographicHash hash(QCryptographicHash::Md5);
  hash.addData(&file);
  return QString::fromLatin1(hash.result().toHex());
}

// The report from the last "*** Exporting" header on
static QString exportReport(MainWindow &window)
{
  const QString strLog = window.textEdit->toPlainText();
  const qsizetype nInd = strLog.lastIndexOf("*** Exporting");
  return (nInd < 0) ? QString("(no export in the report)") : strLog.mid(nInd);
}

static QAction *findAction(MainWindow &window, const QString &strText)
{
  const QList<QAction *> actions = window.findChildren<QAction *>();
  for(QAction *pAction : actions)
  {
    if(pAction->text() == strText)
      return pAction;
  }
  return nullptr;
}

// Tools > Export JPEG and Export TIFF as a user chooses them: answer each
// dialog (OK, or with --export-menus-cancel, Cancel for the settings and
// format dialogs, which come before and after the file dialog) and report
// what was written to strBase.jpg and strBase.tif. With --export-menus-all,
// tick Extract All, and report the numbered files strBase.000001.jpg, ...
// (with --old-numbered, strBase.000001.jpg holds "old" beforehand).
static void exportMenus(MainWindow &window, const GuiOptions &opts, const QString &strBase, QTextStream &out)
{
  const bool bCancel = (opts.nExportMenus == 2);
  const bool bAll = (opts.nExportMenus == 3);
  const QString strOld = strBase + ".000001.jpg";
  if(opts.bOldNumbered)
  {
    // And the name chosen, which Extract All doesn't write, so shouldn't ask about
    for(const QString &strPath : { strOld, strBase + ".jpg" })
    {
      QFile fileOld(strPath);
      if(fileOld.open(QIODevice::WriteOnly))
        fileOld.write("old");
    }
  }

  QTimer timer;
  QObject::connect(&timer, &QTimer::timeout, [&]() {
    QWidget *pModal = QApplication::activeModalWidget();
    if(QFileDialog *pFileDlg = qobject_cast<QFileDialog *>(pModal))
    {
      out << "harness: dialog: " << pFileDlg->windowTitle() << " default="
          << QFileInfo(pFileDlg->selectedFiles().value(0)).fileName() << "\n";
      // selectFile() leaves the name field alone while it has the focus
      if(QLineEdit *pName = pFileDlg->findChild<QLineEdit *>("fileNameEdit"))
        pName->setText(strBase + "." + pFileDlg->defaultSuffix());
      static_cast<QDialog *>(pFileDlg)->accept();   // QFileDialog::accept() is protected
    }
    else if(ExportJpegDialog *pJpegDlg = qobject_cast<ExportJpegDialog *>(pModal))
    {
      const QLabel *pOffset = pJpegDlg->findChild<QLabel *>();
      const QCheckBox *pDht = pJpegDlg->findChild<QCheckBox *>("cbDhtAvi");
      out << "harness: dialog: " << pJpegDlg->windowTitle() << " (" << (pOffset ? pOffset->text() : QString("?"))
          << ", MJPEG DHT " << ((pDht && pDht->isChecked()) ? "on" : "off") << ")\n";
      if(QCheckBox *pAll = pJpegDlg->findChild<QCheckBox *>("cbExtractAll"))
        pAll->setChecked(bAll);
      bCancel ? pJpegDlg->reject() : pJpegDlg->accept();
    }
    else if(ExportTiffDialog *pTiffDlg = qobject_cast<ExportTiffDialog *>(pModal))
    {
      const QWidget *pYcc = pTiffDlg->findChild<QWidget *>("rbYcc8");
      out << "harness: dialog: " << pTiffDlg->windowTitle() << " (YCC "
          << ((pYcc && pYcc->isEnabled()) ? "available" : "unavailable") << ")\n";
      bCancel ? pTiffDlg->reject() : pTiffDlg->accept();
    }
  });
  timer.start(50);

  for(const char *pszAction : { "Export JPEG...", "Export TIFF..." })
  {
    QAction *pAction = findAction(window, QLatin1String(pszAction));
    if(!pAction || !pAction->isEnabled())
    {
      out << "harness: menu " << pszAction << " unavailable\n";
      continue;
    }
    const bool bJpeg = (QLatin1String(pszAction) == QLatin1String("Export JPEG..."));
    const QString strPath = strBase + (bJpeg ? ".jpg" : ".tif");
    pAction->trigger();
    QCoreApplication::processEvents();
    if(bJpeg && bAll)
    {
      const QFileInfo fiBase(strBase + ".jpg");
      const QDir dir(fiBase.path());
      const QStringList astrNames =
        dir.entryList(QStringList() << fiBase.completeBaseName() + ".*.jpg", QDir::Files, QDir::Name);
      QStringList astrMd5;
      for(const QString &strName : astrNames)
        astrMd5 << fileMd5(dir.filePath(strName));
      QFile fileOld(strOld);
      const bool bOldKept = fileOld.open(QIODevice::ReadOnly) && (fileOld.readAll() == "old");
      out << "harness: menu " << pszAction << " all files=" << astrNames.size() << " md5s=" << astrMd5.join(',')
          << " old kept=" << (bOldKept ? 1 : 0) << "\n";
      continue;
    }
    out << "harness: menu " << pszAction << " wrote=" << (QFileInfo::exists(strPath) ? 1 : 0)
        << " size=" << QFileInfo(strPath).size() << "\n";
  }
}

// Click the preview at each image pixel "x,y/x,y" (marking positions, as for
// Detailed Decode's Load Coords)
static void markPositions(const QString &strMarks, QTextStream &out)
{
  Q_Viewer *pViewer = findViewer();
  PreviewCanvas *pCanvas = pViewer ? pViewer->findChild<PreviewCanvas *>() : nullptr;
  if(!pCanvas || pCanvas->image().isNull())
  {
    out << "harness: no image to mark\n";
    return;
  }

  const double dScale = static_cast<double>(pCanvas->width()) / pCanvas->image().width();
  int nMarked = 0;
  const QStringList astrPoints = strMarks.split('/', Qt::SkipEmptyParts);
  for(const QString &strPoint : astrPoints)
  {
    const QStringList astrXy = strPoint.split(',');
    if(astrXy.size() != 2)
      continue;
    const QPointF pt((astrXy.at(0).toInt() + 0.5) * dScale, (astrXy.at(1).toInt() + 0.5) * dScale);
    sendMouse(pCanvas, QEvent::MouseMove, pt);
    sendMouse(pCanvas, QEvent::MouseButtonPress, pt);
    sendMouse(pCanvas, QEvent::MouseButtonRelease, pt);
    nMarked++;
  }
  out << "harness: clicked " << nMarked << " positions\n";
}

static CimgDecode *imgDecoder(MainWindow &window)
{
  return window.findChild<CimgDecode *>();
}

static QString detailSetting(MainWindow &window)
{
  bool bEn = false;
  uint32_t nX = 0;
  uint32_t nY = 0;
  uint32_t nLen = 0;
  if(CimgDecode *pImg = imgDecoder(window))
    pImg->GetDetailVlc(bEn, nX, nY, nLen);
  return QString("en=%1 x=%2 y=%3 len=%4").arg(bEn ? 1 : 0).arg(nX).arg(nY).arg(nLen);
}

// Options > Scan Segment > Detailed Decode as a user would choose it (see
// --detail-dlg above)
static void detailedDecodeMenu(MainWindow &window, const GuiOptions &opts, QTextStream &out)
{
  QAction *pAction = findAction(window, QStringLiteral("Detailed Decode..."));
  if(!pAction || !pAction->isEnabled())
  {
    out << "harness: menu Detailed Decode unavailable\n";
    return;
  }

  // "+" separates the steps for each time the menu item is chosen
  const QStringList astrOpenings = opts.strDetailDlg.split('+');
  QStringList astrSteps;

  QTimer timer;
  QObject::connect(&timer, &QTimer::timeout, [&]() {
    Q_DecodeDetailDlg *pDlg = qobject_cast<Q_DecodeDetailDlg *>(QApplication::activeModalWidget());
    if(!pDlg)
      return;
    QLineEdit *pX = pDlg->findChild<QLineEdit *>("leMcuX");
    QLineEdit *pY = pDlg->findChild<QLineEdit *>("leMcuY");
    QLineEdit *pLen = pDlg->findChild<QLineEdit *>("leNumMcus");
    QCheckBox *pEn = pDlg->findChild<QCheckBox *>("cbEnableDec");
    QPushButton *pLoad = pDlg->findChild<QPushButton *>("pbLoadCoords");
    QLabel *pNote = pDlg->findChild<QLabel *>("lblNote");
    QDialogButtonBox *pButtons = pDlg->findChild<QDialogButtonBox *>("buttonBox");
    if(!pX || !pY || !pLen || !pEn || !pLoad || !pNote || !pButtons)
    {
      out << "harness: detail dialog widgets not found\n";
      static_cast<QDialog *>(pDlg)->reject();
      return;
    }

    auto fields = [&]() {
      return QString("X=%1 Y=%2 len=%3 enabled=%4").arg(pX->text(), pY->text(), pLen->text()).arg(pEn->isChecked() ? 1 : 0);
    };
    out << "harness: detail dialog: " << pDlg->windowTitle() << " (" << fields()
        << " load=" << (pLoad->isEnabled() ? "on" : "off")
        << " note=" << (pNote->isVisibleTo(pDlg) ? pNote->text() : QString("-")) << ")\n";

    while(!astrSteps.isEmpty())
    {
      const QString strStep = astrSteps.takeFirst();
      if(strStep == "load")
        pLoad->click();               // does nothing while it's unavailable
      else if(strStep.startsWith("x="))
        pX->setText(strStep.mid(2));
      else if(strStep.startsWith("y="))
        pY->setText(strStep.mid(2));
      else if(strStep.startsWith("len="))
        pLen->setText(strStep.mid(4));
      else if((strStep == "on") || (strStep == "off"))
        pEn->setChecked(strStep == "on");
      else if((strStep == "ok") || (strStep == "cancel"))
      {
        out << "harness: detail dialog " << strStep << " (" << fields() << ")\n";
        out.flush();
        // A refused OK shows a message box: the steps left carry on after it
        pButtons->button((strStep == "ok") ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
        return;
      }
      else
        out << "harness: unknown detail step " << strStep << "\n";
    }

    out << "harness: detail dialog still open: cancelled\n";
    static_cast<QDialog *>(pDlg)->reject();
  });
  timer.start(50);

  for(const QString &strOpening : astrOpenings)
  {
    astrSteps = strOpening.split(',', Qt::SkipEmptyParts);
    pAction->trigger();
    QCoreApplication::processEvents();
  }
  timer.stop();

  out << "harness: detail set " << detailSetting(window) << "\n";
  out << "harness: detail report now " << detailSummary(window.textEdit->toPlainText()) << " marks="
      << window.textEdit->toPlainText().count(QStringLiteral("Position Marked @")) << "\n";
  window.reprocess();
  QCoreApplication::processEvents();
  out << "harness: detail report after reprocess " << detailSummary(window.textEdit->toPlainText()) << "\n";
  if(opts.bCycleChannels)
  {
    cycleChannels(window);
    out << "harness: detail report after channels " << detailSummary(window.textEdit->toPlainText()) << "\n";
  }
  out.flush();
}

// Help > About JPEGsnoop as a user chooses it (see --about above)
static void aboutMenu(MainWindow &window, QTextStream &out)
{
  QAction *pAction = findAction(window, QStringLiteral("&About JPEGsnoop..."));
  if(!pAction || !pAction->isEnabled())
  {
    out << "harness: menu About unavailable\n";
    return;
  }
  out << "harness: about role=" << ((pAction->menuRole() == QAction::AboutRole) ? "about" : "other") << "\n";

  QTimer timer;
  QObject::connect(&timer, &QTimer::timeout, [&]() {
    AboutDialog *pDlg = qobject_cast<AboutDialog *>(QApplication::activeModalWidget());
    if(!pDlg)
      return;
    auto text = [pDlg](const char *pszName) {
      const QLabel *pLabel = pDlg->findChild<QLabel *>(QLatin1String(pszName));
      return pLabel ? pLabel->text() : QStringLiteral("?");
    };
    out << "harness: about: " << pDlg->windowTitle() << " (" << text("lblName") << " / " << text("lblVersion")
        << " / " << text("lblCopyright") << " / " << text("lblQt") << ")\n";

    static const QRegularExpression reHref(QStringLiteral("href=\"([^\"]*)\">([^<]*)<"));
    QStringList astrLinks;
    bool bExternal = true;
    for(const char *pszName : { "lblDocLink", "lblSiteLink" })
    {
      const QLabel *pLabel = pDlg->findChild<QLabel *>(QLatin1String(pszName));
      const QRegularExpressionMatch match = reHref.match(pLabel ? pLabel->text() : QString());
      astrLinks << (match.hasMatch() ? match.captured(2) + " -> " + match.captured(1) : QStringLiteral("?"));
      bExternal = bExternal && pLabel && pLabel->openExternalLinks();
    }
    out << "harness: about links: " << astrLinks.join(" | ") << " external=" << (bExternal ? 1 : 0) << "\n";

    const QLabel *pIcon = pDlg->findChild<QLabel *>(QStringLiteral("lblIcon"));
    const QImage img = pIcon ? pIcon->pixmap().toImage().convertToFormat(QImage::Format_ARGB32) : QImage();
    QSet<QRgb> setColours;
    for(int nY = 0; nY < img.height(); nY++)
      for(int nX = 0; nX < img.width(); nX++)
        setColours.insert(img.pixel(nX, nY));
    out << QString("harness: about icon=%1x%2 dpr=%3 colours=%4\n")
             .arg(img.width())
             .arg(img.height())
             .arg(pIcon ? pIcon->pixmap().devicePixelRatio() : 0.0)
             .arg(setColours.size());
    out.flush();

    QDialogButtonBox *pButtons = pDlg->findChild<QDialogButtonBox *>(QStringLiteral("buttonBox"));
    if(pButtons && pButtons->button(QDialogButtonBox::Ok))
      pButtons->button(QDialogButtonBox::Ok)->click();
    else
      static_cast<QDialog *>(pDlg)->reject();
  });
  timer.start(50);
  pAction->trigger();
  QCoreApplication::processEvents();
  timer.stop();

  bool bOpen = false;
  for(QWidget *pWidget : QApplication::topLevelWidgets())
    bOpen = bOpen || (qobject_cast<AboutDialog *>(pWidget) && pWidget->isVisible());
  out << "harness: about closed=" << (bOpen ? 0 : 1) << "\n";
}

static int runGui(const QStringList &files, const GuiOptions &opts, const QString &strSettingsDir)
{
  useGuiSettings(strSettingsDir, opts);

  if(opts.bDark)
    QApplication::setPalette(darkPalette());

  MainWindow window;
  window.show();
  QCoreApplication::processEvents();

  QTextStream out(stdout);

  QTimer timerMsgBox;
  closeMessageBoxes(timerMsgBox, out, opts.bReplace);

  // File > Reprocess (or auto-reprocess after an option change) before any
  // file has been opened
  if(opts.bReprocessFirst)
  {
    window.reprocess();
    out << "harness: reprocessed with no file\n";
  }

  double dMemAfterFirst = 0.0;

  for(int nRound = 0; nRound < opts.nRepeat; nRound++)
  {
    int nFileInd = 0;
    for(const QString &strFile : files)
    {
      nFileInd++;
      window.openFile(strFile);
      QCoreApplication::processEvents();

      if(nRound == 0)
      {
        const QString strLog = window.textEdit->toPlainText();
        out << QString("harness: file=%1 scan=%2\n")
                 .arg(QFileInfo(strFile).fileName())
                 .arg(strLog.contains("*** Decoding SCAN Data ***") ? 1 : 0);
        if(opts.bLog)
          out << strLog << "\n";
        out.flush();
      }

      if(nRound == 0)
      {
        if(!opts.strZoom.isEmpty())
          zoomPreview(window, opts.strZoom, opts.nMaxZoomMb, out);
        if(opts.bCheckPreview)
          checkPreview(out);
        if(opts.bCheckHisto)
          checkHistograms(window, out);
        if(opts.bCheckTheme)
          checkTheme(window, out);
        if(opts.nExportMenus != 0)
          exportMenus(window, opts, strSettingsDir + "/menu-export-" + QString::number(nFileInd), out);
        if(opts.bAbout && (nFileInd == 1))
          aboutMenu(window, out);
        if(!opts.strDetailDlg.isEmpty())
        {
          if(!opts.strMarks.isEmpty())
            markPositions(opts.strMarks, out);
          // The setting stays for the files after it
          if(nFileInd == opts.nDetailDlgAt)
            detailedDecodeMenu(window, opts, out);
          else
            out << "harness: detail report " << detailSummary(window.textEdit->toPlainText()) << "\n";
        }
        out.flush();
      }

      if(opts.bMouse)
        exerciseMouse();
      if(opts.bCycleChannels)
      {
        cycleChannels(window);
        if((nRound == 0) && opts.bCheckPreview)
          checkPreview(out, "preview after channels");
      }
    }

    // Measure from the end of the first round, once caches have warmed up
    if(nRound == 0)
      dMemAfterFirst = memoryMb();
  }

  if(opts.nMaxGrowthMb >= 0)
  {
#if defined(__SANITIZE_ADDRESS__) || (defined(__has_feature) && __has_feature(address_sanitizer))
    // ASan keeps freed memory in quarantine, so growth means nothing here
    out << "harness: memory growth not measured under AddressSanitizer: ok\n";
#else
    const double dGrowth = memoryMb() - dMemAfterFirst;
    out << QString("harness: memory growth %1 MB over %2 more rounds (limit %3 MB): %4\n")
             .arg(dGrowth, 0, 'f', 1)
             .arg(opts.nRepeat - 1)
             .arg(opts.nMaxGrowthMb)
             .arg(dGrowth <= opts.nMaxGrowthMb ? "ok" : "too much");
#endif
  }

  out << "harness: done\n";
  return 0;
}

struct ExportOptions
{
  ExportJpegSettings sJpeg;       // --all, --force-soi, --force-eoi, --ignore-eoi, --dht-avi
  QString strOut;                 // --out PATH
  bool bOutSame = false;          // --out-same
  bool bOutSameCase = false;      // --out-same-case
};

static int runExportJpeg(const QString &strFile, const ExportOptions &opts, const QString &strTmpDir)
{
  useGuiSettings(strTmpDir, GuiOptions());

  MainWindow window;
  window.show();
  QCoreApplication::processEvents();

  QTextStream out(stdout);
  QTimer timerMsgBox;
  closeMessageBoxes(timerMsgBox, out);

  const QString strSrcMd5 = fileMd5(strFile);
  window.openFile(strFile);
  QCoreApplication::processEvents();

  QString strOut = opts.strOut;
  if(opts.bOutSame)
  {
    strOut = strFile;
  }
  else if(opts.bOutSameCase)
  {
    const QFileInfo fiSrc(strFile);
    QString strName = fiSrc.fileName();
    for(QChar &ch : strName)
      ch = ch.isUpper() ? ch.toLower() : ch.toUpper();
    strOut = fiSrc.dir().filePath(strName);
    if(!QFileInfo::exists(strOut))
    {
      out << "harness: case-sensitive disk, skipped\n";
      return 0;
    }
  }
  else if(strOut.isEmpty())
  {
    QDir().mkpath(strTmpDir + "/export");
    strOut = strTmpDir + "/export/out.jpg";
  }

  const bool bOk = window.exportJpeg(strOut, opts.sJpeg);
  QCoreApplication::processEvents();

  // What was written: out.jpg, or with Extract All out.000001.jpg, ...
  QStringList astrWritten;
  const QFileInfo fiOut(strOut);
  if(opts.sJpeg.bExtractAll)
  {
    const QDir dir(fiOut.path());
    const QStringList astrNames =
      dir.entryList(QStringList() << fiOut.completeBaseName() + ".*.jpg", QDir::Files, QDir::Name);
    for(const QString &strName : astrNames)
      astrWritten << dir.filePath(strName);
  }
  else if(fiOut.exists() && !opts.bOutSame && !opts.bOutSameCase)
  {
    astrWritten << strOut;
  }

  out << "harness: export ok=" << (bOk ? 1 : 0) << "\n";
  QStringList astrMd5;
  for(const QString &strPath : astrWritten)
  {
    astrMd5 << fileMd5(strPath);
    out << "harness: wrote " << QFileInfo(strPath).fileName() << " size=" << QFileInfo(strPath).size()
        << " md5=" << astrMd5.last() << "\n";
  }
  out << "harness: exported " << astrWritten.size() << " md5s=" << astrMd5.join(',') << "\n";
  out << "harness: source unchanged=" << ((fileMd5(strFile) == strSrcMd5) ? 1 : 0) << "\n";
  out << exportReport(window) << "\n";
  out << "harness: done\n";
  return 0;
}

// Read a TIFF that Export TIFF wrote and check its tags and every sample
// against a separate decode of strSrc with the default settings (as the
// MainWindow used). Returns the "harness: tiff" line.
static QString checkTiff(const QString &strSrc, const QString &strTiff, FileTiff::teFormat eFormat,
                         const QString &strDbDir)
{
  QFile fileTiff(strTiff);
  if(!fileTiff.open(QIODevice::ReadOnly))
    return "harness: tiff check=cannot read the file";
  const QByteArray ab = fileTiff.readAll();
  const qsizetype nSize = ab.size();

  auto be16 = [&ab, nSize](quint64 nPos) -> quint32 {
    return (nPos + 2 <= static_cast<quint64>(nSize))
             ? (static_cast<quint32>(static_cast<uchar>(ab[static_cast<qsizetype>(nPos)])) << 8) |
                 static_cast<uchar>(ab[static_cast<qsizetype>(nPos + 1)])
             : 0xFFFFFFFFu;
  };
  auto be32 = [&be16](quint64 nPos) -> quint32 { return (be16(nPos) << 16) | (be16(nPos + 2) & 0xFFFF); };

  if(!ab.startsWith(QByteArray("MM\0*", 4)) || (be32(4) != 8))
    return "harness: tiff check=bad header";

  // Tag -> values (a RATIONAL gives numerator, denominator)
  QMap<quint32, QList<quint32>> mapTags;
  QStringList errors;
  const quint32 nEntries = be16(8);
  quint32 nPrevTag = 0;
  for(quint32 nInd = 0; nInd < nEntries; nInd++)
  {
    const quint64 nPos = 10 + 12ULL * nInd;
    const quint32 nTag = be16(nPos);
    const quint32 nType = be16(nPos + 2);
    const quint32 nCount = be32(nPos + 4);
    const quint32 nTypeSize = (nType == 3) ? 2 : (nType == 4) ? 4 : (nType == 5) ? 8 : 0;
    if((nTypeSize == 0) || (nCount == 0) || (nCount > 64))
    {
      errors << QString("tag %1 has type %2 count %3").arg(nTag).arg(nType).arg(nCount);
      continue;
    }
    if(nTag <= nPrevTag)
      errors << QString("tag %1 out of order").arg(nTag);
    nPrevTag = nTag;
    const quint64 nValPos = (nTypeSize * nCount <= 4) ? nPos + 8 : be32(nPos + 8);
    if(nValPos % 2)
      errors << QString("tag %1 value at an odd offset").arg(nTag);
    QList<quint32> anVals;
    for(quint32 nVal = 0; nVal < nCount; nVal++)
    {
      if(nType == 3)
        anVals << be16(nValPos + 2ULL * nVal);
      else if(nType == 4)
        anVals << be32(nValPos + 4ULL * nVal);
      else
        anVals << be32(nValPos + 8ULL * nVal) << be32(nValPos + 8ULL * nVal + 4);
    }
    mapTags[nTag] = anVals;
  }
  if(be32(10 + 12ULL * nEntries) != 0)
    errors << "has a next directory";

  // The separate decode
  QPlainTextEdit edit;
  CDocLog log(&edit);
  log.Enable();
  CSnoopConfig cfg;
  cfg.setDbPath(strDbDir + "/");
  cfg.setInteractive(false);
  cfg.setAutoReprocess(true);
  CDbSigs sigs(&log, &cfg);
  CwindowBuf wbuf(&log);
  CimgDecode img(&log, &wbuf, &cfg, nullptr);
  CjfifDecode jfif(&log, &sigs, &wbuf, &img, &cfg, nullptr);
  QFile fileSrc(strSrc);
  if(!fileSrc.open(QIODevice::ReadOnly) || (fileSrc.size() == 0))
    return "harness: tiff check=cannot decode the source";
  wbuf.BufFileSet(&fileSrc);
  wbuf.BufLoadWindow(0);
  jfif.ProcessFile(&fileSrc);
  wbuf.BufFileUnset();
  if(!img.m_bDibTempReady || !img.m_pDibTemp)
    return "harness: tiff check=the source has no decoded image";
  const QImage imgRef = img.m_pDibTemp->convertToFormat(QImage::Format_RGB32);
  uint32_t nW = static_cast<uint32_t>(imgRef.width());
  uint32_t nH = static_cast<uint32_t>(imgRef.height());
  uint32_t nFrameX = 0;
  uint32_t nFrameY = 0;
  if(img.GetFrameSize(nFrameX, nFrameY))
  {
    nW = qMin(nW, nFrameX);
    nH = qMin(nH, nFrameY);
  }

  const bool bYcc = (eFormat == FileTiff::FMT_YCC8);
  const quint32 nBits = (eFormat == FileTiff::FMT_RGB16) ? 16 : 8;
  const quint32 nWhite = (nBits == 16) ? 65535 : 255;
  const quint64 nDataSize = static_cast<quint64>(nW) * nH * 3 * (nBits / 8);
  const QList<quint32> anRefBw = bYcc ? QList<quint32>{ 0, 1, 255, 1, 128, 1, 255, 1, 128, 1, 255, 1 }
                                      : QList<quint32>{ 0, 1, nWhite, 1, 0, 1, nWhite, 1, 0, 1, nWhite, 1 };
  const QList<QPair<quint32, QList<quint32>>> aWant = {
    { 0x100, { nW } }, { 0x101, { nH } }, { 0x102, { nBits, nBits, nBits } }, { 0x103, { 1 } },
    { 0x106, { bYcc ? 6u : 2u } }, { 0x115, { 3 } }, { 0x116, { nH } },
    { 0x117, { static_cast<quint32>(nDataSize) } }, { 0x11C, { 1 } }, { 0x214, anRefBw },
  };
  for(const auto &want : aWant)
  {
    if(mapTags.value(want.first) != want.second)
      errors << QString("tag %1 wrong").arg(want.first, 0, 16);
  }
  if(bYcc && (mapTags.value(0x212) != QList<quint32>{ 1, 1 }))
    errors << "YCbCrSubSampling wrong";

  const quint64 nData = mapTags.value(0x111).value(0, 0);
  quint64 nBad = 0;
  if(nData + nDataSize != static_cast<quint64>(nSize))
  {
    errors << QString("pixel data at %1 isn't the rest of the file").arg(nData);
  }
  else
  {
    const uchar *pData = reinterpret_cast<const uchar *>(ab.constData()) + nData;
    for(uint32_t nY = 0; nY < nH; nY++)
    {
      for(uint32_t nX = 0; nX < nW; nX++)
      {
        int anWant[3];
        if(bYcc)
        {
          int32_t anYcc[3] = { 0, 0, 0 };
          img.GetPixelYcc(nX, nY, anYcc[0], anYcc[1], anYcc[2]);
          for(int nC = 0; nC < 3; nC++)
            anWant[nC] = (qBound(-1024, anYcc[nC], 1023) + 1024) / 8;
        }
        else
        {
          const QRgb rgb = imgRef.pixel(static_cast<int>(nX), static_cast<int>(nY));
          anWant[0] = qRed(rgb);
          anWant[1] = qGreen(rgb);
          anWant[2] = qBlue(rgb);
        }
        for(int nC = 0; nC < 3; nC++)
        {
          const int nGot = (nBits == 16) ? ((pData[0] << 8) | pData[1]) : pData[0];
          pData += nBits / 8;
          if(nGot != ((nBits == 16) ? anWant[nC] * 257 : anWant[nC]))
            nBad++;
        }
      }
    }
    if(nBad > 0)
      errors << QString("%1 of %2 samples differ").arg(nBad).arg(3ULL * nW * nH);
  }

  return QString("harness: tiff %1x%2 bits=%3 photometric=%4 size=%5 md5=%6 check=%7")
    .arg(mapTags.value(0x100).value(0)).arg(mapTags.value(0x101).value(0)).arg(mapTags.value(0x102).value(0))
    .arg(mapTags.value(0x106).value(0)).arg(nSize).arg(fileMd5(strTiff))
    .arg(errors.isEmpty() ? QString("ok") : errors.join("; "));
}

static int runExportTiff(const QString &strFile, const QString &strFormat, const ExportOptions &opts,
                         const QString &strTmpDir)
{
  FileTiff::teFormat eFormat;
  if(strFormat == "rgb8")
    eFormat = FileTiff::FMT_RGB8;
  else if(strFormat == "rgb16")
    eFormat = FileTiff::FMT_RGB16;
  else if(strFormat == "ycc8")
    eFormat = FileTiff::FMT_YCC8;
  else
  {
    usage();
    return 2;
  }

  useGuiSettings(strTmpDir, GuiOptions());

  MainWindow window;
  window.show();
  QCoreApplication::processEvents();

  QTextStream out(stdout);
  QTimer timerMsgBox;
  closeMessageBoxes(timerMsgBox, out);

  const QString strSrcMd5 = fileMd5(strFile);
  window.openFile(strFile);
  QCoreApplication::processEvents();

  const QString strOut = opts.bOutSame ? strFile : opts.strOut.isEmpty() ? strTmpDir + "/out.tif" : opts.strOut;
  QString strError;
  const bool bOk = window.exportTiff(strOut, eFormat, strError);
  out << "harness: tiff export ok=" << (bOk ? 1 : 0);
  if(!bOk)
    out << " error=" << strError;
  out << "\n";
  if(bOk)
    out << checkTiff(strFile, strOut, eFormat, strTmpDir) << "\n";
  out << "harness: source unchanged=" << ((fileMd5(strFile) == strSrcMd5) ? 1 : 0) << "\n";
  out << exportReport(window) << "\n";
  out << "harness: done\n";
  return 0;
}

int main(int argc, char **argv)
{
  if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");

  // --scale has to be in place before QApplication reads the screens
  for(int i = 1; i + 1 < argc; i++)
  {
    if(std::strcmp(argv[i], "--scale") == 0)
      qputenv("QT_SCALE_FACTOR", argv[i + 1]);
  }

  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("JPEGsnoopTests");
  QCoreApplication::setOrganizationDomain("tests.invalid");
  QCoreApplication::setApplicationName("jpegsnoop-harness");

  QStringList args = app.arguments().mid(1);
  if(args.isEmpty())
  {
    usage();
    return 2;
  }
  const QString strMode = args.takeFirst();
  const bool bAc = args.removeAll("--ac") > 0;
  const bool bHisto = args.removeAll("--histo") > 0;
  const bool bUnknownExif = args.removeAll("--unknown-exif") > 0;
  const bool bPreviewMd5 = args.removeAll("--preview-md5") > 0;
  GuiOptions guiOpts;
  guiOpts.bMouse = args.removeAll("--mouse") > 0;
  guiOpts.bLog = args.removeAll("--log") > 0;
  guiOpts.bReprocessFirst = args.removeAll("--reprocess-first") > 0;
  guiOpts.bHisto = bHisto;
  guiOpts.bCycleChannels = args.removeAll("--cycle-channels") > 0;
  guiOpts.bCheckPreview = args.removeAll("--check-preview") > 0;
  guiOpts.bCheckHisto = args.removeAll("--check-histo") > 0;
  guiOpts.bDark = args.removeAll("--dark") > 0;
  guiOpts.bCheckTheme = args.removeAll("--check-theme") > 0;
  if(args.removeAll("--export-menus") > 0)
    guiOpts.nExportMenus = 1;
  if(args.removeAll("--export-menus-cancel") > 0)
    guiOpts.nExportMenus = 2;
  if(args.removeAll("--export-menus-all") > 0)
    guiOpts.nExportMenus = 3;
  guiOpts.bOldNumbered = args.removeAll("--old-numbered") > 0;
  guiOpts.bReplace = args.removeAll("--replace") > 0;
  guiOpts.bAutoReprocess = args.removeAll("--auto-reprocess") > 0;
  guiOpts.bNoDecodeImage = args.removeAll("--no-decode-image") > 0;
  guiOpts.bAbout = args.removeAll("--about") > 0;
  ExportOptions exportOpts;
  exportOpts.sJpeg.bExtractAll = args.removeAll("--all") > 0;
  exportOpts.sJpeg.bForceSoi = args.removeAll("--force-soi") > 0;
  exportOpts.sJpeg.bForceEoi = args.removeAll("--force-eoi") > 0;
  exportOpts.sJpeg.bIgnoreEoi = args.removeAll("--ignore-eoi") > 0;
  exportOpts.sJpeg.bDhtAviInsert = args.removeAll("--dht-avi") > 0;
  exportOpts.bOutSame = args.removeAll("--out-same") > 0;
  exportOpts.bOutSameCase = args.removeAll("--out-same-case") > 0;

  // Options with a text value (--scale was applied above)
  QString strPixels;
  QString strFormat;
  QString strDetail;
  for(const char *pszOpt : { "--scale", "--zoom", "--pixels", "--out", "--format", "--detail", "--detail-dlg", "--mark" })
  {
    const qsizetype nInd = args.indexOf(QLatin1String(pszOpt));
    if(nInd < 0)
      continue;
    if(nInd + 1 >= args.size())
    {
      usage();
      return 2;
    }
    if(QLatin1String(pszOpt) == QLatin1String("--zoom"))
      guiOpts.strZoom = args.at(nInd + 1);
    else if(QLatin1String(pszOpt) == QLatin1String("--pixels"))
      strPixels = args.at(nInd + 1);
    else if(QLatin1String(pszOpt) == QLatin1String("--out"))
      exportOpts.strOut = args.at(nInd + 1);
    else if(QLatin1String(pszOpt) == QLatin1String("--format"))
      strFormat = args.at(nInd + 1);
    else if(QLatin1String(pszOpt) == QLatin1String("--detail"))
      strDetail = args.at(nInd + 1);
    else if(QLatin1String(pszOpt) == QLatin1String("--detail-dlg"))
      guiOpts.strDetailDlg = args.at(nInd + 1);
    else if(QLatin1String(pszOpt) == QLatin1String("--mark"))
      guiOpts.strMarks = args.at(nInd + 1);
    args.remove(nInd, 2);
  }

  // Options with a numeric value
  uint32_t nStart = 0;
  for(const char *pszOpt : { "--repeat", "--max-growth-mb", "--max-zoom-mb", "--start", "--detail-dlg-at" })
  {
    const qsizetype nInd = args.indexOf(QLatin1String(pszOpt));
    if(nInd < 0)
      continue;
    bool bOk = false;
    const int nVal = (nInd + 1 < args.size()) ? args.at(nInd + 1).toInt(&bOk) : 0;
    if(!bOk || nVal < 0)
    {
      usage();
      return 2;
    }
    args.remove(nInd, 2);
    if(QLatin1String(pszOpt) == QLatin1String("--repeat"))
      guiOpts.nRepeat = qMax(1, nVal);
    else if(QLatin1String(pszOpt) == QLatin1String("--max-growth-mb"))
      guiOpts.nMaxGrowthMb = nVal;
    else if(QLatin1String(pszOpt) == QLatin1String("--start"))
      nStart = static_cast<uint32_t>(nVal);
    else if(QLatin1String(pszOpt) == QLatin1String("--detail-dlg-at"))
      guiOpts.nDetailDlgAt = nVal;
    else
      guiOpts.nMaxZoomMb = nVal;
  }

  QTemporaryDir tmpDir;
  if(!tmpDir.isValid())
  {
    fprintf(stderr, "cannot create a temporary folder\n");
    return 2;
  }

  if(strMode == "decode" && args.size() == 1)
    return runDecode(args.first(), bAc, bHisto, bUnknownExif, strPixels, nStart, strDetail, bPreviewMd5, tmpDir.path());
  if(strMode == "gui" && !args.isEmpty())
    return runGui(args, guiOpts, tmpDir.path());
  if(strMode == "settings" && args.isEmpty())
    return runSettings(tmpDir.path());
  if(strMode == "export-jpeg" && args.size() == 1)
    return runExportJpeg(args.first(), exportOpts, tmpDir.path());
  if(strMode == "export-tiff" && args.size() == 1)
    return runExportTiff(args.first(), strFormat, exportOpts, tmpDir.path());

  usage();
  return 2;
}
