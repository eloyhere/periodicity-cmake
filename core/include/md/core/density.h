// Periodicity — density (framework.md §5.1).
//
// Density converts the design baseline's density-independent units (dp) and
// scalable points (sp) into the device-independent pixels that Qt geometry uses.
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

    // The scale the conversions actually apply: the configured `scale` when it
    // is finite and positive, and 1.0 otherwise. A zoom that animates through
    // zero, a negative scale or a NaN would otherwise collapse or invert every
    // measurement, and dp()/pxToDp() would stop being inverses. Keeping this
    // rule in one place makes all three conversions agree.
    //
    // The finite test is written without qIsFinite() so the member stays
    // constexpr: `scale - scale` is zero only for finite values, because NaN
    // fails the comparison and infinity minus infinity is NaN.
    [[nodiscard]] constexpr qreal effectiveScale() const noexcept
    {
        return scale > 0.0 && scale - scale == 0.0 ? scale : 1.0;
    }

    // Converts a dp value into device-independent pixels. A scale that is not
    // finite and positive is treated as 1.0 (see effectiveScale()).
    [[nodiscard]] constexpr qreal dp(qreal value) const noexcept
    {
        return value * effectiveScale();
    }

    // Converts an sp value into device-independent pixels. Because sp is
    // dp scaled by the user's font preference, both factors apply.
    [[nodiscard]] constexpr qreal sp(qreal value) const noexcept
    {
        return dp(value) * fontScale;
    }

    // Converts device-independent pixels back into dp. It uses the same
    // effectiveScale() as dp(), so `pxToDp(dp(x)) == x` holds even when the
    // configured scale is unusable and the conversion never divides by zero.
    [[nodiscard]] constexpr qreal pxToDp(qreal value) const noexcept
    {
        return value / effectiveScale();
    }

    [[nodiscard]] friend constexpr bool operator==(const Density& lhs,
                                                   const Density& rhs) noexcept = default;
};

} // namespace md
