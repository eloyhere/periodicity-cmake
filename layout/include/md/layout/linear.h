// Periodicity — Linear (framework.md §6.3).
//
// A row or a column of natural-size children. A child with a weight takes a
// share of the *positive* spare space on the main axis, so a weighted run
// always fills the axis and an unweighted one keeps its natural length.
// `gravity` does two things at once: its main-axis flag positions the whole run
// (unweighted runs only) and its cross-axis flag aligns every child across the
// run. `spacing` is the gap between neighbours, never before the first child or
// after the last.
//
// Right-to-left handling is deliberately one conversion at the end: the run is
// computed in logical coordinates — distance from the leading edge — and only
// the finished position is mirrored. A column never mirrors, because Qt does
// not flip the vertical axis. That is also why a vertical run's x coordinate
// comes from the cross-axis alignment and never from the main-axis cursor.

#pragma once

#include <md/layout/alignment.h>
#include <md/view/view_group.h>

#include <QColor>
#include <QEvent>
#include <QHash>
#include <QList>
#include <Qt>

#include <exception>

namespace md {

class Linear : public ViewGroup
{
    Q_OBJECT

public:
    enum class Orientation { Horizontal, Vertical };

    explicit Linear(Orientation orientation = Orientation::Horizontal,
                    QWidget* parent = nullptr)
        : ViewGroup(parent)
        , orientation_(orientation)
    {
        setFocusPolicy(Qt::NoFocus);
        setBackgroundColour(QColor(Qt::transparent));
    }

    [[nodiscard]] Orientation orientation() const noexcept { return orientation_; }

    void setOrientation(Orientation value)
    {
        if (orientation_ == value)
            return;

        orientation_ = value;
        invalidate();
    }

    void setSpacing(qreal value)
    {
        const qreal spacing = layout_detail::length(value);
        if (spacing_ == spacing)
            return;

        spacing_ = spacing;
        invalidate();
    }

    [[nodiscard]] qreal spacing() const noexcept { return spacing_; }

    void setGravity(Qt::Alignment value)
    {
        if (gravity_ == value)
            return;

        gravity_ = value;
        invalidate();
    }

    [[nodiscard]] Qt::Alignment gravity() const noexcept { return gravity_; }

    // The share of the spare main-axis space `child` takes. A negative or
    // non-finite weight is stored as zero, and a child that is not a member is
    // ignored.
    void setWeight(View* child, qreal value)
    {
        if (child == nullptr || !views().contains(child))
            return;

        const qreal weight = layout_detail::length(value);
        if (weights_.contains(child) && weights_.value(child) == weight)
            return;

        if (!weights_.contains(child)) {
            connect(child, &QObject::destroyed, this,
                    [this, child] { weights_.remove(child); });
        }

        weights_[child] = weight;
        invalidate();
    }

    [[nodiscard]] qreal weight(View* child) const { return weights_.value(child); }

protected:
    // A layout direction change is a layout event: the run mirrors without the
    // parent having to measure or arrange again.
    void changeEvent(QEvent* event) override
    {
        ViewGroup::changeEvent(event);
        if (event->type() == QEvent::LayoutDirectionChange)
            invalidate();
    }

