// Periodicity — foreground stages (framework.md §3.6, §5.5).
//
// A foreground layer paints after the component's content. The base class
// exists to make that ordering explicit in a stack: a foreground layer added
// after the content is guaranteed to ride on top of it, which is what a state
// overlay, a focus ring and an outline all need.
//
// The three concrete stages read their configuration from the SurfaceContext,
// so none of them owns state or needs invalidation:
//
//   StateOverlayLayer  content colour at the state-layer opacity, clipped to
//                      the shape; nothing when the state has no interaction
//                      flag or the component is disabled.
//   FocusRingLayer     a 2 dp secondary outline 2 dp outside the shape when
//                      Focused is set; nothing otherwise.
//   OutlineLayer       the 1 dp outlineVariant contour, inset so the stroke
//                      lies inside the bounds; used by outlined surfaces.

#pragma once

#include <md/layer/layer.h>

class QPainter;
class QRectF;

namespace md::layer {

// Base for stages that paint after content. It adds no behaviour; its purpose
// is to make the stack's ordering contract readable at the call site.
class ForegroundLayer : public Layer
{
public:
    ~ForegroundLayer() override = default;
};

// The interaction overlay: context.resolvedContentColour() at the opacity the
// state-layer token gives the context state, clipped to context.shape.
//
// The layer is a no-op when the component is disabled (the Enabled bit is
// clear, framework.md §6.4) even if a stale interaction bit is still set, and
// a no-op when the resolved opacity is zero (the idle state).
class StateOverlayLayer final : public ForegroundLayer
{
public:
    void paint(QPainter& painter, const QRectF& bounds,
               const SurfaceContext& context) const override;
};

// The keyboard focus indicator: a 2 dp outline in the theme's secondary role,
// stroked along a path 2 dp outside the shape. Drawn only when Focused is set.
// The offset and the width are scaled by the theme's Density, so a zoomed
// application keeps the ring proportionate.
class FocusRingLayer final : public ForegroundLayer
{
public:
    void paint(QPainter& painter, const QRectF& bounds,
               const SurfaceContext& context) const override;
};

// The contour of an outlined surface: 1 dp of outlineVariant along the shape,
// inset by half the stroke width so it stays inside the bounds.
class OutlineLayer final : public ForegroundLayer
{
public:
    void paint(QPainter& painter, const QRectF& bounds,
               const SurfaceContext& context) const override;
};

} // namespace md::layer
