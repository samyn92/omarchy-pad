#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <memory>
#include <sys/types.h>

class GpuPicture;
struct gbm_device;
struct zwp_linux_dmabuf_v1;

class QScreen;
struct wl_display;
struct wl_event_queue;
struct wl_registry;
struct wl_shm;
struct ext_image_copy_capture_manager_v1;
struct ext_output_image_capture_source_manager_v1;
struct ext_foreign_toplevel_image_capture_source_manager_v1;
struct ext_foreign_toplevel_list_v1;
struct ext_foreign_toplevel_handle_v1;
struct hyprland_toplevel_mapping_manager_v1;
struct hyprland_toplevel_window_mapping_handle_v1;

// Takes still pictures of the screen and of single windows through the
// standard capture protocols (ext-image-copy-capture), so a gesture can move
// a picture of a window under the finger while the real one stays put.
// Captures run on Qt's own Wayland connection and arrive as `captured`.
class Capture : public QObject {
    Q_OBJECT

public:
    explicit Capture(QObject *parent = nullptr);
    ~Capture() override;

    bool connectToCompositor();
    bool canCaptureScreens() const { return m_copy && m_outputs; }
    bool canCaptureWindows() const { return m_copy && m_toplevelSources && m_list; }

    // Each returns a request id, or -1 when it can't be done; `captured`
    // follows with that id (and a null image if it failed).
    int captureScreen(QScreen *screen);
    // Takes the window with Hyprland's address (as `clients` shows it,
    // 0x…). Without Hyprland's toplevel mapping it falls back to app id and
    // title, which can't tell two same-titled windows apart.
    int captureWindow(const QString &address, const QString &appId, const QString &title);

signals:
    void captured(int id, const QImage &image);
    // The same, kept on the GPU, when the compositor and the driver allow it
    // (and then instead of `captured`). `turn` is how the picture lies, as a
    // wl_output_transform: it isn't turned upright.
    void capturedOnGpu(int id, std::shared_ptr<const GpuPicture> picture, int turn);

private:
    struct Toplevel {
        QString appId, title, address;
        hyprland_toplevel_window_mapping_handle_v1 *mapping = nullptr;
    };
    struct Job;

    int start(struct ext_image_capture_source_v1 *source);
    bool onGpu(Job *job);
    bool openGpu(dev_t device);
    void inMemory(Job *job);
    void captureInto(Job *job);
    void finish(Job *job, bool ok);
    void mapToplevel(ext_foreign_toplevel_handle_v1 *handle);

    static void registryGlobal(void *, wl_registry *, uint32_t, const char *, uint32_t);
    static void registryGlobalRemove(void *, wl_registry *, uint32_t);
    static void toplevelAdded(void *, ext_foreign_toplevel_list_v1 *, ext_foreign_toplevel_handle_v1 *);
    static void listFinished(void *, ext_foreign_toplevel_list_v1 *);
    static void handleClosed(void *, ext_foreign_toplevel_handle_v1 *);
    static void handleDone(void *, ext_foreign_toplevel_handle_v1 *);
    static void handleTitle(void *, ext_foreign_toplevel_handle_v1 *, const char *);
    static void handleAppId(void *, ext_foreign_toplevel_handle_v1 *, const char *);
    static void handleIdentifier(void *, ext_foreign_toplevel_handle_v1 *, const char *);
    static void mappingAddress(void *, hyprland_toplevel_window_mapping_handle_v1 *, uint32_t, uint32_t);
    static void mappingFailed(void *, hyprland_toplevel_window_mapping_handle_v1 *);

    wl_display *m_display = nullptr;
    wl_event_queue *m_queue = nullptr;
    wl_registry *m_registry = nullptr;
    wl_shm *m_shm = nullptr;
    zwp_linux_dmabuf_v1 *m_dmabuf = nullptr;
    gbm_device *m_gpu = nullptr;
    int m_gpuFd = -1;
    dev_t m_gpuDevice = 0;
    bool m_gpuTried = false;
    ext_image_copy_capture_manager_v1 *m_copy = nullptr;
    ext_output_image_capture_source_manager_v1 *m_outputs = nullptr;
    ext_foreign_toplevel_image_capture_source_manager_v1 *m_toplevelSources = nullptr;
    ext_foreign_toplevel_list_v1 *m_list = nullptr;
    hyprland_toplevel_mapping_manager_v1 *m_mapping = nullptr;
    QHash<ext_foreign_toplevel_handle_v1 *, Toplevel> m_toplevels;
    QHash<int, Job *> m_jobs;
    int m_nextId = 1;
};
