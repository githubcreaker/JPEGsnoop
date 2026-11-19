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

#include <QApplication>
#include <QTextStream>

#include <cstdio>

#include "cmdline.h"
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    // With any option, the app runs without its window, as on Windows (see
    // cmdline.h). The arguments are read before Qt starts, so that on macOS
    // such a run never shows in the Dock; and Qt doesn't see them then, so
    // it doesn't take a file named "-platform", say, for its own option.
    // (So Qt's own options, such as -style, are unknown options here.)
    QStringList astrArgs;
    for(int nArg = 1; nArg < argc; nArg++)
      astrArgs << QString::fromLocal8Bit(argv[nArg]);
    const CmdLineOptions sCmdLine = parseCmdLine(astrArgs);
    if(sCmdLine.bCmdLine)
    {
      hideArgsFromQt(argc, argv);
#ifdef Q_OS_MACOS
      prepareCmdLineApp();
#endif
    }

    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName("ImpulseAdventure");
    QCoreApplication::setOrganizationDomain("www.impulseadventure.com");
    QCoreApplication::setApplicationName("JPEGsnoopQt");

    if(sCmdLine.bCmdLine)
    {
      QTextStream out(stdout);
      QTextStream err(stderr);
      return runCmdLineApp(sCmdLine, out, err);
    }

    MainWindow w;
    w.show();

    // A file named on the command line, as on Windows
    w.openArguments(QCoreApplication::arguments().mid(1));

    return a.exec();
}
