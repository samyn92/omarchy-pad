#include "apps.h"

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <algorithm>

static QStringList defaultDirectories()
{
    QStringList directories;
    for (const QString &base : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        directories << base + "/applications";
    directories.removeDuplicates();
    return directories;
}

Apps::Apps(const QStringList &directories, const QString &language, QObject *parent)
    : QAbstractListModel(parent),
      m_directories(directories.isEmpty() ? defaultDirectories() : directories),
      m_language(language.isEmpty() ? QLocale().name() : language)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(300);
    connect(&m_debounce, &QTimer::timeout, this, &Apps::reload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    reload();
}

// Field codes (%f, %U, ...) are for files and URLs the launcher doesn't pass;
// %% is a literal percent sign.
QString Apps::stripFieldCodes(const QString &exec)
{
    QString out;
    for (int i = 0; i < exec.size(); ++i) {
        if (exec.at(i) == '%' && i + 1 < exec.size()) {
            if (exec.at(i + 1) == '%')
                out += '%';
            ++i;
            continue;
        }
        out += exec.at(i);
    }
    return out.simplified();
}

bool Apps::parse(const QString &path, const QString &language, App *app)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    // Name[de_DE] beats Name[de] beats Name.
    const QString shortLanguage = language.section('_', 0, 0);
    QString name, localName, shortName;
    QString exec, icon, type, onlyShowIn, notShowIn;
    bool noDisplay = false, hidden = false, terminal = false, inEntry = false;

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith('[')) {
            if (inEntry)
                break;
            inEntry = line == "[Desktop Entry]";
            continue;
        }
        if (!inEntry || line.isEmpty() || line.startsWith('#'))
            continue;
        const int eq = line.indexOf('=');
        if (eq < 0)
            continue;
        const QString key = line.left(eq).trimmed(), value = line.mid(eq + 1).trimmed();
        if (key == "Name")
            name = value;
        else if (key == "Name[" + language + "]")
            localName = value;
        else if (key == "Name[" + shortLanguage + "]")
            shortName = value;
        else if (key == "Exec")
            exec = value;
        else if (key == "Icon")
            icon = value;
        else if (key == "Type")
            type = value;
        else if (key == "NoDisplay")
            noDisplay = value == "true";
        else if (key == "Hidden")
            hidden = value == "true";
        else if (key == "Terminal")
            terminal = value == "true";
        else if (key == "OnlyShowIn")
            onlyShowIn = value;
        else if (key == "NotShowIn")
            notShowIn = value;
    }

    const QStringList desktops = qEnvironmentVariable("XDG_CURRENT_DESKTOP", "Hyprland").split(':');
    auto mentions = [&desktops](const QString &list) {
        for (const QString &desktop : list.split(';', Qt::SkipEmptyParts))
            if (desktops.contains(desktop, Qt::CaseInsensitive))
                return true;
        return false;
    };
    if (type != "Application" || noDisplay || hidden || terminal || exec.isEmpty() || name.isEmpty())
        return false;
    if ((!onlyShowIn.isEmpty() && !mentions(onlyShowIn)) || (!notShowIn.isEmpty() && mentions(notShowIn)))
        return false;

    app->id = QFileInfo(path).completeBaseName();
    app->name = !localName.isEmpty() ? localName : !shortName.isEmpty() ? shortName : name;
    app->icon = icon;
    app->command = stripFieldCodes(exec);
    return !app->command.isEmpty();
}

void Apps::reload()
{
    const auto watched = m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);

    QSet<QString> seen;
    QList<App> apps;
    for (const QString &directory : std::as_const(m_directories)) {
        if (!QFileInfo::exists(directory))
            continue;
        m_watcher.addPath(directory);
        const QDir dir(directory);
        for (const QString &entry : dir.entryList({"*.desktop"}, QDir::Files)) {
            const QString id = QFileInfo(entry).completeBaseName();
            if (seen.contains(id))
                continue;
            seen.insert(id);
            App app;
            if (parse(dir.filePath(entry), m_language, &app))
                apps << app;
        }
    }

    QCollator collator{QLocale(m_language)};
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(apps.begin(), apps.end(), [&collator](const App &a, const App &b) {
        return collator.compare(a.name, b.name) < 0;
    });

    beginResetModel();
    m_apps = apps;
    endResetModel();
}

int Apps::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_apps.size());
}

QVariant Apps::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_apps.size())
        return {};
    const App &app = m_apps.at(index.row());
    switch (role) {
    case NameRole: return app.name;
    case IconRole: return app.icon;
    case IdRole: return app.id;
    }
    return {};
}

QHash<int, QByteArray> Apps::roleNames() const
{
    return {{NameRole, "name"}, {IconRole, "icon"}, {IdRole, "appId"}};
}

const Apps::App *Apps::find(const QString &id) const
{
    for (const App &app : m_apps)
        if (app.id == id)
            return &app;
    return nullptr;
}

QString Apps::name(const QString &id) const
{
    const App *app = find(id);
    return app ? app->name : QString();
}

QString Apps::icon(const QString &id) const
{
    const App *app = find(id);
    return app ? app->icon : QString();
}
