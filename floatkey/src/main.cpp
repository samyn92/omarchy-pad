// floatkey — an iPad-style on-screen keyboard for Omarchy: docked along the
// bottom, or floating anywhere on the screen.

#include <QCoreApplication>
#include <QGuiApplication>
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

#include "controller.h"
#include "dictation.h"
#include "ipc.h"
#include "keyboardwatcher.h"
#include "theme.h"
#include "translator.h"
#include "virtualkeyboard.h"

static const char *usage =
    "Usage: floatkey [--hidden] [--mode docked|floating|orb]\n"
    "       floatkey <command>\n"
    "\n"
    "Starts the on-screen keyboard, or drives the running one:\n"
    "  show, hide, toggle   show or hide the keyboard (show and toggle start it)\n"
    "  dock                 dock it along the bottom of the screen\n"
    "  float                float it, small, wherever it was last dragged\n"
    "  orb                  only the voice orb, floating, for voice typing\n"
    "  status               print whether it is shown, its mode and layout\n"
    "  layouts              print its layouts and what their keys type, as JSON\n"
    "  type TEXT            type TEXT into the focused window\n"
    "  key KEY              press a key, with modifiers: BackSpace, Return,\n"
    "                       ctrl+c, super+space, shift+Left...\n"
    "  focus TYPE, blur     a text field took or lost focus (sent by the fcitx5\n"
    "                       addon; TYPE is text, terminal, url, email, number\n"
    "                       or password)\n"
    "  quit                 stop the keyboard\n";

static const QHash<QString, QString> startModes = {
    {"show", {}}, {"toggle", {}}, {"dock", "docked"}, {"float", "floating"}, {"orb", "orb"},
};

static int runCommand(int argc, char *argv[], const QStringList &args)
{
    QCoreApplication app(argc, argv);
    const QString verb = args.first();
    QString line = verb;
    if (verb == "type" || verb == "key") {
        if (args.size() < 2) {
            std::fprintf(stderr, "floatkey %s needs an argument\n", qPrintable(verb));
            return 1;
        }
        line += ' ' + Ipc::encode(args.mid(1).join(' '));
    } else if (verb == "focus" && args.size() > 1) {
        line += ' ' + Ipc::encode(args.at(1));
    }

    QString reply;
    if (!Ipc::send(Ipc::socketPath(), line, &reply)) {
        // Showing a keyboard that isn't running starts it.
        if (startModes.contains(verb)) {
            QStringList startArgs;
            if (!startModes.value(verb).isEmpty())
                startArgs << "--mode" << startModes.value(verb);
            if (QProcess::startDetached(QCoreApplication::applicationFilePath(), startArgs))
                return 0;
            std::fputs("floatkey: could not start the keyboard\n", stderr);
            return 1;
        }
        if (verb == "hide" || verb == "quit")
            return 0;
        std::fputs("floatkey is not running\n", stderr);
        return 1;
    }
    if (reply.startsWith("error")) {
        std::fprintf(stderr, "floatkey: %s\n", qPrintable(reply.mid(6)));
        return 1;
    }
    if (verb == "status")
        std::printf("%s\n", qPrintable(reply.mid(3)));
    return 0;
}

