#pragma once

#include <QObject>

#include "keymap.h"

struct wl_display;
struct wl_event_queue;
struct wl_registry;
struct wl_seat;
struct zwp_virtual_keyboard_manager_v1;
struct zwp_virtual_keyboard_v1;

// Types into the focused window through the compositor's virtual keyboard
// protocol (zwp_virtual_keyboard_v1), on Qt's own Wayland connection. The
// on-screen keys never take keyboard focus, so whatever the user was typing
// into keeps it.
class VirtualKeyboard : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)
    Q_PROPERTY(QString error READ error NOTIFY readyChanged)

public:
    // Modifier flags QML passes with each key.
    enum Modifier { Ctrl = 1, Alt = 2, Super = 4, Shift = 8 };
    Q_ENUM(Modifier)

    explicit VirtualKeyboard(const QStringList &systemLayouts = {}, QObject *parent = nullptr);
    ~VirtualKeyboard() override;

    bool connectToCompositor();
    bool isReady() const { return m_keyboard != nullptr; }
    QString error() const { return m_error; }

    // Types text as it reads; Shift in `modifiers` is ignored because the text
    // already says which case it wants.
    Q_INVOKABLE void typeText(const QString &text, int modifiers = 0);
    // Taps a named key (Return, BackSpace, Tab, Escape, Left, Right, Up, Down,
    // Home, End, Delete, PageUp, PageDown). Returns false for an unknown name.
    Q_INVOKABLE bool tapKey(const QString &name, int modifiers = 0);
    // Puts these characters in the keymap up front, so typing them never has
    // to wait for a new one.
    Q_INVOKABLE void preload(const QString &characters);

    static uint32_t keycodeFor(const QString &name);

signals:
    void readyChanged();

private:
    static void registryGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface,
                               uint32_t version);
    static void registryGlobalRemove(void *data, wl_registry *registry, uint32_t name);

    void fail(const QString &message);
    bool uploadKeymap();
    void renewKeyboard();
    void drain();
    void send(uint32_t key, uint32_t mods);
    uint32_t xkbModifiers(int modifiers) const;

    // A character, or else a key, with the modifiers to send it with.
    struct Pending {
        uint codepoint = 0;
        uint32_t key = 0;
        uint32_t mods = 0;
    };
    static constexpr int SettleMs = 40;

    Keymap m_keymap;
    QList<Pending> m_pending;
    bool m_settling = false;
    QString m_error;
    wl_display *m_display = nullptr;
    wl_event_queue *m_queue = nullptr;
    wl_registry *m_registry = nullptr;
    wl_seat *m_seat = nullptr;
    zwp_virtual_keyboard_manager_v1 *m_manager = nullptr;
    zwp_virtual_keyboard_v1 *m_keyboard = nullptr;
};
