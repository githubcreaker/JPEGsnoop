// Headless test driver for JPEGsnoopQt.
//
// decode <file> [--ac] [--histo] [--unknown-exif] [--pixels X,Y[/X,Y...]]
//     Decode one file with the app's decoder classes (CjfifDecode, CimgDecode,
//     CwindowBuf, ...) in non-interactive mode and print the report to stdout,
//     followed by a "harness:" summary line. --ac: full IDCT; --histo:
//     histograms, and a line counting the pixels drawn in each histogram
//     image; --unknown-exif: also report unknown EXIF tags; --pixels
//     prints the decoded preview's colour at each point.
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
//     after the first round (a leak check).
//     After each file in the first round: --zoom picks that View > Zoom
//     level ("800" for 800%), and --max-zoom-mb reports whether showing it
//     took more than N MB; --check-preview compares the preview's size, mouse
//     mapping and drawn pixels with the decoded image; --check-histo checks
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
// Always runs offscreen and keeps any settings in a temporary folder, so it
// never shows a window or touches the user's JPEGsnoop settings.

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDate>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QLabel>
#include <QMouseEvent>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QPointingDevice>
#include <QSet>
#include <QSettings>
#include <QStyle>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextStream>
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
#include "ImgDecode.h"
#include "JfifDecode.h"
#include "SnoopConfig.h"
#include "Viewer.h"
#include "WindowBuf.h"
#include "mainwindow.h"
#include "snoopconfigdialog.h"

static void usage()
{
  fprintf(stderr,
          "usage: jpegsnoop-harness decode <file> [--ac] [--histo] [--unknown-exif]\n"
          "                         [--pixels X,Y[/X,Y...]]\n"
          "       jpegsnoop-harness gui <file> [<file> ...] [--mouse] [--log] [--reprocess-first]\n"
          "                         [--histo] [--cycle-channels] [--repeat N] [--max-growth-mb N]\n"
          "                         [--zoom PCT] [--max-zoom-mb N] [--check-preview] [--check-histo]\n"
          "                         [--dark] [--check-theme]\n"
          "       jpegsnoop-harness settings\n"
          "       any mode: [--scale F]\n");
}

static int runDecode(const QString &strFile, bool bAc, bool bHisto, bool bUnknownExif, const QString &strPixels,
                     const QString &strDbDir)
{
  QPlainTextEdit edit;
  CDocLog log(&edit);
  log.Enable();

  CSnoopConfig cfg;                     // defaults; settings are never loaded
  cfg.setDbPath(strDbDir + "/");
  cfg.setInteractive(false);            // no modal error dialogs
  cfg.setAutoReprocess(true);           // option slots would otherwise show a coach note
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
static void checkPreview(QTextStream &out)
{
  Q_Viewer *pViewer = findViewer();
  PreviewCanvas *pCanvas = pViewer ? pViewer->findChild<PreviewCanvas *>() : nullptr;
  if(!pCanvas || pCanvas->image().isNull())
  {
    out << "harness: preview missing\n";
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

  out << QString("harness: preview zoom=%1 scale=%2 canvas=%3x%4 image=%5x%6 pixels=%7: %8\n")
           .arg(dZoom).arg(dDpr).arg(pCanvas->width()).arg(pCanvas->height())
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

static int runGui(const QStringList &files, const GuiOptions &opts, const QString &strSettingsDir)
{
  // Otherwise a fresh install (so "Decode Image" etc. take their defaults),
  // minus the coach tips, which are modal and can't be clicked away here
  useTestSettings(strSettingsDir);
  {
    QSettings settings;
    settings.setValue("Coach\\ReprocessAuto", false);
    settings.setValue("Coach\\DecodeIdct", false);
    if(opts.bHisto)
    {
      settings.setValue("General\\DecScanHisto", true);
      settings.setValue("General\\DumpHistoY", true);
    }
  }

  if(opts.bDark)
    QApplication::setPalette(darkPalette());

  MainWindow window;
  window.show();
  QCoreApplication::processEvents();

  QTextStream out(stdout);

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
    for(const QString &strFile : files)
    {
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
        out.flush();
      }

      if(opts.bMouse)
        exerciseMouse();
      if(opts.bCycleChannels)
        cycleChannels(window);
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

  // Options with a text value (--scale was applied above)
  QString strPixels;
  for(const char *pszOpt : { "--scale", "--zoom", "--pixels" })
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
    args.remove(nInd, 2);
  }

  // Options with a numeric value
  for(const char *pszOpt : { "--repeat", "--max-growth-mb", "--max-zoom-mb" })
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
    return runDecode(args.first(), bAc, bHisto, bUnknownExif, strPixels, tmpDir.path());
  if(strMode == "gui" && !args.isEmpty())
    return runGui(args, guiOpts, tmpDir.path());
  if(strMode == "settings" && args.isEmpty())
    return runSettings(tmpDir.path());

  usage();
  return 2;
}
