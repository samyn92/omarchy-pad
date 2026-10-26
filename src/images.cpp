#include "images.h"

#include <QFileInfo>
#include <QIcon>

IconProvider::IconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}

QPixmap IconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QSize wanted = requestedSize.isValid() ? requestedSize : QSize(128, 128);
    QIcon icon = QFileInfo(id).isAbsolute() ? QIcon(id) : QIcon::fromTheme(id);
    if (icon.isNull())
        icon = QIcon::fromTheme("application-x-executable");
    const QPixmap pixmap = icon.pixmap(wanted);
    if (size)
        *size = pixmap.size();
    return pixmap;
}

Snapshots::Snapshots() : QQuickImageProvider(QQuickImageProvider::Image) {}

void Snapshots::insert(int id, const QImage &image)
{
    QMutexLocker lock(&m_mutex);
    m_images.insert(id, image);
    // A gesture needs at most a handful (the screen, a few console windows).
    while (m_images.size() > 8)
        m_images.remove(*std::min_element(m_images.keyBegin(), m_images.keyEnd()));
}

QImage Snapshots::requestImage(const QString &id, QSize *size, const QSize &)
{
    QMutexLocker lock(&m_mutex);
    const QImage image = m_images.value(id.section('?', 0, 0).toInt());
    if (size)
        *size = image.size();
    return image;
}
