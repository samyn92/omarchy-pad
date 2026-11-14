#include "orientation.h"

namespace Orientation {

static int normalized(int degrees)
{
    return ((degrees % 360) + 360) % 360 / 90 * 90;
}

int frameRotation(int sensorRotation, int screenRotation)
{
    // Turning the screen clockwise turns everything drawn on it clockwise as
    // well, so the frame turns back by the same amount.
    return normalized(sensorRotation - screenRotation);
}

QSize turned(const QSize &size, int rotation)
{
    return rotation % 180 ? size.transposed() : size;
}

QTransform preview(int rotation, bool front, const QSizeF &frameSize)
{
    // Maps the frame's own rectangle onto the turned (and mirrored) one,
    // starting at the origin, so the caller only scales and places it.
    const QSizeF out = rotation % 180 ? frameSize.transposed() : frameSize;
    QTransform t;
    if (front)
        t.translate(out.width(), 0).scale(-1, 1);
    t.translate(out.width() / 2, out.height() / 2);
    t.rotate(rotation);
    t.translate(-frameSize.width() / 2, -frameSize.height() / 2);
    return t;
}

QImage photo(const QImage &frame, int rotation)
{
    if (rotation == 0)
        return frame;
    return frame.transformed(QTransform().rotate(rotation));
}

} // namespace Orientation
