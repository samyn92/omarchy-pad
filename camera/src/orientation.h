#pragma once

#include <QImage>
#include <QTransform>

// Where a camera's picture has to turn to look right on the screen and in the
// saved photo.
//
// libcamera states each sensor's mounting as `Rotation`: the clockwise turn
// that makes its image upright with the tablet in its native orientation (as
// Android's SENSOR_ORIENTATION). The tablet session turns the screen with the
// tablet, so the UI's "up" is the world's up, and a picture turned to fit the
// screen is also upright as a photo.
namespace Orientation {

// The clockwise turn, in degrees (0, 90, 180 or 270), that brings a frame from
// a sensor mounted at `sensorRotation` upright on a screen turned
// `screenRotation` degrees clockwise from the panel's native orientation.
int frameRotation(int sensorRotation, int screenRotation);

// How a frame is drawn in the viewfinder: turned upright, and for a front
// camera mirrored left to right, as a mirror would show the person holding it.
QTransform preview(int rotation, bool front, const QSizeF &frameSize);

// The saved photo: turned upright, never mirrored, so text reads the right way
// and the photo shows the scene as others see it.
QImage photo(const QImage &frame, int rotation);

// A frame's size once turned by `rotation`.
QSize turned(const QSize &size, int rotation);

} // namespace Orientation
