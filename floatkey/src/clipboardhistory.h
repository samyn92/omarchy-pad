#pragma once

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

// What was copied, for the keyboard's clipboard page and edit bar. The
// history is Omarchy's own (the one its Super+Ctrl+V panel shows, captured by
// its shell from every copy), read from its file and kept in step with it:
// deleting a clip here rewrites the file, which Omarchy's panel reloads.
// Pinned clips are floatkey's, in a file of its own, so they outlast
// Omarchy's limit on how much history it keeps.
//
// An entry is {type: "text", text} or {type: "image", mime, path}, plus
// `pinned` and `key` (what tells two entries apart).
class ClipboardHistory : public QObject {
    Q_OBJECT
    // Pinned clips first, then the history, newest first.
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    // The latest copy while it is fresh, for the edit bar to offer; empty
    // once it is older than a minute and a half.
    Q_PROPERTY(QVariantMap recent READ recent NOTIFY recentChanged)

public:
    explicit ClipboardHistory(const QString &historyPath = defaultHistoryPath(),
                              const QString &pinsPath = defaultPinsPath(), QObject *parent = nullptr);

    static QString defaultHistoryPath();
    static QString defaultPinsPath();
    static QString keyOf(const QVariantMap &entry);

    QVariantList entries() const { return m_entries; }
    QVariantMap recent() const { return m_recent; }
    // The program that puts things on the clipboard; the tests use a fake.
    void setCopyProgram(const QString &program) { m_copyProgram = program; }
    // stage announces every copy; putting a clip back to paste it is none,
    // so it is told to stay quiet first. Empty tells no one.
    void setStageSocket(const QString &path) { m_stageSocket = path; }

    // Puts the entry on the clipboard.
    Q_INVOKABLE void copy(const QVariantMap &entry);
    // Puts the entry on the clipboard and says pasteReady once it is there,
    // for the keyboard to send the paste keys.
    Q_INVOKABLE void paste(const QVariantMap &entry);
    Q_INVOKABLE void pin(const QVariantMap &entry, bool pinned);
    Q_INVOKABLE void remove(const QVariantMap &entry);
    Q_INVOKABLE void reload();

signals:
    void entriesChanged();
    void recentChanged();
    void pasteReady();

private:
    void put(const QVariantMap &entry, bool thenPaste);
    void rebuild();
    void watch();
    void savePins();

    QString m_historyPath;
    QString m_pinsPath;
    QString m_copyProgram = "wl-copy";
    QString m_stageSocket;
    QFileSystemWatcher m_watcher;
    QTimer m_settle;
    QTimer m_freshness;

    QVariantList m_history;
    QVariantList m_pins;
    QVariantList m_entries;
    QVariantMap m_recent;
    QString m_newest;
    bool m_loaded = false;
};
