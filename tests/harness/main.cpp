// Headless test driver for JPEGsnoopQt.
//
// decode <file> [--ac] [--histo]
//     Decode one file with the app's decoder classes (CjfifDecode, CimgDecode,
//     CwindowBuf, ...) in non-interactive mode and print the report to stdout,
//     followed by a "harness:" summary line.
//
// gui <file> [<file> ...] [--mouse]
//     Build the real MainWindow, open each file in turn as File > Open does,
//     and print a "harness:" line per file. With --mouse, move the mouse over
//     the preview window and click at points inside, at the edges of and
//     outside the image after each file.
//
// Always runs offscreen and keeps any settings in a temporary folder, so it
// never shows a window or touches the user's JPEGsnoop settings.

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QMouseEvent>
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

static void usage()
{
  fprintf(stderr,
          "usage: jpegsnoop-harness decode <file> [--ac] [--histo]\n"
          "       jpegsnoop-harness gui <file> [<file> ...] [--mouse]\n");
}

static int runDecode(const QString &strFile, bool bAc, bool bHisto, const QString &strDbDir)
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

  CDbSigs sigs(&log, &cfg);
  CwindowBuf wbuf(&log);
  CimgDecode img(&log, &wbuf, &cfg, nullptr);
  CjfifDecode jfif(&log, &sigs, &wbuf, &img, &cfg, nullptr);

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
  out << QString("harness: decoded=%1 preview=%2x%3\n")
           .arg(img.IsImgDecoded() ? 1 : 0)
           .arg(img.m_bDibTempReady && img.m_pDibTemp ? img.m_pDibTemp->width() : 0)
           .arg(img.m_bDibTempReady && img.m_pDibTemp ? img.m_pDibTemp->height() : 0);
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

static int runGui(const QStringList &files, bool bMouse, const QString &strSettingsDir)
{
  // Keep settings in the temporary folder and pre-accept the first-run notice
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, strSettingsDir);
  {
    QSettings settings;
    settings.setValue("General\\EulaAccepted", true);
    settings.setValue("General\\DecScanImg", true);
    settings.setValue("General\\UserDbPath", strSettingsDir);
  }

  MainWindow window;
  window.show();
  QCoreApplication::processEvents();

  QTextStream out(stdout);
  for(const QString &strFile : files)
  {
    window.openFile(strFile);
    QCoreApplication::processEvents();

    const QString strLog = window.textEdit->toPlainText();
    out << QString("harness: file=%1 scan=%2\n")
             .arg(QFileInfo(strFile).fileName())
             .arg(strLog.contains("*** Decoding SCAN Data ***") ? 1 : 0);
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
  if(args.size() < 2)
  {
    usage();
    return 2;
  }
  const QString strMode = args.takeFirst();
  const bool bAc = args.removeAll("--ac") > 0;
  const bool bHisto = args.removeAll("--histo") > 0;
  const bool bMouse = args.removeAll("--mouse") > 0;

  QTemporaryDir tmpDir;
  if(!tmpDir.isValid())
  {
    fprintf(stderr, "cannot create a temporary folder\n");
    return 2;
  }

  if(strMode == "decode" && args.size() == 1)
    return runDecode(args.first(), bAc, bHisto, tmpDir.path());
  if(strMode == "gui" && !args.isEmpty())
    return runGui(args, bMouse, tmpDir.path());

  usage();
  return 2;
}
