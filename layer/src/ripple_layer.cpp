// Periodicity — ripple layer implementation (framework.md §5.5).

#include <md/layer/ripple_layer.h>

#include <QPainter>

#include <utility>

namespace md::layer {

RippleLayer::RippleLayer(Theme theme, QObject* parent)
    : controller_(parent)
{
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
    Q_UNUSED(bounds);
    Q_UNUSED(context);

    // The controller clips to the bounds and corner radius configured by the
    // host; the layer only contributes its position in the stack.
    controller_.paint(painter);
}

bool RippleLayer::isAnimated() const noexcept
{
    return controller_.isAnimating();
}

} // namespace md::layer
