#include <QDir>
#include <QFile>
#include <QPainter>
#include <QPointingDevice>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include "apps.h"
#include "fingers.h"
#include "hyprland.h"
#include "images.h"
#include "stage.h"
#include "theme.h"
#include "translator.h"

// Stands in for the controller: hands the QML a plan for each edge and writes
// down what it was asked to do.
class FakeStage : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool consoleOpen MEMBER consoleOpen NOTIFY consoleOpenChanged)
    Q_PROPERTY(QRectF consoleRect MEMBER consoleRect NOTIFY consoleOpenChanged)
    Q_PROPERTY(QVariantMap look MEMBER look CONSTANT)

public:
    Q_INVOKABLE QVariantMap beginEdge(const QString &edge, qreal, qreal)
    {
        log << "begin:" + edge;
        QVariantMap plan = plans.value(edge);
        if (plan.contains("snapshot"))
            QMetaObject::invokeMethod(this, [this, plan] { emit snapshotReady(plan["snapshot"].toInt(), true); },
                                      Qt::QueuedConnection);
        return plan;
    }
    Q_INVOKABLE void launch(const QVariantMap &plan, const QString &appId, qreal fraction)
    {
        log << QString("launch:%1:%2:%3").arg(plan["edge"].toString(), appId).arg(fraction, 0, 'f', 2);
        QTimer::singleShot(50, this, [this] { emit placed(true); });
    }
    Q_INVOKABLE void openConsole()
    {
        log << "openConsole";
        consoleOpen = true;
        emit consoleOpenChanged();
    }
    Q_INVOKABLE void closeConsole() { log << "closeConsole"; }
    Q_INVOKABLE void setInputRegion(const QVariantList &rects) { region = rects; }

    bool consoleOpen = false;
    QRectF consoleRect;
    QVariantMap look{{"gapsIn", 5}, {"gapsOut", 10}, {"border", 2}, {"rounding", 0},
                     {"active", Stage::parseGradient("ff7aa2f7 0deg")},
                     {"inactive", Stage::parseGradient("aa595959 0deg")}};
    QHash<QString, QVariantMap> plans;
    QStringList log;
    QVariantList region;

signals:
    void consoleOpenChanged();
    void snapshotReady(int id, bool ok);
    void placed(bool ok);
};

static QVariantMap rect(qreal x, qreal y, qreal w, qreal h)
{
    return {{"x", x}, {"y", y}, {"w", w}, {"h", h}};
}

static void write(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}

// The real Main.qml, offscreen, on a 1696×1200 screen like the OnePlus Pad's.
class Harness {
public:
    Harness() : translator(dir.path() + "/language", dir.path()), theme(dir.path())
    {
        QDir(dir.path()).mkpath("applications");
        write(dir.path() + "/applications/foot.desktop",
              "[Desktop Entry]\nType=Application\nName=Foot\nExec=foot\nIcon=foot\n");
        write(dir.path() + "/applications/files.desktop",
              "[Desktop Entry]\nType=Application\nName=Files\nExec=nautilus --new-window %U\nIcon=folder\n");
        apps = std::make_unique<Apps>(QStringList{dir.path() + "/applications"}, "en");

        // A screen picture with something on it, as the capture would give.
        QImage screen(3392, 2400, QImage::Format_RGB32);
        screen.fill(QColor("#334455"));
        QPainter(&screen).fillRect(100, 100, 600, 400, QColor("#ddaa33"));
        snapshots = new Snapshots;
        snapshots->insert(1, screen);

        fake.consoleRect = QRectF(261, 30, 1174, 585);
        fake.plans["right"] = {{"edge", "right"}, {"allowed", true}, {"empty", false}, {"address", "0x1"},
                               {"appClass", "foot"}, {"title", "foot"}, {"x", 12}, {"y", 42}, {"w", 1672}, {"h", 1146},
                               {"usable", rect(0, 30, 1696, 1170)}, {"snapshot", 1}, {"workspace", "1"}};
        fake.plans["bottom"] = fake.plans["right"];
        fake.plans["bottom"]["edge"] = "bottom";
        fake.plans["top"] = {{"edge", "top"}, {"allowed", true}, {"empty", false},
                             {"console", rect(261, 30, 1174, 585)},
                             {"windows", QVariantList{QVariantMap{{"x", 261}, {"y", 30}, {"w", 1174}, {"h", 585},
                                                                  {"appClass", "foot"}, {"snapshot", 1}}}},
                             {"usable", rect(0, 30, 1696, 1170)}};

        engine.addImageProvider("icon", new IconProvider);
        engine.addImageProvider("snapshot", snapshots);
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("stage", &fake);
        engine.rootContext()->setContextProperty("apps", apps.get());
        engine.load(QUrl("qrc:/Main.qml"));
        window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        if (!window)
            return;
        window->resize(1696, 1200);
        window->show();
        if (!QTest::qWaitForWindowExposed(window))
            qWarning("the overlay never showed");
    }

