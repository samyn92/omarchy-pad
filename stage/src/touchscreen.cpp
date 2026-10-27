#include "touchscreen.h"

#include <QLoggingCategory>
#include <QSocketNotifier>
#include <cerrno>
#include <fcntl.h>
#include <libinput.h>
#include <libudev.h>
#include <unistd.h>

Q_DECLARE_LOGGING_CATEGORY(lcStage)

namespace {

int openRestricted(const char *path, int flags, void *)
{
    const int fd = ::open(path, flags | O_CLOEXEC);
    return fd < 0 ? -errno : fd;
}

void closeRestricted(int fd, void *)
{
    ::close(fd);
}

const libinput_interface Interface{openRestricted, closeRestricted};

// libinput hands out positions scaled to a size of our choosing; this one
// keeps a fraction's precision well below a panel pixel.
constexpr uint32_t Scale = 1u << 20;

} // namespace

Touchscreen::Touchscreen(QObject *parent) : QObject(parent) {}

Touchscreen::~Touchscreen()
{
    if (m_libinput)
        libinput_unref(m_libinput);
}

bool Touchscreen::open()
{
    m_libinput = libinput_path_create_context(&Interface, nullptr);
    if (!m_libinput)
        return false;

    int devices = 0;
    udev *u = udev_new();
    udev_enumerate *e = udev_enumerate_new(u);
    udev_enumerate_add_match_subsystem(e, "input");
    udev_enumerate_add_match_property(e, "ID_INPUT_TOUCHSCREEN", "1");
    udev_enumerate_scan_devices(e);
    udev_list_entry *entry;
    udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(e)) {
        udev_device *d = udev_device_new_from_syspath(u, udev_list_entry_get_name(entry));
        const char *node = d ? udev_device_get_devnode(d) : nullptr;
        if (node && QByteArray(udev_device_get_sysname(d)).startsWith("event")) {
            if (libinput_path_add_device(m_libinput, node)) {
                qCDebug(lcStage) << "touch screen" << node;
                ++devices;
            } else {
                qWarning("stage: can't read the touch screen %s; is the user in the input group?", node);
            }
        }
        if (d)
            udev_device_unref(d);
    }
    udev_enumerate_unref(e);
    udev_unref(u);

    if (!devices) {
        libinput_unref(m_libinput);
        m_libinput = nullptr;
        return false;
    }
    m_notifier = new QSocketNotifier(libinput_get_fd(m_libinput), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &Touchscreen::read);
    read();
    return true;
}

void Touchscreen::read()
{
    libinput_dispatch(m_libinput);
    while (libinput_event *event = libinput_get_event(m_libinput)) {
        const libinput_event_type type = libinput_event_get_type(event);
        if (type == LIBINPUT_EVENT_TOUCH_DOWN || type == LIBINPUT_EVENT_TOUCH_MOTION || type == LIBINPUT_EVENT_TOUCH_UP
            || type == LIBINPUT_EVENT_TOUCH_CANCEL) {
            libinput_event_touch *touch = libinput_event_get_touch_event(event);
            const int slot = libinput_event_touch_get_seat_slot(touch);
            const qreal ms = libinput_event_touch_get_time_usec(touch) / 1000.0;
            if (type == LIBINPUT_EVENT_TOUCH_UP || type == LIBINPUT_EVENT_TOUCH_CANCEL) {
                emit touched(Up, slot, {}, ms);
            } else {
                const QPointF panel(libinput_event_touch_get_x_transformed(touch, Scale) / Scale,
                                    libinput_event_touch_get_y_transformed(touch, Scale) / Scale);
                emit touched(type == LIBINPUT_EVENT_TOUCH_DOWN ? Down : Move, slot, panel, ms);
            }
        }
        libinput_event_destroy(event);
    }
}
