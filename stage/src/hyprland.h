#pragma once

#include <QJsonDocument>
#include <QLocalSocket>
#include <QObject>

// Talks to Hyprland over its two sockets: requests (the same ones hyprctl
// sends) on one, and the stream of events (a window opened, the scratchpad
// came up) on the other.
class Hyprland : public QObject {
    Q_OBJECT

public:
    // `directory` holds .socket.sock and .socket2.sock; by default the one of
    // the Hyprland instance this process runs under.
    explicit Hyprland(const QString &directory = {}, QObject *parent = nullptr);

    bool isAvailable() const { return !m_directory.isEmpty(); }

    QByteArray request(const QByteArray &command, int timeoutMs = 400) const;
    // A JSON query such as "clients" or "monitors".
    QJsonDocument query(const QByteArray &what) const;
    // Runs a Lua dispatcher, e.g. hl.dsp.layout("preselect r").
    bool dispatch(const QString &lua) const;
    // Runs a Lua statement, e.g. hl.animation({ ... }).
    bool eval(const QString &lua) const;

    // A Lua string literal for any text, so commands and titles never need
    // escaping.
    static QString quote(const QString &text);

signals:
    // "openwindow", "activespecial", "workspace"..., with the raw data.
    void hyprlandEvent(const QString &name, const QString &data);

private:
    void connectEvents();

    QString m_directory;
    QLocalSocket m_events;
    QByteArray m_pending;
};
