// Periodicity — render layer: the compositor interface. FROZEN.
//
// This header is the freeze point of the L2 line: the compositor abstraction is
// the seam every window host (the in-widget CPU host today, the RHI host next)
// and every backend implements, and it changes only by a decision recorded in
// docs/rendering-architecture.md. Keep it minimal on purpose — two virtual
// functions and one value type. A backend that needs more state keeps it in its
// own derived class; a host that needs another operation is a new interface, not
// another virtual here.
//
// The model. The compositor is given an ordered list of layers — bottom to top,
// in z order — and draws each one into the target. A LayerRef is a *placement*,
// not an ownership claim: the layer outlives the call (it belongs to
// LayerCache), and the compositor never reads or writes its pixels, only blits
// them. That is what lets the same layer be drawn at a different destination
// next frame (a scroll or a drag) without re-rasterising it.
//
//   * `destination` is in the target's logical coordinates and may be a
//     different size than the layer; the backend scales in that case.
//   * `opacity` is the layer's own alpha, clamped to 0..1. A value <= 0 skips
//     the layer.
//   * `clip` is an additional logical-coordinate clip, intersected with the
//     viewport. Empty means no additional clip.
//
// `compose()` never throws and never reports failure through a return value:
// an invalid target is a no-op. A backend that needs to explain a refusal
// exposes lastError() on its own class (CpuCompositor does).
//
// The CPU implementation is the reference: every other backend must produce the
// same picture for the same list, which is what the parity tests assert.

#pragma once

#include <md/render/layer.h>
#include <md/render/render_target.h>

#include <QList>
#include <QRectF>
#include <QString>
#include <QtGlobal>

class QPainter;

namespace md::render {

// One layer placed on the target. Aggregate: a caller writes
//   { layer, QRectF(0, 0, w, h), 1.0, QRectF() }
// and never has to touch a constructor.
struct LayerRef
{
    // The layer to draw. Null and invalid layers are skipped.
    const Layer* layer = nullptr;

    // Where the layer lands, in the target's logical coordinates.
    QRectF destination;

    // The layer's opacity, clamped to 0..1; <= 0 skips it.
    qreal opacity = 1.0;

    // An additional clip in the target's logical coordinates. Empty means no
    // additional clip.
    QRectF clip;
};

class Compositor
{
public:
    virtual ~Compositor() = default;

    // Draws `layers` bottom to top into `target`. The target's device pixel
    // ratio is applied by the implementation; the list is in logical
    // coordinates.
    virtual void compose(RenderTarget&, const QList<LayerRef>&) = 0;

    // The stable backend name ("cpu" today). Never null and never translated;
    // it is an identifier, not a label.
    virtual const char* name() const = 0;
};

// The always-available reference backend: QPainter with SourceOver, a per-layer
// clip and a per-layer opacity. Deterministic and headless, which is what makes
// the layered-view pixel assertions possible.
class CpuCompositor final : public Compositor
{
public:
    CpuCompositor() = default;

    // Applies target.devicePixelRatio() and composes into the target image.
    void compose(RenderTarget& target, const QList<LayerRef>& layers) override;

    const char* name() const override;

    // The same composition into a painter that is already in the destination's
    // logical coordinates. This is the interim in-widget host: a View's
    // paintEvent owns a QPainter on the widget, not a RenderTarget, and blits
    // its own layer through here. The window-level host keeps using compose().
    //
    // The painter's clip, opacity and composition mode are saved and restored
    // around every layer, so a caller's state does not leak between layers.
    void composeInto(QPainter& painter, const QList<LayerRef>& layers);

    // Why the last compose() drew nothing; empty when it ran.
    [[nodiscard]] QString lastError() const { return lastError_; }

private:
    QString lastError_;
};

} // namespace md::render
