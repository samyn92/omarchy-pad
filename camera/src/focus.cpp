#include "focus.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRect>
#include <algorithm>
#include <cmath>
#include <fcntl.h>
#include <linux/media.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

Q_DECLARE_LOGGING_CATEGORY(lcCamera)

void FocusSweep::begin(int minimum, int maximum, int coarseSteps, int fineSteps)
{
    m_minimum = minimum;
    m_maximum = std::max(minimum, maximum);
    m_step = std::max(1, (m_maximum - m_minimum) / std::max(1, coarseSteps - 1));
    m_fineSteps = std::max(2, fineSteps);
    m_position = m_minimum;
    m_end = m_maximum;
    m_fine = false;
    m_done = false;
    m_best = m_minimum;
    m_bestScore = -1;
    m_worse = 0;
}

bool FocusSweep::feed(double sharpness)
{
    if (m_done)
        return true;
    if (sharpness > m_bestScore) {
        m_bestScore = sharpness;
        m_best = m_position;
        m_worse = 0;
    } else if (!m_fine && sharpness < m_bestScore * 0.6) {
        // Two steps well below the best so far: the sharpest point is behind
        // us, and the rest of the coarse pass would only cost time.
        ++m_worse;
    }
    next();
    return m_done;
}

void FocusSweep::next()
{
    if (!m_fine && m_position + m_step <= m_end && m_worse < 2) {
        m_position += m_step;
        return;
    }
    if (!m_fine) {
        // The fine pass covers half a coarse step to either side of the best:
        // the sharpest point is nearer the best than its neighbours.
        m_fine = true;
        const int from = std::max(m_minimum, m_best - m_step / 2);
        m_end = std::min(m_maximum, m_best + m_step / 2);
        m_step = std::max(1, (m_end - from) / m_fineSteps);
        m_position = from;
        if (m_position == m_best)
            m_position += m_step;
        if (m_position <= m_end)
            return;
    } else {
        m_position += m_step;
        if (m_position == m_best)
            m_position += m_step;
        if (m_position <= m_end)
            return;
    }
    m_done = true;
    m_position = m_best;
}

double sharpness(const QImage &picture)
{
    const QImage grey = picture.convertToFormat(QImage::Format_Grayscale8);
    if (grey.width() < 3 || grey.height() < 3)
        return 0;
    double energy = 0;
    double brightness = 0;
    for (int y = 0; y < grey.height() - 1; ++y) {
        const uchar *row = grey.constScanLine(y);
        const uchar *below = grey.constScanLine(y + 1);
        for (int x = 0; x < grey.width() - 1; ++x) {
            const int dx = row[x + 1] - row[x];
            const int dy = below[x] - row[x];
            energy += dx * dx + dy * dy;
            brightness += row[x];
        }
    }
    const double pixels = double(grey.width() - 1) * (grey.height() - 1);
    const double mean = brightness / pixels;
    return energy / pixels / (mean * mean + 1);
}

// The /dev node for a character device, through sysfs.
static QString deviceNode(uint32_t major, uint32_t minor)
{
    const QString sys = QStringLiteral("/sys/dev/char/%1:%2").arg(major).arg(minor);
    const QString target = QFileInfo(sys).symLinkTarget();
    return target.isEmpty() ? QString() : "/dev/" + QFileInfo(target).fileName();
}

// The device tree node of a character device, as libcamera names cameras
// ("/base/soc@0/…/camera@36").
static QString deviceTreeNode(uint32_t major, uint32_t minor)
{
    const QString node = QFileInfo(QStringLiteral("/sys/dev/char/%1:%2/device/of_node").arg(major).arg(minor))
                             .canonicalFilePath();
    const QString prefix = QStringLiteral("/sys/firmware/devicetree");
    return node.startsWith(prefix) ? node.mid(prefix.size()) : QString();
}

