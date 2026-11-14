#include "cameras.h"

#include <QDesktopServices>
#include <QDir>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QThreadPool>
#include <atomic>
#include <chrono>
#include <drm_fourcc.h>
// Qt's `slots` and `emit` keywords are macros, and libcamera uses the words
// itself.
#pragma push_macro("slots")
#pragma push_macro("emit")
#undef slots
#undef emit
#include <libcamera/libcamera.h>
#pragma pop_macro("emit")
#pragma pop_macro("slots")
#include <sys/mman.h>

#include "focus.h"
#include "orientation.h"
#include "photo.h"

Q_LOGGING_CATEGORY(lcCamera, "camera", QtInfoMsg)

using namespace libcamera;

// The pixel layouts the viewfinder and the photos can use as they come, in
// order of preference: the same bytes for the GPU (DRM fourcc) and for QImage.
struct Layout {
    PixelFormat libcamera;
    uint32_t fourcc;
    QImage::Format qt;
};
static const Layout layouts[] = {
    {PixelFormat(DRM_FORMAT_XBGR8888), DRM_FORMAT_XBGR8888, QImage::Format_RGBX8888},
    {PixelFormat(DRM_FORMAT_ABGR8888), DRM_FORMAT_ABGR8888, QImage::Format_RGBA8888},
    {PixelFormat(DRM_FORMAT_XRGB8888), DRM_FORMAT_XRGB8888, QImage::Format_RGB32},
    {PixelFormat(DRM_FORMAT_ARGB8888), DRM_FORMAT_ARGB8888, QImage::Format_ARGB32},
};

// One camera streaming: it is acquired and configured, its buffers are
// allocated and mapped, and its requests go round. Destroying it stops the
// stream and lets the camera go.
class Session {
public:
    Session(Cameras *owner, std::shared_ptr<Camera> camera, uint64_t id) : m_owner(owner), m_camera(std::move(camera)), m_id(id) {}
    ~Session();

    // Returns an error for the user ("busy") or empty when streaming.
    QString start();
    QSize size() const { return m_size; }

private:
    void completed(Request *request);
    void recycle(Request *request);

    Cameras *m_owner;
    std::shared_ptr<Camera> m_camera;
    uint64_t m_id;
    bool m_acquired = false;
    std::unique_ptr<CameraConfiguration> m_config;
    std::unique_ptr<FrameBufferAllocator> m_allocator;
    std::vector<std::unique_ptr<Request>> m_requests;
    std::vector<std::pair<void *, size_t>> m_maps;
    Layout m_layout = layouts[0];
    QSize m_size;
    int m_stride = 0;
    // Shared with every frame handed out: false once the stream stops, so a
    // frame let go late doesn't queue a request on a stopped camera.
    std::shared_ptr<std::atomic<bool>> m_running = std::make_shared<std::atomic<bool>>(false);
    // For the frame rate in the debug log.
    int m_frames = 0;
    std::chrono::steady_clock::time_point m_since;
};

