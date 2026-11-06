#include "images.h"

#include <QFileInfo>
#include <QIcon>
#include <QMutex>

#include "gpupicture.h"

IconProvider::IconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}

QPixmap IconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QSize wanted = requestedSize.isValid() ? requestedSize : QSize(128, 128);
    // What follows a "?" is Apps::iconKey: the theme, and how many times the
    // installed apps or icons changed.
    const QString name = id.section('?', 0, 0), key = id.section('?', 1);
    // Icons are asked for on the GUI thread and on QML's image thread both;
    // one at a time, so the themes aren't dropped under a lookup.
    static QMutex mutex;
    static QString lastKey;
    QMutexLocker lock(&mutex);
    if (key != lastKey) {
        // Qt reads each theme once, and the icon-theme.cache it maps then is
        // the one it keeps: an icon installed after that is not in it, and
        // isn't looked for. Setting the search paths again drops the themes
        // read, so the next lookup reads them, and the new cache, afresh.
        QIcon::setThemeSearchPaths(QIcon::themeSearchPaths());
        lastKey = key;
    }
    QIcon icon = QFileInfo(name).isAbsolute() ? QIcon(name) : QIcon::fromTheme(name);
    if (icon.isNull())
        icon = QIcon::fromTheme("application-x-executable");
    const QPixmap pixmap = icon.pixmap(wanted);
    if (size)
        *size = pixmap.size();
    return pixmap;
}

Snapshots::Snapshots() : QQuickImageProvider(QQuickImageProvider::Texture) {}

void Snapshots::insert(int id, const QImage &image)
{
    keep(id, Picture{image, nullptr, 0});
}

void Snapshots::insert(int id, std::shared_ptr<const GpuPicture> picture, int turn)
{
    keep(id, Picture{QImage(), std::move(picture), turn});
}

void Snapshots::keep(int id, const Picture &picture)
{
    QMutexLocker lock(&m_mutex);
    m_pictures.insert(id, picture);
    // A gesture needs at most a handful (a few console windows, or the windows
    // of three workspaces), and each full-screen picture is about 31 MB.
    while (m_pictures.size() > 8)
        m_pictures.remove(*std::min_element(m_pictures.keyBegin(), m_pictures.keyEnd()));
}

int Snapshots::turn(int id)
{
    QMutexLocker lock(&m_mutex);
    return m_pictures.value(id).turn;
}

// Asked for off the GUI thread (asynchronous Images), so scaling a picture in
// memory down to the size it is shown at costs no frames. One on the GPU is
// drawn as it is, scaled as it's drawn.
QQuickTextureFactory *Snapshots::requestTexture(const QString &id, QSize *size, const QSize &requestedSize)
{
    Picture picture;
    {
        QMutexLocker lock(&m_mutex);
        picture = m_pictures.value(id.section('?', 0, 0).toInt());
    }
    if (picture.gpu) {
        if (size)
            *size = picture.gpu->size();
        return new GpuPictureFactory(picture.gpu);
    }
    QImage image = picture.image;
    if (size)
        *size = image.size();
    if (image.isNull())
        return nullptr;
    if (requestedSize.isValid() && requestedSize.width() < image.width())
        image = image.scaled(requestedSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return QQuickTextureFactory::textureFactoryForImage(image);
}
