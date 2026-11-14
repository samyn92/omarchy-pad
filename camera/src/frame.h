#pragma once

#include <QImage>
#include <QSize>
#include <cstdint>
#include <memory>

// One finished picture from the camera, still in the camera's own buffer (a
// dmabuf). Whoever holds it keeps that buffer away from the camera; letting go
// hands it back to be filled again. Frames never outlive the stream they came
// from: Cameras asks everyone to let go before it stops one.
struct Frame {
    int fd = -1;
    QSize size;
    int stride = 0;
    uint32_t fourcc = 0; // DRM fourcc, for the GPU
    QImage::Format format = QImage::Format_Invalid; // the same layout, for the CPU
    const uchar *data = nullptr; // a read-only mapping of the whole buffer
    size_t length = 0;
    int buffer = 0; // which of the stream's buffers, so textures can be kept per buffer
    uint64_t stream = 0; // which stream, so a new stream's buffers aren't taken for old ones
    uint64_t sequence = 0;

    // The picture as an image the CPU can read, copied out of the buffer
    // (so it stays good after the frame is let go), optionally only `part`.
    QImage copy(const QRect &part = {}) const;
    // The picture made smaller by keeping every `step`th pixel across and
    // down: quick enough to do on every frame.
    QImage reduced(int step) const;
};

using FramePtr = std::shared_ptr<const Frame>;
