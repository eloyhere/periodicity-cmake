// Periodicity — StateLayerDrawable (framework.md §3.6, §5.4).
//
// The opacity comes from the token function rather than a local table: a
// revision of the state-layer specification therefore changes every component
// in one place. The composite is a plain source-over fill: the overlay colour
// keeps its RGB and only its alpha is scaled, which is exactly how M3
// composites an interaction layer over a container.

#include <md/drawable/state_layer_drawable.h>

#include "drawable_hash_p.h"

#include <md/tokens/state_layer.h>

#include <QPainter>
#include <QPainterPath>

#include <utility>

namespace md::drawable {

namespace {

// The token function takes the singular enum and resolves the four interaction
// bits itself, because the flags are independent bits of one value. The
// bitset is passed through unchanged so a combined Hovered|Pressed state still
// yields the strongest token (12 %), never a sum.
ViewState combinedState(ViewStates state) noexcept
{
    return static_cast<ViewState>(static_cast<quint32>(state));
}

} // namespace

StateLayerDrawable::StateLayerDrawable(QColor contentColour)
    : contentColour_(std::move(contentColour))
{
}

void StateLayerDrawable::setContentColour(QColor colour)
{
    if (contentColour_ == colour)
        return;
    contentColour_ = std::move(colour);
    invalidate();
}

void StateLayerDrawable::draw(QPainter& painter, const QRectF& bounds) const
{
    if (bounds.isEmpty() || !painter.isActive())
        return;

    const qreal opacity = stateLayerOpacity(combinedState(state()));
    if (opacity <= 0.0 || !contentColour_.isValid() || contentColour_.alpha() == 0)
        return;

    QColor overlay = contentColour_;
    // setAlphaF multiplies the existing alpha, so a translucent content colour
    // stays translucent rather than being promoted to a solid overlay.
    overlay.setAlphaF(static_cast<float>(contentColour_.alphaF() * opacity));

    painter.save();
    // fillRect obeys the painter's clip, so a component that wants a rounded
    // overlay clips once (or uses layer::StateOverlayLayer, which does).
    painter.fillRect(bounds, overlay);
    painter.restore();
}

quint64 StateLayerDrawable::cacheKey() const
{
    quint64 key = detail::hashColour(contentColour_);
    key = detail::mix(key, static_cast<quint64>(static_cast<quint32>(state())));
    return key;
}

} // namespace md::drawable
