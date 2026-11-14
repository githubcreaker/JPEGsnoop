#-------------------------------------------------
#
# Project created by QtCreator 2018-06-07T16:53:35
#
#-------------------------------------------------

QT += core gui printsupport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = JPEGsnoopQt
TEMPLATE = app

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

win32{
    RC_FILE += win_res.rc
}

OTHER_FILES += \
    images\jpegsnoop.ico \
    win_res.rc

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES =  source/main.cpp \
    source/note.cpp \
    source/eula.cpp
SOURCES += source/DbSigs.cpp
SOURCES += source/DecodePs.cpp
SOURCES += source/DocLog.cpp
SOURCES += source/General.cpp
SOURCES += source/ImgDecode.cpp
SOURCES += source/JfifDecode.cpp
SOURCES += source/labelClick.cpp
SOURCES += source/Md5.cpp
SOURCES += source/SnoopConfig.cpp
SOURCES += source/WindowBuf.cpp
SOURCES += source/mainwindow.cpp
SOURCES += source/q_decodedetaildlg.cpp
SOURCES += source/snoopconfigdialog.cpp
SOURCES += source/Viewer.cpp

HEADERS =  source/mainwindow.h \
    source/note.h \
    source/eula.h
HEADERS += source/DbSigs.h
HEADERS += source/DecodeDicom.h
HEADERS += source/DecodePs.h
HEADERS += source/DocLog.h
HEADERS += source/General.h
HEADERS += source/ImgDecode.h
HEADERS += source/JfifDecode.h
HEADERS += source/labelClick.h
HEADERS += source/Md5.h
HEADERS += source/SnoopConfig.h
HEADERS += source/WindowBuf.h
HEADERS += source/q_decodedetaildlg.h
HEADERS += source/snoop.h
HEADERS += source/snoopconfigdialog.h
HEADERS += source/Viewer.h

FORMS += \
    source/snoopconfigdialog.ui \
    source/q_decodedetaildlg.ui \
    source/note.ui \
    source/eula.ui \
    source/mainwindow.ui

RESOURCES = JPEGsnoop.qrc

DISTFILES += \
    win_res.rc
