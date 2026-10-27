#pragma once

#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QTimer>
#include <QVariantMap>

#include "fingers.h"
#include "touchscreen.h"

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
// the launcher was. Hands (two or three fingers together, read straight from
// the touch screen) come through here too: which window they pull down, and
// where they swipe.
class Stage : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool consoleOpen READ consoleOpen NOTIFY consoleOpenChanged)
    // Two or three fingers that landed together are on the screen; the one
    // finger edge gestures leave them alone.
    Q_PROPERTY(bool hand READ hand NOTIFY handChanged)
    Q_PROPERTY(QRectF consoleRect READ consoleRect NOTIFY consoleOpenChanged)
    // How Hyprland draws windows: gapsIn, gapsOut, border, rounding, and the
    // active and inactive border colors (#aarrggbb lists, with an angle).
    Q_PROPERTY(QVariantMap look READ look NOTIFY lookChanged)

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
    QVariantMap look() const { return m_look; }
    bool hand() const { return m_hand; }

    // A finger from the touch screen (Touchscreen::touched).
    void touch(Touchscreen::Kind kind, int slot, const QPointF &panel, qreal ms);

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
    // Asks the window to close, without Hyprland animating it away a second
    // time (the picture already left); `windowClosed` follows if it does.
    Q_INVOKABLE void closeWindow(const QString &address);
    // What a three-finger swipe does: "left" and "right" go to the next or
    // previous workspace, "up" maximizes the focused window.
    Q_INVOKABLE void swipe(const QString &direction);

    // Pure helpers, kept apart for the tests.
    static QVariantMap pickTarget(const QString &edge, qreal x, qreal y, const QVariantList &windows,
                                  const QRectF &usable, int limit);
    static QRectF consoleBox(const QRectF &usable, int windows);
    // The window a hand at x, y is on, of the visible ones (as from
    // visibleWindows), stacked the way Hyprland draws them. Empty when there
    // is none; `animated` when nothing else shows behind it, so a picture of
    // it can be pulled away over the wallpaper.
    static QVariantMap windowAt(qreal x, qreal y, const QVariantList &windows, const QString &special);
    // "ff7aa2f7 33ccffee 45deg" as {colors: ["#ff7aa2f7", ...], angle: 45}.
    static QVariantMap parseGradient(const QString &gradient);

signals:
    void consoleOpenChanged();
    void lookChanged();
    void snapshotReady(int id, bool ok);
    void placed(bool ok);
    void handChanged();
    // A hand pulls the window in `plan` (from windowAt) down; `at` is where
    // it has brought its first finger, `ms` the touch screen's clock.
    void pullStarted(const QVariantMap &plan, const QPointF &at, qreal ms);
    void pullMoved(const QPointF &at, qreal ms);
    void pullEnded(const QPointF &at, qreal ms);
    void pullCanceled();
    // Three fingers went "left", "right" or "up".
    void swiped(const QString &direction);
    void windowClosed(const QString &address);

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
    void refreshLook();
    QVariantList windowsOn(const QString &workspace, const QPointF &origin) const;
    QVariantList visibleWindows(const Monitor &m) const;
    bool refreshTouch();
    void handle(const QList<Fingers::Event> &events);
    void onEvent(const QString &name, const QString &data);
    void applyInputRegion(bool force);
    void finishPlacement(bool ok);

    Hyprland *m_hyprland;
    Capture *m_capture;
    Snapshots *m_snapshots;
    Apps *m_apps;
    QPointer<QWindow> m_window;
    QLocalServer *m_server = nullptr;

    QVariantMap m_look;
    bool m_consoleOpen = false;
    QRectF m_consoleRect;
    QList<QRect> m_inputRects;

    // The window being opened into the space the launcher had.
    QVariantMap m_pending;
    QTimer m_pendingTimeout;

    // Fingers on the touch screen, as the panel reported them, and how
    // Hyprland turns them onto the screen.
    struct Touch {
        QPointF start;
        QPointF now;
        qreal landed = 0;
    };
    QHash<int, Touch> m_touches;
    Fingers m_fingers;
    int m_touchTransform = 0;
    QSizeF m_touchSize;
    bool m_hand = false;
    bool m_pulling = false;
    bool m_locked = false;
    QVariantMap m_handPlan;
    // Windows asked to close, kept from animating; a window that stays (it
    // asked about unsaved work, say) gets its animations back.
    QHash<QString, QTimer *> m_closing;
};
