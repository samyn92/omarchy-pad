#include "ownerform.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringDecoder>

#include <fcntl.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

OwnerForm::OwnerForm(const Paths &paths, QObject *parent)
    : QObject(parent), m_paths(paths)
{
    m_rules = readRules();

    QFile catalog(m_paths.catalog);
    if (catalog.open(QIODevice::ReadOnly))
        m_catalog = QJsonDocument::fromJson(catalog.readAll()).object().toVariantMap();

    connect(&m_server, &QLocalServer::newConnection, this, &OwnerForm::accept);

    m_returns.setInterval(1000);
    connect(&m_returns, &QTimer::timeout, this, &OwnerForm::sendReturn);

    m_statePoll.setInterval(500);
    connect(&m_statePoll, &QTimer::timeout, this, &OwnerForm::pollState);
    m_statePoll.start();
}

bool OwnerForm::listen()
{
    QLocalServer::removeServer(m_paths.socket);
    // The answers include the owner's password: only root (the owner setup) may connect.
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(m_paths.socket)) {
        qWarning("omarchy-pad-owner-form: cannot listen on %s: %s", qPrintable(m_paths.socket),
                 qPrintable(m_server.errorString()));
        return false;
    }
    ::chmod(QFile::encodeName(m_paths.socket).constData(), 0600);
    return true;
}

void OwnerForm::accept()
{
    while (QLocalSocket *socket = m_server.nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, socket, [this, socket] {
            if (!socket->canReadLine())
                return;
            handle(socket, socket->readLine().trimmed());
        });
    }
}

// One question per connection. A table is not a question: gum prints it and the confirm after it asks, so the window
// keeps it for that page and answers at once.
void OwnerForm::handle(QLocalSocket *socket, const QByteArray &line)
{
    const QVariantMap request = QJsonDocument::fromJson(line).object().toVariantMap();
    const QString cmd = request.value("cmd").toString();

    if (cmd == "table") {
        m_summary = request.value("rows").toList();
        emit summaryChanged();
        reply(socket, 0, QString());
        return;
    }

    // A stand-in that gave up waiting leaves its question behind; the new one replaces it.
    if (m_pending && m_pending != socket)
        m_pending->disconnectFromServer();

    m_returns.stop();
    if (!m_started) {
        m_started = true;
        QFile::remove(startPath());
        emit startedChanged();
    }

    // Omarchy asks the same question again after one of its messages (a taken user name, say); the message is on
    // tty1's screen, under the logo.
    const QString prompt = cmd + '\n' + request.value("header").toString() + '\n' + request.value("prompt").toString()
        + '\n' + request.value("password").toString();
    m_notice = prompt == m_lastPrompt ? screenNotice() : QString();
    m_lastPrompt = prompt;

    m_pending = socket;
    m_question = request;
    ++m_serial;
    emit questionChanged();
}

void OwnerForm::answer(int status, const QString &output)
{
    if (!m_pending)
        return;
    QLocalSocket *socket = m_pending;
    m_pending = nullptr;
    // A summary belongs to the confirm it came before.
    if (m_question.value("cmd") == "confirm" && !m_summary.isEmpty()) {
        m_summary.clear();
        emit summaryChanged();
    }
    reply(socket, status, output);
    emit questionChanged();
}

void OwnerForm::reply(QLocalSocket *socket, int status, const QString &output)
{
    const QJsonObject out{{"status", status}, {"output", output}};
    socket->write(QJsonDocument(out).toJson(QJsonDocument::Compact) + '\n');
    socket->flush();
    socket->disconnectFromServer();
}

// The window runs as an unprivileged user and cannot type into tty1 itself: it leaves a "start" file beside its socket,
// and the service's root helper (owner-form-helper) sends a Return each second while the file is there. The first
// question takes the file away. Running as root (or with a tty that takes TIOCSTI), it also sends them itself.
void OwnerForm::start()
{
    if (m_started)
        return;
    QFile startFile(startPath());
    if (!startFile.open(QIODevice::WriteOnly))
        qWarning("omarchy-pad-owner-form: cannot write %s", qPrintable(startPath()));
    sendReturn();
    m_returns.start();
}

QString OwnerForm::startPath() const
{
    return QFileInfo(m_paths.socket).absolutePath() + "/start";
}

void OwnerForm::sendReturn()
{
    if (!m_paths.tty.isEmpty() && m_paths.tty != "none")
        typeReturn(m_paths.tty, true);
}

