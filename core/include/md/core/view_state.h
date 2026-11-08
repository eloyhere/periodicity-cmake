// Periodicity — interaction state (framework.md §6.4).
//
// ViewStates is a bitset of the interaction states a component can be in.
// State transitions are driven by Qt events in View and are the single source
// of state-layer changes; components never invent their own flags.

#pragma once

#include <QFlags>
#include <QtGlobal>

namespace md {

enum class ViewState : quint32 {
    Enabled = 1u << 0,
    Hovered = 1u << 1,
    Focused = 1u << 2,
    Pressed = 1u << 3,
    Dragged = 1u << 4,
    Selected = 1u << 5,
    Checked = 1u << 6,
    Invalid = 1u << 7,
    ReadOnly = 1u << 8,
};

Q_DECLARE_FLAGS(ViewStates, ViewState)
Q_DECLARE_OPERATORS_FOR_FLAGS(ViewStates)

} // namespace md
