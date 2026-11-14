#pragma once

#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QTransform>
#include <atomic>

#include "cameras.h"
#include "frame.h"

// The live picture: each new frame drawn straight from the camera's buffer on
// the GPU, turned upright for the screen, mirrored for a front camera, and
// fitted whole into the item (as the photo will be).
class Viewfinder : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(Cameras *source READ cameras WRITE setCameras NOTIFY camerasChanged)
    // Where the picture is drawn inside the item, for the controls around it.
    Q_PROPERTY(QRectF pictureRect READ pictureRect NOTIFY pictureRectChanged)
    // True while the last picture is shown frozen (the camera is switching or
    // stopped).
    Q_PROPERTY(bool frozen READ isFrozen NOTIFY frozenChanged)

public:
    explicit Viewfinder(QQuickItem *parent = nullptr);
    ~Viewfinder() override;

    Cameras *cameras() const { return m_cameras; }
    void setCameras(Cameras *cameras);
    QRectF pictureRect() const { return m_pictureRect; }
    bool isFrozen() const { return !m_frame && !m_still.isNull(); }

    // A point of the item as a point of the frame (0..1 across and down,
    // before turning), or (-1, -1) outside the picture.
    Q_INVOKABLE QPointF framePoint(qreal x, qreal y) const;

    // Set when the GPU wouldn't take a camera buffer as a texture: frames are
    // then copied down to the item's size and uploaded instead.
    static std::atomic<bool> s_refused;

signals:
    void camerasChanged();
    void pictureRectChanged();
    void frozenChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void take();
    void freeze();
    void place();
    QTransform transform() const;

    QPointer<Cameras> m_cameras;
    FramePtr m_frame;
    // The frame drawn before, kept until the GPU is surely done with it.
    FramePtr m_previous;
    // What shows while there is no live frame: the last one, made small.
    QImage m_still;
    bool m_stillChanged = false;
    QSize m_frameSize;
    int m_rotation = 0;
    bool m_front = false;
    QRectF m_pictureRect;
};
