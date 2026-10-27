#include "capture.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QScreen>
#include <QTransform>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "ext-foreign-toplevel-list-v1-client-protocol.h"
#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"
#include "hyprland-toplevel-mapping-v1-client-protocol.h"

// Defined in stage.cpp.
Q_DECLARE_LOGGING_CATEGORY(lcStage)

// The mapping protocol can also map wlroots' toplevels, which stage never
// asks for; the generated glue still names their interface, so it gets one.
extern "C" const wl_interface zwlr_foreign_toplevel_handle_v1_interface = {
    "zwlr_foreign_toplevel_handle_v1", 3, 0, nullptr, 0, nullptr,
};

struct Capture::Job {
    Capture *owner = nullptr;
    int id = 0;
    ext_image_capture_source_v1 *source = nullptr;
    ext_image_copy_capture_session_v1 *session = nullptr;
    ext_image_copy_capture_frame_v1 *frame = nullptr;
    wl_shm_pool *pool = nullptr;
    wl_buffer *buffer = nullptr;
    int fd = -1;
    void *data = MAP_FAILED;
    size_t size = 0;
    uint32_t width = 0, height = 0, transform = WL_OUTPUT_TRANSFORM_NORMAL;
    int format = -1;
    bool finished = false;
    QElapsedTimer clock;
};

// The shm formats a capture can come in, best first, as QImage reads them.
static QImage::Format imageFormat(uint32_t format)
{
    switch (format) {
    case WL_SHM_FORMAT_XRGB8888: return QImage::Format_RGB32;
    case WL_SHM_FORMAT_ARGB8888: return QImage::Format_ARGB32_Premultiplied;
    case WL_SHM_FORMAT_XBGR8888: return QImage::Format_RGBX8888;
    case WL_SHM_FORMAT_ABGR8888: return QImage::Format_RGBA8888_Premultiplied;
    default: return QImage::Format_Invalid;
    }
}

static int preference(uint32_t format)
{
    switch (format) {
    case WL_SHM_FORMAT_XRGB8888: return 4;
    case WL_SHM_FORMAT_ARGB8888: return 3;
    case WL_SHM_FORMAT_XBGR8888: return 2;
    case WL_SHM_FORMAT_ABGR8888: return 1;
    default: return 0;
    }
}

Capture::Capture(QObject *parent) : QObject(parent) {}

Capture::~Capture()
{
    for (Job *job : std::as_const(m_jobs)) {
        if (job->frame)
            ext_image_copy_capture_frame_v1_destroy(job->frame);
        if (job->session)
            ext_image_copy_capture_session_v1_destroy(job->session);
        if (job->source)
            ext_image_capture_source_v1_destroy(job->source);
        if (job->buffer)
            wl_buffer_destroy(job->buffer);
        if (job->pool)
            wl_shm_pool_destroy(job->pool);
        if (job->data != MAP_FAILED)
            munmap(job->data, job->size);
        if (job->fd >= 0)
            close(job->fd);
        delete job;
    }
    m_jobs.clear();
    for (auto it = m_toplevels.begin(); it != m_toplevels.end(); ++it) {
        if (it->mapping)
            hyprland_toplevel_window_mapping_handle_v1_destroy(it->mapping);
        ext_foreign_toplevel_handle_v1_destroy(it.key());
    }
    if (m_mapping)
        hyprland_toplevel_mapping_manager_v1_destroy(m_mapping);
    if (m_list)
        ext_foreign_toplevel_list_v1_destroy(m_list);
    if (m_toplevelSources)
        ext_foreign_toplevel_image_capture_source_manager_v1_destroy(m_toplevelSources);
    if (m_outputs)
        ext_output_image_capture_source_manager_v1_destroy(m_outputs);
    if (m_copy)
        ext_image_copy_capture_manager_v1_destroy(m_copy);
    if (m_shm)
        wl_shm_destroy(m_shm);
    if (m_registry)
        wl_registry_destroy(m_registry);
    if (m_queue)
        wl_event_queue_destroy(m_queue);
}

