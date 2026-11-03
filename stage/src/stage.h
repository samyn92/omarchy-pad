#pragma once

#include <QJsonArray>
#include <QElapsedTimer>
#include <QMutex>
#include <QProcess>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
#include <atomic>
#include <functional>

#include "fingers.h"
#include "longpress.h"
#include "pointer.h"
#include "touchscreen.h"

class Apps;
class Capture;
class Hyprland;
class QLocalServer;
class QLocalSocket;
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
    // The console is down with no window in it: a swipe in from the right
    // edge opens an app there.
    Q_PROPERTY(bool consoleEmpty READ consoleEmpty NOTIFY consoleOpenChanged)
    // How Hyprland draws windows: gapsIn, gapsOut, border, rounding, and the
    // active and inactive border colors (#aarrggbb lists, with an angle).
    Q_PROPERTY(QVariantMap look READ look NOTIFY lookChanged)
    // Where tiled windows meet, so a finger can hold and drag the line between
    // them (see dividersIn).
    Q_PROPERTY(QVariantList dividers READ dividers NOTIFY dividersChanged)
    // Omarchy Pad's bar is connected and hands stage the pulls from the top
    // itself, so stage's own strip along the top edge stands aside.
    Q_PROPERTY(bool barConnected READ barConnected NOTIFY barConnectedChanged)
    // How a held divider resizes the windows beside it: live, the windows
    // themselves resized as it moves (the default), or with pictures that
    // follow the finger and the windows set once it lets go, as iPadOS does.
    // `stage resize live|pictures` switches, and it is remembered.
    Q_PROPERTY(bool liveResize READ liveResize NOTIFY liveResizeChanged)

