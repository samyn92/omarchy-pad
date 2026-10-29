QT += core gui qml quick testlib network multimedia
CONFIG += c++20 testcase link_pkgconfig
PKGCONFIG += wayland-client xkbcommon libinput libudev Fcitx5Utils
LIBS += -lLayerShellQtInterface
TARGET = floatkey_tests
TEMPLATE = app

include(../protocols.pri)

INCLUDEPATH += ../src ../fcitx5

HEADERS += \
    ../src/clipboardhistory.h \
    ../src/controller.h \
    ../src/dictation.h \
    ../src/ipc.h \
    ../src/keyboardwatcher.h \
    ../src/keymap.h \
    ../src/taps.h \
    ../src/theme.h \
    ../src/translator.h \
    ../src/virtualkeyboard.h \
    ../src/xkblayouts.h

SOURCES += \
    floatkey_tests.cpp \
    ../src/clipboardhistory.cpp \
    ../src/controller.cpp \
    ../src/dictation.cpp \
    ../src/ipc.cpp \
    ../src/keyboardwatcher.cpp \
    ../src/keymap.cpp \
    ../src/taps.cpp \
    ../src/theme.cpp \
    ../src/translator.cpp \
    ../src/virtualkeyboard.cpp \
    ../src/xkblayouts.cpp

RESOURCES += ../src/resources.qrc
