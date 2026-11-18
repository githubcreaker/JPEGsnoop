# Headless test driver for JPEGsnoopQt (see tests/README.md).
# Builds the app's sources (except source/main.cpp) with tests/harness/main.cpp.

TEMPLATE = app
TARGET = jpegsnoop-harness
CONFIG += console
CONFIG -= app_bundle

include(../../JPEGsnoopQt.pri)

# QTest's key and mouse clicks go through Qt's shortcut handling, as a
# user's do (gui --edit)
QT += testlib
# Qt's own entry points for a drag from another app and a file sent by Finder
# (--outside)
QT += gui-private

SOURCES += main.cpp
