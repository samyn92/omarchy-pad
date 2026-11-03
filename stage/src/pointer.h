#pragma once

#include <QPointF>
#include <QRectF>

struct wl_display;
struct wl_event_queue;
struct wl_registry;
struct zwlr_virtual_pointer_manager_v1;
struct zwlr_virtual_pointer_v1;

// A pointer of stage's own (wlr-virtual-pointer, which Hyprland offers), for
// what a finger cannot do on its own: the right click of a long press. It
// moves to a point on the screen and clicks there, as a mouse would.
class Pointer {
public:
    Pointer() = default;
    ~Pointer();
    Pointer(const Pointer &) = delete;
    Pointer &operator=(const Pointer &) = delete;

    // False when the compositor has no virtual pointers.
    bool connectToCompositor();
    bool isAvailable() const { return m_pointer; }

    // A right click at `at`, in global logical coordinates, with `layout` the
    // area of every output together (absolute motion is a fraction of it).
    bool rightClick(const QPointF &at, const QRectF &layout);

private:
    static void registryGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface,
                               uint32_t version);
    static void registryGlobalRemove(void *, wl_registry *, uint32_t) {}

    wl_display *m_display = nullptr;
    wl_event_queue *m_queue = nullptr;
    wl_registry *m_registry = nullptr;
    zwlr_virtual_pointer_manager_v1 *m_manager = nullptr;
    zwlr_virtual_pointer_v1 *m_pointer = nullptr;
};
