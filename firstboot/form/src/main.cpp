// omarchy-pad-owner-form: the first-boot owner setup by touch. A full-screen window (in Hyprland, on tty2) that answers
// Omarchy's own owner setup, which asks its questions on tty1 through the stand-in gum (owner-path/gum). See
// ../../README.md.

#include "ownerform.h"

#include <QCommandLineParser>
#include <QFile>
#include <QGuiApplication>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include <systemd/sd-daemon.h>

// omarchy-pad-owner-form ask SOCKET: the stand-in gum's side. Sends the question on stdin (one JSON line) to the
// window and prints its answer, however long the owner takes; fails if the window is not there or goes away.
static int ask(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc < 3)
        return 2;
    QFile in;
    if (!in.open(stdin, QIODevice::ReadOnly))
        return 2;
    const QByteArray request = in.readLine().trimmed() + '\n';

    QLocalSocket socket;
    socket.connectToServer(QString::fromLocal8Bit(argv[2]));
    if (!socket.waitForConnected(2000))
        return 1;
    socket.write(request);
    socket.flush();
    while (!socket.canReadLine()) {
        if (!socket.waitForReadyRead(-1))
            return 1;
    }
    std::fputs(socket.readLine().constData(), stdout);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc > 1 && qstrcmp(argv[1], "ask") == 0)
        return ask(argc, argv);
    // omarchy-pad-owner-form return TTY: the root helper's Return into Omarchy's greeter.
    if (argc > 2 && qstrcmp(argv[1], "return") == 0)
        return OwnerForm::typeReturn(QString::fromLocal8Bit(argv[2])) ? 0 : 1;

    QGuiApplication app(argc, argv);
    app.setApplicationName("omarchy-pad-owner-form");
    app.setApplicationVersion("0.4.2");

    QCommandLineParser parser;
    parser.setApplicationDescription("Omarchy Pad: the first-boot owner setup by touch");
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption socket("socket", "The stand-in gum's socket.", "path", "/run/omarchy-pad/owner-form.sock");
    const QCommandLineOption tty("tty", "The console Omarchy's setup runs on, or none (the root helper types into it).",
                                 "path", "none");
    const QCommandLineOption vcs("vcs", "That console's screen (the root helper's copy).", "path",
                                 "/run/omarchy-pad/screen.txt");
    const QCommandLineOption state("state", "Omarchy's setup state file.", "path", "/run/omarchy-provision-owner.state");
    const QCommandLineOption vconsole("vconsole", "Where the keyboard step writes the layout.", "path",
                                      "/etc/vconsole.conf");
    const QCommandLineOption omarchy("omarchy", "Omarchy's files.", "path", "/usr/share/omarchy");
    const QCommandLineOption windowed("windowed", "A window instead of the full screen (tests/try).");
    const QCommandLineOption autotest("autotest", "Answer every page with test values and save a picture of each "
                                                  "in this folder (tests/check).", "dir");
    parser.addOptions({socket, tty, vcs, state, vconsole, omarchy, windowed, autotest});
    parser.process(app);

    const QString omarchyPath = parser.value(omarchy);
    const OwnerForm::Paths paths{parser.value(socket),
                                 parser.value(tty),
                                 parser.value(vcs),
                                 parser.value(state),
                                 parser.value(vconsole),
                                 omarchyPath + "/install/provisioning/setup-form.sh",
                                 omarchyPath + "/default/i18n/en/cli.json"};
    OwnerForm form(paths);
    if (!form.listen())
        return 1;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("form", &form);
    engine.rootContext()->setContextProperty("omarchyPath", omarchyPath);
    engine.rootContext()->setContextProperty("windowed", parser.isSet(windowed) || parser.isSet(autotest));
    engine.rootContext()->setContextProperty("autotest", parser.value(autotest));
    engine.load(QUrl("qrc:/form/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    // Ready for the owner setup to start asking: the socket is up and the window is on screen.
    sd_notify(0, "READY=1");
    return app.exec();
}
