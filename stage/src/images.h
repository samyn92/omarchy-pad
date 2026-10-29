#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <memory>

class GpuPicture;

// "image://icon/<name>": an app's icon from the icon theme (or a path).
class IconProvider : public QQuickImageProvider {
public:
    IconProvider();
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;
};

// "image://snapshot/<id>": pictures of windows taken as a gesture starts,
// on the GPU when they were taken there, else in memory. Only the latest few
// are kept.
class Snapshots : public QQuickImageProvider {
public:
    Snapshots();
    void insert(int id, const QImage &image);
    void insert(int id, std::shared_ptr<const GpuPicture> picture, int turn);
    // How a picture lies, as a wl_output_transform; pictures in memory are
    // already upright.
    int turn(int id);
    QQuickTextureFactory *requestTexture(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    struct Picture {
        QImage image;
        std::shared_ptr<const GpuPicture> gpu;
        int turn = 0;
    };
    void keep(int id, const Picture &picture);
    QMutex m_mutex;
    QHash<int, Picture> m_pictures;
};