QString Session::start()
{
    if (m_camera->acquire() != 0)
        return "busy";
    m_acquired = true;
    m_config = m_camera->generateConfiguration({StreamRole::Viewfinder});
    if (!m_config || m_config->empty())
        return "none";
    StreamConfiguration &stream = m_config->at(0);
    const StreamFormats &formats = stream.formats();
    bool found = false;
    for (const Layout &layout : layouts) {
        const auto offered = formats.pixelformats();
        if (std::find(offered.begin(), offered.end(), layout.libcamera) != offered.end()) {
            m_layout = layout;
            found = true;
            break;
        }
    }
    if (!found)
        return "none";
    stream.pixelFormat = m_layout.libcamera;
    // The whole sensor: on the Pad 4 its software ISP keeps 30 fps even at
    // 13 MP, and the shutter then saves a full photo from the viewfinder's
    // own frame, with no pause to switch modes.
    const SizeRange range = formats.range(m_layout.libcamera);
    if (range.max.width && range.max.height)
        stream.size = range.max;
    if (m_config->validate() == CameraConfiguration::Invalid || m_camera->configure(m_config.get()) != 0)
        return "none";
    m_size = QSize(int(stream.size.width), int(stream.size.height));
    m_stride = int(stream.stride);
    qCDebug(lcCamera) << "streaming" << QString::fromStdString(stream.toString()) << "stride" << m_stride
                      << "buffers" << stream.bufferCount;

    m_allocator = std::make_unique<FrameBufferAllocator>(m_camera);
    if (m_allocator->allocate(stream.stream()) < 0)
        return "none";
    const auto &buffers = m_allocator->buffers(stream.stream());
    for (size_t i = 0; i < buffers.size(); ++i) {
        const FrameBuffer::Plane &plane = buffers[i]->planes().front();
        const size_t length = plane.offset + plane.length;
        void *map = mmap(nullptr, length, PROT_READ, MAP_SHARED, plane.fd.get(), 0);
        if (map == MAP_FAILED)
            map = nullptr;
        m_maps.emplace_back(map, length);
        std::unique_ptr<Request> request = m_camera->createRequest(i);
        if (!request || request->addBuffer(stream.stream(), buffers[i].get()) != 0)
            return "none";
        m_requests.push_back(std::move(request));
    }
    m_camera->requestCompleted.connect(this, &Session::completed);
    if (m_camera->start() != 0)
        return "none";
    *m_running = true;
    m_since = std::chrono::steady_clock::now();
    for (auto &request : m_requests)
        m_camera->queueRequest(request.get());
    return {};
}

Session::~Session()
{
    if (*m_running) {
        *m_running = false;
        m_camera->stop();
    }
    m_camera->requestCompleted.disconnect(this);
    m_requests.clear();
    for (auto &[map, length] : m_maps)
        if (map)
            munmap(map, length);
    // Buffers go before the camera is let go, as libcamera asks.
    m_allocator.reset();
    if (m_acquired)
        m_camera->release();
}

// On libcamera's thread.
void Session::completed(Request *request)
{
    if (request->status() != Request::RequestComplete || !*m_running) {
        if (request->status() != Request::RequestCancelled)
            recycle(request);
        return;
    }
    if (lcCamera().isDebugEnabled() && ++m_frames == 90) {
        const auto now = std::chrono::steady_clock::now();
        qCDebug(lcCamera) << "fps" << m_frames / std::chrono::duration<double>(now - m_since).count();
        m_frames = 0;
        m_since = now;
    }
    const int index = int(request->cookie());
    const FrameBuffer *buffer = request->buffers().begin()->second;
    auto *frame = new Frame;
    frame->fd = buffer->planes().front().fd.get();
    frame->size = m_size;
    frame->stride = m_stride;
    frame->fourcc = m_layout.fourcc;
    frame->format = m_layout.qt;
    frame->data = static_cast<const uchar *>(m_maps[index].first);
    frame->length = m_maps[index].second;
    frame->buffer = index;
    frame->stream = m_id;
    frame->sequence = buffer->metadata().sequence;
    std::shared_ptr<std::atomic<bool>> running = m_running;
    std::shared_ptr<Camera> camera = m_camera;
    FramePtr shared(frame, [request, running, camera](const Frame *f) {
        delete f;
        // libcamera lets any thread queue a request.
        if (*running) {
            request->reuse(Request::ReuseBuffers);
            camera->queueRequest(request);
        }
    });

    // Only the newest frame waits for the UI; an older one still waiting goes
    // straight back to the camera when it's replaced here.
    FramePtr replaced;
    bool post = false;
    {
        std::lock_guard<std::mutex> lock(m_owner->m_lock);
        replaced = std::move(m_owner->m_latest);
        m_owner->m_latest = std::move(shared);
        post = !m_owner->m_posted;
        m_owner->m_posted = true;
    }
    if (post)
        QMetaObject::invokeMethod(m_owner, &Cameras::deliver, Qt::QueuedConnection);
}

