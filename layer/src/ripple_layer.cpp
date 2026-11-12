// Periodicity — ripple layer implementation (framework.md §5.5).

#include <md/layer/ripple_layer.h>

#include <QPainter>

#include <utility>

namespace md::layer {

RippleLayer::RippleLayer(Theme theme, QObject* parent)
    : controller_(nullptr)
{
    // `parent` is accepted for source compatibility but deliberately not used:
    // controller_ is a value member, so parenting it to an external QObject
    // would make that object delete a subobject it does not own. The layer
    // destroys the controller exactly once, with itself.
    Q_UNUSED(parent);

    controller_.setTheme(std::move(theme));
}

RippleLayer::~RippleLayer() = default;

void RippleLayer::setTheme(Theme theme)
{
    controller_.setTheme(std::move(theme));
}

void RippleLayer::setCornerRadius(qreal radius)
{
    controller_.setCornerRadius(radius);
}

void RippleLayer::setBounds(const QRectF& bounds)
{
    controller_.setBounds(bounds);
}

ripple::RippleController& RippleLayer::controller() noexcept
{
    return controller_;
}

const ripple::RippleController& RippleLayer::controller() const noexcept
{
    return controller_;
}

void RippleLayer::paint(QPainter& painter, const QRectF& bounds,
                        const SurfaceContext& context) const
{
    Q_UNUSED(context);

    // Same guard as every other stage: an empty rectangle or an inactive
    // painter has no pixels to contribute, and the controller must not be asked
    // to paint into either.
    if (bounds.isEmpty() || !painter.isActive())
        return;

    // The controller clips to the bounds and corner radius configured by the
    // host. That cached rectangle can be stale, so intersect it with the
    // rectangle the layer was actually asked to paint; a save/restore keeps the
    // stage from leaking that clip into the next one.
    painter.save();
    try {
        painter.setClipRect(bounds, Qt::IntersectClip);
        controller_.paint(painter);
    } catch (...) {
        // A ripple is feedback, never a reason for the rest of the composite
        // to stop painting.
    }
    painter.restore();
}

bool RippleLayer::isAnimated() const noexcept
{
    return controller_.isAnimating();
}

} // namespace md::layer