public:
    // At most this many windows share a workspace, and this many the console.
    static constexpr int WorkspaceLimit = 4;
    static constexpr int ConsoleLimit = 2;
    // Omarchy binds workspaces 1 to 10; a slide makes no new one past that.
    static constexpr int LastWorkspace = 10;

    Stage(Hyprland *hyprland, Capture *capture, Snapshots *snapshots, Apps *apps, QObject *parent = nullptr);

    void attach(QWindow *window);
    bool listen(const QString &socketPath, QString *error);
    QString command(const QString &line);

    bool consoleOpen() const { return m_consoleOpen; }
    QRectF consoleRect() const { return m_consoleRect; }
    bool consoleEmpty() const { return m_consoleEmpty; }
    QVariantMap look() const { return m_look; }
    bool hand() const { return m_hand; }
    QVariantList dividers() const { return m_dividers; }
    bool barConnected() const { return !m_bars.isEmpty(); }
    bool liveResize() const { return m_liveResize; }

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
    // How a picture lies (a wl_output_transform), so it can be shown upright.
    Q_INVOKABLE int pictureTurn(int snapshot) const;
    // Brings the console down without Hyprland sliding it in a second time
    // (the finger already did), or sends it back up.
    Q_INVOKABLE void openConsole();
    Q_INVOKABLE void closeConsole();
    // The console going up under a finger: its windows as they are now, with
    // pictures on their way (beginEdge("top")'s plan, but for an open console).
    Q_INVOKABLE QVariantMap beginLift();
    // Takes the console away at once, its pictures already showing.
    Q_INVOKABLE void hideConsole();
    Q_INVOKABLE void setInputRegion(const QVariantList &rects);
    // Finds the dividers again, a moment from now (after the layout changes,
    // or the screen turns).
    Q_INVOKABLE void refreshDividers();
    // A finger holds `divider` (one of `dividers`): its windows on both sides,
    // with pictures on their way, and the usable area.
    Q_INVOKABLE QVariantMap beginDivider(const QVariantMap &divider);
    // Moves the divider to `fraction` of the room it splits.
    Q_INVOKABLE void resizeDivider(const QVariantMap &divider, qreal fraction);
    // Live resizing: the divider of `plan` (from beginDivider) to `line`, as
    // the finger has it. Steps come at most every LiveStepMs, the latest one
    // winning, and the windows don't animate between them.
    Q_INVOKABLE void moveDivider(const QVariantMap &plan, qreal line);
    // Its last step, where the line came to rest (Hyprland animates there),
    // or, for a negative `line`, none (the windows are leaving the split).
    // Their animations come back.
    Q_INVOKABLE void settleDivider(const QVariantMap &plan, qreal line);
    // The windows a divider was pushed over leave the split: they go to the
    // next empty workspace, still running, and the others fill the room.
    Q_INVOKABLE void pushOff(const QStringList &addresses);
    // A window lifted by a held hand (grabStarted) put down on `side` ("left",
    // "right", "top" or "bottom") of the window `target`, which gives up that
    // half to it.
    Q_INVOKABLE void placeWindow(const QString &address, const QString &target, const QString &side);
    // Or on workspace `id` of the strip (0: the first empty one), without
    // following it there.
    Q_INVOKABLE void sendToWorkspace(const QString &address, int id);
    // Asks the window to close, without Hyprland animating it away a second
    // time (the picture already left); `windowClosed` follows if it does.
    Q_INVOKABLE void closeWindow(const QString &address);
    // While a picture of a window moves under the fingers, the real one is
    // hidden at once, so whatever is behind it shows: the wallpaper, other
    // windows, the dimmed screen around the console. showWindow gives it back
    // as it was.
    Q_INVOKABLE void hideWindow(const QString &address);
    Q_INVOKABLE void showWindow(const QString &address);
    // What a three-finger swipe does: "left" and "right" go to the next or
    // previous workspace, "up" maximizes the focused window.
    Q_INVOKABLE void swipe(const QString &direction);
    // Watches the clipboard for copied(); main starts it, the tests don't.
    void watchClipboard();
    // Shows workspace `id` right away: the fingers already slid it in.
    Q_INVOKABLE void switchWorkspace(int id);

    // Pure helpers, kept apart for the tests.
    static QVariantMap pickTarget(const QString &edge, qreal x, qreal y, const QVariantList &windows,
                                  const QRectF &usable, int limit);
    // Where an empty scratchpad's first window goes: the whole usable area,
    // inside Hyprland's outer gap and border (`inset`), as Omarchy's
    // scratchpad is a full-size workspace.
    static QRectF consoleBox(const QRectF &usable, qreal inset);
    // The window a hand at x, y is on, of the visible ones (as from
    // visibleWindows), stacked the way Hyprland draws them. Empty when there
    // is none.
    static QVariantMap windowAt(qreal x, qreal y, const QVariantList &windows, const QString &special);
    // The workspace three fingers slide to from `current`, of Hyprland's
    // `workspaces`; 0 when there is none that way.
    static int neighbourOf(const QJsonArray &workspaces, const QString &monitor, int current, const QString &direction);
    // The strip a lifted window can be dropped on: workspaces 1 to
    // LastWorkspace on `monitor`, of Hyprland's `workspaces`, each as {id,
    // current, apps (the classes of its windows, from `clients`)}, then the
    // first empty one as {id, empty: true} when there is one.
    static QVariantList workspaceTiles(const QJsonArray &workspaces, const QJsonArray &clients, const QString &monitor,
                                       int current);
    // Every line where windows meet across one gap (`gap`: the space between
    // two windows' surfaces, borders and gaps included), each as {vertical,
    // at (the line), from, to (its extent), before and after (the windows'
    // addresses on either side), start and end (the room the two sides
    // share)}. A vertical line has windows left and right of it.
    static QVariantList dividersIn(const QVariantList &windows, qreal gap);
    // "ff7aa2f7 33ccffee 45deg" as {colors: ["#ff7aa2f7", ...], angle: 45}.
    static QVariantMap parseGradient(const QString &gradient);

