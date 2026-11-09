// Periodicity — foreground stages (framework.md §3.6, §5.5).
//
// All three stages are stateless: they read the SurfaceContext, paint once and
// allocate no persistent resources. The shared shape clip is applied with
// QPainter's own clip path rather than by constructing an intersection path,
// which keeps a rounded surface from showing square overlay corners.

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
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipPath(context.shape.path(bounds));
    painter.fillRect(bounds, overlay);
    painter.restore();
}

void FocusRingLayer::paint(QPainter& painter, const QRectF& bounds,
                           const SurfaceContext& context) const
{
    if (bounds.isEmpty() || !painter.isActive())
        return;

    if (!context.state.testFlag(ViewState::Focused))
        return;

    const Density& density = context.theme.density();
    const qreal offset = density.dp(2.0);
    const qreal width = density.dp(2.0);
    if (width <= 0.0)
        return;

    // The ring path is the shape grown by 2 dp; the pen is centred on it, so
    // the visible band spans 1..3 dp outside the surface. The surface itself
    // is left untouched.
    const QRectF ringBounds = bounds.adjusted(-offset, -offset, offset, offset);
    const QPainterPath path = context.shape.path(ringBounds);
    if (path.isEmpty())
        return;

    QPen pen(context.theme.colours().secondary, width);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
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
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
    painter.restore();
}

} // namespace md::layer
