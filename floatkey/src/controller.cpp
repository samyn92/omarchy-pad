#include "controller.h"

#include <LayerShellQt/window.h>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QProcess>
#include <QRegion>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QWindow>

#include "ipc.h"
#include "xkblayouts.h"
#include "virtualkeyboard.h"

// QT_LOGGING_RULES="floatkey.focus.debug=true" shows every focus report from
// fcitx5's addon and what floatkey made of it.
Q_LOGGING_CATEGORY(lcFocus, "floatkey.focus", QtInfoMsg)

// How long after a tap a field's focus still counts as coming from it: the
// app takes focus as the finger lifts, and fcitx5's addon waits a moment.
static constexpr int TapReach = 600;
// How long a tap into the focused window waits for a window it may have
// opened, and how long after it one still takes the keyboard back down.
static constexpr int TapWaits = 300;
static constexpr int TapOpens = 1500;

QStringList Controller::modes()
{
    return {"docked", "floating", "orb"};
}

QStringList Controller::supportedLayouts()
{
    return {"us", "de"};
}

QString Controller::defaultSettingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/floatkey/floatkey.conf";
}

// Keeps the layouts floatkey can draw, in the order given, else US, with what
// XKB says each one's keys type.
static QStringList usableLayouts(const QStringList &requested, QVariantMap *keys)
{
    QStringList layouts;
    for (QString code : requested) {
        code = code.trimmed();
        if (code.isEmpty() || layouts.contains(code))
            continue;
        const QVariantMap read = XkbLayouts::read(code);
        if (read.isEmpty() && !Controller::supportedLayouts().contains(code))
            continue;
        layouts << code;
        if (!read.isEmpty())
            keys->insert(code, read);
    }
    if (layouts.isEmpty()) {
        layouts << "us";
        const QVariantMap read = XkbLayouts::read("us");
        if (!read.isEmpty())
            keys->insert("us", read);
    }
    return layouts;
}

Controller::Controller(const QString &settingsPath, const QStringList &systemLayouts, QObject *parent)
    : QObject(parent), m_settings(settingsPath, QSettings::IniFormat)
{
    const QString mode = m_settings.value("mode").toString();
    if (modes().contains(mode))
        m_mode = mode;
    // The corners are gone; a keyboard left in one floats.
    else if (mode == "left" || mode == "right")
        m_mode = "floating";
    m_floatingPosition = QPointF(qBound(0.0, m_settings.value("floating/x", m_floatingPosition.x()).toDouble(), 1.0),
                                 qBound(0.0, m_settings.value("floating/y", m_floatingPosition.y()).toDouble(), 1.0));
    m_orbPosition = QPointF(qBound(0.0, m_settings.value("orb/x", m_orbPosition.x()).toDouble(), 1.0),
                            qBound(0.0, m_settings.value("orb/y", m_orbPosition.y()).toDouble(), 1.0));
    m_layouts = usableLayouts(m_settings.contains("layouts") ? m_settings.value("layouts").toStringList()
                                                             : systemLayouts, &m_layoutKeys);
    m_layoutIndex = qBound(0, m_settings.value("layoutIndex", 0).toInt(), int(m_layouts.size()) - 1);
    m_dockedScale = qBound(0.6, m_settings.value("dockedScale", 1.0).toDouble(), 1.6);
    m_compactScale = qBound(0.6, m_settings.value("compactScale", 1.0).toDouble(), 1.8);
    m_autoCapitalize = m_settings.value("autoCapitalize", false).toBool();
    m_doubleSpacePeriod = m_settings.value("doubleSpacePeriod", true).toBool();
    m_recentEmoji = m_settings.value("recentEmoji").toStringList();
    m_autoShow = m_settings.value("autoShow", true).toBool();
    m_autoShowInTerminals = m_settings.value("autoShowInTerminals", true).toBool();
    m_autoShowWithKeyboard = m_settings.value("autoShowWithKeyboard", false).toBool();
    m_autoShowAfterTap = m_settings.value("autoShowAfterTap", true).toBool();

    // Leaving one field for the next comes as a blur and a focus in quick
    // succession; waiting a moment keeps the keyboard from flickering.
    m_tapCheck.setSingleShot(true);
    m_tapCheck.setInterval(TapWaits);
    connect(&m_tapCheck, &QTimer::timeout, this, &Controller::showForTap);
    m_autoHide.setSingleShot(true);
    m_autoHide.setInterval(250);
    connect(&m_autoHide, &QTimer::timeout, this, [this] {
        if (!m_autoShown)
            return;
        m_autoShown = false;
        setShown(false);
    });
}

