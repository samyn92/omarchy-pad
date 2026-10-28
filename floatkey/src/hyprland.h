#pragma once

#include <QJsonDocument>
#include <QLocalSocket>
#include <QObject>

// Talks to Hyprland over its two sockets: requests (the same ones hyprctl
// sends) on one, and the stream of events (a window opened) on the other.
// A trimmed copy of stage's.
class Hyprland : public QObject {
    Q_OBJECT

public:
    explicit Hyprland(QObject *parent = nullptr);

    bool isAvailable() const { return !m_directory.isEmpty(); }

    QByteArray request(const QByteArray &command, int timeoutMs = 400) const;
    // A JSON query such as "activewindow" or "monitors".
    QJsonDocument query(const QByteArray &what) const;

signals:
    // "openwindow", "workspace"..., with the raw data.
    void hyprlandEvent(const QString &name, const QString &data);

private:
    void connectEvents();

    QString m_directory;
    QLocalSocket m_events;
    QByteArray m_pending;
};
