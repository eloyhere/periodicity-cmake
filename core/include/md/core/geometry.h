// Periodicity — geometry helpers (framework.md §5.1).
//
// Two small value types shared by every layer: uniform insets and per-corner
// radii. Both are copyable, comparable and free of any painting policy.

#pragma once

#include <QPainterPath>
#include <QRectF>
#include <QtGlobal>

namespace md {

// Padding or margin around a rectangle. The four sides are independent, so
// asymmetric layouts (for example a leading icon with trailing text) are
// expressed without special cases.
struct Insets
{
    qreal left = 0.0;
    qreal top = 0.0;
    qreal right = 0.0;
    qreal bottom = 0.0;

    [[nodiscard]] constexpr qreal horizontal() const noexcept { return left + right; }
    [[nodiscard]] constexpr qreal vertical() const noexcept { return top + bottom; }

    // Shrinks `rect` by these insets. The result is empty (or even negative)
    // when the insets exceed the rectangle; callers decide whether that is an
    // error or simply an invisible item.
    [[nodiscard]] QRectF appliedTo(const QRectF& rect) const noexcept
    {
        return rect.adjusted(left, top, -right, -bottom);
    }

    [[nodiscard]] friend constexpr bool operator==(const Insets& lhs,
                                                   const Insets& rhs) noexcept = default;
};

// Corner radii in Qt's logical order: start and end are left and right in a
// left-to-right layout and mirror automatically when a caller lays a widget
// out right-to-left, because only the drawing coordinates swap — no radius
// value needs rewriting.
struct CornerRadii
{
    qreal topStart = 0.0;
    qreal topEnd = 0.0;
    qreal bottomEnd = 0.0;
    qreal bottomStart = 0.0;

    // The same radius on all four corners.
    [[nodiscard]] static constexpr CornerRadii uniform(qreal radius) noexcept
    {
        return CornerRadii{radius, radius, radius, radius};
    }

    // The rounded-rectangle outline for `bounds`. Each radius is clamped to
    // half the shorter side of the rectangle, which keeps the path valid for
    // pills and for rectangles smaller than the nominal radius. A rectangle
    // with no area yields an empty path.
    [[nodiscard]] QPainterPath toPath(const QRectF& bounds) const;

    [[nodiscard]] friend constexpr bool operator==(const CornerRadii& lhs,
                                                   const CornerRadii& rhs) noexcept = default;
};

} // namespace md
