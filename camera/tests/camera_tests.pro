QT += core gui qml quick opengl testlib
CONFIG += c++17 testcase link_pkgconfig
PKGCONFIG += libcamera libexif egl libdrm
TARGET = camera_tests
TEMPLATE = app
DEFINES += CAMERA_VERSION=\\\"test\\\" CAMERA_COMMIT=\\\"test\\\"

include(../sources.pri)

SOURCES += camera_tests.cpp
RESOURCES += ../src/resources.qrc
