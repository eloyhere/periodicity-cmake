// Periodicity — state-layer tokens (framework.md §3.6).
//
// Interaction feedback is a translucent overlay of the content colour at a
// fixed opacity. These are state-layer opacities, not component colours: a
// widget never hard-codes a grey for hover; it composites the layer over its
// own container colour.

#pragma once

#include <md/core/view_state.h>

#include <QColor>
#include <QtGlobal>

namespace md {

// Opacity of the interaction layer for `state`.
//
// The four interaction flags are independent bits, so a combined value is
// resolved here: when more than one is set the strongest overlay wins. The
// result is always one of the four token values or zero, never a sum, because
// stacking two overlays would silently invent a fifth opacity.
[[nodiscard]] constexpr qreal stateLayerOpacity(ViewState state) noexcept
{
    const auto bits = static_cast<quint32>(state);

    if ((bits & static_cast<quint32>(ViewState::Dragged)) != 0u)
        return 0.16;

    if ((bits & (static_cast<quint32>(ViewState::Focused)
                 | static_cast<quint32>(ViewState::Pressed)))
        != 0u) {
        return 0.12;
    }

    if ((bits & static_cast<quint32>(ViewState::Hovered)) != 0u)
        return 0.08;

    return 0.0;
}

// Opacity applied to content (text, icons) of a disabled component.
inline constexpr qreal kDisabledContentOpacity = 0.38;

// Opacity applied to the container fill of a disabled component.
inline constexpr qreal kDisabledContainerOpacity = 0.12;

// `content` with its alpha multiplied by the state-layer opacity for `state`.
//
// The RGB channels are untouched: the layer is the content colour at a lower
// alpha, so compositing it over the container yields exactly the M3 overlay.
// A zero-opacity state produces a fully transparent colour.
[[nodiscard]] QColor applyStateLayer(const QColor& content, ViewState state);

} // namespace md