Controller::~Controller()
{
    if (m_server)
        QFile::remove(m_socketPath);
}

void Controller::attach(QWindow *window, VirtualKeyboard *keyboard)
{
    m_window = window;
    m_keyboard = keyboard;
    if (!window)
        return;

    // One transparent overlay over the whole output. The keyboard is drawn
    // inside it, so it can follow the finger and change shape without the
    // compositor resizing the surface; only the keyboard's rectangle takes
    // input, and it never takes keyboard focus away from the app.
    m_layer = LayerShellQt::Window::get(window);
    m_layer->setScope("floatkey");
    m_layer->setLayer(LayerShellQt::Window::LayerOverlay);
    m_layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
                                                      | LayerShellQt::Window::AnchorLeft
                                                      | LayerShellQt::Window::AnchorRight));
    m_layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    m_layer->setActivateOnShow(false);
    m_layer->setWantsToBeOnActiveScreen(true);
    m_layer->setExclusiveEdge(LayerShellQt::Window::AnchorBottom);
    m_layer->setExclusiveZone(0);

    // Hiding destroys the surface; a new one starts without an input region.
    connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
        if (visible)
            applyInputRegion(true);
    });
}

bool Controller::listen(const QString &socketPath, QString *error)
{
    QLocalServer::removeServer(socketPath);
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(socketPath)) {
        if (error)
            *error = m_server->errorString();
        delete m_server;
        m_server = nullptr;
        return false;
    }
    m_socketPath = socketPath;
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server->nextPendingConnection()) {
            // fcitx5's addon writes its line and hangs up without waiting
            // for the reply, so read whatever is there before it goes.
            auto answer = [this, socket] {
                while (socket->canReadLine()) {
                    const QString line = QString::fromUtf8(socket->readLine()).trimmed();
                    if (socket->state() == QLocalSocket::ConnectedState)
                        socket->write(command(line).toUtf8() + '\n');
                    else
                        command(line);
                }
                socket->flush();
            };
            connect(socket, &QLocalSocket::readyRead, socket, answer);
            connect(socket, &QLocalSocket::disconnected, socket, [socket, answer] {
                answer();
                socket->deleteLater();
            });
            answer();
        }
    });
    return true;
}

