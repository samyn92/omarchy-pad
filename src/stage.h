#pragma once

#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QTimer>
#include <QVariantMap>

class Apps;
class Capture;
class Hyprland;
class QLocalServer;
class QWindow;
class Snapshots;

// What the edge gestures act on and what they do to Hyprland. QML asks, when
// a finger lands on an edge, which window it is about to shrink (or what the
// scratchpad console holds, with pictures of its hidden windows); when the
// finger lets go on an app in the launcher, this places the new window where
// the launcher was.
class Stage : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool consoleOpen READ consoleOpen NOTIFY consoleOpenChanged)
    Q_PROPERTY(QRectF consoleRect READ consoleRect NOTIFY consoleOpenChanged)

public:
    // At most this many windows share a workspace, and this many the console.
    static constexpr int WorkspaceLimit = 4;
    static constexpr int ConsoleLimit = 2;

    Stage(Hyprland *hyprland, Capture *capture, Snapshots *snapshots, Apps *apps, QObject *parent = nullptr);

    void attach(QWindow *window);
    bool listen(const QString &socketPath, QString *error);
    QString command(const QString &line);

    bool consoleOpen() const { return m_consoleOpen; }
    QRectF consoleRect() const { return m_consoleRect; }

    // A finger landed on an edge ("right", "bottom" or "top") at x, y in the
    // overlay's coordinates. Returns what the gesture would act on; pictures
    // of the console's hidden windows follow as snapshotReady.
    Q_INVOKABLE QVariantMap beginEdge(const QString &edge, qreal x, qreal y);
    // Opens the app the launcher offered, where the plan from beginEdge says,
    // at `fraction` of the space for the window that made room. `placed`
    // follows once its window is up (or it took too long).
    Q_INVOKABLE void launch(const QVariantMap &plan, const QString &appId, qreal fraction = 0.5);
    // Brings the console down without Hyprland sliding it in a second time
    // (the finger already did), or sends it back up.
    Q_INVOKABLE void openConsole();
    Q_INVOKABLE void closeConsole();
    Q_INVOKABLE void setInputRegion(const QVariantList &rects);

    // Pure helpers, kept apart for the tests.
    static QVariantMap pickTarget(const QString &edge, qreal x, qreal y, const QVariantList &windows,
                                  const QRectF &usable, int limit);
    static QRectF consoleBox(const QRectF &usable, int windows);

signals:
    void consoleOpenChanged();
    void snapshotReady(int id, bool ok);
    void placed(bool ok);

private:
    struct Monitor {
        QRectF logical;   // the whole output, in global logical coordinates
        QRectF usable;    // less the bar and anything else reserved
        int workspace = 0;
        QString workspaceName;
        QString special;  // the open special workspace's name, if any
        bool ok = false;
    };

    Monitor monitor() const;
    QVariantList windowsOn(const QString &workspace, const QPointF &origin) const;
    void onEvent(const QString &name, const QString &data);
    void applyInputRegion(bool force);
    void finishPlacement(bool ok);

    Hyprland *m_hyprland;
    Capture *m_capture;
    Snapshots *m_snapshots;
    Apps *m_apps;
    QPointer<QWindow> m_window;
    QLocalServer *m_server = nullptr;

    bool m_consoleOpen = false;
    QRectF m_consoleRect;
    QList<QRect> m_inputRects;

    // The window being opened into the space the launcher had.
    QVariantMap m_pending;
    QTimer m_pendingTimeout;
};