void Session::recycle(Request *request)
{
    if (!*m_running)
        return;
    request->reuse(Request::ReuseBuffers);
    m_camera->queueRequest(request);
}

Cameras::Cameras(const QString &photoFolder, QObject *parent)
    : QObject(parent), m_photoFolder(photoFolder.isEmpty() ? Photo::defaultFolder() : photoFolder)
{
    m_background.setSingleShot(true);
    // Long enough that glancing at another window keeps the picture live,
    // short enough that a camera left open in the background frees itself.
    m_background.setInterval(15000);
    connect(&m_background, &QTimer::timeout, this, [this] {
        stop();
        setState("stopped");
    });
    findLastPhoto();
}

Cameras::~Cameras()
{
    emit stopping();
    {
        std::lock_guard<std::mutex> lock(m_lock);
        m_latest.reset();
    }
    m_focus.reset();
    m_session.reset();
    m_cameras.clear();
    if (m_manager)
        m_manager->stop();
}

void Cameras::setState(const QString &state)
{
    if (state == m_state)
        return;
    m_state = state;
    qCDebug(lcCamera) << "state" << state;
    emit stateChanged();
}

bool Cameras::isFront() const
{
    if (m_index < 0)
        return false;
    const auto location = m_cameras[m_index]->properties().get(properties::Location);
    return location && *location == properties::CameraLocationFront;
}

bool Cameras::canSwitch() const
{
    return m_cameras.size() > 1;
}

bool Cameras::canFocus() const
{
    return m_focus != nullptr;
}

bool Cameras::isFocusing() const
{
    return m_focus && m_focus->isBusy();
}

QString Cameras::model() const
{
    if (m_index < 0)
        return {};
    const auto model = m_cameras[m_index]->properties().get(properties::Model);
    return model ? QString::fromStdString(std::string(*model)) : QString();
}

int Cameras::sensorRotation() const
{
    if (m_index < 0)
        return 0;
    const auto rotation = m_cameras[m_index]->properties().get(properties::Rotation);
    return rotation ? *rotation : 0;
}

void Cameras::setScreenRotation(int degrees)
{
    if (degrees == m_screenRotation)
        return;
    m_screenRotation = degrees;
    qCDebug(lcCamera) << "screen turned" << degrees << "so the frame turns" << rotation();
    emit screenRotationChanged();
    emit rotationChanged();
}

int Cameras::rotation() const
{
    return Orientation::frameRotation(sensorRotation(), m_screenRotation);
}

FramePtr Cameras::latest() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_latest;
}

void Cameras::start()
{
    m_background.stop();
    if (m_session)
        return;
    if (!m_manager) {
        m_manager = std::make_unique<CameraManager>();
        if (m_manager->start() != 0) {
            m_manager.reset();
            setState("none");
            return;
        }
        // Back cameras first: that's the one a camera app opens with.
        m_cameras = m_manager->cameras();
        std::stable_sort(m_cameras.begin(), m_cameras.end(), [](const auto &a, const auto &b) {
            auto rank = [](const std::shared_ptr<Camera> &c) {
                const auto location = c->properties().get(properties::Location);
                return !location ? 2 : *location == properties::CameraLocationBack ? 0 : 1;
            };
            return rank(a) < rank(b);
        });
        qCDebug(lcCamera) << "cameras" << m_cameras.size();
        emit camerasChanged();
    }
    if (m_cameras.empty()) {
        setState("none");
        return;
    }
    open(m_index < 0 ? 0 : m_index);
}

