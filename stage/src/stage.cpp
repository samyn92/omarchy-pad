#include "stage.h"

#include <LayerShellQt/window.h>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QMap>
#include <QQuickWindow>
#include <QLineF>
#include <QRegion>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QWindow>
#include <QtMath>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

#include "apps.h"
#include "capture.h"
#include "gpupicture.h"
#include "hyprland.h"
#include "images.h"
#include "ipc.h"

// QT_LOGGING_RULES="stage.debug=true" shows what the gestures see.
Q_LOGGING_CATEGORY(lcStage, "stage", QtInfoMsg)

static const QString Scratchpad = "special:scratchpad";
// Windows with this tag are see-through, by a rule stage registers. Taking
// the tag off gives a window back whatever opacity its own rules give it,
// which setting its opacity directly couldn't undo.
static const QString HiddenTag = "stage-hidden";
// The special workspace a window put down beside another passes through.
static const QString GrabAside = "stagegrab";

static qreal clockMs()
{
    // The touch screen's clock: CLOCK_MONOTONIC, as libinput stamps events.
    return std::chrono::duration<qreal, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The apps a long press leaves alone: they open their own menu on one (Chromium, its web apps and the Electron apps
// Omarchy brings), where a right click as well would make two. stage.conf's longpress/skip replaces it.
static const char DefaultLongPressSkip[] =
    "chromium|Chromium|google-chrome.*|Google-chrome.*|brave-browser.*|Brave-browser.*|chrome-.*|code|Code|code-oss|"
    "obsidian|Obsidian|signal|Signal|discord|Slack|typora|Typora|1Password|electron.*";

static QVariantMap rectMap(const QRectF &r)
{
    return {{"x", r.x()}, {"y", r.y()}, {"w", r.width()}, {"h", r.height()}};
}

Stage::Stage(Hyprland *hyprland, Capture *capture, Snapshots *snapshots, Apps *apps, QObject *parent)
    : QObject(parent), m_hyprland(hyprland), m_capture(capture), m_snapshots(snapshots), m_apps(apps)
{
    m_liveResize = QSettings("stage", "stage").value("resize", "live").toString() == "live";
    m_liveStep.setSingleShot(true);
    m_liveStep.setInterval(LiveStepMs);
    connect(&m_liveStep, &QTimer::timeout, this, &Stage::stepDivider);

    // An app that never shows a window (or shows it somewhere else) must not
    // leave the launcher's space hanging.
    m_pendingTimeout.setSingleShot(true);
    m_pendingTimeout.setInterval(10000);
    connect(&m_pendingTimeout, &QTimer::timeout, this, [this] { finishPlacement(false); });
    m_holdTimer.setSingleShot(true);
    m_holdTimer.setInterval(HoldMs);
    connect(&m_holdTimer, &QTimer::timeout, this, &Stage::startGrab);
    // The long press (handlePress, armPress): stage.conf can turn it off, time it and name the apps it leaves alone.
    QSettings settings("stage", "stage");
    m_longPressOn = settings.value("longpress/enabled", true).toBool();
    m_longPressMs = qBound(250, settings.value("longpress/ms", 500).toInt(), 2000);
    m_longPressSkip = settings.value("longpress/skip", QString(DefaultLongPressSkip)).toString();
    m_pressTimer.setSingleShot(true);
    m_pressTimer.setInterval(m_longPressMs);
    connect(&m_pressTimer, &QTimer::timeout, this, &Stage::armPress);

    connect(m_hyprland, &Hyprland::hyprlandEvent, this, &Stage::onEvent);
    // Several layout events come in a burst; the dividers are found once,
    // after it.
    m_dividersLater.setSingleShot(true);
    m_dividersLater.setInterval(80);
    connect(&m_dividersLater, &QTimer::timeout, this, [this] {
        // An app opened into the open console, or its last one gone.
        if (m_consoleOpen)
            refreshConsole();
        const Monitor m = monitor();
        QVariantList found;
        if (m.ok) {
            const QVariantList windows = windowsOn(m.special.isEmpty() ? m.workspaceName : m.special, m.logical.topLeft());
            const bool covered = std::any_of(windows.cbegin(), windows.cend(),
                                             [](const QVariant &w) { return w.toMap().value("fullscreen").toBool(); });
            const qreal gap = 2 * (m_look.value("gapsIn").toReal() + m_look.value("border").toReal());
            if (!covered)
                found = dividersIn(windows, gap);
        }
        if (found != m_dividers) {
            m_dividers = found;
            qCDebug(lcStage) << "dividers" << m_dividers.size();
            emit dividersChanged();
        }
    });
    connect(m_capture, &Capture::captured, this, [this](int id, const QImage &image) {
        qCDebug(lcStage) << "snapshot" << id << image.size();
        if (!image.isNull())
            m_snapshots->insert(id, image);
        emit snapshotReady(id, !image.isNull());
    });
    connect(m_capture, &Capture::capturedOnGpu, this, [this](int id, std::shared_ptr<const GpuPicture> picture, int turn) {
        m_snapshots->insert(id, std::move(picture), turn);
        emit snapshotReady(id, true);
    });

    refreshLook();
    registerHiding();
    // A stage that stopped mid-gesture may have left a window hidden.
    for (const QJsonValue &value : m_hyprland->query("clients").array()) {
        const QJsonObject o = value.toObject();
        const QJsonArray tags = o.value("tags").toArray();
        if (std::any_of(tags.cbegin(), tags.cend(), [](const QJsonValue &t) { return t.toString().startsWith(HiddenTag); })) {
            const QString window = QString("window = \"address:%1\"").arg(o.value("address").toString());
            m_hyprland->dispatch(QString("hl.dsp.window.tag({ tag = \"-%1\", %2 })").arg(HiddenTag, window));
            m_hyprland->dispatch(QString("hl.dsp.window.set_prop({ prop = \"no_anim\", value = \"0\", %1 })").arg(window));
        }
    }
    m_dividersLater.start();
    refreshTouch();
    const Monitor m = monitor();
    if (m.ok && m.special == Scratchpad) {
        m_consoleOpen = true;
        refreshConsole();
    }
}

void Stage::refreshConsole()
{
    const Monitor m = monitor();
    const QVariantList windows = windowsOn(Scratchpad, m.logical.topLeft());
    QRectF rect = consoleBox(m.usable.translated(-m.logical.topLeft()), consoleInset());
    if (!windows.isEmpty()) {
        QRectF bounds;
        for (const QVariant &v : windows) {
            const QVariantMap w = v.toMap();
            bounds = bounds.united(QRectF(w["x"].toReal(), w["y"].toReal(), w["w"].toReal(), w["h"].toReal()));
        }
        rect = bounds;
    }
    if (rect != m_consoleRect || windows.isEmpty() != m_consoleEmpty) {
        m_consoleRect = rect;
        m_consoleEmpty = windows.isEmpty();
        emit consoleOpenChanged();
    }
}

void Stage::attach(QWindow *window)
{
    m_window = window;
    // One transparent overlay over the whole output, the bar included, that
    // only takes touches along the edges (and wherever a gesture is going
    // on); it never takes keyboard focus.
    auto *layer = LayerShellQt::Window::get(window);
    layer->setScope("stage");
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
                                                    | LayerShellQt::Window::AnchorLeft
                                                    | LayerShellQt::Window::AnchorRight));
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setActivateOnShow(false);
    layer->setExclusiveZone(-1);
    if (!m_pointer.connectToCompositor())
        qWarning("stage: the compositor has no virtual pointer; a long press won't right-click");
    connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
        if (visible)
            applyInputRegion(true);
    });
    if (auto *quick = qobject_cast<QQuickWindow *>(window))
        connect(quick, &QQuickWindow::frameSwapped, this, [this] {
            if (!m_timing)
                return;
            QMutexLocker lock(&m_timingLock);
            m_frameTimes << clockMs();
        }, Qt::DirectConnection);
}

void Stage::startTiming()
{
    if (!lcStage().isDebugEnabled())
        return;
    QMutexLocker lock(&m_timingLock);
    m_frameTimes.clear();
    m_moveTimes.clear();
    m_timingStart = clockMs();
    m_timing = true;
}

// Steps between frames and between finger moves: how many, the usual one,
// the longest, and how many took more than one and a half usual ones.
void Stage::reportTiming(const char *what)
{
    if (!m_timing)
        return;
    QTimer::singleShot(450, this, [this, what] {
        m_timing = false;
        QMutexLocker lock(&m_timingLock);
        auto steps = [](const QList<qreal> &times) {
            QList<qreal> d;
            for (qsizetype i = 1; i < times.size(); ++i)
                d << times[i] - times[i - 1];
            if (d.isEmpty())
                return QString("none");
            QList<qreal> sorted = d;
            std::sort(sorted.begin(), sorted.end());
            const qreal usual = sorted[sorted.size() / 2];
            const auto late = std::count_if(d.cbegin(), d.cend(), [usual](qreal x) { return x > usual * 1.5; });
            QStringList where;
            for (qsizetype i = 0; i < d.size(); ++i)
                if (d[i] > usual * 1.5)
                    where << QString("%1ms@%2").arg(d[i], 0, 'f', 0).arg(times[i] - times[0], 0, 'f', 0);
            return QString("%1 every %2 ms, longest %3 ms, %4 late [%5]")
                .arg(d.size())
                .arg(usual, 0, 'f', 1)
                .arg(sorted.last(), 0, 'f', 1)
                .arg(late)
                .arg(where.mid(0, 12).join(' '));
        };
        if (!m_frameTimes.isEmpty() && !m_moveTimes.isEmpty())
            qCDebug(lcStage).noquote() << what << "first frame" << m_frameTimes.first() - m_timingStart
                                       << "ms after the start, first move" << m_moveTimes.first() - m_timingStart;
        qCDebug(lcStage).noquote() << what << "frames:" << steps(m_frameTimes) << "| moves:" << steps(m_moveTimes);
    });
}

