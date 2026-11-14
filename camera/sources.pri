# Everything but main(), shared by the app and its tests.
INCLUDEPATH += $$PWD/src

HEADERS += \
    $$PWD/src/cameras.h \
    $$PWD/src/focus.h \
    $$PWD/src/frame.h \
    $$PWD/src/orientation.h \
    $$PWD/src/photo.h \
    $$PWD/src/theme.h \
    $$PWD/src/viewfinder.h

SOURCES += \
    $$PWD/src/cameras.cpp \
    $$PWD/src/focus.cpp \
    $$PWD/src/frame.cpp \
    $$PWD/src/orientation.cpp \
    $$PWD/src/photo.cpp \
    $$PWD/src/theme.cpp \
    $$PWD/src/viewfinder.cpp
