#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

#include "cameras.h"
#include "focus.h"
#include "frame.h"
#include "orientation.h"
#include "photo.h"
#include "theme.h"
#include "viewfinder.h"

// A 4×2 picture with one red pixel in the top-left corner, to follow it as
// pictures turn.
static QImage marked()
{
    QImage image(4, 2, QImage::Format_RGB32);
    image.fill(Qt::black);
    image.setPixelColor(0, 0, Qt::red);
    return image;
}

static void touch(const QString &path)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
}

static QPoint redPixel(const QImage &image)
{
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixelColor(x, y) == QColor(Qt::red))
                return {x, y};
    return {-1, -1};
}

class CameraTests : public QObject {
    Q_OBJECT

private slots:
    void backCameraHeldLandscapeSavesUpright()
    {
        // The Pad 4's sensors are mounted upright for the landscape panel.
        QCOMPARE(Orientation::frameRotation(0, 0), 0);
        QCOMPARE(Orientation::photo(marked(), 0), marked());
        // A sensor mounted a quarter turn off is turned back clockwise.
        QCOMPARE(Orientation::frameRotation(90, 0), 90);
        const QImage turned = Orientation::photo(marked(), 90);
        QCOMPARE(turned.size(), QSize(2, 4));
        QCOMPARE(redPixel(turned), QPoint(1, 0));
    }

    void portraitTurnsThePhoto()
    {
        // The screen turned a quarter clockwise (portrait): the frame turns a
        // quarter back, and so does the photo.
        QCOMPARE(Orientation::frameRotation(0, 90), 270);
        QCOMPARE(Orientation::frameRotation(0, 270), 90);
        QCOMPARE(Orientation::frameRotation(90, 90), 0);
        QCOMPARE(Orientation::frameRotation(0, 180), 180);
        const QImage turned = Orientation::photo(marked(), 270);
        QCOMPARE(turned.size(), QSize(2, 4));
        QCOMPARE(redPixel(turned), QPoint(0, 3));
        QCOMPARE(Orientation::turned(QSize(4152, 3120), 270), QSize(3120, 4152));
    }

    void frontPreviewIsMirroredButThePhotoIsNot()
    {
        const QSizeF frame(4, 2);
        QCOMPARE(Orientation::preview(0, false, frame).map(QPointF(0, 0)), QPointF(0, 0));
        QCOMPARE(Orientation::preview(0, true, frame).map(QPointF(0, 0)), QPointF(4, 0));
        // Turned and mirrored, the frame still lands on the turned rectangle.
        const QRectF placed = Orientation::preview(90, true, frame).mapRect(QRectF(QPointF(), frame));
        QCOMPARE(placed, QRectF(0, 0, 2, 4));
        // The photo has no mirror: rotation is all it gets.
        QCOMPARE(Orientation::photo(marked(), 0), marked());
    }

    void photoNamesSortByTimeAndNeverCollide()
    {
        QTemporaryDir dir;
        const QDateTime when(QDate(2026, 11, 13), QTime(19, 5, 1));
        const QString first = Photo::nextName(dir.path(), when);
        QCOMPARE(first, QString("IMG_20261113_190501.jpg"));
        touch(dir.filePath(first));
        QCOMPARE(Photo::nextName(dir.path(), when), QString("IMG_20261113_190501_2.jpg"));
        // A name handed out to a photo still being written isn't handed out again.
        QCOMPARE(Photo::nextName(dir.path(), when, {"IMG_20261113_190501_2.jpg"}), QString("IMG_20261113_190501_3.jpg"));
        touch(dir.filePath("IMG_20261113_190501_2.jpg"));
        touch(dir.filePath("IMG_20261112_235959.jpg"));
        QCOMPARE(Photo::newest(dir.path()), dir.filePath("IMG_20261113_190501_2.jpg"));
        QCOMPARE(Photo::newest(dir.filePath("nothing-here")), QString());
    }

    void photosCarryTheirExif()
    {
        QImage picture(64, 48, QImage::Format_RGB888);
        picture.fill(Qt::darkGreen);
        const QDateTime when(QDate(2026, 11, 13), QTime(19, 5, 1));
        const QByteArray jpeg = Photo::jpeg(picture, when);
        QVERIFY(jpeg.startsWith("\xff\xd8\xff\xe1"));
        QVERIFY(jpeg.contains(QByteArray("Exif\0\0", 6)));
        QVERIFY(jpeg.contains("2026:11:13 19:05:01"));
        QVERIFY(jpeg.contains("Omarchy Pad Camera"));
        QBuffer buffer;
        buffer.setData(jpeg);
        QImageReader reader(&buffer, "JPEG");
        QCOMPARE(reader.read().size(), QSize(64, 48));
    }