// TIOCSTI puts the byte in the tty's input as if typed. It needs CAP_SYS_ADMIN for a tty that is not ours, which root
// has (also where legacy TIOCSTI is turned off for everyone else). The input is emptied first, so that the Returns sent
// each second while Omarchy's greeter is still settling leave one Return waiting, not one for every second, for
// whatever reads tty1 after the greeter.
bool OwnerForm::typeReturn(const QString &tty, bool quiet)
{
    const int fd = ::open(QFile::encodeName(tty).constData(), O_WRONLY | O_NOCTTY | O_CLOEXEC);
    if (fd < 0) {
        if (!quiet)
            qWarning("omarchy-pad-owner-form: cannot open %s", qPrintable(tty));
        return false;
    }
    ::tcflush(fd, TCIFLUSH);
    const char cr = '\n';
    const bool typed = ::ioctl(fd, TIOCSTI, &cr) == 0;
    if (!typed && !quiet)
        qWarning("omarchy-pad-owner-form: TIOCSTI on %s failed", qPrintable(tty));
    ::close(fd);
    return typed;
}

// The last "●" line on tty1's screen: Omarchy's notice() writes its messages so. /dev/vcsuN is the screen as UTF-32
// characters, row after row, with the size in /dev/vcsaN's header (readable by root only). Any other file is the
// screen as text: the root helper's copy, all rows run together, or the tests' lines; the message ends where a run of
// spaces begins.
QString OwnerForm::screenNotice() const
{
    QStringList lines;
    const QString &path = m_paths.vcs;
    if (path.startsWith("/dev/vcsu")) {
        QFile attrs(QString(path).replace("/dev/vcsu", "/dev/vcsa")), chars(path);
        if (!attrs.open(QIODevice::ReadOnly) || !chars.open(QIODevice::ReadOnly))
            return QString();
        const QByteArray header = attrs.read(4);
        if (header.size() < 4)
            return QString();
        const int cols = quint8(header.at(1));
        QStringDecoder utf32(QStringDecoder::Utf32LE);
        const QString screen = utf32(chars.readAll());
        for (int i = 0; cols > 0 && i + cols <= screen.size(); i += cols)
            lines << screen.mid(i, cols);
    } else {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return QString();
        lines = QString::fromUtf8(file.readAll()).split('\n');
    }
    for (qsizetype i = lines.size() - 1; i >= 0; --i) {
        const qsizetype dot = lines.at(i).indexOf(QChar(0x25CF));
        if (dot >= 0)
            return lines.at(i).mid(dot + 1).trimmed().section("   ", 0, 0);
    }
    return QString();
}

void OwnerForm::pollState()
{
    QString state;
    QFile file(m_paths.state);
    if (file.open(QIODevice::ReadOnly))
        state = QString::fromUtf8(file.readAll()).trimmed();
    if (state != m_setupState) {
        m_setupState = state;
        emit setupStateChanged();
    }
}

QString OwnerForm::xkbLayout() const
{
    QFile file(m_paths.vconsole);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    static const QRegularExpression line(R"(^XKBLAYOUT=["']?([^"'\s]*)["']?\s*$)", QRegularExpression::MultilineOption);
    const QRegularExpressionMatch match = line.match(QString::fromUtf8(file.readAll()));
    return match.hasMatch() ? match.captured(1).section(',', 0, 0) : QString();
}

bool OwnerForm::userExists(const QString &name) const
{
    return !name.isEmpty() && ::getpwnam(name.toLocal8Bit().constData()) != nullptr;
}

QString OwnerForm::tinted(const QString &path, const QString &color) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QByteArray svg = file.readAll();
    svg.replace("fill=\"#000\"", "fill=\"" + color.toUtf8() + "\"");
    svg.replace("fill=\"#000000\"", "fill=\"" + color.toUtf8() + "\"");
    return "data:image/svg+xml;base64," + QString::fromLatin1(svg.toBase64());
}

QString OwnerForm::text(const QString &key) const
{
    const QString value = m_catalog.value(key).toString();
    return value.isEmpty() ? key : value;
}

// Omarchy's rules for the user name and hostname, from the setup form it sources itself, so the window turns away
// what Omarchy would and lets through what it accepts. Its patterns are POSIX extended regular expressions that read
// the same in JavaScript.
QVariantMap OwnerForm::readRules() const
{
    QVariantMap rules{{"username", R"(^[a-z_][a-z0-9_-]*[$]?$)"},
                      {"reserved", "^(root)$"},
                      {"hostname", R"(^[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?$)"},
                      {"hostnameDefault", "omarchy"}};
    QFile file(m_paths.formScript);
    if (!file.open(QIODevice::ReadOnly))
        return rules;
    const QString script = QString::fromUtf8(file.readAll());
    const QList<std::pair<QString, QString>> names{{"username", "OMARCHY_USERNAME_PATTERN"},
                                                   {"reserved", "OMARCHY_RESERVED_USERNAMES"},
                                                   {"hostname", "OMARCHY_HOSTNAME_PATTERN"},
                                                   {"hostnameDefault", "OMARCHY_HOSTNAME_DEFAULT"}};
    for (const auto &[key, name] : names) {
        const QRegularExpression assignment("^" + name + "='([^']*)'", QRegularExpression::MultilineOption);
        const QRegularExpressionMatch match = assignment.match(script);
        if (match.hasMatch())
            rules.insert(key, match.captured(1));
    }
    return rules;
}