    QString phase() const { return window->property("phase").toString(); }
    qreal pull() const { return window->property("pull").toReal(); }

    void press(QPoint at) { QTest::touchEvent(window, device()).press(0, at, window); }
    void move(QPoint to) { QTest::touchEvent(window, device()).move(0, to, window); }
    void release(QPoint at) { QTest::touchEvent(window, device()).release(0, at, window); }
    void tap(QPoint at)
    {
        press(at);
        QTest::qWait(30);
        release(at);
    }
    // A finger dragging from one point to another in `ms`.
    void swipe(QPoint from, QPoint to, int ms = 300, bool lift = true)
    {
        press(from);
        const int steps = 15;
        for (int i = 1; i <= steps; ++i) {
            QTest::qWait(ms / steps);
            move(from + (to - from) * i / steps);
        }
        if (lift)
            release(to);
    }

    // The centre of the launcher tile for an app, in window coordinates.
    // Views give their delegates a visual parent only, so walk the item tree.
    QPoint tile(const QString &appId) const
    {
        QList<QQuickItem *> items{window->contentItem()};
        while (!items.isEmpty()) {
            QQuickItem *item = items.takeFirst();
            items += item->childItems();
            if (item->property("appId").toString() == appId && item->isVisible() && item->width() > 0) {
                const QPointF centre = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
                if (QRectF(0, 0, window->width(), window->height()).contains(centre))
                    return centre.toPoint();
            }
        }
        return {-1, -1};
    }

    static QPointingDevice *device()
    {
        static QPointingDevice *touch = QTest::createTouchDevice();
        return touch;
    }

    QTemporaryDir dir;
    Translator translator;
    Theme theme;
    std::unique_ptr<Apps> apps;
    Snapshots *snapshots = nullptr;
    FakeStage fake;
    QQmlApplicationEngine engine;
    QQuickWindow *window = nullptr;
};

class StageTests : public QObject {
    Q_OBJECT

private slots:
    // ---- what a gesture acts on ----

    void aLoneWindowCanBeSplitBothWays()
    {
        const QRectF usable(0, 30, 1696, 1170);
        const QVariantList one{QVariantMap{{"x", 12}, {"y", 42}, {"w", 1672}, {"h", 1146}, {"address", "a"}}};
        QVariantMap plan = Stage::pickTarget("right", 1690, 600, one, usable, 4);
        QVERIFY(plan["allowed"].toBool());
        QCOMPARE(plan["address"].toString(), QString("a"));
        QVERIFY(Stage::pickTarget("bottom", 800, 1195, one, usable, 4)["allowed"].toBool());
        QVERIFY(Stage::pickTarget("right", 1690, 600, {}, usable, 4)["empty"].toBool());
    }