signals:
    void consoleOpenChanged();
    void barConnectedChanged();
    void liveResizeChanged();
    void lookChanged();
    void dividersChanged();
    // The scratchpad pulled down from the top: by two fingers on the bar, or
    // by whatever sends its pull over the command socket (`stage top
    // begin|follow|end`).
    void topPulled(const QVariantMap &plan, const QPointF &at);
    void topMoved(const QPointF &at);
    void topReleased(const QPointF &at, bool open);
    // Two fingers pulled down from the bar bring the scratchpad the same way;
    // where they let go, the overlay decides whether it opens.
    void topLetGo(const QPointF &at);
    // Two fingers push the open scratchpad up: it follows them, and where
    // they let go (`lifted`, rather than parting) the overlay decides
    // whether it goes.
    void liftStarted(const QPointF &at);
    void liftMoved(const QPointF &at);
    void liftEnded(const QPointF &at, bool lifted);
    void snapshotReady(int id, bool ok);
    void placed(bool ok);
    void handChanged();
    // A hand pulls the window in `plan` (from windowAt) down; `at` is where
    // it has brought its first finger, `ms` the touch screen's clock.
    void pullStarted(const QVariantMap &plan, const QPointF &at, qreal ms);
    void pullMoved(const QPointF &at, qreal ms);
    void pullEnded(const QPointF &at, qreal ms);
    void pullCanceled();
    // Two fingers held still on a window for HoldMs lift it: `plan` is the
    // window (as for a pull) with the windows it can be put beside
    // (`others`), the `workspaces` strip (workspaceTiles) and the `usable`
    // area. It follows the hand until a finger lifts.
    void grabStarted(const QVariantMap &plan, const QPointF &at);
    void grabMoved(const QPointF &at);
    void grabEnded(const QPointF &at);
    // Three fingers went "left", "right" or "up".
    void swiped(const QString &direction);
    // Three fingers pinched "in" (copy) or spread "out" (paste); floatkey
    // has been asked to send the keys.
    void pinched(const QString &direction);
    // Something was copied, from anywhere.
    void copied();
    void windowClosed(const QString &address);
    // Three fingers slide the workspaces; `plan` (from slidePlan) has both
    // sets of windows, `next` the workspace that comes in (none at the end).
    void slideStarted(const QVariantMap &plan, const QPointF &at, qreal ms);
    void slideMoved(const QPointF &at, qreal ms);
    void slideEnded(const QPointF &at, qreal ms);
    void slideCanceled();
    void workspaceShown(const QString &name);
    // One finger held still on a window: it began at `at` (the overlay shows
    // a ring there from a moment on); after LongPressMs it is armed
    // (`pressArmed`, the ring full), and when it lifts it right-clicks there
    // (`pressFired`). Moving, another finger, or lifting before it is armed
    // calls it off (`pressCanceled`).
    void pressStarted(const QPointF &at, int ms);
    void pressArmed(const QPointF &at);
    void pressCanceled();
    void pressFired(const QPointF &at);

