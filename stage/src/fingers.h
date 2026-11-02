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
//  - two or three fingers moving down together pull a window down, and two
//    moving up push (the scratchpad) up;
//  - three fingers moving sideways together slide the workspaces;
//  - three fingers up swipe, once the first of them lifts;
//  - three fingers pinching in or spreading out copy and paste, as on an iPad;
//  - two fingers held still lift a window (stage times the hold and calls
//    hold()), which then follows the hand, on one finger too once lifted.
// A hand ends at its first lift, so the last finger's jump can't move a pull;
// a held one goes on with the fingers it has left, until the last lifts.
class Fingers {
public:
    struct Event {
        enum Type {
            HandDown,      // a hand formed (again when a third finger joins); `at` is its first finger
            PullStarted,   // it moves down together (`direction` "down"), or two fingers
                           // up ("up"); `at` is where the hand brought its first finger
            PullMoved,
            PullEnded,     // a finger lifted
            PullCanceled,  // the fingers went apart, a pinch rather than a pull
            SlideStarted,  // three fingers move sideways together; `direction` "left" or "right"
            SlideMoved,
            SlideEnded,
            SlideCanceled,
            Swiped,        // three fingers, `direction` "up"
            Pinched,       // three fingers came together ("in") or spread ("out")
            GrabMoved,     // a hand that was held still (hold()) moves; `at` as for a pull
            GrabEnded,     // and its last finger lifted
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
    // A pinch: the fingers' spread (their mean distance from their middle)
    // shrinks to this share of what it was, or grows by this factor, and by
    // at least this far.
    static constexpr qreal PinchRatio = 0.55;
    static constexpr qreal SpreadRatio = 1.7;
    static constexpr qreal PinchDistance = 60;

    QList<Event> down(int slot, const QPointF &at, qreal ms);
    QList<Event> move(int slot, const QPointF &at, qreal ms);
    QList<Event> up(int slot, qreal ms);
    void reset();

    bool hand() const { return !m_members.isEmpty(); }
    int fingers() const { return int(m_members.size()); }
    // A hand that has done nothing yet and moved no more than `slack`.
    bool resting(qreal slack) const;
    // Holds that hand (it was still for long enough): from now on it moves
    // what it holds, rather than pulling, sliding or pinching.
    bool hold();
    // Where the hand is, as for a pull: its first finger as it landed, moved
    // by as much as the hand has.
    QPointF point() const { return m_state == State::Holding ? carryPoint() : pullPoint(); }

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
    enum class State { Forming, Pulling, Sliding, Holding, Done };

    QPointF travel() const;
    bool together() const;
    QPointF lead() const { return m_fingers.value(m_members.first()).now; }
    QPointF pullPoint() const;
    QPointF middle() const;
    // Where a held hand has what it holds: where it was held, moved as much
    // as the fingers still down have moved since (the last lift).
    QPointF carryPoint() const;
    qreal spread(bool atStart) const;

    bool everyFingerAlong(const QPointF &t) const;

    QHash<int, Finger> m_fingers;   // every finger down, by slot
    QList<int> m_members;           // the hand's fingers, first one first
    State m_state = State::Forming;
    QString m_direction;            // which way a slide goes
    bool m_parted = false;          // the fingers went apart: only a pinch is left
    QPointF m_carryAt, m_carryFrom;
};