    void theFingerPicksTheWindowBesideIt()
    {
        const QRectF usable(0, 30, 1696, 1170);
        const QVariantList grid{
            QVariantMap{{"x", 12}, {"y", 42}, {"w", 829}, {"h", 1146}, {"address", "left"}},
            QVariantMap{{"x", 855}, {"y", 42}, {"w", 829}, {"h", 566}, {"address", "top-right"}},
            QVariantMap{{"x", 855}, {"y", 622}, {"w", 829}, {"h", 566}, {"address", "bottom-right"}},
        };
        QCOMPARE(Stage::pickTarget("right", 1690, 900, grid, usable, 4)["address"].toString(), QString("bottom-right"));
        QCOMPARE(Stage::pickTarget("bottom", 300, 1195, grid, usable, 4)["address"].toString(), QString("left"));
        // Already sharing the width: no more room on that side.
        QCOMPARE(Stage::pickTarget("right", 1690, 200, grid, usable, 4)["reason"].toString(), QString("split"));
        // The left one still has the full height to give.
        QVERIFY(Stage::pickTarget("bottom", 300, 1195, grid, usable, 4)["allowed"].toBool());
        QCOMPARE(Stage::pickTarget("bottom", 300, 1195, grid, usable, 3)["reason"].toString(), QString("full"));
    }

    void theConsoleMatchesOmarchys()
    {
        const QRectF usable(0, 26, 1696, 1174);
        QCOMPARE(Stage::consoleBox(usable, 1), QRectF(261, 26, 1174, 587));
        QCOMPARE(Stage::consoleBox(usable, 2), QRectF(0, 26, 1696, 587));
    }

    void desktopEntriesBecomeApps()
    {
        QTemporaryDir dir;
        write(dir.path() + "/web.desktop", "[Desktop Entry]\nType=Application\nName=Web\nName[de]=Netz\n"
                                           "Exec=helium --app=%u \"--x=100%%\"\nIcon=web\n");
        write(dir.path() + "/hidden.desktop", "[Desktop Entry]\nType=Application\nName=Hidden\nExec=x\nNoDisplay=true\n");
        write(dir.path() + "/tui.desktop", "[Desktop Entry]\nType=Application\nName=Top\nExec=btop\nTerminal=true\n");
        Apps::App app;
        QVERIFY(Apps::parse(dir.path() + "/web.desktop", "de_DE", &app));
        QCOMPARE(app.name, QString("Netz"));
        QCOMPARE(app.command, QString("helium --app= \"--x=100%\""));
        QVERIFY(!Apps::parse(dir.path() + "/hidden.desktop", "en", &app));
        QVERIFY(!Apps::parse(dir.path() + "/tui.desktop", "en", &app));
        Apps apps({dir.path()}, "en");
        QCOMPARE(apps.rowCount(), 1);
        QCOMPARE(apps.name("web"), QString("Web"));
    }

    void bordersReadLikeHyprlands()
    {
        const QVariantMap one = Stage::parseGradient("ff7aa2f7 0deg");
        QCOMPARE(one["colors"].toList(), QVariantList{"#ff7aa2f7"});
        const QVariantMap two = Stage::parseGradient("ee33ccff ee00ff99 45deg");
        QCOMPARE(two["colors"].toList().size(), 2);
        QCOMPARE(two["angle"].toInt(), 45);
    }

    void luaStringsNeverBreak()
    {
        QCOMPARE(Hyprland::quote("foot"), QString("[[foot]]"));
        QCOMPARE(Hyprland::quote("a]]b"), QString("[=[a]]b]=]"));
    }

    // ---- a hand on the touch screen ----

    void theTouchScreenTurnsWithTheScreen()
    {
        const QSizeF landscape(1696, 1200), portrait(1200, 1696);
        // The panel's top left corner, wherever the screen has turned it.
        QCOMPARE(Fingers::map({0, 0}, 0, landscape), QPointF(0, 0));
        QCOMPARE(Fingers::map({0, 0}, 1, portrait), QPointF(1200, 0));
        QCOMPARE(Fingers::map({0, 0}, 2, landscape), QPointF(1696, 1200));
        QCOMPARE(Fingers::map({0, 0}, 3, portrait), QPointF(0, 1696));
        for (int t = 0; t < 4; ++t) {
            const QSizeF size = t % 2 ? portrait : landscape;
            const QPointF at(300, 450);
            const QPointF back = Fingers::map(Fingers::unmap(at, t, size), t, size);
            QVERIFY2(qAbs(back.x() - at.x()) < 1e-6 && qAbs(back.y() - at.y()) < 1e-6, qPrintable(QString::number(t)));
        }
    }