    void focusSweepSettlesOnTheSharpestPosition()
    {
        // Sharpness peaking at lens position 700, as a near subject would.
        auto curve = [](int position) { return std::exp(-std::pow((position - 700) / 120.0, 2)); };
        FocusSweep sweep;
        sweep.begin(0, 1023);
        int measured = 0;
        while (!sweep.done() && measured < 100) {
            sweep.feed(curve(sweep.position()));
            ++measured;
        }
        QVERIFY(sweep.done());
        QVERIFY2(std::abs(sweep.best() - 700) <= 30, qPrintable(QString::number(sweep.best())));
        QCOMPARE(sweep.position(), sweep.best());
        // Fewer measurements than every coarse and fine step: the coarse pass
        // stops once it is clearly past the peak.
        QVERIFY2(measured <= 13, qPrintable(QString::number(measured)));
    }

    void focusSweepFindsAFarSubjectAtTheStart()
    {
        auto curve = [](int position) { return 1.0 / (1 + position / 50.0); };
        FocusSweep sweep;
        sweep.begin(0, 1023);
        while (!sweep.done())
            sweep.feed(curve(sweep.position()));
        QVERIFY2(sweep.best() <= 30, qPrintable(QString::number(sweep.best())));
    }

    void sharpnessPrefersTheSharperPicture()
    {
        QImage sharp(64, 64, QImage::Format_RGB32);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                sharp.setPixel(x, y, ((x / 4 + y / 4) % 2) ? 0xffc0c0c0 : 0xff404040);
        const QImage blurred = sharp.scaled(8, 8, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                   .scaled(64, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QVERIFY(sharpness(sharp) > sharpness(blurred) * 2);
        // The same picture brighter isn't sharper.
        QImage brighter = sharp;
        QPainter(&brighter).fillRect(brighter.rect(), QColor(255, 255, 255, 40));
        QVERIFY(std::abs(sharpness(brighter) - sharpness(sharp)) < sharpness(sharp) * 0.5);
    }

    void framesCopyOnlyThePartAsked()
    {
        QImage source(8, 4, QImage::Format_RGBX8888);
        source.fill(Qt::black);
        source.setPixelColor(5, 2, Qt::red);
        Frame frame;
        frame.size = source.size();
        frame.stride = int(source.bytesPerLine());
        frame.format = source.format();
        frame.data = source.constBits();
        const QImage part = frame.copy(QRect(4, 2, 4, 2));
        QCOMPARE(part.size(), QSize(4, 2));
        QCOMPARE(part.pixelColor(1, 0), QColor(Qt::red));
        QCOMPARE(frame.copy().size(), QSize(8, 4));
        const QImage small = frame.reduced(2);
        QCOMPARE(small.size(), QSize(4, 2));
    }

    void appStartsWithNoCamera()
    {
        // No pipeline handler matches, so libcamera finds no camera at all
        // (and the tablet's real cameras are left alone).
        qputenv("LIBCAMERA_PIPELINES_MATCH_LIST", "no-such-pipeline");
        QTemporaryDir state;
        QTemporaryDir photos;
        Theme theme(state.path());
        Cameras cameras(photos.path());
        qmlRegisterType<Viewfinder>("Camera", 1, 0, "Viewfinder");
        qmlRegisterUncreatableType<Cameras>("Camera", 1, 0, "Cameras", "the app's own");
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("cameras", &cameras);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.load(QUrl("qrc:/Main.qml"));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        cameras.start();
        QCOMPARE(cameras.state(), QString("none"));
        auto *message = window->findChild<QQuickItem *>("message");
        QVERIFY(message);
        QCOMPARE(message->property("text").toString(), QString("No camera found"));
        auto *shutter = window->findChild<QQuickItem *>("shutter");
        QVERIFY(shutter);
        QVERIFY(!shutter->property("ready").toBool());
        QVERIFY(!cameras.canSwitch());
        // With nothing to stream, taking a photo says so instead of saving.
        QSignalSpy failed(&cameras, &Cameras::photoFailed);
        cameras.takePhoto();
        QCOMPARE(failed.size(), 1);
        QVERIFY(QDir(photos.path()).isEmpty());
        qunsetenv("LIBCAMERA_PIPELINES_MATCH_LIST");
    }
};

QTEST_MAIN(CameraTests)
#include "camera_tests.moc"
