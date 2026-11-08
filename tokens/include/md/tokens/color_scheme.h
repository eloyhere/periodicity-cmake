// Periodicity — the colour scheme (framework.md §3.1, §5.2).
//
// A ColorScheme is the complete M3 role set: every colour a component may use
// is named here, so a component never invents a tint. Roles come in contrast
// pairs (`X` / `onX`); a component may place `onX` content only on `X`.
//
// The type is an immutable value: copy it, compare it, snapshot it for golden
// tests. Only the baseline light and dark schemes exist — dynamic colour is
// outside the baseline (framework.md §3.1), and the factories are the only
// supported way to obtain a fully populated scheme.

#pragma once

#include <md/core/view_state.h>
#include <md/tokens/elevation.h>

#include <QColor>
#include <QtGlobal>

namespace md {

class ColorScheme
{
public:
    // Primary: the main brand colour, used for the highest-emphasis actions.
    QColor primary;
    QColor onPrimary;
    QColor primaryContainer;
    QColor onPrimaryContainer;

    // Secondary: supporting actions and less prominent surfaces.
    QColor secondary;
    QColor onSecondary;
    QColor secondaryContainer;
    QColor onSecondaryContainer;

    // Tertiary: accent, used to balance primary and secondary.
    QColor tertiary;
    QColor onTertiary;
    QColor tertiaryContainer;
    QColor onTertiaryContainer;

    // Error: destructive and invalid states.
    QColor error;
    QColor onError;
    QColor errorContainer;
    QColor onErrorContainer;

    // Surface: the base layer and its content colours. `surfaceVariant` is
    // retained for compatibility with the baseline role set even though newer
    // Material guidance models it with the surface-container ramp.
    QColor surface;
    QColor onSurface;
    QColor surfaceVariant;
    QColor onSurfaceVariant;

    // The surface-container ramp: hierarchy is encoded as a tonal step, never
    // as an alpha overlay (framework.md §3.1, §7.4).
    QColor surfaceDim;
    QColor surfaceBright;
    QColor surfaceContainerLowest;
    QColor surfaceContainerLow;
    QColor surfaceContainer;
    QColor surfaceContainerHigh;
    QColor surfaceContainerHighest;

    // Outline and utility roles.
    QColor outline;
    QColor outlineVariant;
    QColor shadow;
    QColor scrim;
    QColor surfaceTint;

    // Inverse roles, for snackbars and other surfaces that invert the theme.
    QColor inverseSurface;
    QColor inverseOnSurface;
    QColor inversePrimary;

    // Fixed roles: identical in the light and dark schemes by definition, so a
    // branded element can keep its colour across a theme switch.
    QColor primaryFixed;
    QColor primaryFixedDim;
    QColor onPrimaryFixed;
    QColor onPrimaryFixedVariant;

    QColor secondaryFixed;
    QColor secondaryFixedDim;
    QColor onSecondaryFixed;
    QColor onSecondaryFixedVariant;

    QColor tertiaryFixed;
    QColor tertiaryFixedDim;
    QColor onTertiaryFixed;
    QColor onTertiaryFixedVariant;

    // The tonal surface colour for an elevation level. This is a colour
    // operation on the neutral palette, not an alpha-blended overlay: level 3
    // and level 4 deliberately share `surfaceContainerHigh`, which is the
    // baseline M3 mapping.
    [[nodiscard]] QColor surfaceAtElevation(Elevation level) const;

    // The interaction overlay for `content` in `state`, as a translucent copy
    // of the content colour (framework.md §3.6).
    [[nodiscard]] QColor stateLayer(const QColor& content, ViewState state) const;

    [[nodiscard]] static ColorScheme baselineLight();
    [[nodiscard]] static ColorScheme baselineDark();

    [[nodiscard]] friend bool operator==(const ColorScheme& lhs, const ColorScheme& rhs) = default;
};

} // namespace md
