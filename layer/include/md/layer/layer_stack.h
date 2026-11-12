// Periodicity — the ordered layer stack (framework.md §5.5, §6.5).
//
// A LayerStack owns layers in paint order: index 0 paints first (the
// elevation/shadow stage), the background follows, the component's content is
// painted by the host between the background and the foreground layers, and
// the foreground layers paint last. The stack itself does not know about that
// split; it only guarantees that paint() is in insertion order and hitTest()
// is in reverse order (framework.md §6.5).
//
// The stack is a value type that owns its layers: moving it is cheap, copying
// it is deleted because a layer is a unique object.
//
// MUTATION RULE. A layer's paint() callback must not add, insert or clear
// layers. Those calls are ignored while paint() is running, and a re-entrant
// paint() is ignored as well, so the paint traversal can never be invalidated
// from inside itself. advance() and hitTest() are not guarded: their callbacks
// must not mutate the stack either.

#pragma once

#include <md/layer/layer.h>

#include <QPointF>
#include <QRectF>

#include <chrono>
#include <cstddef>
#include <memory>
#include <vector>

class QPainter;

namespace md::layer {

struct SurfaceContext;

class LayerStack
{
public:
    LayerStack() = default;
    ~LayerStack() = default;

    LayerStack(const LayerStack&) = delete;
    LayerStack& operator=(const LayerStack&) = delete;
    LayerStack(LayerStack&&) noexcept = default;
    LayerStack& operator=(LayerStack&&) noexcept = default;

    // Appends a layer at the top of the paint order. A null pointer is
    // ignored, so an optional layer needs no branch at the call site.
    void add(std::unique_ptr<Layer> layer);

    // Inserts at `index`; an index past the end appends. A null pointer is
    // ignored.
    void insert(std::size_t index, std::unique_ptr<Layer> layer);

    // Removes and destroys every layer.
    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return layers_.size(); }
    [[nodiscard]] bool empty() const noexcept { return layers_.empty(); }

    // The layer at `index`, or nullptr when out of range. The pointer stays
    // valid until the stack is modified.
    [[nodiscard]] const Layer* at(std::size_t index) const noexcept;

    // Paints every layer in insertion order with the same bounds and context.
    // Each stage is isolated: an exception thrown by one layer is caught and
    // the remaining layers still paint. Mutating the stack from a paint
    // callback is ignored for the duration of the call.
    void paint(QPainter& painter, const QRectF& bounds,
               const SurfaceContext& context) const;

    // Advances every layer that wants frames. A stack with no animated layer
    // can skip its timer; anyAnimated() reports whether one exists.
    void advance(std::chrono::milliseconds elapsed);

    // Asks the layers in reverse paint order and returns true at the first
    // claimant, so the topmost layer wins (framework.md §6.5).
    [[nodiscard]] bool hitTest(const QPointF& point) const;

    [[nodiscard]] bool anyAnimated() const noexcept;

private:
    std::vector<std::unique_ptr<Layer>> layers_;

    // True while paint() walks layers_. Mutators ignore a call made in that
    // window so the vector cannot be cleared or reallocated under the
    // traversal; paint() ignores a re-entrant call for the same reason.
    // Mutable because paint() is const.
    mutable bool painting_ = false;
};

} // namespace md::layer
