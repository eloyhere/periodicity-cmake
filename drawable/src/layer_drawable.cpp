// Periodicity — LayerDrawable (framework.md §5.4).
//
// A thin ordered container: it owns the children and forwards the same
// rectangle to each of them in turn. There is deliberately no clipping and no
// layout, because a drawable composite must behave exactly like a sequence of
// QPainter calls made by hand.

#include <md/drawable/layer_drawable.h>

#include "drawable_hash_p.h"

#include <QPainter>

#include <utility>

namespace md::drawable {

void LayerDrawable::add(std::unique_ptr<Drawable> drawable)
{
    if (drawable == nullptr)
        return;
    children_.push_back(std::move(drawable));
}

void LayerDrawable::clear()
{
    children_.clear();
}

const Drawable* LayerDrawable::at(std::size_t index) const noexcept
{
    return index < children_.size() ? children_[index].get() : nullptr;
}

void LayerDrawable::draw(QPainter& painter, const QRectF& bounds) const
{
    if (!painter.isActive())
        return;

    for (const auto& child : children_) {
        if (child != nullptr)
            child->draw(painter, bounds);
    }
}

quint64 LayerDrawable::cacheKey() const
{
    // Folding in paint order means a reordered composite gets a different key,
    // which is correct: the raster would differ.
    quint64 key = 0;
    bool first = true;
    for (const auto& child : children_) {
        const quint64 childKey = child != nullptr ? child->cacheKey() : 0;
        key = first ? childKey : detail::mix(key, childKey);
        first = false;
    }
    return key;
}

} // namespace md::drawable
