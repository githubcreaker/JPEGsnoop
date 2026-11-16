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
//     [--dht-avi] [--overlays] [--overlay STEPS] [--out PATH] [--out-same]
//     [--out-same-case]
//     Open the file in the real MainWindow and run Tools > Export JPEG with
//     these settings (--all: Extract All), to a temporary out.jpg unless
//     --out says where. --out-same / --out-same-case export over the file
//     itself, by the same name or by the name in another case. --overlay
//     first answers Tools > File Overlay's dialog with STEPS (as for gui),
//     and afterwards reprocesses and prints the overlay summary again;
//     --overlays ticks Enable Overlays. Prints the
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
// gui ... --edit STEPS [--edit-before]
//     After each file (and with --edit-before, also once before the first),
//     run STEPS on the report, a comma-separated list of: state (nothing),
//     selectall and copy (choose Edit > Select All or Copy), key:NAME (press
//     selectall, copy, cut, paste, undo, backspace or shift-up, as Cmd+A and
//     so on, in the focused widget), type:TEXT (type it), click (click at the
//     report's top left) and unfocus (take the focus from the report). The
//     clipboard first holds a marker. After each step print how much of the
//     report is selected, which Edit items are enabled, where the focus is,
//     whether the report has changed and what the clipboard holds; after a
//     copy, also the clipboard's formats, the colours and fonts in its HTML,
//     and the report's own colours.
//     Edit > Find's steps: find and findnext (choose Edit > Find or Find
//     Next), key:find and key:findnext (press Cmd+F, Cmd+G), and in the Find
//     dialog what:TEXT (type TEXT, percent-encoded, in place of the field's
//     text), case, word (click Match case, Match whole word only), up, down,
//     next (click Find Next), return, escape (press them in the field) and
//     cancel (click Cancel); and select:TEXT (select the first TEXT in the
//     report, as with the mouse), line:N or line:end (put the caret at the
//     start of line N or at the end), cover (move the dialog over the
//     selection), top (move the main window to the top of the screen) and
//     activate (bring the main window to the front). With any
//     of them, each step also prints a "harness: find" line: which Find items
//     are enabled, the dialog's state and fields, the selection (line, length
//     and text) and which of the dialog text's matches it is, where the
//     dialog is (over it, above, below or beside it), and the status bar's
//     message.
//
// gui ... --overlay STEPS
//     For each file, print a "harness: overlay file N" summary of the report:
//     APP0's version, how many buffer overlays its scan section lists and the
//     first one's length and offset, and its header's overlay line. After the first file (or file N with --overlay-at
//     N), choose Tools > File Overlay
//     and answer its dialog with STEPS, a comma-separated list of on and off
//     (Overlay Enabled?), offset=HEX, value=HEX (typed, so Len follows it),
//     len=N, load (Load Original), fields (print the fields), apply, ok and
//     cancel; "+" chooses the menu item again for the steps after it. Print
//     the dialog's fields each time it shows, at apply, ok and cancel, and
//     the report summary after apply, after the last step and after File >
//     Reprocess.
//
// gui ... --offset STEPS
//     For each file, print a "harness: offset file N" summary of the report
//     and preview: the start offset, whether an SOI was found there and the
//     scan decoded, the signature section's offset, the assessment, and the
//     preview's size and MD5, with ref=same if it is the image a separate
//     decode from that offset gives. After the first file (or file N with
//     --offset-at N; 0 is before any file), print File > Offset's place in
//     the menu, choose it and answer its dialog with STEPS, a comma-separated
//     list of hex and dec (click those), value=TEXT (replace the field's
//     text by typing TEXT, percent-encoded), type=TEXT (type it as the field
//     is), fields, ok, cancel, return and escape; "+" chooses the menu item
//     again for the steps after it. Print the dialog's fields (base, text,
//     focus and selection) each time it shows, after hex and dec, and at the
//     end, and the summary after each opening and after File > Reprocess.
//     The batch mode takes --offset too, for the file opened first.
//
// Always runs offscreen and keeps any settings in a temporary folder, so it
// never shows a window or touches the user's JPEGsnoop settings.

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QCoreApplication>
#include <QDate>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QLabel>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPalette>
#include <QPixmap>
#include <QPointer>
#include <QProcess>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QStatusBar>
#include <QUrl>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QPointingDevice>
#include <QSet>
#include <QSettings>
#include <QStyle>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextStream>
#include <QTimer>
#include <QtMath>
#include <cmath>
#include <functional>
#include <cstdio>
#include <cstring>

#ifdef Q_OS_MACOS
#include <mach/mach.h>
#else
#include <sys/resource.h>
#endif
#include <sys/stat.h>
#include <unistd.h>

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
          "                         [--edit STEPS [--edit-before]] [--overlay STEPS [--overlay-at N]]\n"
          "                         [--offset STEPS [--offset-at N]]\n"
          "       jpegsnoop-harness settings\n"
          "       jpegsnoop-harness export-jpeg <file> [--all] [--force-soi] [--force-eoi] [--ignore-eoi]\n"
          "                         [--dht-avi] [--overlays] [--overlay STEPS] [--out PATH] [--out-same]\n"
          "                         [--out-same-case]\n"
          "       jpegsnoop-harness export-tiff <file> --format rgb8|rgb16|ycc8 [--out PATH] [--out-same]\n"
          "       jpegsnoop-harness batch <folder> [<file to open first> ...] --batch STEPS [--batch-cat REL]...\n"
          "                         [--batch-unreadable REL]... [--batch-mode OCTAL:REL]...\n"
          "                         [--batch-link sym|hard:NAME=TARGET]... [--batch-compare] [--batch-open-missing]\n"
          "                         [--batch-cancel-after N] [--batch-close-after N] [--replace]\n"
          "                         [--no-decode-image] [--overlay STEPS] [--offset STEPS]\n"
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

// An MD5 of an image's pixels (of no data for a null image)
static QString imageMd5(const QImage &img)
{
  QCryptographicHash hash(QCryptographicHash::Md5);
  if(!img.isNull())
  {
    const QImage imgRgb = img.convertToFormat(QImage::Format_RGB32);
    for(int nY = 0; nY < imgRgb.height(); nY++)
      hash.addData(QByteArrayView(reinterpret_cast<const char *>(imgRgb.constScanLine(nY)), imgRgb.width() * 4));
  }
  return QString::fromLatin1(hash.result().toHex());
}