QString Controller::command(const QString &line)
{
    const QString verb = line.section(' ', 0, 0).trimmed();
    const QString argument = Ipc::decode(line.section(' ', 1).trimmed());
    static const QHash<QString, QString> placements = {
        {"dock", "docked"}, {"float", "floating"}, {"orb", "orb"},
    };

    if (verb == "show")
        show();
    else if (verb == "hide")
        hide();
    else if (verb == "toggle")
        toggle();
    else if (verb == "focus") {
        // The field's kind, then anything the addon adds about where it is.
        static const QStringList kinds = {"text", "terminal", "url", "email", "number", "password"};
        const QString kind = argument.section(' ', 0, 0);
        qCDebug(lcFocus) << "focus" << argument << "visible" << m_visible << "auto" << m_autoShown;
        focusChanged(kinds.contains(kind) ? kind : "text");
    } else if (verb == "blur") {
        qCDebug(lcFocus) << "blur" << "visible" << m_visible << "auto" << m_autoShown;
        focusChanged({});
    }
    else if (placements.contains(verb)) {
        setMode(placements.value(verb));
        show();
    } else if (verb == "status")
        return QString("ok %1 %2 %3").arg(m_visible ? "shown" : "hidden", m_mode, m_layouts.value(m_layoutIndex));
    else if (verb == "type") {
        if (!m_keyboard || !m_keyboard->isReady())
            return "error typing is unavailable" + (m_keyboard ? ": " + m_keyboard->error() : QString());
        m_keyboard->typeText(argument);
    } else if (verb == "key") {
        // ctrl+alt+Left, super+space, ctrl+c, BackSpace...
        if (!m_keyboard || !m_keyboard->isReady())
            return "error typing is unavailable" + (m_keyboard ? ": " + m_keyboard->error() : QString());
        QStringList parts = argument.split('+');
        const QString key = parts.takeLast();
        int modifiers = 0;
        for (const QString &part : parts) {
            static const QHash<QString, int> names = {
                {"ctrl", VirtualKeyboard::Ctrl}, {"alt", VirtualKeyboard::Alt},
                {"super", VirtualKeyboard::Super}, {"shift", VirtualKeyboard::Shift},
            };
            if (!names.contains(part.toLower()))
                return "error unknown modifier: " + part;
            modifiers |= names.value(part.toLower());
        }
        if (key.toUcs4().size() == 1)
            m_keyboard->typeText((modifiers & VirtualKeyboard::Shift) ? key.toUpper() : key, modifiers);
        else if (!m_keyboard->tapKey(key, modifiers))
            return "error unknown key: " + key;
    } else if (verb == "clip") {
        emit clipRequested(argument);
    } else if (verb == "clipboard") {
        show();
        emit pageRequested("clipboard");
    } else if (editActions().contains(verb)) {
        if (!m_keyboard || !m_keyboard->isReady())
            return "error typing is unavailable" + (m_keyboard ? ": " + m_keyboard->error() : QString());
        edit(verb);
    } else if (verb == "quit")
        QMetaObject::invokeMethod(QCoreApplication::instance(), &QCoreApplication::quit, Qt::QueuedConnection);
    else
        return "error unknown command: " + verb;
    return "ok";
}

void Controller::setMode(const QString &mode)
{
    if (!modes().contains(mode) || mode == m_mode)
        return;
    m_mode = mode;
    m_settings.setValue("mode", mode);
    emit modeChanged();
    applyReservation();
}

void Controller::setFloatingPosition(const QPointF &position)
{
    const QPointF bounded(qBound(0.0, position.x(), 1.0), qBound(0.0, position.y(), 1.0));
    if (bounded == m_floatingPosition)
        return;
    m_floatingPosition = bounded;
    m_settings.setValue("floating/x", bounded.x());
    m_settings.setValue("floating/y", bounded.y());
    emit floatingPositionChanged();
}

void Controller::setOrbPosition(const QPointF &position)
{
    const QPointF bounded(qBound(0.0, position.x(), 1.0), qBound(0.0, position.y(), 1.0));
    if (bounded == m_orbPosition)
        return;
    m_orbPosition = bounded;
    m_settings.setValue("orb/x", bounded.x());
    m_settings.setValue("orb/y", bounded.y());
    emit orbPositionChanged();
}

void Controller::setLayoutIndex(int index)
{
    index = qBound(0, index, int(m_layouts.size()) - 1);
    if (index == m_layoutIndex)
        return;
    m_layoutIndex = index;
    m_settings.setValue("layoutIndex", index);
    emit layoutIndexChanged();
}

void Controller::setDockedHeight(int height)
{
    if (height == m_dockedHeight)
        return;
    m_dockedHeight = height;
    emit dockedHeightChanged();
    applyReservation();
}

// Shown by hand while a field has focus, the keyboard belongs to that field
// and goes with it; shown with nothing focused, it stays until hidden.
void Controller::show()
{
    m_autoHide.stop();
    m_tapCheck.stop();
    m_shownForTap = false;
    m_autoShown = !m_fieldType.isEmpty();
    setShown(true);
}

