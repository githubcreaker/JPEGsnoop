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

macx{
    # Bundle ID com.impulseadventure.JPEGsnoopQt: the same name macOS gives
    # the app's settings (from APP_ORG_DOMAIN and APP_NAME in source/snoop.h),
    # instead of qmake's placeholder com.yourcompany.
    QMAKE_TARGET_BUNDLE_PREFIX = com.impulseadventure

    # Version for Finder and the bundle (CFBundleShortVersionString), taken
    # from VERSION_STR in source/snoop.h so it is kept in one place
    SNOOP_H_LINES = $$cat($$PWD/source/snoop.h, lines)
    VERSION_LINE = $$find(SNOOP_H_LINES, "^.define VERSION_STR ")
    VERSION = $$section(VERSION_LINE, \", 1, 1)
    isEmpty(VERSION): error("Could not read VERSION_STR from source/snoop.h")

    # Qt's default template has no version keys
    QMAKE_INFO_PLIST = macos/Info.plist

    # Made from images/jpegsnoop.ico by scripts/make-macos-icon.py
    ICON = images/jpegsnoop.icns
}

OTHER_FILES += \
    images\jpegsnoop.ico \
    win_res.rc

DISTFILES += \
    win_res.rc
