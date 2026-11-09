// Periodicity — the resolved surface a layer paints into (framework.md §5.5).
//
// SurfaceContext is the value a component hands to a layer stack at paint time.
// It carries everything the layers need — the theme, the interaction state, the
// shape, the elevation and the two resolved colours — so that a layer never
// has to resolve a token or reach into a widget.
//
// LIFETIME RULE. `theme` is a non-owning reference. The caller guarantees that
// the referenced Theme outlives every paint call that uses this context, which
// is automatic when the context is built as a local from
// Theme::forWidget(widget) or from an application theme. A context must not be
// stored in a widget or in a layer: it is a transient descriptor of one paint
// pass, and a ThemeScope may have been destroyed by the next one. Because the
// reference binds at construction, a SurfaceContext is copy-constructible but
// deliberately not copy-assignable; build a new value when the surface
// changes.
//
// An empty (invalid) containerColour or contentColour means "derive it":
//   container -> theme.colours().surfaceAtElevation(elevation)
//   content   -> theme.colours().onSurface
// A valid colour, including a transparent one, is used verbatim — that is how
// a component expresses a custom container or a disabled content role without
// the layer second-guessing it.

#pragma once

#include <md/core/view_state.h>
#include <md/tokens/elevation.h>
#include <md/tokens/shape.h>
#include <md/theme/theme.h>

#include <QColor>

namespace md::layer {

struct SurfaceContext
{
    // Non-owning; see the lifetime rule above.
    const Theme& theme;

    // Interaction state, including the Enabled bit. A disabled component
    // clears Enabled, which suppresses the interaction overlay.
    ViewStates state = ViewState::Enabled;

    // The resolved outline of the surface.
    Shape shape;

    // The tonal height of the surface.
    Elevation elevation = Elevation::Level0;

    // The container fill. Empty = derive from `elevation`.
    QColor containerColour;

    // The colour interaction overlays and content are made of. Empty = the
    // theme's onSurface.
    QColor contentColour;

    // The container fill the background layer paints: containerColour when it
    // is set, otherwise the tonal surface for `elevation`.
    [[nodiscard]] QColor resolvedContainerColour() const;

    // The content colour interaction overlays are made of: contentColour when
    // it is set, otherwise the theme's onSurface.
    [[nodiscard]] QColor resolvedContentColour() const;
};

} // namespace md::layer
