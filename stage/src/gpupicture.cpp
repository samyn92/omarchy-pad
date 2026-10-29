#include "gpupicture.h"

#include <QImage>
#include <QLoggingCategory>
#include <QList>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQuickWindow>
#include <QSGTexture>
#include <QtQuick/qsgtexture_platform.h>
#include <gbm.h>
#include <libdrm/drm_fourcc.h>
#include <unistd.h>

// Without it, EGL's headers bring in X11's, whose macros clash with Qt's.
#define EGL_NO_X11
#include <EGL/egl.h>
#include <EGL/eglext.h>

// Defined in stage.cpp.
Q_DECLARE_LOGGING_CATEGORY(lcStage)

std::atomic<bool> GpuPicture::s_refused{false};

std::shared_ptr<GpuPicture> GpuPicture::allocate(gbm_device *gpu, const QSize &size, uint32_t format,
                                                 const uint64_t *modifiers, int count)
{
    if (!gpu || size.isEmpty())
        return nullptr;
    // DRM_FORMAT_MOD_INVALID among them means the driver may also lay it out
    // as it likes, as long as it doesn't say how.
    QList<uint64_t> named;
    bool implicit = count == 0;
    for (int i = 0; i < count; ++i) {
        if (modifiers[i] == DRM_FORMAT_MOD_INVALID)
            implicit = true;
        else
            named << modifiers[i];
    }
    gbm_bo *bo = nullptr;
    uint64_t modifier = DRM_FORMAT_MOD_INVALID;
    if (!named.isEmpty()) {
        bo = gbm_bo_create_with_modifiers2(gpu, uint32_t(size.width()), uint32_t(size.height()), format, named.data(),
                                           unsigned(named.size()), GBM_BO_USE_RENDERING);
        if (bo)
            modifier = gbm_bo_get_modifier(bo);
    }
    if (!bo && implicit)
        bo = gbm_bo_create(gpu, uint32_t(size.width()), uint32_t(size.height()), format, GBM_BO_USE_RENDERING);
    if (!bo)
        return nullptr;

    std::shared_ptr<GpuPicture> picture(new GpuPicture);
    picture->m_bo = bo;
    picture->m_size = size;
    picture->m_format = format;
    picture->m_modifier = modifier;
    picture->m_planes = qBound(1, gbm_bo_get_plane_count(bo), 4);
    for (int i = 0; i < picture->m_planes; ++i) {
        picture->m_fds[i] = gbm_bo_get_fd_for_plane(bo, i);
        if (picture->m_fds[i] < 0)
            return nullptr;
        picture->m_strides[i] = gbm_bo_get_stride_for_plane(bo, i);
        picture->m_offsets[i] = gbm_bo_get_offset(bo, i);
    }
    return picture;
}

GpuPicture::~GpuPicture()
{
    for (int fd : m_fds)
        if (fd >= 0)
            close(fd);
    if (m_bo)
        gbm_bo_destroy(m_bo);
}

namespace {

// The scene graph's texture for a GL texture made here, which it doesn't own:
// this one deletes it once the scene graph lets go of the picture (on the
// render thread, with the context current).
class OwnedTexture : public QSGTexture {
public:
    OwnedTexture(QSGTexture *texture, GLuint id) : m_texture(texture), m_id(id) {}
    ~OwnedTexture() override
    {
        delete m_texture;
        if (QOpenGLContext *gl = QOpenGLContext::currentContext())
            gl->functions()->glDeleteTextures(1, &m_id);
    }
    qint64 comparisonKey() const override { return m_texture->comparisonKey(); }
    QRhiTexture *rhiTexture() const override { return m_texture->rhiTexture(); }
    QSize textureSize() const override { return m_texture->textureSize(); }
    bool hasAlphaChannel() const override { return m_texture->hasAlphaChannel(); }
    bool hasMipmaps() const override { return false; }

private:
    QSGTexture *m_texture;
    GLuint m_id;
};

using TargetTexture = void (*)(GLenum target, void *image);

} // namespace

