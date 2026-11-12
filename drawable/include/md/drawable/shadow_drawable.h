// Periodicity — the elevation shadow as a cached blurred mask
// (framework.md §3.4, §7.3).
//
// ShadowDrawable renders the silhouette of a Shape in a single colour, blurs
// it, and caches the resulting raster in QPixmapCache. It is the only shadow
// implementation in the library: QGraphicsDropShadowEffect is prohibited
// (framework.md §7.3) because it forces an offscreen render of the whole
// widget, does not compose with QPainter clipping and is a performance trap in
// lists.
//
// The masks are generated the way the framework specifies: the shape path is
// painted in black into an ARGB32_Premultiplied image sized to
// (bounds + shadowMargins()) at the painter's device pixel ratio, and a
// three-pass separable box blur approximates the Gaussian. The blur is linear
// in the pixel count: each pass is a sliding-window sum, and the six passes
// (three horizontal, three vertical) reuse two ping-pong buffers. No
// per-pixel division remains outside the sliding update.
//
// Cache identity is (shape, elevation, colour, blur scale) from cacheKey()
// plus the size bucket, the device pixel ratio and revision(). Sizes are
// bucketed to 8 device pixels so an animation that resizes by a pixel per
// frame does not create a pixmap per frame; the bucket is stretched into the
// requested rectangle, which is imperceptible for a blur mask. The colour is
// part of cacheKey() because a recoloured shadow must not reuse the previous
// raster.
//
// Level0 draws nothing and reports zero margins. For every other level the
// shadow extends beyond the shape on all sides; `shadowMargins()` reports how
// far, so a component can reserve an inset and never clip its own shadow
// (framework.md §7.3). The layer above (layer::ElevationLayer) paints this
// before the background, which covers the solid interior.
//
// The raster is bounded: a mask whose device-pixel extent exceeds 8192 pixels
// per side (a corrupt or effectively unbounded rectangle) is refused rather
// than allocated, and an allocation that Qt cannot satisfy is skipped. A
// failed shadow paints nothing; it never aborts the composite.

#pragma once

#include <md/drawable/drawable.h>
#include <md/tokens/elevation.h>
#include <md/tokens/shape.h>

#include <QColor>
#include <QMarginsF>

class QPainter;
class QRectF;

namespace md::drawable {

class ShadowDrawable : public Drawable
{
public:
    ShadowDrawable() = default;

    // The elevation level. A change bumps the revision.
    void setElevation(Elevation elevation);
    [[nodiscard]] Elevation elevation() const noexcept { return elevation_; }

    // The silhouette to blur. A change bumps the revision.
    void setShape(Shape shape);
    [[nodiscard]] Shape shape() const noexcept { return shape_; }

    // The shadow colour; opaque black by default. The alpha scales the whole
    // mask, so a translucent colour yields a fainter shadow.
    void setColour(QColor colour);
    [[nodiscard]] QColor colour() const noexcept { return colour_; }

    // Multiplies the blur extent and the vertical offset. 1.0 is the token
    // geometry; a larger value makes a softer, wider shadow. Non-positive
    // values disable the blur and the offset, leaving only the silhouette;
    // negative and non-finite values are clamped to zero.
    void setBlurScale(qreal scale);
    [[nodiscard]] qreal blurScale() const noexcept { return blurScale_; }

    // How far the shadow extends beyond the bounds on each side, in logical
    // pixels. Zero at Level0. The value grows with the elevation level and
    // with blurScale(); it is symmetric because the canvas is padded once and
    // the small vertical offset is included in the margin.
    [[nodiscard]] QMarginsF shadowMargins() const;

    void draw(QPainter& painter, const QRectF& bounds) const override;

    // Hashes the shape, elevation, colour and blur scale. The caller adds the
    // rectangle, the device pixel ratio and revision() when it builds a cache
    // key (draw() does exactly that).
    [[nodiscard]] quint64 cacheKey() const override;

private:
    Elevation elevation_ = Elevation::Level0;
    Shape shape_;
    QColor colour_{Qt::black};
    qreal blurScale_ = 1.0;
};

} // namespace md::drawable
