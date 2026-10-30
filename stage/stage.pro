QT += core gui qml quick network

CONFIG += c++17 release link_pkgconfig
PKGCONFIG += wayland-client libinput libudev gbm libdrm egl
LIBS += -lLayerShellQtInterface
TARGET = stage
# The one place stage's version is set: --version and the package read it
# from here. The commit it was built from comes along, so builds of the same
# version can be told apart.
VERSION = 0.8.0
COMMIT = $$system(git -C $$PWD rev-parse --short HEAD 2>/dev/null)
DEFINES += STAGE_VERSION=\\\"$$VERSION\\\" STAGE_COMMIT=\\\"$$COMMIT\\\"
TEMPLATE = app

include(protocols.pri)

HEADERS += \
    src/apps.h \
    src/capture.h \
    src/fingers.h \
    src/gpupicture.h \
    src/hyprland.h \
    src/images.h \
    src/ipc.h \
    src/shelves.h \
    src/stage.h \
    src/theme.h \
    src/touchscreen.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/apps.cpp \
    src/capture.cpp \
    src/fingers.cpp \
    src/gpupicture.cpp \
    src/hyprland.cpp \
    src/images.cpp \
    src/ipc.cpp \
    src/shelves.cpp \
    src/stage.cpp \
    src/theme.cpp \
    src/touchscreen.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