// One of Hyprland's input options, as a comma-separated list.
static QStringList hyprlandOption(const QString &name)
{
    if (qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"))
        return {};
    const QString hyprctl = QStandardPaths::findExecutable("hyprctl");
    if (hyprctl.isEmpty())
        return {};
    QProcess process;
    process.start(hyprctl, {"-j", "getoption", name});
    if (!process.waitForFinished(1000)) {
        process.kill();
        return {};
    }
    const QString value = QJsonDocument::fromJson(process.readAllStandardOutput()).object().value("str").toString();
    // What Hyprland says for an option nobody set.
    if (value == "[[EMPTY]]")
        return {};
    return value.split(',');
}

// Hyprland's input:kb_layout, so the keyboard starts in the user's layout.
static QStringList hyprlandLayouts()
{
    QStringList layouts = hyprlandOption("input:kb_layout");
    layouts.removeAll(QString());
    return layouts;
}

// The same with each one's input:kb_variant, as "de(nodeadkeys)", so the keys
// show what the user's own keyboard types.
static QStringList hyprlandLayoutVariants()
{
    const QStringList layouts = hyprlandOption("input:kb_layout"), variants = hyprlandOption("input:kb_variant");
    QStringList named;
    for (qsizetype i = 0; i < layouts.size(); ++i) {
        const QString layout = layouts.at(i).trimmed(), variant = variants.value(i).trimmed();
        if (!layout.isEmpty())
            named << (variant.isEmpty() ? layout : QString("%1(%2)").arg(layout, variant));
    }
    return named;
}

// The layouts the keyboard shows and what their keys type, as JSON, for the
// lock screen's keyboard to draw the same keys.
static int printLayouts(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const Controller controller(Controller::defaultSettingsPath(), hyprlandLayoutVariants());
    QJsonArray layouts;
    for (const QString &code : controller.layouts()) {
        QJsonObject layout = QJsonObject::fromVariantMap(controller.layoutKeys().value(code).toMap());
        layout.insert("code", code);
        layouts.append(layout);
    }
    const QJsonObject out{{"index", controller.layoutIndex()}, {"layouts", layouts}};
    std::printf("%s\n", QJsonDocument(out).toJson(QJsonDocument::Compact).constData());
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
        std::puts("floatkey 0.5.2");
        return 0;
    }
    if (args.value(0) == "layouts")
        return printLayouts(argc, argv);
    if (!args.isEmpty() && (startModes.contains(args.first()) || QStringList{"hide", "status", "type", "key", "focus", "blur", "quit"}.contains(args.first())))
        return runCommand(argc, argv, args);

    bool hidden = false;
    QString mode;
    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i) == "--hidden")
            hidden = true;
        else if (args.at(i) == "--mode" && i + 1 < args.size())
            mode = args.at(++i);
        else {
            std::fprintf(stderr, "floatkey: unknown argument %s\n\n%s", qPrintable(args.at(i)), usage);
            return 2;
        }
    }
    if (!mode.isEmpty() && !Controller::modes().contains(mode)) {
        std::fprintf(stderr, "floatkey: unknown mode %s\n", qPrintable(mode));
        return 2;
    }

    // The overlay is see-through everywhere but the keys.
    QQuickWindow::setDefaultAlphaBuffer(true);
    QGuiApplication app(argc, argv);
    app.setApplicationName("floatkey");
    app.setApplicationVersion("0.5.2");
    app.setDesktopFileName("floatkey");
    if (QGuiApplication::platformName() != "wayland") {
        std::fputs("floatkey needs a Wayland session\n", stderr);
        return 1;
    }

    // One keyboard per Wayland display: starting another shows the first.
    const QString socket = Ipc::socketPath();
    if (Ipc::send(socket, "status", nullptr, 300)) {
        static const QHash<QString, QString> placements = {
            {"docked", "dock"}, {"floating", "float"}, {"orb", "orb"},
        };
        if (!mode.isEmpty())
            Ipc::send(socket, placements.value(mode), nullptr);
        else if (!hidden)
            Ipc::send(socket, "show", nullptr);
        return 0;
    }

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    const QStringList systemLayouts = hyprlandLayouts();
    VirtualKeyboard keyboard(systemLayouts);
    keyboard.connectToCompositor();
    Controller controller(Controller::defaultSettingsPath(), hyprlandLayoutVariants());
    if (!mode.isEmpty())
        controller.setMode(mode);
    KeyboardWatcher keyboards;
    controller.setHardwareKeyboard(keyboards.isAttached());
    QObject::connect(&keyboards, &KeyboardWatcher::attachedChanged, &controller,
                     [&] { controller.setHardwareKeyboard(keyboards.isAttached()); });

    Dictation dictation;

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("controller", &controller);
    engine.rootContext()->setContextProperty("virtualKeyboard", &keyboard);
    engine.rootContext()->setContextProperty("dictation", &dictation);
    engine.load(QUrl("qrc:/Main.qml"));
    auto *window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window)
        return 1;
    controller.attach(window, &keyboard);

    QString error;
    if (!controller.listen(socket, &error))
        qWarning("floatkey: no command socket at %s: %s", qPrintable(socket), qPrintable(error));
    if (!hidden)
        controller.show();

    return app.exec();
}
