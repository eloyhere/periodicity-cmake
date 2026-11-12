// Periodicity — alignment and measurement helpers for the layout containers
// (framework.md §6.3).
//
// Every container answers the same three questions: how does a logical gravity
// become a physical one, where does a child of a given size sit on an axis, and
// how does a parent's MeasureSpec become a bound for a child. Answering them in
// one place keeps the containers from drifting apart.
//
// QStyle::visualAlignment cannot be used for the first question: for an empty
// alignment it invents AlignLeft, which would turn the containers' "no flag
// means fill" rule into "align to the left". The helpers below only ever mirror
// flags that are actually present.
//
// Two coordinate systems appear below. The *physical* one is Qt's: x grows to
// the right. The *logical* one counts from the leading edge, so a leading inset
// is the same number in a left-to-right and in a right-to-left widget; the two
// are converted at the last moment with physicalFromLeading().

#pragma once

#include <md/core/measure_spec.h>

#include <Qt>
#include <QtGlobal>

namespace md::layout_detail {

// The positional flags of `alignment`, mirrored when `direction` is
// right-to-left. Alignment flags that are absent stay absent, which is what
// lets a container treat an axis without a flag as "fill the axis".
//
// Qt::AlignAbsolute opts out of the mirroring, so a caller that needs a
// physically fixed side can ask for one.
[[nodiscard]] inline Qt::Alignment physicalAlignment(Qt::LayoutDirection direction,
                                                     Qt::Alignment alignment) noexcept
{
    Qt::Alignment result = alignment & (Qt::AlignLeft | Qt::AlignRight | Qt::AlignHCenter
                                        | Qt::AlignTop | Qt::AlignBottom | Qt::AlignVCenter);

    if (direction == Qt::RightToLeft && !alignment.testFlag(Qt::AlignAbsolute)) {
        const bool left = result.testFlag(Qt::AlignLeft);
        const bool right = result.testFlag(Qt::AlignRight);
        if (left != right) {
            result &= ~(Qt::AlignLeft | Qt::AlignRight);
            result |= left ? Qt::AlignRight : Qt::AlignLeft;
        }
    }

    return result;
}

// Offset of a `size`-long child on a horizontal axis `extent` long, for an
// already physical alignment. An absent flag yields zero, so the caller decides
// whether the axis fills.
[[nodiscard]] inline qreal horizontalOffset(Qt::Alignment alignment, qreal extent,
                                            qreal size) noexcept
{
    if (alignment.testFlag(Qt::AlignHCenter))
        return (extent - size) / 2.0;
    if (alignment.testFlag(Qt::AlignRight))
        return extent - size;
    return 0.0;
}

// Vertical counterpart of horizontalOffset(). The vertical axis never mirrors.
[[nodiscard]] inline qreal verticalOffset(Qt::Alignment alignment, qreal extent,
                                          qreal size) noexcept
{
    if (alignment.testFlag(Qt::AlignVCenter))
        return (extent - size) / 2.0;
    if (alignment.testFlag(Qt::AlignBottom))
        return extent - size;
    return 0.0;
}

// Offset of a `size`-long run on the main axis of a row or a column, for a
// *logical* alignment: AlignLeft is the start of a row, AlignTop the start of a
// column. A row is mirrored by the caller, so this always returns a logical
// distance from the leading edge.
[[nodiscard]] inline qreal mainOffset(bool horizontal, Qt::Alignment alignment,
                                     qreal extent, qreal size) noexcept
{
    return horizontal ? horizontalOffset(alignment, extent, size)
                      : verticalOffset(alignment, extent, size);
}

// Converts a logical distance from the leading edge into a physical x inside an
// `extent`-wide box.
[[nodiscard]] inline qreal physicalFromLeading(qreal logicalX, qreal size, qreal extent,
                                               bool rightToLeft) noexcept
{
    return rightToLeft ? extent - logicalX - size : logicalX;
}

// The bound a parent's spec hands to a stacked child: an axis that was resolved
// for the parent caps the child instead of forcing it. `inset` is subtracted
// from the bound so a container's own margins do not leak into the child.
[[nodiscard]] inline MeasureSpec capTo(MeasureSpec spec, qreal inset = 0.0) noexcept
{
    if (spec.mode() == MeasureSpec::Mode::Unspecified)
        return spec;

    const qreal bound = spec.size() - inset;
    return MeasureSpec::atMost(bound > 0.0 ? bound : 0.0);
}

// A finite, non-negative length below the 1e9 geometry cap. Used wherever a
// public setter accepts a qreal that ends up as geometry. Zero is a legal
// length — a zero-height bar, a collapsed header, a run without spacing — so
// only negative, non-finite and absurdly large values fall back; a NaN fails
// the comparison and takes the fallback too.
[[nodiscard]] inline qreal length(qreal value, qreal fallback = 0.0) noexcept
{
    return value >= 0.0 && value < 1.0e9 ? value : fallback;
}

// Reports an exception that escaped a container hook. A container's
// measure/arrange/paint override isolates its body with try/catch — a child's
// hook is user code and a geometry mistake must not unwind through Qt's frames
// — and calls this from the handler. The hook that failed is named so the
// warning points at a container rather than at a line in Qt.
inline void reportFailure(const char* where, const char* what)
{
    qWarning("md::layout: %s failed: %s", where,
             what != nullptr ? what : "unknown exception");
}

} // namespace md::layout_detail
