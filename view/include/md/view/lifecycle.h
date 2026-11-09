// Periodicity — component lifecycle (docs/backlog.md "Component lifecycle").
//
// Every widget moves through the same states, so components do not invent their
// own "initialised" flags. The state is observable and the transitions are
// driven by the base class:
//
//   Created -> Attached -> Shown <-> Hidden -> Detached
//
// A component overrides the hooks, not the transitions.

#pragma once

#include <QtGlobal>

namespace md {

enum class LifecycleState : quint8 {
    Created,  // Constructed; it has never been attached to a parent
    Attached, // Has a parent, but has not been shown yet
    Shown,    // Visible: animations and timers are allowed
    Hidden,   // Was shown and is now hidden: animations must be stopped
    Detached, // Removed from its parent; caches must be released
};

} // namespace md