Stage::Monitor Stage::monitor() const
{
    Monitor m;
    for (const QJsonValue &value : m_hyprland->query("monitors").array()) {
        const QJsonObject o = value.toObject();
        if (!o.value("focused").toBool())
            continue;
        const double scale = o.value("scale").toDouble(1);
        double width = o.value("width").toDouble(), height = o.value("height").toDouble();
        // Width and height are the panel's own; a quarter turn swaps them.
        if (o.value("transform").toInt() % 2 == 1)
            std::swap(width, height);
        m.logical = QRectF(o.value("x").toDouble(), o.value("y").toDouble(), width / scale, height / scale);
        const QJsonArray reserved = o.value("reserved").toArray();
        m.usable = m.logical.adjusted(reserved.at(0).toDouble(), reserved.at(1).toDouble(),
                                      -reserved.at(2).toDouble(), -reserved.at(3).toDouble());
        m.name = o.value("name").toString();
        m.workspace = o.value("activeWorkspace").toObject().value("id").toInt();
        m.workspaceName = o.value("activeWorkspace").toObject().value("name").toString();
        m.special = o.value("specialWorkspace").toObject().value("name").toString();
        m.ok = m.logical.isValid();
        break;
    }
    return m;
}

QVariantList Stage::windowsOn(const QString &workspace, const QPointF &origin) const
{
    QVariantList windows;
    for (const QJsonValue &value : m_hyprland->query("clients").array()) {
        const QJsonObject o = value.toObject();
        if (o.value("workspace").toObject().value("name").toString() != workspace || !o.value("mapped").toBool()
            || o.value("hidden").toBool() || o.value("floating").toBool())
            continue;
        const QJsonArray at = o.value("at").toArray(), size = o.value("size").toArray();
        windows << QVariantMap{
            {"address", o.value("address").toString()},
            {"appClass", o.value("class").toString()},
            {"title", o.value("title").toString()},
            {"x", at.at(0).toDouble() - origin.x()},
            {"y", at.at(1).toDouble() - origin.y()},
            {"w", size.at(0).toDouble()},
            {"h", size.at(1).toDouble()},
            {"fullscreen", o.value("fullscreen").toInt() != 0},
            {"active", o.value("focusHistoryID").toInt(-1) == 0},
        };
    }
    return windows;
}

// Every window on screen: the workspace's, the open console's and pinned
// ones, floating or not.
QVariantList Stage::visibleWindows(const Monitor &m) const
{
    const QPointF origin = m.logical.topLeft();
    QVariantList windows;
    for (const QJsonValue &value : m_hyprland->query("clients").array()) {
        const QJsonObject o = value.toObject();
        const QString workspace = o.value("workspace").toObject().value("name").toString();
        const bool pinned = o.value("pinned").toBool();
        if (!o.value("mapped").toBool() || o.value("hidden").toBool()
            || (workspace != m.workspaceName && (m.special.isEmpty() || workspace != m.special) && !pinned))
            continue;
        const QJsonArray at = o.value("at").toArray(), size = o.value("size").toArray();
        windows << QVariantMap{
            {"address", o.value("address").toString()},
            {"appClass", o.value("class").toString()},
            {"title", o.value("title").toString()},
            {"x", at.at(0).toDouble() - origin.x()},
            {"y", at.at(1).toDouble() - origin.y()},
            {"w", size.at(0).toDouble()},
            {"h", size.at(1).toDouble()},
            {"workspace", workspace},
            {"workspaceId", o.value("workspace").toObject().value("id").toInt()},
            {"floating", o.value("floating").toBool() || pinned},
            {"fullscreen", o.value("fullscreen").toInt() != 0},
            {"focus", o.value("focusHistoryID").toInt(1000)},
        };
    }
    return windows;
}

QVariantMap Stage::windowAt(qreal x, qreal y, const QVariantList &windows, const QString &special)
{
    // The open console over everything, then floating windows (the one used
    // last on top), then a fullscreen or maximized one, then the tiled ones.
    auto rank = [&special](const QVariantMap &w) {
        if (!special.isEmpty() && w["workspace"].toString() == special)
            return 0;
        return w["floating"].toBool() ? 1 : w["fullscreen"].toBool() ? 2 : 3;
    };
    auto rect = [](const QVariantMap &w) { return QRectF(w["x"].toReal(), w["y"].toReal(), w["w"].toReal(), w["h"].toReal()); };
    QList<QVariantMap> stack;
    for (const QVariant &v : windows)
        stack << v.toMap();
    std::stable_sort(stack.begin(), stack.end(), [&](const QVariantMap &a, const QVariantMap &b) {
        return rank(a) != rank(b) ? rank(a) < rank(b) : a["focus"].toInt() < b["focus"].toInt();
    });

    for (const QVariantMap &w : std::as_const(stack)) {
        // With the console down, only its windows can be pulled away.
        if (!special.isEmpty() && rank(w) != 0)
            break;
        if (!rect(w).contains(x, y))
            continue;
        QVariantMap plan = w;
        plan["edge"] = "close";
        return plan;
    }
    return {};
}

QVariantMap Stage::pickTarget(const QString &edge, qreal x, qreal y, const QVariantList &windows,
                              const QRectF &usable, int limit)
{
    QVariantMap result{{"edge", edge}, {"usable", rectMap(usable)}};
    if (windows.isEmpty()) {
        // Nothing to shrink: the launcher gets the whole space.
        result["empty"] = true;
        result["allowed"] = true;
        return result;
    }
    const bool right = edge == "right";
    auto farEdge = [right](const QVariantMap &w) {
        return right ? w["x"].toReal() + w["w"].toReal() : w["y"].toReal() + w["h"].toReal();
    };
    qreal outermost = 0;
    for (const QVariant &v : windows)
        outermost = qMax(outermost, farEdge(v.toMap()));

    // Of the windows along that edge, the one level with the finger.
    QVariantMap target;
    qreal best = std::numeric_limits<qreal>::max();
    for (const QVariant &v : windows) {
        const QVariantMap w = v.toMap();
        if (qAbs(farEdge(w) - outermost) > 2)
            continue;
        const qreal along = right ? y : x;
        const qreal low = right ? w["y"].toReal() : w["x"].toReal();
        const qreal high = low + (right ? w["h"].toReal() : w["w"].toReal());
        const qreal distance = along < low ? low - along : along > high ? along - high : 0;
        if (distance < best) {
            best = distance;
            target = w;
        }
    }
    for (auto it = target.cbegin(); it != target.cend(); ++it)
        result.insert(it.key(), it.value());

    // A window can be split once each way: one that already shares the
    // width (or height) makes no more room on that side.
    const qreal span = right ? target["w"].toReal() / usable.width() : target["h"].toReal() / usable.height();
    QString reason;
    if (windows.size() >= limit)
        reason = "full";
    else if (span < 0.66)
        reason = "split";
    else if (target["fullscreen"].toBool())
        reason = "fullscreen";
    result["empty"] = false;
    result["allowed"] = reason.isEmpty();
    result["reason"] = reason;
    return result;
}

// Omarchy's console: the top half of the work area, and while it holds one
// window a box twice as wide as tall in the middle (see qconsole.lua).
QVariantMap Stage::parseGradient(const QString &gradient)
{
    QVariantList colors;
    int angle = 0;
    for (const QString &word : gradient.split(' ', Qt::SkipEmptyParts)) {
        if (word.endsWith("deg"))
            angle = word.chopped(3).toInt();
        else if (word.size() == 8)
            colors << "#" + word;
    }
    if (colors.isEmpty())
        colors << "#ff888888";
    return {{"colors", colors}, {"angle", angle}};
}

// Read once at start and again whenever Hyprland reloads its config, so the
// windows a gesture draws look like the ones Hyprland does.
void Stage::refreshLook()
{
    auto option = [this](const char *name) {
        return m_hyprland->query(QByteArray("getoption ") + name).object();
    };
    // Gaps come as "5 5 5 5" (top right bottom left); they're all the same here.
    auto first = [](const QJsonObject &o) {
        const QString css = o.value("css").toString();
        return css.isEmpty() ? o.value("int").toDouble() : css.section(' ', 0, 0).toDouble();
    };
    QVariantMap look{
        {"gapsIn", first(option("general:gaps_in"))},
        {"gapsOut", first(option("general:gaps_out"))},
        {"border", option("general:border_size").value("int").toDouble(2)},
        {"rounding", option("decoration:rounding").value("int").toDouble(0)},
        {"active", parseGradient(option("general:col.active_border").value("gradient").toString())},
        {"inactive", parseGradient(option("general:col.inactive_border").value("gradient").toString())},
    };
    if (look != m_look) {
        m_look = look;
        emit lookChanged();
    }
}

