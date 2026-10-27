#include "xkblayouts.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <xkbcommon/xkbcommon.h>

namespace XkbLayouts {

QList<QStringList> rows()
{
    auto numbered = [](const QString &prefix, int count) {
        QStringList names;
        for (int i = 1; i <= count; ++i)
            names << QString("%1%2").arg(prefix).arg(i, 2, 10, QChar('0'));
        return names;
    };
    return {
        QStringList{"TLDE"} + numbered("AE", 12),
        numbered("AD", 12) + QStringList{"BKSL"},
        numbered("AC", 12),
        QStringList{"LSGT"} + numbered("AB", 10),
    };
}

// A dead key types nothing by itself; the key shows the accent it puts on
// the next letter.
static QString deadKey(xkb_keysym_t sym)
{
    static const QHash<QString, QString> accents = {
        {"dead_grave", "`"}, {"dead_acute", "´"}, {"dead_circumflex", "^"}, {"dead_tilde", "~"},
        {"dead_macron", "¯"}, {"dead_breve", "˘"}, {"dead_abovedot", "˙"}, {"dead_diaeresis", "¨"},
        {"dead_abovering", "˚"}, {"dead_doubleacute", "˝"}, {"dead_caron", "ˇ"}, {"dead_cedilla", "¸"},
        {"dead_ogonek", "˛"}, {"dead_iota", "ͺ"}, {"dead_stroke", "/"}, {"dead_currency", "¤"},
    };
    char name[64];
    if (xkb_keysym_get_name(sym, name, sizeof name) <= 0)
        return {};
    return accents.value(QString::fromLatin1(name));
}

static QString character(xkb_keymap *keymap, xkb_keycode_t code, xkb_level_index_t level)
{
    const xkb_keysym_t *syms = nullptr;
    if (xkb_keymap_key_get_syms_by_level(keymap, code, 0, level, &syms) != 1)
        return {};
    const uint32_t ch = xkb_keysym_to_utf32(syms[0]);
    if (ch >= 0x20 && ch != 0x7f)
        return QString::fromUcs4(reinterpret_cast<const char32_t *>(&ch), 1);
    return deadKey(syms[0]);
}

// "de(nodeadkeys)" into "de" and "nodeadkeys".
static bool split(const QString &layout, QByteArray *name, QByteArray *variant)
{
    static const QRegularExpression pattern("^([a-z0-9_-]+)(?:\\(([a-zA-Z0-9_-]+)\\))?$");
    const QRegularExpressionMatch match = pattern.match(layout.trimmed());
    if (!match.hasMatch())
        return false;
    *name = match.captured(1).toUtf8();
    *variant = match.captured(2).toUtf8();
    return true;
}

QVariantMap read(const QString &layout)
{
    QByteArray name, variant;
    if (!split(layout, &name, &variant))
        return {};
    xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!context)
        return {};
    const xkb_rule_names names{"evdev", "pc105", name.constData(), variant.constData(), nullptr};
    xkb_keymap *keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    QVariantMap keys;
    if (keymap) {
        // Some names are aliases of others (AC12 is BKSL): each key once.
        QList<xkb_keycode_t> seen;
        for (const QStringList &row : rows()) {
            for (const QString &key : row) {
                const xkb_keycode_t code = xkb_keymap_key_by_name(keymap, key.toLatin1().constData());
                if (code == XKB_KEYCODE_INVALID || seen.contains(code))
                    continue;
                seen << code;
                const QString base = character(keymap, code, 0);
                if (base.isEmpty())
                    continue;
                keys.insert(key, QStringList{base, character(keymap, code, 1)});
            }
        }
        xkb_keymap_unref(keymap);
    }
    xkb_context_unref(context);
    if (keys.isEmpty())
        return {};
    const QString description = describe(layout);
    return {{"name", description.isEmpty() ? layout : description}, {"keys", keys}};
}

QString describe(const QString &layout)
{
    QByteArray name, variant;
    if (!split(layout, &name, &variant))
        return {};
    QFile list("/usr/share/X11/xkb/rules/evdev.lst");
    if (!list.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    // "! layout" lines read "  de   German"; "! variant" lines read
    // "  nodeadkeys   de: German (no dead keys)".
    QString section;
    while (!list.atEnd()) {
        const QString line = QString::fromUtf8(list.readLine()).trimmed();
        if (line.startsWith('!')) {
            section = line.mid(1).trimmed();
            continue;
        }
        const qsizetype gap = line.indexOf(QRegularExpression("\\s"));
        if (gap < 0)
            continue;
        const QString id = line.left(gap), rest = line.mid(gap).trimmed();
        if (variant.isEmpty() && section == "layout" && id == QString::fromUtf8(name))
            return rest;
        if (!variant.isEmpty() && section == "variant" && id == QString::fromUtf8(variant)
            && rest.startsWith(QString::fromUtf8(name) + ':'))
            return rest.mid(name.size() + 1).trimmed();
    }
    return {};
}

}
