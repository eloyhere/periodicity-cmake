// Periodicity — the container kernel (framework.md §5.7, §6.3, §6.5).
//
// ViewGroup adds children to View. It does not replace QLayout: a group is
// arranged by the layout bridge, and it arranges its children through their
// own measure()/arrange() entry points rather than by calling setGeometry() on
// them behind the framework's back.
//
// Children are real QWidgets parented to the group, so Qt's focus chain, input
// delivery and accessibility tree see them. They are also painted explicitly by
// paintChildren() in insertion order, which is what makes "the second child
// paints over the first" a contract rather than an accident of creation order.

#pragma once

#include <md/view/view.h>

#include <QList>

#include <vector>

namespace md {

// A View that owns child Views.
//
// Subclasses implement arrangeChildren(); measureOverride() stays theirs as
// well, because a container's size policy is its own. paintContent() is a
// no-op: a group has no content besides its children.
class ViewGroup : public View
{
    Q_OBJECT

public:
    explicit ViewGroup(QWidget* parent = nullptr);
    ~ViewGroup() override;

    // Reparents `view` to this group and appends it to the paint order. The
    // pointer stays owned by Qt's widget tree; the group never deletes it.
    // Adding a child that is already present is a no-op.
    void addView(View* view);

    // Removes `view` from the paint order and detaches it from the group. The
    // caller keeps ownership of the widget.
    void removeView(View* view);

    // The children in paint order.
    [[nodiscard]] QList<View*> views() const;

protected:
    // A group has no content of its own; the children are the content.
    void paintContent(QPainter& painter, const QRectF& content) override;

    // Bridges View's arrange hook to arrangeChildren(): the base class hands
    // the content rectangle to this override, which computes where the
    // children belong.
    void arrangeOverride(const QRectF& content) override;

    // Paints every visible child in insertion order, after the parent's
    // content and before its foreground stages (framework.md §6.5).
    void paintChildren(QPainter& painter) override;

    // Places the children inside `content`. Still abstract: a group that does
    // not know its own arrangement is not a container. Implementations call
    // child->arrange() with the rectangles they computed.
    virtual void arrangeChildren(const QRectF& content) = 0;

private:
    std::vector<View*> views_;
};

} // namespace md
