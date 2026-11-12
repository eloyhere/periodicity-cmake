// Periodicity — elevation tokens (framework.md §3.4).
//
// Six elevation levels, each expressed in dp. The token is deliberately only
// the number: the shadow geometry and the tonal surface colour belong to the
// layers above (the shadow drawable and ColorScheme::surfaceAtElevation),
// because a token must not carry painting policy.

#pragma once

#include <QtGlobal>

namespace md {

enum class Elevation : quint8 {
    Level0 = 0,
    Level1 = 1,
    Level2 = 2,
    Level3 = 3,
    Level4 = 4,
    Level5 = 5,
};

// Height of `level` above the surface, in dp.
//
// The ramp is the canonical 0, 1, 3, 6, 8, 12 elevation ramp rather than a
// linear scale, because perceived depth grows more slowly than the physical
// offset. The values are the single source of truth for shadow offsets and
// for the tonal surface mapping in ColorScheme.
[[nodiscard]] constexpr qreal elevationDp(Elevation level) noexcept
{
    switch (level) {
    case Elevation::Level0:
        return 0.0;
    case Elevation::Level1:
        return 1.0;
    case Elevation::Level2:
        return 3.0;
    case Elevation::Level3:
        return 6.0;
    case Elevation::Level4:
        return 8.0;
    case Elevation::Level5:
        return 12.0;
    }
    return 0.0;
}

} // namespace md