QVariantList Stage::dividersIn(const QVariantList &windows, qreal gap)
{
    struct Piece {
        qreal at, from, to, start, end;
        QStringList before, after;
    };
    QList<QPair<QRectF, QString>> boxes;
    for (const QVariant &v : windows) {
        const QVariantMap w = v.toMap();
        boxes << qMakePair(QRectF(w["x"].toReal(), w["y"].toReal(), w["w"].toReal(), w["h"].toReal()), w["address"].toString());
    }
    QVariantList out;
    for (const bool vertical : {true, false}) {
        // Along a vertical line, "across" is x and "along" is y.
        auto lo = [vertical](const QRectF &r) { return vertical ? r.left() : r.top(); };
        auto hi = [vertical](const QRectF &r) { return vertical ? r.right() : r.bottom(); };
        auto alongLo = [vertical](const QRectF &r) { return vertical ? r.top() : r.left(); };
        auto alongHi = [vertical](const QRectF &r) { return vertical ? r.bottom() : r.right(); };
        QList<Piece> pieces;
        for (const auto &[a, aId] : boxes) {
            for (const auto &[b, bId] : boxes) {
                const qreal between = lo(b) - hi(a);
                const qreal from = qMax(alongLo(a), alongLo(b)), to = qMin(alongHi(a), alongHi(b));
                // One gap apart, side by side for more than a corner.
                if (between <= 0 || between > gap + 2 || to - from < 1)
                    continue;
                pieces << Piece{(hi(a) + lo(b)) / 2, from, to, lo(a), hi(b), {aId}, {bId}};
            }
        }
        std::sort(pieces.begin(), pieces.end(),
                  [](const Piece &p, const Piece &q) { return p.at != q.at ? p.at < q.at : p.from < q.from; });
        // Pieces on the same line, one gap or less apart, are one divider: a
        // window on one side and two stacked on the other meet along one line.
        QList<Piece> merged;
        for (const Piece &p : std::as_const(pieces)) {
            if (!merged.isEmpty() && qAbs(merged.last().at - p.at) < 2 && p.from <= merged.last().to + gap + 2) {
                Piece &m = merged.last();
                m.from = qMin(m.from, p.from);
                m.to = qMax(m.to, p.to);
                m.start = qMin(m.start, p.start);
                m.end = qMax(m.end, p.end);
                for (const QString &id : p.before)
                    if (!m.before.contains(id))
                        m.before << id;
                for (const QString &id : p.after)
                    if (!m.after.contains(id))
                        m.after << id;
            } else {
                merged << p;
            }
        }
        for (const Piece &m : std::as_const(merged))
            out << QVariantMap{{"vertical", vertical}, {"at", m.at},         {"from", m.from},     {"to", m.to},
                               {"start", m.start},     {"end", m.end},       {"before", m.before}, {"after", m.after}};
    }
    return out;
}

void Stage::refreshDividers()
{
    if (!m_dividersLater.isActive())
        m_dividersLater.start();
}

QRectF Stage::consoleBox(const QRectF &usable, qreal inset)
{
    return usable.adjusted(inset, inset, -inset, -inset);
}

qreal Stage::consoleInset() const
{
    return m_look.value("gapsOut").toReal() + m_look.value("border").toReal();
}

QVariantMap Stage::beginEdge(const QString &edge, qreal x, qreal y)
{
    const Monitor m = monitor();
    if (!m.ok)
        return {{"edge", edge}, {"allowed", false}, {"reason", "hyprland"}};
    const QPointF origin = m.logical.topLeft();
    const QRectF usable = m.usable.translated(-origin);

    if (edge == "top") {
        QVariantList windows = windowsOn(Scratchpad, origin);
        QRectF box = consoleBox(usable, consoleInset());
        // Hidden windows keep the place they had, so they are the console.
        if (!windows.isEmpty()) {
            QRectF bounds;
            for (const QVariant &v : std::as_const(windows)) {
                const QVariantMap w = v.toMap();
                bounds = bounds.united(QRectF(w["x"].toReal(), w["y"].toReal(), w["w"].toReal(), w["h"].toReal()));
            }
            box = bounds;
        }
        for (QVariant &v : windows) {
            QVariantMap w = v.toMap();
            w["snapshot"] = m_capture->captureWindow(w["address"].toString(), w["appClass"].toString(),
                                                     w["title"].toString());
            v = w;
        }
        return {
            {"edge", "top"},
            {"allowed", !m_consoleOpen},
            {"reason", m_consoleOpen ? "open" : ""},
            {"empty", windows.isEmpty()},
            {"console", rectMap(box)},
            {"windows", windows},
            {"usable", rectMap(usable)},
        };
    }

    const bool inConsole = m_consoleOpen;
    const QString workspace = inConsole ? m.special : m.workspaceName;
    QVariantMap result = pickTarget(edge, x, y, windowsOn(workspace, origin), usable,
                                    inConsole ? ConsoleLimit : WorkspaceLimit);
    if (edge == "bottom" && result["allowed"].toBool() && (inConsole || m.usable.bottom() < m.logical.bottom() - 1)) {
        // The console only splits sideways, and a docked keyboard owns the
        // bottom edge.
        result["allowed"] = false;
        result["reason"] = inConsole ? "console" : "keyboard";
    }
    result["workspace"] = workspace;
    result["inConsole"] = inConsole;
    // An empty console's first app opens in the console's box, not on the
    // whole screen.
    if (inConsole)
        result["console"] = rectMap(m_consoleRect);
    qCDebug(lcStage) << "edge at" << x << y << result;
    return result;
}

int Stage::pictureTurn(int snapshot) const
{
    return m_snapshots->turn(snapshot);
}

void Stage::launch(const QVariantMap &plan, const QString &appId, qreal fraction)
{
    const Apps::App *app = m_apps->find(appId);
    if (!app) {
        emit placed(false);
        return;
    }
    // Launched the way Omarchy launches apps, as their own systemd units.
    const QString command = "uwsm-app -- " + app->command;
    const QString edge = plan.value("edge").toString();

    m_pending.clear();
    if (edge == "top") {
        // Into the hidden console first; it comes down once the app is there.
        m_pending = {{"kind", "console"}, {"workspace", Scratchpad}};
        m_hyprland->dispatch(QString("hl.dsp.exec_cmd(%1)").arg(Hyprland::quote("[workspace " + Scratchpad + " silent] " + command)));
    } else if (plan.value("empty").toBool()) {
        m_pending = {{"kind", "plain"}, {"workspace", plan.value("workspace")}};
        m_hyprland->dispatch(QString("hl.dsp.exec_cmd(%1)").arg(Hyprland::quote(command)));
    } else {
        // Dwindle puts the next window beside (or under) the focused one when
        // told which side; the ratio comes once the window is there.
        m_pending = {{"kind", "split"}, {"workspace", plan.value("workspace")}, {"fraction", qBound(0.2, fraction, 0.8)}};
        m_hyprland->dispatch(QString("hl.dsp.focus({ window = \"address:%1\" })").arg(plan.value("address").toString()));
        m_hyprland->dispatch(QString("hl.dsp.layout(\"preselect %1\")").arg(edge == "bottom" ? "d" : "r"));
        m_hyprland->dispatch(QString("hl.dsp.exec_cmd(%1)").arg(Hyprland::quote(command)));
    }
    m_pendingTimeout.start();
}

void Stage::onEvent(const QString &name, const QString &data)
{
    // Anything that can move a window moves the lines between them.
    static const QStringList layoutEvents{"openwindow",  "closewindow", "movewindowv2", "changefloatingmode",
                                          "fullscreen",  "workspacev2", "activespecial", "configreloaded",
                                          "monitoradded", "monitorremoved"};
    if (layoutEvents.contains(name))
        refreshDividers();
    if (name == "configreloaded") {
        refreshLook();
        refreshTouch();
        // A reload drops rules that aren't in the config.
        registerHiding();
        return;
    }
    if (name == "activespecial") {
        const bool open = data.section(',', 0, 0) == Scratchpad;
        if (open != m_consoleOpen) {
            m_consoleOpen = open;
            if (open)
                refreshConsole();
            emit consoleOpenChanged();
        }
        return;
    }
    if (name == "workspace") {
        emit workspaceShown(data);
        return;
    }
    if (name == "closewindow") {
        const QString address = "0x" + data.section(',', 0, 0);
        if (QTimer *timer = m_closing.take(address))
            timer->deleteLater();
        m_hidden.remove(address);
        emit windowClosed(address);
        return;
    }
    if (name == "openwindow" && !m_pending.isEmpty()) {
        const QString address = "0x" + data.section(',', 0, 0);
        const QString workspace = data.section(',', 1, 1);
        if (workspace != m_pending.value("workspace").toString())
            return;
        const QString kind = m_pending.value("kind").toString();
        qCDebug(lcStage) << "placed" << kind << address << "on" << workspace;
        if (kind == "split") {
            // The window that made room is the first of the pair, and the
            // ratio is twice its share.
            const qreal ratio = 2 * m_pending.value("fraction").toReal();
            m_hyprland->dispatch(QString("hl.dsp.focus({ window = \"address:%1\" })").arg(address));
            m_hyprland->dispatch(QString("hl.dsp.layout(\"splitratio %1 exact\")").arg(ratio, 0, 'f', 3));
        } else if (kind == "console") {
            openConsole();
        }
        finishPlacement(true);
    }
}

void Stage::finishPlacement(bool ok)
{
    if (m_pending.isEmpty())
        return;
    m_pending.clear();
    m_pendingTimeout.stop();
    emit placed(ok);
}

void Stage::openConsole()
{
    if (m_consoleOpen)
        return;
    // The finger already brought the console down, dimming the screen, so
    // Hyprland's own slide in and the fade of its dimming are switched off
    // for this one toggle: the overlay's dimming hands over to Hyprland's in
    // one frame, rather than both showing while Hyprland's fades in.
    withoutAnimation("fadeDim", [this] {
        withoutAnimation("specialWorkspaceIn", [this] { m_hyprland->dispatch("hl.dsp.workspace.toggle_special(\"scratchpad\")"); });
    }, "fade");
}

// The workspace next to this one on its monitor: the next higher one for
// fingers going left, as "m+1" would be, the next lower one going right.
// Past the last one with windows there is one more, empty, like a new page
// to open something on (Hyprland makes it once it is shown); never two.
int Stage::neighbour(const Monitor &m, const QString &direction) const
{
    return neighbourOf(m_hyprland->query("workspaces").array(), m.name, m.workspace, direction);
}

int Stage::neighbourOf(const QJsonArray &workspaces, const QString &monitor, int current, const QString &direction)
{
    int next = 0;
    int windows = 0;
    QList<int> taken;
    for (const QJsonValue &value : workspaces) {
        const QJsonObject o = value.toObject();
        const int id = o.value("id").toInt();
        taken << id;
        if (id <= 0 || o.value("monitor").toString() != monitor)
            continue;
        if (id == current)
            windows = o.value("windows").toInt();
        if (direction == "left" ? id > current && (!next || id < next) : id < current && id > next)
            next = id;
    }
    if (!next && direction == "left" && windows > 0 && current < LastWorkspace && !taken.contains(current + 1))
        next = current + 1;
    return next;
}

