// Periodicity — Constraint (framework.md §6.3).
//
// Parent-edge constraints, not a general solver: a child may pin its start,
// end, top or bottom edge to the parent's content rectangle, and may ask to be
// centred on an axis. Two opposing anchors stretch the child across the space
// between them; one anchor positions a natural-size child; no anchor at all
// leaves the child at its natural size in the top-leading corner.
//
// start and end are logical. In a right-to-left container they mirror, so the
// same constraint description produces a mirrored layout; top and bottom never
// mirror because Qt does not flip the vertical axis.
//
// Invalid values are rejected at set time rather than clamped at layout time: a
// negative or non-finite anchor would silently produce a negative rectangle, so
// setConstraints() returns false and the previous anchors stay in place.

#pragma once

#include <md/layout/alignment.h>
#include <md/view/view_group.h>

#include <QColor>
#include <QEvent>
#include <QHash>
#include <QList>
#include <Qt>

#include <optional>

namespace md {

class Constraint : public ViewGroup
{
    Q_OBJECT

public:
    struct Anchors
    {
        // Distances from the parent's content edges, in logical pixels.
        std::optional<qreal> start;
        std::optional<qreal> end;
        std::optional<qreal> top;
        std::optional<qreal> bottom;

        // Centring applies only to an axis without an anchor: an explicit
        // anchor always wins.
        bool centerHorizontal = false;
        bool centerVertical = false;

        [[nodiscard]] friend bool operator==(const Anchors& lhs, const Anchors& rhs) = default;
    };

    explicit Constraint(QWidget* parent = nullptr)
        : ViewGroup(parent)
    {
        setFocusPolicy(Qt::NoFocus);
        setBackgroundColour(QColor(Qt::transparent));
    }

    // Installs `anchors` for `child`. Returns false and changes nothing when
    // `child` is not a member or when an anchor is negative or not finite.
    bool setConstraints(View* child, const Anchors& anchors)
    {
        if (child == nullptr || !views().contains(child))
            return false;

        if (!isValid(anchors))
            return false;

        if (anchors_.contains(child) && anchors_.value(child) == anchors)
            return true;

        if (!anchors_.contains(child)) {
            connect(child, &QObject::destroyed, this,
                    [this, child] { anchors_.remove(child); });
        }

        anchors_[child] = anchors;
        updateGeometry();
        requestUpdate();
        arrangeChildren(contentRect());
        return true;
    }

    [[nodiscard]] Anchors constraints(View* child) const { return anchors_.value(child); }

    [[nodiscard]] bool hasConstraints(View* child) const { return anchors_.contains(child); }

    void clearConstraints(View* child)
    {
        if (!anchors_.remove(child))
            return;

        updateGeometry();
        requestUpdate();
        arrangeChildren(contentRect());
    }

protected:
    // start and end are logical, so a direction change mirrors every child.
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

        qreal desiredWidth = 0.0;
        qreal desiredHeight = 0.0;

        for (View* child : views()) {
            if (child == nullptr || child->isHidden())
                continue;

            const Anchors anchors = constraints(child);
            child->measure(horizontalSpec(anchors, width), verticalSpec(anchors, height));

            const QSizeF size = child->measuredSize();
            desiredWidth = qMax(desiredWidth,
                                size.width() + anchors.start.value_or(0.0)
                                    + anchors.end.value_or(0.0));
            desiredHeight = qMax(desiredHeight,
                                 size.height() + anchors.top.value_or(0.0)
                                     + anchors.bottom.value_or(0.0));
        }

        setMeasuredSize(QSizeF(width.resolve(desiredWidth), height.resolve(desiredHeight)));
    }