private:
    void sendClipboardKeys(const QString &action);
    // Says `line` to every connected bar.
    void tellBars(const QByteArray &line);

    QProcess m_clipboardWatch;
    QElapsedTimer m_clipboardSince;
    QElapsedTimer m_hushed;

    struct Monitor {
        QRectF logical;   // the whole output, in global logical coordinates
        QRectF usable;    // less the bar and anything else reserved
        QString name;
        int workspace = 0;
        QString workspaceName;
        QString special;  // the open special workspace's name, if any
        bool ok = false;
    };

    Monitor monitor() const;
    void refreshLook();
    void registerHiding();
    QVariantList windowsOn(const QString &workspace, const QPointF &origin) const;
    QVariantList visibleWindows(const Monitor &m) const;
    bool refreshTouch();
    QVariantMap slidePlan(const QString &direction);
    QVariantMap grabPlan();
    void startGrab();
    int neighbour(const Monitor &m, const QString &direction) const;
    int pictureOf(const QVariantMap &window);
    void prefetchSlide();
    // `inherits`: the animation a leaf Hyprland has no setting for follows,
    // which it is given back as afterwards.
    void withoutAnimation(const QString &leaf, const std::function<void()> &change, const QString &inherits = {});
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
    QSet<QLocalSocket *> m_bars;    // connections that said `bar here`

    QVariantMap m_look;
    QPointF m_topFrom;
    qreal m_topDistance = 0;
    QVariantList m_dividers;
    QTimer m_dividersLater;
    bool m_consoleOpen = false;
    bool m_consoleEmpty = false;
    QRectF m_consoleRect;
    // Where the console's windows are, or would be: their bounds, or the
    // box Omarchy gives a first one.
    void refreshConsole();
    qreal consoleInset() const;
    // A hand that landed on the bar, which pulls the scratchpad down.
    bool m_topHand = false;
    bool m_topPulling = false;
    bool m_lifting = false;
    // How far below the bar a hand may land and still be on it.
    static constexpr qreal TopHandSlack = 24;
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
    QHash<int, std::pair<QPointF, qreal>> m_moved;   // where fingers moved since the last frame, and when
    Fingers m_fingers;
    int m_touchTransform = 0;
    QSizeF m_touchSize;
    bool m_hand = false;
    bool m_pulling = false;
    bool m_sliding = false;
    bool m_locked = false;
    bool m_grabbing = false;
    QVariantMap m_handPlan;
    // How long two fingers rest on a window before they lift it, and how far
    // they may wander meanwhile (as fingers do, held still).
    static constexpr int HoldMs = 500;
    static constexpr qreal HoldSlack = 14;
    QTimer m_holdTimer;

    // A long press, one finger held still on a window, right-clicks there, as
    // on a tablet: through a pointer of stage's own, since the app already has
    // the finger as a touch. The click comes when the finger lifts, after the
    // app has had the end of its touch (as Windows does it): sent while the
    // finger was still down, the lift that followed closed the menu it had
    // opened (3 Nov 2026). Not on stage's own strips, the keyboard, the lock
    // screen or the desktop, and not in the apps that open their own menu on a
    // long press (Chromium and Electron ones, `longpress/skip` in stage.conf).
    // Omarchy's screensaver ends on a key or when it loses focus, never on a touch; on a tablet the first touch ends
    // it. Its windows (org.omarchy.screensaver, one per monitor), from Hyprland's events.
    void endScreensaver();
    QSet<QString> m_screensavers;
    void handlePress(LongPress::Result result);
    void armPress();
    void firePress();
    bool overKeyboard(const QPointF &at) const;
    static constexpr int PressClickDelayMs = 40;   // after the lift, for the app to have taken the touch's end
    LongPress m_longPress;
    QPointF m_pressAt, m_pressGlobal;   // where the armed press clicks, and every output together
    QRectF m_pressLayout;
    QTimer m_pressTimer;
    int m_longPressMs = 500;
    bool m_longPressOn = true;
    QString m_longPressSkip;
    Pointer m_pointer;
    QHash<QString, int> m_pictures;   // this hand's pictures, by window address
    // Windows asked to close, kept from animating; a window that stays (it
    // asked about unsaved work, say) gets its animations back.
    QHash<QString, QTimer *> m_closing;
    // Windows hidden by hideWindow, and whether they had no_anim of their own.
    QHash<QString, bool> m_hidden;

    // Live resizing (moveDivider): the step waiting for its turn, and the
    // windows kept from animating meanwhile, with whether they had no_anim
    // of their own.
    static constexpr int LiveStepMs = 30;
    void stepDivider();
    void sizeDivider(const QVariantMap &plan, qreal line);
    bool m_liveResize = false;
    QTimer m_liveStep;
    QVariantMap m_livePlan;
    qreal m_liveLine = -1;
    QHash<QString, bool> m_liveQuiet;

    // With debug logging on, when the overlay's frames reached the screen
    // (noted from the render thread) and when the fingers moved, while a hand
    // pulls or slides and the picture settles, so the log can say how
    // smoothly it went.
    void startTiming();
    void reportTiming(const char *what);
    QMutex m_timingLock;
    std::atomic<bool> m_timing{false};
    QList<qreal> m_frameTimes;
    QList<qreal> m_moveTimes;
    qreal m_timingStart = 0;

    // A hand without fingers, for `stage simulate pull` (and `grab`, which
    // rests for `restMs` first): it goes through the
    // touch screen's path, turn and all.
    void simulatePull(const QPointF &from, const QPointF &to, int ms, int fingers, int restMs = 0);
    void simulateRelease();
    void simulatePinch(bool in, int ms);
    QTimer m_simulated;
    QPointF m_simulatedFrom, m_simulatedTo;
    int m_simulatedFingers = 0, m_simulatedSteps = 1, m_simulatedStep = 0;
};