// One picture per window and hand: the slide uses the ones a third finger
// already asked for, or the window a pull started on.
int Stage::pictureOf(const QVariantMap &window)
{
    const QString address = window["address"].toString();
    if (!m_pictures.contains(address))
        m_pictures.insert(address, m_capture->captureWindow(address, window["appClass"].toString(), window["title"].toString()));
    return m_pictures.value(address);
}

QVariantMap Stage::beginDivider(const QVariantMap &divider)
{
    m_pictures.clear();
    const Monitor m = monitor();
    const QStringList before = divider["before"].toStringList(), after = divider["after"].toStringList();
    QVariantList beforeWindows, afterWindows;
    for (const QVariant &v : windowsOn(m.special.isEmpty() ? m.workspaceName : m.special, m.logical.topLeft())) {
        QVariantMap w = v.toMap();
        const QString address = w["address"].toString();
        if (!before.contains(address) && !after.contains(address))
            continue;
        w["snapshot"] = pictureOf(w);
        (before.contains(address) ? beforeWindows : afterWindows) << w;
    }
    qCDebug(lcStage) << "divider held" << divider["at"] << "between" << before << "and" << after;
    return {{"edge", "divider"},          {"divider", divider},
            {"before", beforeWindows},    {"after", afterWindows},
            {"usable", rectMap(m.usable.translated(-m.logical.topLeft()))},
            {"inConsole", !m.special.isEmpty()}};
}

// Hyprland moves a divider by the difference between the size a window
// beside it is given and the size it has, whichever side the window is on
// (tried in a nested Hyprland). A window whose far side is the room's edge
// touches only this divider, so that is the one that moves.
void Stage::resizeDivider(const QVariantMap &divider, qreal fraction)
{
    const bool vertical = divider["vertical"].toBool();
    const qreal start = divider["start"].toReal(), end = divider["end"].toReal();
    const qreal shift = start + fraction * (end - start) - divider["at"].toReal();
    const Monitor m = monitor();
    const QVariantList windows = windowsOn(m.special.isEmpty() ? m.workspaceName : m.special, m.logical.topLeft());
    QVariantMap chosen;
    for (const QVariant &v : windows) {
        const QVariantMap w = v.toMap();
        const qreal lo = vertical ? w["x"].toReal() : w["y"].toReal();
        const qreal hi = lo + (vertical ? w["w"].toReal() : w["h"].toReal());
        const bool before = divider["before"].toStringList().contains(w["address"].toString());
        const bool after = divider["after"].toStringList().contains(w["address"].toString());
        if ((before && qAbs(lo - start) < 2) || (after && qAbs(hi - end) < 2)) {
            chosen = w;
            break;
        }
        if ((before || after) && chosen.isEmpty())
            chosen = w;
    }
    if (chosen.isEmpty() || qAbs(shift) < 1)
        return;
    const qreal width = chosen["w"].toReal() + (vertical ? shift : 0);
    const qreal height = chosen["h"].toReal() + (vertical ? 0 : shift);
    qCDebug(lcStage) << "divider to" << fraction << "by" << shift << "through" << chosen["address"];
    // The pictures already show the new sizes; Hyprland needn't animate there.
    withoutAnimation("windows", [&] {
        m_hyprland->dispatch(QString("hl.dsp.window.resize({ x = %1, y = %2, window = \"address:%3\" })")
                                 .arg(qRound(width))
                                 .arg(qRound(height))
                                 .arg(chosen["address"].toString()));
    });
    refreshDividers();
}

// The window to size for the divider of `plan` to be at `line`, chosen as
// resizeDivider chooses it, and sized from where it was when the finger took
// hold, so steps don't add up.
void Stage::sizeDivider(const QVariantMap &plan, qreal line)
{
    const QVariantMap divider = plan["divider"].toMap();
    const bool vertical = divider["vertical"].toBool();
    const qreal start = divider["start"].toReal(), end = divider["end"].toReal();
    // One whose far side is the room's edge touches only this divider.
    QVariantMap chosen, first;
    for (const QString &side : {QString("before"), QString("after")}) {
        for (const QVariant &v : plan[side].toList()) {
            const QVariantMap w = v.toMap();
            const qreal lo = vertical ? w["x"].toReal() : w["y"].toReal();
            const qreal hi = lo + (vertical ? w["w"].toReal() : w["h"].toReal());
            if (first.isEmpty())
                first = w;
            if (chosen.isEmpty() && ((side == "before" && qAbs(lo - start) < 2) || (side == "after" && qAbs(hi - end) < 2)))
                chosen = w;
        }
    }
    if (chosen.isEmpty())
        chosen = first;
    if (chosen.isEmpty())
        return;
    const qreal shift = line - divider["at"].toReal();
    const qreal width = chosen["w"].toReal() + (vertical ? shift : 0);
    const qreal height = chosen["h"].toReal() + (vertical ? 0 : shift);
    m_hyprland->dispatch(QString("hl.dsp.window.resize({ x = %1, y = %2, window = \"address:%3\" })")
                             .arg(qRound(qMax<qreal>(1, width)))
                             .arg(qRound(qMax<qreal>(1, height)))
                             .arg(chosen["address"].toString()));
}

void Stage::moveDivider(const QVariantMap &plan, qreal line)
{
    if (m_liveQuiet.isEmpty()) {
        // Animated, each step would trail behind the finger.
        for (const QString &side : {QString("before"), QString("after")}) {
            for (const QVariant &v : plan[side].toList()) {
                const QString address = v.toMap()["address"].toString();
                const bool still = m_hyprland->query(QString("getprop address:%1 no_anim").arg(address).toUtf8())
                                       .object()
                                       .value("no_anim")
                                       .toBool();
                m_liveQuiet.insert(address, still);
                if (!still)
                    m_hyprland->dispatch(
                        QString("hl.dsp.window.set_prop({ prop = \"no_anim\", value = \"1\", window = \"address:%1\" })").arg(address));
            }
        }
    }
    m_livePlan = plan;
    m_liveLine = line;
    if (!m_liveStep.isActive())
        stepDivider();
}

void Stage::stepDivider()
{
    if (m_liveLine < 0 || m_livePlan.isEmpty())
        return;
    sizeDivider(m_livePlan, m_liveLine);
    m_liveLine = -1;
    m_liveStep.start();
}

void Stage::settleDivider(const QVariantMap &plan, qreal line)
{
    m_liveStep.stop();
    m_liveLine = -1;
    m_livePlan.clear();
    for (auto it = m_liveQuiet.cbegin(); it != m_liveQuiet.cend(); ++it)
        if (!it.value())
            m_hyprland->dispatch(
                QString("hl.dsp.window.set_prop({ prop = \"no_anim\", value = \"0\", window = \"address:%1\" })").arg(it.key()));
    m_liveQuiet.clear();
    if (line >= 0)
        sizeDivider(plan, line);
    qCDebug(lcStage) << "divider settled live at" << line;
    refreshDividers();
}

void Stage::pushOff(const QStringList &addresses)
{
    const Monitor m = monitor();
    QList<int> taken;
    for (const QJsonValue &value : m_hyprland->query("workspaces").array())
        taken << value.toObject().value("id").toInt();
    int empty = 0;
    for (int id = 1; id <= LastWorkspace && !empty; ++id)
        if (!taken.contains(id))
            empty = id;
    if (!empty)
        return;
    qCDebug(lcStage) << "pushed off" << addresses << "to workspace" << empty;
    withoutAnimation("windows", [&] {
        for (const QString &address : addresses)
            m_hyprland->dispatch(QString("hl.dsp.window.move({ workspace = \"%1\", follow = false, window = \"address:%2\" })")
                                     .arg(empty)
                                     .arg(address));
    });
    refreshDividers();
}

QVariantList Stage::workspaceTiles(const QJsonArray &workspaces, const QJsonArray &clients, const QString &monitor,
                                   int current)
{
    QMap<int, QStringList> apps;
    for (const QJsonValue &value : workspaces) {
        const QJsonObject o = value.toObject();
        const int id = o.value("id").toInt();
        if (id >= 1 && id <= LastWorkspace && (monitor.isEmpty() || o.value("monitor").toString() == monitor))
            apps[id];
    }
    for (const QJsonValue &value : clients) {
        const QJsonObject o = value.toObject();
        const int id = o.value("workspace").toObject().value("id").toInt();
        const QString appClass = o.value("class").toString();
        if (apps.contains(id) && o.value("mapped").toBool() && !appClass.isEmpty() && !apps[id].contains(appClass))
            apps[id] << appClass;
    }
    QVariantList tiles;
    for (auto it = apps.cbegin(); it != apps.cend(); ++it)
        tiles << QVariantMap{{"id", it.key()}, {"current", it.key() == current}, {"apps", it.value()}};
    // Workspaces on another monitor are taken too, so the empty one is
    // looked for among all of them.
    QList<int> taken;
    for (const QJsonValue &value : workspaces)
        taken << value.toObject().value("id").toInt();
    for (int id = 1; id <= LastWorkspace; ++id)
        if (!taken.contains(id)) {
            tiles << QVariantMap{{"id", id}, {"empty", true}, {"current", false}, {"apps", QStringList()}};
            break;
        }
    return tiles;
}

QVariantMap Stage::grabPlan()
{
    QVariantMap plan = m_handPlan;
    plan["edge"] = "grab";
    const Monitor m = monitor();
    // A floating or maximized window has no tiled neighbours to go beside;
    // it can still go to another workspace.
    QVariantList others;
    if (!plan["floating"].toBool() && !plan["fullscreen"].toBool())
        for (const QVariant &v : visibleWindows(m)) {
            const QVariantMap w = v.toMap();
            if (w["address"] != plan["address"] && w["workspace"] == plan["workspace"] && !w["floating"].toBool()
                && !w["fullscreen"].toBool()) {
                // Pictured, so they can make room as the window comes over them.
                QVariantMap other = w;
                other["snapshot"] = pictureOf(w);
                others << other;
            }
        }
    plan["others"] = others;
    plan["workspaces"] =
        workspaceTiles(m_hyprland->query("workspaces").array(), m_hyprland->query("clients").array(), m.name, m.workspace);
    const QRectF usable = m.usable.translated(-m.logical.topLeft());
    plan["usable"] = QVariantMap{{"x", usable.x()}, {"y", usable.y()}, {"w", usable.width()}, {"h", usable.height()}};
    return plan;
}

