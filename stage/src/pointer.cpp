#include "pointer.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <cmath>
#include <cstring>
#include <linux/input-event-codes.h>
#include <wayland-client.h>

#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

namespace {

// Absolute motion goes as a fraction of `extent` units; this keeps it well
// below a pixel on any screen.
constexpr uint32_t Extent = 1u << 16;

uint32_t now()
{
    static QElapsedTimer clock;
    if (!clock.isValid())
        clock.start();
    return uint32_t(clock.elapsed());
}

} // namespace

Pointer::~Pointer()
{
    if (m_pointer)
        zwlr_virtual_pointer_v1_destroy(m_pointer);
    if (m_manager)
        zwlr_virtual_pointer_manager_v1_destroy(m_manager);
    if (m_registry)
        wl_registry_destroy(m_registry);
    if (m_queue)
        wl_event_queue_destroy(m_queue);
}

bool Pointer::connectToCompositor()
{
    auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wayland || !wayland->display())
        return false;
    m_display = wayland->display();

    // Bound on a queue of our own, as Capture does, so the roundtrip doesn't
    // dispatch Qt's events from under it; requests need no queue after that.
    m_queue = wl_display_create_queue(m_display);
    auto *display = static_cast<wl_display *>(wl_proxy_create_wrapper(m_display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(display), m_queue);
    m_registry = wl_display_get_registry(display);
    wl_proxy_wrapper_destroy(display);
    static const wl_registry_listener listener = {&registryGlobal, &registryGlobalRemove};
    wl_registry_add_listener(m_registry, &listener, this);
    wl_display_roundtrip_queue(m_display, m_queue);

    if (m_manager)
        m_pointer = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(m_manager, nullptr);
    return m_pointer;
}

void Pointer::registryGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t)
{
    auto *self = static_cast<Pointer *>(data);
    if (std::strcmp(interface, zwlr_virtual_pointer_manager_v1_interface.name) == 0 && !self->m_manager)
        self->m_manager = static_cast<zwlr_virtual_pointer_manager_v1 *>(
            wl_registry_bind(registry, name, &zwlr_virtual_pointer_manager_v1_interface, 1));
}

bool Pointer::rightClick(const QPointF &at, const QRectF &layout)
{
    if (!m_pointer || layout.isEmpty())
        return false;
    const double fx = (at.x() - layout.left()) / layout.width(), fy = (at.y() - layout.top()) / layout.height();
    if (fx < 0 || fx > 1 || fy < 0 || fy > 1)
        return false;
    const uint32_t t = now();
    zwlr_virtual_pointer_v1_motion_absolute(m_pointer, t, uint32_t(std::lround(fx * Extent)),
                                            uint32_t(std::lround(fy * Extent)), Extent, Extent);
    zwlr_virtual_pointer_v1_frame(m_pointer);
    zwlr_virtual_pointer_v1_button(m_pointer, t, BTN_RIGHT, WL_POINTER_BUTTON_STATE_PRESSED);
    zwlr_virtual_pointer_v1_frame(m_pointer);
    zwlr_virtual_pointer_v1_button(m_pointer, t + 1, BTN_RIGHT, WL_POINTER_BUTTON_STATE_RELEASED);
    zwlr_virtual_pointer_v1_frame(m_pointer);
    wl_display_flush(m_display);
    return true;
}
