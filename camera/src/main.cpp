// Omarchy Pad Camera: take photos with the tablet's cameras, by touch.

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <cstdio>

#include "cameras.h"
#include "theme.h"
#include "viewfinder.h"

static const char *usage =
    "Usage: omarchy-pad-camera [--version]\n"
    "\n"
    "Takes photos with the tablet's cameras. Photos go to Camera in your\n"
    "Pictures folder. QT_LOGGING_RULES=camera.debug=true logs the stream,\n"
    "its frame rate and focusing.\n";

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "-h" || arg == "--help") {
            std::fputs(usage, stdout);
            return 0;
        }
        if (arg == "--version") {
            std::printf("omarchy-pad-camera %s (%s)\n", CAMERA_VERSION, CAMERA_COMMIT);
            return 0;
        }
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("omarchy-pad-camera");
    app.setApplicationDisplayName(QObject::tr("Camera"));
    app.setApplicationVersion(CAMERA_VERSION);
    app.setDesktopFileName("omarchy-pad-camera");

    Theme theme;
    // Made before the engine, so it outlives the viewfinder holding its frames.
    Cameras cameras;
    qmlRegisterType<Viewfinder>("Camera", 1, 0, "Viewfinder");
    qmlRegisterUncreatableType<Cameras>("Camera", 1, 0, "Cameras", "the app's own");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("cameras", &cameras);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &cameras,
                     [&cameras](Qt::ApplicationState state) { cameras.setActive(state == Qt::ApplicationActive); });
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &cameras, &Cameras::stop);
    cameras.start();
    return app.exec();
}