// The hand has rested on its window for HoldMs: it lifts it, unless it has
// moved on to something else meanwhile.
void Stage::startGrab()
{
    if (m_handPlan.isEmpty() || m_fingers.fingers() != 2 || !m_fingers.resting(HoldSlack) || !m_fingers.hold())
        return;
    m_grabbing = true;
    const QVariantMap plan = grabPlan();
    qCDebug(lcStage) << "grabbed" << plan["appClass"] << plan["address"] << "beside" << plan["others"].toList().size()
                     << "workspaces" << plan["workspaces"].toList().size();
    startTiming();
    emit grabStarted(plan, m_fingers.point());
}

void Stage::placeWindow(const QString &address, const QString &target, const QString &side)
{
    QString workspace;
    for (const QJsonValue &value : m_hyprland->query("clients").array()) {
        const QJsonObject o = value.toObject();
        if (o.value("address").toString() == address) {
            const QJsonObject ws = o.value("workspace").toObject();
            workspace = ws.value("id").toInt() > 0 ? QString::number(ws.value("id").toInt()) : ws.value("name").toString();
        }
    }
    static const QHash<QString, QString> directions{{"left", "l"}, {"right", "r"}, {"top", "u"}, {"bottom", "d"}};
    if (workspace.isEmpty() || !directions.contains(side))
        return;
    qCDebug(lcStage) << "placing" << address << side << "of" << target << "on" << workspace;
    // Dwindle puts a window coming onto a workspace beside the focused one,
    // on the side preselected: so the window steps out to a workspace no one
    // sees and comes back in beside its target.
    // The pictures already show the windows where they go, so Hyprland puts
    // them there at once, under the pictures.
    const QString window = QString("window = \"address:%1\"").arg(address);
    withoutAnimation("windows", [&] {
        m_hyprland->dispatch(QString("hl.dsp.window.move({ workspace = \"special:%1\", follow = false, %2 })").arg(GrabAside, window));
        m_hyprland->dispatch(QString("hl.dsp.focus({ window = \"address:%1\" })").arg(target));
        m_hyprland->dispatch(QString("hl.dsp.layout(\"preselect %1\")").arg(directions[side]));
        m_hyprland->dispatch(QString("hl.dsp.window.move({ workspace = \"%1\", follow = false, %2 })").arg(workspace, window));
        m_hyprland->dispatch(QString("hl.dsp.focus({ %1 })").arg(window));
    });
    refreshDividers();
}

void Stage::sendToWorkspace(const QString &address, int id)
{
    if (id <= 0) {
        QList<int> taken;
        for (const QJsonValue &value : m_hyprland->query("workspaces").array())
            taken << value.toObject().value("id").toInt();
        for (int i = 1; i <= LastWorkspace && id <= 0; ++i)
            if (!taken.contains(i))
                id = i;
    }
    if (id <= 0)
        return;
    qCDebug(lcStage) << "sent" << address << "to workspace" << id;
    withoutAnimation("windows", [&] {
        m_hyprland->dispatch(
            QString("hl.dsp.window.move({ workspace = \"%1\", follow = false, window = \"address:%2\" })").arg(id).arg(address));
    });
    refreshDividers();
}

void Stage::prefetchSlide()
{
    const Monitor m = monitor();
    if (!m.ok || !m.special.isEmpty())
        return;
    const QList<int> workspaces{m.workspace, neighbour(m, "left"), neighbour(m, "right")};
    for (const QJsonValue &value : m_hyprland->query("clients").array()) {
        const QJsonObject o = value.toObject();
        const int workspace = o.value("workspace").toObject().value("id").toInt();
        if (workspace > 0 && workspaces.contains(workspace) && o.value("mapped").toBool() && !o.value("hidden").toBool()
            && !o.value("pinned").toBool())
            pictureOf({{"address", o.value("address").toString()}, {"appClass", o.value("class").toString()},
                       {"title", o.value("title").toString()}});
    }
    qCDebug(lcStage) << "pictures ahead of a slide" << m_pictures.size();
}

// Runs `change` with one of Hyprland's animations switched off, and puts it
// back the way it was a moment later.
void Stage::withoutAnimation(const QString &leaf, const std::function<void()> &change, const QString &inherits)
{
    QJsonObject animation, parent;
    for (const QJsonValue &v : m_hyprland->query("animations").array().at(0).toArray()) {
        const QString name = v.toObject().value("name").toString();
        if (name == leaf)
            animation = v.toObject();
        if (name == inherits)
            parent = v.toObject();
    }
    if (!animation.value("overridden").toBool() && !inherits.isEmpty())
        animation = parent;
    const bool silence = animation.value("overridden").toBool() && animation.value("enabled").toBool();
    if (silence)
        m_hyprland->eval(QString("hl.animation({ leaf = %1, enabled = false })").arg(Hyprland::quote(leaf)));
    change();
    if (silence) {
        const QString restore = QString("hl.animation({ leaf = %1, enabled = true, speed = %2, bezier = %3, style = %4 })")
                                    .arg(Hyprland::quote(leaf))
                                    .arg(animation.value("speed").toDouble())
                                    .arg(Hyprland::quote(animation.value("bezier").toString()),
                                         Hyprland::quote(animation.value("style").toString()));
        QTimer::singleShot(250, this, [this, restore] { m_hyprland->eval(restore); });
    }
}

void Stage::switchWorkspace(int id)
{
    // The fingers already slid the next workspace in.
    withoutAnimation("workspaces", [this, id] { m_hyprland->dispatch(QString("hl.dsp.focus({ workspace = \"%1\" })").arg(id)); });
}

// What three fingers slide: this workspace's windows and those of the one
// next to it on this monitor (the next higher one for fingers going left, as
// "m+1" would be), bottom first, each with a picture on its way.
QVariantMap Stage::slidePlan(const QString &direction)
{
    const Monitor m = monitor();
    if (!m.ok || !m.special.isEmpty())
        return {};
    const int next = neighbour(m, direction);
    const QPointF origin = m.logical.topLeft();
    const QJsonArray clients = m_hyprland->query("clients").array();
    auto windows = [&](int workspace) {
        QList<QVariantMap> list;
        for (const QJsonValue &value : clients) {
            const QJsonObject o = value.toObject();
            if (o.value("workspace").toObject().value("id").toInt() != workspace || !o.value("mapped").toBool()
                || o.value("hidden").toBool() || o.value("pinned").toBool())
                continue;
            const QJsonArray at = o.value("at").toArray(), size = o.value("size").toArray();
            list << QVariantMap{
                {"address", o.value("address").toString()},
                {"appClass", o.value("class").toString()},
                {"x", at.at(0).toDouble() - origin.x()},
                {"y", at.at(1).toDouble() - origin.y()},
                {"w", size.at(0).toDouble()},
                {"h", size.at(1).toDouble()},
                {"rank", o.value("floating").toBool() ? 2 : o.value("fullscreen").toInt() ? 1 : 0},
                {"focus", o.value("focusHistoryID").toInt(1000)},
                {"title", o.value("title").toString()},
            };
        }
        // Drawn bottom up: tiled, fullscreen, then floating ones, the last used on top.
        std::stable_sort(list.begin(), list.end(), [](const QVariantMap &a, const QVariantMap &b) {
            return a["rank"] != b["rank"] ? a["rank"].toInt() < b["rank"].toInt() : a["focus"].toInt() > b["focus"].toInt();
        });
        QVariantList out;
        int lowest = 1000;
        for (const QVariantMap &w : std::as_const(list))
            lowest = qMin(lowest, w["focus"].toInt());
        for (QVariantMap w : std::as_const(list)) {
            w["active"] = w["focus"].toInt() == lowest;
            w["snapshot"] = pictureOf(w);
            out << w;
        }
        return out;
    };
    QVariantMap plan{
        {"edge", "slide"},
        {"direction", direction},
        {"usable", rectMap(m.usable.translated(-origin))},
        {"current", windows(m.workspace)},
        {"next", next ? QVariant(next) : QVariant()},
    };
    if (next)
        plan["nextWindows"] = windows(next);
    qCDebug(lcStage) << "slide" << direction << "from" << m.workspace << "to" << next;
    return plan;
}

QVariantMap Stage::beginLift()
{
    const Monitor m = monitor();
    const QPointF origin = m.logical.topLeft();
    QVariantList windows = windowsOn(Scratchpad, origin);
    m_pictures.clear();
    for (QVariant &v : windows) {
        QVariantMap w = v.toMap();
        w["snapshot"] = pictureOf(w);
        v = w;
    }
    return {
        {"edge", "top"},
        {"allowed", true},
        {"empty", windows.isEmpty()},
        {"lift", true},
        {"console", rectMap(m_consoleRect)},
        {"windows", windows},
        {"usable", rectMap(m.usable.translated(-origin))},
    };
}

void Stage::hideConsole()
{
    // The overlay's pictures and dimming are already up in its place, so
    // Hyprland's slide out and the fade of its dimming are off for this one
    // toggle, as on the way in.
    if (m_consoleOpen)
        withoutAnimation("fadeDim", [this] {
            withoutAnimation("specialWorkspaceOut", [this] { m_hyprland->dispatch("hl.dsp.workspace.toggle_special(\"scratchpad\")"); });
        }, "fade");
}

void Stage::closeConsole()
{
    if (m_consoleOpen)
        m_hyprland->dispatch("hl.dsp.workspace.toggle_special(\"scratchpad\")");
}

