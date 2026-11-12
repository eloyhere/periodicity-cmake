// Periodicity — BackgroundLayer (framework.md §5.5, §7.4).
//
// A single fill behind a shape clip. Keeping the fill a plain rectangle (and
// the geometry in the clip path) means the tonal colour is exact at every
// opaque pixel; the antialiased clip only softens the rounded edge, which is
// what the surface should look like.

#include <md/layer/background.h>

#include <md/layer/surface_context.h>

#include <QPainter>
#include <QPainterPath>

namespace md::layer {

void BackgroundLayer::paint(QPainter& painter, const QRectF& bounds,
                            const SurfaceContext& context) const
{
    if (bounds.isEmpty() || !painter.isActive())
        return;

    const QColor fill = context.resolvedContainerColour();
    if (!fill.isValid() || fill.alpha() == 0)
        return;

    painter.save();
    try {
        painter.setRenderHint(QPainter::Antialiasing, true);
        // IntersectClip, not ReplaceClip: the caller's damage region, viewport
        // or pre-set clip must remain an upper bound on the fill. Replacing it
        // would paint the whole shape into a painter that had already excluded
        // most of it.
        painter.setClipPath(context.shape.path(bounds), Qt::IntersectClip);
        painter.fillRect(bounds, fill);
    } catch (...) {
        // A malformed shape or an allocation failure must not abort the rest
        // of the composite; the painter state is restored below.
    }
    painter.restore();
}

} // namespace md::layer