static QString previewMd5(CimgDecode &img)
{
  return imageMd5((img.m_bDibTempReady && img.m_pDibTemp) ? *img.m_pDibTemp : QImage());
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
  QString strEdit;                // --edit STEPS
  bool bEditBefore = false;       // --edit-before
  QString strOverlay;             // --overlay STEPS
  int nOverlayAt = 1;             // --overlay-at N
  QString strOffset;              // --offset STEPS
  int nOffsetAt = 1;              // --offset-at N
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
      const QCheckBox *pOverlays = pJpegDlg->findChild<QCheckBox *>("cbOverlay");
      out << "harness: dialog: " << pJpegDlg->windowTitle() << " (" << (pOffset ? pOffset->text() : QString("?"))
          << ", MJPEG DHT " << ((pDht && pDht->isChecked()) ? "on" : "off") << ")\n";
      out << "harness: dialog: Enable Overlays " << ((pOverlays && pOverlays->isEnabled()) ? "available" : "unavailable")
          << "\n";
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

// What the report shows of a File Overlay (see --overlay above): APP0's
// version, the number of buffer overlays listed in the scan section and the
// first one's length and offset, and the header's overlay line (length@offset)
static QString overlayReport(MainWindow &window)
{
  const QString strLog = window.textEdit->toPlainText();
  const QRegularExpressionMatch mVersion = QRegularExpression(QStringLiteral("^  version    = (\\S+)$"),
                                                              QRegularExpression::MultilineOption).match(strLog);
  const QRegularExpressionMatch mCount = QRegularExpression(QStringLiteral("^  Buffer Overlays active: (\\d+)$"),
                                                            QRegularExpression::MultilineOption).match(strLog);
  const QRegularExpressionMatch mFirst =
    QRegularExpression(QStringLiteral("^    000: .* Overlay Byte Len=\\[ *(\\d+)\\] Offset=\\[(0x[0-9A-F]+)\\]$"),
                       QRegularExpression::MultilineOption).match(strLog);
  const QRegularExpressionMatch mHeader =
    QRegularExpression(QStringLiteral("^  Overlay:  \\[(\\d+)\\] bytes replaced at offset \\[(0x[0-9A-F]+)\\] "),
                       QRegularExpression::MultilineOption).match(strLog);
  return QString("version=%1 overlays=%2 len=%3 offset=%4 header=%5")
           .arg(mVersion.hasMatch() ? mVersion.captured(1) : QStringLiteral("-"),
                mCount.hasMatch() ? mCount.captured(1) : QStringLiteral("0"),
                mFirst.hasMatch() ? mFirst.captured(1) : QStringLiteral("-"),
                mFirst.hasMatch() ? mFirst.captured(2) : QStringLiteral("-"),
                mHeader.hasMatch() ? (mHeader.captured(1) + QLatin1Char('@') + mHeader.captured(2)) : QStringLiteral("-"));
}

// Tools > File Overlay as a user chooses it (see --overlay above). The dialog
// is found by name, so the driver also builds with versions without it.
static void overlayMenu(MainWindow &window, const QString &strSteps, QTextStream &out)
{
  QAction *pAction = findAction(window, QStringLiteral("File Overlay..."));
  if(!pAction || !pAction->isEnabled())
  {
    out << "harness: menu File Overlay unavailable\n";
    return;
  }

  const QStringList astrOpenings = strSteps.split('+');
  QStringList astrSteps;

  QTimer timer;
  QObject::connect(&timer, &QTimer::timeout, [&]() {
    QDialog *pDlg = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    if(!pDlg || (pDlg->objectName() != QLatin1String("fileOverlayDialog")))
      return;
    QCheckBox *pEn = pDlg->findChild<QCheckBox *>("overlayEnabled");
    QLineEdit *pOffset = pDlg->findChild<QLineEdit *>("offset");
    QLabel *pOrigHex = pDlg->findChild<QLabel *>("originalHex");
    QLabel *pOrigBin = pDlg->findChild<QLabel *>("originalBin");
    QLineEdit *pValue = pDlg->findChild<QLineEdit *>("newValue");
    QLineEdit *pLen = pDlg->findChild<QLineEdit *>("length");
    QPushButton *pLoad = pDlg->findChild<QPushButton *>("loadOriginal");
    QDialogButtonBox *pButtons = pDlg->findChild<QDialogButtonBox *>("buttonBox");
    if(!pEn || !pOffset || !pOrigHex || !pOrigBin || !pValue || !pLen || !pLoad || !pButtons)
    {
      out << "harness: overlay dialog widgets not found\n";
      pDlg->reject();
      return;
    }

    auto fields = [&]() {
      return QString("en=%1 offset=%2 original=\"%3\" bin=\"%4\" value=%5 len=%6")
               .arg(pEn->isChecked() ? 1 : 0)
               .arg(pOffset->text(), pOrigHex->text(), pOrigBin->text(), pValue->text(), pLen->text());
    };
    out << "harness: overlay dialog: " << pDlg->windowTitle() << " (" << fields() << ")\n";

    while(!astrSteps.isEmpty())
    {
      const QString strStep = astrSteps.takeFirst();
      if((strStep == "on") || (strStep == "off"))
        pEn->setChecked(strStep == "on");
      else if(strStep.startsWith("offset="))
        pOffset->setText(strStep.mid(7));
      else if(strStep.startsWith("value="))
      {
        pValue->clear();
        QTest::keyClicks(pValue, strStep.mid(6));
      }
      else if(strStep.startsWith("len="))
        pLen->setText(strStep.mid(4));
      else if(strStep == "load")
        pLoad->click();
      else if(strStep == "fields")
        out << "harness: overlay fields (" << fields() << ")\n";
      else if(strStep == "apply")
      {
        // A refused Apply shows a message box, closed and printed
        pButtons->button(QDialogButtonBox::Apply)->click();
        QCoreApplication::processEvents();
        out << "harness: overlay applied (" << fields() << ") report " << overlayReport(window) << "\n";
      }
      else if((strStep == "ok") || (strStep == "cancel"))
      {
        out << "harness: overlay dialog " << strStep << " (" << fields() << ")\n";
        out.flush();
        // A refused OK shows a message box: the steps left carry on after it
        pButtons->button((strStep == "ok") ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
        return;
      }
      else
        out << "harness: unknown overlay step " << strStep << "\n";
    }

    out << "harness: overlay dialog still open: cancelled\n";
    pDlg->reject();
  });
  timer.start(50);

  for(const QString &strOpening : astrOpenings)
  {
    astrSteps = strOpening.split(',', Qt::SkipEmptyParts);
    pAction->trigger();
    QCoreApplication::processEvents();
  }
  timer.stop();

  out << "harness: overlay after: " << overlayReport(window) << "\n";
  window.reprocess();
  QCoreApplication::processEvents();
  out << "harness: overlay after reprocess: " << overlayReport(window) << "\n";
  out.flush();
}

// What the report and preview show of the start offset (see --offset
// above): the report's start offset, whether it found an SOI there and
// decoded the scan, the signature section's offset, the assessment, and the
// preview's size and MD5 and whether it is the image a separate decode of
// strFile from that offset gives
static QString offsetReport(MainWindow &window, const QString &strFile, const QString &strDbDir)
{
  const QString strLog = window.textEdit->toPlainText();
  const QRegularExpressionMatch mStart =
    QRegularExpression(QStringLiteral("^Start Offset: 0x([0-9a-fA-F]+)$"), QRegularExpression::MultilineOption)
      .match(strLog);
  const QRegularExpressionMatch mSig =
    QRegularExpression(QStringLiteral("^  File Offset: +(\\d+) bytes$"), QRegularExpression::MultilineOption)
      .match(strLog);
  QString strAssess = QStringLiteral("-");
  if(strLog.contains(QLatin1String("  ASSESSMENT not done as file offset non-zero")))
    strAssess = QStringLiteral("skipped");
  else if(strLog.contains(QLatin1String("  ASSESSMENT: ")))
    strAssess = QStringLiteral("done");

  Q_Viewer *pViewer = findViewer();
  PreviewCanvas *pCanvas = pViewer ? pViewer->findChild<PreviewCanvas *>() : nullptr;
  const QImage img = (pCanvas && pViewer->isVisible()) ? pCanvas->image() : QImage();
  QString strRef = QStringLiteral("-");
  if(mStart.hasMatch())
  {
    QString strRefLog;
    QString strRefMd5;
    if(decodeForDetail(strFile, false, mStart.captured(1).toUInt(nullptr, 16), QString(), strDbDir, strRefLog,
                       strRefMd5))
      strRef = (strRefMd5 == imageMd5(img)) ? QStringLiteral("same") : QStringLiteral("differs");
  }

  return QString("start=%1 soi=%2 scan=%3 sig=%4 assessment=%5 preview=%6 md5=%7 ref=%8")
           .arg(mStart.hasMatch() ? ("0x" + mStart.captured(1).toUpper()) : QStringLiteral("-"))
           .arg(strLog.contains(QLatin1String("*** Marker: SOI (xFFD8) ***")) ? 1 : 0)
           .arg(strLog.contains(QLatin1String("  Compression stats:")) ? 1 : 0)
           .arg(mSig.hasMatch() ? mSig.captured(1) : QStringLiteral("-"), strAssess,
                img.isNull() ? QStringLiteral("none") : QString("%1x%2").arg(img.width()).arg(img.height()),
                img.isNull() ? QStringLiteral("-") : imageMd5(img).left(8), strRef);
}

// File > Offset as a user chooses it (see --offset above). The dialog is
// found by name, so the driver also builds with versions without it.
static void offsetMenu(MainWindow &window, const QString &strSteps, const QString &strFile, const QString &strDbDir,
                       QTextStream &out)
{
  QAction *pAction = findAction(window, QStringLiteral("Offset..."));
  if(!pAction)
  {
    out << "harness: menu Offset unavailable\n";
    return;
  }

  // Where it is: the menu's title, the item before it and whether a
  // separator is between them
  {
    QString strMenu = "-";
    QString strBefore = "-";
    bool bSeparator = false;
    const QList<QObject *> objects = pAction->associatedObjects();
    for(QObject *pObj : objects)
    {
      QMenu *pMenu = qobject_cast<QMenu *>(pObj);
      if(!pMenu)
        continue;
      strMenu = pMenu->title();
      const QList<QAction *> actions = pMenu->actions();
      for(qsizetype nInd = actions.indexOf(pAction) - 1; nInd >= 0; nInd--)
      {
        if(!actions.at(nInd)->isSeparator())
        {
          strBefore = actions.at(nInd)->text();
          break;
        }
        bSeparator = true;
      }
    }
    out << "harness: offset menu=" << strMenu << " after=\"" << strBefore << "\" separator=" << (bSeparator ? 1 : 0)
        << " enabled=" << (pAction->isEnabled() ? 1 : 0) << " tip=\"" << pAction->statusTip() << "\"\n";
  }
  if(!pAction->isEnabled())
  {
    out << "harness: menu Offset unavailable\n";
    return;
  }

  const QStringList astrOpenings = strSteps.split('+');
  QStringList astrSteps;

  QTimer timer;
  QObject::connect(&timer, &QTimer::timeout, [&]() {
    QDialog *pDlg = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    if(!pDlg || (pDlg->objectName() != QLatin1String("fileOffsetDialog")))
      return;
    // The offscreen platform activates a window a little after it shows:
    // wait for that, as a user would. (It doesn't activate the dialog again
    // after a message box, so focus then shows as none and inactive.)
    if(!pDlg->property("harnessShown").toBool())
    {
      pDlg->setProperty("harnessShown", true);
      static_cast<void>(QTest::qWaitFor([pDlg]() { return QApplication::activeWindow() == pDlg; }, 1000));
    }
    QLineEdit *pValue = pDlg->findChild<QLineEdit *>("offsetValue");
    QRadioButton *pHex = pDlg->findChild<QRadioButton *>("baseHex");
    QRadioButton *pDec = pDlg->findChild<QRadioButton *>("baseDecimal");
    QDialogButtonBox *pButtons = pDlg->findChild<QDialogButtonBox *>("buttonBox");
    if(!pValue || !pHex || !pDec || !pButtons)
    {
      out << "harness: offset dialog widgets not found\n";
      pDlg->reject();
      return;
    }

    auto fields = [&]() {
      QString strSel = QStringLiteral("none");
      if(pValue->hasSelectedText())
        strSel = (pValue->selectedText() == pValue->text()) ? QStringLiteral("all") : QStringLiteral("part");
      const QWidget *pFocus = QApplication::focusWidget();
      QString strFocus = !pFocus ? QStringLiteral("none")
                                 : ((pFocus == pValue) ? QStringLiteral("value")
                                                       : (pFocus->objectName().isEmpty()
                                                            ? QString::fromLatin1(pFocus->metaObject()->className())
                                                            : pFocus->objectName()));
      if(QApplication::activeWindow() != pDlg)
        strFocus += QStringLiteral("(inactive)");
      return QString("base=%1 value=\"%2\" focus=%3 selected=%4")
               .arg(pHex->isChecked() ? (pDec->isChecked() ? "both" : "hex") : (pDec->isChecked() ? "dec" : "none"),
                    pValue->text(), strFocus, strSel);
    };
    out << "harness: offset dialog: " << pDlg->windowTitle() << " (" << fields() << ")\n";

    while(!astrSteps.isEmpty())
    {
      const QString strStep = astrSteps.takeFirst();
      if((strStep == "hex") || (strStep == "dec"))
      {
        // A refused change shows a message box, closed and printed
        ((strStep == "hex") ? pHex : pDec)->click();
        QCoreApplication::processEvents();
        out << "harness: offset clicked " << strStep << " (" << fields() << ")\n";
      }
      else if(strStep.startsWith("value="))
      {
        pValue->clear();
        QTest::keyClicks(pValue, QUrl::fromPercentEncoding(strStep.mid(6).toUtf8()));
      }
      else if(strStep.startsWith("type="))
        QTest::keyClicks(pValue, QUrl::fromPercentEncoding(strStep.mid(5).toUtf8()));
      else if(strStep == "fields")
        out << "harness: offset fields (" << fields() << ")\n";
      else if((strStep == "ok") || (strStep == "cancel") || (strStep == "return") || (strStep == "escape"))
      {
        out << "harness: offset dialog " << strStep << " (" << fields() << ")\n";
        out.flush();
        // A refused OK shows a message box: the steps left carry on after it
        if(strStep == "return")
          QTest::keyClick(pValue, Qt::Key_Return);
        else if(strStep == "escape")
          QTest::keyClick(pValue, Qt::Key_Escape);
        else
          pButtons->button((strStep == "ok") ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
        return;
      }
      else
        out << "harness: unknown offset step " << strStep << "\n";
    }

    out << "harness: offset dialog still open: cancelled\n";
    pDlg->reject();
  });
  timer.start(50);

  for(const QString &strOpening : astrOpenings)
  {
    astrSteps = strOpening.split(',', Qt::SkipEmptyParts);
    pAction->trigger();
    QCoreApplication::processEvents();
    out << "harness: offset now: " << offsetReport(window, strFile, strDbDir) << "\n";
  }
  timer.stop();

  window.reprocess();
  QCoreApplication::processEvents();
  out << "harness: offset after reprocess: " << offsetReport(window, strFile, strDbDir) << "\n";
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

// What the clipboard holds before gui --edit's steps
static const QString kClipboardMarker = QStringLiteral("(harness clipboard marker)");

// The report as gui --edit prints it after each step: how much is selected
// (all, part or none) and its length, the Edit items enabled, the report's
// undo history, where the focus is, whether the report differs from
// strBefore and what the clipboard holds
static QString editState(MainWindow &window, const QString &strBefore)
{
  const QPlainTextEdit *pReport = window.textEdit;
  const QString strText = pReport->toPlainText();
  const QString strSel = pReport->textCursor().selection().toPlainText();
  const QString strSelWhat = strSel.isEmpty() ? QStringLiteral("none")
                                              : ((strSel == strText) ? QStringLiteral("all") : QStringLiteral("part"));

  auto enabled = [&window](const char *pszText) {
    const QAction *pAction = findAction(window, QLatin1String(pszText));
    return pAction ? QString::number(pAction->isEnabled() ? 1 : 0) : QStringLiteral("?");
  };

  const QWidget *pFocus = QApplication::focusWidget();
  QString strFocus = !pFocus ? QStringLiteral("none") : ((pFocus == pReport) ? QStringLiteral("report") : QStringLiteral("other"));
  if(QApplication::activeWindow() != &window)
    strFocus += QStringLiteral("(inactive)");

  const QString strClip = QGuiApplication::clipboard()->text();
  QString strClipWhat;
  if(strClip == kClipboardMarker)
    strClipWhat = QStringLiteral("unchanged");
  else if(!strText.isEmpty() && (strClip == strText))
    strClipWhat = QStringLiteral("report");
  else if(!strSel.isEmpty() && (strClip == strSel))
    strClipWhat = QStringLiteral("selection");
  else
    strClipWhat = QString("other(%1)").arg(strClip.size());

  return QString("selected=%1 chars=%2 copy=%3 selectall=%4 undo=%5 undo-steps=%6 readonly=%7 focus=%8 report=%9 clipboard=%10")
           .arg(strSelWhat)
           .arg(strText.size())
           .arg(enabled("&Copy"), enabled("Select All"), enabled("Undo"))
           .arg(pReport->document()->availableUndoSteps())
           .arg(pReport->isReadOnly() ? 1 : 0)
           .arg(strFocus, (strText == strBefore) ? QStringLiteral("same") : QStringLiteral("changed"), strClipWhat);
}

// The selection in the report as a rectangle on the screen
static QRect selectionOnScreen(const QPlainTextEdit *pReport)
{
  const QTextCursor cursor = pReport->textCursor();
  QTextCursor start(cursor);
  start.setPosition(cursor.selectionStart());
  QTextCursor end(cursor);
  end.setPosition(cursor.selectionEnd());
  const QRect rect = pReport->cursorRect(start).united(pReport->cursorRect(end));
  return QRect(pReport->viewport()->mapToGlobal(rect.topLeft()), rect.size());
}

// The Find dialog, once Edit > Find has made it (found by name, so the driver
// also builds with versions that haven't got it)
static QDialog *findDialog(MainWindow &window)
{
  return window.findChild<QDialog *>(QStringLiteral("findDialog"));
}

// Edit > Find's state as gui --edit prints it after each step: the Find items
// enabled; the dialog (hidden, shown or active), its text, whether Find Next
// is on, its options; the selection (its line, length and text, cut to 40
// characters) and which match of the dialog's text, with its options, it is
// (0 if none); where the dialog is (over the selection, above, below or
// beside it); and the status bar's message
static QString findState(MainWindow &window)
{
  const QPlainTextEdit *pReport = window.textEdit;
  auto enabled = [&window](const char *pszText) {
    const QAction *pAction = findAction(window, QLatin1String(pszText));
    return pAction ? QString::number(pAction->isEnabled() ? 1 : 0) : QStringLiteral("?");
  };

  const QDialog *pDlg = findDialog(window);
  const bool bShown = pDlg && pDlg->isVisible();
  QString strWhat = QStringLiteral("-");
  QString strOptions = QStringLiteral("button=- case=- word=- dir=-");
  QTextDocument::FindFlags eFlags;
  if(pDlg)
  {
    const QLineEdit *pWhat = pDlg->findChild<QLineEdit *>(QStringLiteral("findWhat"));
    const auto checked = [pDlg](const char *pszName) {
      const QAbstractButton *pButton = pDlg->findChild<QAbstractButton *>(QLatin1String(pszName));
      return pButton && pButton->isChecked();
    };
    const QAbstractButton *pNext = pDlg->findChild<QAbstractButton *>(QStringLiteral("findNext"));
    strWhat = pWhat ? pWhat->text() : QStringLiteral("?");
    strOptions = QString("button=%1 case=%2 word=%3 dir=%4")
                   .arg((pNext && pNext->isEnabled()) ? 1 : 0)
                   .arg(checked("matchCase") ? 1 : 0)
                   .arg(checked("matchWholeWord") ? 1 : 0)
                   .arg(checked("directionUp") ? QStringLiteral("up")
                                               : (checked("directionDown") ? QStringLiteral("down") : QStringLiteral("none")));
    if(checked("matchCase"))
      eFlags |= QTextDocument::FindCaseSensitively;
    if(checked("matchWholeWord"))
      eFlags |= QTextDocument::FindWholeWords;
  }

  const QTextCursor cursor = pReport->textCursor();
  QString strSel = QStringLiteral("none");
  if(cursor.hasSelection())
  {
    QString strText = cursor.selectedText();
    strText.replace(QChar::ParagraphSeparator, QLatin1Char('|'));
    if(strText.size() > 40)
      strText = strText.left(40) + QStringLiteral("...");
    strSel = QString("L%1+%2 \"").arg(pReport->document()->findBlock(cursor.selectionStart()).blockNumber() + 1)
                                 .arg(cursor.selectionEnd() - cursor.selectionStart()) + strText + QStringLiteral("\"");
  }

  // Each match, including those overlapping the one before (as Find Next finds them)
  int nMatch = 0;
  int nMatches = 0;
  if(pDlg && (strWhat != QStringLiteral("?")) && !strWhat.isEmpty())
  {
    for(QTextCursor found = pReport->document()->find(strWhat, 0, eFlags); !found.isNull();
        found = pReport->document()->find(strWhat, found.selectionStart() + 1, eFlags))
    {
      ++nMatches;
      if((found.selectionStart() == cursor.selectionStart()) && (found.selectionEnd() == cursor.selectionEnd()))
        nMatch = nMatches;
    }
  }

  // Where the dialog is: over the selection, or above, below or beside it
  QString strPlace = QStringLiteral("-");
  if(bShown && cursor.hasSelection())
  {
    const QRect rectDlg = pDlg->frameGeometry();
    const QRect rectSel = selectionOnScreen(pReport);
    strPlace = rectDlg.intersects(rectSel) ? QStringLiteral("over")
             : (rectDlg.bottom() < rectSel.top()) ? QStringLiteral("above")
             : (rectDlg.top() > rectSel.bottom()) ? QStringLiteral("below") : QStringLiteral("beside");
  }

  return QStringLiteral("find=") + enabled("Find...") + QStringLiteral(" findnext=") + enabled("Find Next")
         + QStringLiteral(" dialog=") + (!bShown ? QStringLiteral("hidden")
                                                 : ((QApplication::activeWindow() == pDlg) ? QStringLiteral("active") : QStringLiteral("shown")))
         + QStringLiteral(" what=\"") + strWhat + QStringLiteral("\" ") + strOptions
         + QStringLiteral(" sel=") + strSel + QString(" match=%1/%2 dialog-at=%3").arg(nMatch).arg(nMatches).arg(strPlace)
         + QStringLiteral(" status=\"") + window.statusBar()->currentMessage() + QStringLiteral("\"");
}

// gui --edit STEPS (see above)
static void editSteps(MainWindow &window, const QString &strSteps, QTextStream &out)
{
  QPlainTextEdit *pReport = window.textEdit;
  const QString strBefore = pReport->toPlainText();
  QGuiApplication::clipboard()->setText(kClipboardMarker);

  // Window shortcuts (Cmd+A, Cmd+C) only reach the active window, and the
  // image window a decode shows takes that. (qWaitForWindowActive doesn't
  // wait for it: it counts the main window as active while its child window
  // is.)
  window.activateWindow();
  if(!QTest::qWaitFor([&window]() { return QApplication::activeWindow() == &window; }, 2000))
    out << "harness: edit: main window not active\n";

  // The keys as the platform maps them (Cmd+A and so on on macOS)
  auto standardKey = [](QKeySequence::StandardKey eKey) {
    const QList<QKeySequence> aKeys = QKeySequence::keyBindings(eKey);
    return aKeys.isEmpty() ? QKeyCombination() : aKeys.first()[0];
  };

  static const QRegularExpression reFindStep(
    QStringLiteral("(^|,)(find|findnext|key:find|key:findnext|what:[^,]*|case|word|up|down|next|return|escape|cancel"
                   "|select:[^,]*|line:[^,]*|cover|top|activate)(,|$)"));
  const bool bFindSteps = reFindStep.match(strSteps).hasMatch();

  for(const QString &strStep : strSteps.split(',', Qt::SkipEmptyParts))
  {
    QWidget *pTarget = QApplication::focusWidget() ? QApplication::focusWidget() : static_cast<QWidget *>(&window);
    QDialog *pDlg = findDialog(window);
    auto dlgChild = [pDlg](const char *pszName) {
      return pDlg ? pDlg->findChild<QWidget *>(QLatin1String(pszName)) : nullptr;
    };
    auto clickDlgButton = [&dlgChild](const char *pszName) {
      QAbstractButton *pButton = qobject_cast<QAbstractButton *>(dlgChild(pszName));
      if(pButton)
        pButton->click();    // does nothing while disabled, as a click
      return pButton != nullptr;
    };

    if(strStep == "state")
    {
    }
    else if((strStep == "find") || (strStep == "findnext"))
    {
      QAction *pAction = findAction(window, (strStep == "find") ? QStringLiteral("Find...") : QStringLiteral("Find Next"));
      if(pAction)
        pAction->trigger();    // does nothing while disabled, as the menu
    }
    else if(strStep.startsWith("what:"))
    {
      QLineEdit *pWhat = qobject_cast<QLineEdit *>(dlgChild("findWhat"));
      if(!pWhat)
      {
        out << "harness: edit: no Find dialog for " << strStep << "\n";
        continue;
      }
      pWhat->setFocus(Qt::OtherFocusReason);
      pWhat->selectAll();
      QTest::keyClick(pWhat, Qt::Key_Delete);
      QTest::keyClicks(pWhat, QUrl::fromPercentEncoding(strStep.mid(5).toUtf8()));
    }
    else if((strStep == "case") || (strStep == "word") || (strStep == "up") || (strStep == "down") ||
            (strStep == "next") || (strStep == "cancel"))
    {
      const char *pszName = (strStep == "case") ? "matchCase"
                          : (strStep == "word") ? "matchWholeWord"
                          : (strStep == "up") ? "directionUp"
                          : (strStep == "down") ? "directionDown"
                          : (strStep == "next") ? "findNext" : "cancel";
      if(!clickDlgButton(pszName))
      {
        out << "harness: edit: no Find dialog for " << strStep << "\n";
        continue;
      }
    }
    else if((strStep == "return") || (strStep == "escape"))
    {
      QWidget *pWhat = dlgChild("findWhat");
      if(!pWhat)
      {
        out << "harness: edit: no Find dialog for " << strStep << "\n";
        continue;
      }
      QTest::keyClick(pWhat, (strStep == "return") ? Qt::Key_Return : Qt::Key_Escape);
    }
    else if(strStep.startsWith("select:"))
    {
      const QTextCursor found = pReport->document()->find(QUrl::fromPercentEncoding(strStep.mid(7).toUtf8()), 0,
                                                          QTextDocument::FindCaseSensitively);
      if(found.isNull())
      {
        out << "harness: edit: not in the report: " << strStep << "\n";
        continue;
      }
      pReport->setTextCursor(found);
    }
    else if(strStep.startsWith("line:"))
    {
      QTextCursor cursor(pReport->document());
      if(strStep == "line:end")
        cursor.movePosition(QTextCursor::End);
      else
        cursor.setPosition(pReport->document()->findBlockByNumber(strStep.mid(5).toInt() - 1).position());
      pReport->setTextCursor(cursor);
    }
    else if(strStep == "cover")
    {
      if(!pDlg || !pReport->textCursor().hasSelection())
      {
        out << "harness: edit: nothing to cover\n";
        continue;
      }
      const QRect rectDlg = pDlg->frameGeometry();
      pDlg->move(selectionOnScreen(pReport).center() - QPoint(rectDlg.width() / 2, rectDlg.height() / 2));
    }
    else if(strStep == "top")
    {
      window.move(window.screen()->availableGeometry().topLeft());
    }
    else if(strStep == "activate")
    {
      window.activateWindow();
      if(!QTest::qWaitFor([&window]() { return QApplication::activeWindow() == &window; }, 2000))
        out << "harness: edit: main window not active\n";
    }
    else if((strStep == "selectall") || (strStep == "copy"))
    {
      QAction *pAction = findAction(window, (strStep == "copy") ? QStringLiteral("&Copy") : QStringLiteral("Select All"));
      if(pAction)
        pAction->trigger();    // does nothing while disabled, as the menu
    }
    else if(strStep.startsWith("key:"))
    {
      const QString strKey = strStep.mid(4);
      QKeyCombination key;
      if(strKey == "selectall")
        key = standardKey(QKeySequence::SelectAll);
      else if(strKey == "find")
        key = standardKey(QKeySequence::Find);
      else if(strKey == "findnext")
        key = standardKey(QKeySequence::FindNext);
      else if(strKey == "copy")
        key = standardKey(QKeySequence::Copy);
      else if(strKey == "cut")
        key = standardKey(QKeySequence::Cut);
      else if(strKey == "paste")
        key = standardKey(QKeySequence::Paste);
      else if(strKey == "undo")
        key = standardKey(QKeySequence::Undo);
      else if(strKey == "backspace")
        key = QKeyCombination(Qt::Key_Backspace);
      else if(strKey == "shift-up")
        key = QKeyCombination(Qt::ShiftModifier, Qt::Key_Up);
      else
      {
        out << "harness: edit: unknown key " << strKey << "\n";
        continue;
      }
      QTest::keyClick(pTarget, key.key(), key.keyboardModifiers());
    }
    else if(strStep.startsWith("type:"))
    {
      QTest::keyClicks(pTarget, strStep.mid(5));
    }
    else if(strStep == "click")
    {
      QTest::mouseClick(pReport->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(4, 4));
    }
    else if(strStep == "unfocus")
    {
      if(QApplication::focusWidget())
        QApplication::focusWidget()->clearFocus();
    }
    else
    {
      out << "harness: edit: unknown step " << strStep << "\n";
      continue;
    }
    QCoreApplication::processEvents();
    // The Find dialog takes a moment to become the active window
    if(strStep.endsWith("find") && findDialog(window) && findDialog(window)->isVisible() &&
       !QTest::qWaitFor([&window]() { return QApplication::activeWindow() == findDialog(window); }, 1000))
      out << "harness: edit: Find dialog not active\n";
    out << "harness: edit " << strStep << ": " << editState(window, strBefore) << "\n";
    if(bFindSteps)
      out << "harness: find " << strStep << ": " << findState(window) << "\n";

    // What a copy put on the clipboard: its formats, and the text colours and
    // fonts in its HTML (what a rich-text app pastes); and the colours of the
    // report itself, which a copy mustn't change
    if(strStep.endsWith("copy"))
    {
      const QMimeData *pData = QGuiApplication::clipboard()->mimeData();
      const QString strHtml = pData ? pData->html() : QString();
      auto distinct = [&strHtml](const QString &strPattern) {
        QStringList astrFound;
        QRegularExpressionMatchIterator it = QRegularExpression(strPattern).globalMatch(strHtml);
        while(it.hasNext())
        {
          const QString strValue = it.next().captured(1);
          if(!astrFound.contains(strValue))
            astrFound << strValue;
        }
        astrFound.sort();
        return astrFound.isEmpty() ? QStringLiteral("none") : astrFound.join(',');
      };

      QStringList astrReport;
      for(QTextBlock block = pReport->document()->begin(); block.isValid(); block = block.next())
      {
        for(QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
        {
          const QBrush brush = it.fragment().charFormat().foreground();
          if((brush.style() != Qt::NoBrush) && !astrReport.contains(brush.color().name()))
            astrReport << brush.color().name();
        }
      }
      astrReport.sort();

      out << "harness: edit clipboard formats=" << (pData ? pData->formats().join(',') : QString())
          << " html colours=" << distinct(QStringLiteral("[^-]color:(#[0-9a-f]{6})"))
          << " fonts=" << distinct(QStringLiteral("font-family:'([^']*)'"))
          << " report colours=" << (astrReport.isEmpty() ? QStringLiteral("none") : astrReport.join(',')) << "\n";
    }
  }
  out.flush();
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

  if(!opts.strEdit.isEmpty() && opts.bEditBefore)
  {
    out << "harness: edit before any file\n";
    editSteps(window, opts.strEdit, out);
  }

  // File > Offset before any file has been opened (--offset-at 0)
  if(!opts.strOffset.isEmpty() && (opts.nOffsetAt == 0))
  {
    out << "harness: offset before any file\n";
    offsetMenu(window, opts.strOffset, QString(), strSettingsDir, out);
  }

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

      if((nRound == 0) && !opts.strOverlay.isEmpty())
      {
        out << "harness: overlay file " << nFileInd << ": " << overlayReport(window) << "\n";
        if(nFileInd == opts.nOverlayAt)
          overlayMenu(window, opts.strOverlay, out);
      }

      if((nRound == 0) && !opts.strOffset.isEmpty())
      {
        out << "harness: offset file " << nFileInd << ": " << offsetReport(window, strFile, strSettingsDir) << "\n";
        if(nFileInd == opts.nOffsetAt)
          offsetMenu(window, opts.strOffset, strFile, strSettingsDir, out);
        if(!opts.strOverlay.isEmpty())
          out << "harness: overlay with offset: " << overlayReport(window) << "\n";
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
        if(!opts.strEdit.isEmpty())
          editSteps(window, opts.strEdit, out);
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
  QString strOverlay;             // --overlay STEPS
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

  if(!opts.strOverlay.isEmpty())
    overlayMenu(window, opts.strOverlay, out);

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

  // The File Overlay is still in place afterwards (an export without Enable
  // Overlays sets it aside)
  if(!opts.strOverlay.isEmpty())
  {
    window.reprocess();
    QCoreApplication::processEvents();
    out << "harness: overlay after export and reprocess: " << overlayReport(window) << "\n";
  }
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

// Calls fnShown when a widget is shown, as it is shown (the batch's progress
// is shown and updated with no event loop between)
class ShowHook : public QObject
{
public:
  explicit ShowHook(std::function<void(QWidget *)> fnShown) : m_fnShown(std::move(fnShown)) {}

protected:
  bool eventFilter(QObject *pObj, QEvent *pEvent) override
  {
    if((pEvent->type() == QEvent::Show) && pObj->isWidgetType())
      m_fnShown(static_cast<QWidget *>(pObj));
    return false;
  }

private:
  std::function<void(QWidget *)> m_fnShown;
};

struct BatchOptions
{
  QString strSteps;               // --batch STEPS
  QStringList astrCat;            // --batch-cat REL (any number)
  QStringList astrUnreadable;     // --batch-unreadable REL (any number)
  QStringList astrModes;          // --batch-mode OCTAL:REL (any number)
  QStringList astrLinks;          // --batch-link sym|hard:NAME=TARGET (any number)
  bool bCompare = false;          // --batch-compare
  bool bReopenMissing = false;    // --batch-open-missing
  int nCancelAfter = -1;          // --batch-cancel-after N
  int nCloseAfter = -1;           // --batch-close-after N
};

// Every file below strRoot (with hidden ones), by its path below it, and its MD5
static QMap<QString, QString> filesBelow(const QString &strRoot)
{
  QMap<QString, QString> mapFiles;
  const QDir dirRoot(strRoot);
  QDirIterator it(strRoot, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
  while(it.hasNext())
  {
    const QString strPath = it.next();
    mapFiles.insert(dirRoot.relativeFilePath(strPath), fileMd5(strPath));
  }
  return mapFiles;
}

// File > Batch Process as a user chooses it. strFolder is copied to <tmp>/batch/in
// first, so the batch can write beside its files. The files in astrOpen are
// opened first (and --overlay's steps answered for the last). Then for each
// of STEPS' openings ("+" between them), the menu item is chosen and its
// dialog answered (see tests/README.md), and what the batch wrote and its
// report are printed.
static int runBatchMode(const QString &strFolder, const QStringList &astrOpen, const GuiOptions &guiOpts,
                        const BatchOptions &opts, const QString &strTmpDir)
{
  useGuiSettings(strTmpDir, guiOpts);

  MainWindow window;
  window.show();
  QCoreApplication::processEvents();

  QTextStream out(stdout);
  QTimer timerMsgBox;
  closeMessageBoxes(timerMsgBox, out, guiOpts.bReplace);

  const QString strRoot = strTmpDir + "/batch";
  QDir().mkpath(strRoot);
  if(QProcess::execute("/bin/cp", QStringList() << "-R" << strFolder << strRoot + "/in") != 0)
  {
    out << "harness: couldn't copy " << strFolder << "\n";
    return 2;
  }
  // Links, made after the copy (cp -R copies symbolic links, but not hard
  // ones). NAME and TARGET are below <tmp>/batch; a symbolic link leads to
  // TARGET's full path, which needn't exist.
  struct MadeLink
  {
    QString strSpec;
    QString strName;
    QString strTarget;
    QString strTargetMd5;         // Before the batch ("" if it didn't exist)
  };
  QList<MadeLink> links;
  for(const QString &strSpec : opts.astrLinks)
  {
    const qsizetype nColon = strSpec.indexOf(':');
    const qsizetype nEq = strSpec.indexOf('=');
    const QString strKind = strSpec.left(nColon);
    MadeLink sLink{ strSpec, strSpec.mid(nColon + 1, nEq - nColon - 1), strSpec.mid(nEq + 1), QString() };
    const QByteArray abName = QFile::encodeName(strRoot + "/" + sLink.strName);
    const QByteArray abTarget = QFile::encodeName(strRoot + "/" + sLink.strTarget);
    QDir().mkpath(QFileInfo(strRoot + "/" + sLink.strName).path());
    ::unlink(abName.constData());
    const bool bMade = (nColon > 0) && (nEq > nColon) &&
                       (((strKind == "sym") && (::symlink(abTarget.constData(), abName.constData()) == 0)) ||
                        ((strKind == "hard") && (::link(abTarget.constData(), abName.constData()) == 0)));
    if(!bMade)
    {
      out << "harness: couldn't make the link " << strSpec << "\n";
      return 2;
    }
    if(QFileInfo::exists(strRoot + "/" + sLink.strTarget))
      sLink.strTargetMd5 = fileMd5(strRoot + "/" + sLink.strTarget);
    links << sLink;
  }

  // Permissions taken away, and given back afterwards so the temporary
  // folder can be removed
  QStringList astrModes = opts.astrModes;
  for(const QString &strRel : opts.astrUnreadable)
    astrModes << "000:" + strRel;
  const auto restoreModes = qScopeGuard([&]() {
    for(const QString &strMode : astrModes)
      ::chmod(QFile::encodeName(strRoot + "/in/" + strMode.mid(strMode.indexOf(':') + 1)).constData(), 0755);
  });
  for(const QString &strMode : astrModes)
  {
    bool bOk = false;
    const qsizetype nColon = strMode.indexOf(':');
    const mode_t nMode = static_cast<mode_t>(strMode.left(nColon).toUInt(&bOk, 8));
    if(!bOk || (nColon < 0) ||
       (::chmod(QFile::encodeName(strRoot + "/in/" + strMode.mid(nColon + 1)).constData(), nMode) != 0))
    {
      out << "harness: couldn't set the mode " << strMode << "\n";
      return 2;
    }
  }
  const QMap<QString, QString> mapSources = filesBelow(strRoot);

  // Paths as printed: the batch folder and the home folder by name
  const QString strRootCanonical = QFileInfo(strRoot).canonicalFilePath();
  auto shown = [&](QString str) {
    str.replace(strRootCanonical, "<batch>");
    str.replace(strRoot, "<batch>");
    str.replace(QDir::homePath(), "<home>");
    return str;
  };
  // A path in a step: below <tmp>/batch, or as given if it starts with "/"
  // or "~" or after "raw:" ("-" is empty)
  auto stepPath = [&](const QString &strArg) {
    if(strArg == "-")
      return QString();
    if(strArg.startsWith("raw:"))
      return strArg.mid(4);
    if(strArg.startsWith('/') || strArg.startsWith('~'))
      return strArg;
    return strRoot + "/" + strArg;
  };

  for(const QString &strFile : astrOpen)
  {
    window.openFile(strFile);
    QCoreApplication::processEvents();
    out << "harness: opened " << QFileInfo(strFile).fileName() << "\n";
  }
  if(!astrOpen.isEmpty() && !guiOpts.strOverlay.isEmpty())
    overlayMenu(window, guiOpts.strOverlay, out);
  if(!astrOpen.isEmpty() && !guiOpts.strOffset.isEmpty())
    offsetMenu(window, guiOpts.strOffset, astrOpen.last(), strTmpDir, out);

  QAction *pAction = findAction(window, QStringLiteral("Batch Process..."));
  if(!pAction)
  {
    out << "harness: menu Batch Process unavailable\n";
    out << "harness: done\n";
    return 0;
  }
  // Where it is: the menu's title and the item before it
  {
    QString strMenu = "-";
    QString strBefore = "-";
    const QList<QObject *> objects = pAction->associatedObjects();
    for(QObject *pObj : objects)
    {
      QMenu *pMenu = qobject_cast<QMenu *>(pObj);
      if(!pMenu)
        continue;
      strMenu = pMenu->title();
      const QList<QAction *> actions = pMenu->actions();
      const qsizetype nInd = actions.indexOf(pAction);
      if(nInd > 0)
        strBefore = actions.at(nInd - 1)->text();
    }
    out << "harness: batch menu=" << strMenu << " after=\"" << strBefore << "\" enabled=" << (pAction->isEnabled() ? 1 : 0)
        << " tip=\"" << pAction->statusTip() << "\"\n";
  }

  QStringList astrSteps;
  QString strChoose;              // The folder to choose in the folder dialog
  QPointer<QProgressDialog> pHooked;

  // The progress, while the batch runs
  ShowHook hook([&](QWidget *pWidget) {
    QProgressDialog *pProgress = qobject_cast<QProgressDialog *>(pWidget);
    if(pProgress && (pProgress->objectName() == QLatin1String("batchProgress")))
    {
      if(pHooked != pProgress)
      {
        pHooked = pProgress;
        out << "harness: batch progress shown=" << (pProgress->isVisible() ? 1 : 0) << " modal="
            << ((pProgress->windowModality() == Qt::ApplicationModal) ? "app" : "other") << " title=\""
            << pProgress->windowTitle() << "\"\n";
        QProgressBar *pBar = pProgress->findChild<QProgressBar *>();
        QObject::connect(pBar, &QProgressBar::valueChanged, pProgress, [&, pProgress](int nValue) {
          out << "harness: batch progress " << nValue << "/" << pProgress->maximum() << " \""
              << shown(pProgress->labelText()).replace('\n', " / ") << "\"\n";
          if((opts.nCancelAfter >= 0) && (nValue >= opts.nCancelAfter) && !pProgress->wasCanceled())
          {
            out << "harness: batch cancel\n";
            pProgress->cancel();
          }
          if((opts.nCloseAfter >= 0) && (nValue == opts.nCloseAfter))
          {
            window.close();
            out << "harness: batch window closed=" << (window.isVisible() ? 0 : 1) << "\n";
          }
        });
      }
    }
  });
  qApp->installEventFilter(&hook);

  // The folder dialog opens from the dialog's timer's slot, where that timer
  // doesn't fire again, so it has its own
  QTimer timerChooser;
  QObject::connect(&timerChooser, &QTimer::timeout, [&]() {
    QFileDialog *pChooser = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
    if(!pChooser || (pChooser->objectName() != QLatin1String("batchFolderDialog")))
      return;
    out << "harness: batch chooser: " << pChooser->windowTitle() << " (start "
        << shown(pChooser->directory().absolutePath()) << ")\n";
    if(strChoose.isEmpty())
    {
      pChooser->reject();
      return;
    }
    pChooser->setDirectory(strChoose);
    static_cast<QDialog *>(pChooser)->done(QDialog::Accepted);
  });
  timerChooser.start(20);

  QTimer timer;
  QObject::connect(&timer, &QTimer::timeout, [&]() {
    QWidget *pModal = QApplication::activeModalWidget();
    QDialog *pDlg = qobject_cast<QDialog *>(pModal);
    if(!pDlg || (pDlg->objectName() != QLatin1String("batchDialog")))
      return;
    QLineEdit *pIn = pDlg->findChild<QLineEdit *>("inputFolder");
    QLineEdit *pOut = pDlg->findChild<QLineEdit *>("outputFolder");
    QCheckBox *pSub = pDlg->findChild<QCheckBox *>("processSubfolders");
    QCheckBox *pAll = pDlg->findChild<QCheckBox *>("extractAll");
    QPushButton *pBrowseIn = pDlg->findChild<QPushButton *>("browseInput");
    QPushButton *pBrowseOut = pDlg->findChild<QPushButton *>("browseOutput");
    QDialogButtonBox *pButtons = pDlg->findChild<QDialogButtonBox *>("buttonBox");
    if(!pIn || !pOut || !pSub || !pAll || !pBrowseIn || !pBrowseOut || !pButtons)
    {
      out << "harness: batch dialog widgets not found\n";
      pDlg->reject();
      return;
    }

    auto fields = [&]() {
      return QString("in=%1 out=%2 subfolders=%3 all=%4")
               .arg(shown(pIn->text()), shown(pOut->text()))
               .arg(pSub->isChecked() ? 1 : 0)
               .arg(pAll->isChecked() ? 1 : 0);
    };

    while(!astrSteps.isEmpty())
    {
      const QString strStep = astrSteps.takeFirst();
      if(strStep == "fields")
        out << "harness: batch dialog: " << pDlg->windowTitle() << " (" << fields() << ")\n";
      else if(strStep.startsWith("in="))
        pIn->setText(stepPath(strStep.mid(3)));
      else if(strStep.startsWith("out="))
        pOut->setText(stepPath(strStep.mid(4)));
      else if(strStep == "subfolders")
        pSub->setChecked(true);
      else if(strStep == "all")
        pAll->setChecked(true);
      else if(strStep.startsWith("browse-in") || strStep.startsWith("browse-out"))
      {
        // The folder dialog comes next; the steps left carry on after it
        const qsizetype nEq = strStep.indexOf('=');
        strChoose = (nEq < 0) ? QString() : stepPath(strStep.mid(nEq + 1));
        (strStep.startsWith("browse-in") ? pBrowseIn : pBrowseOut)->click();
        out << "harness: batch after " << strStep << " (" << fields() << ")\n";
      }
      else if((strStep == "ok") || (strStep == "cancel"))
      {
        out << "harness: batch dialog " << strStep << " (" << fields() << ")\n";
        out.flush();
        // A refused OK shows a message box: the steps left carry on after it
        pButtons->button((strStep == "ok") ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
        return;
      }
      else
        out << "harness: unknown batch step " << strStep << "\n";
    }

    out << "harness: batch dialog still open: cancelled\n";
    pDlg->reject();
  });
  timer.start(20);

  int nOpening = 0;
  for(const QString &strOpening : opts.strSteps.split('+'))
  {
    nOpening++;
    astrSteps = strOpening.split(',', Qt::SkipEmptyParts);
    const QMap<QString, QString> mapBefore = filesBelow(strRoot);
    const QString strReportBefore = window.textEdit->toPlainText();

    out.flush();
    pAction->trigger();
    QCoreApplication::processEvents();

    // What it wrote, below <tmp>/batch
    const QMap<QString, QString> mapAfter = filesBelow(strRoot);
    int nNew = 0;
    int nChanged = 0;
    QStringList astrWritten;
    for(auto it = mapAfter.cbegin(); it != mapAfter.cend(); ++it)
    {
      const bool bNew = !mapBefore.contains(it.key());
      if(!bNew && (mapBefore.value(it.key()) == it.value()))
        continue;
      (bNew ? nNew : nChanged)++;
      astrWritten << it.key();
      out << "harness: batch " << nOpening << (bNew ? " new " : " changed ") << it.key()
          << " size=" << QFileInfo(strRoot + "/" + it.key()).size() << " md5=" << it.value() << "\n";
    }
    out << "harness: batch " << nOpening << " wrote new=" << nNew << " changed=" << nChanged << "\n";
    out << "harness: batch " << nOpening << " files: " << (astrWritten.isEmpty() ? "-" : astrWritten.join(',')) << "\n";

    const QString strReport = window.textEdit->toPlainText();
    if(strReport == strReportBefore)
      out << "harness: batch " << nOpening << " report unchanged\n";
    else if(strReport.contains("*** Batch Processing ***"))
      out << shown(strReport) << "\n";
    else
      out << "harness: batch " << nOpening << " report changed, without a batch\n";
    out.flush();
  }
  timer.stop();
  timerChooser.stop();
  qApp->removeEventFilter(&hook);

  for(const QString &strRel : opts.astrCat)
  {
    QFile file(strRoot + "/" + strRel);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
      out << "harness: cat " << strRel << ": can't be read\n";
      continue;
    }
    const QString strText = QString::fromUtf8(file.readAll());
    out << "harness: cat " << strRel << " bytes=" << file.size() << " lines=" << strText.split('\n').size()
        << " starts-blank=" << (strText.startsWith('\n') ? 1 : 0) << " ends-newline=" << (strText.endsWith('\n') ? 1 : 0)
        << "\n";
    for(const QString &strLine : strText.split('\n'))
      out << "harness: cat: " << shown(strLine) << "\n";
  }

  // What each link is now, and whether the file it led to changed
  for(const MadeLink &sLink : links)
  {
    struct stat sStat;
    const bool bStat = (::lstat(QFile::encodeName(strRoot + "/" + sLink.strName).constData(), &sStat) == 0);
    const QString strTarget = strRoot + "/" + sLink.strTarget;
    const bool bTargetExists = QFileInfo::exists(strTarget);
    const QString strTargetMd5 = bTargetExists ? fileMd5(strTarget) : QString();
    out << "harness: batch link " << sLink.strSpec << " now exists=" << (bStat ? 1 : 0)
        << " symlink=" << ((bStat && S_ISLNK(sStat.st_mode)) ? 1 : 0)
        << " names=" << (bStat ? static_cast<qulonglong>(sStat.st_nlink) : 0) << " target exists=" << (bTargetExists ? 1 : 0)
        << " unchanged=" << ((strTargetMd5 == sLink.strTargetMd5) ? 1 : 0) << "\n";
  }

  // The sources are as they were (unless the batch replaced one)
  {
    const QMap<QString, QString> mapAfter = filesBelow(strRoot);
    int nChanged = 0;
    for(auto it = mapSources.cbegin(); it != mapSources.cend(); ++it)
    {
      if(mapAfter.value(it.key()) != it.value())
        nChanged++;
    }
    out << "harness: batch sources unchanged=" << ((nChanged == 0) ? 1 : 0) << "\n";
  }

  // The file shown before the batch is still there
  if(!astrOpen.isEmpty())
  {
    const QString strLog = window.textEdit->toPlainText();
    const QRegularExpression reName("Filename: \\[([^\\]]*)\\]");
    window.reprocess();
    QCoreApplication::processEvents();
    const QRegularExpressionMatch match = reName.match(window.textEdit->toPlainText());
    out << "harness: batch after reprocess file=" << (match.hasMatch() ? QFileInfo(match.captured(1)).fileName() : "-")
        << " scan=" << (window.textEdit->toPlainText().contains("*** Decoding SCAN Data ***") ? 1 : 0) << "\n";
    if(!guiOpts.strOverlay.isEmpty())
      out << "harness: batch after reprocess overlay: " << overlayReport(window) << "\n";
    if(!guiOpts.strOffset.isEmpty())
      out << "harness: batch after reprocess offset: " << offsetReport(window, astrOpen.last(), strTmpDir) << "\n";
  }

  // Each log is the report File > Open shows for its file
  if(opts.bCompare)
  {
    const QRegularExpression reFile("Batch processing file .*: \\[(.*)\\]$");
    const QRegularExpression reLog("^    Log written to \\[(.*)\\]$");
    QString strSrc;
    int nSame = 0;
    int nCompared = 0;
    // The batch's report, from the last opening
    QStringList astrLines;
    {
      QStringList astrAll = window.textEdit->toPlainText().split('\n');
      astrLines = astrAll;
    }
    // (Reprocess above replaced the report if a file was open: run the
    // comparison only without one)
    for(const QString &strLine : astrLines)
    {
      const QRegularExpressionMatch mFile = reFile.match(strLine);
      if(mFile.hasMatch())
      {
        strSrc = mFile.captured(1);
        continue;
      }
      const QRegularExpressionMatch mLog = reLog.match(strLine);
      if(!mLog.hasMatch() || strSrc.isEmpty())
        continue;
      QFile fileLog(mLog.captured(1));
      if(!fileLog.open(QIODevice::ReadOnly | QIODevice::Text))
        continue;
      const QString strLogText = QString::fromUtf8(fileLog.readAll());
      window.openFile(strSrc);
      QCoreApplication::processEvents();
      const bool bSame = (window.textEdit->toPlainText() == strLogText);
      nCompared++;
      nSame += bSame ? 1 : 0;
      out << "harness: batch compare " << shown(mLog.captured(1)) << " same=" << (bSame ? 1 : 0) << "\n";
    }
    out << "harness: batch compared " << nCompared << " same=" << nSame << "\n";
  }

  // Message boxes are on again after the batch
  if(opts.bReopenMissing)
  {
    window.openFile(strRoot + "/no-such-file.jpg");
    QCoreApplication::processEvents();
  }

  // The folders are saved with the settings when the app quits
  window.close();
  QCoreApplication::processEvents();
  {
    QSettings settings;
    out << "harness: batch saved in=" << shown(settings.value("General\\BatchLastInput").toString())
        << " out=" << shown(settings.value("General\\BatchLastOutput").toString()) << "\n";
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
  guiOpts.bEditBefore = args.removeAll("--edit-before") > 0;
  ExportOptions exportOpts;
  exportOpts.sJpeg.bExtractAll = args.removeAll("--all") > 0;
  exportOpts.sJpeg.bForceSoi = args.removeAll("--force-soi") > 0;
  exportOpts.sJpeg.bForceEoi = args.removeAll("--force-eoi") > 0;
  exportOpts.sJpeg.bIgnoreEoi = args.removeAll("--ignore-eoi") > 0;
  exportOpts.sJpeg.bDhtAviInsert = args.removeAll("--dht-avi") > 0;
  exportOpts.sJpeg.bOverlayEn = args.removeAll("--overlays") > 0;
  exportOpts.bOutSame = args.removeAll("--out-same") > 0;
  exportOpts.bOutSameCase = args.removeAll("--out-same-case") > 0;
  BatchOptions batchOpts;
  batchOpts.bCompare = args.removeAll("--batch-compare") > 0;
  batchOpts.bReopenMissing = args.removeAll("--batch-open-missing") > 0;
  // Options that can be given more than once
  for(const char *pszOpt : { "--batch-cat", "--batch-unreadable", "--batch-mode", "--batch-link" })
  {
    QStringList &astrValues = (QLatin1String(pszOpt) == QLatin1String("--batch-cat"))          ? batchOpts.astrCat
                              : (QLatin1String(pszOpt) == QLatin1String("--batch-unreadable")) ? batchOpts.astrUnreadable
                              : (QLatin1String(pszOpt) == QLatin1String("--batch-mode"))       ? batchOpts.astrModes
                                                                                               : batchOpts.astrLinks;
    for(qsizetype nInd = args.indexOf(QLatin1String(pszOpt)); nInd >= 0; nInd = args.indexOf(QLatin1String(pszOpt)))
    {
      if(nInd + 1 >= args.size())
      {
        usage();
        return 2;
      }
      astrValues << args.at(nInd + 1);
      args.remove(nInd, 2);
    }
  }

  // Options with a text value (--scale was applied above)
  QString strPixels;
  QString strFormat;
  QString strDetail;
  for(const char *pszOpt : { "--scale", "--zoom", "--pixels", "--out", "--format", "--detail", "--detail-dlg", "--mark",
                             "--edit", "--overlay", "--offset", "--batch" })
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
    else if(QLatin1String(pszOpt) == QLatin1String("--edit"))
      guiOpts.strEdit = args.at(nInd + 1);
    else if(QLatin1String(pszOpt) == QLatin1String("--overlay"))
      guiOpts.strOverlay = exportOpts.strOverlay = args.at(nInd + 1);
    else if(QLatin1String(pszOpt) == QLatin1String("--offset"))
      guiOpts.strOffset = args.at(nInd + 1);
    else if(QLatin1String(pszOpt) == QLatin1String("--batch"))
      batchOpts.strSteps = args.at(nInd + 1);
    args.remove(nInd, 2);
  }

  // Options with a numeric value
  uint32_t nStart = 0;
  for(const char *pszOpt : { "--repeat", "--max-growth-mb", "--max-zoom-mb", "--start", "--detail-dlg-at", "--overlay-at",
                             "--offset-at", "--batch-cancel-after", "--batch-close-after" })
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
    else if(QLatin1String(pszOpt) == QLatin1String("--overlay-at"))
      guiOpts.nOverlayAt = nVal;
    else if(QLatin1String(pszOpt) == QLatin1String("--offset-at"))
      guiOpts.nOffsetAt = nVal;
    else if(QLatin1String(pszOpt) == QLatin1String("--batch-cancel-after"))
      batchOpts.nCancelAfter = nVal;
    else if(QLatin1String(pszOpt) == QLatin1String("--batch-close-after"))
      batchOpts.nCloseAfter = nVal;
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
  if(strMode == "batch" && !args.isEmpty() && !batchOpts.strSteps.isEmpty())
    return runBatchMode(args.first(), args.mid(1), guiOpts, batchOpts, tmpDir.path());

  usage();
  return 2;
}
