#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QPainter>
#include <QPointingDevice>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QIcon>
#include <QTemporaryDir>
#include <QtMath>
#include <QtTest>

#include "apps.h"
#include "shelves.h"
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
    Q_PROPERTY(bool hand MEMBER hand NOTIFY handChanged)
    Q_PROPERTY(QVariantList dividers MEMBER dividers NOTIFY dividersChanged)
    Q_PROPERTY(bool barConnected MEMBER barConnected NOTIFY barConnectedChanged)
    Q_PROPERTY(bool liveResize MEMBER liveResize NOTIFY liveResizeChanged)

public:
    Q_INVOKABLE void closeWindow(const QString &address)
    {
        log << "close:" + address;
        if (!refuse)
            QTimer::singleShot(30, this, [this, address] { emit windowClosed(address); });
    }
    Q_INVOKABLE void swipe(const QString &direction) { log << "swipe:" + direction; }
    Q_INVOKABLE void refreshDividers() {}
    Q_INVOKABLE QVariantMap beginLift()
    {
        QVariantMap plan = plans["top"];
        plan["lift"] = true;
        plan["console"] = QVariantMap{{"x", consoleRect.x()}, {"y", consoleRect.y()}, {"w", consoleRect.width()},
                                      {"h", consoleRect.height()}};
        QMetaObject::invokeMethod(this, [this] { emit snapshotReady(1, true); }, Qt::QueuedConnection);
        return plan;
    }
    Q_INVOKABLE void hideConsole()
    {
        log << "hideConsole";
        consoleOpen = false;
        emit consoleOpenChanged();
    }
    Q_INVOKABLE QVariantMap beginDivider(const QVariantMap &divider)
    {
        QVariantMap plan = plans["divider"];
        plan["divider"] = divider;
        QMetaObject::invokeMethod(this, [this] { emit snapshotReady(1, true); }, Qt::QueuedConnection);
        return plan;
    }
    Q_INVOKABLE void resizeDivider(const QVariantMap &, qreal fraction)
    {
        log << QString("resize:%1").arg(fraction, 0, 'f', 2);
        QTimer::singleShot(30, this, [this] { emit dividersChanged(); });
    }
    Q_INVOKABLE void pushOff(const QStringList &addresses) { log << "pushoff:" + addresses.join(','); }
    Q_INVOKABLE void placeWindow(const QString &address, const QString &target, const QString &side)
    {
        log << QString("place:%1:%2:%3").arg(address, side, target);
    }
    Q_INVOKABLE void sendToWorkspace(const QString &address, int id) { log << QString("send:%1:%2").arg(address).arg(id); }
    // Live resizing: where the line was sent, and where it settled.
    Q_INVOKABLE void moveDivider(const QVariantMap &, qreal line) { moves << line; }
    Q_INVOKABLE void settleDivider(const QVariantMap &, qreal line)
    {
        log << QString("settle:%1").arg(qRound(line));
        QTimer::singleShot(30, this, [this] { emit dividersChanged(); });
    }
    Q_INVOKABLE void switchWorkspace(int id)
    {
        log << "switch:" + QString::number(id);
        QTimer::singleShot(20, this, [this, id] { emit workspaceShown(QString::number(id)); });
    }
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
    Q_INVOKABLE int pictureTurn(int) const { return 0; }
    Q_INVOKABLE void hideWindow(const QString &address) { hiding << "hide:" + address; }
    Q_INVOKABLE void showWindow(const QString &address) { hiding << "show:" + address; }

    bool consoleOpen = false;
    QRectF consoleRect;
    QVariantMap look{{"gapsIn", 5}, {"gapsOut", 10}, {"border", 2}, {"rounding", 0},
                     {"active", Stage::parseGradient("ff7aa2f7 0deg")},
                     {"inactive", Stage::parseGradient("aa595959 0deg")}};
    QHash<QString, QVariantMap> plans;
    QStringList log;
    QStringList hiding;   // windows hidden under their pictures, and shown again
    QVariantList region;
    bool hand = false;
    QVariantList dividers;
    bool barConnected = false;
    bool liveResize = false;
    QList<qreal> moves;
    bool refuse = false;   // windows that won't close

