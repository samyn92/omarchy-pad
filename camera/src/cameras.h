#pragma once

#include <QObject>
#include <QSet>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <memory>
#include <mutex>

#include "frame.h"

namespace libcamera {
class CameraManager;
class Camera;
}

class Focus;
class Session;

// The tablet's cameras through libcamera, one streaming at a time. It holds a
// camera only while the app is in use, so browsers and calls can have it
// otherwise, and hands each new frame to the viewfinder.
class Cameras : public QObject {
    Q_OBJECT
    // "starting", "running", "none" (no camera at all), "busy" (another
    // program has it) or "stopped" (the app is in the background).
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool front READ isFront NOTIFY cameraChanged)
    Q_PROPERTY(bool canSwitch READ canSwitch NOTIFY camerasChanged)
    Q_PROPERTY(bool canFocus READ canFocus NOTIFY cameraChanged)
    Q_PROPERTY(bool focusing READ isFocusing NOTIFY focusingChanged)
    Q_PROPERTY(QString model READ model NOTIFY cameraChanged)
    Q_PROPERTY(int sensorRotation READ sensorRotation NOTIFY cameraChanged)
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY cameraChanged)
    // The screen's clockwise turn from the panel's native orientation, set by
    // the UI, which knows its screen.
    Q_PROPERTY(int screenRotation READ screenRotation WRITE setScreenRotation NOTIFY screenRotationChanged)
    // How the frame turns to look right on that screen (and in the photo).
    Q_PROPERTY(int rotation READ rotation NOTIFY rotationChanged)
    Q_PROPERTY(QUrl lastPhoto READ lastPhoto NOTIFY lastPhotoChanged)
    Q_PROPERTY(int saving READ saving NOTIFY savingChanged)

public:
    explicit Cameras(const QString &photoFolder = {}, QObject *parent = nullptr);
    ~Cameras() override;

    QString state() const { return m_state; }
    bool isFront() const;
    bool canSwitch() const;
    bool canFocus() const;
    bool isFocusing() const;
    QString model() const;
    int sensorRotation() const;
    QSize frameSize() const { return m_frameSize; }
    int screenRotation() const { return m_screenRotation; }
    void setScreenRotation(int degrees);
    int rotation() const;
    QUrl lastPhoto() const { return m_lastPhoto; }
    int saving() const { return m_saving; }
    QString photoFolder() const { return m_photoFolder; }

    // The newest frame, or null; the viewfinder takes it on frameReady().
    FramePtr latest() const;

    // Opens the camera last used (the back one the first time) and streams.
    Q_INVOKABLE void start();
    // Lets the camera go, after the viewfinder has let go of its frames.
    Q_INVOKABLE void stop();
    Q_INVOKABLE void switchCamera();
    // Saves what the viewfinder shows now as a photo.
    Q_INVOKABLE void takePhoto();
    // Focuses on a point of the frame (0..1 across and down, before turning).
    Q_INVOKABLE void focusAt(qreal x, qreal y);
    Q_INVOKABLE void openLastPhoto();

    // The app went to the background (or came back): after a while there, the
    // camera is let go, and taken again on return.
    void setActive(bool active);

signals:
    void stateChanged();
    void cameraChanged();
    void camerasChanged();
    void focusingChanged();
    void screenRotationChanged();
    void rotationChanged();
    void lastPhotoChanged();
    void savingChanged();
    void frameReady();
    // Sent before a stream stops: let go of every frame now.
    void stopping();
    void photoTaken(const QUrl &photo);
    void photoFailed(const QString &why);

private:
    void setState(const QString &state);
    void open(int index);
    void deliver();
    void findLastPhoto();

    std::unique_ptr<libcamera::CameraManager> m_manager;
    std::vector<std::shared_ptr<libcamera::Camera>> m_cameras;
    int m_index = -1;
    std::unique_ptr<Session> m_session;
    std::unique_ptr<Focus> m_focus;
    QString m_state = "stopped";
    QSize m_frameSize;
    int m_screenRotation = 0;
    QString m_photoFolder;
    QUrl m_lastPhoto;
    int m_saving = 0;
    // Names handed out to photos still being written.
    QSet<QString> m_reserved;
    QTimer m_background;
    uint64_t m_streams = 0;

    mutable std::mutex m_lock;
    FramePtr m_latest;
    bool m_posted = false;
    friend class Session;
};