void Capture::registryGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t)
{
    auto *self = static_cast<Capture *>(data);
    auto is = [interface](const wl_interface &i) { return std::strcmp(interface, i.name) == 0; };
    if (is(wl_shm_interface) && !self->m_shm)
        self->m_shm = static_cast<wl_shm *>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    else if (is(ext_image_copy_capture_manager_v1_interface) && !self->m_copy)
        self->m_copy = static_cast<ext_image_copy_capture_manager_v1 *>(
            wl_registry_bind(registry, name, &ext_image_copy_capture_manager_v1_interface, 1));
    else if (is(ext_output_image_capture_source_manager_v1_interface) && !self->m_outputs)
        self->m_outputs = static_cast<ext_output_image_capture_source_manager_v1 *>(
            wl_registry_bind(registry, name, &ext_output_image_capture_source_manager_v1_interface, 1));
    else if (is(ext_foreign_toplevel_image_capture_source_manager_v1_interface) && !self->m_toplevelSources)
        self->m_toplevelSources = static_cast<ext_foreign_toplevel_image_capture_source_manager_v1 *>(
            wl_registry_bind(registry, name, &ext_foreign_toplevel_image_capture_source_manager_v1_interface, 1));
    else if (is(ext_foreign_toplevel_list_v1_interface) && !self->m_list) {
        self->m_list = static_cast<ext_foreign_toplevel_list_v1 *>(
            wl_registry_bind(registry, name, &ext_foreign_toplevel_list_v1_interface, 1));
        static const ext_foreign_toplevel_list_v1_listener listener = {&toplevelAdded, &listFinished};
        ext_foreign_toplevel_list_v1_add_listener(self->m_list, &listener, self);
    } else if (is(hyprland_toplevel_mapping_manager_v1_interface) && !self->m_mapping) {
        self->m_mapping = static_cast<hyprland_toplevel_mapping_manager_v1 *>(
            wl_registry_bind(registry, name, &hyprland_toplevel_mapping_manager_v1_interface, 1));
        // Toplevels listed before the manager was bound ask now.
        for (auto it = self->m_toplevels.cbegin(); it != self->m_toplevels.cend(); ++it)
            self->mapToplevel(it.key());
    }
}

void Capture::registryGlobalRemove(void *, wl_registry *, uint32_t) {}

void Capture::toplevelAdded(void *data, ext_foreign_toplevel_list_v1 *, ext_foreign_toplevel_handle_v1 *handle)
{
    auto *self = static_cast<Capture *>(data);
    self->m_toplevels.insert(handle, {});
    static const ext_foreign_toplevel_handle_v1_listener listener = {
        &handleClosed, &handleDone, &handleTitle, &handleAppId, &handleIdentifier,
    };
    ext_foreign_toplevel_handle_v1_add_listener(handle, &listener, self);
    self->mapToplevel(handle);
}

// Asks Hyprland which of its windows the toplevel is, once.
void Capture::mapToplevel(ext_foreign_toplevel_handle_v1 *handle)
{
    Toplevel &toplevel = m_toplevels[handle];
    if (!m_mapping || toplevel.mapping || !toplevel.address.isEmpty())
        return;
    toplevel.mapping = hyprland_toplevel_mapping_manager_v1_get_window_for_toplevel(m_mapping, handle);
    static const hyprland_toplevel_window_mapping_handle_v1_listener listener = {&mappingAddress, &mappingFailed};
    hyprland_toplevel_window_mapping_handle_v1_add_listener(toplevel.mapping, &listener, this);
}

void Capture::mappingAddress(void *data, hyprland_toplevel_window_mapping_handle_v1 *mapping, uint32_t hi,
                             uint32_t lo)
{
    auto *self = static_cast<Capture *>(data);
    for (Toplevel &toplevel : self->m_toplevels) {
        if (toplevel.mapping != mapping)
            continue;
        // The form Hyprland's clients JSON uses: 0x, then lowercase hex.
        toplevel.address = QStringLiteral("0x") + QString::number((quint64(hi) << 32) | lo, 16);
        toplevel.mapping = nullptr;
        break;
    }
    hyprland_toplevel_window_mapping_handle_v1_destroy(mapping);
}

void Capture::mappingFailed(void *data, hyprland_toplevel_window_mapping_handle_v1 *mapping)
{
    for (Toplevel &toplevel : static_cast<Capture *>(data)->m_toplevels) {
        if (toplevel.mapping == mapping) {
            toplevel.mapping = nullptr;
            break;
        }
    }
    hyprland_toplevel_window_mapping_handle_v1_destroy(mapping);
}

void Capture::listFinished(void *, ext_foreign_toplevel_list_v1 *) {}

void Capture::handleClosed(void *data, ext_foreign_toplevel_handle_v1 *handle)
{
    const Toplevel toplevel = static_cast<Capture *>(data)->m_toplevels.take(handle);
    if (toplevel.mapping)
        hyprland_toplevel_window_mapping_handle_v1_destroy(toplevel.mapping);
    ext_foreign_toplevel_handle_v1_destroy(handle);
}

void Capture::handleDone(void *, ext_foreign_toplevel_handle_v1 *) {}

