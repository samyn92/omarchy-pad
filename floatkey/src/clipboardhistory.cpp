#include "clipboardhistory.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

#include "ipc.h"

// How many history entries the page shows, after the pinned ones.
static constexpr int shown = 60;
// How long a new copy counts as just copied.
static constexpr int freshFor = 90 * 1000;
// wl-copy has handed the clipboard over by the time it exits; give the
// compositor a moment more before the paste keys go out, as Omarchy does.
static constexpr int pasteAfter = 150;

static QString stateDir()
{
    const QString state = qEnvironmentVariable("XDG_STATE_HOME");
    return state.isEmpty() ? QDir::homePath() + "/.local/state" : state;
}

QString ClipboardHistory::defaultHistoryPath()
{
    return stateDir() + "/omarchy/clipboard-history.json";
}

QString ClipboardHistory::defaultPinsPath()
{
    return stateDir() + "/floatkey/clipboard-pins.json";
}

QString ClipboardHistory::keyOf(const QVariantMap &entry)
{
    if (entry.value("type") == "image")
        return "image:" + entry.value("path").toString();
    return "text:" + entry.value("text").toString();
}

// Only the fields Omarchy's history has, so what floatkey writes back reads
// the same to its panel.
static QVariantMap plain(const QVariantMap &entry)
{
    QVariantMap out;
    if (entry.value("type") == "image") {
        out = {{"type", "image"}, {"mime", entry.value("mime")}, {"path", entry.value("path")}};
        if (entry.contains("capturedAt"))
            out.insert("capturedAt", entry.value("capturedAt"));
    } else {
        out = {{"type", "text"}, {"text", entry.value("text")}};
    }
    return out;
}

static bool usable(const QVariantMap &entry)
{
    if (entry.value("type") == "image")
        return !entry.value("path").toString().isEmpty();
    return entry.value("type") == "text" && !entry.value("text").toString().trimmed().isEmpty();
}

static QVariantList readList(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QVariantList out;
    for (const QVariant &value : QJsonDocument::fromJson(file.readAll()).array().toVariantList()) {
        const QVariantMap entry = value.toMap();
        if (usable(entry))
            out << plain(entry);
    }
    return out;
}

static bool writeList(const QString &path, const QVariantList &list)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(QJsonArray::fromVariantList(list)).toJson(QJsonDocument::Indented));
    return file.commit();
}

ClipboardHistory::ClipboardHistory(const QString &historyPath, const QString &pinsPath, QObject *parent)
    : QObject(parent), m_historyPath(historyPath), m_pinsPath(pinsPath),
      m_stageSocket(Ipc::socketPath().replace("/floatkey-", "/stage-"))
{
    // Omarchy replaces the file on every copy, sometimes twice in a row.
    m_settle.setSingleShot(true);
    m_settle.setInterval(60);
    connect(&m_settle, &QTimer::timeout, this, &ClipboardHistory::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_settle, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_settle, qOverload<>(&QTimer::start));

    m_freshness.setSingleShot(true);
    m_freshness.setInterval(freshFor);
    connect(&m_freshness, &QTimer::timeout, this, [this] {
        m_recent.clear();
        emit recentChanged();
    });

    m_pins = readList(m_pinsPath);
    reload();
}

void ClipboardHistory::watch()
{
    const QString dir = QFileInfo(m_historyPath).absolutePath();
    if (!m_watcher.directories().contains(dir) && QFileInfo::exists(dir))
        m_watcher.addPath(dir);
    if (!m_watcher.files().contains(m_historyPath) && QFileInfo::exists(m_historyPath))
        m_watcher.addPath(m_historyPath);
}

void ClipboardHistory::reload()
{
    watch();
    m_history = readList(m_historyPath);

    // A new copy at the top is fresh; the first read only says what was there.
    const QString newest = m_history.isEmpty() ? QString() : keyOf(m_history.first().toMap());
    if (m_loaded && !newest.isEmpty() && newest != m_newest) {
        m_recent = m_history.first().toMap();
        m_recent.insert("key", newest);
        m_freshness.start();
        emit recentChanged();
    }
    m_newest = newest;
    m_loaded = true;
    rebuild();
}

void ClipboardHistory::rebuild()
{
    QVariantList entries;
    QStringList keys;
    for (const QVariant &value : m_pins) {
        QVariantMap entry = value.toMap();
        const QString key = keyOf(entry);
        if (keys.contains(key))
            continue;
        keys << key;
        entry.insert("pinned", true);
        entry.insert("key", key);
        entries << entry;
    }
    int count = 0;
    for (const QVariant &value : m_history) {
        if (count >= shown)
            break;
        QVariantMap entry = value.toMap();
        const QString key = keyOf(entry);
        if (keys.contains(key))
            continue;
        keys << key;
        entry.insert("pinned", false);
        entry.insert("key", key);
        entries << entry;
        ++count;
    }
    if (entries != m_entries) {
        m_entries = entries;
        emit entriesChanged();
    }
}

void ClipboardHistory::copy(const QVariantMap &entry)
{
    put(entry, false);
}

void ClipboardHistory::paste(const QVariantMap &entry)
{
    put(entry, true);
}

void ClipboardHistory::put(const QVariantMap &entry, bool thenPaste)
{
    if (!usable(entry))
        return;
    if (thenPaste && !m_stageSocket.isEmpty())
        Ipc::send(m_stageSocket, "hush", nullptr, 200);
    auto *process = new QProcess(this);
    QStringList args;
    if (entry.value("type") == "image") {
        const QString path = entry.value("path").toString();
        if (!QFileInfo(path).isReadable()) {
            process->deleteLater();
            return;
        }
        args << "--type" << entry.value("mime").toString();
        process->setStandardInputFile(path);
    } else {
        // Left to guess, wl-copy calls a short clip ("42") or a script a
        // file, and browsers then have no text to paste.
        args << "--type" << "text/plain;charset=utf-8";
    }
    connect(process, &QProcess::finished, this, [this, process, thenPaste](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        if (thenPaste && status == QProcess::NormalExit && code == 0)
            QTimer::singleShot(pasteAfter, this, &ClipboardHistory::pasteReady);
    });
    connect(process, &QProcess::errorOccurred, process, &QObject::deleteLater);
    process->start(m_copyProgram, args);
    if (entry.value("type") != "image") {
        process->write(entry.value("text").toString().toUtf8());
        process->closeWriteChannel();
    }
}

void ClipboardHistory::pin(const QVariantMap &entry, bool pinned)
{
    if (!usable(entry))
        return;
    const QString key = keyOf(entry);
    QVariantList pins;
    for (const QVariant &value : m_pins)
        if (keyOf(value.toMap()) != key)
            pins << value;
    if (pinned)
        pins.prepend(plain(entry));
    m_pins = pins;
    savePins();
    rebuild();
}

void ClipboardHistory::remove(const QVariantMap &entry)
{
    const QString key = keyOf(entry);
    if (entry.value("pinned").toBool()) {
        pin(entry, false);
        return;
    }
    // Rewritten from the file as it is now, not from what was last read, so a
    // copy Omarchy just added isn't lost.
    QVariantList kept;
    for (const QVariant &value : readList(m_historyPath))
        if (keyOf(value.toMap()) != key)
            kept << value;
    writeList(m_historyPath, kept);
    reload();
}

void ClipboardHistory::savePins()
{
    writeList(m_pinsPath, m_pins);
}
