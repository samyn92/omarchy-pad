# The fcitx5 addon that tells floatkey when a text field has focus. Plain C++
# against fcitx5's own libraries; no Qt.
TEMPLATE = lib
CONFIG += plugin c++20 release link_pkgconfig
CONFIG -= qt
PKGCONFIG += Fcitx5Core
TARGET = floatkey

HEADERS += fieldtype.h
SOURCES += floatkeyfocus.cpp
