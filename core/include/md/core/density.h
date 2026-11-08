// Periodicity — density (framework.md §5.1).
//
// Density converts Material's density-independent units (dp) and scalable
// points (sp) into the device-independent pixels that Qt geometry uses.
//
// At scale 1.0 one dp is exactly one device-independent pixel. Qt already
// applies the operating system's display scaling to widget geometry, so
// Density must *not* multiply by logicalDotsPerInch() / 96 — doing so would
// double-scale every dimension. The scale member exists for an explicit
// application-level zoom, and fontScale for an accessibility text multiplier;
// neither is a substitute for the DPI handling Qt performs itself.

#pragma once

#include <QtGlobal>

namespace md {

struct Density
{
    // Application-level zoom. 1.0 means "one dp is one DIP".
    qreal scale = 1.0;

    // Additional text multiplier applied on top of `scale` (accessibility
    // font scaling), so sp tracks both settings.
    qreal fontScale = 1.0;

    // Converts a dp value into device-independent pixels.
    [[nodiscard]] constexpr qreal dp(qreal value) const noexcept
    {
        return value * scale;
    }

    // Converts an sp value into device-independent pixels. Because sp is
    // dp scaled by the user's font preference, both factors apply.
    [[nodiscard]] constexpr qreal sp(qreal value) const noexcept
    {
        return dp(value) * fontScale;
    }

    // Converts device-independent pixels back into dp. A non-positive scale
    // is treated as 1.0 so that the conversion never divides by zero.
    [[nodiscard]] constexpr qreal pxToDp(qreal value) const noexcept
    {
        return scale > 0.0 ? value / scale : value;
    }

    [[nodiscard]] friend constexpr bool operator==(const Density& lhs,
                                                   const Density& rhs) noexcept = default;
};

} // namespace md
