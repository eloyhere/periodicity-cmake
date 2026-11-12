// Periodicity — Frame (framework.md §6.3).
//
// Frame stacks its children: every child is measured and placed in the same
// rectangle, so the paint order decides which one is on top. ViewGroup paints
// children in insertion order, which makes "the later child paints over the
// earlier one" a contract rather than an accident of creation order.
//
// A child without a gravity fills the content rectangle; an explicit gravity
// gives the child its natural size and places it in the nine-cell grid the
// flags describe. AlignLeading and AlignTrailing are logical and mirror in a
// right-to-left frame. `margin` insets the whole grid, which is how a frame
// reserves padding without the View having to own a padding concept.

#pragma once

#include <md/layout/alignment.h>
#include <md/view/view_group.h>

#include <QColor>
#include <QEvent>
#include <QHash>
#include <QList>
#include <Qt>

namespace md {

class Frame : public ViewGroup
{
    Q_OBJECT

public:
    explicit Frame(QWidget* parent = nullptr)
        : ViewGroup(parent)
    {
        setFocusPolicy(Qt::NoFocus);
        setBackgroundColour(QColor(Qt::transparent));
    }

    // Sets how `child` is placed inside the content. A null view, or one that
    // is not a member of this frame, is ignored.
    void setGravity(View* child, Qt::Alignment gravity)
    {
        if (child == nullptr || !views().contains(child))
            return;

        if (gravity_.contains(child) && gravity_.value(child) == gravity)
            return;

        if (!gravity_.contains(child)) {
            connect(child, &QObject::destroyed, this,
                    [this, child] { gravity_.remove(child); });
        }

        gravity_[child] = gravity;
        requestUpdate();
        arrangeChildren(contentRect());
    }

    [[nodiscard]] Qt::Alignment gravity(View* child) const
    {
        return gravity_.value(child);
    }

    // Uniform inset between the content rectangle and the children.
    void setMargin(qreal margin)
    {
        const qreal value = layout_detail::length(margin);
        if (margin_ == value)
            return;

        margin_ = value;
        updateGeometry();
        requestUpdate();
        arrangeChildren(contentRect());
    }

    [[nodiscard]] qreal margin() const noexcept { return margin_; }

protected:
    // Leading and trailing mirror with the direction, so a direction change has
    // to re-place the children rather than wait for the next measure pass.
    void changeEvent(QEvent* event) override
    {
        ViewGroup::changeEvent(event);
        if (event->type() == QEvent::LayoutDirectionChange) {
            requestUpdate();
            arrangeChildren(contentRect());
        }
    }

    void measureOverride(MeasureSpec width, MeasureSpec height) override
    {
        forgetGoneChildren();

        const qreal inset = 2.0 * margin_;
        qreal naturalWidth = 0.0;
        qreal naturalHeight = 0.0;

        for (View* child : views()) {
            if (child == nullptr || child->isHidden())
                continue;

            // A stacked child is capped, never forced: an exactly-sized frame
            // must not make every child report the frame's own size.
            child->measure(layout_detail::capTo(width, inset),
                           layout_detail::capTo(height, inset));

            const QSizeF size = child->measuredSize();
            naturalWidth = qMax(naturalWidth, size.width());
            naturalHeight = qMax(naturalHeight, size.height());
        }

        setMeasuredSize(QSizeF(width.resolve(naturalWidth + inset),
                               height.resolve(naturalHeight + inset)));
    }

    void arrangeChildren(const QRectF& content) override
    {
        // The nine-cell grid is resolved in whole pixels: a fractional margin
        // used to round every child outwards with toAlignedRect(), which made a
        // trailing-aligned child one pixel larger than it measured and pushed it
        // past the frame's edge.
        const QRect bounds = content.toAlignedRect();
        const int inset = qRound(margin_);
        const int left = bounds.left() + inset;
        const int top = bounds.top() + inset;
        const int availableWidth = qMax(0, bounds.width() - 2 * inset);
        const int availableHeight = qMax(0, bounds.height() - 2 * inset);

        for (View* child : views()) {
            if (child == nullptr || child->isHidden())
                continue;

            // `logical` decides whether an axis fills, `physical` where the
            // child sits on that axis.
            const Qt::Alignment logical = gravity_.value(child);
            const Qt::Alignment physical =
                layout_detail::physicalAlignment(layoutDirection(), logical);

            child->measure(MeasureSpec::atMost(availableWidth),
                           MeasureSpec::atMost(availableHeight));

            const QSizeF measured = child->measuredSize();
            const int width = (logical & Qt::AlignHorizontal_Mask)
                                  ? qBound(0, qRound(measured.width()), availableWidth)
                                  : availableWidth;
            const int height = (logical & Qt::AlignVertical_Mask)
                                   ? qBound(0, qRound(measured.height()), availableHeight)
                                   : availableHeight;

            const int x = left
                + qRound(layout_detail::horizontalOffset(physical, availableWidth, width));
            const int y = top
                + qRound(layout_detail::verticalOffset(physical, availableHeight, height));

            const QRect geometry(x, y, width, height);
            child->measure(MeasureSpec::exactly(width), MeasureSpec::exactly(height));
            child->arrange(geometry);
        }
    }

private:
    // Drops the gravity of children that left the group, so a removed child
    // never leaves a stale key behind.
    void forgetGoneChildren()
    {
        if (gravity_.isEmpty())
            return;

        const QList<View*> members = views();
        for (auto it = gravity_.begin(); it != gravity_.end();) {
            if (members.contains(it.key()))
                ++it;
            else
                it = gravity_.erase(it);
        }
    }

    QHash<View*, Qt::Alignment> gravity_;
    qreal margin_ = 0.0;
};

} // namespace md
