#include "keyboardwatcher.h"

#include <QDir>
#include <QFile>
#include <linux/input-event-codes.h>

KeyboardWatcher::KeyboardWatcher(const QString &sysInput, const QString &devInput, QObject *parent)
    : QObject(parent), m_sysInput(sysInput)
{
    // A device node appears or goes when a keyboard connects; its sysfs
    // entry is complete a moment later.
    m_settle.setSingleShot(true);
    m_settle.setInterval(400);
    connect(&m_settle, &QTimer::timeout, this, &KeyboardWatcher::rescan);
    if (QFileInfo::exists(devInput))
        m_watcher.addPath(devInput);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_settle.start(); });
    rescan();
}

bool KeyboardWatcher::hasLetterKeys(const QString &bitmap)
{
    const QStringList words = bitmap.split(' ', Qt::SkipEmptyParts);
    auto has = [&words](int key) {
        const int word = key / 64;
        if (word >= words.size())
            return false;
        bool ok = false;
        const quint64 bits = words.at(words.size() - 1 - word).toULongLong(&ok, 16);
        return ok && (bits >> (key % 64)) & 1;
    };
    for (int key : {KEY_Q, KEY_A, KEY_Z, KEY_M, KEY_SPACE})
        if (!has(key))
            return false;
    return true;
}

void KeyboardWatcher::rescan()
{
    QStringList names;
    const QDir dir(m_sysInput);
    for (const QString &entry : dir.entryList({"event*"}, QDir::Dirs | QDir::System)) {
        QFile keys(dir.filePath(entry + "/device/capabilities/key"));
        if (!keys.open(QIODevice::ReadOnly) || !hasLetterKeys(QString::fromLatin1(keys.readAll()).trimmed()))
            continue;
        QFile name(dir.filePath(entry + "/device/name"));
        names << (name.open(QIODevice::ReadOnly) ? QString::fromUtf8(name.readAll()).trimmed() : entry);
    }
    m_names = names;
    if (!names.isEmpty() != m_attached) {
        m_attached = !names.isEmpty();
        emit attachedChanged();
    }
}