    void realPullsCount()
    {
        // Pulls recorded on the tablet: 110 px in 0.15 s up to 400 px in 0.55 s, one leaning 15 degrees.
        QCOMPARE(kinds(pull(2, 110, 150)), QStringList({"hand", "pull", "ended", "up"}));
        QCOMPARE(kinds(pull(2, 400, 550)), QStringList({"hand", "pull", "ended", "up"}));
        QCOMPARE(kinds(pull(2, 300, 230, 15)), QStringList({"hand", "pull", "ended", "up"}));
        QCOMPARE(kinds(pull(3, 300, 300)), QStringList({"hand", "pull", "ended", "up"}));
        // The pull follows the hand one to one.
        const QList<Fingers::Event> events = pull(2, 300, 300);
        const QPointF start = events.first().at;
        QCOMPARE(qRound(last(events, Fingers::Event::PullEnded).at.y() - start.y()), 300);
    }

    void handsThatDontPullDoNothing()
    {
        // Upward, sideways, or leaning too far.
        QCOMPARE(kinds(pull(2, -300, 300)), QStringList({"hand", "up"}));
        QCOMPARE(kinds(pull(2, 300, 300, 80)), QStringList({"hand", "up"}));
        QCOMPARE(kinds(pull(2, 300, 300, 40)), QStringList({"hand", "up"}));
        // A two-finger tap.
        QCOMPARE(kinds(pull(2, 0, 80)), QStringList({"hand", "up"}));
        // A pinch whose middle drifts down: one finger stays, the other comes 200 px down.
        Fingers f;
        QList<Fingers::Event> pinch = f.down(0, {600, 400}, 0) + f.down(1, {700, 400}, 20);
        for (int i = 1; i <= 25; ++i)
            pinch += f.move(1, {700, 400 + i * 8.0}, 20 + i * 8);
        pinch += f.up(1, 300) + f.up(0, 310);
        QCOMPARE(kinds(pinch), QStringList({"hand", "up"}));
    }

    void fingersApartAreNoHand()
    {
        // Landing 300 ms apart.
        Fingers f;
        QList<Fingers::Event> events = f.down(0, {600, 400}, 0) + f.down(1, {700, 400}, 300);
        for (int i = 1; i <= 20; ++i)
            events += f.move(0, {600, 400 + i * 10.0}, 300 + i * 8) + f.move(1, {700, 400 + i * 10.0}, 300 + i * 8);
        QVERIFY(events.isEmpty());
        QVERIFY(!f.hand());
    }

    void aRestingThumbIsNotPartOfTheHand()
    {
        Fingers f;
        QList<Fingers::Event> events = f.down(9, {5, 600}, 0);   // holds the tablet
        events += pull(f, 2, 300, 300, 0, 2000);
        QCOMPARE(kinds(events), QStringList({"hand", "pull", "ended", "up"}));
        QCOMPARE(events.first().fingers, 2);
        QVERIFY(f.up(9, 3000).isEmpty());
    }

    void threeFingersSwipe()
    {
        QCOMPARE(swiped(swipe(-400, 0)), QString("left"));
        QCOMPARE(swiped(swipe(400, 30)), QString("right"));
        QCOMPARE(swiped(swipe(0, -300)), QString("up"));
        QCOMPARE(swiped(swipe(100, 0)), QString());    // too short
        QCOMPARE(swiped(swipe(300, -200)), QString()); // between two directions
    }

    void theHandEndsAtItsFirstLift()
    {
        Fingers f;
        QList<Fingers::Event> events = f.down(0, {600, 400}, 0) + f.down(1, {720, 400}, 20);
        for (int i = 1; i <= 20; ++i)
            events += f.move(0, {600, 400 + i * 10.0}, 20 + i * 8) + f.move(1, {720, 400 + i * 10.0}, 20 + i * 8);
        events += f.up(0, 200);
        // The finger left behind moves on: the pull is over.
        events += f.move(1, {720, 900}, 220);
        events += f.up(1, 240);
        QCOMPARE(kinds(events), QStringList({"hand", "pull", "ended", "up"}));
        // And a new hand can form afterwards.
        QCOMPARE(kinds(pull(f, 2, 200, 200, 0, 1000)), QStringList({"hand", "pull", "ended", "up"}));
    }

