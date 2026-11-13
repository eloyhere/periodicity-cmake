// Periodicity — DamageTracker (md/view/damage_tracker.h).
//
// The implementation is intentionally small and copyable: a QList of at most
// kMaxRects rectangles plus the reference rectangle. add() is the only path
// that grows the list, and every growth is followed by the two collapse rules,
// so the retained count and the retained area are both bounded by construction.

#include <md/view/damage_tracker.h>

#include <QtGlobal>
#include <QtNumeric>

namespace md {
namespace {

// A rectangle is storable only when it has a positive, finite extent. QRectF
// itself accepts NaN in a coordinate, and a NaN would poison every later union,
// so finiteness is checked coordinate by coordinate before validity.
[[nodiscard]] bool isTrackable(const QRectF& rect) noexcept
{
    if (!qIsFinite(rect.x()) || !qIsFinite(rect.y())
        || !qIsFinite(rect.width()) || !qIsFinite(rect.height())) {
        return false;
    }

    return rect.isValid() && !rect.isEmpty();
}

// True when the two rectangles overlap or share an edge. QRectF::intersects()
// excludes edge contact, but a caret and the run of text beside it are one
// damaged strip, so touching rectangles must merge as well.
[[nodiscard]] bool touches(const QRectF& a, const QRectF& b) noexcept
{
    return !(a.right() < b.left() || b.right() < a.left()
             || a.bottom() < b.top() || b.bottom() < a.top());
}

[[nodiscard]] qreal areaOf(const QRectF& rect) noexcept
{
    return rect.width() * rect.height();
}

} // namespace

void DamageTracker::add(const QRectF& rect)
{
    if (!isTrackable(rect) || full_)
        return;

    // Merge the new rectangle with every rectangle it overlaps or touches.
    // The loop restarts after each merge because one new rectangle can bridge
    // two previously separate pieces; the fixpoint is reached after at most
    // one pass per retained rectangle.
    QRectF merged = rect;
    bool mergedAny = true;
    while (mergedAny) {
        mergedAny = false;

        for (int index = 0; index < static_cast<int>(rects_.size());) {
            if (!touches(merged, rects_.at(index))) {
                ++index;
                continue;
            }

            merged = merged.united(rects_.at(index));
            rects_.removeAt(index);
            mergedAny = true;
        }
    }

    rects_.append(merged);

    // Rule 1: a bounded list. The bounding rectangle is a conservative
    // description of every retained rectangle, so no damage is lost.
    if (static_cast<int>(rects_.size()) > kMaxRects)
        collapseToBounding();

    // Rule 2: a bounded description of a scattered storm. The retained
    // rectangles are pairwise non-touching, so their areas do not overlap and
    // the sum is the exact accumulated area. Without a usable fullRect there is
    // no reference area, and the rule stays inert.
    if (!full_ && isTrackable(fullRect_)) {
        qreal damagedArea = 0.0;
        for (const QRectF& retained : rects_)
            damagedArea += areaOf(retained);

        if (damagedArea > kFullRectRatio * areaOf(fullRect_))
            collapseToFull();
    }
}

void DamageTracker::addAll()
{
    if (full_ || !isTrackable(fullRect_))
        return;

    collapseToFull();
}

void DamageTracker::clear()
{
    rects_.clear();
    full_ = false;
}

void DamageTracker::setFullRect(const QRectF& bounds)
{
    // Normalizing before the finiteness check lets a caller pass an inverted
    // rectangle; an empty or NaN one still disables the full-rect rules.
    const QRectF normalized = bounds.normalized();
    const QRectF usable = isTrackable(normalized) ? normalized : QRectF();

    if (usable == fullRect_)
        return;

    fullRect_ = usable;

    // A full tracker describes "everything in fullRect"; when the reference
    // moves, the retained full rectangle must follow it or stop claiming
    // completeness.
    if (full_) {
        rects_.clear();
        if (isTrackable(fullRect_))
            rects_.append(fullRect_);
        else
            full_ = false;
    }
}

QRectF DamageTracker::bounds() const noexcept
{
    if (rects_.isEmpty())
        return QRectF();

    QRectF result = rects_.first();
    for (qsizetype index = 1; index < rects_.size(); ++index)
        result = result.united(rects_.at(index));

    return result;
}

QList<QRectF> DamageTracker::rects() const
{
    return rects_;
}

void DamageTracker::collapseToBounding()
{
    if (rects_.isEmpty())
        return;

    const QRectF bounding = bounds();
    rects_.clear();
    rects_.append(bounding);
}

void DamageTracker::collapseToFull()
{
    if (!isTrackable(fullRect_))
        return;

    rects_.clear();
    rects_.append(fullRect_);
    full_ = true;
}

} // namespace md
