#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

// "image://icon/<name>": an app's icon from the icon theme (or a path).
class IconProvider : public QQuickImageProvider {
public:
    IconProvider();
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;
};

// "image://snapshot/<id>": pictures of windows taken as a gesture starts.
// Only the latest few are kept.
class Snapshots : public QQuickImageProvider {
public:
    Snapshots();
    void insert(int id, const QImage &image);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QMutex m_mutex;
    QHash<int, QImage> m_images;
};
