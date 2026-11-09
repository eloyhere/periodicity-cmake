// Periodicity — an ordered composite of drawables (framework.md §5.4).
//
// LayerDrawable is the drawable counterpart of layer::LayerStack: it owns
// children and paints them in insertion order, so a component can build a
// background out of a tonal fill, a shadow and an icon without knowing how
// each is implemented.
//
// The composite does not redistribute the rectangle: every child receives the
// same bounds, exactly as a QPainter would with sequential draw calls. A child
// that wants an inset is responsible for it (or is wrapped by a
// future InsetDrawable).
//
// cacheKey() folds the children's cache keys in order, which makes a
// composite of cacheable children cacheable itself. Reordering the children
// changes the key, as it changes the rendering.

#pragma once

#include <md/drawable/drawable.h>

#include <cstddef>
#include <memory>
#include <vector>

class QPainter;
class QRectF;

namespace md::drawable {

class LayerDrawable : public Drawable
{
public:
    LayerDrawable() = default;

    // Appends a child at the top of the paint order. A null pointer is
    // ignored, so a caller can forward an optional child without a branch.
    void add(std::unique_ptr<Drawable> drawable);

    // Removes every child. The children are destroyed.
    void clear();

    [[nodiscard]] std::size_t size() const noexcept { return children_.size(); }
    [[nodiscard]] bool empty() const noexcept { return children_.empty(); }

    // The child at `index`, or nullptr when the index is out of range. The
    // reference stays valid until the composite is modified.
    [[nodiscard]] const Drawable* at(std::size_t index) const noexcept;

    void draw(QPainter& painter, const QRectF& bounds) const override;

    // Combines the children's cache keys in paint order. An empty composite
    // returns 0.
    [[nodiscard]] quint64 cacheKey() const override;

private:
    std::vector<std::unique_ptr<Drawable>> children_;
};

} // namespace md::drawable
