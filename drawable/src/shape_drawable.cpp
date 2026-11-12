// Periodicity — ShapeDrawable (framework.md §3.3, §5.4).
//
// The path is built per call from the shape token and the rectangle; a
// component that repaints every frame should hoist the bounds, not the
// drawable, because the shape is a value and the path factory is cheap
// relative to rasterisation.

#include <md/drawable/shape_drawable.h>

#include "drawable_hash_p.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <utility>

namespace md::drawable {

ShapeDrawable::ShapeDrawable(Shape shape)
    : shape_(shape)
{
}

ShapeDrawable::ShapeDrawable(Shape shape, QColor fill)
    : shape_(shape)
    , fill_(std::move(fill))
{
}

void ShapeDrawable::setShape(Shape shape)
{
    if (shape_ == shape)
        return;
    shape_ = shape;
    invalidate();
}

void ShapeDrawable::setFill(QColor colour)
{
    if (fill_ == colour)
        return;
    fill_ = std::move(colour);
    invalidate();
}

void ShapeDrawable::setStroke(QColor colour, qreal widthDp)
{
    if (stroke_ == colour && qFuzzyCompare(strokeWidthDp_, widthDp))
        return;
    stroke_ = std::move(colour);
    strokeWidthDp_ = widthDp;
    invalidate();
}

void ShapeDrawable::setDensity(Density density)
{
    if (density_ == density)
        return;
    density_ = density;
    invalidate();
}

qreal ShapeDrawable::strokeWidth() const noexcept
{
    return density_.dp(strokeWidthDp_);
}

void ShapeDrawable::draw(QPainter& painter, const QRectF& bounds) const
{
    if (bounds.isEmpty() || !painter.isActive())
        return;

    // An invalid colour and a fully transparent colour mean the same thing to
    // a painter: do not spend a draw call on it. A zero-width stroke is
    // likewise invisible.
    const bool hasFill = fill_.isValid() && fill_.alpha() > 0;
    const bool hasStroke = stroke_.isValid() && stroke_.alpha() > 0 && strokeWidth() > 0.0;
    if (!hasFill && !hasStroke)
        return;

    // Save before the try so the state is restored on both the success and
    // the failure path. A throw from path construction, allocation or the
    // raster engine is contained here; one drawable must not abort the
    // composite that contains it.
    painter.save();
    try {
        const QPainterPath path = shape_.path(bounds);
        if (!path.isEmpty()) {
            painter.setRenderHint(QPainter::Antialiasing, true);

            if (hasFill) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(fill_);
                painter.drawPath(path);
            }

            if (hasStroke) {
                QPen pen(stroke_, strokeWidth());
                // Round joins and caps keep a rounded outline from growing a
                // miter spike at the corners, which is the M3 look for a
                // stroked surface.
                pen.setJoinStyle(Qt::RoundJoin);
                pen.setCapStyle(Qt::RoundCap);
                painter.setPen(pen);
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(path);
            }
        }
    } catch (...) {
        // A failed primitive is skipped; the painter state is restored below.
    }
    painter.restore();
}

quint64 ShapeDrawable::cacheKey() const
{
    quint64 key = detail::hashShape(shape_);
    key = detail::mix(key, detail::hashColour(fill_));
    key = detail::mix(key, detail::hashColour(stroke_));
    key = detail::mix(key, detail::hashScalar(strokeWidthDp_));
    // Hash the scale the conversions actually use, not the raw member: an
    // unusable scale falls back to 1.0, and hashing NaN would make qRound64()
    // undefined.
    key = detail::mix(key, detail::hashScalar(density_.effectiveScale()));
    return key;
}

} // namespace md::drawable
