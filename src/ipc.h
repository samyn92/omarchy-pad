#pragma once

#include <QString>

// One line in, one line out, over a socket beside the Wayland display, so
// `stage <command>` can drive the running overlay.
namespace Ipc {

QString socketPath();
bool send(const QString &path, const QString &line, QString *reply, int timeoutMs = 1000);

// Command arguments travel percent-encoded, so any text fits on one line.
QString encode(const QString &text);
QString decode(const QString &text);

}
