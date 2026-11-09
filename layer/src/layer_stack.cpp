// Periodicity — LayerStack (framework.md §5.5, §6.5).
//
// The stack is intentionally the only place that knows the ordering rules:
// paint forward, hit-test backward. Everything else is delegated, so adding a
// new stage never changes the traversal.

#include <md/layer/layer_stack.h>

#include <md/layer/surface_context.h>

#include <QPainter>

#include <algorithm>
#include <utility>

namespace md::layer {

void LayerStack::add(std::unique_ptr<Layer> layer)
{
    if (layer == nullptr)
        return;
    layers_.push_back(std::move(layer));
}

void LayerStack::insert(std::size_t index, std::unique_ptr<Layer> layer)
{
    if (layer == nullptr)
        return;

    // Clamping rather than throwing matches the rest of the library: a layout
    // computed from a theme or a size class may legitimately run past the end.
    if (index > layers_.size())
        index = layers_.size();

    layers_.insert(layers_.begin() + static_cast<std::ptrdiff_t>(index),
                   std::move(layer));
}

void LayerStack::clear() noexcept
{
    layers_.clear();
}

const Layer* LayerStack::at(std::size_t index) const noexcept
{
    return index < layers_.size() ? layers_[index].get() : nullptr;
}

void LayerStack::paint(QPainter& painter, const QRectF& bounds,
                       const SurfaceContext& context) const
{
    if (!painter.isActive())
        return;

    for (const auto& layer : layers_) {
        if (layer != nullptr)
            layer->paint(painter, bounds, context);
    }
}

void LayerStack::advance(std::chrono::milliseconds elapsed)
{
    for (const auto& layer : layers_) {
        if (layer != nullptr && layer->isAnimated())
            layer->advance(elapsed);
    }
}

bool LayerStack::hitTest(const QPointF& point) const
{
    // Reverse order: the topmost layer that claims the point wins, and the
    // layers below it are not consulted (framework.md §6.5).
    for (auto it = layers_.rbegin(); it != layers_.rend(); ++it) {
        const auto& layer = *it;
        if (layer != nullptr && layer->hitTest(point))
            return true;
    }
    return false;
}

bool LayerStack::anyAnimated() const noexcept
{
    return std::any_of(layers_.begin(), layers_.end(),
                       [](const std::unique_ptr<Layer>& layer) {
                           return layer != nullptr && layer->isAnimated();
                       });
}

} // namespace md::layer
