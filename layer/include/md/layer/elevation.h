// Periodicity — the elevation (shadow) stage (framework.md §3.4, §5.5, §7.3).
//
// ElevationLayer owns one drawable::ShadowDrawable and paints it before the
// background, so the background covers the solid interior of the mask and only
// the blurred rim remains visible. The shadow is never clipped to the
// content rectangle: a shadow that the surface clipped would defeat the point
// (framework.md §3.4), which is why paint() does not set a clip.
//
// Effective configuration. A layer can be configured explicitly with
// setElevation/setShape/setColour, or it can read those values from the
// SurfaceContext on every paint. The explicit value wins when set; when it is
// not, the context supplies the default. That makes both styles correct:
//
//   ElevationLayer layer;                      // follows the context
//   layer.setElevation(Elevation::Level3);     // fixed elevation
//
// shadowMargins() reports the inset for the configured (possibly unset) state;
// shadowMargins(context) resolves the effective configuration and is the
// overload a widget should use when it reserves geometry.
//
// Level0 draws nothing and reports zero margins. An out-of-range level (for
// example static_cast<Elevation>(6), which stored data can produce) is
// normalised to Level0 by every entry point.

#pragma once

#include <md/drawable/shadow_drawable.h>
#include <md/layer/layer.h>
#include <md/tokens/elevation.h>
#include <md/tokens/shape.h>

#include <QColor>
#include <QMarginsF>

#include <optional>

class QPainter;
class QRectF;

namespace md::layer {

class ElevationLayer final : public Layer
{
public:
    ElevationLayer() = default;

    // The explicit elevation. Level0 disables the shadow; unset means "use the
    // context". An out-of-range level is normalised to Level0. Each setter
    // keeps the owned ShadowDrawable in sync so shadowMargins() is correct
    // before the first paint.
    void setElevation(Elevation elevation);
    [[nodiscard]] Elevation elevation() const noexcept;

    // The explicit shape. Unset means "use context.shape".
    void setShape(Shape shape);
    [[nodiscard]] Shape shape() const noexcept;

    // The explicit shadow colour. Unset or invalid means "use the theme's
    // shadow role" (black in both baseline schemes).
    void setColour(QColor colour);
    [[nodiscard]] QColor colour() const noexcept { return colour_; }

    // How far the shadow extends beyond the bounds, for the configured state.
    [[nodiscard]] QMarginsF shadowMargins() const;

    // How far the shadow extends beyond the bounds for `context`, resolving
    // shape and elevation exactly as paint() does.
    [[nodiscard]] QMarginsF shadowMargins(const SurfaceContext& context) const;

    // Draws the shadow outside `bounds`, unclipped. Level0 is a no-op.
    void paint(QPainter& painter, const QRectF& bounds,
               const SurfaceContext& context) const override;

private:
    [[nodiscard]] Elevation effectiveElevation(const SurfaceContext& context) const noexcept;
    [[nodiscard]] Shape effectiveShape(const SurfaceContext& context) const noexcept;
    [[nodiscard]] QColor effectiveColour(const SurfaceContext& context) const;

    std::optional<Elevation> elevation_;
    std::optional<Shape> shape_;
    QColor colour_;

    // Mutable because paint() is part of the const Layer contract and the
    // drawable has to be synchronised with the effective configuration before
    // drawing. The setters are idempotent, so a steady paint loop does not
    // disturb the pixmap cache.
    mutable drawable::ShadowDrawable shadow_;
};

} // namespace md::layer
