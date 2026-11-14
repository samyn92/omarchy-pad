// Importing a camera buffer as a texture follows stage's GpuPicture
// (stage/src/gpupicture.cpp).
#include "viewfinder.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <QHash>
#include <QLoggingCategory>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QSGTransformNode>
#include <algorithm>
#include <drm_fourcc.h>

#include "cameras.h"
#include "orientation.h"

Q_DECLARE_LOGGING_CATEGORY(lcCamera)

std::atomic<bool> Viewfinder::s_refused = false;

namespace {

// The scene graph's texture for a GL texture made here, which it doesn't own:
// this one deletes it with itself (on the render thread, with the context
// current).
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
    bool hasAlphaChannel() const override { return false; }
    bool hasMipmaps() const override { return false; }

private:
    QSGTexture *m_texture;
    GLuint m_id;
};

using TargetTexture = void (*)(GLenum target, void *image);

// The camera's buffer as a texture, without copying it; null when the driver
// won't take it.
QSGTexture *importBuffer(QQuickWindow *window, const Frame &frame)
{
    static const auto createImage = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
    static const auto destroyImage = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
    static const auto targetTexture = reinterpret_cast<TargetTexture>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    QOpenGLContext *gl = QOpenGLContext::currentContext();
    const EGLDisplay display = eglGetCurrentDisplay();
    if (!gl || display == EGL_NO_DISPLAY || !createImage || !destroyImage || !targetTexture) {
        qCWarning(lcCamera) << "no EGL image import: the viewfinder copies frames instead";
        return nullptr;
    }
    const EGLint attributes[] = {EGL_WIDTH, frame.size.width(), EGL_HEIGHT, frame.size.height(),
                                 EGL_LINUX_DRM_FOURCC_EXT, EGLint(frame.fourcc),
                                 EGL_DMA_BUF_PLANE0_FD_EXT, frame.fd,
                                 EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
                                 EGL_DMA_BUF_PLANE0_PITCH_EXT, frame.stride,
                                 EGL_NONE};
    const EGLImageKHR image = createImage(display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attributes);
    if (image == EGL_NO_IMAGE_KHR) {
        qCWarning(lcCamera) << "the GPU won't take the camera's buffer: the viewfinder copies frames instead";
        return nullptr;
    }
    QOpenGLFunctions *f = gl->functions();
    // Errors left from before aren't this texture's.
    while (f->glGetError() != GL_NO_ERROR) {
    }
    // Leave the texture binding as Qt had it.
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
        qCWarning(lcCamera) << "a texture can't use the camera's buffer: the viewfinder copies frames instead";
        return nullptr;
    }
    QSGTexture *texture = QNativeInterface::QSGOpenGLTexture::fromNative(id, window, frame.size);
    return new OwnedTexture(texture, id);
}

// The picture node, turned and placed by its transform. It keeps a texture for
// each of the stream's buffers, made once: the camera refills the same
// buffers, and the texture shows whatever is in its buffer now.
class PictureNode : public QSGTransformNode {
public:
    PictureNode()
    {
        m_picture = new QSGSimpleTextureNode;
        m_picture->setOwnsTexture(false);
        m_picture->setFiltering(QSGTexture::Linear);
        appendChildNode(m_picture);
    }
    ~PictureNode() override
    {
        forget();
        delete m_copied;
    }

    void forget()
    {
        qDeleteAll(m_textures);
        m_textures.clear();
    }

    QSGSimpleTextureNode *m_picture;
    QHash<int, QSGTexture *> m_textures;
    uint64_t m_stream = 0;
    // A texture of a copied picture: the still, or frames when the GPU won't
    // take buffers.
    QSGTexture *m_copied = nullptr;
};

} // namespace

