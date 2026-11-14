# Headless test driver for JPEGsnoopQt (see tests/README.md).
# Builds the app's sources (except source/main.cpp) with tests/harness/main.cpp.

TEMPLATE = app
TARGET = jpegsnoop-harness
CONFIG += console
CONFIG -= app_bundle

include(../../JPEGsnoopQt.pri)

SOURCES += main.cpp
