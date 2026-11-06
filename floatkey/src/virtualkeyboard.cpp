#include "virtualkeyboard.h"

#include <QGuiApplication>
#include <QHash>
#include <QTimer>
#include <chrono>
#include <cstring>
#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "virtual-keyboard-unstable-v1-client-protocol.h"

VirtualKeyboard::VirtualKeyboard(const QStringList &systemLayouts, QObject *parent)
    : QObject(parent), m_keymap(systemLayouts)
{
}

VirtualKeyboard::~VirtualKeyboard()
{
    if (m_keyboard)
        zwp_virtual_keyboard_v1_destroy(m_keyboard);
    if (m_manager)
        zwp_virtual_keyboard_manager_v1_destroy(m_manager);
    if (m_registry)
        wl_registry_destroy(m_registry);
    if (m_display)
        wl_display_flush(m_display);
    if (m_queue)
        wl_event_queue_destroy(m_queue);
}

void VirtualKeyboard::fail(const QString &message)
{
    m_error = message;
    qWarning("floatkey: %s", qPrintable(message));
    emit readyChanged();
}

bool VirtualKeyboard::connectToCompositor()
{
    if (!m_keymap.isValid()) {
        fail(m_keymap.error());
        return false;
    }
    auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wayland || !wayland->display() || !wayland->seat()) {
        fail(tr("floatkey needs a Wayland session"));
        return false;
    }
    m_display = wayland->display();

    // Our own queue keeps the registry roundtrip from dispatching Qt's events
    // out from under it.
    m_queue = wl_display_create_queue(m_display);
    auto *display = static_cast<wl_display *>(wl_proxy_create_wrapper(m_display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(display), m_queue);
    m_registry = wl_display_get_registry(display);
    wl_proxy_wrapper_destroy(display);
    static const wl_registry_listener listener = {&registryGlobal, &registryGlobalRemove};
    wl_registry_add_listener(m_registry, &listener, this);
    wl_display_roundtrip_queue(m_display, m_queue);

    if (!m_manager) {
        fail(tr("The compositor doesn't offer a virtual keyboard (zwp_virtual_keyboard_manager_v1)"));
        return false;
    }
    m_seat = wayland->seat();
    m_keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(m_manager, m_seat);
    if (!uploadKeymap()) {
        zwp_virtual_keyboard_v1_destroy(m_keyboard);
        m_keyboard = nullptr;
        fail(tr("Could not hand the keymap to the compositor"));
        return false;
    }
    wl_display_flush(m_display);
    m_error.clear();
    emit readyChanged();
    return true;
}

void VirtualKeyboard::registryGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface,
                                     uint32_t)
{
    auto *self = static_cast<VirtualKeyboard *>(data);
    if (std::strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0 && !self->m_manager)
        self->m_manager = static_cast<zwp_virtual_keyboard_manager_v1 *>(
            wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1));
}

void VirtualKeyboard::registryGlobalRemove(void *, wl_registry *, uint32_t) {}

bool VirtualKeyboard::uploadKeymap()
{
    const QByteArray text = m_keymap.text();
    const int fd = memfd_create("floatkey-keymap", MFD_CLOEXEC);
    if (fd < 0)
        return false;
    qsizetype written = 0;
    while (written < text.size()) {
        const ssize_t n = ::write(fd, text.constData() + written, size_t(text.size() - written));
        if (n <= 0) {
            ::close(fd);
            return false;
        }
        written += n;
    }
    // libwayland duplicates the descriptor when it queues the request.
    zwp_virtual_keyboard_v1_keymap(m_keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, uint32_t(text.size()));
    ::close(fd);
    return true;
}

// Compositors (and input methods holding a keyboard grab) pass a virtual
// keyboard's keymap on when that keyboard becomes the active one, not when it
// changes later. A new keymap therefore comes with a new virtual keyboard, and
// keystrokes wait a moment while it travels on to the focused app, through
// the input method when one is running.
void VirtualKeyboard::renewKeyboard()
{
    zwp_virtual_keyboard_v1_destroy(m_keyboard);
    m_keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(m_manager, m_seat);
    uploadKeymap();
    wl_display_flush(m_display);
    m_settling = true;
    QTimer::singleShot(SettleMs, this, [this] {
        m_settling = false;
        drain();
    });
}

