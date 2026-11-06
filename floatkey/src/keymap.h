#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <cstdint>

struct xkb_context;
struct xkb_keymap;

// The XKB keymap floatkey types with. The user's own layout carries what its
// keys can, so shortcuts land on the keys applications and the compositor
// expect: the first two levels, then AltGr and AltGr with shift for what
// those leave out (@ on a German layout is AltGr+Q, as on a real keyboard).
// Keycodes that the user's own
// keymap leaves empty carry two more characters each, plain and with shift
// (umlauts, accents, emoji): they are handed out on demand, and when they run
// out the one used longest ago is reused, so any character can be typed
// whatever the layout on screen.
class Keymap {
public:
    struct Stroke {
        uint32_t key = 0;   // evdev keycode, as zwp_virtual_keyboard_v1.key takes it
        bool shift = false;
        uint32_t mods = 0;  // any other modifiers its level needs (AltGr's), as a mask
    };

    // systemLayouts are the compositor's own layouts (Hyprland's
    // input:kb_layout), whose keys the spare keycodes must stay clear of.
    explicit Keymap(const QStringList &systemLayouts = {});
    ~Keymap();
    Keymap(const Keymap &) = delete;
    Keymap &operator=(const Keymap &) = delete;

    bool isValid() const { return m_keymap != nullptr; }
    QString error() const { return m_error; }

    // Makes every code point typeable. Returns true when that changed the
    // keymap, which then has to reach the compositor before the keystrokes.
    bool reserve(const QVector<uint> &codepoints);
    bool stroke(uint codepoint, Stroke *stroke);

    // The compiled keymap, NUL-terminated, the way wl_keyboard expects it.
    QByteArray text() const { return m_text; }

    uint32_t shiftMask() const { return m_shift; }
    uint32_t ctrlMask() const { return m_ctrl; }
    uint32_t altMask() const { return m_alt; }
    uint32_t superMask() const { return m_super; }

    // How many characters beyond US QWERTY fit at once.
    int capacity() const;

private:
    // One level of a spare keycode.
    struct Slot {
        uint32_t xkbKeycode = 0;
        QByteArray name;
        int level = 0;
        uint codepoint = 0;
        quint64 used = 0;
    };

    bool compile();

    xkb_context *m_context = nullptr;
    xkb_keymap *m_keymap = nullptr;
    QString m_error;
    QByteArray m_baseLayout = "us";
    QByteArray m_text;
    QHash<uint, Stroke> m_base;
    QVector<Slot> m_slots;
    QHash<uint, int> m_slotOf;
    quint64 m_clock = 0;
    uint32_t m_shift = 0, m_ctrl = 0, m_alt = 0, m_super = 0;
};
