// Periodicity — geometry helpers (framework.md §5.1).
//
// The rounded-rectangle outline lives here rather than in the header so that
// including <md/core/geometry.h> stays cheap.

#include <md/core/geometry.h>

#include <algorithm>

namespace md {

namespace {

// Negative and non-finite radii are meaningless and become zero. Positive
// radii are capped at `limit`, the largest radius the rectangle can carry on
// its shorter side. The test is inverted so NaN — for which every comparison
// is false — degenerates to a square corner instead of falling through to the
// clamp and becoming the maximum radius. Infinity is positive, so it clamps.
constexpr qreal clampRadius(qreal radius, qreal limit) noexcept
{
    if (!(radius > 0.0))
        return 0.0;
    return radius < limit ? radius : limit;
}

} // namespace

QPainterPath CornerRadii::toPath(const QRectF& bounds) const
{
    QPainterPath path;

    // An empty rectangle has no outline to draw. Returning an empty path
    // keeps the result predictable for degenerate sizes.
    if (bounds.isEmpty())
        return path;

    // QRectF::isEmpty() only tests `width() <= 0 || height() <= 0`, which is
    // false for NaN and for an infinite extent. Without this guard every
    // radius would clamp to a non-finite limit and the path would contain NaN
    // or infinite points; a non-finite rectangle is a no-op instead.
    if (!qIsFinite(bounds.x()) || !qIsFinite(bounds.y())
        || !qIsFinite(bounds.width()) || !qIsFinite(bounds.height())) {
        return path;
    }

    const qreal limit = std::min(bounds.width(), bounds.height()) / 2.0;
    const qreal startTop = clampRadius(topStart, limit);
    const qreal endTop = clampRadius(topEnd, limit);
    const qreal endBottom = clampRadius(bottomEnd, limit);
    const qreal startBottom = clampRadius(bottomStart, limit);

    const qreal left = bounds.left();
    const qreal top = bounds.top();
    const qreal right = bounds.right();
    const qreal bottom = bounds.bottom();

    // Clockwise from the top edge, with negative sweep angles because Qt's
    // y axis points down.
    path.moveTo(left + startTop, top);
    path.lineTo(right - endTop, top);
    if (endTop > 0.0) {
        path.arcTo(QRectF(right - 2.0 * endTop, top, 2.0 * endTop, 2.0 * endTop),
                   90.0, -90.0);
    }

    path.lineTo(right, bottom - endBottom);
    if (endBottom > 0.0) {
        path.arcTo(QRectF(right - 2.0 * endBottom, bottom - 2.0 * endBottom,
                          2.0 * endBottom, 2.0 * endBottom),
                   0.0, -90.0);
    }

    path.lineTo(left + startBottom, bottom);
    if (startBottom > 0.0) {
        path.arcTo(QRectF(left, bottom - 2.0 * startBottom, 2.0 * startBottom,
                          2.0 * startBottom),
                   270.0, -90.0);
    }

    path.lineTo(left, top + startTop);
    if (startTop > 0.0) {
        path.arcTo(QRectF(left, top, 2.0 * startTop, 2.0 * startTop), 180.0, -90.0);
    }

    path.closeSubpath();
    return path;
}

} // namespace md