static QString linkedLens(const QString &media, const QString &cameraId)
{
    const int fd = ::open(qPrintable(media), O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return {};
    media_v2_topology topology = {};
    QString lens;
    if (ioctl(fd, MEDIA_IOC_G_TOPOLOGY, &topology) == 0) {
        std::vector<media_v2_entity> entities(topology.num_entities);
        std::vector<media_v2_interface> interfaces(topology.num_interfaces);
        std::vector<media_v2_link> links(topology.num_links);
        topology.ptr_entities = reinterpret_cast<uintptr_t>(entities.data());
        topology.ptr_interfaces = reinterpret_cast<uintptr_t>(interfaces.data());
        topology.ptr_links = reinterpret_cast<uintptr_t>(links.data());
        if (ioctl(fd, MEDIA_IOC_G_TOPOLOGY, &topology) == 0) {
            auto devnodeOf = [&](uint32_t entity) -> const media_v2_interface * {
                for (const auto &link : links) {
                    if ((link.flags & MEDIA_LNK_FL_LINK_TYPE) != MEDIA_LNK_FL_INTERFACE_LINK || link.sink_id != entity)
                        continue;
                    for (const auto &interface : interfaces)
                        if (interface.id == link.source_id)
                            return &interface;
                }
                return nullptr;
            };
            for (const auto &sensor : entities) {
                if (sensor.function != MEDIA_ENT_F_CAM_SENSOR)
                    continue;
                const media_v2_interface *node = devnodeOf(sensor.id);
                if (!node || deviceTreeNode(node->devnode.major, node->devnode.minor) != cameraId)
                    continue;
                for (const auto &link : links) {
                    if ((link.flags & MEDIA_LNK_FL_LINK_TYPE) != MEDIA_LNK_FL_ANCILLARY_LINK
                        || link.source_id != sensor.id)
                        continue;
                    for (const auto &entity : entities)
                        if (entity.id == link.sink_id && entity.function == MEDIA_ENT_F_LENS)
                            if (const media_v2_interface *lensNode = devnodeOf(entity.id))
                                lens = deviceNode(lensNode->devnode.major, lensNode->devnode.minor);
                }
            }
        }
    }
    ::close(fd);
    return lens;
}

std::unique_ptr<Lens> Lens::find(const QString &cameraId)
{
    const QString chosen = qEnvironmentVariable("OMARCHY_PAD_CAMERA_LENS");
    if (!chosen.isEmpty())
        return open(chosen);
    const QStringList medias = QDir("/dev").entryList({"media*"}, QDir::System);
    for (const QString &media : medias) {
        const QString lens = linkedLens("/dev/" + media, cameraId);
        if (!lens.isEmpty())
            return open(lens);
    }
    return nullptr;
}

std::unique_ptr<Lens> Lens::open(const QString &device)
{
    const int fd = ::open(qPrintable(device), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        qCWarning(lcCamera) << "can't open the lens" << device;
        return nullptr;
    }
    v4l2_queryctrl query = {};
    query.id = V4L2_CID_FOCUS_ABSOLUTE;
    if (ioctl(fd, VIDIOC_QUERYCTRL, &query) != 0 || query.maximum <= query.minimum) {
        qCWarning(lcCamera) << device << "has no focus control";
        ::close(fd);
        return nullptr;
    }
    std::unique_ptr<Lens> lens(new Lens);
    lens->m_fd = fd;
    lens->m_minimum = query.minimum;
    lens->m_maximum = query.maximum;
    qCDebug(lcCamera) << "lens" << device << "from" << query.minimum << "to" << query.maximum;
    return lens;
}

Lens::~Lens()
{
    if (m_fd >= 0)
        ::close(m_fd);
}

bool Lens::move(int position)
{
    v4l2_control control = {};
    control.id = V4L2_CID_FOCUS_ABSOLUTE;
    control.value = std::clamp(position, m_minimum, m_maximum);
    return ioctl(m_fd, VIDIOC_S_CTRL, &control) == 0;
}

Focus::Focus(std::unique_ptr<Lens> lens, QObject *parent) : QObject(parent), m_lens(std::move(lens)) {}

Focus::~Focus() = default;

void Focus::start(const QPointF &point)
{
    m_point = QPointF(std::clamp(point.x(), 0.0, 1.0), std::clamp(point.y(), 0.0, 1.0));
    m_sweep.begin(m_lens->minimum(), m_lens->maximum());
    m_lens->move(m_sweep.position());
    // The frame being exposed while the lens moves is blurred by the move; the
    // one after it is the first to show the new position.
    m_skip = 2;
    if (!m_busy) {
        m_busy = true;
        emit busyChanged();
    }
}

void Focus::frame(const FramePtr &frame)
{
    if (!m_busy || !frame)
        return;
    if (m_skip > 0) {
        --m_skip;
        return;
    }
    // A spot an eighth of the frame across around the tap, enough for detail
    // and small enough to copy out quickly on every frame.
    const int side = std::max(64, frame->size.width() / 8);
    QRect spot(0, 0, side, side);
    spot.moveCenter(QPoint(int(m_point.x() * frame->size.width()), int(m_point.y() * frame->size.height())));
    spot = spot.intersected(QRect(QPoint(), frame->size));
    const double score = sharpness(frame->copy(spot));
    qCDebug(lcCamera) << "focus at" << m_sweep.position() << "sharpness" << score;
    if (m_sweep.feed(score)) {
        m_lens->move(m_sweep.best());
        qCDebug(lcCamera) << "focused at" << m_sweep.best();
        m_busy = false;
        emit busyChanged();
        return;
    }
    m_lens->move(m_sweep.position());
    m_skip = 2;
}
