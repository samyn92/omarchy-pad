QT += core gui qml quick testlib network
CONFIG += c++17 testcase link_pkgconfig
PKGCONFIG += wayland-client libinput libudev gbm libdrm egl
LIBS += -lLayerShellQtInterface
TARGET = stage_tests
TEMPLATE = app

include(../protocols.pri)

INCLUDEPATH += ../src

HEADERS += \
    ../src/apps.h \
    ../src/capture.h \
    ../src/fingers.h \
    ../src/gpupicture.h \
    ../src/hyprland.h \
    ../src/images.h \
    ../src/ipc.h \
    ../src/longpress.h \
    ../src/pointer.h \
    ../src/shelves.h \
    ../src/stage.h \
    ../src/theme.h \
    ../src/touchscreen.h \
    ../src/translator.h

SOURCES += \
    stage_tests.cpp \
    ../src/apps.cpp \
    ../src/capture.cpp \
    ../src/fingers.cpp \
    ../src/gpupicture.cpp \
    ../src/hyprland.cpp \
    ../src/images.cpp \
    ../src/ipc.cpp \
    ../src/pointer.cpp \
    ../src/shelves.cpp \
    ../src/stage.cpp \
    ../src/theme.cpp \
    ../src/touchscreen.cpp \
    ../src/translator.cpp

RESOURCES += ../src/resources.qrc
