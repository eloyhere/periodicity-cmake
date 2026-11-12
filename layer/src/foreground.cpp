// Periodicity — foreground stages (framework.md §3.6, §5.5).
//
// All three stages are stateless: they read the SurfaceContext, paint once and
// allocate no persistent resources. The shared shape clip is applied with
// QPainter's own clip path rather than by constructing an intersection path,
// which keeps a rounded surface from showing square overlay corners. That clip
// is intersected with whatever the caller already set, never substituted for it.

#include <md/layer/foreground.h>

#include <md/core/density.h>
#include <md/layer/surface_context.h>
#include <md/tokens/state_layer.h>

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

namespace md::layer {

namespace {

// The token function takes the singular enum and resolves the interaction bits
// itself; the bitset is forwarded unchanged so a combined state yields the
// strongest token instead of a sum.
ViewState combinedState(ViewStates state) noexcept
{
    return static_cast<ViewState>(static_cast<quint32>(state));
}

} // namespace

void StateOverlayLayer::paint(QPainter& painter, const QRectF& bounds,
                              const SurfaceContext& context) const
{
    if (bounds.isEmpty() || !painter.isActive())
        return;

    // A disabled component gets no interaction overlay, even if a stale hover
    // bit survived the state update that disabled it.
    if (!context.state.testFlag(ViewState::Enabled))
        return;

    const qreal opacity = stateLayerOpacity(combinedState(context.state));
    if (opacity <= 0.0)
        return;

    QColor overlay = context.resolvedContentColour();
    if (!overlay.isValid() || overlay.alpha() == 0)
        return;

    overlay.setAlphaF(static_cast<float>(overlay.alphaF() * opacity));

    painter.save();
    try {
        painter.setRenderHint(QPainter::Antialiasing, true);
        // Intersect, never replace: the caller's clip is a damage region or a
        // host clip and must stay in force for this overlay too.
        painter.setClipPath(context.shape.path(bounds), Qt::IntersectClip);
        painter.fillRect(bounds, overlay);
    } catch (...) {
        // A decorative overlay must not stop the stages that paint after it.
    }
    painter.restore();
}

FocusRingLayer::FocusRingLayer(Placement placement) noexcept
    : placement_(placement)
{
}

void FocusRingLayer::paint(QPainter& painter, const QRectF& bounds,
                           const SurfaceContext& context) const
{
    if (bounds.isEmpty() || !painter.isActive())
        return;

    if (!context.state.testFlag(ViewState::Focused))
        return;

    const Density& density = context.theme.density();
    const qreal width = density.dp(2.0);
    if (width <= 0.0)
        return;

    // The pen is centred on the path, so the stroke spans half its width on
    // either side. An outside ring keeps the token geometry (a 2 dp band
    // starting 1 dp outside the shape). An inside ring moves the path inwards
    // by half the stroke, which puts the whole band inside the shape's edge:
    // a widget's painter is clipped to the widget rectangle, and an outside
    // ring would otherwise leave only the bits of its rounded-corner arcs that
    // fall back inside the widget — the corner-dot and clipped-arc artefacts.
    const qreal inset = placement_ == Placement::Outside ? -density.dp(2.0) : width / 2.0;
    const QRectF ringBounds = bounds.adjusted(inset, inset, -inset, -inset);
    const QPainterPath path = context.shape.path(ringBounds);
    if (path.isEmpty())
        return;

    QPen pen(context.theme.colours().secondary, width);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);

    painter.save();
    try {
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    } catch (...) {
        // A focus ring is decorative: isolate it from the rest of the stack.
    }
    painter.restore();
}

void OutlineLayer::paint(QPainter& painter, const QRectF& bounds,
                         const SurfaceContext& context) const
{
    if (bounds.isEmpty() || !painter.isActive())
        return;

    const qreal width = context.theme.density().dp(1.0);
    if (width <= 0.0)
        return;

    // Inset by half the stroke so the contour lies inside the bounds and the
    // surface measures exactly the rectangle it was given.
    const qreal inset = width / 2.0;
    const QRectF outlineBounds = bounds.adjusted(inset, inset, -inset, -inset);
    const QPainterPath path = context.shape.path(outlineBounds);
    if (path.isEmpty())
        return;

    QPen pen(context.theme.colours().outlineVariant, width);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);

    painter.save();
    try {
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    } catch (...) {
        // A contour is decorative: isolate it from the rest of the stack.
    }
    painter.restore();
}

} // namespace md::layer
