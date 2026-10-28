#pragma once

#include <QHash>
#include <QObject>
#include <QPointF>

class QSocketNotifier;
struct libinput;

// Taps on the touch screen, straight from the device, so the keyboard can
// tell a field the user tapped from one that took focus by itself (a window
// that just opened). Only the devices udev tags as touch screens are opened,
// never keyboards; nothing is grabbed, so Hyprland and the apps get the same
// touches as before (it needs the user in the input group). Positions are
// fractions of the panel's native width and height, before any rotation.
// The same reading as stage's Touchscreen.
class Taps : public QObject {
    Q_OBJECT

public:
    explicit Taps(QObject *parent = nullptr);
    ~Taps() override;

    // False when there is no touch screen, or it can't be read.
    bool open();

    enum Kind { Down, Move, Up };
    // One finger's touch, as the device reports it; public for the tests.
    void feed(Kind kind, int slot, const QPointF &panel);

    // How far a finger may wander, as a fraction of the panel, and still
    // count as a tap rather than a swipe.
    static constexpr qreal Slop = 0.012;

signals:
    // A finger came down on an empty screen.
    void started();
    // The last finger lifted. A tap is one finger that hardly moved; `panel`
    // is where it came down.
    void ended(bool tap, QPointF panel);

private:
    void read();

    libinput *m_libinput = nullptr;
    QSocketNotifier *m_notifier = nullptr;
    QHash<int, QPointF> m_down;
    QPointF m_first;
    int m_fingers = 0;
    bool m_tap = false;
};
