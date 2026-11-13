// Periodicity — accumulated repaint damage for one view.
//
// DamageTracker is the bookkeeping half of damage-aware invalidation: a view
// records the rectangles that changed, in a coordinate space the caller states
// (for View that is the widget-local rectangle), and paintContent() can read
// the coalesced result instead of repainting everything.
//
// It is a plain value type: no QObject, no signals, no allocation until the
// first rectangle is retained, and a bounded number of retained rectangles
// (kMaxRects), so a burst of small updates cannot grow the list without bound.
//
// The coalescing policy is deliberately conservative — a damaged region may
// cover more than the caller asked for, never less:
//
//   * overlapping and edge-touching rectangles merge into their union, so a
//     caret and the glyph run beside it become one strip;
//   * once more than kMaxRects rectangles would be retained, all of them
//     collapse into their bounding rectangle;
//   * when the accumulated area exceeds kFullRectRatio of fullRect(), the
//     damage collapses to fullRect() itself, the cheaper description of a
//     storm of scattered updates.
//
// The tracker never clips additions to fullRect(): a caller may legitimately
// damage an overhang (a shadow, a focus ring), and the paint path clips the
// final result anyway. fullRect() is only the reference that addAll() and the
// area threshold use.
//
// Empty, invalid and non-finite rectangles are ignored, so a NaN from a
// division somewhere upstream cannot poison the accumulated bounds. Stored
// rectangles are always normalized (positive width and height).

#pragma once

#include <QList>
#include <QRectF>
#include <QtGlobal>

namespace md {

class DamageTracker
{
public:
    // The largest number of rectangles retained before they collapse into
    // their bounding rectangle. Sixteen keeps the per-paint loop short while
    // still describing a typical "a few separate strips changed" frame.
    static constexpr int kMaxRects = 16;

    // The accumulated-area fraction of fullRect() above which the tracker
    // collapses to a full repaint. "More than 60 % of the bounds" is the point
    // where the bookkeeping costs more than simply repainting the view.
    static constexpr qreal kFullRectRatio = 0.6;

    DamageTracker() = default;

    // Records one damaged rectangle. Ignored when the rectangle is empty,
    // invalid or non-finite, and when the tracker already covers fullRect().
    void add(const QRectF& rect);

    // Marks the whole of fullRect() as damaged. Does nothing until
    // setFullRect() has supplied a usable rectangle.
    void addAll();

    // Drops every retained rectangle. fullRect() is kept.
    void clear();

    // Sets the reference rectangle used by addAll() and by the area threshold.
    // An inverted rectangle is normalized; an empty, invalid or non-finite one
    // disables both (there is nothing to mark as "everything"). Changing the
    // rectangle while the tracker is full re-targets the full coverage.
    void setFullRect(const QRectF& bounds);

    // True when no rectangle is retained. A full tracker is not empty.
    [[nodiscard]] bool isEmpty() const noexcept { return rects_.isEmpty(); }

    // True when the whole of fullRect() is marked damaged.
    [[nodiscard]] bool isFull() const noexcept { return full_; }

    // The union of the retained rectangles; an empty rectangle when nothing is
    // damaged.
    [[nodiscard]] QRectF bounds() const noexcept;

    // The coalesced rectangles. A merged rectangle moves to the end of the
    // list; a full tracker reports exactly one rectangle, fullRect().
    [[nodiscard]] QList<QRectF> rects() const;

    [[nodiscard]] QRectF fullRect() const noexcept { return fullRect_; }

private:
    // Replaces the list with its bounding rectangle. Used when the cap is hit.
    void collapseToBounding();

    // Replaces the list with fullRect() and sets the full flag.
    void collapseToFull();

    QList<QRectF> rects_;
    QRectF fullRect_;
    bool full_ = false;
};

} // namespace md
