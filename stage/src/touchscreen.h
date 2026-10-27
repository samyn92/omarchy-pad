#pragma once

#include <QObject>
#include <QPointF>

class QSocketNotifier;
struct libinput;

// Every finger on the touch screen, straight from the device, so stage sees a
// hand even where the overlay doesn't take touches. Only the devices udev
// tags as touch screens are opened, never keyboards; nothing is grabbed, so
// Hyprland and the apps get the same touches as before (it needs the user in
// the input group). Positions are fractions of the panel's
// native width and height, before any rotation.
class Touchscreen : public QObject {
    Q_OBJECT

public:
    enum Kind { Down, Move, Up };
    Q_ENUM(Kind)

    explicit Touchscreen(QObject *parent = nullptr);
    ~Touchscreen() override;

    // False when there is no touch screen, or it can't be read.
    bool open();

signals:
    void touched(Touchscreen::Kind kind, int slot, QPointF panel, qreal ms);

private:
    void read();

    libinput *m_libinput = nullptr;
    QSocketNotifier *m_notifier = nullptr;
};
