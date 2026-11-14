// Headless test driver for JPEGsnoopQt.
//
// decode <file> [--ac] [--histo] [--unknown-exif]
//     Decode one file with the app's decoder classes (CjfifDecode, CimgDecode,
//     CwindowBuf, ...) in non-interactive mode and print the report to stdout,
//     followed by a "harness:" summary line. --ac: full IDCT; --histo:
//     histograms; --unknown-exif: also report unknown EXIF tags.
//
// gui <file> [<file> ...] [--mouse] [--log] [--reprocess-first]
//     Build the real MainWindow, open each file in turn as File > Open does,
//     and print a "harness:" line per file. With --mouse, move the mouse over
//     the preview window and click at points inside, at the edges of and
//     outside the image after each file; --log prints the report after each
//     file; --reprocess-first runs Reprocess before opening anything.
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
#include <QMouseEvent>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QPointingDevice>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>
#include <cstdio>

#include "DbSigs.h"
#include "DocLog.h"
#include "ImgDecode.h"
#include "JfifDecode.h"
#include "SnoopConfig.h"
#include "Viewer.h"
#include "WindowBuf.h"
#include "labelClick.h"
#include "mainwindow.h"
#include "snoopconfigdialog.h"

static void usage()
{
  fprintf(stderr,
          "usage: jpegsnoop-harness decode <file> [--ac] [--histo] [--unknown-exif]\n"
          "       jpegsnoop-harness gui <file> [<file> ...] [--mouse] [--log] [--reprocess-first]\n"
          "       jpegsnoop-harness settings\n");
}

static int runDecode(const QString &strFile, bool bAc, bool bHisto, bool bUnknownExif, const QString &strDbDir)
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

static void sendMouse(QWidget *pWidget, QEvent::Type eType, const QPoint &pt)
{
  const Qt::MouseButton eButton = (eType == QEvent::MouseMove) ? Qt::NoButton : Qt::LeftButton;
  const Qt::MouseButtons nButtons = (eType == QEvent::MouseButtonPress) ? Qt::LeftButton : Qt::NoButton;
  QMouseEvent ev(eType, QPointF(pt), pWidget->mapToGlobal(QPointF(pt)), eButton, nButtons, Qt::NoModifier,
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
  labelClick *pLabel = pViewer->findChild<labelClick *>();
  if(!pLabel)
    return;

  const int w = pLabel->width();
  const int h = pLabel->height();
  const QPoint aPts[] = {
    QPoint(0, 0), QPoint(w / 2, h / 2), QPoint(w - 1, h - 1), QPoint(w - 1, 0), QPoint(0, h - 1),
    QPoint(w * 3, h * 3), QPoint(-40, -40), QPoint(-1, h / 2), QPoint(w / 2, -1),
  };
  for(const QPoint &pt : aPts)
  {
    sendMouse(pLabel, QEvent::MouseMove, pt);
    sendMouse(pLabel, QEvent::MouseButtonPress, pt);
    sendMouse(pLabel, QEvent::MouseButtonRelease, pt);
  }
  QCoreApplication::processEvents();
}

static int runGui(const QStringList &files, bool bMouse, bool bLog, bool bReprocessFirst,
                  const QString &strSettingsDir)
{
  // Otherwise a fresh install (so "Decode Image" etc. take their defaults),
  // minus the coach tips, which are modal and can't be clicked away here
  useTestSettings(strSettingsDir);
  {
    QSettings settings;
    settings.setValue("Coach\\ReprocessAuto", false);
    settings.setValue("Coach\\DecodeIdct", false);
  }

  MainWindow window;
  window.show();
  QCoreApplication::processEvents();

  QTextStream out(stdout);

  // File > Reprocess (or auto-reprocess after an option change) before any
  // file has been opened
  if(bReprocessFirst)
  {
    window.reprocess();
    out << "harness: reprocessed with no file\n";
  }

  for(const QString &strFile : files)
  {
    window.openFile(strFile);
    QCoreApplication::processEvents();

    const QString strLog = window.textEdit->toPlainText();
    out << QString("harness: file=%1 scan=%2\n")
             .arg(QFileInfo(strFile).fileName())
             .arg(strLog.contains("*** Decoding SCAN Data ***") ? 1 : 0);
    if(bLog)
      out << strLog << "\n";
    out.flush();

    if(bMouse)
      exerciseMouse();
  }
  out << "harness: done\n";
  return 0;
}

int main(int argc, char **argv)
{
  if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");

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
  const bool bMouse = args.removeAll("--mouse") > 0;
  const bool bLog = args.removeAll("--log") > 0;
  const bool bReprocessFirst = args.removeAll("--reprocess-first") > 0;

  QTemporaryDir tmpDir;
  if(!tmpDir.isValid())
  {
    fprintf(stderr, "cannot create a temporary folder\n");
    return 2;
  }

  if(strMode == "decode" && args.size() == 1)
    return runDecode(args.first(), bAc, bHisto, bUnknownExif, tmpDir.path());
  if(strMode == "gui" && !args.isEmpty())
    return runGui(args, bMouse, bLog, bReprocessFirst, tmpDir.path());
  if(strMode == "settings" && args.isEmpty())
    return runSettings(tmpDir.path());

  usage();
  return 2;
}
