#include "fingers.h"

#include <QtMath>
#include <algorithm>

namespace {

qreal length(const QPointF &p)
{
    return std::hypot(p.x(), p.y());
}

// Degrees between a movement and a direction (a unit vector).
qreal degreesOff(const QPointF &moved, const QPointF &direction)
{
    const qreal len = length(moved);
    if (len <= 0)
        return 180;
    const qreal cos = (moved.x() * direction.x() + moved.y() * direction.y()) / len;
    return qRadiansToDegrees(std::acos(std::clamp(cos, -1.0, 1.0)));
}

} // namespace

QList<Fingers::Event> Fingers::down(int slot, const QPointF &at, qreal ms)
{
    m_fingers.insert(slot, Finger{at, at, ms});
    if (m_state != State::Forming)
        return {};

    if (m_members.isEmpty()) {
        QList<int> together;
        for (auto it = m_fingers.cbegin(); it != m_fingers.cend(); ++it)
            if (ms - it->landed <= TogetherMs)
                together << it.key();
        if (together.size() < 2)
            return {};
        std::sort(together.begin(), together.end(),
                  [this](int a, int b) { return m_fingers[a].landed < m_fingers[b].landed; });
        m_members = together.mid(0, 3);
        return {Event{Event::HandDown, m_fingers[m_members.first()].start, ms, int(m_members.size())}};
    }

    // A late third finger still joins, as long as the hand hasn't done anything yet.
    if (m_members.size() < 3 && ms - m_fingers[m_members.first()].landed <= TogetherMs)
        m_members << slot;
    return {};
}

QList<Fingers::Event> Fingers::move(int slot, const QPointF &at, qreal ms)
{
    auto it = m_fingers.find(slot);
    if (it == m_fingers.end())
        return {};
    it->now = at;
    if (!m_members.contains(slot) || m_state == State::Done)
        return {};

    if (!together()) {
        const bool pulling = m_state == State::Pulling;
        m_state = State::Done;
        if (pulling)
            return {Event{Event::PullCanceled, pullPoint(), ms, int(m_members.size())}};
        return {};
    }
    if (m_state == State::Pulling)
        return {Event{Event::PullMoved, pullPoint(), ms, int(m_members.size())}};

    const QPointF t = travel();
    const bool everyFingerDown = std::all_of(m_members.cbegin(), m_members.cend(), [&](int s) {
        const Finger &f = m_fingers[s];
        return f.now.y() - f.start.y() >= t.y() / 2;
    });
    if (length(t) >= PullDistance && t.y() > 0 && degreesOff(t, {0, 1}) <= PullDegrees && everyFingerDown) {
        m_state = State::Pulling;
        return {Event{Event::PullStarted, pullPoint(), ms, int(m_members.size())}};
    }
    return {};
}

QList<Fingers::Event> Fingers::up(int slot, qreal ms)
{
    QList<Event> events;
    if (m_members.contains(slot) && m_fingers.contains(slot)) {
        if (m_state == State::Pulling) {
            events << Event{Event::PullEnded, pullPoint(), ms, int(m_members.size())};
        } else if (m_state == State::Forming && m_members.size() == 3) {
            const QPointF t = travel();
            static const QList<QPair<QString, QPointF>> directions{
                {"left", {-1, 0}}, {"right", {1, 0}}, {"up", {0, -1}}};
            if (length(t) >= SwipeDistance) {
                for (const auto &[name, direction] : directions)
                    if (degreesOff(t, direction) <= SwipeDegrees)
                        events << Event{Event::Swiped, lead(), ms, 3, name};
            }
        }
        m_state = State::Done;
    }
    m_fingers.remove(slot);

    if (!m_members.isEmpty()
        && std::none_of(m_members.cbegin(), m_members.cend(), [this](int s) { return m_fingers.contains(s); })) {
        events << Event{Event::HandUp, {}, ms, int(m_members.size())};
        m_members.clear();
        m_state = State::Forming;
    }
    return events;
}

void Fingers::reset()
{
    m_fingers.clear();
    m_members.clear();
    m_state = State::Forming;
}

// How far the hand has come: the mean of its fingers' movements.
QPointF Fingers::travel() const
{
    QPointF sum;
    for (int slot : m_members) {
        const Finger &f = m_fingers[slot];
        sum += f.now - f.start;
    }
    return sum / qreal(m_members.size());
}

// Every finger moves with the hand. Fingers that part, as in a pinch or a
// spread, aren't a pull even when their middle drifts down.
bool Fingers::together() const
{
    const QPointF t = travel();
    const qreal slack = std::max<qreal>(40, length(t) / 2);
    for (int slot : m_members) {
        const Finger &f = m_fingers[slot];
        if (length(f.now - f.start - t) > slack)
            return false;
    }
    return true;
}

QPointF Fingers::pullPoint() const
{
    return m_fingers[m_members.first()].start + travel();
}

// Hyprland's touch matrices for the four turns (its MATRICES in InputManager.hpp).
QPointF Fingers::map(const QPointF &panel, int transform, const QSizeF &logical)
{
    const qreal x = panel.x(), y = panel.y();
    QPointF u;
    switch (transform & 3) {
    case 1: u = {1 - y, x}; break;
    case 2: u = {1 - x, 1 - y}; break;
    case 3: u = {y, 1 - x}; break;
    default: u = {x, y}; break;
    }
    return {u.x() * logical.width(), u.y() * logical.height()};
}

QPointF Fingers::unmap(const QPointF &at, int transform, const QSizeF &logical)
{
    const qreal u = at.x() / logical.width(), v = at.y() / logical.height();
    switch (transform & 3) {
    case 1: return {v, 1 - u};
    case 2: return {1 - u, 1 - v};
    case 3: return {1 - v, u};
    default: return {u, v};
    }
}
