QT += core gui qml quick opengl

CONFIG += c++17 release link_pkgconfig
PKGCONFIG += libcamera libexif egl libdrm
TARGET = omarchy-pad-camera
# The one place the camera's version is set: --version, the EXIF and the
# package read it from here. The commit it was built from comes along, so
# builds of the same version can be told apart.
VERSION = 0.1.0
COMMIT = $$system(git -C $$PWD rev-parse --short HEAD 2>/dev/null)
DEFINES += CAMERA_VERSION=\\\"$$VERSION\\\" CAMERA_COMMIT=\\\"$$COMMIT\\\"
TEMPLATE = app

include(sources.pri)

SOURCES += src/main.cpp
RESOURCES += src/resources.qrc
