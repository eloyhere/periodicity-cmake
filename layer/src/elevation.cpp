// Periodicity — ElevationLayer (framework.md §3.4, §5.5, §7.3).
//
// The layer is a thin adapter over one ShadowDrawable. Its only real job is
// resolving the effective elevation/shape/colour: an explicit layer value wins,
// a context value is the fallback. Syncing the drawable is idempotent, so a
// compiling paint loop leaves the drawable's revision — and therefore the
// pixmap cache key — untouched.

#include <md/layer/elevation.h>

#include <md/layer/surface_context.h>

#include <QColor>
#include <QMarginsF>
#include <QPainter>

#include <utility>

namespace md::layer {

namespace {

// A level outside Level0..Level5 can only come from a static_cast on stored or
// serialised data. It has no shadow geometry, so treat it as Level0 instead of
// letting ShadowDrawable see an unknown enum value (which falls back to 0 dp
// for the offset but still reserves a blurred rim).
Elevation normaliseElevation(Elevation elevation) noexcept
{
    switch (elevation) {
    case Elevation::Level0:
    case Elevation::Level1:
    case Elevation::Level2:
    case Elevation::Level3:
    case Elevation::Level4:
    case Elevation::Level5:
        return elevation;
    }
    return Elevation::Level0;
}

} // namespace

void ElevationLayer::setElevation(Elevation elevation)
{
    elevation = normaliseElevation(elevation);
    if (elevation_ == elevation)
        return;

    elevation_ = elevation;
    // Keep the owned drawable current even before the first paint so that
    // shadowMargins() answers correctly for the configured state.
    shadow_.setElevation(elevation);
}

Elevation ElevationLayer::elevation() const noexcept
{
    return elevation_.value_or(Elevation::Level0);
}

void ElevationLayer::setShape(Shape shape)
{
    if (shape_ == shape)
        return;

    shape_ = shape;
    shadow_.setShape(shape);
}

Shape ElevationLayer::shape() const noexcept
{
    return shape_.value_or(Shape());
}

void ElevationLayer::setColour(QColor colour)
{
    if (colour_ == colour)
        return;

    // The colour is resolved at paint time, because an invalid value means
    // "use the theme's shadow role" and the theme is not available here.
    colour_ = std::move(colour);
}

QMarginsF ElevationLayer::shadowMargins() const
{
    return shadow_.shadowMargins();
}

QMarginsF ElevationLayer::shadowMargins(const SurfaceContext& context) const
{
    // A throw-away probe is cheaper than mutating the owned drawable from a
    // const query, and it keeps the two margin queries independent.
    drawable::ShadowDrawable probe;
    probe.setElevation(effectiveElevation(context));
    probe.setShape(effectiveShape(context));
    probe.setBlurScale(shadow_.blurScale());
    return probe.shadowMargins();
}

Elevation ElevationLayer::effectiveElevation(const SurfaceContext& context) const noexcept
{
    return normaliseElevation(elevation_.value_or(context.elevation));
}

Shape ElevationLayer::effectiveShape(const SurfaceContext& context) const noexcept
{
    return shape_.value_or(context.shape);
}

QColor ElevationLayer::effectiveColour(const SurfaceContext& context) const
{
    // `shadow` is the dedicated shadow role; both baseline schemes define it
    // as black, so the layer matches ShadowDrawable's default while still
    // respecting a scheme that redefines it.
    return colour_.isValid() ? colour_ : context.theme.colours().shadow;
}

void ElevationLayer::paint(QPainter& painter, const QRectF& bounds,
                           const SurfaceContext& context) const
{
    if (bounds.isEmpty() || !painter.isActive())
        return;

    const Elevation level = effectiveElevation(context);
    if (level == Elevation::Level0)
        return;

    try {
        shadow_.setElevation(level);
        shadow_.setShape(effectiveShape(context));
        shadow_.setColour(effectiveColour(context));

        // Unclipped on purpose: the shadow belongs outside the shape's bounds
        // (framework.md §3.4).
        shadow_.draw(painter, bounds);
    } catch (...) {
        // The shadow is decorative; a mask allocation failure must not abort
        // the background and content stages that follow it.
    }
}

} // namespace md::layer
