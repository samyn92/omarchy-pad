#include "keymap.h"

#include <QRegularExpression>
#include <QSet>
#include <cstdlib>
#include <cstring>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>

// Keycodes that carry nothing in the user's own keymap. The compositor
// matches its key bindings against that keymap, not ours, so a character
// parked on, say, the calculator key would open the calculator instead of
// being typed. Those at or below 255 come first, because X11 apps under
// Xwayland can't receive anything higher.
static QVector<QPair<uint32_t, QByteArray>> freeKeycodes(xkb_context *context, const QStringList &systemLayouts)
{
    QStringList layouts;
    for (const QString &layout : systemLayouts + QStringList{"us"})
        if (!layout.trimmed().isEmpty() && !layouts.contains(layout.trimmed()) && layouts.size() < 4)
            layouts << layout.trimmed();
    const QByteArray layoutNames = layouts.join(',').toUtf8();
    xkb_rule_names names{"evdev", "pc105", layoutNames.constData(), nullptr, nullptr};
    xkb_keymap *system = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!system) {
        names.layout = "us";
        system = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    }
    QVector<QPair<uint32_t, QByteArray>> low, high;
    if (!system)
        return low;
    const xkb_keycode_t min = xkb_keymap_min_keycode(system), max = xkb_keymap_max_keycode(system);
    for (xkb_keycode_t code = qMax<xkb_keycode_t>(min, 9); code <= max; ++code) {
        const char *name = xkb_keymap_key_get_name(system, code);
        // KEY_UNKNOWN is dropped by some input stacks.
        if (!name || code == KEY_UNKNOWN + 8)
            continue;
        bool empty = true;
        for (xkb_layout_index_t group = 0; empty && group < xkb_keymap_num_layouts_for_key(system, code); ++group)
            for (xkb_level_index_t level = 0; empty && level < xkb_keymap_num_levels_for_key(system, code, group); ++level) {
                const xkb_keysym_t *syms = nullptr;
                empty = xkb_keymap_key_get_syms_by_level(system, code, group, level, &syms) == 0;
            }
        if (empty)
            (code <= 255 ? low : high).append({code, QByteArray(name)});
    }
    xkb_keymap_unref(system);
    // Past the end of the user's keymap, up to the highest evdev key, nothing
    // can be bound at all; floatkey names those keycodes itself.
    for (xkb_keycode_t code = max + 1; code <= KEY_MAX + 8; ++code)
        high.append({code, "Z" + QByteArray::number(code)});
    return low + high;
}

Keymap::Keymap(const QStringList &systemLayouts)
{
    m_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!m_context) {
        m_error = "Could not create an XKB context";
        return;
    }
    for (const auto &[code, name] : freeKeycodes(m_context, systemLayouts))
        for (int level = 0; level < 2; ++level)
            m_slots.append(Slot{code, name, level, 0, 0});
    // The base is the user's own first layout, so a keycode means the same
    // to apps as to the compositor's key bindings (super+z stays super+z on
    // QWERTZ).
    static const QRegularExpression valid("^[a-z]{2,8}$");
    const QString first = systemLayouts.value(0).trimmed();
    if (valid.match(first).hasMatch())
        m_baseLayout = first.toUtf8();
    if (!compile() && m_baseLayout != "us") {
        m_baseLayout = "us";
        compile();
    }
}

int Keymap::capacity() const
{
    return int(m_slots.size());
}

Keymap::~Keymap()
{
    if (m_keymap)
        xkb_keymap_unref(m_keymap);
    if (m_context)
        xkb_context_unref(m_context);
}

bool Keymap::reserve(const QVector<uint> &codepoints)
{
    if (!m_context)
        return false;
    const QSet<uint> wanted(codepoints.begin(), codepoints.end());
    bool changed = false;
    for (uint codepoint : codepoints) {
        if (m_base.contains(codepoint))
            continue;
        if (m_slotOf.contains(codepoint)) {
            m_slots[m_slotOf.value(codepoint)].used = ++m_clock;
            continue;
        }
        if (xkb_utf32_to_keysym(codepoint) == XKB_KEY_NoSymbol)
            continue;

        // A free slot, or else the one used longest ago that this call
        // doesn't need itself.
        int pick = -1;
        for (int i = 0; i < m_slots.size(); ++i) {
            const Slot &slot = m_slots.at(i);
            if (slot.codepoint == 0) {
                pick = i;
                break;
            }
            if (wanted.contains(slot.codepoint))
                continue;
            if (pick < 0 || slot.used < m_slots.at(pick).used)
                pick = i;
        }
        if (pick < 0)
            continue;

        Slot &slot = m_slots[pick];
        if (slot.codepoint)
            m_slotOf.remove(slot.codepoint);
        slot.codepoint = codepoint;
        slot.used = ++m_clock;
        m_slotOf.insert(codepoint, pick);
        changed = true;
    }
    if (changed)
        compile();
    return changed;
}

bool Keymap::stroke(uint codepoint, Stroke *stroke)
{
    const auto base = m_base.constFind(codepoint);
    if (base != m_base.constEnd()) {
        *stroke = base.value();
        return true;
    }
    const auto slot = m_slotOf.constFind(codepoint);
    if (slot == m_slotOf.constEnd())
        return false;
    Slot &used = m_slots[slot.value()];
    used.used = ++m_clock;
    *stroke = Stroke{used.xkbKeycode - 8, used.level == 1};
    return true;
}

