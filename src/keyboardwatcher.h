#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

// Notices when a hardware keyboard (built in, USB or Bluetooth) is attached,
// so the on-screen one can stay out of the way, as on an iPad. A keyboard is
// an input device with letter keys; power buttons, headset jacks and the
// mouse half of a keyboard don't count. Virtual keyboards made through the
// compositor (floatkey's own, fcitx5's) never appear here at all.
class KeyboardWatcher : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool attached READ isAttached NOTIFY attachedChanged)

public:
    explicit KeyboardWatcher(const QString &sysInput = "/sys/class/input", const QString &devInput = "/dev/input",
                             QObject *parent = nullptr);

    bool isAttached() const { return m_attached; }
    QStringList names() const { return m_names; }

    // Whether a capabilities/key bitmap (hex words, most significant first)
    // has the letter keys and space.
    static bool hasLetterKeys(const QString &bitmap);

signals:
    void attachedChanged();

private:
    void rescan();

    QString m_sysInput;
    QFileSystemWatcher m_watcher;
    QTimer m_settle;
    bool m_attached = false;
    QStringList m_names;
};
