#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointingDevice>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>
#include <linux/input-event-codes.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

#include "controller.h"
#include "dictation.h"
#include "fieldtype.h"
#include "keyboardwatcher.h"
#include "keymap.h"
#include "theme.h"
#include "translator.h"
#include "xkblayouts.h"

// Stands in for the Wayland virtual keyboard and writes down what the QML
// asked it to type.
class FakeKeyboard : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready CONSTANT)
    Q_PROPERTY(QString error READ error CONSTANT)

public:
    bool ready() const { return true; }
    QString error() const { return {}; }
    Q_INVOKABLE void typeText(const QString &text, int modifiers = 0) { log << QString("type:%1:%2").arg(text).arg(modifiers); }
    Q_INVOKABLE bool tapKey(const QString &name, int modifiers = 0)
    {
        log << QString("key:%1:%2").arg(name).arg(modifiers);
        return true;
    }
    Q_INVOKABLE void preload(const QString &characters) { preloaded = characters; }

    QStringList log;
    QString preloaded;
};

// Stands in for `superwhisper`: writes down each command it was given.
static QString fakeSuperwhisper(const QString &dir)
{
    const QString path = dir + "/superwhisper";
    QFile script(path);
    if (!script.open(QIODevice::WriteOnly))
        return {};
    script.write("#!/bin/sh\necho \"$1\" >> \"$(dirname \"$0\")/voice.log\"\n");
    script.close();
    script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return path;
}