void VirtualKeyboard::preload(const QString &characters)
{
    QVector<uint> codepoints;
    for (uint codepoint : characters.toUcs4())
        codepoints << codepoint;
    if (m_keymap.reserve(codepoints) && m_keyboard)
        renewKeyboard();
}

static uint32_t timestamp()
{
    using namespace std::chrono;
    return uint32_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void VirtualKeyboard::send(uint32_t key, uint32_t mods)
{
    const uint32_t time = timestamp();
    if (mods)
        zwp_virtual_keyboard_v1_modifiers(m_keyboard, mods, 0, 0, 0);
    zwp_virtual_keyboard_v1_key(m_keyboard, time, key, WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(m_keyboard, time, key, WL_KEYBOARD_KEY_STATE_RELEASED);
    if (mods)
        zwp_virtual_keyboard_v1_modifiers(m_keyboard, 0, 0, 0, 0);
}

uint32_t VirtualKeyboard::xkbModifiers(int modifiers) const
{
    uint32_t mask = 0;
    if (modifiers & Ctrl)
        mask |= m_keymap.ctrlMask();
    if (modifiers & Alt)
        mask |= m_keymap.altMask();
    if (modifiers & Super)
        mask |= m_keymap.superMask();
    if (modifiers & Shift)
        mask |= m_keymap.shiftMask();
    return mask;
}

void VirtualKeyboard::typeText(const QString &text, int modifiers)
{
    if (!m_keyboard)
        return;
    const uint32_t mods = xkbModifiers(modifiers & ~Shift);
    for (uint codepoint : text.toUcs4()) {
        if (codepoint == '\n')
            m_pending.append(Pending{0, KEY_ENTER, mods});
        else if (codepoint == '\t')
            m_pending.append(Pending{0, KEY_TAB, mods});
        else
            m_pending.append(Pending{codepoint, 0, mods});
    }
    drain();
}

// Sends queued keystrokes in order, stopping to renew the keyboard when a
// character needs a keymap change.
void VirtualKeyboard::drain()
{
    if (!m_keyboard)
        return;
    while (!m_settling && !m_pending.isEmpty()) {
        const Pending next = m_pending.first();
        if (next.codepoint) {
            if (m_keymap.reserve({next.codepoint})) {
                renewKeyboard();
                break;
            }
            Keymap::Stroke stroke;
            if (m_keymap.stroke(next.codepoint, &stroke))
                send(stroke.key, next.mods | stroke.mods | (stroke.shift ? m_keymap.shiftMask() : 0));
        } else {
            send(next.key, next.mods);
        }
        m_pending.removeFirst();
    }
    wl_display_flush(m_display);
}

uint32_t VirtualKeyboard::keycodeFor(const QString &name)
{
    static const QHash<QString, uint32_t> keys = {
        {"Return", KEY_ENTER},   {"BackSpace", KEY_BACKSPACE}, {"Tab", KEY_TAB},
        {"Escape", KEY_ESC},     {"Left", KEY_LEFT},           {"Right", KEY_RIGHT},
        {"Up", KEY_UP},          {"Down", KEY_DOWN},           {"Home", KEY_HOME},
        {"End", KEY_END},        {"Delete", KEY_DELETE},       {"PageUp", KEY_PAGEUP},
        {"PageDown", KEY_PAGEDOWN}, {"space", KEY_SPACE},
    };
    return keys.value(name, 0);
}

bool VirtualKeyboard::tapKey(const QString &name, int modifiers)
{
    const uint32_t key = keycodeFor(name);
    if (!key)
        return false;
    if (!m_keyboard)
        return true;
    m_pending.append(Pending{0, key, xkbModifiers(modifiers)});
    drain();
    return true;
}