void Stage::closeWindow(const QString &address)
{
    const QString window = QString("window = \"address:%1\"").arg(address);
    m_hyprland->dispatch(QString("hl.dsp.window.set_prop({ prop = \"no_anim\", value = \"1\", %1 })").arg(window));
    m_hyprland->dispatch(QString("hl.dsp.window.close({ %1 })").arg(window));
    if (m_closing.contains(address))
        return;
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, address, window] {
        if (QTimer *t = m_closing.take(address))
            t->deleteLater();
        m_hyprland->dispatch(QString("hl.dsp.window.set_prop({ prop = \"no_anim\", value = \"0\", %1 })").arg(window));
    });
    m_closing.insert(address, timer);
    timer->start(5000);
}

// Named, so registering it again (after a reload, or when stage restarts)
// replaces it rather than adding another.
void Stage::registerHiding()
{
    m_hyprland->eval(QString("hl.window_rule({ name = %1, match = { tag = %1 }, opacity = \"0 override 0 override\" })")
                         .arg(Hyprland::quote(HiddenTag)));
}

void Stage::hideWindow(const QString &address)
{
    if (m_hidden.contains(address))
        return;
    const QString window = QString("window = \"address:%1\"").arg(address);
    const bool still = m_hyprland->query(QString("getprop address:%1 no_anim").arg(address).toUtf8())
                           .object()
                           .value("no_anim")
                           .toBool();
    m_hidden.insert(address, still);
    // Hyprland fades a change of opacity in, and the window must be gone in
    // the frame the picture takes its place.
    if (!still)
        m_hyprland->dispatch(QString("hl.dsp.window.set_prop({ prop = \"no_anim\", value = \"1\", %1 })").arg(window));
    m_hyprland->dispatch(QString("hl.dsp.window.tag({ tag = \"+%1\", %2 })").arg(HiddenTag, window));
}

void Stage::showWindow(const QString &address)
{
    if (!m_hidden.contains(address))
        return;
    const bool still = m_hidden.take(address);
    const QString window = QString("window = \"address:%1\"").arg(address);
    m_hyprland->dispatch(QString("hl.dsp.window.tag({ tag = \"-%1\", %2 })").arg(HiddenTag, window));
    // A window asked to close keeps no_anim until it does (or the close gives up).
    if (!still && !m_closing.contains(address))
        m_hyprland->dispatch(QString("hl.dsp.window.set_prop({ prop = \"no_anim\", value = \"0\", %1 })").arg(window));
}

void Stage::swipe(const QString &direction)
{
    // Fingers going left bring the next workspace in from the right.
    if (direction == "left")
        m_hyprland->dispatch("hl.dsp.focus({ workspace = \"m+1\" })");
    else if (direction == "right")
        m_hyprland->dispatch("hl.dsp.focus({ workspace = \"m-1\" })");
    else if (direction == "up")
        m_hyprland->dispatch("hl.dsp.window.fullscreen({ mode = \"maximized\" })");
}

void Stage::setInputRegion(const QVariantList &rects)
{
    QList<QRect> region;
    for (const QVariant &v : rects) {
        const QRect r = v.toRectF().toAlignedRect();
        if (!r.isEmpty())
            region << r;
    }
    if (region == m_inputRects)
        return;
    m_inputRects = region;
    applyInputRegion(false);
}

void Stage::applyInputRegion(bool force)
{
    if (!m_window || m_inputRects.isEmpty())
        return;
    QRegion region;
    for (const QRect &r : std::as_const(m_inputRects))
        region += r;
    // Qt skips a mask equal to the one it last sent, even to a new surface,
    // so step through a different one first.
    if (force)
        m_window->setMask(QRegion(0, 0, 1, 1));
    m_window->setMask(region);
}

// How Hyprland puts the touch screen on its output: the turn it gives the
// touch device and the output's logical size. True when that changed.
bool Stage::refreshTouch()
{
    const int transform = qMax(0, m_hyprland->query("getoption input:touchdevice:transform").object().value("int").toInt());
    const QString output = m_hyprland->query("getoption input:touchdevice:output").object().value("str").toString();
    QSizeF size;
    for (const QJsonValue &value : m_hyprland->query("monitors").array()) {
        const QJsonObject o = value.toObject();
        if (o.value("name").toString() != output && !(size.isEmpty() && o.value("focused").toBool()))
            continue;
        const double scale = o.value("scale").toDouble(1);
        double width = o.value("width").toDouble(), height = o.value("height").toDouble();
        if (o.value("transform").toInt() % 2 == 1)
            std::swap(width, height);
        size = QSizeF(width / scale, height / scale);
        if (o.value("name").toString() == output)
            break;
    }
    if (size.isEmpty() || (transform == m_touchTransform && size == m_touchSize))
        return false;
    m_touchTransform = transform;
    m_touchSize = size;
    qCDebug(lcStage) << "touch screen turned" << transform << size;
    return true;
}

// Fingers are only put on the screen, not asked about: the turn is checked
// when a hand forms, and if the screen turned since, the fingers down are
// told again with the new turn.
void Stage::touch(Touchscreen::Kind kind, int slot, const QPointF &panel, qreal ms)
{
    if (m_touchSize.isEmpty())
        return;
    QList<Fingers::Event> events;
    const QPointF at = Fingers::map(panel, m_touchTransform, m_touchSize);
    switch (kind) {
    case Touchscreen::Down:
        qCDebug(lcStage) << "finger" << slot << "at" << at;
        m_touches.insert(slot, Touch{panel, panel, ms});
        events = m_fingers.down(slot, at, ms);
        // One finger alone may become a long press (not on stage's own strips); a second one makes a hand.
        handlePress(m_longPress.down(slot, at, m_touches.size() == 1 && m_longPressOn && m_pointer.isAvailable(),
                                     std::any_of(m_inputRects.cbegin(), m_inputRects.cend(),
                                                 [&at](const QRect &r) { return r.contains(at.toPoint()); })));
        break;
    case Touchscreen::Move:
        // Held until the panel's frame, when every finger that moved has.
        if (auto it = m_touches.find(slot); it != m_touches.end())
            it->now = panel;
        m_moved.insert(slot, {at, ms});
        return;
    case Touchscreen::Up:
        handlePress(m_longPress.up(slot));
        m_touches.remove(slot);
        m_moved.remove(slot);
        events = m_fingers.up(slot, ms);
        break;
    case Touchscreen::Frame: {
        // Each finger moves the hand's middle; seen one at a time, the hand
        // would go half a step and then the rest. Only where the whole hand
        // has come counts.
        for (auto it = m_moved.cbegin(); it != m_moved.cend(); ++it)
            events += m_fingers.move(it.key(), it->first, it->second);
        for (auto it = m_moved.cbegin(); it != m_moved.cend(); ++it)
            handlePress(m_longPress.move(it.key(), it->first));
        m_moved.clear();
        for (const auto type : {Fingers::Event::PullMoved, Fingers::Event::SlideMoved, Fingers::Event::GrabMoved}) {
            const auto last = std::find_if(events.crbegin(), events.crend(), [type](const Fingers::Event &e) { return e.type == type; });
            if (last != events.crend()) {
                const Fingers::Event keep = *last;
                events.removeIf([type](const Fingers::Event &e) { return e.type == type; });
                events << keep;
            }
        }
        break;
    }
    }
    handle(events);
}

void Stage::handlePress(LongPress::Result result)
{
    switch (result) {
    case LongPress::Started:
        m_pressTimer.start();
        emit pressStarted(m_longPress.at(), m_longPressMs);
        break;
    case LongPress::Canceled:
        m_pressTimer.stop();
        emit pressCanceled();
        break;
    case LongPress::Click:
        // The armed finger lifted: the click, once the app has had the end of its touch.
        QTimer::singleShot(PressClickDelayMs, this, &Stage::firePress);
        break;
    case LongPress::Nothing:
        break;
    }
}

// The finger has rested long enough: armed, if it is on a window of an app that wants a right click there. Hyprland
// is asked only now, not for every touch.
void Stage::armPress()
{
    const QPointF at = m_longPress.at();
    auto refuse = [this](const QString &why) {
        qCDebug(lcStage) << "long press:" << why;
        m_longPress.cancel();
        emit pressCanceled();
    };
    if (m_hyprland->query("locked").object().value("locked").toBool())
        return refuse("the session is locked");
    const Monitor m = monitor();
    if (!m.ok)
        return refuse("no monitor");
    const QVariantMap window = windowAt(at.x(), at.y(), visibleWindows(m), m.special);
    if (window.isEmpty())
        return refuse("no window there");
    const QString appClass = window["appClass"].toString();
    if (QRegularExpression("^(?:" + m_longPressSkip + ")$").match(appClass).hasMatch())
        return refuse(appClass + " opens its own menu");
    if (overKeyboard(at))
        return refuse("on the keyboard");
    // Absolute motion is a fraction of every output together.
    QRectF layout;
    for (const QJsonValue &value : m_hyprland->query("monitors").array()) {
        const QJsonObject o = value.toObject();
        const double scale = o.value("scale").toDouble(1);
        double width = o.value("width").toDouble(), height = o.value("height").toDouble();
        if (o.value("transform").toInt() % 2 == 1)
            std::swap(width, height);
        layout |= QRectF(o.value("x").toDouble(), o.value("y").toDouble(), width / scale, height / scale);
    }
    if (!m_longPress.arm())
        return;
    m_pressAt = at;
    m_pressGlobal = at + m.logical.topLeft();
    m_pressLayout = layout;
    qCDebug(lcStage) << "long press: armed on" << appClass << "at" << at;
    emit pressArmed(at);
}

// The armed finger lifted, and the app has had the end of its touch: the right click.
void Stage::firePress()
{
    if (!m_pointer.rightClick(m_pressGlobal, m_pressLayout)) {
        qCDebug(lcStage) << "long press: the click didn't go";
        emit pressCanceled();
        return;
    }
    qCDebug(lcStage) << "long press: right click at" << m_pressAt;
    emit pressFired(m_pressAt);
}

