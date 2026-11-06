#include "apps.h"

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QLocale>
#include <QProcess>
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
    // pacman's icon cache hook runs after the files are in, so an install
    // can change the icon folders after the apps' reload has been done.
    m_iconDebounce.setSingleShot(true);
    m_iconDebounce.setInterval(300);
    connect(&m_iconDebounce, &QTimer::timeout, this, &Apps::iconsChanged);
    connect(&m_iconWatcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_iconDebounce.start(); });
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
    QString exec, icon, type, onlyShowIn, notShowIn, categories;
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
        else if (key == "Categories")
            categories = value;
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
    app->categories = categories.split(';', Qt::SkipEmptyParts);
    app->webApp = app->command.startsWith("omarchy-launch-webapp");
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
    // The apps changed, and with them, likely, the icons there are.
    if (!m_iconTheme.isEmpty())
        iconsChanged();
}

void Apps::watchIconFolders()
{
    const auto watched = m_iconWatcher.directories();
    if (!watched.isEmpty())
        m_iconWatcher.removePaths(watched);
    QStringList folders;
    for (const QString &base : QIcon::themeSearchPaths()) {
        if (base.startsWith(':'))
            continue;
        folders << base + '/' + m_iconTheme << base + "/hicolor";
    }
    for (const QString &base : QIcon::fallbackSearchPaths())
        if (!base.startsWith(':'))
            folders << base;
    folders.removeDuplicates();
    for (const QString &folder : std::as_const(folders))
        if (QFileInfo(folder).isDir())
            m_iconWatcher.addPath(folder);
}

void Apps::iconsChanged()
{
    ++m_iconRevision;
    // An install can make an icon folder that wasn't there to watch before
    // (hicolor in ~/.local/share/icons, say).
    watchIconFolders();
    emit iconThemeChanged();
}

// Qt shows no icons at all for a theme it can't find, not even the ones
// every app keeps in hicolor, and Omarchy's Vantablack and White themes name a
// Yaru-gray that Yaru doesn't have: the nearest installed one stands in.
QString Apps::installedIconTheme(const QString &wanted)
{
    auto installed = [](const QString &name) {
        for (const QString &directory : QIcon::themeSearchPaths())
            if (QFileInfo::exists(directory + "/" + name + "/index.theme"))
                return true;
        return false;
    };
    QString name = wanted.trimmed();
    while (!name.isEmpty()) {
        if (installed(name))
            return name;
        const int dash = name.lastIndexOf('-');
        if (dash <= 0)
            break;
        name = name.left(dash);
    }
    return "hicolor";
}

void Apps::useIconTheme(const QString &wanted)
{
    const QString name = installedIconTheme(wanted);
    QIcon::setFallbackThemeName("hicolor");
    if (name == m_iconTheme)
        return;
    m_iconTheme = name;
    QIcon::setThemeName(name);
    watchIconFolders();
    emit iconThemeChanged();
}

void Apps::followIconTheme()
{
    auto unquoted = [](QString text) { return text.trimmed().remove('\''); };
    QProcess get;
    get.start("gsettings", {"get", "org.gnome.desktop.interface", "icon-theme"});
    if (get.waitForFinished(1000))
        useIconTheme(unquoted(QString::fromUtf8(get.readAllStandardOutput())));
    else
        useIconTheme("hicolor");

    // Prints "icon-theme: 'Name'" at every change.
    m_iconMonitor = new QProcess(this);
    connect(m_iconMonitor, &QProcess::readyReadStandardOutput, this, [this, unquoted] {
        while (m_iconMonitor->canReadLine()) {
            const QString line = QString::fromUtf8(m_iconMonitor->readLine());
            if (line.contains(':'))
                useIconTheme(unquoted(line.section(':', 1)));
        }
    });
    m_iconMonitor->start("gsettings", {"monitor", "org.gnome.desktop.interface", "icon-theme"});
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
