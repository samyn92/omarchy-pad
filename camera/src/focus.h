#pragma once

#include <QImage>
#include <QObject>
#include <QPointF>
#include <QString>
#include <memory>

#include "frame.h"

// Where to try the lens next while looking for the sharpest picture: a coarse
// pass over the whole range, stopping early once the picture has clearly gone
// past its sharpest, then a fine pass around the best coarse position.
class FocusSweep {
public:
    void begin(int minimum, int maximum, int coarseSteps = 10, int fineSteps = 4);
    // The lens position the next sharpness is measured at.
    int position() const { return m_position; }
    // The sharpness measured at position(); true once the sweep is over and
    // best() is where the lens should stay.
    bool feed(double sharpness);
    int best() const { return m_best; }
    bool done() const { return m_done; }

private:
    void next();

    int m_minimum = 0;
    int m_maximum = 0;
    int m_position = 0;
    int m_step = 0;
    int m_fineSteps = 0;
    int m_end = 0;
    bool m_fine = false;
    bool m_done = true;
    int m_best = 0;
    double m_bestScore = -1;
    int m_worse = 0;
};

// How sharp a picture is: the energy of the brightness steps between
// neighbouring pixels, divided by the brightness itself so the exposure
// settling while the lens moves doesn't count as focus.
double sharpness(const QImage &picture);

// A camera's focus motor, as a V4L2 subdevice with FOCUS_ABSOLUTE.
class Lens {
public:
    // The lens the kernel links to the sensor whose device tree node is
    // `cameraId` (libcamera's id for the camera), or null. The environment
    // variable OMARCHY_PAD_CAMERA_LENS=/dev/v4l-subdevN names one by hand,
    // for trying a lens whose link the device tree doesn't have yet.
    static std::unique_ptr<Lens> find(const QString &cameraId);
    static std::unique_ptr<Lens> open(const QString &device);
    ~Lens();

    int minimum() const { return m_minimum; }
    int maximum() const { return m_maximum; }
    bool move(int position);

private:
    Lens() = default;
    int m_fd = -1;
    int m_minimum = 0;
    int m_maximum = 0;
};

// Tap to focus: moves the lens through a sweep, measuring each position on the
// frames that follow, and leaves it where the tapped spot was sharpest.
class Focus : public QObject {
    Q_OBJECT
public:
    explicit Focus(std::unique_ptr<Lens> lens, QObject *parent = nullptr);
    ~Focus() override;

    bool isBusy() const { return m_busy; }
    // Starts a sweep for the spot at `point` (0..1 across and down the frame).
    void start(const QPointF &point);
    // Each new frame while busy.
    void frame(const FramePtr &frame);

signals:
    void busyChanged();

private:
    std::unique_ptr<Lens> m_lens;
    FocusSweep m_sweep;
    QPointF m_point;
    bool m_busy = false;
    int m_skip = 0;
};
