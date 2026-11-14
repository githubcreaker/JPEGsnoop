#-------------------------------------------------
#
# Project created by QtCreator 2018-06-07T16:53:35
#
#-------------------------------------------------

TARGET = JPEGsnoopQt
TEMPLATE = app

# Compiler settings and all sources except main.cpp (shared with tests/harness)
include(JPEGsnoopQt.pri)

SOURCES += source/main.cpp

win32{
    RC_FILE += win_res.rc
}

OTHER_FILES += \
    images\jpegsnoop.ico \
    win_res.rc

DISTFILES += \
    win_res.rc
