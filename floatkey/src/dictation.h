#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>

class QAudioSource;
class QIODevice;

// Voice typing through Superwhisper. The keyboard's voice orb starts and stops
// a dictation with the `superwhisper` command, the same one Omarchy's dictation
// key runs, and follows it through the daemon's control socket: listening,
// working on the text, done. Superwhisper types the text itself, through its
// own fcitx5 addon, into whatever field has focus. While it listens floatkey
// also opens the microphone, only to measure how loud the voice is, so the orb
// can glow with it.
class Dictation : public QObject {
    Q_OBJECT
    // The `superwhisper` command is installed.
    Q_PROPERTY(bool available READ isAvailable CONSTANT)
    // idle, listening, working (turning speech into text) or done (for a
    // moment after the text went in).
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    // How loud the voice is right now, 0 to 1, smoothed for drawing.
    Q_PROPERTY(double level READ level NOTIFY levelChanged)
    // How long this dictation has been listening, in seconds.
    Q_PROPERTY(double seconds READ seconds NOTIFY secondsChanged)
    // Keeps an eye on the daemon while the keyboard is up, so a dictation
    // started from Omarchy's key shows on the orb too.
    Q_PROPERTY(bool watching READ isWatching WRITE setWatching NOTIFY watchingChanged)

public:
    explicit Dictation(const QString &socketPath = defaultSocketPath(), const QString &program = defaultProgram(),
                       QObject *parent = nullptr);
    ~Dictation() override;

    static QString defaultSocketPath();
    static QString defaultProgram();
    // What the orb shows for one status reply from the daemon.
    static QString stateFor(const QJsonObject &status);
    // A level from 0 to 1 for a block of 16-bit samples.
    static double loudness(const QByteArray &samples);

    bool isAvailable() const { return !m_program.isEmpty(); }
    QString state() const { return m_state; }
    double level() const { return m_level; }
    double seconds() const { return m_seconds; }
    bool isWatching() const { return m_watching; }
    void setWatching(bool watching);
    // Measuring the voice opens the microphone; the tests do without.
    void setMetering(bool on) { m_metering = on; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void toggle();

signals:
    void stateChanged();
    void levelChanged();
    void secondsChanged();
    void watchingChanged();

private:
    void run(const QString &verb);
    void poll();
    void received();
    void apply(const QJsonObject &status);
    void setState(const QString &state);
    void setLevel(double level);
    void schedule();
    void meter(bool on);
    void readAudio();

    QString m_socketPath;
    QString m_program;
    QLocalSocket m_socket;
    QByteArray m_buffer;
    QTimer m_poll;
    QTimer m_done;
    QElapsedTimer m_asked;

    QString m_state = "idle";
    // The state a tap just left, which a status reply that was already on its
    // way doesn't bring back.
    QString m_left;
    QElapsedTimer m_leftAt;
    QElapsedTimer m_listeningSince;
    double m_level = 0;
    double m_seconds = 0;
    bool m_watching = false;

    bool m_metering = true;
    QAudioSource *m_audio = nullptr;
    QIODevice *m_audioInput = nullptr;
};