void Capture::handleTitle(void *data, ext_foreign_toplevel_handle_v1 *handle, const char *title)
{
    static_cast<Capture *>(data)->m_toplevels[handle].title = QString::fromUtf8(title);
}

void Capture::handleAppId(void *data, ext_foreign_toplevel_handle_v1 *handle, const char *appId)
{
    static_cast<Capture *>(data)->m_toplevels[handle].appId = QString::fromUtf8(appId);
}

void Capture::handleIdentifier(void *, ext_foreign_toplevel_handle_v1 *, const char *) {}

bool Capture::connectToCompositor()
{
    auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wayland || !wayland->display())
        return false;
    m_display = wayland->display();

    // Globals are bound on a queue of our own, so the roundtrips don't
    // dispatch Qt's events from under it; afterwards everything moves to the
    // default queue, which Qt dispatches on the main thread.
    m_queue = wl_display_create_queue(m_display);
    auto *display = static_cast<wl_display *>(wl_proxy_create_wrapper(m_display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(display), m_queue);
    m_registry = wl_display_get_registry(display);
    wl_proxy_wrapper_destroy(display);
    static const wl_registry_listener listener = {&registryGlobal, &registryGlobalRemove};
    wl_registry_add_listener(m_registry, &listener, this);
    // Globals, then the toplevels, then their window addresses.
    wl_display_roundtrip_queue(m_display, m_queue);
    wl_display_roundtrip_queue(m_display, m_queue);
    wl_display_roundtrip_queue(m_display, m_queue);

    auto release = [](void *proxy) {
        if (proxy)
            wl_proxy_set_queue(static_cast<wl_proxy *>(proxy), nullptr);
    };
    release(m_shm);
    release(m_copy);
    release(m_outputs);
    release(m_toplevelSources);
    release(m_list);
    release(m_mapping);
    for (auto it = m_toplevels.begin(); it != m_toplevels.end(); ++it) {
        release(it.key());
        release(it->mapping);
    }
    return canCaptureScreens();
}

int Capture::captureScreen(QScreen *screen)
{
    if (!canCaptureScreens() || !screen)
        return -1;
    auto *native = screen->nativeInterface<QNativeInterface::QWaylandScreen>();
    if (!native || !native->output())
        return -1;
    return start(ext_output_image_capture_source_manager_v1_create_source(m_outputs, native->output()));
}

int Capture::captureWindow(const QString &address, const QString &appId, const QString &title)
{
    if (!canCaptureWindows())
        return -1;
    ext_foreign_toplevel_handle_v1 *match = nullptr;
    if (m_mapping && !address.isEmpty()) {
        // Only the exact window will do: a guess could show another's picture.
        for (auto it = m_toplevels.cbegin(); it != m_toplevels.cend(); ++it) {
            if (it->address == address) {
                match = it.key();
                break;
            }
        }
        if (!match)
            return -1;
        return start(ext_foreign_toplevel_image_capture_source_manager_v1_create_source(m_toplevelSources, match));
    }
    for (auto it = m_toplevels.cbegin(); it != m_toplevels.cend(); ++it) {
        if (it->appId != appId)
            continue;
        if (it->title == title) {
            match = it.key();
            break;
        }
        if (!match)
            match = it.key();
    }
    if (!match)
        return -1;
    return start(ext_foreign_toplevel_image_capture_source_manager_v1_create_source(m_toplevelSources, match));
}

