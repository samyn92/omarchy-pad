#pragma once

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QTimer>

// The apps the launcher offers, read from the .desktop files the way app
// launchers do: hidden and terminal-only entries left out, names in the
// user's language, sorted by name. Follows installs and removals live.
class Apps : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role { NameRole = Qt::UserRole + 1, IconRole, IdRole };

    struct App {
        QString id;        // desktop file name without .desktop
        QString name;
        QString icon;
        QString command;   // Exec with its field codes taken out
    };

    // Searched in order; an id found earlier hides the same id later, as in
    // the XDG spec. Empty means the user's and the system's directories.
    explicit Apps(const QStringList &directories = {}, const QString &language = {}, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    const App *find(const QString &id) const;
    Q_INVOKABLE QString name(const QString &id) const;
    Q_INVOKABLE QString icon(const QString &id) const;

    // Parses one desktop entry; returns false for entries a launcher hides.
    static bool parse(const QString &path, const QString &language, App *app);
    static QString stripFieldCodes(const QString &exec);

private:
    void reload();

    QStringList m_directories;
    QString m_language;
    QList<App> m_apps;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};
