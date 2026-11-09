// Periodicity — internal hash helpers for drawable cache keys.
//
// This header is private to the module (it lives under src/, which is not on
// the public include path) because framework.md §13.2 forbids a public header
// from including a private one. Every public cacheKey() implementation is
// deliberately inline-free and lives in the corresponding .cpp, so the
// combiner never leaks into the installed headers.

#pragma once

#include <md/tokens/shape.h>

#include <QColor>
#include <QtGlobal>

namespace md::drawable::detail {

// A 64-bit mixing function in the style of Boost's hash_combine. It is not a
// cryptographic hash: it only has to keep a cache key stable and free of
// obvious collisions, which it does by stirring every bit of `value` into
// `seed`.
[[nodiscard]] inline quint64 mix(quint64 seed, quint64 value) noexcept
{
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

// The full ARGB value, not just the RGB: a translucent fill must not collide
// with its opaque counterpart.
[[nodiscard]] inline quint64 hashColour(const QColor& colour) noexcept
{
    return static_cast<quint64>(colour.rgba());
}

// The corner radii are floating-point, so they are quantised to 1/16 dp
// before hashing. Sub-pixel radius differences do not alter the raster.
[[nodiscard]] inline quint64 hashShape(const Shape& shape) noexcept
{
    quint64 key = shape.isPill() ? 0x243f6a8885a308d3ULL : 0x13198a2e03707344ULL;

    const CornerRadii radii = shape.radii();
    key = mix(key, static_cast<quint64>(qRound64(radii.topStart * 16.0)));
    key = mix(key, static_cast<quint64>(qRound64(radii.topEnd * 16.0)));
    key = mix(key, static_cast<quint64>(qRound64(radii.bottomEnd * 16.0)));
    key = mix(key, static_cast<quint64>(qRound64(radii.bottomStart * 16.0)));

    return key;
}

// A scalar that is part of the appearance but may be fractional (a stroke
// width, a scale). Quantise once, in one place.
[[nodiscard]] inline quint64 hashScalar(qreal value) noexcept
{
    return static_cast<quint64>(qRound64(value * 1000.0));
}

} // namespace md::drawable::detail