// Hidden by hand, it stays hidden until a field takes focus again.
void Controller::hide()
{
    m_autoHide.stop();
    m_tapCheck.stop();
    m_shownForTap = false;
    m_autoShown = false;
    setShown(false);
}

void Controller::setShown(bool shown)
{
    if (shown == m_visible)
        return;
    m_visible = shown;
    emit visibleChanged();
    applyReservation();
}

// From fcitx5's floatkey addon: a field took focus, or none has it any more.
void Controller::focusChanged(const QString &fieldType)
{
    if (fieldType != m_fieldType) {
        m_fieldType = fieldType;
        emit fieldTypeChanged();
    }
    if (fieldType.isEmpty()) {
        if (m_autoShown)
            m_autoHide.start();
        return;
    }
    m_autoHide.stop();
    if (!wantedFor(fieldType) || m_visible)
        return;
    if (m_windowAt && m_autoShowAfterTap) {
        // A finger still down decides when it lifts; otherwise the focus
        // must come right after a tap, into the window tapped.
        const bool justTapped = m_sinceTap.isValid() && m_sinceTap.elapsed() < TapReach;
        qCDebug(lcFocus) << "focus after a tap?" << justTapped << "touching" << m_touching;
        if (m_touching)
            m_focusedWhileTouching = true;
        if (m_touching || !justTapped || !tappedIntoFocus())
            return;
        m_tapCheck.stop();
    }
    m_autoShown = true;
    setShown(true);
}

bool Controller::wantedFor(const QString &fieldType) const
{
    return m_autoShow && (fieldType != "terminal" || m_autoShowInTerminals)
           && (!m_hardwareKeyboard || m_autoShowWithKeyboard);
}

void Controller::setTouchScreen(std::function<QString(const QPointF &panel)> windowAt)
{
    m_windowAt = std::move(windowAt);
}

void Controller::touchStarted()
{
    m_touching = true;
    // A touch that starts on the keyboard, such as its hide button, is for
    // the keyboard, never one that brings it back.
    m_tapCounts = !m_visible;
    m_focusedWhileTouching = false;
    m_shownForTap = false;
    m_tapCheck.stop();
    m_sinceTap.invalidate();
    m_newWindows.clear();
}

void Controller::touchEnded(bool tap, const QPointF &panel)
{
    m_touching = false;
    if (!tap || !m_tapCounts)
        return;
    m_tapAt = panel;
    m_sinceTap.start();
    if (m_fieldType.isEmpty() || m_visible || !wantedFor(m_fieldType) || !m_windowAt || !m_autoShowAfterTap)
        return;
    // The tap moved focus to this field: up at once. A tap into the window
    // that had focus all along may instead have been on something over it,
    // a launcher's icon say, so wait a moment for the window it opens.
    if (m_focusedWhileTouching)
        showForTap();
    else
        m_tapCheck.start();
}

void Controller::showForTap()
{
    const bool into = tappedIntoFocus();
    qCDebug(lcFocus) << "tap" << m_tapAt << "into the focused window" << into << "waited" << !m_focusedWhileTouching;
    if (!into || m_fieldType.isEmpty() || m_visible || !wantedFor(m_fieldType))
        return;
    m_autoHide.stop();
    m_autoShown = true;
    m_shownForTap = !m_focusedWhileTouching;
    setShown(true);
}

void Controller::windowOpened(const QString &address)
{
    m_newWindows.insert(address);
    m_tapCheck.stop();
    // An app slower than the wait: the tap was its launcher's, after all.
    if (m_shownForTap && m_autoShown && m_sinceTap.isValid() && m_sinceTap.elapsed() < TapOpens) {
        qCDebug(lcFocus) << "a window opened after the tap: down again";
        m_shownForTap = false;
        m_autoShown = false;
        setShown(false);
    }
}

// The tap landed in the focused window, and that window was there before
// the finger came down: not one the tap opened, say from a launcher.
bool Controller::tappedIntoFocus() const
{
    const QString window = m_windowAt(m_tapAt);
    return !window.isEmpty() && !m_newWindows.contains(window);
}

