#include "hyprland.h"

#include <QDir>
#include <QTimer>

Hyprland::Hyprland(const QString &directory, QObject *parent) : QObject(parent)
{
    m_directory = directory;
    const QString signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    if (m_directory.isEmpty() && !signature.isEmpty())
        m_directory = qEnvironmentVariable("XDG_RUNTIME_DIR", QDir::tempPath()) + "/hypr/" + signature;
    if (m_directory.isEmpty())
        return;

    connect(&m_events, &QLocalSocket::readyRead, this, [this] {
        m_pending += m_events.readAll();
        int end;
        while ((end = m_pending.indexOf('\n')) >= 0) {
            const QString line = QString::fromUtf8(m_pending.left(end));
            m_pending.remove(0, end + 1);
            const int split = line.indexOf(">>");
            if (split > 0)
                emit hyprlandEvent(line.left(split), line.mid(split + 2));
        }
    });
    // Hyprland restarting (or a hiccup) drops the stream; pick it up again.
    connect(&m_events, &QLocalSocket::disconnected, this,
            [this] { QTimer::singleShot(1000, this, &Hyprland::connectEvents); });
    connectEvents();
}

void Hyprland::connectEvents()
{
    m_pending.clear();
    m_events.abort();
    m_events.connectToServer(m_directory + "/.socket2.sock");
}

QByteArray Hyprland::request(const QByteArray &command, int timeoutMs) const
{
    if (m_directory.isEmpty())
        return {};
    QLocalSocket socket;
    socket.connectToServer(m_directory + "/.socket.sock");
    if (!socket.waitForConnected(timeoutMs))
        return {};
    socket.write(command);
    socket.waitForBytesWritten(timeoutMs);
    QByteArray reply;
    while (socket.state() == QLocalSocket::ConnectedState && socket.waitForReadyRead(timeoutMs))
        reply += socket.readAll();
    reply += socket.readAll();
    return reply;
}

QJsonDocument Hyprland::query(const QByteArray &what) const
{
    return QJsonDocument::fromJson(request("j/" + what));
}

bool Hyprland::dispatch(const QString &lua) const
{
    return request("dispatch " + lua.toUtf8()).trimmed() == "ok";
}

bool Hyprland::eval(const QString &lua) const
{
    const QByteArray reply = request("eval " + lua.toUtf8()).trimmed();
    if (reply.startsWith("error"))
        qWarning("stage: Hyprland refused %s: %s", qPrintable(lua), reply.constData());
    return !reply.isEmpty() && !reply.startsWith("error");
}

QString Hyprland::quote(const QString &text)
{
    // A long bracket with enough '=' that the text can't close it early.
    QString level;
    while (text.contains("]" + level + "]"))
        level += "=";
    return "[" + level + "[" + text + "]" + level + "]";
}
