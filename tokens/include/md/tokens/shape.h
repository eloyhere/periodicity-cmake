// Periodicity — shape tokens (framework.md §3.3).
//
// A Shape is a path factory: it carries per-corner radii, or a pill flag that
// resolves to a semicircular end cap once the bounds are known. Keeping the
// factory in a value type means a component never hard-codes a corner radius,
// and a custom outline is substituted in one place rather than at every call
// site.

#pragma once

#include <md/core/geometry.h>

#include <QPainterPath>
#include <QRectF>
#include <QtGlobal>

namespace md {

class Shape
{
public:
    // A plain rectangle: no rounding and no cap.
    constexpr Shape() noexcept = default;

    constexpr Shape(CornerRadii radii, bool pill) noexcept
        : radii_(radii)
        , pill_(pill)
    {
    }

    // The named M3 shape tokens, with the baseline radii from ShapeTokens.kt.
    // `extraExtraLarge` covers the largest surfaces and is not used by any
    // baseline component, but it is part of the token set.
    [[nodiscard]] static constexpr Shape none() noexcept { return Shape(); }

    [[nodiscard]] static constexpr Shape extraSmall() noexcept
    {
        return Shape(CornerRadii::uniform(4.0), false);
    }

    [[nodiscard]] static constexpr Shape small() noexcept
    {
        return Shape(CornerRadii::uniform(8.0), false);
    }

    [[nodiscard]] static constexpr Shape medium() noexcept
    {
        return Shape(CornerRadii::uniform(12.0), false);
    }

    [[nodiscard]] static constexpr Shape large() noexcept
    {
        return Shape(CornerRadii::uniform(16.0), false);
    }

    [[nodiscard]] static constexpr Shape extraLarge() noexcept
    {
        return Shape(CornerRadii::uniform(28.0), false);
    }

    [[nodiscard]] static constexpr Shape extraExtraLarge() noexcept
    {
        return Shape(CornerRadii::uniform(48.0), false);
    }

    // A fully rounded end cap: the radius resolves to half the shorter side,
    // so the stored corner radii are ignored.
    [[nodiscard]] static constexpr Shape pill() noexcept
    {
        return Shape(CornerRadii::uniform(0.0), true);
    }

    // A shape with explicit, possibly asymmetric, per-corner radii.
    [[nodiscard]] static constexpr Shape rounded(CornerRadii radii) noexcept
    {
        return Shape(radii, false);
    }

    [[nodiscard]] constexpr CornerRadii radii() const noexcept { return radii_; }
    [[nodiscard]] constexpr bool isPill() const noexcept { return pill_; }

    // The radii this shape uses for `bounds`. A pill ignores the stored radii
    // and resolves to a semicircle; every other shape passes its corners
    // through unchanged.
    [[nodiscard]] constexpr CornerRadii resolved(const QRectF& bounds) const noexcept
    {
        if (!pill_)
            return radii_;

        const qreal shorter = bounds.width() < bounds.height() ? bounds.width()
                                                               : bounds.height();
        return CornerRadii::uniform(shorter > 0.0 ? shorter / 2.0 : 0.0);
    }

    // The outline for `bounds`. This is the single place where a shape token
    // becomes geometry; a custom outline only has to replace the factory.
    [[nodiscard]] QPainterPath path(const QRectF& bounds) const;

    [[nodiscard]] friend constexpr bool operator==(const Shape& lhs,
                                                   const Shape& rhs) noexcept = default;

private:
    CornerRadii radii_{};
    bool pill_ = false;
};

// The named shapes a theme exposes. `full` is the pill, so the scale covers
// Material's shape categories without leaking corner constants.
class ShapeScale
{
public:
    Shape none;
    Shape extraSmall;
    Shape small;
    Shape medium;
    Shape large;
    Shape extraLarge;
    Shape full;

    [[nodiscard]] static constexpr ShapeScale baseline() noexcept
    {
        return ShapeScale{Shape::none(),        Shape::extraSmall(), Shape::small(),
                          Shape::medium(),      Shape::large(),      Shape::extraLarge(),
                          Shape::pill()};
    }

    [[nodiscard]] friend constexpr bool operator==(const ShapeScale& lhs,
                                                   const ShapeScale& rhs) noexcept = default;
};

} // namespace md
