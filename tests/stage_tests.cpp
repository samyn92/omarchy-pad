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

    void luaStringsNeverBreak()
    {
        QCOMPARE(Hyprland::quote("foot"), QString("[[foot]]"));
        QCOMPARE(Hyprland::quote("a]]b"), QString("[=[a]]b]=]"));
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
        // Leave two thirds to the window, and let go.
        h.swipe(QPoint(1693, 600), QPoint(1693 - 560, 600));
        QTRY_COMPARE(h.phase(), QString("room"));
        QVERIFY(qAbs(h.pull() - 1672 / 3.0) < 2);
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
        QVERIFY(qAbs(h.pull() - 1672 / 2.0) < 2);
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
};

QTEST_MAIN(StageTests)
#include "stage_tests.moc"
