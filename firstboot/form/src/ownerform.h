#pragma once

#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

// The window's side of the stand-in gum (owner-path/gum): Omarchy's owner setup asks each question through gum, the
// stand-in sends it here over a local socket as one JSON line, and waits for one line back with the status and what
// gum would have printed. The window shows the question in `question` and answers it with answer().
//
// Also what the window needs from the machine: the form's rules and messages from Omarchy's own files, the layout
// the owner picked, the setup's progress, and a Return into tty1 for Omarchy's "Press Return" greeter.
class OwnerForm : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap question READ question NOTIFY questionChanged)
    Q_PROPERTY(bool asked READ asked NOTIFY questionChanged)
    // Counts the questions, so the window can tell a new one from the same one read again.
    Q_PROPERTY(int serial READ serial NOTIFY questionChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY questionChanged)
    Q_PROPERTY(QVariantList summary READ summary NOTIFY summaryChanged)
    Q_PROPERTY(QString setupState READ setupState NOTIFY setupStateChanged)
    Q_PROPERTY(bool started READ started NOTIFY startedChanged)
    Q_PROPERTY(QVariantMap rules READ rules CONSTANT)

public:
    struct Paths {
        QString socket;
        QString tty;
        QString vcs;
        QString state;
        QString vconsole;
        QString formScript;
        QString catalog;
    };

    explicit OwnerForm(const Paths &paths, QObject *parent = nullptr);

    bool listen();

    QVariantMap question() const { return m_question; }
    bool asked() const { return !m_pending.isNull(); }
    int serial() const { return m_serial; }
    QString notice() const { return m_notice; }
    QVariantList summary() const { return m_summary; }
    QString setupState() const { return m_setupState; }
    bool started() const { return m_started; }
    QVariantMap rules() const { return m_rules; }

    // Answers the question on screen: gum's exit status (0, 1 for Esc, 130 for Ctrl+C) and what it prints.
    Q_INVOKABLE void answer(int status, const QString &output = QString());
    // Passes Omarchy's greeter, which waits for Return on tty1: sends one now and again each second until the first
    // question comes (a Return sent before the setup reads tty1 would be lost).
    Q_INVOKABLE void start();
    // The XKB layout the keyboard step left in /etc/vconsole.conf, for the window's keys.
    Q_INVOKABLE QString xkbLayout() const;
    Q_INVOKABLE bool userExists(const QString &name) const;
    // An SVG drawn in black (Omarchy's logo) in another colour, as a data URL for an Image.
    Q_INVOKABLE QString tinted(const QString &path, const QString &color) const;
    // A Return into a tty (Omarchy's greeter on tty1); what `omarchy-pad-owner-form return TTY` does for the helper.
    static bool typeReturn(const QString &tty, bool quiet = false);
    // Omarchy's form text for a key of its catalog (setupForm.usernameTaken...), English if it has none.
    Q_INVOKABLE QString text(const QString &key) const;

signals:
    void questionChanged();
    void summaryChanged();
    void setupStateChanged();
    void startedChanged();

private:
    void accept();
    void handle(QLocalSocket *socket, const QByteArray &line);
    void reply(QLocalSocket *socket, int status, const QString &output);
    QString screenNotice() const;
    void pollState();
    void sendReturn();
    QString startPath() const;
    QVariantMap readRules() const;

    Paths m_paths;
    QLocalServer m_server;
    QPointer<QLocalSocket> m_pending;
    QVariantMap m_question;
    int m_serial = 0;
    QString m_lastPrompt;
    QString m_notice;
    QVariantList m_summary;
    QString m_setupState;
    bool m_started = false;
    QTimer m_returns;
    QTimer m_statePoll;
    QVariantMap m_rules;
    QVariantMap m_catalog;
};