signals:
    void consoleOpenChanged();
    void snapshotReady(int id, bool ok);
    void placed(bool ok);
    void handChanged();
    void dividersChanged();
    void barConnectedChanged();
    void liveResizeChanged();
    void topPulled(const QVariantMap &plan, const QPointF &at);
    void topMoved(const QPointF &at);
    void topReleased(const QPointF &at, bool open);
    void pullStarted(const QVariantMap &plan, const QPointF &at, qreal ms);
    void pullMoved(const QPointF &at, qreal ms);
    void pullEnded(const QPointF &at, qreal ms);
    void pullCanceled();
    void grabStarted(const QVariantMap &plan, const QPointF &at);
    void grabMoved(const QPointF &at);
    void grabEnded(const QPointF &at);
    void swiped(const QString &direction);
    void windowClosed(const QString &address);
    void slideStarted(const QVariantMap &plan, const QPointF &at, qreal ms);
    void slideMoved(const QPointF &at, qreal ms);
    void slideEnded(const QPointF &at, qreal ms);
    void slideCanceled();
    void workspaceShown(const QString &name);
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
        shelves = std::make_unique<Shelves>(apps.get(), dir.path() + "/launcher.json", QStringList{"files"});

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
        fake.plans["close"] = {{"edge", "close"}, {"address", "0x9"}, {"appClass", "foot"}, {"title", "foot"},
                               {"x", 12}, {"y", 42}, {"w", 829}, {"h", 1146}, {"snapshot", 1},
                               {"fromX", 400}, {"fromY", 300}};
        const QVariantMap one{{"address", "0x9"}, {"appClass", "foot"}, {"x", 12}, {"y", 42}, {"w", 1672}, {"h", 1146},
                              {"active", true}, {"snapshot", 1}};
        fake.plans["slide"] = {{"edge", "slide"}, {"direction", "left"}, {"usable", rect(0, 30, 1696, 1170)},
                               {"current", QVariantList{one}}, {"next", 2}, {"nextWindows", QVariantList{one}},
                               {"fromX", 900}, {"fromY", 600}};
        fake.plans["bottom"] = fake.plans["right"];
        // Two windows side by side, and the line between them.
        auto tile = [](const char *address, qreal x) {
            return QVariantMap{{"address", address}, {"appClass", "foot"}, {"x", x}, {"y", 42}, {"w", 829}, {"h", 1146},
                               {"snapshot", 1}};
        };
        fake.plans["divider"] = {{"edge", "divider"}, {"before", QVariantList{tile("left", 12)}},
                                 {"after", QVariantList{tile("right", 855)}}, {"usable", rect(0, 30, 1696, 1170)},
                                 {"inConsole", false}};
        fake.dividers = {QVariantMap{{"vertical", true}, {"at", 848}, {"from", 42}, {"to", 1188}, {"start", 12},
                                     {"end", 1684}, {"before", QStringList{"left"}}, {"after", QStringList{"right"}}}};
        fake.plans["bottom"]["edge"] = "bottom";
        // The left one of two, lifted: it can go beside the right one, or to
        // workspace 2 or a new one.
        fake.plans["grab"] = fake.plans["close"];
        fake.plans["grab"]["edge"] = "grab";
        fake.plans["grab"]["workspace"] = "1";
        fake.plans["grab"]["workspaceId"] = 1;
        fake.plans["grab"]["others"] = QVariantList{tile("right", 855)};
        fake.plans["grab"]["workspaces"] = QVariantList{
            QVariantMap{{"id", 1}, {"current", true}, {"apps", QStringList{"foot"}}},
            QVariantMap{{"id", 2}, {"current", false}, {"apps", QStringList{"foot", "chromium"}}},
            QVariantMap{{"id", 3}, {"current", false}, {"empty", true}, {"apps", QStringList()}}};
        fake.plans["grab"]["usable"] = rect(0, 30, 1696, 1170);
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
        engine.rootContext()->setContextProperty("shelves", shelves.get());
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

    // Two fingers landing together, as stage reports them from the touch
    // screen: they pull the "close" plan's window `dy` down in `ms`.
    void handDown()
    {
        fake.hand = true;
        emit fake.handChanged();
        const int snapshot = fake.plans["close"].value("snapshot").toInt();
        if (snapshot >= 0)
            emit fake.snapshotReady(snapshot, true);
    }
    void handPull(qreal dy, int ms, bool lift = true)
    {
        const QVariantMap plan = fake.plans["close"];
        const QPointF from(plan["fromX"].toReal(), plan["fromY"].toReal());
        handDown();
        qreal t = 1000;
        emit fake.pullStarted(plan, from + QPointF(0, std::min<qreal>(24, dy)), t);
        const int steps = std::max(1, ms / 16);
        for (int i = 1; i <= steps; ++i) {
            QTest::qWait(16);
            t += 16;
            emit fake.pullMoved(from + QPointF(0, dy * i / steps), t);
        }
        if (lift)
            handUp(from + QPointF(0, dy), t + 8);
    }
    void handUp(const QPointF &at, qreal ms)
    {
        emit fake.pullEnded(at, ms);
        fake.hand = false;
        emit fake.handChanged();
    }

    // Two fingers that rested on the "grab" plan's window, carrying it to
    // `to` and, unless told not to, putting it down there.
    void handGrab(QPointF to, bool lift = true)
    {
        const QVariantMap plan = fake.plans["grab"];
        const QPointF from(plan["fromX"].toReal(), plan["fromY"].toReal());
        handDown();
        emit fake.grabStarted(plan, from);
        for (int i = 1; i <= 15; ++i) {
            QTest::qWait(16);
            emit fake.grabMoved(from + (to - from) * i / 15);
        }
        if (lift)
            putDown(to);
    }
    void putDown(QPointF at)
    {
        emit fake.grabEnded(at);
        fake.hand = false;
        emit fake.handChanged();
    }
    // The middle of a tile of the workspace strip.
    QPointF stripTile(int i) const
    {
        QObject *strip = window->findChild<QObject *>("workspaceStrip");
        QVariant box;
        QMetaObject::invokeMethod(strip, "tileBox", Q_RETURN_ARG(QVariant, box), Q_ARG(QVariant, i));
        return box.toRectF().center();
    }

    // Three fingers sliding `dx` sideways in `ms`.
    void handSlide(qreal dx, int ms)
    {
        const QVariantMap plan = fake.plans["slide"];
        const QPointF from(plan["fromX"].toReal(), plan["fromY"].toReal());
        fake.hand = true;
        emit fake.handChanged();
        emit fake.snapshotReady(1, true);
        qreal t = 1000;
        emit fake.slideStarted(plan, from + QPointF(dx < 0 ? -24 : 24, 0), t);
        const int steps = std::max(1, ms / 16);
        for (int i = 1; i <= steps; ++i) {
            QTest::qWait(16);
            t += 16;
            emit fake.slideMoved(from + QPointF(dx * i / steps, 0), t);
        }
        emit fake.slideEnded(from + QPointF(dx, 0), t + 8);
        fake.hand = false;
        emit fake.handChanged();
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
    std::unique_ptr<Shelves> shelves;
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

    // The launcher's rows, made up from the apps until one is moved.
    static QStringList rowNames(const Shelves &shelves)
    {
        QStringList names;
        for (const QVariant &row : shelves.rows()) {
            QStringList ids;
            for (const QVariant &app : row.toMap()["apps"].toList())
                ids << app.toMap()["appId"].toString();
            names << row.toMap()["name"].toString() + ":" + ids.join(',');
        }
        return names;
    }

    static void writeApps(const QString &dir)
    {
        QDir(dir).mkpath(".");
        write(dir + "/claude.desktop", "[Desktop Entry]\nType=Application\nName=Claude\nExec=claude-desktop\nCategories=Utility;\n");
        write(dir + "/x.desktop", "[Desktop Entry]\nType=Application\nName=X\nExec=omarchy-launch-webapp https://x.com/\n");
        write(dir + "/writer.desktop", "[Desktop Entry]\nType=Application\nName=Writer\nExec=lowriter\nCategories=Office;\n");
        write(dir + "/mpv.desktop", "[Desktop Entry]\nType=Application\nName=mpv\nExec=mpv\nCategories=AudioVideo;Video;\n");
        write(dir + "/foot.desktop", "[Desktop Entry]\nType=Application\nName=Foot\nExec=foot\nCategories=System;TerminalEmulator;\n");
        write(dir + "/odd.desktop", "[Desktop Entry]\nType=Application\nName=Odd\nExec=odd\n");
    }

    void theLauncherRowsAreMadeUpFromTheApps()
    {
        QTemporaryDir dir;
        writeApps(dir.path() + "/applications");
        Apps apps({dir.path() + "/applications"}, "en");
        Shelves shelves(&apps, dir.path() + "/launcher.json", {"foot", "gone"});
        // The dock's favourite first (and only there), AI by name, Omarchy's
        // web apps, then by category; an app of no kind goes under Other.
        QCOMPARE(rowNames(shelves), QStringList({"Favorites:foot", "AI:claude", "Web apps:x", "Office:writer",
                                                 "Media:mpv", "Other:odd"}));
        QVERIFY(!QFile::exists(dir.path() + "/launcher.json"));
    }

    void anAppMovedStaysWhereItWasPut()
    {
        QTemporaryDir dir;
        writeApps(dir.path() + "/applications");
        {
            Apps apps({dir.path() + "/applications"}, "en");
            Shelves shelves(&apps, dir.path() + "/launcher.json", {"foot"});
            shelves.moveApp("claude", 3, 0);   // into Office, first
            QCOMPARE(rowNames(shelves), QStringList({"Favorites:foot", "Web apps:x", "Office:claude,writer",
                                                     "Media:mpv", "Other:odd"}));
            shelves.moveApp("mpv", 2, 2);      // Office, last
            shelves.newRow("odd");
            shelves.moveRow(3, 0);
            QCOMPARE(rowNames(shelves), QStringList({"New row:odd", "Favorites:foot", "Web apps:x",
                                                     "Office:claude,writer,mpv"}));
        }
        // Kept across a restart, and an app installed since goes to its kind's row.
        write(dir.path() + "/applications/calc.desktop",
              "[Desktop Entry]\nType=Application\nName=Calc\nExec=localc\nCategories=Office;\n");
        write(dir.path() + "/applications/game.desktop",
              "[Desktop Entry]\nType=Application\nName=Game\nExec=game\nCategories=Game;\n");
        Apps apps({dir.path() + "/applications"}, "en");
        Shelves shelves(&apps, dir.path() + "/launcher.json", {"foot"});
        QCOMPARE(rowNames(shelves), QStringList({"New row:odd", "Favorites:foot", "Web apps:x",
                                                 "Office:claude,writer,mpv,calc", "Other:game"}));
    }

    void aMissingIconThemeGivesWayToTheNearest()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("Yaru");
        write(dir.path() + "/Yaru/index.theme", "[Icon Theme]\nName=Yaru\n");
        const QStringList before = QIcon::themeSearchPaths();
        QIcon::setThemeSearchPaths({dir.path()});
        QCOMPARE(Apps::installedIconTheme("Yaru"), QString("Yaru"));
        QCOMPARE(Apps::installedIconTheme("Yaru-gray"), QString("Yaru"));
        QCOMPARE(Apps::installedIconTheme("Nothing-like-it"), QString("hicolor"));
        QIcon::setThemeSearchPaths(before);
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

    void aHandPullsTheWindowItIsOn()
    {
        auto window = [](const char *address, qreal x, qreal y, qreal w, qreal h, const char *workspace = "1",
                         bool floating = false, bool fullscreen = false, int focus = 1) {
            return QVariantMap{{"address", address}, {"x", x}, {"y", y}, {"w", w}, {"h", h}, {"workspace", workspace},
                               {"floating", floating}, {"fullscreen", fullscreen}, {"focus", focus}};
        };
        const QVariantList tiled{window("left", 12, 42, 829, 1146), window("right", 855, 42, 829, 1146)};
        // Tiled windows have only wallpaper behind them.
        QVariantMap plan = Stage::windowAt(300, 500, tiled, {});
        QCOMPARE(plan["address"].toString(), QString("left"));
        QCOMPARE(plan["edge"].toString(), QString("close"));
        QCOMPARE(Stage::windowAt(1200, 500, tiled, {})["address"].toString(), QString("right"));
        // The bar and the gaps are nobody's.
        QVERIFY(Stage::windowAt(300, 15, tiled, {}).isEmpty());

        // A floating window is on top.
        QVariantList withFloat = tiled;
        withFloat << window("float", 200, 300, 600, 400, "1", true, false, 0);
        QCOMPARE(Stage::windowAt(300, 500, withFloat, {})["address"].toString(), QString("float"));
        QCOMPARE(Stage::windowAt(1200, 500, withFloat, {})["address"].toString(), QString("right"));

        // A maximized window covers the others.
        QVariantList maximized = tiled;
        maximized << window("max", 12, 42, 1672, 1146, "1", false, true);
        QCOMPARE(Stage::windowAt(1200, 500, maximized, {})["address"].toString(), QString("max"));

        // With the console down, only its windows can go.
        QVariantList console = tiled;
        console << window("agent", 261, 30, 1174, 587, "special:scratchpad");
        QCOMPARE(Stage::windowAt(600, 300, console, "special:scratchpad")["address"].toString(), QString("agent"));
        QVERIFY(Stage::windowAt(600, 900, console, "special:scratchpad").isEmpty());
    }

    void slidesReachOneNewWorkspace()
    {
        auto ws = [](int id, int windows) {
            return QJsonObject{{"id", id}, {"monitor", "DSI-1"}, {"windows", windows}};
        };
        const QJsonArray two{ws(1, 1), ws(2, 1)};
        QCOMPARE(Stage::neighbourOf(two, "DSI-1", 1, "left"), 2);
        QCOMPARE(Stage::neighbourOf(two, "DSI-1", 2, "right"), 1);
        // Past the last one with windows, an empty one to start something on...
        QCOMPARE(Stage::neighbourOf(two, "DSI-1", 2, "left"), 3);
        // ...but only one: from there, only back.
        const QJsonArray onTheNewOne{ws(1, 1), ws(2, 1), ws(3, 0)};
        QCOMPARE(Stage::neighbourOf(onTheNewOne, "DSI-1", 3, "left"), 0);
        QCOMPARE(Stage::neighbourOf(onTheNewOne, "DSI-1", 3, "right"), 2);
        QCOMPARE(Stage::neighbourOf(two, "DSI-1", 1, "right"), 0);
        // Gaps are skipped, and Omarchy's last workspace is the last.
        QCOMPARE(Stage::neighbourOf(QJsonArray{ws(1, 1), ws(4, 2)}, "DSI-1", 1, "left"), 4);
        QCOMPARE(Stage::neighbourOf(QJsonArray{ws(10, 1)}, "DSI-1", 10, "left"), 0);
    }

    void dividersAreWhereWindowsMeet()
    {
        auto w = [](const char *address, qreal x, qreal y, qreal width, qreal height) {
            return QVariantMap{{"address", address}, {"x", x}, {"y", y}, {"w", width}, {"h", height}};
        };
        // Two side by side, as on the tablet: one line down the middle.
        QVariantList found = Stage::dividersIn({w("left", 12, 42, 829, 1146), w("right", 855, 42, 829, 1146)}, 14);
        QCOMPARE(found.size(), 1);
        QVariantMap d = found.first().toMap();
        QVERIFY(d["vertical"].toBool());
        QCOMPARE(d["at"].toReal(), 848.0);
        QCOMPARE(d["from"].toReal(), 42.0);
        QCOMPARE(d["to"].toReal(), 1188.0);
        QCOMPARE(d["before"].toStringList(), QStringList({"left"}));
        QCOMPARE(d["after"].toStringList(), QStringList({"right"}));
        QCOMPARE(d["start"].toReal(), 12.0);
        QCOMPARE(d["end"].toReal(), 1684.0);

        // Two on top, one under both: a short line between the top two, and
        // one across the whole width with both of them above it.
        found = Stage::dividersIn({w("B", 12, 12, 396, 554), w("C", 422, 12, 395, 554), w("A", 12, 580, 805, 554)}, 14);
        QCOMPARE(found.size(), 2);
        const QVariantMap between = found.at(0).toMap(), across = found.at(1).toMap();
        QVERIFY(between["vertical"].toBool());
        QCOMPARE(between["at"].toReal(), 415.0);
        QCOMPARE(between["to"].toReal(), 566.0);
        QVERIFY(!across["vertical"].toBool());
        QCOMPARE(across["at"].toReal(), 573.0);
        QCOMPARE(across["before"].toStringList(), QStringList({"B", "C"}));
        QCOMPARE(across["after"].toStringList(), QStringList({"A"}));
        QCOMPARE(across["from"].toReal(), 12.0);
        QCOMPARE(across["to"].toReal(), 817.0);
        QCOMPARE(across["end"].toReal(), 1134.0);

        // Stacked, as in portrait.
        found = Stage::dividersIn({w("top", 12, 42, 1176, 812), w("bottom", 12, 868, 1176, 816)}, 14);
        QCOMPARE(found.size(), 1);
        QVERIFY(!found.first().toMap()["vertical"].toBool());

        // Alone, too far apart, or meeting only at a corner: no line.
        QVERIFY(Stage::dividersIn({w("only", 12, 42, 1672, 1146)}, 14).isEmpty());
        QVERIFY(Stage::dividersIn({w("a", 12, 42, 400, 400), w("b", 600, 42, 400, 400)}, 14).isEmpty());
        QVERIFY(Stage::dividersIn({w("a", 12, 42, 400, 400), w("b", 426, 456, 400, 400)}, 14).isEmpty());
    }

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
        QCOMPARE(kinds(pull(3, 300, 300)), QStringList({"hand", "hand", "pull", "ended", "up"}));
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

    void threeFingersSlideAndSwipe()
    {
        // Sideways they slide, from the start, the way they go.
        QCOMPARE(kinds(swipe(-400, 0)), QStringList({"hand", "hand", "slide", "slid", "up"}));
        QCOMPARE(last(swipe(-400, 0), Fingers::Event::SlideStarted).direction, QString("left"));
        QCOMPARE(last(swipe(400, 30), Fingers::Event::SlideEnded).direction, QString("right"));
        // Up they swipe once they lift.
        QCOMPARE(swiped(swipe(0, -300)), QString("up"));
        QCOMPARE(swiped(swipe(0, -100)), QString());   // too short
        // Between two directions, nothing.
        QCOMPARE(kinds(swipe(300, -250)), QStringList({"hand", "hand", "up"}));
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

    void twoFingersHeldStillLiftAWindow()
    {
        Fingers f;
        QList<Fingers::Event> events = f.down(0, {600, 400}, 0) + f.down(1, {720, 400}, 20);
        // Fingers held still wander a little.
        events += f.move(0, {604, 397}, 200) + f.move(1, {717, 405}, 250);
        QCOMPARE(f.fingers(), 2);
        QVERIFY(f.resting(14));
        QVERIFY(f.hold());
        // Held, the hand carries it anywhere, its fingers apart or not, and a
        // third finger joins nothing.
        events += f.move(0, {300, 900}, 600) + f.move(1, {900, 950}, 610);
        events += f.down(2, {1000, 400}, 615);
        events += f.up(0, 700) + f.up(1, 710);
        QCOMPARE(kinds(events), QStringList({"hand", "grabmoved", "grabmoved", "grabbed", "up"}));
        // Where the hand as a whole has taken its first finger.
        QCOMPARE(last(events, Fingers::Event::GrabEnded).at, QPointF(540, 925));

        // One finger lifted, the other carries on from where the hand had it,
        // without a jump, and the last one puts it down.
        Fingers c;
        c.down(0, {600, 400}, 0);
        c.down(1, {720, 400}, 20);
        QVERIFY(c.hold());
        c.move(0, {650, 450}, 100);
        c.move(1, {770, 450}, 100);
        QVERIFY(c.up(1, 200).isEmpty());
        QCOMPARE(c.point(), QPointF(650, 450));
        QList<Fingers::Event> one = c.move(0, {350, 150}, 300);
        QCOMPARE(kinds(one), QStringList({"grabmoved"}));
        QCOMPARE(one.first().at, QPointF(350, 150));
        QCOMPARE(kinds(c.up(0, 400)), QStringList({"grabbed", "up"}));

        // Moved away first, the hand rests no longer: it was a pull.
        Fingers g;
        g.down(0, {600, 400}, 0);
        g.down(1, {720, 400}, 20);
        g.move(0, {600, 440}, 100);
        g.move(1, {720, 440}, 100);
        QVERIFY(!g.resting(14));
        QVERIFY(!g.hold());
    }

    void theStripHasEveryWorkspaceAndANewOne()
    {
        auto ws = [](int id, const char *monitor = "DSI-1") { return QJsonObject{{"id", id}, {"monitor", monitor}}; };
        auto client = [](int ws, const char *appClass) {
            return QJsonObject{{"workspace", QJsonObject{{"id", ws}}}, {"class", appClass}, {"mapped", true}};
        };
        const QJsonArray clients{client(1, "foot"), client(1, "foot"), client(1, "chromium"), client(4, "obsidian"),
                                 client(-98, "foot")};
        const QVariantList tiles =
            Stage::workspaceTiles({ws(1), ws(4), ws(-98), ws(2, "HDMI-1")}, clients, "DSI-1", 4);
        QCOMPARE(tiles.size(), 3);
        QCOMPARE(tiles[0].toMap()["id"].toInt(), 1);
        QCOMPARE(tiles[0].toMap()["apps"].toStringList(), QStringList({"foot", "chromium"}));
        QCOMPARE(tiles[1].toMap()["current"].toBool(), true);
        // 2 is taken on the other screen, so the new one is 3.
        QCOMPARE(tiles[2].toMap()["id"].toInt(), 3);
        QVERIFY(tiles[2].toMap()["empty"].toBool());
        // All ten taken: no new one.
        QJsonArray full;
        for (int id = 1; id <= Stage::LastWorkspace; ++id)
            full << ws(id);
        QCOMPARE(Stage::workspaceTiles(full, {}, "DSI-1", 1).size(), Stage::LastWorkspace);
    }

    // ---- the gestures, on the real QML ----

    void atRestOnlyTheEdgesTakeTouches()
    {
        Harness h;
        QVERIFY(h.window);
        h.fake.dividers.clear();
        emit h.fake.dividersChanged();
        QCOMPARE(h.fake.region.size(), 3);
        h.fake.consoleOpen = true;
        emit h.fake.consoleOpenChanged();
        QCOMPARE(h.fake.region.size(), 6);
    }

    void swipingInFromTheRightMakesRoomForAnApp()
    {
        Harness h;
        // Leave the window 70% of its room, and let go: it keeps just that,
        // not the nearest third. The node dwindle splits is the window, its
        // border and half a gap each side.
        const qreal span = 1672 + 2 * (2 + 5);
        h.swipe(QPoint(1693, 600), QPoint(1693 - 506, 600));
        QTRY_COMPARE(h.phase(), QString("room"));
        QVERIFY(qAbs(h.pull() - span * 0.3) < 2);
        const QPoint foot = h.tile("foot");
        QVERIFY(foot.x() > 1100);
        h.tap(foot);
        QCOMPARE(h.fake.log.last(), QString("launch:right:foot:0.70"));
        QTRY_COMPARE(h.phase(), QString("idle"));
    }

    void pulledPastWhereItGivesItKeepsAQuarter()
    {
        Harness h;
        h.swipe(QPoint(1693, 600), QPoint(1693 - 1500, 600));
        QTRY_COMPARE(h.phase(), QString("room"));
        QVERIFY(qAbs(h.pull() - (1672 + 14) * 0.75) < 2);
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

    void holdingAnAppInTheLauncherArrangesIt()
    {
        Harness h;
        // Files is the dock's favourite; Foot, of no kind, is under Other.
        QCOMPARE(rowNames(*h.shelves), QStringList({"Favorites:files", "Other:foot"}));
        h.swipe(QPoint(1693, 600), QPoint(1693 - 840, 600));
        QTRY_COMPARE(h.phase(), QString("room"));
        const QPoint foot = h.tile("foot"), into = h.tile("files") - QPoint(30, 0);
        h.press(foot);
        QTest::qWait(600);
        for (int i = 1; i <= 10; ++i) {
            QTest::qWait(16);
            h.move(foot + QPoint((into.x() - foot.x()) * i / 10, (into.y() - foot.y()) * i / 10));
        }
        QTest::qWait(60);
        h.release(into);
        // In before Files; Other, emptied, is gone. Nothing was opened.
        QTRY_COMPARE(rowNames(*h.shelves), QStringList({"Favorites:foot,files"}));
        QVERIFY(!h.fake.log.join(' ').contains("launch"));
        // Arranging, a tap opens nothing either.
        h.tap(h.tile("files"));
        QTest::qWait(100);
        QVERIFY(!h.fake.log.join(' ').contains("launch"));
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

    void withTheBarTheTopEdgeIsTheBars()
    {
        Harness h;
        h.fake.dividers.clear();
        emit h.fake.dividersChanged();
        h.fake.barConnected = true;
        emit h.fake.barConnectedChanged();
        QCOMPARE(h.fake.region.size(), 2);
        h.swipe(QPoint(800, 3), QPoint(800, 400));
        QVERIFY(!h.fake.log.contains("openConsole"));
        QCOMPARE(h.phase(), QString("idle"));
        h.fake.barConnected = false;
        emit h.fake.barConnectedChanged();
        QCOMPARE(h.fake.region.size(), 3);
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

    void swipingUpFromTheBottomTakesTheConsoleUp()
    {
        Harness h;
        h.fake.consoleOpen = true;
        h.fake.consoleRect = QRectF(0, 30, 1696, 1158);   // its windows fill the screen
        emit h.fake.consoleOpenChanged();
        h.swipe(QPoint(800, 1196), QPoint(800, 700), 400, false);
        // Its pictures follow the finger; the real one is gone already.
        QCOMPARE(h.phase(), QString("tracking"));
        QVERIFY(h.fake.log.contains("hideConsole"));
        QVERIFY(h.pull() < 1158 - 400);
        h.release(QPoint(800, 700));
        QTRY_COMPARE(h.phase(), QString("idle"));
        QVERIFY(!h.fake.log.contains("openConsole"));
        QVERIFY(!h.fake.log.join(' ').contains("begin:bottom"));
    }

    void aShortPushPutsTheConsoleBack()
    {
        Harness h;
        h.fake.consoleOpen = true;
        emit h.fake.consoleOpenChanged();
        h.swipe(QPoint(800, 1000), QPoint(800, 940), 600);
        QTRY_VERIFY(h.fake.log.contains("openConsole"));
        QTRY_COMPARE(h.phase(), QString("idle"));
    }

    void pullingFromTheBarBringsTheConsoleDown()
    {
        Harness h;
        QVariantMap plan = h.fake.plans["top"];
        plan["fromX"] = 848;
        plan["fromY"] = 15;
        emit h.fake.topPulled(plan, QPointF(848, 15));
        for (int y = 40; y <= 400; y += 40) {
            QTest::qWait(16);
            emit h.fake.topMoved(QPointF(848, y));
        }
        // The console is where the finger has it.
        QCOMPARE(qRound(h.pull()), 385);
        emit h.fake.topReleased(QPointF(848, 400), true);
        QTRY_VERIFY(h.fake.log.contains("openConsole"));
        QTRY_COMPARE(h.phase(), QString("idle"));
    }

    // ---- holding the line between two windows ----

    void theLineBetweenWindowsTakesTouches()
    {
        Harness h;
        // The right and bottom edges, the top strip, and a grip along the line.
        QCOMPARE(h.fake.region.size(), 4);
        // It reaches a little into the windows on both sides, and stops short
        // of the bottom edge's strip.
        QCOMPARE(h.fake.region.last().toRectF(), QRectF(830, 42, 36, 1138));
    }

    void holdingTheLineGrabsItAndDraggingResizes()
    {
        Harness h;
        h.press(QPoint(848, 600));
        QTest::qWait(80);
        QCOMPARE(h.phase(), QString("holding"));
        QTRY_COMPARE_WITH_TIMEOUT(h.phase(), QString("tracking"), 1000);
        for (int x = 860; x <= 1100; x += 20) {
            QTest::qWait(16);
            h.move(QPoint(x, 600));
        }
        QTest::qWait(40);
        QCOMPARE(qRound(h.pull()), 252);
        QTest::qWait(200);
        h.release(QPoint(1100, 600));
        QTRY_COMPARE(h.fake.log, QStringList({"resize:0.67"}));
        QTRY_COMPARE(h.phase(), QString("idle"));
    }

    void resizingLiveMovesTheWindowsThemselves()
    {
        Harness h;
        h.fake.liveResize = true;
        emit h.fake.liveResizeChanged();
        h.press(QPoint(848, 600));
        QTRY_COMPARE_WITH_TIMEOUT(h.phase(), QString("tracking"), 1000);
        for (int x = 860; x <= 1100; x += 20) {
            QTest::qWait(16);
            h.move(QPoint(x, 600));
        }
        QTest::qWait(40);
        // The windows went along with the line, not pictures of them.
        QVERIFY(h.fake.moves.size() > 5);
        QCOMPARE(qRound(h.fake.moves.last()), 1100);
        h.release(QPoint(1100, 600));
        // It settles on two thirds of the room (12 to 1684), where it lets
        // Hyprland animate it to, and never resizes the pictures' way.
        QTRY_COMPARE(h.fake.log, QStringList({"settle:1127"}));
        QTRY_COMPARE(h.phase(), QString("idle"));
    }

    void aSecondFingerPutsLiveWindowsBack()
    {
        Harness h;
        h.fake.liveResize = true;
        emit h.fake.liveResizeChanged();
        h.press(QPoint(848, 600));
        QTRY_COMPARE_WITH_TIMEOUT(h.phase(), QString("tracking"), 1000);
        for (int x = 860; x <= 1000; x += 20) {
            QTest::qWait(16);
            h.move(QPoint(x, 600));
        }
        h.handDown();
        QTRY_COMPARE(h.phase(), QString("idle"));
        QCOMPARE(h.fake.log, QStringList({"settle:848"}));
    }

    void aQuickTouchOnTheLineDoesNothing()
    {
        Harness h;
        h.press(QPoint(848, 600));
        QTest::qWait(60);
        h.release(QPoint(848, 600));
        QTRY_COMPARE(h.phase(), QString("idle"));
        QVERIFY(h.fake.log.isEmpty());
    }

    void movingAwayBeforeTheGrabLetsGo()
    {
        Harness h;
        h.press(QPoint(848, 600));
        QTest::qWait(60);
        h.move(QPoint(848, 640));
        QTRY_COMPARE(h.phase(), QString("idle"));
        QTest::qWait(600);
        QCOMPARE(h.phase(), QString("idle"));
        QVERIFY(h.fake.log.isEmpty());
    }

    void aSecondFingerLetsGoOfTheLine()
    {
        Harness h;
        h.press(QPoint(848, 600));
        QTest::qWait(60);
        h.handDown();
        QTRY_COMPARE(h.phase(), QString("idle"));
    }

    void pushingTheLineToTheEdgeSendsThatWindowAway()
    {
        Harness h;
        h.press(QPoint(848, 600));
        QTRY_COMPARE_WITH_TIMEOUT(h.phase(), QString("tracking"), 1000);
        for (int x = 880; x <= 1660; x += 60) {
            QTest::qWait(16);
            h.move(QPoint(x, 600));
        }
        QTest::qWait(200);
        h.release(QPoint(1660, 600));
        QTRY_VERIFY(h.fake.log.contains("pushoff:right"));
    }

    void tappingTheDimmedScreenClosesTheConsole()
    {
        Harness h;
        h.fake.consoleOpen = true;
        emit h.fake.consoleOpenChanged();
        h.tap(QPoint(800, 900));
        QVERIFY(h.fake.log.contains("closeConsole"));
    }

    // ---- two fingers pulling a window down ----

    void pullingAWindowDownClosesIt()
    {
        Harness h;
        h.handPull(300, 300, false);
        // The picture follows the fingers.
        QVERIFY(h.window->property("showing").toBool());
        QCOMPARE(qRound(h.pull()), 300);
        // The real window is out of the way while its picture is.
        QTRY_COMPARE(h.fake.hiding, QStringList({"hide:0x9"}));
        h.handUp(QPointF(400, 600), 1400);
        // Asked once the picture is off the screen.
        QVERIFY(h.fake.log.isEmpty());
        QTRY_COMPARE(h.fake.log, QStringList({"close:0x9"}));
        QTRY_COMPARE(h.phase(), QString("idle"));
        // Closed, it isn't asked back.
        QCOMPARE(h.fake.hiding, QStringList({"hide:0x9"}));
    }

    void aShortSlowPullPutsTheWindowBack()
    {
        Harness h;
        h.handPull(60, 600);
        QTRY_COMPARE(h.phase(), QString("idle"));
        QVERIFY(h.fake.log.isEmpty());
        QCOMPARE(h.pull(), 0.0);
        QCOMPARE(h.fake.hiding, QStringList({"hide:0x9", "show:0x9"}));
    }

    void aQuickFlickClosesToo()
    {
        Harness h;
        h.handPull(80, 48);
        QTRY_COMPARE(h.fake.log, QStringList({"close:0x9"}));
    }

    void aWindowThatStaysShowsAgain()
    {
        Harness h;
        h.fake.refuse = true;
        h.handPull(300, 300);
        QTRY_COMPARE(h.fake.log, QStringList({"close:0x9"}));
        QTRY_COMPARE(h.phase(), QString("leaving"));
        QTRY_COMPARE_WITH_TIMEOUT(h.phase(), QString("idle"), 3000);
        QCOMPARE(h.fake.hiding, QStringList({"hide:0x9", "show:0x9"}));
    }

    void aWindowWithoutAPictureJustCloses()
    {
        Harness h;
        // No picture could be taken (-1, as Capture says).
        h.fake.plans["close"]["snapshot"] = -1;
        h.handPull(300, 300, false);
        QVERIFY(!h.window->property("showing").toBool());
        h.handUp(QPointF(400, 600), 1400);
        QCOMPARE(h.fake.log, QStringList({"close:0x9"}));
        QCOMPARE(h.phase(), QString("idle"));
        QVERIFY(h.fake.hiding.isEmpty());
    }

    void aLiftedWindowGoesBesideAnother()
    {
        Harness h;
        // Lifted, the left window's place closes up: the right one takes the
        // whole width, springing there.
        h.handGrab(QPointF(1500, 600), false);
        QCOMPARE(h.phase(), QString("tracking"));
        QVERIFY(h.window->property("showing").toBool());
        QTRY_VERIFY(h.window->property("board").toBool());
        QTRY_COMPARE(h.fake.hiding, QStringList({"hide:0x9"}));
        auto at = [&h](const char *address) {
            return h.window->property("arranged").toMap()[address].toMap();
        };
        // Over its right third, it makes way to the left for the window.
        const QVariantMap zone = h.window->property("dropZone").toMap();
        QCOMPARE(zone["side"].toString(), QString("right"));
        QCOMPARE(zone["x"].toReal(), 12 + (1672 + 14) / 2.0);
        QCOMPARE(zone["w"].toReal(), (1672 + 14) / 2.0 - 14);
        QCOMPARE(at("right")["x"].toReal(), 12.0);
        QCOMPARE(at("right")["w"].toReal(), (1672 + 14) / 2.0 - 14);
        h.putDown(QPointF(1500, 600));
        QCOMPARE(h.fake.log, QStringList({"place:0x9:right:right"}));
        QTRY_COMPARE(h.phase(), QString("idle"));
        QCOMPARE(h.fake.hiding, QStringList({"hide:0x9", "show:0x9"}));

        // Its middle splits it top and bottom.
        Harness g;
        g.handGrab(QPointF(848, 1000), false);
        QTRY_VERIFY(g.window->property("board").toBool());
        QTest::qWait(50);
        emit g.fake.grabMoved(QPointF(848, 1000));
        QCOMPARE(g.window->property("dropZone").toMap()["side"].toString(), QString("bottom"));
        g.putDown(QPointF(848, 1000));
        QCOMPARE(g.fake.log, QStringList({"place:0x9:bottom:right"}));
    }

    void theOthersCloseUpWhereTheWindowWas()
    {
        Harness h;
        // One window on the left, two stacked on the right, 14 px apart.
        auto w = [](const char *address, qreal x, qreal y, qreal width, qreal height) {
            return QVariantMap{{"address", address}, {"x", x}, {"y", y}, {"w", width}, {"h", height}};
        };
        const QVariantMap left = w("left", 12, 42, 829, 1146), top = w("top", 855, 42, 829, 566),
                          bottom = w("bottom", 855, 622, 829, 566);
        auto closeUp = [&h](const QVariantList &others, const QVariantMap &gone) {
            QVariant out;
            QMetaObject::invokeMethod(h.window, "closeUp", Q_RETURN_ARG(QVariant, out), Q_ARG(QVariant, others),
                                      Q_ARG(QVariant, gone));
            return out.toMap();
        };
        // The top right one goes: the one under it grows up to the top.
        QVariantMap after = closeUp({left, bottom}, top);
        QCOMPARE(after["bottom"].toMap()["y"].toReal(), 42.0);
        QCOMPARE(after["bottom"].toMap()["h"].toReal(), 1146.0);
        QCOMPARE(after["left"].toMap()["w"].toReal(), 829.0);
        // The left one goes: both on the right grow across, still stacked.
        after = closeUp({top, bottom}, left);
        QCOMPARE(after["top"].toMap()["x"].toReal(), 12.0);
        QCOMPARE(after["top"].toMap()["w"].toReal(), 1672.0);
        QCOMPARE(after["bottom"].toMap()["w"].toReal(), 1672.0);
        QCOMPARE(after["bottom"].toMap()["y"].toReal(), 622.0);
    }

    void aLiftedWindowGoesToAnotherWorkspace()
    {
        Harness h;
        h.handGrab(QPointF(800, 600), false);
        // The strip has come down; over it, the tile lights up.
        QTRY_VERIFY(h.window->findChild<QQuickItem *>("workspaceStrip")->isVisible());
        const QPointF two = h.stripTile(1);
        for (int i = 1; i <= 10; ++i) {
            QTest::qWait(16);
            emit h.fake.grabMoved(QPointF(800, 600) + (two - QPointF(800, 600)) * i / 10);
        }
        QCOMPARE(h.window->property("dropTile").toInt(), 1);
        QVERIFY(h.window->property("dropZone").isNull());
        h.putDown(two);
        QCOMPARE(h.fake.log, QStringList({"send:0x9:2"}));
        QTRY_COMPARE(h.phase(), QString("idle"));

        // The last tile is a new one.
        Harness g;
        g.handGrab(QPointF(800, 600), false);
        QTRY_VERIFY(g.window->findChild<QQuickItem *>("workspaceStrip")->isVisible());
        QTest::qWait(300);
        const QPointF last = g.stripTile(2);
        emit g.fake.grabMoved(last);
        g.putDown(last);
        QCOMPARE(g.fake.log, QStringList({"send:0x9:0"}));
    }

    void aLiftedWindowPutDownAnywhereElseGoesBack()
    {
        // On the wallpaper below the windows, and on its own workspace's tile.
        Harness h;
        h.handGrab(QPointF(500, 1195));
        QTRY_COMPARE(h.phase(), QString("idle"));
        QVERIFY(h.fake.log.isEmpty());
        QCOMPARE(h.fake.hiding, QStringList({"hide:0x9", "show:0x9"}));

        Harness g;
        g.handGrab(QPointF(800, 600), false);
        QTRY_VERIFY(g.window->findChild<QQuickItem *>("workspaceStrip")->isVisible());
        QTest::qWait(300);
        const QPointF here = g.stripTile(0);
        emit g.fake.grabMoved(here);
        g.putDown(here);
        QTRY_COMPARE(g.phase(), QString("idle"));
        QVERIFY(g.fake.log.isEmpty());
    }

    void aHandPutsDownAnEdgeGesture()
    {
        Harness h;
        h.swipe(QPoint(1693, 600), QPoint(1300, 600), 200, false);
        QCOMPARE(h.phase(), QString("tracking"));
        h.handDown();
        QCOMPARE(h.phase(), QString("idle"));
        // The edge finger's last moves and its lift are the hand's now.
        h.move(QPoint(1200, 600));
        h.release(QPoint(1200, 600));
        QCOMPARE(h.phase(), QString("idle"));
        QVERIFY(!h.fake.log.join(' ').contains("launch"));
    }

    void handsWaitWhileTheLauncherIsOut()
    {
        Harness h;
        h.swipe(QPoint(1693, 600), QPoint(1100, 600), 300);
        QTRY_COMPARE(h.phase(), QString("room"));
        h.handPull(300, 300);
        emit h.fake.swiped("left");
        QCOMPARE(h.phase(), QString("room"));
        QVERIFY(!h.fake.log.join(' ').contains("close:"));
        QVERIFY(!h.fake.log.join(' ').contains("swipe:"));
    }

    void threeFingersSwipeUpToMaximize()
    {
        Harness h;
        emit h.fake.swiped("up");
        QCOMPARE(h.fake.log, QStringList({"swipe:up"}));
    }

    void slidingAQuarterBringsTheNextWorkspace()
    {
        Harness h;
        h.handSlide(-500, 300);
        QTRY_COMPARE(h.fake.log, QStringList({"switch:2"}));
        QTRY_COMPARE(h.phase(), QString("idle"));
    }

    void aShortSlowSlideStays()
    {
        Harness h;
        h.handSlide(-200, 800);
        QTRY_COMPARE(h.phase(), QString("idle"));
        QVERIFY(h.fake.log.isEmpty());
    }

    void theLastWorkspaceOnlyGives()
    {
        Harness h;
        h.fake.plans["slide"]["next"] = QVariant();
        h.fake.plans["slide"].remove("nextWindows");
        h.handSlide(-800, 300);
        QTRY_COMPARE(h.phase(), QString("idle"));
        QVERIFY(h.fake.log.isEmpty());
    }

    void threeFingersPinchToCopyAndSpreadToPaste()
    {
        // Three fingers drawing in towards their middle: copy.
        const QList<Fingers::Event> in = pinch(160, 40);
        QCOMPARE(last(in, Fingers::Event::Pinched).direction, QString("in"));
        QVERIFY(!kinds(in).contains("pull"));
        QVERIFY(!kinds(in).contains("slide"));
        QVERIFY(swiped(in).isEmpty());
        // Spreading out from close together: paste.
        QCOMPARE(last(pinch(40, 170), Fingers::Event::Pinched).direction, QString("out"));
        // A small squeeze is no pinch, and a hand moving together never is.
        QCOMPARE(last(pinch(160, 120), Fingers::Event::Pinched).type, Fingers::Event::HandDown);
        QCOMPARE(last(swipe(-300, 0), Fingers::Event::Pinched).type, Fingers::Event::HandDown);
        // Two fingers pinching are left to the apps, to zoom.
        Fingers two;
        QList<Fingers::Event> events;
        events += two.down(0, {600, 500}, 0);
        events += two.down(1, {800, 500}, 10);
        for (int s = 1; s <= 20; ++s) {
            events += two.move(0, {600 + 4.0 * s, 500}, 20 + s * 8);
            events += two.move(1, {800 - 4.0 * s, 500}, 20 + s * 8);
        }
        QCOMPARE(last(events, Fingers::Event::Pinched).type, Fingers::Event::HandDown);
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
    // Three fingers around a middle, `from` away from it, drawing to `to`.
    static QList<Fingers::Event> pinch(qreal from, qreal to)
    {
        Fingers f;
        QList<Fingers::Event> events;
        const QPointF middle(800, 500);
        const QList<qreal> angles{-90, 30, 150};
        auto at = [&](int i, qreal r) {
            const qreal a = qDegreesToRadians(angles[i]);
            return middle + QPointF(std::cos(a), std::sin(a)) * r;
        };
        for (int i = 0; i < 3; ++i)
            events += f.down(i, at(i, from), i * 15);
        for (int s = 1; s <= 30; ++s)
            for (int i = 0; i < 3; ++i)
                events += f.move(i, at(i, from + (to - from) * s / 30), 30 + s * 8);
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
            {Fingers::Event::Swiped, "swiped"},      {Fingers::Event::HandUp, "up"},
            {Fingers::Event::SlideStarted, "slide"}, {Fingers::Event::SlideEnded, "slid"},
            {Fingers::Event::SlideCanceled, "slidecanceled"}, {Fingers::Event::GrabMoved, "grabmoved"},
            {Fingers::Event::GrabEnded, "grabbed"}};
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
