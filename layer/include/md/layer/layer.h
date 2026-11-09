// Periodicity — the layer base class (framework.md §5.5).
//
// A layer is a stage in the paint pipeline rather than a separate render
// target. A component owns an ordered stack of them; the stack paints the
// elevation layer first, then the background, then the component's content,
// then the foreground layers (state overlay, ripple, focus ring, outline).
//
// `hitTest` deliberately takes only a point: a layer that claims hits (a
// ripple, a child region) knows its own geometry, while a background cannot
// hit-test without bounds and therefore returns false. The stack asks the
// layers in reverse paint order, so the topmost claimant wins.
//
// `isAnimated`/`advance` are the animation seam: a ripple is a foreground
// layer that returns true from isAnimated and is advanced by the host's frame
// timer. A static layer inherits the no-op implementations and costs nothing.

#pragma once

#include <QPointF>
#include <QRectF>

#include <chrono>

class QPainter;

namespace md::layer {

struct SurfaceContext;

class Layer
{
public:
    virtual ~Layer() = default;

    // Paints this stage. The painter is in logical coordinates and may target
    // an image with no widget behind it; an implementation saves and restores
    // whatever painter state it changes.
    virtual void paint(QPainter& painter, const QRectF& bounds,
                       const SurfaceContext& context) const = 0;

    // True when the layer claims `point`, in the same coordinate system as the
    // bounds passed to paint(). The default claims nothing.
    [[nodiscard]] virtual bool hitTest(const QPointF&) const { return false; }

    // True while the layer needs frame callbacks from advance(). Static layers
    // keep the default, so a stack can skip its timer entirely.
    [[nodiscard]] virtual bool isAnimated() const noexcept { return false; }

    // Advances the layer's animation by `elapsed`. Only meaningful when
    // isAnimated() is true; the default ignores the frame.
    virtual void advance(std::chrono::milliseconds) {}
};

} // namespace md::layer
