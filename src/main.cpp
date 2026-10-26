// stage — iPad-style multitasking for Omarchy on a tablet: drag in from the
// right or bottom edge to make room beside a window and pick an app for it,
// or pull the scratchpad console down from the top.

#include <QGuiApplication>
#include <QIcon>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QUrl>
#include <cstdio>

#include "apps.h"
#include "capture.h"
#include "hyprland.h"
#include "images.h"
#include "ipc.h"
#include "stage.h"
#include "theme.h"
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
    "  simulate drag X0 Y0 X1 Y1 [MS]\n"
    "                           a finger landing at X0,Y0 and dragging to X1,Y1, still\n"
    "                           held; then simulate release, pick APP or cancel\n"
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
    if (args.first() == "status" || args.first() == "simulate")
        std::printf("%s\n", qPrintable(reply.mid(3)));
    return 0;
}

// Qt without a platform theme only knows hicolor; the launcher wants the
// icons the rest of the desktop shows.
static void useDesktopIconTheme()
{
    if (!QIcon::themeName().isEmpty() && QIcon::themeName() != "hicolor")
        return;
    QProcess gsettings;
    gsettings.start("gsettings", {"get", "org.gnome.desktop.interface", "icon-theme"});
    if (gsettings.waitForFinished(1000)) {
        const QString name = QString::fromUtf8(gsettings.readAllStandardOutput()).trimmed().remove('\'');
        if (!name.isEmpty())
            QIcon::setThemeName(name);
    }
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
        std::puts("stage 0.1.1");
        return 0;
    }
    if (!args.isEmpty())
        return runCommand(argc, argv, args);

    QQuickWindow::setDefaultAlphaBuffer(true);
    QGuiApplication app(argc, argv);
    app.setApplicationName("stage");
    app.setApplicationVersion("0.1.1");
    app.setDesktopFileName("stage");
    if (QGuiApplication::platformName() != "wayland") {
        std::fputs("stage needs a Wayland session\n", stderr);
        return 1;
    }
    const QString socket = Ipc::socketPath();
    if (Ipc::send(socket, "status", nullptr, 300))
        return 0;

    useDesktopIconTheme();
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
    auto *snapshots = new Snapshots;
    Stage stage(&hyprland, &capture, snapshots, &apps);

    QQmlApplicationEngine engine;
    engine.addImageProvider("icon", new IconProvider);
    engine.addImageProvider("snapshot", snapshots);
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("stage", &stage);
    engine.rootContext()->setContextProperty("apps", &apps);
    engine.load(QUrl("qrc:/Main.qml"));
    auto *window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window)
        return 1;
    stage.attach(window);
    window->show();

    QString error;
    if (!stage.listen(socket, &error))
        qWarning("stage: no command socket at %s: %s", qPrintable(socket), qPrintable(error));
    return app.exec();
}