Viewfinder::Viewfinder(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

Viewfinder::~Viewfinder() = default;

void Viewfinder::setCameras(Cameras *cameras)
{
    if (cameras == m_cameras)
        return;
    if (m_cameras)
        disconnect(m_cameras, nullptr, this, nullptr);
    m_cameras = cameras;
    if (m_cameras) {
        connect(m_cameras, &Cameras::frameReady, this, &Viewfinder::take);
        // Directly: the buffers are freed right after.
        connect(m_cameras, &Cameras::stopping, this, &Viewfinder::freeze, Qt::DirectConnection);
        connect(m_cameras, &Cameras::rotationChanged, this, &Viewfinder::place);
        connect(m_cameras, &Cameras::cameraChanged, this, &Viewfinder::place);
    }
    emit camerasChanged();
    place();
}

void Viewfinder::take()
{
    FramePtr frame = m_cameras ? m_cameras->latest() : nullptr;
    if (!frame || frame == m_frame)
        return;
    const bool wasFrozen = isFrozen();
    m_previous = std::move(m_frame);
    m_frame = std::move(frame);
    if (m_frame->size != m_frameSize)
        place();
    if (wasFrozen) {
        m_still = {};
        m_stillChanged = true;
        emit frozenChanged();
    }
    update();
}

void Viewfinder::freeze()
{
    if (!m_frame)
        return;
    // Keep the last picture, small, to show until the next stream's first
    // frame: switching cameras then doesn't flash black.
    const int step = std::max(1, m_frame->size.width() / 640);
    m_still = m_frame->reduced(step);
    m_stillChanged = true;
    m_frame.reset();
    m_previous.reset();
    emit frozenChanged();
    update();
}

QTransform Viewfinder::transform() const
{
    if (m_frameSize.isEmpty() || m_pictureRect.isEmpty())
        return {};
    const QTransform turn = Orientation::preview(m_rotation, m_front, QSizeF(m_frameSize));
    const QSize turned = Orientation::turned(m_frameSize, m_rotation);
    const qreal scale = m_pictureRect.width() / turned.width();
    QTransform place;
    place.translate(m_pictureRect.x(), m_pictureRect.y());
    place.scale(scale, scale);
    return turn * place;
}

void Viewfinder::place()
{
    // The still keeps the old camera's shape until the new one sends a frame.
    if (m_frame)
        m_frameSize = m_frame->size;
    else if (m_frameSize.isEmpty() && m_cameras)
        m_frameSize = m_cameras->frameSize();
    if (m_cameras && (m_frame || m_still.isNull())) {
        m_rotation = m_cameras->rotation();
        m_front = m_cameras->isFront();
    }
    QRectF rect;
    if (!m_frameSize.isEmpty() && width() > 0 && height() > 0) {
        const QSizeF turned = Orientation::turned(m_frameSize, m_rotation);
        const qreal scale = std::min(width() / turned.width(), height() / turned.height());
        const QSizeF size = turned * scale;
        rect = QRectF(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);
    }
    if (rect != m_pictureRect) {
        m_pictureRect = rect;
        emit pictureRectChanged();
    }
    update();
}

void Viewfinder::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    place();
}

QPointF Viewfinder::framePoint(qreal x, qreal y) const
{
    bool invertible = false;
    const QTransform inverse = transform().inverted(&invertible);
    if (!invertible || m_frameSize.isEmpty())
        return {-1, -1};
    const QPointF p = inverse.map(QPointF(x, y));
    if (p.x() < 0 || p.y() < 0 || p.x() > m_frameSize.width() || p.y() > m_frameSize.height())
        return {-1, -1};
    return {p.x() / m_frameSize.width(), p.y() / m_frameSize.height()};
}

QSGNode *Viewfinder::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    auto *node = static_cast<PictureNode *>(old);
    if ((!m_frame && m_still.isNull()) || m_pictureRect.isEmpty()) {
        delete node;
        return nullptr;
    }
    if (!node)
        node = new PictureNode;
    node->setMatrix(transform());
    node->m_picture->setRect(QRectF(QPointF(), QSizeF(m_frameSize)));

    QSGTexture *texture = nullptr;
    if (m_frame) {
        if (node->m_stream != m_frame->stream) {
            node->forget();
            node->m_stream = m_frame->stream;
        }
        if (!s_refused) {
            texture = node->m_textures.value(m_frame->buffer);
            if (!texture) {
                texture = importBuffer(window(), *m_frame);
                if (texture)
                    node->m_textures.insert(m_frame->buffer, texture);
                else
                    s_refused = true;
            }
        }
        if (!texture) {
            // Copied down to about the size it's drawn at, so the CPU moves a
            // few megabytes per frame rather than fifty.
            const qreal drawn = m_pictureRect.width() * window()->effectiveDevicePixelRatio();
            const QSize turned = Orientation::turned(m_frameSize, m_rotation);
            const int step = std::max(1, int(turned.width() / std::max<qreal>(1, drawn)));
            delete node->m_copied;
            node->m_copied = window()->createTextureFromImage(m_frame->reduced(step));
            texture = node->m_copied;
        }
    } else {
        if (m_stillChanged || !node->m_copied) {
            delete node->m_copied;
            node->m_copied = window()->createTextureFromImage(m_still);
            m_stillChanged = false;
        }
        texture = node->m_copied;
    }
    node->m_picture->setTexture(texture);
    node->m_picture->markDirty(QSGNode::DirtyMaterial);
    return node;
}
