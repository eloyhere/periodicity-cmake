// Periodicity — the drawable base class (framework.md §5.4).
//
// The base owns the two pieces of shared bookkeeping: the interaction state and
// the revision counter. Everything else is virtual and belongs to a concrete
// primitive.

#include <md/drawable/drawable.h>

namespace md::drawable {

void Drawable::setState(ViewStates state)
{
    // An idempotent setter: a state transition that produces the same bitset
    // must not invalidate a cached raster or wake a listener.
    if (state == state_)
        return;

    const ViewStates previous = state_;
    state_ = state;

    // The hook runs before the revision moves so an override can still read
    // the previous state if it keeps its own record.
    onStateChanged(previous);

    // A state change is an appearance change (framework.md §5.4 names state
    // changes as an invalidation trigger), so the revision moves even when the
    // owner does not call invalidate() itself.
    invalidate();
}

void Drawable::invalidate()
{
    // Wrapping after 2^64 revisions is not a practical concern: the counter
    // exists to distinguish "before" from "after", not to be globally unique.
    ++revision_;
}

} // namespace md::drawable
