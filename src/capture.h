#pragma once

#include <QHash>
#include <QImage>
#include <QObject>

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
    // Windows are known to Wayland by app id and title, which is how
    // Hyprland's windows are matched to them.
    int captureWindow(const QString &appId, const QString &title);

signals:
    void captured(int id, const QImage &image);

private:
    struct Toplevel {
        QString appId, title;
    };
    struct Job;

    int start(struct ext_image_capture_source_v1 *source);
    void finish(Job *job, bool ok);

    static void registryGlobal(void *, wl_registry *, uint32_t, const char *, uint32_t);
    static void registryGlobalRemove(void *, wl_registry *, uint32_t);
    static void toplevelAdded(void *, ext_foreign_toplevel_list_v1 *, ext_foreign_toplevel_handle_v1 *);
    static void listFinished(void *, ext_foreign_toplevel_list_v1 *);
    static void handleClosed(void *, ext_foreign_toplevel_handle_v1 *);
    static void handleDone(void *, ext_foreign_toplevel_handle_v1 *);
    static void handleTitle(void *, ext_foreign_toplevel_handle_v1 *, const char *);
    static void handleAppId(void *, ext_foreign_toplevel_handle_v1 *, const char *);
    static void handleIdentifier(void *, ext_foreign_toplevel_handle_v1 *, const char *);

    wl_display *m_display = nullptr;
    wl_event_queue *m_queue = nullptr;
    wl_registry *m_registry = nullptr;
    wl_shm *m_shm = nullptr;
    ext_image_copy_capture_manager_v1 *m_copy = nullptr;
    ext_output_image_capture_source_manager_v1 *m_outputs = nullptr;
    ext_foreign_toplevel_image_capture_source_manager_v1 *m_toplevelSources = nullptr;
    ext_foreign_toplevel_list_v1 *m_list = nullptr;
    QHash<ext_foreign_toplevel_handle_v1 *, Toplevel> m_toplevels;
    QHash<int, Job *> m_jobs;
    int m_nextId = 1;
};