void Cameras::open(int index)
{
    setState("starting");
    m_index = index;
    auto session = std::make_unique<Session>(this, m_cameras[index], ++m_streams);
    const QString error = session->start();
    if (!error.isEmpty()) {
        qCWarning(lcCamera) << "can't stream camera" << QString::fromStdString(m_cameras[index]->id()) << error;
        session.reset();
        m_frameSize = {};
        emit cameraChanged();
        setState(error);
        return;
    }
    m_session = std::move(session);
    m_frameSize = m_session->size();
    m_focus.reset();
    if (std::unique_ptr<Lens> lens = Lens::find(QString::fromStdString(m_cameras[index]->id()))) {
        m_focus = std::make_unique<Focus>(std::move(lens));
        connect(m_focus.get(), &Focus::busyChanged, this, &Cameras::focusingChanged);
    }
    emit cameraChanged();
    emit rotationChanged();
    setState("running");
}

void Cameras::stop()
{
    m_background.stop();
    if (!m_session)
        return;
    // Everyone lets go of their frames first: the buffers are freed below.
    emit stopping();
    {
        std::lock_guard<std::mutex> lock(m_lock);
        m_latest.reset();
        m_posted = false;
    }
    if (m_focus && m_focus->isBusy())
        emit focusingChanged();
    m_focus.reset();
    m_session.reset();
    setState("stopped");
}

void Cameras::switchCamera()
{
    if (m_cameras.size() < 2)
        return;
    const int next = (m_index + 1) % int(m_cameras.size());
    stop();
    open(next);
}

void Cameras::deliver()
{
    FramePtr frame;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        m_posted = false;
        frame = m_latest;
    }
    if (!frame || !m_session)
        return;
    if (m_focus)
        m_focus->frame(frame);
    emit frameReady();
}

void Cameras::takePhoto()
{
    const FramePtr frame = latest();
    if (!frame) {
        emit photoFailed(tr("No picture yet"));
        return;
    }
    // Copied out now, so the buffer goes back to the camera at once and the
    // slow part (turning, encoding, writing) happens off the UI thread.
    const QImage picture = frame->copy();
    if (picture.isNull()) {
        emit photoFailed(tr("The camera's picture can't be read"));
        return;
    }
    if (!QDir().mkpath(m_photoFolder)) {
        emit photoFailed(tr("Can't make %1").arg(m_photoFolder));
        return;
    }
    const QDateTime when = QDateTime::currentDateTime();
    const QString name = Photo::nextName(m_photoFolder, when, m_reserved);
    m_reserved.insert(name);
    const QString path = QDir(m_photoFolder).filePath(name);
    const int rotation = this->rotation();
    ++m_saving;
    emit savingChanged();
    QThreadPool::globalInstance()->start([this, picture, rotation, when, path, name] {
        const QByteArray jpeg = Photo::jpeg(Orientation::photo(picture, rotation).convertToFormat(QImage::Format_RGB888), when);
        QSaveFile file(path);
        const bool saved = !jpeg.isEmpty() && file.open(QIODevice::WriteOnly) && file.write(jpeg) == jpeg.size()
                           && file.commit();
        QMetaObject::invokeMethod(this, [this, saved, path, name] {
            m_reserved.remove(name);
            --m_saving;
            emit savingChanged();
            if (!saved) {
                qCWarning(lcCamera) << "couldn't save" << path;
                emit photoFailed(tr("Couldn't save the photo"));
                return;
            }
            qCDebug(lcCamera) << "saved" << path;
            m_lastPhoto = QUrl::fromLocalFile(path);
            emit lastPhotoChanged();
            emit photoTaken(m_lastPhoto);
        });
    });
}

void Cameras::focusAt(qreal x, qreal y)
{
    if (m_focus && m_session)
        m_focus->start(QPointF(x, y));
}

void Cameras::openLastPhoto()
{
    if (!m_lastPhoto.isEmpty())
        QDesktopServices::openUrl(m_lastPhoto);
}

void Cameras::setActive(bool active)
{
    if (active) {
        m_background.stop();
        if (m_state == "stopped" || m_state == "busy")
            start();
    } else if (m_session) {
        m_background.start();
    }
}

void Cameras::findLastPhoto()
{
    const QString newest = Photo::newest(m_photoFolder);
    m_lastPhoto = newest.isEmpty() ? QUrl() : QUrl::fromLocalFile(newest);
}
