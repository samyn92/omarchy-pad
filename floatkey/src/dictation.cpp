#include "dictation.h"

#include <QAudioFormat>
#include <QAudioSource>
#include <QJsonDocument>
#include <QMediaDevices>
#include <QProcess>
#include <QStandardPaths>
#include <cmath>

// How often the daemon is asked while something is happening, and while the
// keyboard is merely up.
static constexpr int busyPoll = 120;
static constexpr int idlePoll = 900;
// How long a reply may contradict a tap before it is believed.
static constexpr int tapWins = 1500;
static constexpr int doneFor = 900;

Dictation::Dictation(const QString &socketPath, const QString &program, QObject *parent)
    : QObject(parent), m_socketPath(socketPath), m_program(program)
{
    m_poll.setSingleShot(true);
    connect(&m_poll, &QTimer::timeout, this, &Dictation::poll);
    m_done.setSingleShot(true);
    m_done.setInterval(doneFor);
    connect(&m_done, &QTimer::timeout, this, [this] { setState("idle"); });

    connect(&m_socket, &QLocalSocket::connected, this, [this] {
        m_socket.write(R"({"version":1,"command":"status"})" "\n");
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, &Dictation::received);
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this] {
        m_socket.abort();
        // No daemon: nothing is being dictated.
        apply({});
    });
}

Dictation::~Dictation()
{
    meter(false);
}

QString Dictation::defaultSocketPath()
{
    const QString runtime = qEnvironmentVariable("SUPERWHISPER_RUNTIME");
    if (!runtime.isEmpty())
        return runtime + "/control.sock";
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/superwhisper/control.sock";
}

QString Dictation::defaultProgram()
{
    return QStandardPaths::findExecutable("superwhisper");
}

QString Dictation::stateFor(const QJsonObject &status)
{
    const QString phase = status.value("phase").toString();
    if (phase.isEmpty() || phase == "idle")
        return "idle";
    // Warming up records already: the voice is not lost.
    if (phase == "recording" || phase == "warming")
        return "listening";
    return "working";
}

double Dictation::loudness(const QByteArray &samples)
{
    const auto *data = reinterpret_cast<const qint16 *>(samples.constData());
    const qsizetype count = samples.size() / 2;
    if (count == 0)
        return 0;
    double sum = 0;
    for (qsizetype i = 0; i < count; ++i)
        sum += double(data[i]) * data[i];
    const double rms = std::sqrt(sum / count) / 32768.0;
    if (rms <= 0)
        return 0;
    // Quiet room noise sits below -55 dB, a raised voice near -15 dB.
    return qBound(0.0, (20 * std::log10(rms) + 55) / 40, 1.0);
}

void Dictation::setWatching(bool watching)
{
    if (watching == m_watching)
        return;
    m_watching = watching;
    emit watchingChanged();
    if (watching)
        poll();
}

void Dictation::start()
{
    if (!isAvailable())
        return;
    run("start");
    m_left = "idle";
    m_leftAt.start();
    m_listeningSince.start();
    m_seconds = 0;
    emit secondsChanged();
    setState("listening");
}

void Dictation::stop()
{
    if (m_state != "listening")
        return;
    run("stop");
    m_left = "listening";
    m_leftAt.start();
    setState("working");
}

void Dictation::cancel()
{
    if (m_state != "listening" && m_state != "working")
        return;
    run("cancel");
    m_left = "listening";
    m_leftAt.start();
    setState("idle");
}

void Dictation::toggle()
{
    if (m_state == "listening")
        stop();
    else if (m_state == "idle" || m_state == "done")
        start();
}

void Dictation::run(const QString &verb)
{
    QProcess::startDetached(m_program, {verb});
}

void Dictation::poll()
{
    if (m_socket.state() != QLocalSocket::UnconnectedState) {
        if (m_asked.elapsed() < 1000)
            return;
        m_socket.abort();
    }
    m_buffer.clear();
    m_asked.start();
    m_socket.connectToServer(m_socketPath);
}

void Dictation::received()
{
    m_buffer += m_socket.readAll();
    const qsizetype end = m_buffer.indexOf('\n');
    if (end < 0)
        return;
    const QJsonObject status = QJsonDocument::fromJson(m_buffer.left(end)).object();
    m_socket.abort();
    apply(status);
}

void Dictation::apply(const QJsonObject &status)
{
    const QString next = stateFor(status);
    if (!m_left.isEmpty()) {
        if (next == m_left && m_leftAt.elapsed() < tapWins) {
            schedule();
            return;
        }
        m_left.clear();
    }
    if (next == "listening") {
        const double seconds = status.contains("recording_seconds")
            ? status.value("recording_seconds").toDouble()
            : m_listeningSince.isValid() ? m_listeningSince.elapsed() / 1000.0 : 0;
        if (m_state != "listening")
            m_listeningSince.start();
        if (seconds != m_seconds) {
            m_seconds = seconds;
            emit secondsChanged();
        }
    }
    if (next == "idle" && m_state == "working")
        setState("done");
    else if (!(next == "idle" && m_state == "done"))
        setState(next);
    schedule();
}

void Dictation::setState(const QString &state)
{
    if (state == "done")
        m_done.start();
    else
        m_done.stop();
    if (state != m_state) {
        m_state = state;
        meter(state == "listening");
        emit stateChanged();
    }
    schedule();
}

void Dictation::setLevel(double level)
{
    if (qAbs(level - m_level) < 0.002 && level != 0)
        return;
    m_level = level;
    emit levelChanged();
}

void Dictation::schedule()
{
    const bool busy = m_state == "listening" || m_state == "working" || !m_left.isEmpty();
    if (!busy && !m_watching) {
        m_poll.stop();
        return;
    }
    const int interval = busy ? busyPoll : idlePoll;
    if (!m_poll.isActive() || m_poll.remainingTime() > interval)
        m_poll.start(interval);
}

void Dictation::meter(bool on)
{
    if (!on) {
        if (m_audio) {
            m_audio->stop();
            m_audio->deleteLater();
            m_audio = nullptr;
            m_audioInput = nullptr;
        }
        setLevel(0);
        return;
    }
    if (m_audio || !m_metering)
        return;
    const QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (device.isNull())
        return;
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    m_audio = new QAudioSource(device, format, this);
    // A block every 30 ms or so, smooth enough for the glow.
    m_audio->setBufferSize(16000 * 2 * 30 / 1000);
    m_audioInput = m_audio->start();
    if (!m_audioInput) {
        m_audio->deleteLater();
        m_audio = nullptr;
        return;
    }
    connect(m_audioInput, &QIODevice::readyRead, this, &Dictation::readAudio);
}

void Dictation::readAudio()
{
    if (!m_audioInput)
        return;
    const QByteArray samples = m_audioInput->readAll();
    if (samples.isEmpty())
        return;
    const double target = loudness(samples);
    // Quick to rise with the voice, slower to fall back.
    setLevel(m_level + (target - m_level) * (target > m_level ? 0.6 : 0.18));
}