// floatkey's keyboard is where a long press means its accents. A floatkey that can't say where its keyboard is
// counts as covering the screen while it is shown.
bool Stage::overKeyboard(const QPointF &at) const
{
    const QString socket = Ipc::socketPath().replace("/stage-", "/floatkey-");
    QString reply;
    if (Ipc::send(socket, "region", &reply, 150) && reply.startsWith("ok ")) {
        const QStringList parts = reply.split(' ');
        if (parts.size() == 5)
            return QRectF(parts[1].toDouble(), parts[2].toDouble(), parts[3].toDouble(), parts[4].toDouble()).contains(at);
        return false;   // "ok none": hidden
    }
    return Ipc::send(socket, "status", &reply, 150) && reply.contains(" shown ");
}

void Stage::tellBars(const QByteArray &line)
{
    for (QLocalSocket *bar : std::as_const(m_bars)) {
        bar->write(line + "\n");
        bar->flush();
    }
}

void Stage::handle(const QList<Fingers::Event> &events)
{
    for (const Fingers::Event &e : events) {
        switch (e.type) {
        case Fingers::Event::HandDown: {
            if (refreshTouch()) {
                QList<int> fingers = m_touches.keys();
                std::sort(fingers.begin(), fingers.end(), [this](int a, int b) { return m_touches[a].landed < m_touches[b].landed; });
                m_fingers.reset();
                QList<Fingers::Event> again;
                for (int slot : std::as_const(fingers))
                    again += m_fingers.down(slot, Fingers::map(m_touches[slot].start, m_touchTransform, m_touchSize),
                                            m_touches[slot].landed);
                for (int slot : std::as_const(fingers))
                    again += m_fingers.move(slot, Fingers::map(m_touches[slot].now, m_touchTransform, m_touchSize),
                                            m_touches[slot].landed);
                handle(again);
                return;
            }
            if (m_hand) {
                // A third finger: it may be about to slide the workspaces, so
                // the pictures for that start now rather than once it moves.
                // Three fingers never lift a window.
                m_holdTimer.stop();
                if (e.fingers == 3 && !m_locked)
                    prefetchSlide();
                break;
            }
            // Pictures from a gesture before (holding a line, say) show windows
            // as they were then.
            m_pictures.clear();
            m_locked = m_hyprland->query("locked").object().value("locked").toBool();
            const Monitor m = monitor();
            // On the bar (or just under it), a hand pulls the scratchpad down
            // rather than whatever window is there.
            m_topHand = !m_locked && m.ok && e.at.y() < m.usable.top() - m.logical.top() + TopHandSlack;
            if (m_topHand)
                m_topFrom = e.at;
            m_handPlan = m_locked || !m.ok || m_topHand ? QVariantMap()
                                                         : windowAt(e.at.x(), e.at.y(), visibleWindows(m), m.special);
            if (!m_handPlan.isEmpty()) {
                m_handPlan["fromX"] = e.at.x();
                m_handPlan["fromY"] = e.at.y();
                // Taken now, so the picture is there by the time the hand has moved.
                m_handPlan["snapshot"] = pictureOf(m_handPlan);
                // How see-through the window is, for its picture.
                const QString prop = m_handPlan["focus"].toInt() == 0 ? "opacity" : "opacity_inactive";
                m_handPlan["opacity"] = m_hyprland->query(QString("getprop address:%1 %2").arg(m_handPlan["address"].toString(), prop).toUtf8())
                                            .object()
                                            .value(prop)
                                            .toDouble(1);
            }
            qCDebug(lcStage) << "hand" << e.fingers << "at" << e.at << "locked" << m_locked << "on"
                             << m_handPlan.value("appClass") << m_handPlan.value("address");
            // Two fingers resting on a window lift it, once they have rested
            // long enough.
            if (e.fingers == 2 && !m_handPlan.isEmpty())
                m_holdTimer.start();
            m_hand = true;
            emit handChanged();
            // A finger of this hand on the bar may have started a drawer: the
            // bar lets go of it, as the hand is stage's.
            tellBars("hand down");
            break;
        }
        case Fingers::Event::PullStarted:
            m_holdTimer.stop();
            // Two fingers up take the scratchpad up, wherever they are on it
            // (a full-size one leaves no dimmed screen to tap); otherwise they
            // are the apps'.
            if (e.direction == "up") {
                if (m_consoleOpen && !m_locked) {
                    m_lifting = true;
                    qCDebug(lcStage) << "scratchpad pushed up by a hand";
                    emit liftStarted(e.at);
                }
                break;
            }
            if (m_topHand) {
                QVariantMap plan = beginEdge("top", e.at.x(), e.at.y());
                // From where the hand landed, so the scratchpad comes down as
                // far as the fingers have.
                plan["fromX"] = m_topFrom.x();
                plan["fromY"] = m_topFrom.y();
                m_topPulling = true;
                qCDebug(lcStage) << "scratchpad pulled by a hand" << plan.value("empty");
                emit topPulled(plan, e.at);
                break;
            }
            if (m_handPlan.isEmpty())
                break;
            m_pulling = true;
            startTiming();
            emit pullStarted(m_handPlan, e.at, e.ms);
            break;
        case Fingers::Event::PullMoved:
            if (m_lifting)
                emit liftMoved(e.at);
            if (m_topPulling)
                emit topMoved(e.at);
            if (m_pulling) {
                if (m_timing) {
                    QMutexLocker lock(&m_timingLock);
                    m_moveTimes << clockMs();
                }
                emit pullMoved(e.at, e.ms);
            }
            break;
        case Fingers::Event::PullEnded:
        case Fingers::Event::PullCanceled:
            if (m_lifting) {
                m_lifting = false;
                emit liftEnded(e.at, e.type == Fingers::Event::PullEnded);
                break;
            }
            if (m_topPulling) {
                m_topPulling = false;
                e.type == Fingers::Event::PullEnded ? emit topLetGo(e.at) : emit topReleased(e.at, false);
                break;
            }
            if (!m_pulling)
                break;
            m_pulling = false;
            qCDebug(lcStage) << (e.type == Fingers::Event::PullEnded ? "pull ended" : "pull canceled") << "at" << e.at;
            reportTiming("pull");
            e.type == Fingers::Event::PullEnded ? emit pullEnded(e.at, e.ms) : emit pullCanceled();
            break;
        case Fingers::Event::SlideStarted:
            if (m_locked)
                break;
            m_handPlan = slidePlan(e.direction);
            if (m_handPlan.isEmpty())
                break;
            m_sliding = true;
            startTiming();
            m_handPlan["fromX"] = e.at.x();
            m_handPlan["fromY"] = e.at.y();
            emit slideStarted(m_handPlan, e.at, e.ms);
            break;
        case Fingers::Event::SlideMoved:
            if (m_sliding) {
                if (m_timing) {
                    QMutexLocker lock(&m_timingLock);
                    m_moveTimes << clockMs();
                }
                emit slideMoved(e.at, e.ms);
            }
            break;
        case Fingers::Event::SlideEnded:
        case Fingers::Event::SlideCanceled:
            if (!m_sliding)
                break;
            m_sliding = false;
            reportTiming("slide");
            e.type == Fingers::Event::SlideEnded ? emit slideEnded(e.at, e.ms) : emit slideCanceled();
            break;
        case Fingers::Event::Pinched:
            qCDebug(lcStage) << "pinched" << e.direction << "locked" << m_locked;
            if (!m_locked) {
                sendClipboardKeys(e.direction == "in" ? "copy" : "paste");
                emit pinched(e.direction);
            }
            break;
        case Fingers::Event::GrabMoved:
            if (m_grabbing) {
                if (m_timing) {
                    QMutexLocker lock(&m_timingLock);
                    m_moveTimes << clockMs();
                }
                emit grabMoved(e.at);
            }
            break;
        case Fingers::Event::GrabEnded:
            if (!m_grabbing)
                break;
            m_grabbing = false;
            qCDebug(lcStage) << "grab ended at" << e.at;
            reportTiming("grab");
            emit grabEnded(e.at);
            break;
        case Fingers::Event::Swiped:
            qCDebug(lcStage) << "swiped" << e.direction << "locked" << m_locked;
            if (!m_locked)
                emit swiped(e.direction);
            break;
        case Fingers::Event::HandUp:
            m_holdTimer.stop();
            m_topHand = false;
            tellBars("hand up");
            m_handPlan.clear();
            m_pictures.clear();
            m_hand = false;
            emit handChanged();
            break;
        }
    }
}

// Fingers a hand's width apart, on slots no real finger uses.
static constexpr int SimulatedSlot = 100;

void Stage::simulatePull(const QPointF &from, const QPointF &to, int ms, int fingers, int restMs)
{
    if (m_simulatedFingers)
        simulateRelease();
    refreshTouch();
    m_simulatedFrom = from;
    m_simulatedTo = to;
    m_simulatedFingers = fingers;
    m_simulatedSteps = qMax(1, ms / 16);
    m_simulatedStep = 0;
    const qreal now = clockMs();
    for (int i = 0; i < fingers; ++i)
        touch(Touchscreen::Down, SimulatedSlot + i,
              Fingers::unmap(from + QPointF(i * 120, 0), m_touchTransform, m_touchSize), now + i * 10);
    m_simulated.setInterval(16);
    m_simulated.disconnect(this);
    connect(&m_simulated, &QTimer::timeout, this, [this] {
        m_simulatedStep += 1;
        const qreal t = qMin(1.0, qreal(m_simulatedStep) / m_simulatedSteps);
        const QPointF at = m_simulatedFrom + (m_simulatedTo - m_simulatedFrom) * t;
        const qreal now = clockMs();
        for (int i = 0; i < m_simulatedFingers; ++i)
            touch(Touchscreen::Move, SimulatedSlot + i,
                  Fingers::unmap(at + QPointF(i * 120, 0), m_touchTransform, m_touchSize), now);
        touch(Touchscreen::Frame, -1, {}, now);
        if (t >= 1)
            m_simulated.stop();
    });
    // A hand that rests first, to lift its window, moves once it has.
    if (restMs > 0)
        QTimer::singleShot(restMs, this, [this, fingers] {
            if (m_simulatedFingers == fingers && !m_simulated.isActive() && m_simulatedStep == 0)
                m_simulated.start();
        });
    else
        m_simulated.start();
}

