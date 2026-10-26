#include <QDir>
#include <QFile>
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
#include "fieldtype.h"
#include "keymap.h"
#include "theme.h"
#include "translator.h"

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

// The real Main.qml, offscreen, with a fake keyboard and a temporary home.
class Harness {
public:
    explicit Harness(const QString &mode = "docked")
        : translator(dir.path() + "/language", dir.path()), theme(dir.path()),
          controller(dir.path() + "/floatkey.conf", {"us"})
    {
        controller.setMode(mode);
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("controller", &controller);
        engine.rootContext()->setContextProperty("virtualKeyboard", &keyboard);
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
        QCOMPARE(controller.command("right"), QString("ok"));
        QCOMPARE(controller.mode(), QString("right"));
        QVERIFY(controller.command("sing").startsWith("error"));
        QVERIFY(controller.command("type hello").startsWith("error"));   // no keyboard attached
    }

    void controllerRemembersPlacement()
    {
        QTemporaryDir dir;
        {
            Controller controller(dir.path() + "/floatkey.conf");
            controller.setMode("left");
            controller.setFloatingPosition(QPointF(0.25, 0.4));
        }
        Controller controller(dir.path() + "/floatkey.conf");
        QCOMPARE(controller.mode(), QString("left"));
        QCOMPARE(controller.floatingPosition(), QPointF(0.25, 0.4));
    }

    void controllerPicksLayouts()
    {
        QTemporaryDir dir;
        QCOMPARE(Controller(dir.path() + "/a.conf", {"de", "fr"}).layouts(), QStringList{"de"});
        QCOMPARE(Controller(dir.path() + "/b.conf", {"fr"}).layouts(), QStringList{"us"});
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
        // Slide onto "Left corner", the third row, and lift.
        const QPoint left = h.evaluate(menu, "mapToItem(null, width / 2, 6 + rowHeight * 2.5)").toPointF().toPoint();
        h.slide(0, hide, left);
        h.release(0, left);
        QCOMPARE(h.controller.mode(), QString("left"));
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

    void draggingIntoACornerTucksItThere()
    {
        Harness h("floating");
        QObject *handle = h.window->findChild<QObject *>("handle");
        const QPoint grip = h.evaluate(handle, "mapToItem(null, width / 2, height / 2)").toPointF().toPoint();
        const QPoint corner(80, h.window->height() - 20);
        h.press(0, grip);
        h.slide(0, grip, corner, 20);
        h.release(0, corner);
        QCOMPARE(h.controller.mode(), QString("left"));

        // And out again: anywhere in the open is floating.
        const QPoint from = h.evaluate(handle, "mapToItem(null, width / 2, height / 2)").toPointF().toPoint();
        const QPoint middle(h.window->width() / 2, h.window->height() / 2);
        QTest::qWait(400);
        h.press(0, from);
        h.slide(0, from, middle, 20);
        h.release(0, middle);
        QCOMPARE(h.controller.mode(), QString("floating"));
        QVERIFY(qAbs(h.controller.floatingPosition().x() - 0.5) < 0.1);
    }
};

QTEST_MAIN(FloatkeyTests)
#include "floatkey_tests.moc"
