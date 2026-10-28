#pragma once

#include <QHash>
#include <QList>
#include <QPointF>
#include <QSizeF>
#include <QString>

// Two and three fingers that land together on the touch screen: a "hand".
// One finger stays with the edge gestures, which come through the overlay's
// own touch input; this sees every finger, straight from the touch screen,
// and only speaks up for a hand. Fed in the overlay's logical coordinates and
// milliseconds, it says what the hand does:
//  - two or three fingers moving down together pull a window down;
//  - three fingers moving sideways together slide the workspaces;
//  - three fingers up swipe, once the first of them lifts.
// A hand ends at its first lift, so the last finger's jump can't move a pull.
class Fingers {
public:
    struct Event {
        enum Type {
            HandDown,      // a hand formed (again when a third finger joins); `at` is its first finger
            PullStarted,   // it moves down together; `at` is where the hand brought its first finger
            PullMoved,
            PullEnded,     // a finger lifted
            PullCanceled,  // the fingers went apart, a pinch rather than a pull
            SlideStarted,  // three fingers move sideways together; `direction` "left" or "right"
            SlideMoved,
            SlideEnded,
            SlideCanceled,
            Swiped,        // three fingers, `direction` "up"
            HandUp,        // every finger of the hand has lifted
        };
        Type type;
        QPointF at;
        qreal ms = 0;
        int fingers = 0;
        QString direction = {};
    };

    // Fingers landing this close together make a hand; one already resting
    // longer than that (a thumb holding the tablet) is not part of it.
    static constexpr qreal TogetherMs = 150;
    // A pull counts once the hand has come this far, this close to straight down.
    static constexpr qreal PullDistance = 24;
    static constexpr qreal PullDegrees = 30;
    // A slide counts as soon as a pull would, this close to level.
    static constexpr qreal SlideDegrees = 30;
    // A three-finger swipe up needs this distance, this close to straight up.
    static constexpr qreal SwipeDistance = 150;
    static constexpr qreal SwipeDegrees = 22;

    QList<Event> down(int slot, const QPointF &at, qreal ms);
    QList<Event> move(int slot, const QPointF &at, qreal ms);
    QList<Event> up(int slot, qreal ms);
    void reset();

    bool hand() const { return !m_members.isEmpty(); }

    // A point on the panel, as fractions of its native width and height, in
    // the overlay's logical coordinates once the output is turned by
    // `transform` (0-3, Hyprland's), the way Hyprland maps the touch screen.
    static QPointF map(const QPointF &panel, int transform, const QSizeF &logical);
    static QPointF unmap(const QPointF &at, int transform, const QSizeF &logical);

private:
    struct Finger {
        QPointF start;
        QPointF now;
        qreal landed = 0;
    };
    enum class State { Forming, Pulling, Sliding, Done };

    QPointF travel() const;
    bool together() const;
    QPointF lead() const { return m_fingers.value(m_members.first()).now; }
    QPointF pullPoint() const;

    bool everyFingerAlong(const QPointF &t) const;

    QHash<int, Finger> m_fingers;   // every finger down, by slot
    QList<int> m_members;           // the hand's fingers, first one first
    State m_state = State::Forming;
    QString m_direction;            // which way a slide goes
};
