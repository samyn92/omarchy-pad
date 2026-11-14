#include "frame.h"

#include <linux/dma-buf.h>
#include <sys/ioctl.h>

// Brackets a CPU read of a frame's buffer: the camera wrote it behind the
// CPU's caches, so the kernel is told a read starts and ends and can make the
// caches agree.
namespace {
class Reading {
public:
    explicit Reading(int fd) : m_fd(fd) { sync(DMA_BUF_SYNC_START); }
    ~Reading() { sync(DMA_BUF_SYNC_END); }

private:
    void sync(uint64_t when)
    {
        if (m_fd < 0)
            return;
        dma_buf_sync sync = {};
        sync.flags = when | DMA_BUF_SYNC_READ;
        ioctl(m_fd, DMA_BUF_IOCTL_SYNC, &sync);
    }
    int m_fd;
};
} // namespace

QImage Frame::copy(const QRect &part) const
{
    if (!data || format == QImage::Format_Invalid)
        return {};
    const QRect area = part.isNull() ? QRect(QPoint(), size) : part.intersected(QRect(QPoint(), size));
    if (area.isEmpty())
        return {};
    Reading reading(fd);
    const int bytesPerPixel = QImage::toPixelFormat(format).bitsPerPixel() / 8;
    const uchar *first = data + size_t(area.y()) * stride + size_t(area.x()) * bytesPerPixel;
    return QImage(first, area.width(), area.height(), stride, format).copy();
}

QImage Frame::reduced(int step) const
{
    if (!data || QImage::toPixelFormat(format).bitsPerPixel() != 32 || step < 1)
        return {};
    QImage small(size.width() / step, size.height() / step, format);
    if (small.isNull())
        return {};
    Reading reading(fd);
    for (int y = 0; y < small.height(); ++y) {
        const auto *from = reinterpret_cast<const uint32_t *>(data + size_t(y) * step * stride);
        auto *to = reinterpret_cast<uint32_t *>(small.scanLine(y));
        for (int x = 0; x < small.width(); ++x)
            to[x] = from[size_t(x) * step];
    }
    return small;
}