static QStringList voiceLog(const QString &dir)
{
    QFile log(dir + "/voice.log");
    if (!log.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(log.readAll()).split('\n', Qt::SkipEmptyParts);
}

// Stands in for Superwhisper's control socket, answering every status
// request with `status`.
class FakeDaemon : public QObject {
public:
    explicit FakeDaemon(const QString &path)
    {
        QLocalServer::removeServer(path);
        server.listen(path);
        connect(&server, &QLocalServer::newConnection, this, [this] {
            while (QLocalSocket *client = server.nextPendingConnection()) {
                connect(client, &QLocalSocket::readyRead, client, [this, client] {
                    if (!client->readAll().contains("status"))
                        return;
                    client->write(QJsonDocument(status).toJson(QJsonDocument::Compact) + "\n");
                    client->flush();
                });
                connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
            }
        });
    }
    QLocalServer server;
    QJsonObject status{{"phase", "idle"}};
};

// The real Main.qml, offscreen, with a fake keyboard and a temporary home.
class Harness {
public:
    explicit Harness(const QString &mode = "docked", const QStringList &layouts = {"us"})
        : translator(dir.path() + "/language", dir.path()), theme(dir.path()),
          controller(dir.path() + "/floatkey.conf", layouts),
          dictation(dir.path() + "/control.sock", fakeSuperwhisper(dir.path()))
    {
        dictation.setMetering(false);
        controller.setMode(mode);
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("controller", &controller);
        engine.rootContext()->setContextProperty("virtualKeyboard", &keyboard);
        engine.rootContext()->setContextProperty("dictation", &dictation);
        engine.load(QUrl("qrc:/Main.qml"));
        window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        if (!window)
            return;
        window->resize(1696, 1174);
        controller.show();
        if (!QTest::qWaitForWindowExposed(window))
            qWarning("the keyboard window never showed");
        // Let the keyboard finish sliding in.
        QTest::qWait(350);
    }

    QObject *keys() const { return window->findChild<QObject *>("keys"); }

    QVariant evaluate(QObject *scope, const QString &script) const
    {
        QQmlExpression expression(qmlContext(scope), scope, script);
        return expression.evaluate();
    }

    // The centre of the first key matching a JS condition on `k`, in window
    // coordinates.
    QPoint key(const QString &condition) const
    {
        const QString script = QString("(function() { const k = model.keys.find(k => %1); const r = keyRect(k);"
                                       " return mapToItem(null, r.x + r.width / 2, r.y + r.height / 2) })()")
                                   .arg(condition);
        return evaluate(keys(), script).toPointF().toPoint();
    }
    QPoint character(const QString &c) const { return key(QString("k.c === \"%1\"").arg(c)); }
    QPoint special(const QString &kind) const { return key(QString("k.kind === \"%1\"").arg(kind)); }

    void press(int id, QPoint at) { QTest::touchEvent(window, device()).press(id, at, window); }
    void move(int id, QPoint to) { QTest::touchEvent(window, device()).move(id, to, window); }
    void release(int id, QPoint at) { QTest::touchEvent(window, device()).release(id, at, window); }
    void tap(QPoint at)
    {
        press(0, at);
        release(0, at);
    }
    // Moves a touch in small steps, as a finger would.
    void slide(int id, QPoint from, QPoint to, int steps = 10)
    {
        for (int i = 1; i <= steps; ++i) {
            move(id, from + (to - from) * i / steps);
            QTest::qWait(10);
        }
    }

    static QPointingDevice *device()
    {
        static QPointingDevice *touch = QTest::createTouchDevice();
        return touch;
    }

    QTemporaryDir dir;
    Translator translator;
    Theme theme;
    FakeKeyboard keyboard;
    Controller controller;
    Dictation dictation;
    QQmlApplicationEngine engine;
    QQuickWindow *window = nullptr;
};

class FloatkeyTests : public QObject {
    Q_OBJECT

private slots:
    // ---- keymap ----

    void keymapTypesTheBaseLayoutDirectly()
    {
        Keymap keymap({"us"});
        QVERIFY(keymap.isValid());
        Keymap::Stroke stroke;
        QVERIFY(!keymap.reserve({'a', 'A', '@'}));
        QVERIFY(keymap.stroke('a', &stroke));
        QCOMPARE(stroke.key, uint32_t(KEY_A));
        QVERIFY(!stroke.shift);
        QVERIFY(keymap.stroke('A', &stroke));
        QCOMPARE(stroke.key, uint32_t(KEY_A));
        QVERIFY(stroke.shift);
        QVERIFY(keymap.stroke('@', &stroke));
        QCOMPARE(stroke.key, uint32_t(KEY_2));
    }

    void keymapFollowsTheSystemLayout()
    {
        // On QWERTZ, z lives where US has y, so shortcuts agree with Hyprland.
        Keymap keymap({"de"});
        Keymap::Stroke stroke;
        QVERIFY(keymap.stroke('z', &stroke));
        QCOMPARE(stroke.key, uint32_t(KEY_Y));
        QVERIFY(keymap.stroke(0xE4, &stroke));   // ä
        QCOMPARE(stroke.key, uint32_t(KEY_APOSTROPHE));
    }

    void keymapCarriesAnyCharacter()
    {
        Keymap keymap({"us"});
        const QVector<uint> characters{0xE4, 0xC4, 0x20AC, 0x1F600};   // ä Ä € 😀
        QVERIFY(keymap.reserve(characters));
        QVERIFY(!keymap.reserve(characters));

        xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        xkb_keymap *compiled = xkb_keymap_new_from_string(context, keymap.text().constData(),
                                                          XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
        QVERIFY(compiled);
        for (uint codepoint : characters) {
            Keymap::Stroke stroke;
            QVERIFY(keymap.stroke(codepoint, &stroke));
            xkb_state *state = xkb_state_new(compiled);
            if (stroke.shift)
                xkb_state_update_mask(state, keymap.shiftMask(), 0, 0, 0, 0, 0);
            QCOMPARE(xkb_state_key_get_utf32(state, stroke.key + 8), codepoint);
            xkb_state_unref(state);
        }
        xkb_keymap_unref(compiled);
        xkb_context_unref(context);
    }

    void keymapStaysClearOfTheSystemsKeys()
    {
        // A character on, say, the calculator key would open the calculator.
        Keymap keymap({"de"});
        QVector<uint> characters;
        for (uint codepoint = 0xC0; codepoint < 0xC0 + 60; ++codepoint)
            characters << codepoint;
        keymap.reserve(characters);

        xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        xkb_rule_names names{"evdev", "pc105", "de,us", nullptr, nullptr};
        xkb_keymap *system = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
        QVERIFY(system);
        int low = 0;
        for (uint codepoint : characters) {
            Keymap::Stroke stroke;
            QVERIFY(keymap.stroke(codepoint, &stroke));
            const xkb_keycode_t code = stroke.key + 8;
            if (code > xkb_keymap_max_keycode(system))
                continue;
            // Either the character's own key in the German layout (ä, ö, ü),
            // or a key that means nothing to the system at all.
            xkb_state *state = xkb_state_new(system);
            if (stroke.shift)
                xkb_state_update_mask(state, 1, 0, 0, 0, 0, 0);
            const bool ownKey = xkb_state_key_get_utf32(state, code) == codepoint;
            xkb_state_unref(state);
            if (ownKey)
                continue;
            if (code <= 255)
                ++low;
            for (xkb_layout_index_t group = 0; group < xkb_keymap_num_layouts_for_key(system, code); ++group) {
                for (xkb_level_index_t level = 0; level < xkb_keymap_num_levels_for_key(system, code, group); ++level) {
                    const xkb_keysym_t *syms = nullptr;
                    QCOMPARE(xkb_keymap_key_get_syms_by_level(system, code, group, level, &syms), 0);
                }
            }
        }
        // The first characters get keycodes that X11 apps can receive too.
        QVERIFY(low > 0);
        xkb_keymap_unref(system);
        xkb_context_unref(context);
    }

    void keymapReusesTheOldestSlot()
    {
        Keymap keymap({"us"});
        const uint first = 0x1F600;
        QVERIFY(keymap.reserve({first}));
        for (int i = 1; i <= keymap.capacity(); ++i)
            keymap.reserve({first + uint(i)});
        Keymap::Stroke stroke;
        QVERIFY(!keymap.stroke(first, &stroke));
        QVERIFY(keymap.stroke(first + uint(keymap.capacity()), &stroke));
    }

    // ---- controller ----

    void controllerRunsCommands()
    {
        QTemporaryDir dir;
        Controller controller(dir.path() + "/floatkey.conf", {"us"});
        QCOMPARE(controller.command("float"), QString("ok"));
        QCOMPARE(controller.mode(), QString("floating"));
        QVERIFY(controller.isVisible());
        QCOMPARE(controller.command("status"), QString("ok shown floating us"));
        QCOMPARE(controller.command("toggle"), QString("ok"));
        QVERIFY(!controller.isVisible());
        QCOMPARE(controller.command("orb"), QString("ok"));
        QCOMPARE(controller.mode(), QString("orb"));
        QVERIFY(controller.command("left").startsWith("error"));
        QVERIFY(controller.command("sing").startsWith("error"));
        QVERIFY(controller.command("type hello").startsWith("error"));   // no keyboard attached
    }

    void controllerRemembersPlacement()
    {
        QTemporaryDir dir;
        {
            Controller controller(dir.path() + "/floatkey.conf");
            controller.setMode("orb");
            controller.setFloatingPosition(QPointF(0.25, 0.4));
        }
        Controller controller(dir.path() + "/floatkey.conf");
        QCOMPARE(controller.mode(), QString("orb"));
        QCOMPARE(controller.floatingPosition(), QPointF(0.25, 0.4));

        // A keyboard left in a corner, which are gone, floats.
        QFile old(dir.path() + "/old.conf");
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write("mode=right\n");
        old.close();
        QCOMPARE(Controller(dir.path() + "/old.conf").mode(), QString("floating"));
    }

    void controllerPicksLayouts()
    {
        QTemporaryDir dir;
        QCOMPARE(Controller(dir.path() + "/a.conf", {"de", "fr", "zz"}).layouts(), (QStringList{"de", "fr"}));
        QCOMPARE(Controller(dir.path() + "/b.conf", {"zz"}).layouts(), QStringList{"us"});
        QCOMPARE(Controller(dir.path() + "/d.conf", {"us(dvorak)"}).layouts(), QStringList{"us(dvorak)"});
        QFile settings(dir.path() + "/c.conf");
        QVERIFY(settings.open(QIODevice::WriteOnly));
        settings.write("layouts=us, de\n");
        settings.close();
        QCOMPARE(Controller(dir.path() + "/c.conf", {"fr"}).layouts(), (QStringList{"us", "de"}));
    }

    // ---- showing up for text fields ----

    void focusShowsAndBlurHides()
    {
        QTemporaryDir dir;
        Controller controller(dir.path() + "/floatkey.conf");
        QCOMPARE(controller.command("focus url"), QString("ok"));
        QVERIFY(controller.isVisible());
        QCOMPARE(controller.fieldType(), QString("url"));
        controller.command("blur");
        QVERIFY(controller.isVisible());   // not at once: the next field may be on its way
        QTRY_VERIFY(!controller.isVisible());
        QCOMPARE(controller.fieldType(), QString());
    }

    void movingBetweenFieldsDoesntFlicker()
    {
        QTemporaryDir dir;
        Controller controller(dir.path() + "/floatkey.conf");
        QSignalSpy changes(&controller, &Controller::visibleChanged);
        controller.command("focus text");
        controller.command("blur");
        QTest::qWait(50);
        controller.command("focus text");
        QTest::qWait(400);
        QVERIFY(controller.isVisible());
        QCOMPARE(changes.count(), 1);
    }

    void keyboardOpenedByHandStays()
    {
        QTemporaryDir dir;
        Controller controller(dir.path() + "/floatkey.conf");
        controller.show();
        controller.command("focus text");
        controller.command("blur");
        QTest::qWait(400);
        QVERIFY(controller.isVisible());
    }

    void keyboardOpenedByHandInAFieldGoesWithIt()
    {
        QTemporaryDir dir;
        Controller controller(dir.path() + "/floatkey.conf");
        controller.command("focus text");
        controller.hide();
        controller.show();
        controller.command("blur");
        QTRY_VERIFY(!controller.isVisible());
    }

    void swipedAwayStaysAwayUntilTheNextField()
    {
        QTemporaryDir dir;
        Controller controller(dir.path() + "/floatkey.conf");
        controller.command("focus text");
        controller.hide();
        QTest::qWait(300);
        QVERIFY(!controller.isVisible());
        controller.command("focus text");
        QVERIFY(controller.isVisible());
    }

    void autoShowFollowsSettings()
    {
        QTemporaryDir dir;
        QFile settings(dir.path() + "/floatkey.conf");
        QVERIFY(settings.open(QIODevice::WriteOnly));
        settings.write("autoShowInTerminals=false\n");
        settings.close();
        {
            Controller controller(dir.path() + "/floatkey.conf");
            controller.command("focus terminal");
            QVERIFY(!controller.isVisible());
            QCOMPARE(controller.fieldType(), QString("terminal"));
            controller.command("focus whatever");
            QCOMPARE(controller.fieldType(), QString("text"));
            QVERIFY(controller.isVisible());
        }
        QVERIFY(settings.open(QIODevice::WriteOnly));
        settings.write("autoShow=false\n");
        settings.close();
        Controller controller(dir.path() + "/floatkey.conf");
        controller.command("focus text");
        QVERIFY(!controller.isVisible());
    }

    void hardwareKeyboardKeepsItAway()
    {
        QTemporaryDir dir;
        Controller controller(dir.path() + "/floatkey.conf");
        controller.command("focus text");
        QVERIFY(controller.isVisible());
        controller.setHardwareKeyboard(true);   // came up for the field, goes
        QVERIFY(!controller.isVisible());
        controller.command("blur");
        controller.command("focus text");
        QVERIFY(!controller.isVisible());
        controller.show();                      // by hand it still comes
        QVERIFY(controller.isVisible());
        controller.hide();
        controller.setHardwareKeyboard(false);
        controller.command("focus text");
        QVERIFY(controller.isVisible());
    }

    void watcherFindsKeyboardsByTheirLetterKeys()
    {
        // The Air75's bitmap from this tablet, and its power button's.
        QVERIFY(KeyboardWatcher::hasLetterKeys("73ffff 0 0 4c3ffff17aff32d bfd4445600000000 1 1130ffb8b17c007 "
                                               "ffff7bfad951dfff ffbeffdfffefffff fffffffffffffffe"));
        QVERIFY(!KeyboardWatcher::hasLetterKeys("4000000000000 0"));
        QVERIFY(!KeyboardWatcher::hasLetterKeys(""));

        QTemporaryDir sys;
        auto device = [&sys](const QString &event, const QByteArray &name, const QByteArray &keys) {
            QDir(sys.path()).mkpath(event + "/device/capabilities");
            QFile n(sys.path() + "/" + event + "/device/name");
            QVERIFY(n.open(QIODevice::WriteOnly));
            n.write(name + "\n");
            QFile k(sys.path() + "/" + event + "/device/capabilities/key");
            QVERIFY(k.open(QIODevice::WriteOnly));
            k.write(keys + "\n");
        };
        device("event0", "pmic_pwrkey", "4000000000000 0");
        QVERIFY(!KeyboardWatcher(sys.path(), sys.path()).isAttached());
        device("event7", "Air75 V3 ISO-1 Keyboard", "ffbeffdfffefffff fffffffffffffffe");
        KeyboardWatcher watcher(sys.path(), sys.path());
        QVERIFY(watcher.isAttached());
        QCOMPARE(watcher.names(), QStringList{"Air75 V3 ISO-1 Keyboard"});
    }

    void addonLinesArriveThroughTheSocket()
    {
        // Written and hung up on at once, the way fcitx5's addon does it.
        QTemporaryDir dir;
        Controller controller(dir.path() + "/floatkey.conf");
        const QString path = dir.path() + "/floatkey.sock";
        QVERIFY(controller.listen(path, nullptr));
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        const QByteArray native = path.toLocal8Bit();
        std::memcpy(address.sun_path, native.constData(), qMin<size_t>(native.size(), sizeof address.sun_path - 1));
        const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        QCOMPARE(::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address), 0);
        const char line[] = "focus email\n";
        QCOMPARE(::send(fd, line, sizeof line - 1, MSG_NOSIGNAL), qsizetype(sizeof line - 1));
        ::close(fd);
        QTRY_VERIFY(controller.isVisible());
        QCOMPARE(controller.fieldType(), QString("email"));
    }

    void addonNamesFieldsByTheirHints()
    {
        using fcitx::CapabilityFlag;
        using Flags = fcitx::CapabilityFlags;
        QCOMPARE(floatkey::fieldType(Flags{CapabilityFlag::Preedit}), "text");
        QCOMPARE(floatkey::fieldType(Flags{CapabilityFlag::Terminal}), "terminal");
        QCOMPARE(floatkey::fieldType(Flags{CapabilityFlag::Url, CapabilityFlag::SurroundingText}), "url");
        QCOMPARE(floatkey::fieldType(Flags{CapabilityFlag::Email}), "email");
        QCOMPARE(floatkey::fieldType(Flags{CapabilityFlag::Dialable}), "number");
        QCOMPARE(floatkey::fieldType(Flags{CapabilityFlag::Sensitive, CapabilityFlag::Url}), "password");
        QVERIFY(floatkey::wantsKeyboard(Flags{CapabilityFlag::Preedit}));
        QVERIFY(!floatkey::wantsKeyboard(Flags{CapabilityFlag::NoOnScreenKeyboard}));
        QVERIFY(!floatkey::wantsKeyboard(Flags{CapabilityFlag::Disable}));
    }

    // ---- theme and translator ----

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QVERIFY(theme.isDark());
    }

    void themeReadsColors()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QFile colors(dir.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("mode = \"light\"\naccent = \"#112233\"\nbackground = \"#fafafa\"\nforeground = \"#222222\"\n");
        colors.close();
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
        QCOMPARE(theme.background(), QString("#fafafa"));
        QVERIFY(!theme.isDark());
    }

    void translatorFollowsTheLanguage()
    {
        QTemporaryDir dir;
        QFile language(dir.path() + "/language");
        QVERIFY(language.open(QIODevice::WriteOnly));
        language.write("de\n");
        language.close();
        Translator translator(dir.path() + "/language");
        QCOMPARE(translator.translate("ModeMenu", "Floating"), QString("Schwebend"));
        QVERIFY(translator.translate("ModeMenu", "Nothing like this").isNull());
    }

    // ---- touch, on the real QML ----

    void tapTypesTheKey()
    {
        Harness h;
        QVERIFY(h.window);
        QVERIFY(h.keyboard.preloaded.contains(QString::fromUtf8("é")));
        h.tap(h.character("q"));
        QCOMPARE(h.keyboard.log, QStringList{"type:q:0"});
    }

    void rolloverKeepsPressOrder()
    {
        // The second thumb lifts first; the keys still land in press order.
        Harness h;
        const QPoint q = h.character("q"), w = h.character("w");
        h.press(0, q);
        h.press(1, w);
        h.release(1, w);
        h.release(0, q);
        QCOMPARE(h.keyboard.log, (QStringList{"type:q:0", "type:w:0"}));
    }

    void slidingPicksTheKeyUnderTheFinger()
    {
        Harness h;
        const QPoint q = h.character("q"), e = h.character("e");
        h.press(0, q);
        h.slide(0, q, e);
        h.release(0, e);
        QCOMPARE(h.keyboard.log, QStringList{"type:e:0"});
    }

    void shiftCapitalisesOnceOrLocks()
    {
        Harness h;
        const QPoint shift = h.special("shift"), a = h.character("a");
        h.tap(shift);
        h.tap(a);
        h.tap(a);
        QCOMPARE(h.keyboard.log, (QStringList{"type:A:0", "type:a:0"}));

        h.keyboard.log.clear();
        h.tap(shift);
        h.tap(shift);   // a double tap locks
        h.tap(a);
        h.tap(a);
        QCOMPARE(h.keyboard.log, (QStringList{"type:A:0", "type:A:0"}));
    }

    void modifiersApplyToTheNextKey()
    {
        Harness h;
        h.tap(h.special("ctrl"));
        h.tap(h.character("c"));
        h.tap(h.character("c"));
        QCOMPARE(h.keyboard.log, (QStringList{"type:c:1", "type:c:0"}));
    }

    void flickDownTypesTheUpperCharacter()
    {
        Harness h;
        const QPoint one = h.character("1");
        h.press(0, one);
        h.slide(0, one, one + QPoint(0, 26), 4);
        h.release(0, one + QPoint(0, 26));
        QCOMPARE(h.keyboard.log, QStringList{"type:!:0"});
    }

    void holdingALetterOffersAccents()
    {
        Harness h;
        const QPoint e = h.character("e");
        h.press(0, e);
        QTest::qWait(600);
        h.release(0, e);
        QCOMPARE(h.keyboard.log, QStringList{QString::fromUtf8("type:è:0")});
    }

    void spaceBarBecomesATrackpad()
    {
        Harness h;
        const QPoint space = h.special("space");
        h.press(0, space);
        h.slide(0, space, space + QPoint(160, 0), 16);
        h.release(0, space + QPoint(160, 0));
        QVERIFY(h.keyboard.log.count("key:Right:0") >= 4);
        QVERIFY(!h.keyboard.log.contains("type: :0"));
    }

    void twoSpacesMakeAFullStop()
    {
        Harness h;
        h.tap(h.character("a"));
        h.tap(h.special("space"));
        h.tap(h.special("space"));
        QCOMPARE(h.keyboard.log, (QStringList{"type:a:0", "type: :0", "key:BackSpace:0", "type:. :0"}));
    }

    void backspaceRepeatsWhileHeld()
    {
        Harness h;
        const QPoint backspace = h.special("backspace");
        h.press(0, backspace);
        QTest::qWait(700);
        h.release(0, backspace);
        QVERIFY(h.keyboard.log.count("key:BackSpace:0") >= 3);
    }

    void pinchingInFloatsTheKeyboard()
    {
        Harness h;
        const QPoint a = h.character("a"), l = h.character("l");
        h.press(0, a);
        h.press(1, l);
        const QPoint middle = (a + l) / 2;
        for (int i = 1; i <= 8; ++i) {
            QTest::touchEvent(h.window, Harness::device())
                .move(0, a + (middle - QPoint(30, 0) - a) * i / 8, h.window)
                .move(1, l + (middle + QPoint(30, 0) - l) * i / 8, h.window);
            QTest::qWait(10);
        }
        QTest::touchEvent(h.window, Harness::device()).release(0, middle, h.window).release(1, middle, h.window);
        QCOMPARE(h.controller.mode(), QString("floating"));
        QVERIFY(h.keyboard.log.isEmpty());
    }

    void holdingHideOpensThePlacements()
    {
        Harness h;
        const QPoint hide = h.special("hide");
        h.press(0, hide);
        QTest::qWait(600);
        QObject *menu = h.window->findChild<QObject *>("menu");
        QVERIFY(menu->property("opened").toBool());
        QTest::qWait(200);
        // Slide onto "Voice orb", the third row, and lift.
        const QPoint orb = h.evaluate(menu, "mapToItem(null, width / 2, 6 + rowHeight * 2.5)").toPointF().toPoint();
        h.slide(0, hide, orb);
        h.release(0, orb);
        QCOMPARE(h.controller.mode(), QString("orb"));
        QVERIFY(h.controller.isVisible());
    }

    void tappingHideHides()
    {
        Harness h;
        h.tap(h.special("hide"));
        QVERIFY(!h.controller.isVisible());
    }

    void compactPagesSwitchAndReturn()
    {
        Harness h("floating");
        h.tap(h.special("numbers"));
        QCOMPARE(h.keys()->property("page").toString(), QString("numbers"));
        h.tap(h.character("1"));
        h.tap(h.special("space"));
        QCOMPARE(h.keyboard.log, (QStringList{"type:1:0", "type: :0"}));
        QCOMPARE(h.keys()->property("page").toString(), QString("letters"));
    }

    void draggingMovesOrDocksIt()
    {
        Harness h("floating");
        QObject *handle = h.window->findChild<QObject *>("handle");
        // The grabber beside the orb.
        const QString onGrabber = "mapToItem(null, width / 2 - 43, height / 2)";
        const QPoint grip = h.evaluate(handle, onGrabber).toPointF().toPoint();
        const QPoint middle(h.window->width() / 2, h.window->height() / 2);
        h.press(0, grip);
        h.slide(0, grip, middle, 20);
        h.release(0, middle);
        QCOMPARE(h.controller.mode(), QString("floating"));
        QVERIFY(qAbs(h.controller.floatingPosition().x() - 0.5) < 0.1);

        // Down past the bottom edge, it docks.
        QTest::qWait(400);
        const QPoint from = h.evaluate(handle, onGrabber).toPointF().toPoint();
        const QPoint below(from.x(), h.window->height() + 120);
        h.press(0, from);
        h.slide(0, from, below, 20);
        h.release(0, below);
        QCOMPARE(h.controller.mode(), QString("docked"));
    }

    void dictationReadsTheDaemonsPhases()
    {
        QCOMPARE(Dictation::stateFor({}), QString("idle"));
        QCOMPARE(Dictation::stateFor({{"phase", "idle"}}), QString("idle"));
        QCOMPARE(Dictation::stateFor({{"phase", "recording"}}), QString("listening"));
        QCOMPARE(Dictation::stateFor({{"phase", "warming"}}), QString("listening"));
        QCOMPARE(Dictation::stateFor({{"phase", "transcribing"}}), QString("working"));

        QCOMPARE(Dictation::loudness(QByteArray(640, 0)), 0.0);
        QByteArray loud;
        for (int i = 0; i < 320; ++i) {
            const qint16 sample = i % 2 ? 20000 : -20000;
            loud.append(reinterpret_cast<const char *>(&sample), 2);
        }
        QVERIFY(Dictation::loudness(loud) > 0.9);
    }

    void dictationFollowsTheDaemon()
    {
        QTemporaryDir dir;
        FakeDaemon daemon(dir.path() + "/control.sock");
        Dictation dictation(dir.path() + "/control.sock", fakeSuperwhisper(dir.path()));
        dictation.setMetering(false);
        dictation.setWatching(true);

        // Started from Omarchy's key: the orb lights up all the same.
        daemon.status = {{"phase", "recording"}, {"recording_seconds", 3.5}};
        QTRY_COMPARE_WITH_TIMEOUT(dictation.state(), QString("listening"), 2000);
        QTRY_COMPARE(dictation.seconds(), 3.5);
        daemon.status = {{"phase", "transcribing"}};
        QTRY_COMPARE(dictation.state(), QString("working"));
        daemon.status = {{"phase", "idle"}};
        QTRY_COMPARE(dictation.state(), QString("done"));
        QTRY_COMPARE_WITH_TIMEOUT(dictation.state(), QString("idle"), 2000);
    }

    void dictationTrustsTheTapOverAStaleReply()
    {
        QTemporaryDir dir;
        FakeDaemon daemon(dir.path() + "/control.sock");
        Dictation dictation(dir.path() + "/control.sock", fakeSuperwhisper(dir.path()));
        dictation.setMetering(false);
        dictation.start();
        QCOMPARE(dictation.state(), QString("listening"));
        // The daemon still says idle for a moment: the orb stays lit.
        QTest::qWait(400);
        QCOMPARE(dictation.state(), QString("listening"));
        QTRY_COMPARE(voiceLog(dir.path()), QStringList{"start"});
        daemon.status = {{"phase", "recording"}};
        QTest::qWait(300);
        dictation.toggle();
        QCOMPARE(dictation.state(), QString("working"));
        QTRY_COMPARE(voiceLog(dir.path()), (QStringList{"start", "stop"}));
    }

    void tappingTheOrbDictates()
    {
        Harness h("floating");
        QObject *orb = h.window->findChild<QObject *>("orb");
        QVERIFY(orb->property("visible").toBool());
        const QPoint centre = h.evaluate(orb, "mapToItem(null, width / 2, height / 2)").toPointF().toPoint();
        h.tap(centre);
        QCOMPARE(h.dictation.state(), QString("listening"));
        QTRY_COMPARE(voiceLog(h.dir.path()), QStringList{"start"});
        // Its cancel button shows beside it, and lets the words go.
        QObject *cancel = h.window->findChild<QObject *>("cancelVoice");
        QTRY_VERIFY(cancel->property("opacity").toDouble() > 0.99);
        h.tap(h.evaluate(cancel, "mapToItem(null, width / 2, height / 2)").toPointF().toPoint());
        QCOMPARE(h.dictation.state(), QString("idle"));
        QTRY_COMPARE(voiceLog(h.dir.path()), (QStringList{"start", "cancel"}));
        // Typing never moved the keyboard.
        QCOMPARE(h.controller.mode(), QString("floating"));
        QVERIFY(h.keyboard.log.isEmpty());
    }

    void dockedMicKeyDictatesAndEscapeLetsGo()
    {
        Harness h;
        h.tap(h.special("mic"));
        QCOMPARE(h.dictation.state(), QString("listening"));
        h.tap(h.special("esc"));
        QCOMPARE(h.dictation.state(), QString("idle"));
        QTRY_COMPARE(voiceLog(h.dir.path()), (QStringList{"start", "cancel"}));
        QVERIFY(h.keyboard.log.isEmpty());
        // Without a dictation Escape is Escape.
        h.tap(h.special("esc"));
        QCOMPARE(h.keyboard.log, QStringList{"key:Escape:0"});
    }

    void holdingTheMicKeyTalks()
    {
        Harness h;
        const QPoint mic = h.special("mic");
        h.press(0, mic);
        QTest::qWait(600);
        QCOMPARE(h.dictation.state(), QString("listening"));
        h.release(0, mic);
        QCOMPARE(h.dictation.state(), QString("working"));
        QTRY_COMPARE(voiceLog(h.dir.path()), (QStringList{"start", "stop"}));
    }

    void xkbSaysWhatTheKeysType()
    {
        const QVariantMap french = XkbLayouts::read("fr");
        QCOMPARE(french.value("name").toString(), QString("French"));
        const QVariantMap keys = french.value("keys").toMap();
        QCOMPARE(keys.value("AD01").toStringList(), (QStringList{"a", "A"}));
        QCOMPARE(keys.value("AE02").toStringList(), (QStringList{"é", "2"}));
        // A dead key shows its accent.
        QCOMPARE(keys.value("AD11").toStringList().value(0), QString("^"));

        const QVariantMap dvorak = XkbLayouts::read("us(dvorak)").value("keys").toMap();
        QCOMPARE(dvorak.value("AD03").toStringList(), (QStringList{".", ">"}));
        QCOMPARE(XkbLayouts::describe("de(nodeadkeys)"), QString("German (no dead keys)"));
        QVERIFY(XkbLayouts::read("zz").isEmpty());
        QVERIFY(XkbLayouts::read("us;rm").isEmpty());
    }

    void keysFollowTheXkbLayout()
    {
        Harness h("docked", {"fr"});
        // AZERTY: a where q is on a US keyboard, and the number row's
        // unshifted characters.
        QCOMPARE(h.evaluate(h.keys(), "model.keys.filter(k => k.row === 1 && k.kind === 'char')[0].c").toString(),
                 QString("a"));
        h.tap(h.character("é"));
        QCOMPARE(h.keyboard.log, QStringList{"type:é:0"});
        QVERIFY(h.keyboard.preloaded.contains("ù"));
    }

    void compactLettersFollowTheXkbLayout()
    {
        Harness h("floating", {"ru"});
        QCOMPARE(h.evaluate(h.keys(), "model.keys[0].c").toString(), QString("й"));
        // The number pages are the usual ones.
        h.tap(h.special("numbers"));
        h.tap(h.character("1"));
        QCOMPARE(h.keyboard.log, QStringList{"type:1:0"});
    }

    void usKeyboardHasNoIsoKey()
    {
        Harness us("docked", {"us"});
        QCOMPARE(us.evaluate(us.keys(), "model.keys.filter(k => k.c === '<').length").toInt(), 0);
        Harness de("docked", {"de"});
        QCOMPARE(de.evaluate(de.keys(), "model.keys.filter(k => k.c === '<').length").toInt(), 1);
    }

    void orbModeIsTheOrbAlone()
    {
        Harness h("orb");
        QObject *panel = h.window->findChild<QObject *>("panel");
        QTRY_COMPARE(panel->property("width").toInt(), 236);
        QTRY_VERIFY(!h.keys()->property("visible").toBool());

        QObject *orb = h.window->findChild<QObject *>("orb");
        const QPoint centre = h.evaluate(orb, "mapToItem(null, width / 2, height / 2)").toPointF().toPoint();
        h.tap(centre);
        QCOMPARE(h.dictation.state(), QString("listening"));
        QTRY_COMPARE(voiceLog(h.dir.path()), QStringList{"start"});
        // Tapping the orb is not tapping the strip under it.
        QVERIFY(!h.window->findChild<QObject *>("menu")->property("opened").toBool());
        QCOMPARE(h.controller.mode(), QString("orb"));
    }

    void orbGrabbersOpenTheMenuAndDrag()
    {
        Harness h("orb");
        QObject *handle = h.window->findChild<QObject *>("handle");
        QTest::qWait(300);
        const QPoint grabber = h.evaluate(handle, "mapToItem(null, width / 2 - 70, height / 2)").toPointF().toPoint();
        h.tap(grabber);
        QObject *menu = h.window->findChild<QObject *>("menu");
        QTRY_VERIFY(menu->property("opened").toBool());
        // Its last row hides the keyboard (once it has faded in).
        const int rows = h.evaluate(menu, "entries.length").toInt();
        const QPoint hide = h.evaluate(menu, QString("mapToItem(null, width / 2, 6 + rowHeight * %1)").arg(rows - 0.5))
                                .toPointF().toPoint();
        QTest::qWait(200);
        h.tap(hide);
        QVERIFY(!h.controller.isVisible());

        // Dragged, it stays the orb, where it was let go.
        h.controller.show();
        QTest::qWait(350);
        const QPoint from = h.evaluate(handle, "mapToItem(null, width / 2 - 70, height / 2)").toPointF().toPoint();
        const QPoint to(300, 300);
        h.press(0, from);
        h.slide(0, from, to, 20);
        h.release(0, to);
        QCOMPARE(h.controller.mode(), QString("orb"));
        QVERIFY(h.controller.orbPosition().y() < 0.5);
    }

    void returnSendsWhatWasSaid()
    {
        Harness h("orb");
        QObject *orb = h.window->findChild<QObject *>("orb");
        QTest::qWait(300);
        const QPoint centre = h.evaluate(orb, "mapToItem(null, width / 2, height / 2)").toPointF().toPoint();
        h.tap(centre);
        QCOMPARE(h.dictation.state(), QString("listening"));
        QTest::qWait(200);
        h.tap(centre);
        // No daemon here, so the dictation is done as soon as the tap's
        // grace runs out: return comes up for a few seconds.
        QTRY_COMPARE_WITH_TIMEOUT(h.dictation.state(), QString("done"), 3000);
        QObject *send = h.window->findChild<QObject *>("sendVoice");
        QVERIFY(send->property("offered").toBool());
        QTest::qWait(250);
        h.tap(h.evaluate(send, "mapToItem(null, width / 2, height / 2)").toPointF().toPoint());
        QCOMPARE(h.keyboard.log, QStringList{"key:Return:0"});
        QVERIFY(!send->property("offered").toBool());
    }

    void returnGoesByItself()
    {
        Harness h("floating");
        QObject *send = h.window->findChild<QObject *>("sendVoice");
        QMetaObject::invokeMethod(send, "offer");
        QVERIFY(send->property("offered").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!send->property("offered").toBool(), 5000);
        QVERIFY(h.keyboard.log.isEmpty());
    }
};

QTEST_MAIN(FloatkeyTests)
#include "floatkey_tests.moc"
