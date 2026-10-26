QT += core gui qml quick testlib network
CONFIG += c++17 testcase link_pkgconfig
PKGCONFIG += wayland-client xkbcommon
LIBS += -lLayerShellQtInterface
TARGET = floatkey_tests
TEMPLATE = app

include(../protocols.pri)

INCLUDEPATH += ../src

HEADERS += \
    ../src/controller.h \
    ../src/ipc.h \
    ../src/keymap.h \
    ../src/theme.h \
    ../src/translator.h \
    ../src/virtualkeyboard.h

SOURCES += \
    floatkey_tests.cpp \
    ../src/controller.cpp \
    ../src/ipc.cpp \
    ../src/keymap.cpp \
    ../src/theme.cpp \
    ../src/translator.cpp \
    ../src/virtualkeyboard.cpp

RESOURCES += ../src/resources.qrc
