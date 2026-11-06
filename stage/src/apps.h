#pragma once

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QTimer>

// The apps the launcher offers, read from the .desktop files the way app
// launchers do: hidden and terminal-only entries left out, names in the
// user's language, sorted by name. Follows installs and removals live.
class QProcess;

class Apps : public QAbstractListModel {
    Q_OBJECT
    // The icon theme the icons come from: the desktop's (GNOME's icon-theme
    // setting, which Omarchy's themes set), or the nearest installed one when
    // it isn't installed. It changes with Omarchy's theme; icon URLs carry it,
    // so a change loads them anew.
    Q_PROPERTY(QString iconTheme READ iconTheme NOTIFY iconThemeChanged)
    // What icon URLs carry after the "?": the theme, and a count of the
    // installs and removals seen since. A new key makes the icon provider drop
    // what Qt has read of the themes (an app installed since stage started
    // has its icon in no copy Qt holds), and QML load every icon anew.
    Q_PROPERTY(QString iconKey READ iconKey NOTIFY iconThemeChanged)

public:
    enum Role { NameRole = Qt::UserRole + 1, IconRole, IdRole };

    struct App {
        QString id;        // desktop file name without .desktop
        QString name;
        QString icon;
        QString command;   // Exec with its field codes taken out
        QStringList categories;
        bool webApp = false;   // one of Omarchy's web apps (omarchy-launch-webapp)
    };

    // Searched in order; an id found earlier hides the same id later, as in
    // the XDG spec. Empty means the user's and the system's directories.
    explicit Apps(const QStringList &directories = {}, const QString &language = {}, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString iconTheme() const { return m_iconTheme; }
    QString iconKey() const { return m_iconTheme + '/' + QString::number(m_iconRevision); }
    // Takes the desktop's icon theme, and follows it from then on.
    void followIconTheme();
    // The installed theme closest to `wanted`: itself, then its name cut back
    // at each dash, then hicolor.
    static QString installedIconTheme(const QString &wanted);

    const App *find(const QString &id) const;
    const QList<App> &all() const { return m_apps; }
    Q_INVOKABLE QString name(const QString &id) const;
    Q_INVOKABLE QString icon(const QString &id) const;

    // Parses one desktop entry; returns false for entries a launcher hides.
    static bool parse(const QString &path, const QString &language, App *app);
    static QString stripFieldCodes(const QString &exec);

signals:
    void iconThemeChanged();

private:
    void reload();
    void useIconTheme(const QString &wanted);
    // Watches the folders icons are installed to: the theme's and hicolor's
    // in each search path, and the unthemed fallbacks such as pixmaps.
    void watchIconFolders();
    void iconsChanged();

    QStringList m_directories;
    QString m_language;
    QList<App> m_apps;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_iconTheme;
    int m_iconRevision = 0;
    QFileSystemWatcher m_iconWatcher;
    QTimer m_iconDebounce;
    QProcess *m_iconMonitor = nullptr;
};
