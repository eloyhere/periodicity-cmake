// Periodicity — event target contract (framework.md §5.10).
//
// Anything that can receive a dispatched event implements EventTarget. The
// two graph queries let the dispatcher build the chain without knowing the
// visual tree: parentTarget() names the next node towards the root and
// mapPointFrom() converts event positions between nodes.
//
// Lifetime. A handler may destroy a target — its own or an ancestor's — while
// a dispatch is running. EventTarget is deliberately not a QObject (View
// derives from both QWidget and EventTarget), so QPointer cannot be used on
// this hierarchy. Every target instead carries a shared liveness token: it is
// the QPointer equivalent for this interface. The dispatcher copies the token
// into its chain and asks it whether the target is still alive before the next
// delivery, without ever dereferencing a destroyed target. EventTarget
// therefore owns one small heap allocation for its lifetime and is
// non-copyable: two targets must not share one liveness token.
//
// The token is read with plain loads, so creating, dispatching and destroying
// targets belongs to one thread (the GUI thread).

#pragma once

#include <QPointF>

#include <memory>

namespace md {

namespace detail {

// The shared flag behind EventTarget's QPointer-like guard. It outlives the
// target so a dispatch can tell "destroyed" from "still alive" without
// touching the freed object. EventTarget invalidates it in its destructor; the
// dispatcher only reads it.
class TargetLifetime
{
public:
    TargetLifetime() = default;
    TargetLifetime(const TargetLifetime&) = delete;
    TargetLifetime& operator=(const TargetLifetime&) = delete;

    [[nodiscard]] bool isAlive() const noexcept { return m_alive; }
    void invalidate() noexcept { m_alive = false; }

private:
    bool m_alive = true;
};

} // namespace detail

class EventTarget
{
public:
    EventTarget()
        : m_lifetime(std::make_shared<detail::TargetLifetime>())
    {
    }

    // Targets are addressed by identity and owned elsewhere; a copied base
    // would give two distinct targets one liveness token.
    EventTarget(const EventTarget&) = delete;
    EventTarget& operator=(const EventTarget&) = delete;

    virtual ~EventTarget()
    {
        m_lifetime->invalidate();
    }

    // The next target towards the root, or nullptr for a root. The dispatcher
    // caps the resulting chain, so a malformed graph cannot loop forever.
    [[nodiscard]] virtual EventTarget* parentTarget() const noexcept = 0;

    // Translates `point`, expressed in `source`'s coordinate system, into this
    // target's coordinate system. The dispatcher calls it before delivering a
    // PointerEvent or WheelEvent, always with the original dispatch target as
    // `source`.
    [[nodiscard]] virtual QPointF mapPointFrom(const EventTarget& source,
                                               const QPointF& point) const = 0;

    // Gives an ancestor the chance to claim a gesture before its descendants
    // see it, mirroring the declarative view system's intercept hook. Returning
    // true makes this target the delivery target. The default does not intercept.
    virtual bool onInterceptEvent(Event& /*event*/) { return false; }

    // Handles the event in the current phase. The return value is advisory;
    // acceptance is expressed through Event::accept().
    virtual bool onEvent(Event& event) = 0;

    // Internal: the dispatcher copies this token into its chain so a target
    // destroyed by a handler is detected before the next delivery. Application
    // code must not call this.
    [[nodiscard]] std::shared_ptr<const detail::TargetLifetime> lifetimeToken() const noexcept
    {
        return m_lifetime;
    }

private:
    std::shared_ptr<detail::TargetLifetime> m_lifetime;
};

} // namespace md
