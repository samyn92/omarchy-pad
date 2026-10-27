QT += core gui qml quick network

CONFIG += c++17 release link_pkgconfig
PKGCONFIG += wayland-client libinput libudev
LIBS += -lLayerShellQtInterface
TARGET = stage
TEMPLATE = app

include(protocols.pri)

HEADERS += \
    src/apps.h \
    src/capture.h \
    src/fingers.h \
    src/hyprland.h \
    src/images.h \
    src/ipc.h \
    src/stage.h \
    src/theme.h \
    src/touchscreen.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/apps.cpp \
    src/capture.cpp \
    src/fingers.cpp \
    src/hyprland.cpp \
    src/images.cpp \
    src/ipc.cpp \
    src/stage.cpp \
    src/theme.cpp \
    src/touchscreen.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
