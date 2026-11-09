// Periodicity — the drawable base class (framework.md §5.4).
//
// A Drawable paints itself into a rectangle and describes the size it would
// like to occupy. It knows nothing about widgets, layouts or the component it
// is attached to: the same ShapeDrawable backs a button's container, a card's
// background and a dialog's surface.
//
// State and invalidation are the two pieces of bookkeeping every drawable
// shares. `ViewStates` is the interaction bitset from core (framework.md §6.4);
// `revision` is a monotonic stamp that a cache key can include so a raster
// never outlives the configuration that produced it. Every appearance setter
// on a concrete drawable bumps the revision through invalidate(), and so does
// a state change, which means a cache keyed on `cacheKey() + revision()` is
// always correct.
//
// The framework's caching contract (§5.4) lives in the concrete classes: a
// drawable that renders expensively implements cacheKey() and stores its
// raster in QPixmapCache under a key composed of that cache key, the bounds and
// the device pixel ratio.

#pragma once

#include <md/core/view_state.h>

#include <QRectF>
#include <QSizeF>
#include <QtGlobal>

class QPainter;

namespace md::drawable {

class Drawable
{
public:
    virtual ~Drawable() = default;

    Drawable() = default;

    // Paints into `bounds`, in the painter's logical coordinates. The painter
    // is supplied by the caller and may target a QImage with no widget behind
    // it; an implementation saves and restores the painter state it changes.
    virtual void draw(QPainter& painter, const QRectF& bounds) const = 0;

    // The size the drawable would like to have, in logical pixels. An empty
    // size means "no preference": a background or a layer composite fills
    // whatever rectangle it is given, while an icon reports its requested
    // extent so a component can measure it.
    [[nodiscard]] virtual QSizeF intrinsicSize() const { return {}; }

    // Replaces the interaction state. A change notifies onStateChanged() and
    // bumps the revision, so a dependent raster is invalidated even when the
    // owner forgets to call invalidate() itself.
    void setState(ViewStates state);
    [[nodiscard]] ViewStates state() const noexcept { return state_; }

    // Marks the rendered appearance as out of date. The revision increases
    // monotonically; caches include it in their key.
    void invalidate();
    [[nodiscard]] quint64 revision() const noexcept { return revision_; }

    // The identity of everything that affects the rendered result, excluding
    // the rectangle and the device pixel ratio (both are supplied by the
    // caller when it builds a cache key). The default 0 means "not cacheable
    // by identity alone"; subclasses that build rasters override it.
    [[nodiscard]] virtual quint64 cacheKey() const { return 0; }

protected:
    // Called after the state changed, before the revision is bumped. An
    // override that owns a cached raster can drop it here; a state layer
    // simply reads state() at paint time and needs no hook.
    virtual void onStateChanged(ViewStates previous) { Q_UNUSED(previous); }

private:
    ViewStates state_{ViewState::Enabled};
    quint64 revision_ = 0;
};

} // namespace md::drawable