int Capture::start(ext_image_capture_source_v1 *source)
{
    auto *job = new Job;
    job->owner = this;
    job->id = m_nextId++;
    job->source = source;
    job->clock.start();
    job->session = ext_image_copy_capture_manager_v1_create_session(m_copy, source, 0);
    m_jobs.insert(job->id, job);

    static const ext_image_copy_capture_session_v1_listener sessionListener = {
        // buffer_size
        [](void *data, ext_image_copy_capture_session_v1 *, uint32_t width, uint32_t height) {
            auto *job = static_cast<Job *>(data);
            job->width = width;
            job->height = height;
        },
        // shm_format
        [](void *data, ext_image_copy_capture_session_v1 *, uint32_t format) {
            auto *job = static_cast<Job *>(data);
            if (preference(format) > (job->format < 0 ? 0 : preference(uint32_t(job->format))))
                job->format = int(format);
        },
        // dmabuf_device, dmabuf_format
        [](void *, ext_image_copy_capture_session_v1 *, wl_array *) {},
        [](void *, ext_image_copy_capture_session_v1 *, uint32_t, wl_array *) {},
        // done: the constraints are known, so hand over a buffer and capture
        [](void *data, ext_image_copy_capture_session_v1 *session) {
            auto *job = static_cast<Job *>(data);
            if (job->frame || job->finished)
                return;
            Capture *self = job->owner;
            if (job->format < 0 || !job->width || !job->height) {
                self->finish(job, false);
                return;
            }
            const uint32_t stride = job->width * 4;
            job->size = size_t(stride) * job->height;
            job->fd = memfd_create("stage-capture", MFD_CLOEXEC);
            if (job->fd < 0 || ftruncate(job->fd, off_t(job->size)) < 0) {
                self->finish(job, false);
                return;
            }
            job->data = mmap(nullptr, job->size, PROT_READ | PROT_WRITE, MAP_SHARED, job->fd, 0);
            if (job->data == MAP_FAILED) {
                self->finish(job, false);
                return;
            }
            job->pool = wl_shm_create_pool(self->m_shm, job->fd, int32_t(job->size));
            job->buffer = wl_shm_pool_create_buffer(job->pool, 0, int32_t(job->width), int32_t(job->height),
                                                    int32_t(stride), uint32_t(job->format));
            job->frame = ext_image_copy_capture_session_v1_create_frame(session);
            static const ext_image_copy_capture_frame_v1_listener frameListener = {
                // transform
                [](void *data, ext_image_copy_capture_frame_v1 *, uint32_t transform) {
                    static_cast<Job *>(data)->transform = transform;
                },
                // damage, presentation_time
                [](void *, ext_image_copy_capture_frame_v1 *, int32_t, int32_t, int32_t, int32_t) {},
                [](void *, ext_image_copy_capture_frame_v1 *, uint32_t, uint32_t, uint32_t) {},
                // ready
                [](void *data, ext_image_copy_capture_frame_v1 *) {
                    auto *job = static_cast<Job *>(data);
                    job->owner->finish(job, true);
                },
                // failed
                [](void *data, ext_image_copy_capture_frame_v1 *, uint32_t) {
                    auto *job = static_cast<Job *>(data);
                    job->owner->finish(job, false);
                },
            };
            ext_image_copy_capture_frame_v1_add_listener(job->frame, &frameListener, job);
            ext_image_copy_capture_frame_v1_attach_buffer(job->frame, job->buffer);
            ext_image_copy_capture_frame_v1_damage_buffer(job->frame, 0, 0, int32_t(job->width), int32_t(job->height));
            ext_image_copy_capture_frame_v1_capture(job->frame);
            wl_display_flush(self->m_display);
        },
        // stopped
        [](void *data, ext_image_copy_capture_session_v1 *) {
            auto *job = static_cast<Job *>(data);
            job->owner->finish(job, false);
        },
    };
    ext_image_copy_capture_session_v1_add_listener(job->session, &sessionListener, job);
    wl_display_flush(m_display);
    return job->id;
}

void Capture::finish(Job *job, bool ok)
{
    if (job->finished)
        return;
    job->finished = true;

    QImage image;
    if (ok && job->data != MAP_FAILED) {
        const QImage::Format format = imageFormat(uint32_t(job->format));
        image = QImage(static_cast<const uchar *>(job->data), int(job->width), int(job->height), int(job->width * 4),
                       format).copy();
        // The buffer is in the output's own orientation; turn it upright.
        if (job->transform != WL_OUTPUT_TRANSFORM_NORMAL) {
            QTransform turn;
            if (job->transform >= WL_OUTPUT_TRANSFORM_FLIPPED)
                turn.scale(-1, 1);
            turn.rotate(-90.0 * (job->transform % 4));
            image = image.transformed(turn);
        }
    }

    // Wayland objects go once the current dispatch is over.
    QMetaObject::invokeMethod(this, [this, job] {
        if (job->frame)
            ext_image_copy_capture_frame_v1_destroy(job->frame);
        if (job->session)
            ext_image_copy_capture_session_v1_destroy(job->session);
        if (job->source)
            ext_image_capture_source_v1_destroy(job->source);
        if (job->buffer)
            wl_buffer_destroy(job->buffer);
        if (job->pool)
            wl_shm_pool_destroy(job->pool);
        if (job->data != MAP_FAILED)
            munmap(job->data, job->size);
        if (job->fd >= 0)
            close(job->fd);
        m_jobs.remove(job->id);
        delete job;
        wl_display_flush(m_display);
    }, Qt::QueuedConnection);

    qCDebug(lcStage) << "capture" << job->id << (ok ? "took" : "failed after") << job->clock.elapsed() << "ms"
                     << image.size();
    emit captured(job->id, image);
}
