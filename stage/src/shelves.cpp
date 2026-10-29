#include "shelves.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

// The made-up rows, top first. Favourites only ever holds what the dock
// pins (or the user puts there); the rest take apps by their kind.
static const QStringList Kinds{"favorites", "ai", "web", "internet", "office", "media", "development", "games", "system", "other"};

// AI apps have no category of their own: these words in an app's name or
// desktop id say it is one.
static const QStringList AiWords{"claude", "chatgpt", "grok", "gemini", "copilot", "perplexity", "hermes", "t3 code",
                                 "t3code", "superwhisper", "codex", "cursor", "ollama"};

Shelves::Shelves(Apps *apps, const QString &file, const QStringList &favourites, QObject *parent)
    : QObject(parent), m_apps(apps), m_file(file), m_favourites(favourites)
{
    QFile in(m_file);
    if (in.open(QIODevice::ReadOnly)) {
        const QJsonArray rows = QJsonDocument::fromJson(in.readAll()).object().value("rows").toArray();
        for (const QJsonValue &value : rows) {
            const QJsonObject o = value.toObject();
            Row row{o.value("name").toString(), o.value("kind").toString(), {}};
            for (const QJsonValue &id : o.value("apps").toArray())
                row.apps << id.toString();
            m_saved << row;
        }
    }
    // Installs and removals reach the rows as they reach the apps.
    connect(m_apps, &QAbstractItemModel::modelReset, this, &Shelves::rebuild);
    rebuild();
}

QString Shelves::kindOf(const Apps::App &app)
{
    const QString id = app.id.toLower(), name = app.name.toLower();
    for (const QString &word : AiWords)
        if (id.contains(word) || name.contains(word))
            return "ai";
    if (app.webApp)
        return "web";
    auto any = [&app](const QStringList &categories) {
        for (const QString &category : categories)
            if (app.categories.contains(category))
                return true;
        return false;
    };
    if (any({"Game"}))
        return "games";
    if (any({"Office"}))
        return "office";
    if (any({"AudioVideo", "Audio", "Video", "Graphics", "Photography"}))
        return "media";
    if (any({"Development", "IDE"}))
        return "development";
    if (any({"Network", "WebBrowser"}))
        return "internet";
    if (any({"System", "Utility", "Settings", "FileManager", "TerminalEmulator"}))
        return "system";
    return "other";
}

QString Shelves::nameOf(const QString &kind)
{
    static const QHash<QString, QString> names{
        {"favorites", "Favorites"}, {"ai", "AI"},         {"web", "Web apps"},       {"internet", "Internet"},
        {"office", "Office"},       {"media", "Media"},   {"development", "Development"}, {"games", "Games"},
        {"system", "System"},       {"other", "Other"},
    };
    return names.value(kind, kind);
}

QList<Shelves::Row> Shelves::placed() const
{
    QList<Row> rows;
    QSet<QString> taken;
    auto put = [&](Row &row, const QString &id) {
        if (taken.contains(id) || !m_apps->find(id))
            return;
        taken.insert(id);
        row.apps << id;
    };
    if (m_saved.isEmpty()) {
        for (const QString &kind : Kinds)
            rows << Row{nameOf(kind), kind, {}};
        for (const QString &id : m_favourites)
            put(rows.first(), id);
    } else {
        for (const Row &saved : m_saved) {
            Row row{saved.name, saved.kind, {}};
            for (const QString &id : saved.apps)
                put(row, id);
            rows << row;
        }
    }

    // Every app without a place, by name, into the row of its kind.
    for (const Apps::App &app : m_apps->all()) {
        if (taken.contains(app.id))
            continue;
        const QString kind = kindOf(app);
        Row *home = nullptr;
        for (Row &row : rows)
            if (row.kind == kind)
                home = &row;
        if (!home)
            for (Row &row : rows)
                if (row.kind == "other")
                    home = &row;
        if (!home) {
            rows << Row{nameOf("other"), "other", {}};
            home = &rows.last();
        }
        put(*home, app.id);
    }

    rows.removeIf([](const Row &row) { return row.apps.isEmpty(); });
    return rows;
}

void Shelves::rebuild()
{
    QVariantList rows;
    for (const Row &row : placed()) {
        QVariantList apps;
        for (const QString &id : row.apps) {
            const Apps::App *app = m_apps->find(id);
            apps << QVariantMap{{"appId", id}, {"name", app->name}, {"icon", app->icon}};
        }
        rows << QVariantMap{{"name", row.name}, {"kind", row.kind}, {"apps", apps}};
    }
    m_rows = rows;
    emit rowsChanged();
}

void Shelves::save()
{
    QJsonArray rows;
    for (const Row &row : std::as_const(m_saved))
        rows << QJsonObject{{"name", row.name}, {"kind", row.kind}, {"apps", QJsonArray::fromStringList(row.apps)}};
    QDir().mkpath(QFileInfo(m_file).absolutePath());
    QSaveFile out(m_file);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(QJsonObject{{"rows", rows}}).toJson());
        out.commit();
    }
}

void Shelves::moveApp(const QString &appId, int row, int index)
{
    QList<Row> rows = placed();
    if (row < 0 || row >= rows.size())
        return;
    for (int r = 0; r < rows.size(); ++r) {
        const int at = int(rows[r].apps.indexOf(appId));
        if (at < 0)
            continue;
        rows[r].apps.removeAt(at);
        if (r == row && at < index)
            --index;
    }
    QStringList &target = rows[row].apps;
    target.insert(qBound(0, index, int(target.size())), appId);
    // Rows are dropped once emptied, after the move, so `row` meant the row
    // it pointed at.
    rows.removeIf([](const Row &r) { return r.apps.isEmpty(); });
    m_saved = rows;
    save();
    rebuild();
}

void Shelves::newRow(const QString &appId)
{
    QList<Row> rows = placed();
    for (Row &row : rows)
        row.apps.removeAll(appId);
    rows << Row{"New row", "", {appId}};
    rows.removeIf([](const Row &r) { return r.apps.isEmpty(); });
    m_saved = rows;
    save();
    rebuild();
}

void Shelves::moveRow(int from, int to)
{
    QList<Row> rows = placed();
    if (from < 0 || from >= rows.size() || to < 0 || to >= rows.size() || from == to)
        return;
    rows.move(from, to);
    m_saved = rows;
    save();
    rebuild();
}
