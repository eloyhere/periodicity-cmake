// Periodicity — Event value implementation (framework.md §5.10).

#include <md/event/event.h>

namespace md {

Event::Event(EventType type, std::chrono::milliseconds timestamp)
    : m_type(type)
    , m_timestamp(timestamp)
{
}

Event::~Event() = default;

EventType Event::type() const noexcept
{
    return m_type;
}

EventPhase Event::phase() const noexcept
{
    return m_phase;
}

EventTarget* Event::target() const noexcept
{
    return m_target;
}

std::chrono::milliseconds Event::timestamp() const noexcept
{
    return m_timestamp;
}

void Event::accept() noexcept
{
    m_accepted = true;
}

void Event::ignore() noexcept
{
    m_accepted = false;
}

bool Event::isAccepted() const noexcept
{
    return m_accepted;
}

void Event::stopPropagation() noexcept
{
    m_propagationStopped = true;
}

bool Event::isPropagationStopped() const noexcept
{
    return m_propagationStopped;
}

void Event::setPhase(EventPhase phase) noexcept
{
    m_phase = phase;
}

void Event::setTarget(EventTarget* target) noexcept
{
    m_target = target;
}

void Event::resetForDispatch()
{
    m_phase = EventPhase::Target;
    m_target = nullptr;
    m_accepted = false;
    m_propagationStopped = false;
}

} // namespace md
