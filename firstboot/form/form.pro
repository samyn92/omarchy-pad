QT += core gui qml quick quickcontrols2 network

CONFIG += c++17 release link_pkgconfig
PKGCONFIG += libsystemd
TARGET = omarchy-pad-owner-form
TEMPLATE = app

HEADERS += src/ownerform.h
SOURCES += src/main.cpp src/ownerform.cpp
RESOURCES += src/resources.qrc
