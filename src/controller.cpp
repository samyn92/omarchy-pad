#include "controller.h"

#include <LayerShellQt/window.h>
#include <QCoreApplication>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QRegion>
#include <QStandardPaths>
#include <QWindow>

#include "ipc.h"
#include "virtualkeyboard.h"

QStringList Controller::modes()
{
    return {"docked", "floating", "left", "right"};
}

QStringList Controller::supportedLayouts()
{
    return {"us", "de"};
}

QString Controller::defaultSettingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/floatkey/floatkey.conf";
}

// Keeps the layouts floatkey has keys for, in the order given, else US.
static QStringList usableLayouts(const QStringList &requested)
{
    QStringList layouts;
    for (QString code : requested) {
        code = code.trimmed().toLower();
        if (Controller::supportedLayouts().contains(code) && !layouts.contains(code))
            layouts << code;
    }
    return layouts.isEmpty() ? QStringList{"us"} : layouts;
}

Controller::Controller(const QString &settingsPath, const QStringList &systemLayouts, QObject *parent)
    : QObject(parent), m_settings(settingsPath, QSettings::IniFormat)
{
    const QString mode = m_settings.value("mode").toString();
    if (modes().contains(mode))
        m_mode = mode;
    m_floatingPosition = QPointF(qBound(0.0, m_settings.value("floating/x", m_floatingPosition.x()).toDouble(), 1.0),
                                 qBound(0.0, m_settings.value("floating/y", m_floatingPosition.y()).toDouble(), 1.0));
    m_layouts = usableLayouts(m_settings.contains("layouts") ? m_settings.value("layouts").toStringList()
                                                             : systemLayouts);
    m_layoutIndex = qBound(0, m_settings.value("layoutIndex", 0).toInt(), int(m_layouts.size()) - 1);
    m_dockedScale = qBound(0.6, m_settings.value("dockedScale", 1.0).toDouble(), 1.6);
    m_compactScale = qBound(0.6, m_settings.value("compactScale", 1.0).toDouble(), 1.8);
    m_autoCapitalize = m_settings.value("autoCapitalize", false).toBool();
    m_doubleSpacePeriod = m_settings.value("doubleSpacePeriod", true).toBool();
    m_recentEmoji = m_settings.value("recentEmoji").toStringList();
    m_autoShow = m_settings.value("autoShow", true).toBool();
    m_autoShowInTerminals = m_settings.value("autoShowInTerminals", true).toBool();
    m_autoShowWithKeyboard = m_settings.value("autoShowWithKeyboard", false).toBool();

    // Leaving one field for the next comes as a blur and a focus in quick
    // succession; waiting a moment keeps the keyboard from flickering.
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
        {"dock", "docked"}, {"float", "floating"}, {"left", "left"}, {"right", "right"},
    };

    if (verb == "show")
        show();
    else if (verb == "hide")
        hide();
    else if (verb == "toggle")
        toggle();
    else if (verb == "focus") {
        static const QStringList kinds = {"text", "terminal", "url", "email", "number", "password"};
        focusChanged(kinds.contains(argument) ? argument : "text");
    } else if (verb == "blur")
        focusChanged({});
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
    m_autoShown = !m_fieldType.isEmpty();
    setShown(true);
}

// Hidden by hand, it stays hidden until a field takes focus again.
void Controller::hide()
{
    m_autoHide.stop();
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
    const bool wanted = m_autoShow && (fieldType != "terminal" || m_autoShowInTerminals)
                        && (!m_hardwareKeyboard || m_autoShowWithKeyboard);
    if (!wanted || m_visible)
        return;
    m_autoShown = true;
    setShown(true);
}

void Controller::toggle()
{
    m_visible ? hide() : show();
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