void Controller::toggle()
{
    m_visible ? hide() : show();
}

QStringList Controller::editActions()
{
    return {"copy", "cut", "paste", "undo", "redo", "selectAll"};
}

QPair<QString, int> Controller::editKeys(const QString &action, bool terminal)
{
    const int ctrl = VirtualKeyboard::Ctrl, shift = VirtualKeyboard::Shift;
    if (action == "copy")
        return terminal ? qMakePair(QString("C"), ctrl | shift) : qMakePair(QString("c"), ctrl);
    if (action == "paste")
        return terminal ? qMakePair(QString("V"), ctrl | shift) : qMakePair(QString("v"), ctrl);
    if (action == "cut")
        return {"x", ctrl};
    if (action == "undo")
        return {"z", ctrl};
    if (action == "redo")
        return {"Z", ctrl | shift};
    if (action == "selectAll")
        return {"a", ctrl};
    return {};
}

// fcitx5 says so only for a terminal that takes text input, so Hyprland is
// asked too: Omarchy tags its terminals "terminal", and the class list is that
// tag's own, for a session without Omarchy's rules.
bool Controller::inTerminal() const
{
    if (m_fieldType == "terminal")
        return true;
    if (qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"))
        return false;
    QProcess hyprctl;
    hyprctl.start("hyprctl", {"-j", "activewindow"});
    if (!hyprctl.waitForFinished(300)) {
        hyprctl.kill();
        return false;
    }
    const QJsonObject window = QJsonDocument::fromJson(hyprctl.readAllStandardOutput()).object();
    for (const QJsonValue &tag : window.value("tags").toArray())
        if (tag.toString().startsWith("terminal"))
            return true;
    static const QRegularExpression terminals(
        "^(Alacritty|kitty|com\\.mitchellh\\.ghostty|foot|org\\.codeberg\\.dnkl\\.foot|wezterm|org\\.omarchy\\..*|TUI\\..*)$");
    return terminals.match(window.value("class").toString()).hasMatch();
}

bool Controller::edit(const QString &action)
{
    if (!m_keyboard)
        return false;
    const QPair<QString, int> keys = editKeys(action, (action == "copy" || action == "paste") && inTerminal());
    if (!m_keyboard || keys.first.isEmpty())
        return false;
    m_keyboard->typeText(keys.first, keys.second);
    return true;
}

void Controller::noteEmoji(const QString &emoji)
{
    m_recentEmoji.removeAll(emoji);
    m_recentEmoji.prepend(emoji);
    while (m_recentEmoji.size() > 24)
        m_recentEmoji.removeLast();
    m_settings.setValue("recentEmoji", m_recentEmoji);
    emit recentEmojiChanged();
}

void Controller::setHardwareKeyboard(bool attached)
{
    if (attached == m_hardwareKeyboard)
        return;
    m_hardwareKeyboard = attached;
    if (attached && !m_autoShowWithKeyboard && m_autoShown) {
        m_autoHide.stop();
        m_autoShown = false;
        setShown(false);
    }
}

void Controller::applyReservation()
{
    if (!m_layer)
        return;
    m_layer->setExclusiveZone(m_visible && m_mode == "docked" ? m_dockedHeight : 0);
}

void Controller::setInputRegion(const QRectF &rect)
{
    const QRect region = rect.toAlignedRect();
    if (region.isEmpty() || region == m_inputRegion)
        return;
    m_inputRegion = region;
    applyInputRegion(false);
}

void Controller::applyInputRegion(bool force)
{
    if (!m_window || m_inputRegion.isEmpty())
        return;
    // Qt skips a mask equal to the one it last sent, even to a new surface,
    // so step through a different one first.
    if (force)
        m_window->setMask(QRegion(m_inputRegion.adjusted(0, 0, -1, 0)));
    m_window->setMask(QRegion(m_inputRegion));
}
