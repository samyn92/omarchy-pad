#pragma once

#include <QObject>
#include <QVariantList>

#include "apps.h"

// The launcher's apps in rows, one row a kind of app, each row in the order
// the user put it. Until the user moves anything the rows are made up from
// the apps themselves: Omarchy's dock favourites first, then AI apps, web
// apps, and the rest by the categories in their desktop entries. Once
// something moves, every app's place is kept in a file; an app installed
// after that goes into the row of its kind (or Other), so none goes missing.
class Shelves : public QObject {
    Q_OBJECT
    // [{ name, kind, apps: [{ appId, name, icon }] }], top row first.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)

public:
    // `file` keeps the user's rows; `favourites` are app ids for the top
    // row, as Omarchy's dock pins them.
    Shelves(Apps *apps, const QString &file, const QStringList &favourites, QObject *parent = nullptr);

    QVariantList rows() const { return m_rows; }

    // The kind of row an app goes in when it has no place yet: ai, web,
    // internet, office, media, development, games, system or other.
    static QString kindOf(const Apps::App &app);
    // What a row of that kind is called.
    static QString nameOf(const QString &kind);

    // Puts the app in `row` before the app at `index` (past the end: last).
    Q_INVOKABLE void moveApp(const QString &appId, int row, int index);
    // Puts the app in a row of its own, under the others.
    Q_INVOKABLE void newRow(const QString &appId);
    // Moves a whole row up or down the list.
    Q_INVOKABLE void moveRow(int from, int to);

signals:
    void rowsChanged();

private:
    struct Row {
        QString name;
        QString kind;   // where apps without a place go; "" for a row of the user's own
        QStringList apps;
    };

    void rebuild();
    void save();
    // The rows as they are shown now, each app placed.
    QList<Row> placed() const;

    Apps *m_apps;
    QString m_file;
    QStringList m_favourites;
    QList<Row> m_saved;   // the user's rows, empty until something moved
    QVariantList m_rows;
};
