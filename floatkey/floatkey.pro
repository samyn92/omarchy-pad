QT += core gui qml quick network multimedia

CONFIG += c++17 release link_pkgconfig
PKGCONFIG += wayland-client xkbcommon
LIBS += -lLayerShellQtInterface
TARGET = floatkey
TEMPLATE = app

include(protocols.pri)

HEADERS += \
    src/controller.h \
    src/dictation.h \
    src/ipc.h \
    src/keyboardwatcher.h \
    src/keymap.h \
    src/theme.h \
    src/translator.h \
    src/virtualkeyboard.h \
    src/xkblayouts.h

SOURCES += \
    src/main.cpp \
    src/controller.cpp \
    src/dictation.cpp \
    src/ipc.cpp \
    src/keyboardwatcher.cpp \
    src/keymap.cpp \
    src/theme.cpp \
    src/translator.cpp \
    src/virtualkeyboard.cpp \
    src/xkblayouts.cpp

RESOURCES += src/resources.qrc
