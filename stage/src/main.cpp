// stage — iPad-style multitasking for Omarchy on a tablet: drag in from the
// right or bottom edge to make room beside a window and pick an app for it,
// or pull the scratchpad console down from the top.

#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QUrl>
#include <cstdio>

#include "apps.h"
#include "capture.h"
#include "hyprland.h"
#include "images.h"
#include "ipc.h"
#include "shelves.h"
#include "stage.h"
#include "theme.h"
#include "touchscreen.h"
#include "translator.h"

static const char *usage =
    "Usage: stage            run the edge gestures (one per Wayland display)\n"
    "       stage <command>\n"
    "\n"
    "Commands for the running one:\n"
    "  status                   whether the console is open, how many apps it knows\n"
    "  launch APP [right|bottom|console] [FRACTION]\n"
    "                           open APP (a .desktop name) beside the focused window,\n"
    "                           under it, or in the console, as the launcher would\n"
    "  console open|close       bring the scratchpad console down or send it up\n"
    "  resize [live|pictures]   how holding the line between windows resizes them:\n"
    "                           live (the default), or with pictures until the finger\n"
    "                           lets go (remembered); on its own, which it does now\n"
    "  simulate drag X0 Y0 X1 Y1 [MS]\n"
    "                           a finger landing at X0,Y0 and dragging to X1,Y1, still\n"
    "                           held; then simulate release, pick APP or cancel\n"
    "  simulate press X Y       a finger landing and staying (on a divider, it holds it);\n"
    "                           then simulate move X Y [MS] and simulate release\n"
    "  simulate pinch in|out [MS]\n"
    "                           three fingers drawing together (copy) or spreading\n"
    "                           (paste) in the middle of the screen, then lifting\n"
    "  simulate pull X0 Y0 X1 Y1 [MS] [FINGERS]\n"
    "                           two (or three) fingers landing at X0,Y0 and moving to\n"
    "                           X1,Y1, as if from the touch screen, still held; then\n"
    "                           simulate release\n"
    "  hush                     don't announce the next copy for a moment (floatkey\n"
    "                           sends it before putting a clip back to paste it)\n"
    "  quit                     stop\n";

static int runCommand(int argc, char *argv[], const QStringList &args)
{
    QCoreApplication app(argc, argv);
    QString reply;
    if (!Ipc::send(Ipc::socketPath(), args.join(' '), &reply, 3000)) {
        std::fputs("stage is not running\n", stderr);
        return 1;
    }
    if (reply.startsWith("error")) {
        std::fprintf(stderr, "stage: %s\n", qPrintable(reply.mid(6)));
        return 1;
    }
    if (args.first() == "status" || args.first() == "simulate" || args.first() == "resize")
        std::printf("%s\n", qPrintable(reply.mid(3)));
    return 0;
}

int main(int argc, char *argv[])
{
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);
    if (args.contains("-h") || args.contains("--help")) {
        std::fputs(usage, stdout);
        return 0;
    }
    if (args.contains("--version")) {
        std::printf("stage %s (%s)\n", STAGE_VERSION, STAGE_COMMIT);
        return 0;
    }
    if (!args.isEmpty())
        return runCommand(argc, argv, args);

    QQuickWindow::setDefaultAlphaBuffer(true);
    QGuiApplication app(argc, argv);
    app.setApplicationName("stage");
    app.setApplicationVersion(STAGE_VERSION);
    app.setDesktopFileName("stage");
    if (QGuiApplication::platformName() != "wayland") {
        std::fputs("stage needs a Wayland session\n", stderr);
        return 1;
    }
    const QString socket = Ipc::socketPath();
    if (Ipc::send(socket, "status", nullptr, 300))
        return 0;

    Translator translator;
    app.installTranslator(&translator);
    Theme theme;
    Hyprland hyprland;
    if (!hyprland.isAvailable()) {
        std::fputs("stage needs Hyprland\n", stderr);
        return 1;
    }
    Capture capture;
    if (!capture.connectToCompositor())
        qWarning("stage: the compositor can't capture the screen; gestures will show placeholders");
    Apps apps;
    // Qt without a platform theme only knows hicolor; the launcher wants the
    // icons the rest of the desktop shows.
    apps.followIconTheme();
    // The launcher's rows: the user's, else made up with the apps Omarchy's
    // dock pins on top.
    QStringList pinned;
    QFile shellConfig(QDir::homePath() + "/.config/omarchy/shell.json");
    if (shellConfig.open(QIODevice::ReadOnly))
        for (const QJsonValue &id : QJsonDocument::fromJson(shellConfig.readAll())["dock"]["pinned"].toArray())
            pinned << id.toString();
    Shelves shelves(&apps, QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/stage/launcher.json",
                    pinned);
    auto *snapshots = new Snapshots;
    Stage stage(&hyprland, &capture, snapshots, &apps);
    Touchscreen touchscreen;
    QObject::connect(&touchscreen, &Touchscreen::touched, &stage, &Stage::touch);
    // A stage in a nested test session must not take the real screen's fingers.
    if (qEnvironmentVariableIsSet("STAGE_NO_TOUCHSCREEN"))
        qInfo("stage: not reading the touch screen (STAGE_NO_TOUCHSCREEN)");
    else if (!touchscreen.open())
        qWarning("stage: no touch screen to read; two- and three-finger gestures are off");

    QQmlApplicationEngine engine;
    engine.addImageProvider("icon", new IconProvider);
    engine.addImageProvider("snapshot", snapshots);
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("stage", &stage);
    engine.rootContext()->setContextProperty("apps", &apps);
    engine.rootContext()->setContextProperty("shelves", &shelves);
    engine.load(QUrl("qrc:/Main.qml"));
    auto *window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window)
        return 1;
    stage.attach(window);
    window->show();
    stage.watchClipboard();

    QString error;
    if (!stage.listen(socket, &error))
        qWarning("stage: no command socket at %s: %s", qPrintable(socket), qPrintable(error));
    return app.exec();
}
