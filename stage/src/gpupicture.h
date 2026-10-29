#pragma once

#include <QQuickTextureFactory>
#include <QSize>
#include <atomic>
#include <cstdint>
#include <memory>

struct gbm_bo;
struct gbm_device;

// A window's picture kept on the GPU: a buffer (dmabuf) the compositor copies
// the window into, which the scene graph then draws from directly. Nothing
// goes through the CPU, so the compositor doesn't stall reading the window
// back and stage doesn't copy or upload 30 MB per picture.
class GpuPicture {
public:
    // A buffer for a picture of `size` in `format` (a DRM fourcc), laid out
    // in one of `modifiers` (what the compositor can write), on `gpu`. Null
    // when the driver can't make one.
    static std::shared_ptr<GpuPicture> allocate(gbm_device *gpu, const QSize &size, uint32_t format,
                                                const uint64_t *modifiers, int count);
    ~GpuPicture();

    QSize size() const { return m_size; }
    uint32_t format() const { return m_format; }
    uint64_t modifier() const { return m_modifier; }
    int planes() const { return m_planes; }
    int fd(int plane) const { return m_fds[plane]; }
    uint32_t stride(int plane) const { return m_strides[plane]; }
    uint32_t offset(int plane) const { return m_offsets[plane]; }

    // Set when the scene graph couldn't make a texture of one (the driver
    // refused the buffer), so captures go back to the CPU's way.
    static std::atomic<bool> s_refused;

private:
    GpuPicture() = default;
    gbm_bo *m_bo = nullptr;
    QSize m_size;
    uint32_t m_format = 0;
    uint64_t m_modifier = 0;
    int m_planes = 0;
    int m_fds[4] = {-1, -1, -1, -1};
    uint32_t m_strides[4] = {};
    uint32_t m_offsets[4] = {};
};

// Hands a GpuPicture to an Image: the texture is made on the render thread,
// from the buffer itself.
class GpuPictureFactory : public QQuickTextureFactory {
public:
    explicit GpuPictureFactory(std::shared_ptr<const GpuPicture> picture) : m_picture(std::move(picture)) {}
    QSGTexture *createTexture(QQuickWindow *window) const override;
    QSize textureSize() const override { return m_picture->size(); }
    int textureByteCount() const override { return m_picture->size().width() * m_picture->size().height() * 4; }
    QImage image() const override { return {}; }

private:
    std::shared_ptr<const GpuPicture> m_picture;
};