    void measureOverride(MeasureSpec width, MeasureSpec height) override
    {
        forgetGoneChildren();

        const bool horizontal = orientation_ == Orientation::Horizontal;

        qreal naturalMain = 0.0;
        qreal naturalCross = 0.0;
        qreal weightTotal = 0.0;
        int count = 0;

        for (View* child : views()) {
            if (child == nullptr || child->isHidden())
                continue;

            // Only the cross axis is capped by the parent: the main axis is
            // what the child is measured freely along.
            child->measure(horizontal ? MeasureSpec::unspecified() : layout_detail::capTo(width),
                           horizontal ? layout_detail::capTo(height)
                                      : MeasureSpec::unspecified());

            const QSizeF size = child->measuredSize();
            naturalMain += horizontal ? size.width() : size.height();
            naturalCross = qMax(naturalCross, horizontal ? size.height() : size.width());
            weightTotal += weight(child);
            ++count;
        }

        if (count > 1)
            naturalMain += spacing_ * static_cast<qreal>(count - 1);

        const MeasureSpec mainSpec = horizontal ? width : height;
        const MeasureSpec crossSpec = horizontal ? height : width;

        // A weighted run owns the main axis the parent offered; an unweighted
        // run asks for exactly what it measured.
        qreal measuredMain = naturalMain;
        if (weightTotal > 0.0 && mainSpec.mode() == MeasureSpec::Mode::Exactly)
            measuredMain = mainSpec.size();
        measuredMain = mainSpec.resolve(measuredMain);

        const qreal measuredCross = crossSpec.resolve(naturalCross);

        setMeasuredSize(horizontal ? QSizeF(measuredMain, measuredCross)
                                   : QSizeF(measuredCross, measuredMain));
    }

