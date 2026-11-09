// Periodicity — the background stage (framework.md §5.5, §7.4).
//
// BackgroundLayer paints the container: the tonal surface for the context's
// elevation, clipped to the context's shape. Tonal elevation is a colour
// lookup, never a white overlay at a fixed alpha (framework.md §7.4), so the
// layer simply asks the scheme for surfaceAtElevation(level) unless the caller
// supplied an explicit containerColour.
//
// The layer is stateless: every input lives in the SurfaceContext, which means
// a theme change or a state change needs no setter and no invalidation.

#pragma once

#include <md/layer/layer.h>

class QPainter;
class QRectF;

namespace md::layer {

class BackgroundLayer final : public Layer
{
public:
    BackgroundLayer() = default;

    // Fills the shape-clipped bounds with context.resolvedContainerColour().
    // Draws nothing when the resolved colour is fully transparent.
    void paint(QPainter& painter, const QRectF& bounds,
               const SurfaceContext& context) const override;
};

} // namespace md::layer
