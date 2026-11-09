// Periodicity — the interaction overlay (framework.md §3.6, §5.4).
//
// A state layer is a translucent copy of the content colour composited over
// the container. The opacity is never a component colour: it comes from the
// state-layer tokens, which resolve Hovered to 8 %, Focused and Pressed to
// 12 % and Dragged to 16 %. This drawable simply fills its bounds with
// contentColour at the opacity of its current state; the owner clips it to the
// shape (a layer::StateOverlayLayer does that in the composited pipeline).
//
// The drawable paints nothing when the resolved opacity is zero, so an enabled
// idle component pays only a state read. Setting a state bumps the revision,
// which keeps a raster cache honest even if one is added above it.

#pragma once

#include <md/drawable/drawable.h>

#include <QColor>

class QPainter;
class QRectF;

namespace md::drawable {

class StateLayerDrawable : public Drawable
{
public:
    StateLayerDrawable() = default;

    // The overlay colour; opaque black by default, so the drawable is visible
    // even before a component resolves the theme's onSurface role.
    explicit StateLayerDrawable(QColor contentColour);

    // The colour the overlay is made of. Its own alpha is preserved and
    // multiplied by the state opacity, so a translucent content colour stays
    // translucent.
    void setContentColour(QColor colour);
    [[nodiscard]] QColor contentColour() const noexcept { return contentColour_; }

    void draw(QPainter& painter, const QRectF& bounds) const override;

    // Hashes the content colour and the current state bits.
    [[nodiscard]] quint64 cacheKey() const override;

private:
    QColor contentColour_{Qt::black};
};

} // namespace md::drawable
