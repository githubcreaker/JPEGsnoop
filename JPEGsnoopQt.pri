# Settings and sources shared by the app (JPEGsnoopQt.pro) and the
# headless test driver (tests/harness/harness.pro). Paths use $$PWD so this
# file can be included from any directory.

QT += core gui printsupport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

# Qt 6 requires C++17 (also fine for Qt 5.12+)
CONFIG += c++17

###
# Disable warnings in Qt system files
# Clang drops a -I/-F path that is also given as -isystem/-iframework,
# so mark the exact paths qmake uses as system paths. Homebrew Qt uses
# include/QtX symlinks; the official Qt installer uses framework Headers.
macx{
    QMAKE_CXXFLAGS += -iframework $$[QT_INSTALL_LIBS]
    for(mod, $$list(QtCore QtGui QtWidgets QtPrintSupport)) {
        exists($$[QT_INSTALL_HEADERS]/$$mod): QMAKE_CXXFLAGS += -isystem $$[QT_INSTALL_HEADERS]/$$mod
        exists($$[QT_INSTALL_LIBS]/$${mod}.framework/Headers): QMAKE_CXXFLAGS += -isystem $$[QT_INSTALL_LIBS]/$${mod}.framework/Headers
    }
}

linux:!macx{
    QMAKE_CXXFLAGS += -isystem /usr/include/qt5/QtCore
    QMAKE_CXXFLAGS += -isystem /usr/include/qt5/QtGui
    QMAKE_CXXFLAGS += -isystem /usr/include/qt5/QtWidgets
}

win32{
    QMAKE_CXXFLAGS += -isystem C:/Qt/5.11.1/mingw53_32/include/QtCore
    QMAKE_CXXFLAGS += -isystem C:/Qt/5.11.1/mingw53_32/include/QtGui
    QMAKE_CXXFLAGS += -isystem C:/Qt/5.11.1/mingw53_32/include/QtWidgets
}
###

gcc{
    QMAKE_CXXFLAGS += -fstrict-aliasing -Wextra -pedantic -Weffc++ -Wfloat-equal -Wswitch-default -Wcast-align -Wcast-qual -Wchar-subscripts -Wcomment
    QMAKE_CXXFLAGS += -Wdisabled-optimization -Wformat-nonliteral -Wformat-security -Wconversion -Wformat-nonliteral -Wformat-y2k -Wformat=2 -Wimport
    QMAKE_CXXFLAGS += -Winit-self -Winline -Winvalid-pch -Wmissing-format-attribute -Wmissing-include-dirs -Wmissing-noreturn -Wold-style-cast
    QMAKE_CXXFLAGS += -Wpacked -Wpointer-arith -Wredundant-decls -Wshadow -Wstack-protector -Wstrict-aliasing=2 -Wswitch-enum -Wunreachable-code
    QMAKE_CXXFLAGS += -Wunused -Wvariadic-macros -Wwrite-strings
}

# Address C1228 for DbSigs
msvc{
    QMAKE_CXXFLAGS += -bigobj
}

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

INCLUDEPATH += $$PWD/source

# All sources except source/main.cpp, which only the app adds
SOURCES += \
    $$PWD/source/note.cpp \
    $$PWD/source/eula.cpp \
    $$PWD/source/DbSigs.cpp \
    $$PWD/source/DecodePs.cpp \
    $$PWD/source/DocLog.cpp \
    $$PWD/source/FileTiff.cpp \
    $$PWD/source/General.cpp \
    $$PWD/source/ImgDecode.cpp \
    $$PWD/source/JfifDecode.cpp \
    $$PWD/source/Md5.cpp \
    $$PWD/source/SnoopConfig.cpp \
    $$PWD/source/WindowBuf.cpp \
    $$PWD/source/mainwindow.cpp \
    $$PWD/source/exportdialogs.cpp \
    $$PWD/source/q_decodedetaildlg.cpp \
    $$PWD/source/snoopconfigdialog.cpp \
    $$PWD/source/Viewer.cpp

HEADERS += \
    $$PWD/source/mainwindow.h \
    $$PWD/source/note.h \
    $$PWD/source/eula.h \
    $$PWD/source/DbSigs.h \
    $$PWD/source/DecodeDicom.h \
    $$PWD/source/DecodePs.h \
    $$PWD/source/DocLog.h \
    $$PWD/source/FileTiff.h \
    $$PWD/source/General.h \
    $$PWD/source/ImgDecode.h \
    $$PWD/source/JfifDecode.h \
    $$PWD/source/Md5.h \
    $$PWD/source/SnoopConfig.h \
    $$PWD/source/WindowBuf.h \
    $$PWD/source/exportdialogs.h \
    $$PWD/source/q_decodedetaildlg.h \
    $$PWD/source/snoop.h \
    $$PWD/source/snoopconfigdialog.h \
    $$PWD/source/Viewer.h

FORMS += \
    $$PWD/source/snoopconfigdialog.ui \
    $$PWD/source/q_decodedetaildlg.ui \
    $$PWD/source/note.ui \
    $$PWD/source/eula.ui \
    $$PWD/source/mainwindow.ui

RESOURCES += $$PWD/JPEGsnoop.qrc
