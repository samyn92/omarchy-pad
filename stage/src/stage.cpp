#include "stage.h"

#include <LayerShellQt/window.h>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QRegion>
#include <QWindow>
#include <cmath>
#include <limits>

#include "apps.h"
#include "capture.h"
#include "hyprland.h"
#include "images.h"
#include "ipc.h"

// QT_LOGGING_RULES="stage.debug=true" shows what the gestures see.
Q_LOGGING_CATEGORY(lcStage, "stage", QtInfoMsg)

static const QString Scratchpad = "special:scratchpad";

static QVariantMap rectMap(const QRectF &r)
{
    return {{"x", r.x()}, {"y", r.y()}, {"w", r.width()}, {"h", r.height()}};
}

Stage::Stage(Hyprland *hyprland, Capture *capture, Snapshots *snapshots, Apps *apps, QObject *parent)
    : QObject(parent), m_hyprland(hyprland), m_capture(capture), m_snapshots(snapshots), m_apps(apps)
{
    // An app that never shows a window (or shows it somewhere else) must not
    // leave the launcher's space hanging.
    m_pendingTimeout.setSingleShot(true);
    m_pendingTimeout.setInterval(10000);
    connect(&m_pendingTimeout, &QTimer::timeout, this, [this] { finishPlacement(false); });

    connect(m_hyprland, &Hyprland::hyprlandEvent, this, &Stage::onEvent);
    connect(m_capture, &Capture::captured, this, [this](int id, const QImage &image) {
        qCDebug(lcStage) << "snapshot" << id << image.size();
        if (!image.isNull())
            m_snapshots->insert(id, image);
        emit snapshotReady(id, !image.isNull());
    });

    refreshLook();
    const Monitor m = monitor();
    if (m.ok && m.special == Scratchpad) {
        m_consoleOpen = true;
        m_consoleRect = consoleBox(m.usable.translated(-m.logical.topLeft()),
                                   int(windowsOn(Scratchpad, m.logical.topLeft()).size()));
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
    connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
        if (visible)
            applyInputRegion(true);
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

QRectF Stage::consoleBox(const QRectF &usable, int windows)
{
    const qreal tall = std::floor(usable.height() * 0.5);
    const qreal wide = windows <= 1 ? qMin(usable.width(), tall * 2) : usable.width();
    return QRectF(usable.x() + std::floor((usable.width() - wide) / 2), usable.y(), wide, tall);
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
        QRectF box = consoleBox(usable, int(windows.size()));
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
    qCDebug(lcStage) << "edge" << result;
    return result;
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
    if (name == "configreloaded") {
        refreshLook();
        return;
    }
    if (name == "activespecial") {
        const bool open = data.section(',', 0, 0) == Scratchpad;
        if (open != m_consoleOpen) {
            m_consoleOpen = open;
            if (open) {
                const Monitor m = monitor();
                m_consoleRect = consoleBox(m.usable.translated(-m.logical.topLeft()),
                                           int(windowsOn(Scratchpad, m.logical.topLeft()).size()));
                const QVariantList windows = windowsOn(Scratchpad, m.logical.topLeft());
                if (!windows.isEmpty()) {
                    QRectF bounds;
                    for (const QVariant &v : windows) {
                        const QVariantMap w = v.toMap();
                        bounds = bounds.united(QRectF(w["x"].toReal(), w["y"].toReal(), w["w"].toReal(), w["h"].toReal()));
                    }
                    m_consoleRect = bounds;
                }
            }
            emit consoleOpenChanged();
        }
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
    // The finger already brought the console down, so Hyprland's own slide in
    // is switched off for this one toggle and put back the way it was.
    QJsonObject slide;
    for (const QJsonValue &v : m_hyprland->query("animations").array().at(0).toArray())
        if (v.toObject().value("name").toString() == "specialWorkspaceIn")
            slide = v.toObject();
    const bool silence = slide.value("overridden").toBool() && slide.value("enabled").toBool();
    if (silence)
        m_hyprland->eval("hl.animation({ leaf = \"specialWorkspaceIn\", enabled = false })");
    m_hyprland->dispatch("hl.dsp.workspace.toggle_special(\"scratchpad\")");
    if (silence) {
        const QString restore = QString("hl.animation({ leaf = \"specialWorkspaceIn\", enabled = true, speed = %1, "
                                        "bezier = %2, style = %3 })")
                                    .arg(slide.value("speed").toDouble())
                                    .arg(Hyprland::quote(slide.value("bezier").toString()),
                                         Hyprland::quote(slide.value("style").toString()));
        QTimer::singleShot(250, this, [this, restore] { m_hyprland->eval(restore); });
    }
}

void Stage::closeConsole()
{
    if (m_consoleOpen)
        m_hyprland->dispatch("hl.dsp.workspace.toggle_special(\"scratchpad\")");
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
QString Stage::command(const QString &line)
{
    const QStringList words = line.split(' ', Qt::SkipEmptyParts);
    const QString verb = words.value(0);
    if (verb == "status")
        return QString("ok console=%1 apps=%2").arg(m_consoleOpen ? "open" : "closed").arg(m_apps->rowCount());
    if (verb == "quit") {
        QMetaObject::invokeMethod(QCoreApplication::instance(), &QCoreApplication::quit, Qt::QueuedConnection);
        return "ok";
    }
    if (verb == "console") {
        words.value(1) == "close" ? closeConsole() : openConsole();
        return "ok";
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