    void arrangeChildren(const QRectF& content) override
    {
        // Placing a child calls its own measure()/arrange() hooks, which is
        // user code: one throw must not unwind through Qt's frame.
        try {
            arrangeRun(content);
        } catch (const std::exception& error) {
            layout_detail::reportFailure("Linear::arrangeChildren", error.what());
        } catch (...) {
            layout_detail::reportFailure("Linear::arrangeChildren", nullptr);
        }
    }

private:
    void arrangeRun(const QRectF& content)
    {
        const bool horizontal = orientation_ == Orientation::Horizontal;
        const bool rightToLeft = layoutDirection() == Qt::RightToLeft;

        // The run is laid out in whole device-independent pixels. QRectF's
        // toAlignedRect() rounds both edges outwards, so a run of fractional
        // weighted slots would end one pixel past the axis; a cumulative
        // rounding of the slot boundaries instead makes the slots add up to the
        // content rectangle exactly.
        const QRect bounds = content.toAlignedRect();
        const int extentMain = qMax(0, horizontal ? bounds.width() : bounds.height());
        const int extentCross = qMax(0, horizontal ? bounds.height() : bounds.width());

        // Pass one: natural sizes and the length of the run.
        QList<View*> children;
        QList<qreal> naturals;
        qreal naturalMain = 0.0;
        qreal weightTotal = 0.0;

        for (View* child : views()) {
            if (child == nullptr || child->isHidden())
                continue;

            child->measure(horizontal ? MeasureSpec::unspecified()
                                      : MeasureSpec::atMost(extentCross),
                           horizontal ? MeasureSpec::atMost(extentCross)
                                      : MeasureSpec::unspecified());

            children.append(child);
            const QSizeF size = child->measuredSize();
            const qreal natural = qMax(qreal(0.0), horizontal ? size.width() : size.height());
            naturals.append(natural);
            naturalMain += natural;
            weightTotal += weight(child);
        }

        if (children.isEmpty())
            return;

        // `spacing` is a gap, so it has to be a whole number of pixels as well:
        // the boundary walk below adds it between neighbours only.
        const int gap = qRound(spacing_);
        const int gaps = (children.size() - 1) * gap;
        const qreal naturalChildren = naturalMain;
        naturalMain += spacing_ * static_cast<qreal>(children.size() - 1);

        const qreal spare =
            qMax(qreal(0.0), static_cast<qreal>(extentMain - gaps) - naturalChildren);

        // Pass two: where the run starts. A weighted run fills the axis, so
        // gravity has nothing to move.
        const qreal runOffset = weightTotal > 0.0
                                    ? 0.0
                                    : layout_detail::mainOffset(horizontal, gravity_, extentMain,
                                                                naturalMain);
        const int runOffsetPx = qRound(runOffset);

        const Qt::Alignment physicalGravity =
            layout_detail::physicalAlignment(layoutDirection(), gravity_);

        // Pass three: the main-axis slot of every child, in pixels. A weighted
        // run distributes the *whole* axis (minus the gaps) while it fits, and
        // the cumulative rounding puts the final boundary exactly on the axis
        // end: the sum of the slots plus the gaps equals extentMain, never one
        // pixel less or more. A weight only takes a share of *positive* spare
        // space, so a run whose natural children overflow the axis keeps every
        // natural size and spills over instead of being compressed into it.
        QList<int> mainSlots;
        mainSlots.reserve(children.size());
        if (weightTotal > 0.0) {
            const int allocatable = qMax(0, extentMain - gaps);
            qreal target = 0.0;
            int previous = 0;
            for (int index = 0; index < children.size(); ++index) {
                target += naturals.at(index);
                if (weight(children.at(index)) > 0.0)
                    target += spare * weight(children.at(index)) / weightTotal;

                const int boundary = qRound(target);

                // Cumulative rounding must never take a pixel off a child's
                // measurement: a slot is at least the natural length.
                mainSlots.append(qMax(qRound(naturals.at(index)), boundary - previous));
                previous = boundary;
            }

            // The run fills the axis: give the rounding remainder to the last
            // child that took a weight, so the slots add up to `allocatable`.
            // There is no shared spare to correct while the run overflows
            // (spare is 0), so the fix-up is skipped there — it would otherwise
            // crush one weighted child to make the other ones fit.
            int used = 0;
            for (int slot : mainSlots)
                used += slot;

            if (spare > 0.0) {
                int remainder = allocatable - used;
                for (int index = children.size() - 1; index >= 0 && remainder != 0; --index) {
                    if (weight(children.at(index)) <= 0.0)
                        continue;

                    const int adjusted = mainSlots.at(index) + remainder;
                    if (adjusted < qRound(naturals.at(index)))
                        continue;

                    mainSlots[index] = adjusted;
                    remainder = 0;
                }
            }
        } else {
            for (qreal natural : naturals)
                mainSlots.append(qRound(natural));
        }

        // Pass four: place the children in logical order.
        int cursor = runOffsetPx;
        for (int index = 0; index < children.size(); ++index) {
            View* child = children.at(index);
            const QSizeF measured = child->measuredSize();
            const int main = qMax(0, mainSlots.at(index));

            const qreal crossNatural = horizontal ? measured.height() : measured.width();
            const int cross = qBound(0, qRound(crossNatural), extentCross);

            const int crossOffset = qRound(
                horizontal ? layout_detail::verticalOffset(physicalGravity, extentCross, cross)
                           : layout_detail::horizontalOffset(physicalGravity, extentCross,
                                                             cross));

            int x = 0;
            int y = 0;
            if (horizontal) {
                const int logical = rightToLeft ? extentMain - cursor - main : cursor;
                x = bounds.left() + logical;
                y = bounds.top() + crossOffset;
            } else {
                x = bounds.left() + crossOffset;
                y = bounds.top() + cursor;
            }

            const QRect geometry(x, y, horizontal ? main : cross, horizontal ? cross : main);
            child->measure(MeasureSpec::exactly(geometry.width()),
                           MeasureSpec::exactly(geometry.height()));
            child->arrange(geometry);

            cursor += main + gap;
        }
    }

    void invalidate()
    {
        updateGeometry();
        requestUpdate();
        arrangeChildren(contentRect());
    }

    void forgetGoneChildren()
    {
        if (weights_.isEmpty())
            return;

        const QList<View*> members = views();
        for (auto it = weights_.begin(); it != weights_.end();) {
            if (members.contains(it.key()))
                ++it;
            else
                it = weights_.erase(it);
        }
    }

    Orientation orientation_;
    qreal spacing_ = 0.0;
    Qt::Alignment gravity_ = Qt::AlignLeft | Qt::AlignTop;
    QHash<View*, qreal> weights_;
};

} // namespace md
