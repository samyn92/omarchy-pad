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
    // A gesture needs at most a handful (a few console windows, or the windows
    // of three workspaces), and each full-screen picture is about 31 MB.
    while (m_images.size() > 8)
        m_images.remove(*std::min_element(m_images.keyBegin(), m_images.keyEnd()));
}

// Asked for off the GUI thread (asynchronous Images), so scaling a picture
// down to the size it is shown at costs no frames.
QImage Snapshots::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    QImage image;
    {
        QMutexLocker lock(&m_mutex);
        image = m_images.value(id.section('?', 0, 0).toInt());
    }
    if (size)
        *size = image.size();
    if (!image.isNull() && requestedSize.isValid() && requestedSize.width() < image.width())
        image = image.scaled(requestedSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return image;
}