QSGTexture *GpuPictureFactory::createTexture(QQuickWindow *window) const
{
    auto refuse = [window](const char *why) {
        qCWarning(lcStage) << "can't show a picture from the GPU:" << why << "- taking pictures the CPU's way from now on";
        GpuPicture::s_refused = true;
        QImage blank(1, 1, QImage::Format_ARGB32_Premultiplied);
        blank.fill(Qt::transparent);
        return window->createTextureFromImage(blank);
    };

    static const auto createImage = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
    static const auto destroyImage = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
    static const auto targetTexture = reinterpret_cast<TargetTexture>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    QOpenGLContext *gl = QOpenGLContext::currentContext();
    const EGLDisplay display = eglGetCurrentDisplay();
    if (!gl || display == EGL_NO_DISPLAY || !createImage || !destroyImage || !targetTexture)
        return refuse("no EGL image import");

    const GpuPicture &p = *m_picture;
    static const EGLint fdKeys[] = {EGL_DMA_BUF_PLANE0_FD_EXT, EGL_DMA_BUF_PLANE1_FD_EXT, EGL_DMA_BUF_PLANE2_FD_EXT,
                                    EGL_DMA_BUF_PLANE3_FD_EXT};
    static const EGLint offsetKeys[] = {EGL_DMA_BUF_PLANE0_OFFSET_EXT, EGL_DMA_BUF_PLANE1_OFFSET_EXT,
                                        EGL_DMA_BUF_PLANE2_OFFSET_EXT, EGL_DMA_BUF_PLANE3_OFFSET_EXT};
    static const EGLint pitchKeys[] = {EGL_DMA_BUF_PLANE0_PITCH_EXT, EGL_DMA_BUF_PLANE1_PITCH_EXT,
                                       EGL_DMA_BUF_PLANE2_PITCH_EXT, EGL_DMA_BUF_PLANE3_PITCH_EXT};
    static const EGLint modifierLoKeys[] = {EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT,
                                            EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT};
    static const EGLint modifierHiKeys[] = {EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT,
                                            EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT};
    QList<EGLint> attributes = {EGL_WIDTH, p.size().width(), EGL_HEIGHT, p.size().height(),
                                EGL_LINUX_DRM_FOURCC_EXT, EGLint(p.format())};
    for (int i = 0; i < p.planes(); ++i) {
        attributes << fdKeys[i] << p.fd(i) << offsetKeys[i] << EGLint(p.offset(i)) << pitchKeys[i] << EGLint(p.stride(i));
        if (p.modifier() != DRM_FORMAT_MOD_INVALID)
            attributes << modifierLoKeys[i] << EGLint(p.modifier() & 0xffffffff) << modifierHiKeys[i]
                       << EGLint(p.modifier() >> 32);
    }
    attributes << EGL_NONE;
    const EGLImageKHR image = createImage(display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attributes.data());
    if (image == EGL_NO_IMAGE_KHR)
        return refuse("the driver wouldn't take the buffer");

    // Leave the texture binding as Qt had it.
    QOpenGLFunctions *f = gl->functions();
    // Errors left from before aren't this texture's.
    while (f->glGetError() != GL_NO_ERROR) {
    }
    GLint was = 0;
    f->glGetIntegerv(GL_TEXTURE_BINDING_2D, &was);
    GLuint id = 0;
    f->glGenTextures(1, &id);
    f->glBindTexture(GL_TEXTURE_2D, id);
    targetTexture(GL_TEXTURE_2D, image);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const GLenum error = f->glGetError();
    f->glBindTexture(GL_TEXTURE_2D, GLuint(was));
    // The texture keeps the buffer; the image was only the way in.
    destroyImage(display, image);
    if (error != GL_NO_ERROR) {
        f->glDeleteTextures(1, &id);
        return refuse("the texture couldn't use the buffer");
    }

    const bool alpha = p.format() == DRM_FORMAT_ARGB8888 || p.format() == DRM_FORMAT_ABGR8888;
    QSGTexture *texture = QNativeInterface::QSGOpenGLTexture::fromNative(
        id, window, p.size(), alpha ? QQuickWindow::TextureHasAlphaChannel : QQuickWindow::CreateTextureOptions());
    return new OwnedTexture(texture, id);
}