bool Keymap::compile()
{
    QByteArray keycodes = "  xkb_keycodes {\n    include \"evdev+aliases(qwerty)\"\n";
    for (int i = 0; i + 1 < m_slots.size(); i += 2)
        if (m_slots.at(i).name.startsWith('Z'))
            keycodes += "    <" + m_slots.at(i).name + "> = " + QByteArray::number(m_slots.at(i).xkbKeycode) + ";\n";
    keycodes += "  };\n";
    QByteArray source =
        "xkb_keymap {\n" + keycodes +
        "  xkb_types { include \"complete\" };\n"
        "  xkb_compat { include \"complete\" };\n"
        "  xkb_symbols {\n"
        "    include \"pc+" + m_baseLayout + "\"\n";
    auto keysymName = [](uint codepoint) -> QByteArray {
        char name[64];
        if (!codepoint || xkb_keysym_get_name(xkb_utf32_to_keysym(codepoint), name, sizeof name) <= 0)
            return "NoSymbol";
        return name;
    };
    for (int i = 0; i + 1 < m_slots.size(); i += 2) {
        const Slot &plain = m_slots.at(i), &shifted = m_slots.at(i + 1);
        if (!plain.codepoint && !shifted.codepoint)
            continue;
        source += "    replace key <" + plain.name + "> { type = \"TWO_LEVEL\", "
                + "symbols[Group1] = [ " + keysymName(plain.codepoint) + ", " + keysymName(shifted.codepoint)
                + " ] };\n";
    }
    source += "  };\n};\n";

    xkb_keymap *keymap = xkb_keymap_new_from_string(m_context, source.constData(), XKB_KEYMAP_FORMAT_TEXT_V1,
                                                    XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
        m_error = "Could not compile the keymap (are the XKB data files installed?)";
        return false;
    }
    char *text = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    if (!text) {
        xkb_keymap_unref(keymap);
        m_error = "Could not serialize the keymap";
        return false;
    }
    m_text = QByteArray(text, int(std::strlen(text)) + 1);
    std::free(text);

    if (m_keymap)
        xkb_keymap_unref(m_keymap);
    m_keymap = keymap;
    m_error.clear();

    auto mask = [keymap](const char *name) {
        const xkb_mod_index_t index = xkb_keymap_mod_get_index(keymap, name);
        return index == XKB_MOD_INVALID ? 0u : 1u << index;
    };
    m_shift = mask(XKB_MOD_NAME_SHIFT);
    m_ctrl = mask(XKB_MOD_NAME_CTRL);
    m_alt = mask(XKB_MOD_NAME_ALT);
    m_super = mask(XKB_MOD_NAME_LOGO);

    // The base layout never changes, so read it once: every character the
    // first two levels of a key produce, lower keycodes first so the main
    // block wins over the keypad; then, for what those leave out, the AltGr
    // levels. Those go out with AltGr held on the real key rather than on a
    // spare keycode, because Chromium and Electron apps drop keycodes that
    // aren't in their own table, and most spare ones aren't: @ never reached
    // the Claude desktop app's email field on a German layout (6 Nov 2026).
    if (m_base.isEmpty()) {
        QSet<uint32_t> slotCodes;
        for (const Slot &slot : std::as_const(m_slots))
            slotCodes.insert(slot.xkbKeycode);
        // A level is usable when nothing but shift and the level's own
        // modifiers (AltGr, behind Mod5 or similar) selects it: never caps
        // or num lock, and nothing a shortcut uses.
        const xkb_mod_mask_t avoid = m_ctrl | m_alt | m_super | mask(XKB_MOD_NAME_CAPS) | mask(XKB_MOD_NAME_NUM);
        const xkb_keycode_t min = xkb_keymap_min_keycode(keymap), max = xkb_keymap_max_keycode(keymap);
        for (xkb_level_index_t first : {0u, 2u}) {
            for (xkb_keycode_t code = min; code <= max; ++code) {
                if (slotCodes.contains(code))
                    continue;
                const xkb_level_index_t levels = qMin<xkb_level_index_t>(first + 2, xkb_keymap_num_levels_for_key(keymap, code, 0));
                for (xkb_level_index_t level = first; level < levels; ++level) {
                    const xkb_keysym_t *syms = nullptr;
                    if (xkb_keymap_key_get_syms_by_level(keymap, code, 0, level, &syms) != 1)
                        continue;
                    const uint codepoint = xkb_keysym_to_utf32(syms[0]);
                    if (codepoint < 0x20 || codepoint == 0x7f || m_base.contains(codepoint))
                        continue;
                    xkb_mod_mask_t masks[8];
                    const size_t count = xkb_keymap_key_get_mods_for_level(keymap, code, 0, level, masks, 8);
                    for (size_t i = 0; i < count; ++i) {
                        if (masks[i] & avoid)
                            continue;
                        // Levels 1 and 2 only as before: plain or shift alone.
                        if (first == 0 && masks[i] != 0 && masks[i] != m_shift)
                            continue;
                        if (first == 2 && (masks[i] & ~m_shift) == 0)
                            continue;
                        m_base.insert(codepoint, Stroke{code - 8, (masks[i] & m_shift) != 0, masks[i] & ~m_shift});
                        break;
                    }
                }
            }
        }
    }
    return true;
}
