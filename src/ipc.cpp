#include "ipc.h"

#include <QDir>
#include <QLocalSocket>
#include <QUrl>

QString Ipc::socketPath()
{
    QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty())
        runtime = QDir::tempPath();
    QString display = qEnvironmentVariable("WAYLAND_DISPLAY", "wayland-0");
    display.replace('/', '_');
    return runtime + "/stage-" + display + ".sock";
}

bool Ipc::send(const QString &path, const QString &line, QString *reply, int timeoutMs)
{
    QLocalSocket socket;
    socket.connectToServer(path);
    if (!socket.waitForConnected(timeoutMs))
        return false;
    socket.write(line.toUtf8() + '\n');
    if (!socket.waitForBytesWritten(timeoutMs))
        return false;
    while (!socket.canReadLine()) {
        if (!socket.waitForReadyRead(timeoutMs))
            return false;
    }
    if (reply)
        *reply = QString::fromUtf8(socket.readLine()).trimmed();
    return true;
}

QString Ipc::encode(const QString &text)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(text));
}

QString Ipc::decode(const QString &text)
{
    return QUrl::fromPercentEncoding(text.toLatin1());
}