// Three fingers around the middle of the screen, drawing in from far apart
// or spreading out from close together, then lifting.
void Stage::simulatePinch(bool in, int ms)
{
    if (m_simulatedFingers)
        simulateRelease();
    refreshTouch();
    const QPointF middle(m_touchSize.width() / 2, m_touchSize.height() / 2);
    const qreal from = in ? 200 : 45, to = in ? 45 : 220;
    auto at = [middle](int i, qreal r) {
        const qreal a = qDegreesToRadians(-90.0 + 120.0 * i);
        return middle + QPointF(std::cos(a), std::sin(a)) * r;
    };
    m_simulatedFingers = 3;
    m_simulatedSteps = qMax(1, ms / 16);
    m_simulatedStep = 0;
    const qreal now = clockMs();
    for (int i = 0; i < 3; ++i)
        touch(Touchscreen::Down, SimulatedSlot + i, Fingers::unmap(at(i, from), m_touchTransform, m_touchSize), now + i * 10);
    m_simulated.setInterval(16);
    m_simulated.disconnect(this);
    connect(&m_simulated, &QTimer::timeout, this, [this, at, from, to] {
        m_simulatedStep += 1;
        const qreal t = qMin(1.0, qreal(m_simulatedStep) / m_simulatedSteps);
        const qreal now = clockMs();
        for (int i = 0; i < 3; ++i)
            touch(Touchscreen::Move, SimulatedSlot + i,
                  Fingers::unmap(at(i, from + (to - from) * t), m_touchTransform, m_touchSize), now);
        touch(Touchscreen::Frame, -1, {}, now);
        if (t >= 1)
            simulateRelease();
    });
    m_simulated.start();
}

void Stage::simulateRelease()
{
    m_simulated.stop();
    const qreal now = clockMs();
    for (int i = 0; i < m_simulatedFingers; ++i)
        touch(Touchscreen::Up, SimulatedSlot + i, {}, now + i * 10);
    m_simulatedFingers = 0;
}

bool Stage::listen(const QString &socketPath, QString *error)
{
    QLocalServer::removeServer(socketPath);
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(socketPath)) {
        if (error)
            *error = m_server->errorString();
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, socket, [this, socket] {
                while (socket->canReadLine()) {
                    const QString line = QString::fromUtf8(socket->readLine()).trimmed();
                    if (line == "bar here") {
                        // The bar stays connected; it is there until it hangs up.
                        if (!m_bars.contains(socket)) {
                            m_bars.insert(socket);
                            connect(socket, &QObject::destroyed, this, [this, socket] {
                                m_bars.remove(socket);
                                if (m_bars.isEmpty())
                                    emit barConnectedChanged();
                            });
                            if (m_bars.size() == 1)
                                emit barConnectedChanged();
                        }
                        socket->write("ok\n");
                        continue;
                    }
                    socket->write(command(line).toUtf8() + '\n');
                }
                socket->flush();
            });
        }
    });
    return true;
}

// `stage launch firefox right 0.5` does what letting go on Firefox in the
// launcher would after a swipe in from the right edge, beside the focused
// window: handy for scripts, and for trying placement without a finger.
// floatkey sends them, as Omarchy's universal clipboard keys would: with
// shift in a terminal. Straight to the running one over its socket, so an
// older floatkey command on the PATH can't get in the way.
void Stage::sendClipboardKeys(const QString &action)
{
    const QString socket = Ipc::socketPath().replace("/stage-", "/floatkey-");
    QString reply;
    const bool sent = Ipc::send(socket, action, &reply, 500);
    qCDebug(lcStage) << "clipboard" << action << (sent ? reply : QString("no floatkey"));
}

// wl-paste runs `echo` for every new copy, so each line is one copy. Its
// first line is what was already on the clipboard. If it ever stops, it
// starts again.
void Stage::watchClipboard()
{
    const QString wlPaste = QStandardPaths::findExecutable("wl-paste");
    if (wlPaste.isEmpty())
        return;
    if (m_clipboardSince.isValid()) {
        m_clipboardSince.start();
        m_clipboardWatch.start(wlPaste, {"--watch", "echo"});
        return;
    }
    connect(&m_clipboardWatch, &QProcess::readyReadStandardOutput, this, [this] {
        const qsizetype lines = m_clipboardWatch.readAllStandardOutput().count('\n');
        const bool hushed = m_hushed.isValid() && m_hushed.elapsed() < 1500;
        if (lines > 0 && m_clipboardSince.elapsed() > 1500 && !hushed)
            emit copied();
    });
    connect(&m_clipboardWatch, &QProcess::finished, this, [this] {
        QTimer::singleShot(2000, this, &Stage::watchClipboard);
    });
    m_clipboardSince.start();
    m_clipboardWatch.start(wlPaste, {"--watch", "echo"});
}

QString Stage::command(const QString &line)
{
    const QStringList words = line.split(' ', Qt::SkipEmptyParts);
    const QString verb = words.value(0);
    // floatkey puts a clip back on the clipboard to paste it; that's no copy
    // to announce.
    if (verb == "hush") {
        m_hushed.start();
        return "ok";
    }
    if (verb == "resize") {
        // `resize live|pictures`: how a held divider resizes; on its own, which.
        const QString how = words.value(1);
        if (how == "live" || how == "pictures") {
            QSettings("stage", "stage").setValue("resize", how);
            if (m_liveResize != (how == "live")) {
                m_liveResize = how == "live";
                emit liveResizeChanged();
            }
        } else if (!how.isEmpty()) {
            return "error resize live|pictures";
        }
        return QString("ok %1").arg(m_liveResize ? "live" : "pictures");
    }
    if (verb == "status")
        return QString("ok console=%1 apps=%2").arg(m_consoleOpen ? "open" : "closed").arg(m_apps->rowCount());
    if (verb == "quit") {
        QMetaObject::invokeMethod(QCoreApplication::instance(), &QCoreApplication::quit, Qt::QueuedConnection);
        return "ok";
    }
    if (verb == "top") {
        // `top begin X Y`, `top follow DISTANCE`, `top end 1|0`: a finger on
        // the bar pulling the console down, as the bar sees it.
        const QString action = words.value(1);
        if (action == "begin") {
            m_topFrom = QPointF(words.value(2).toDouble(), words.value(3).toDouble());
            m_topDistance = 0;
            QVariantMap plan = beginEdge("top", m_topFrom.x(), m_topFrom.y());
            plan["fromX"] = m_topFrom.x();
            plan["fromY"] = m_topFrom.y();
            emit topPulled(plan, m_topFrom);
        } else if (action == "follow") {
            m_topDistance = words.value(2).toDouble();
            emit topMoved(m_topFrom + QPointF(0, m_topDistance));
        } else if (action == "end") {
            emit topReleased(m_topFrom + QPointF(0, m_topDistance), words.value(2) == "1");
        } else {
            return "error top begin|follow|end";
        }
        return "ok";
    }
    if (verb == "console") {
        words.value(1) == "close" ? closeConsole() : openConsole();
        return "ok";
    }
    if (verb == "simulate" && words.value(1) == "pinch") {
        simulatePinch(words.value(2) != "out", words.value(3, "300").toInt());
        return "ok pinch";
    }
    if (verb == "simulate" && m_window
        && (words.value(1) == "pull" || words.value(1) == "grab" || (words.value(1) == "release" && m_simulatedFingers))) {
        // `stage simulate pull 400 300 400 700 [ms] [fingers]`, then `release`:
        // a hand as the touch screen would report it. `grab` is two fingers
        // that rest long enough to lift the window first, then carry it.
        if (words.value(1) == "pull")
            simulatePull({words.value(2).toDouble(), words.value(3).toDouble()},
                         {words.value(4).toDouble(), words.value(5).toDouble()}, words.value(6, "400").toInt(),
                         qBound(2, words.value(7, "2").toInt(), 3));
        else if (words.value(1) == "grab")
            simulatePull({words.value(2).toDouble(), words.value(3).toDouble()},
                         {words.value(4).toDouble(), words.value(5).toDouble()}, words.value(6, "600").toInt(), 2,
                         HoldMs + 150);
        else
            simulateRelease();
        return "ok " + m_window->property("phase").toString();
    }
    if (verb == "simulate" && m_window) {
        // `stage simulate drag 1693 600 1273 600 [ms]`, then `release`,
        // `pick APP` or `cancel`: a gesture without a finger.
        QVariant phase;
        const QString action = words.value(1);
        QVariantList numbers;
        for (int i = 2; i < 7; ++i)
            numbers << (action == "pick" && i == 2 ? QVariant(words.value(i)) : QVariant(words.value(i, "0").toDouble()));
        if (action == "drag" && words.size() < 7)
            numbers[4] = 400.0;
        QMetaObject::invokeMethod(m_window, "simulate", Q_RETURN_ARG(QVariant, phase), Q_ARG(QVariant, action),
                                  Q_ARG(QVariant, numbers[0]), Q_ARG(QVariant, numbers[1]), Q_ARG(QVariant, numbers[2]),
                                  Q_ARG(QVariant, numbers[3]), Q_ARG(QVariant, numbers[4]));
        return "ok " + phase.toString();
    }
    if (verb == "launch") {
        const QString appId = words.value(1);
        const QString where = words.value(2, "right");
        if (!m_apps->find(appId))
            return "error unknown app: " + appId;
        const Monitor m = monitor();
        const QRectF usable = m.usable.translated(-m.logical.topLeft());
        const QString edge = where == "console" ? "top" : where;
        const QVariantMap plan = beginEdge(edge, edge == "right" ? usable.right() : usable.center().x(),
                                           edge == "bottom" ? usable.bottom() : usable.center().y());
        if (edge != "top" && !plan.value("allowed").toBool())
            return "error no room: " + plan.value("reason").toString();
        launch(plan, appId, words.value(3, "0.5").toDouble());
        return "ok";
    }
    return "error unknown command: " + verb;
}