    void arrangeChildren(const QRectF& content) override
    {
        const bool rightToLeft = layoutDirection() == Qt::RightToLeft;

        // Anchors, centring and the mirror are all resolved in whole pixels:
        // centring a child on an odd leftover used to produce a fractional left
        // edge that toAlignedRect() then rounded outwards, so the centred child
        // was one pixel wider than the space it had been measured against.
        const QRect bounds = content.toAlignedRect();
        const int width = qMax(0, bounds.width());
        const int height = qMax(0, bounds.height());

        for (View* child : views()) {
            if (child == nullptr || child->isHidden())
                continue;

            const Anchors anchors = constraints(child);

            const int insetStart = qRound(anchors.start.value_or(0.0));
            const int insetEnd = qRound(anchors.end.value_or(0.0));
            const int insetTop = qRound(anchors.top.value_or(0.0));
            const int insetBottom = qRound(anchors.bottom.value_or(0.0));

            const int availableWidth = qMax(0, width - insetStart - insetEnd);
            const int availableHeight = qMax(0, height - insetTop - insetBottom);

            // Two opposing anchors stretch; one anchor lets the child choose.
            const MeasureSpec widthSpec = anchors.start && anchors.end
                                              ? MeasureSpec::exactly(availableWidth)
                                              : MeasureSpec::atMost(availableWidth);
            const MeasureSpec heightSpec = anchors.top && anchors.bottom
                                               ? MeasureSpec::exactly(availableHeight)
                                               : MeasureSpec::atMost(availableHeight);

            child->measure(widthSpec, heightSpec);

            const QSizeF measured = child->measuredSize();
            const int childWidth = qBound(0, qRound(measured.width()), availableWidth);
            const int childHeight = qBound(0, qRound(measured.height()), availableHeight);

            // Logical position: start and end are physical left and right in a
            // left-to-right container and are mirrored below.
            int x = 0;
            if (anchors.start)
                x = insetStart;
            else if (anchors.end)
                x = width - insetEnd - childWidth;
            else if (anchors.centerHorizontal)
                x = (width - childWidth) / 2;

            int y = 0;
            if (anchors.top)
                y = insetTop;
            else if (anchors.bottom)
                y = height - insetBottom - childHeight;
            else if (anchors.centerVertical)
                y = (height - childHeight) / 2;

            // A centred child is already symmetric: mirroring it again would
            // move it one pixel whenever the leftover is odd.
            const bool centredHorizontally =
                anchors.centerHorizontal && !anchors.start && !anchors.end;
            if (rightToLeft && !centredHorizontally)
                x = width - x - childWidth;

            const QRect geometry(bounds.left() + x, bounds.top() + y, childWidth, childHeight);
            child->measure(MeasureSpec::exactly(childWidth), MeasureSpec::exactly(childHeight));
            child->arrange(geometry);
        }
    }

private:
    static bool isValid(const Anchors& anchors)
    {
        for (const std::optional<qreal>& value :
             {anchors.start, anchors.end, anchors.top, anchors.bottom}) {
            if (!value.has_value())
                continue;
            if (!(*value >= 0.0) || *value > 1.0e9)
                return false;
        }
        return true;
    }

    // The spec a child is measured with on the horizontal axis. An unspecified
    // parent cannot resolve a stretch, so the child measures itself freely and
    // the arrange pass deals with whatever geometry the parent ends up with.
    static MeasureSpec horizontalSpec(const Anchors& anchors, MeasureSpec parent)
    {
        if (parent.mode() == MeasureSpec::Mode::Unspecified)
            return parent;

        const qreal inset = anchors.start.value_or(0.0) + anchors.end.value_or(0.0);
        const qreal available = qMax(qreal(0.0), parent.size() - inset);
        return anchors.start && anchors.end ? MeasureSpec::exactly(available)
                                            : MeasureSpec::atMost(available);
    }

    static MeasureSpec verticalSpec(const Anchors& anchors, MeasureSpec parent)
    {
        if (parent.mode() == MeasureSpec::Mode::Unspecified)
            return parent;

        const qreal inset = anchors.top.value_or(0.0) + anchors.bottom.value_or(0.0);
        const qreal available = qMax(qreal(0.0), parent.size() - inset);
        return anchors.top && anchors.bottom ? MeasureSpec::exactly(available)
                                             : MeasureSpec::atMost(available);
    }

    void forgetGoneChildren()
    {
        if (anchors_.isEmpty())
            return;

        const QList<View*> members = views();
        for (auto it = anchors_.begin(); it != anchors_.end();) {
            if (members.contains(it.key()))
                ++it;
            else
                it = anchors_.erase(it);
        }
    }

    QHash<View*, Anchors> anchors_;
};

} // namespace md