    void fingersThatPartCancelThePull()
    {
        Fingers f;
        QList<Fingers::Event> events = f.down(0, {600, 400}, 0) + f.down(1, {720, 400}, 20);
        for (int i = 1; i <= 10; ++i)
            events += f.move(0, {600, 400 + i * 10.0}, 20 + i * 8) + f.move(1, {720, 400 + i * 10.0}, 20 + i * 8);
        // Now they spread apart.
        events += f.move(1, {1100, 500}, 120);
        events += f.up(0, 150) + f.up(1, 160);
        QCOMPARE(kinds(events), QStringList({"hand", "pull", "canceled", "up"}));
    }

    // ---- the gestures, on the real QML ----

    void atRestOnlyTheEdgesTakeTouches()
    {
        Harness h;
        QVERIFY(h.window);
        QCOMPARE(h.fake.region.size(), 3);
        h.fake.consoleOpen = true;
        emit h.fake.consoleOpenChanged();
        QCOMPARE(h.fake.region.size(), 6);
    }

    void swipingInFromTheRightMakesRoomForAnApp()
    {
        Harness h;
        // Leave two thirds to the window, and let go. The node dwindle
        // splits is the window, its border and half a gap each side.
        const qreal span = 1672 + 2 * (2 + 5);
        h.swipe(QPoint(1693, 600), QPoint(1693 - 560, 600));
        QTRY_COMPARE(h.phase(), QString("room"));
        QVERIFY(qAbs(h.pull() - span / 3.0) < 2);
        const QPoint foot = h.tile("foot");
        QVERIFY(foot.x() > 1100);
        h.tap(foot);
        QCOMPARE(h.fake.log.last(), QString("launch:right:foot:0.67"));
        QTRY_COMPARE(h.phase(), QString("idle"));
    }

    void aQuickFlickMakesHalves()
    {
        Harness h;
        h.swipe(QPoint(1693, 600), QPoint(1693 - 300, 600), 90);
        QTRY_COMPARE(h.phase(), QString("room"));
        QVERIFY(qAbs(h.pull() - (1672 + 14) / 2.0) < 2);
    }

    void aShortPullSpringsBack()
    {
        Harness h;
        h.swipe(QPoint(1693, 600), QPoint(1693 - 30, 600), 400);
        QTRY_COMPARE(h.phase(), QString("idle"));
        QVERIFY(!h.fake.log.join(' ').contains("launch"));
    }

    void noRoomOnlyGivesALittle()
    {
        Harness h;
        h.fake.plans["right"]["allowed"] = false;
        h.fake.plans["right"]["reason"] = "full";
        h.swipe(QPoint(1693, 600), QPoint(1693 - 400, 600), 300, false);
        QVERIFY(h.pull() > 5 && h.pull() < 37);
        h.release(QPoint(1693 - 400, 600));
        QTRY_COMPARE(h.phase(), QString("idle"));
    }

    void tappingBesideTheLauncherPutsThingsBack()
    {
        Harness h;
        h.swipe(QPoint(1693, 600), QPoint(1693 - 840, 600));
        QTRY_COMPARE(h.phase(), QString("room"));
        h.tap(QPoint(300, 600));
        QTRY_COMPARE(h.phase(), QString("idle"));
        QVERIFY(!h.fake.log.join(' ').contains("launch"));
    }

    void swipingUpFromTheBottomSplitsTheOtherWay()
    {
        Harness h;
        h.swipe(QPoint(800, 1197), QPoint(800, 1197 - 580));
        QTRY_COMPARE(h.phase(), QString("room"));
        h.tap(h.tile("files"));
        QCOMPARE(h.fake.log.last(), QString("launch:bottom:files:0.50"));
    }

