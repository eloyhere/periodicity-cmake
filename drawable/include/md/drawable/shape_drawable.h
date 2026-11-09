// Periodicity — a filled and/or stroked Shape (framework.md §3.3, §5.4).
//
// ShapeDrawable is the workhorse primitive of the drawable layer: it turns a
// shape token into a path for the given bounds and paints that path with a
// fill colour, a stroke colour, or both. It deliberately owns no layout and no
// token resolution — a component decides which shape and which role colour to
// hand it.
//
// The stroke width is expressed in dp, because that is the unit the Material
// specification uses for outlines. A Density converts it to logical pixels;
// the default density has scale 1.0, so a stroke of 2 dp is 2 logical pixels.
// The density is only needed when an application applies an explicit zoom:
// Qt already handles the operating system's display scaling.
//
// A fully transparent fill (the default) draws nothing, and so does a stroke
// of zero width. When both are present the fill is painted first and the
// stroke rides on top of it.

#pragma once

#include <md/core/density.h>
#include <md/drawable/drawable.h>
#include <md/tokens/shape.h>

#include <QColor>

class QPainter;
class QRectF;

namespace md::drawable {

class ShapeDrawable : public Drawable
{
public:
    ShapeDrawable() = default;

    // A shape-only drawable: no fill and no stroke until one is set.
    explicit ShapeDrawable(Shape shape);

    // The common case: a shape with a solid fill.
    ShapeDrawable(Shape shape, QColor fill);

    // Replaces the outline. A shape change bumps the revision.
    void setShape(Shape shape);
    [[nodiscard]] Shape shape() const noexcept { return shape_; }

    // The interior colour. The default is invalid (fully transparent), which
    // means "do not fill". An invalid colour clears the fill.
    void setFill(QColor colour);
    [[nodiscard]] QColor fill() const noexcept { return fill_; }

    // The outline colour and width. A non-positive width disables the stroke;
    // an invalid colour does too. The default is no stroke.
    void setStroke(QColor colour, qreal widthDp = 1.0);
    [[nodiscard]] QColor stroke() const noexcept { return stroke_; }
    [[nodiscard]] qreal strokeWidthDp() const noexcept { return strokeWidthDp_; }

    // The density used to convert strokeWidthDp() into logical pixels. The
    // default is the identity density (1 dp = 1 logical pixel).
    void setDensity(Density density);
    [[nodiscard]] Density density() const noexcept { return density_; }

    // strokeWidthDp() converted to logical pixels with density().
    [[nodiscard]] qreal strokeWidth() const noexcept;

    void draw(QPainter& painter, const QRectF& bounds) const override;

    // Hashes the shape, both colours, the stroke width and the density scale.
    [[nodiscard]] quint64 cacheKey() const override;

private:
    Shape shape_;
    QColor fill_;
    QColor stroke_;
    qreal strokeWidthDp_ = 1.0;
    Density density_{};
};

} // namespace md::drawable
