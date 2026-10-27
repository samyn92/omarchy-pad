#pragma once

#include <QString>
#include <QVariantMap>

// What the keys of an XKB layout type, read from the system's keyboard
// layouts, so the keyboard can draw any layout the way the user's hardware
// one has it. A layout is named as in Hyprland's input:kb_layout, with an
// optional variant in brackets: "fr", "de(nodeadkeys)", "us(dvorak)".
namespace XkbLayouts {

// {name: "French", keys: {AE01: ["&", "1"], AD01: ["a", "A"], ...}}: the
// unshifted and shifted character of every key of the main block, by its XKB
// name. Empty when XKB doesn't know the layout.
QVariantMap read(const QString &layout);

// The layout's name in XKB's list ("German (no dead keys)"), or empty.
QString describe(const QString &layout);

// The keys read() covers, row by row, top to bottom.
QList<QStringList> rows();

}
