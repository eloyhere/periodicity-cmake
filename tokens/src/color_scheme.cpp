// Periodicity — the colour scheme (framework.md §3.1, §5.2).
//
// The baseline tables are the canonical palette from the upstream reference
// documentation, quoted as 8-bit sRGB and always opaque. Dynamic colour is out
// of scope for the baseline, so these two tables are the whole colour system.

#include <md/tokens/color_scheme.h>

#include <md/tokens/state_layer.h>

namespace md {

namespace {

// Baseline colours are quoted as RGB triples; the alpha is always solid.
QColor rgb(quint32 hex) noexcept
{
    return QColor(static_cast<int>((hex >> 16) & 0xFFu),
                  static_cast<int>((hex >> 8) & 0xFFu),
                  static_cast<int>(hex & 0xFFu));
}

} // namespace

QColor ColorScheme::surfaceAtElevation(Elevation level) const
{
    // Tonal elevation is a lookup in the neutral ramp, not an overlay: a
    // caller must get the same colour from a theme snapshot on every platform.
    switch (level) {
    case Elevation::Level0:
        return surface;
    case Elevation::Level1:
        return surfaceContainerLow;
    case Elevation::Level2:
        return surfaceContainer;
    case Elevation::Level3:
    case Elevation::Level4:
        // The baseline mapping deliberately plateaus at level 4.
        return surfaceContainerHigh;
    case Elevation::Level5:
        return surfaceContainerHighest;
    }
    return surface;
}

QColor ColorScheme::stateLayer(const QColor& content, ViewState state) const
{
    return applyStateLayer(content, state);
}

ColorScheme ColorScheme::baselineLight()
{
    ColorScheme scheme;

    scheme.primary = rgb(0x6750A4u);
    scheme.onPrimary = rgb(0xFFFFFFu);
    scheme.primaryContainer = rgb(0xEADDFFu);
    scheme.onPrimaryContainer = rgb(0x21005Du);

    scheme.secondary = rgb(0x625B71u);
    scheme.onSecondary = rgb(0xFFFFFFu);
    scheme.secondaryContainer = rgb(0xE8DEF8u);
    scheme.onSecondaryContainer = rgb(0x1D192Bu);

    scheme.tertiary = rgb(0x7D5260u);
    scheme.onTertiary = rgb(0xFFFFFFu);
    scheme.tertiaryContainer = rgb(0xFFD8E4u);
    scheme.onTertiaryContainer = rgb(0x31111Du);

    scheme.error = rgb(0xB3261Eu);
    scheme.onError = rgb(0xFFFFFFu);
    scheme.errorContainer = rgb(0xF9DEDCu);
    scheme.onErrorContainer = rgb(0x410E0Bu);

    scheme.surface = rgb(0xFEF7FFu);
    scheme.onSurface = rgb(0x1D1B20u);
    scheme.surfaceVariant = rgb(0xE7E0ECu);
    scheme.onSurfaceVariant = rgb(0x49454Fu);

    scheme.surfaceDim = rgb(0xDED8E1u);
    scheme.surfaceBright = rgb(0xFEF7FFu);
    scheme.surfaceContainerLowest = rgb(0xFFFFFFu);
    scheme.surfaceContainerLow = rgb(0xF7F2FAu);
    scheme.surfaceContainer = rgb(0xF3EDF7u);
    scheme.surfaceContainerHigh = rgb(0xECE6F0u);
    scheme.surfaceContainerHighest = rgb(0xE6E0E9u);

    scheme.outline = rgb(0x79747Eu);
    scheme.outlineVariant = rgb(0xCAC4D0u);
    scheme.shadow = rgb(0x000000u);
    scheme.scrim = rgb(0x000000u);
    scheme.surfaceTint = rgb(0x6750A4u);

    scheme.inverseSurface = rgb(0x322F35u);
    scheme.inverseOnSurface = rgb(0xF5EFF7u);
    scheme.inversePrimary = rgb(0xD0BCFFu);

    scheme.primaryFixed = rgb(0xEADDFFu);
    scheme.primaryFixedDim = rgb(0xD0BCFFu);
    scheme.onPrimaryFixed = rgb(0x21005Du);
    scheme.onPrimaryFixedVariant = rgb(0x4F378Bu);

    scheme.secondaryFixed = rgb(0xE8DEF8u);
    scheme.secondaryFixedDim = rgb(0xCCC2DCu);
    scheme.onSecondaryFixed = rgb(0x1D192Bu);
    scheme.onSecondaryFixedVariant = rgb(0x4A4458u);

    scheme.tertiaryFixed = rgb(0xFFD8E4u);
    scheme.tertiaryFixedDim = rgb(0xEFB8C8u);
    scheme.onTertiaryFixed = rgb(0x31111Du);
    scheme.onTertiaryFixedVariant = rgb(0x633B48u);

    return scheme;
}

ColorScheme ColorScheme::baselineDark()
{
    ColorScheme scheme;

    scheme.primary = rgb(0xD0BCFFu);
    scheme.onPrimary = rgb(0x381E72u);
    scheme.primaryContainer = rgb(0x4F378Bu);
    scheme.onPrimaryContainer = rgb(0xEADDFFu);

    scheme.secondary = rgb(0xCCC2DCu);
    scheme.onSecondary = rgb(0x332D41u);
    scheme.secondaryContainer = rgb(0x4A4458u);
    scheme.onSecondaryContainer = rgb(0xE8DEF8u);

    scheme.tertiary = rgb(0xEFB8C8u);
    scheme.onTertiary = rgb(0x492532u);
    scheme.tertiaryContainer = rgb(0x633B48u);
    scheme.onTertiaryContainer = rgb(0xFFD8E4u);

    scheme.error = rgb(0xF2B8B5u);
    scheme.onError = rgb(0x601410u);
    scheme.errorContainer = rgb(0x8C1D18u);
    scheme.onErrorContainer = rgb(0xF9DEDCu);

    scheme.surface = rgb(0x141218u);
    scheme.onSurface = rgb(0xE6E0E9u);
    scheme.surfaceVariant = rgb(0x49454Fu);
    scheme.onSurfaceVariant = rgb(0xCAC4D0u);

    // The dark neutral ramp runs the other way: `surfaceDim` is the darkest
    // step and `surfaceBright` the lightest, so the names stay relative.
    scheme.surfaceDim = rgb(0x141218u);
    scheme.surfaceBright = rgb(0x3B383Eu);
    scheme.surfaceContainerLowest = rgb(0x0F0D13u);
    scheme.surfaceContainerLow = rgb(0x1D1B20u);
    scheme.surfaceContainer = rgb(0x211F26u);
    scheme.surfaceContainerHigh = rgb(0x2B2930u);
    scheme.surfaceContainerHighest = rgb(0x36343Bu);

    scheme.outline = rgb(0x938F99u);
    scheme.outlineVariant = rgb(0x49454Fu);
    scheme.shadow = rgb(0x000000u);
    scheme.scrim = rgb(0x000000u);
    scheme.surfaceTint = rgb(0xD0BCFFu);

    scheme.inverseSurface = rgb(0xE6E0E9u);
    scheme.inverseOnSurface = rgb(0x322F35u);
    scheme.inversePrimary = rgb(0x6750A4u);

    // The fixed roles are identical in both schemes by definition.
    scheme.primaryFixed = rgb(0xEADDFFu);
    scheme.primaryFixedDim = rgb(0xD0BCFFu);
    scheme.onPrimaryFixed = rgb(0x21005Du);
    scheme.onPrimaryFixedVariant = rgb(0x4F378Bu);

    scheme.secondaryFixed = rgb(0xE8DEF8u);
    scheme.secondaryFixedDim = rgb(0xCCC2DCu);
    scheme.onSecondaryFixed = rgb(0x1D192Bu);
    scheme.onSecondaryFixedVariant = rgb(0x4A4458u);

    scheme.tertiaryFixed = rgb(0xFFD8E4u);
    scheme.tertiaryFixedDim = rgb(0xEFB8C8u);
    scheme.onTertiaryFixed = rgb(0x31111Du);
    scheme.onTertiaryFixedVariant = rgb(0x633B48u);

    return scheme;
}

} // namespace md
