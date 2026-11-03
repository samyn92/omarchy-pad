#pragma once

#include <QLineF>
#include <QPointF>

// One finger held still: what a long press does with the fingers, apart from what it clicks (Stage). It starts when
// a finger lands alone, outside stage's own strips; another finger, or the finger moving more than Slack, calls it
// off; once it has rested long enough (arm(), from Stage's timer) it is armed, and the armed finger's lift is the
// click. A lift before that calls it off.
class LongPress {
public:
    enum Result { Nothing, Started, Canceled, Click };
    static constexpr qreal Slack = 12;

    // A finger landed; `alone` when no other finger is down, `onStrip` when it is on stage's own strips.
    Result down(int slot, const QPointF &at, bool alone, bool onStrip)
    {
        const bool wasOn = on();
        m_slot = -1;
        m_armed = false;
        if (!alone || onStrip)
            return wasOn ? Canceled : Nothing;
        m_slot = slot;
        m_at = at;
        return Started;
    }
    Result move(int slot, const QPointF &at)
    {
        if (slot != m_slot || QLineF(at, m_at).length() <= Slack)
            return Nothing;
        cancel();
        return Canceled;
    }
    Result up(int slot)
    {
        if (slot != m_slot)
            return Nothing;
        const bool armed = m_armed;
        cancel();
        return armed ? Click : Canceled;
    }
    // It has rested long enough: armed, true if it is still there.
    bool arm()
    {
        if (m_slot < 0)
            return false;
        m_armed = true;
        return true;
    }
    void cancel()
    {
        m_slot = -1;
        m_armed = false;
    }

    bool on() const { return m_slot >= 0; }
    bool armed() const { return m_armed; }
    QPointF at() const { return m_at; }

private:
    int m_slot = -1;
    QPointF m_at;
    bool m_armed = false;
};