    void pullingDownTheTopBringsTheConsole()
    {
        Harness h;
        h.swipe(QPoint(800, 3), QPoint(800, 400));
        QTRY_VERIFY(h.fake.log.contains("openConsole"));
        QTRY_COMPARE(h.phase(), QString("idle"));
    }

    void anEmptyConsoleOffersApps()
    {
        Harness h;
        h.fake.plans["top"]["empty"] = true;
        h.fake.plans["top"]["windows"] = QVariantList();
        h.swipe(QPoint(800, 3), QPoint(800, 400));
        QTRY_COMPARE(h.phase(), QString("room"));
        h.tap(h.tile("foot"));
        QCOMPARE(h.fake.log.last(), QString("launch:top:foot:1.00"));
    }

    void tappingTheDimmedScreenClosesTheConsole()
    {
        Harness h;
        h.fake.consoleOpen = true;
        emit h.fake.consoleOpenChanged();
        h.tap(QPoint(800, 900));
        QVERIFY(h.fake.log.contains("closeConsole"));
    }

private:
    // A hand of `fingers` 120 px apart lands 20 ms apart at `start` ms, comes
    // `distance` px down (up when negative) in `ms`, leaning `lean` degrees,
    // then lifts.
    static QList<Fingers::Event> pull(Fingers &f, int fingers, qreal distance, int ms, qreal lean = 0, qreal start = 0)
    {
        QList<Fingers::Event> events;
        const QPointF dir(std::sin(qDegreesToRadians(lean)), std::cos(qDegreesToRadians(lean)));
        for (int i = 0; i < fingers; ++i)
            events += f.down(i, {500 + i * 120.0, 300}, start + i * 20);
        const qreal from = start + (fingers - 1) * 20;
        const int steps = std::max(1, ms / 8);
        for (int s = 1; s <= steps; ++s)
            for (int i = 0; i < fingers; ++i)
                events += f.move(i, QPointF(500 + i * 120.0, 300) + dir * (distance * s / steps), from + s * 8);
        for (int i = 0; i < fingers; ++i)
            events += f.up(i, from + steps * 8 + 10 + i * 15);
        return events;
    }
    static QList<Fingers::Event> pull(int fingers, qreal distance, int ms, qreal lean = 0)
    {
        Fingers f;
        return pull(f, fingers, distance, ms, lean);
    }
    static QList<Fingers::Event> swipe(qreal dx, qreal dy)
    {
        Fingers f;
        QList<Fingers::Event> events;
        for (int i = 0; i < 3; ++i)
            events += f.down(i, {700 + i * 100.0, 500}, i * 15);
        for (int s = 1; s <= 30; ++s)
            for (int i = 0; i < 3; ++i)
                events += f.move(i, {700 + i * 100.0 + dx * s / 30, 500 + dy * s / 30}, 30 + s * 8);
        for (int i = 0; i < 3; ++i)
            events += f.up(i, 300 + i * 10);
        return events;
    }
    static QString swiped(const QList<Fingers::Event> &events)
    {
        for (const Fingers::Event &e : events)
            if (e.type == Fingers::Event::Swiped)
                return e.direction;
        return {};
    }
    // What happened, without the many moves in between.
    static QStringList kinds(const QList<Fingers::Event> &events)
    {
        static const QHash<int, QString> names{
            {Fingers::Event::HandDown, "hand"},      {Fingers::Event::PullStarted, "pull"},
            {Fingers::Event::PullEnded, "ended"},    {Fingers::Event::PullCanceled, "canceled"},
            {Fingers::Event::Swiped, "swiped"},      {Fingers::Event::HandUp, "up"}};
        QStringList out;
        for (const Fingers::Event &e : events)
            if (names.contains(e.type))
                out << names[e.type];
        return out;
    }
    static Fingers::Event last(const QList<Fingers::Event> &events, Fingers::Event::Type type)
    {
        for (auto it = events.crbegin(); it != events.crend(); ++it)
            if (it->type == type)
                return *it;
        return {};
    }
};

QTEST_MAIN(StageTests)
#include "stage_tests.moc"
