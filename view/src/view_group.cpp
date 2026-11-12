// Periodicity — the container kernel (framework.md §5.7, §6.3, §6.5).
//
// A ViewGroup is a View plus an ordered list of child Views. The list is the
// paint order and the ownership stays with Qt's widget tree: reparenting is the
// only ownership transfer, and removeView() hands the widget back.
//
// Children are painted twice by necessity. paintChildren() renders each child
// into the parent's painter so that the parent's foreground stages really do
// paint above it (framework.md §6.5); Qt's backing store then composites the
// child again as a normal widget. For an opaque child the second pass is
// idempotent; the alternative — hiding the child from Qt — would break its
// input handling, focus chain and accessibility.

#include <md/view/view_group.h>

#include <QPainter>
#include <QPointer>
#include <QRegion>

#include <algorithm>

namespace md {

ViewGroup::ViewGroup(QWidget* parent)
    : View(parent)
{
}

ViewGroup::~ViewGroup()
{
    // The children are deleted by QWidget after this class's members are gone.
    // Disconnecting first keeps their destroyed() notification from touching a
    // vector that no longer exists.
    for (View* view : views_) {
        if (view != nullptr)
            QObject::disconnect(view, &QObject::destroyed, this, nullptr);
    }
    views_.clear();
}

void ViewGroup::addView(View* view)
{
    if (view == nullptr || view == this)
        return;

    if (std::find(views_.begin(), views_.end(), view) != views_.end())
        return;

    // Transfer group-level membership before Qt reparents the widget.
    if (auto* oldGroup = qobject_cast<ViewGroup*>(view->parentWidget()))
        oldGroup->removeView(view);

    view->setParent(this);
    views_.push_back(view);

    // A child removed from the group keeps no dangling entry if someone
    // deletes it directly.
    QObject::connect(view, &QObject::destroyed, this, [this, view] {
        std::erase(views_, view);
    });

    // setParent() hides the widget, so show it explicitly; Qt keeps it hidden
    // until the group itself is shown, which is the behaviour a constructor
    // that adds children before the window exists relies on.
    view->show();

    requestUpdate();
}

void ViewGroup::removeView(View* view)
{
    const auto position = std::find(views_.begin(), views_.end(), view);
    if (position == views_.end())
        return;

    views_.erase(position);
    QObject::disconnect(view, &QObject::destroyed, this, nullptr);

    // Reparenting to nothing makes the widget a hidden top-level; the caller
    // decides whether to show it or delete it.
    view->setParent(nullptr);
    requestUpdate();
}

QList<View*> ViewGroup::views() const
{
    QList<View*> result;
    result.reserve(static_cast<qsizetype>(views_.size()));

    for (View* view : views_)
        result.append(view);

    return result;
}

void ViewGroup::paintContent(QPainter&, const QRectF&)
{
    // A group has no content of its own. Its children are painted by
    // paintChildren(), which the pipeline calls immediately afterwards.
}

void ViewGroup::arrangeOverride(const QRectF& content)
{
    arrangeChildren(content);
}

void ViewGroup::paintChildren(QPainter& painter)
{
    // render() runs arbitrary child code: a child may add, remove or delete a
    // sibling from its own paintContent(), which mutates views_ and can
    // reallocate it. Paint a guarded snapshot and re-check membership before
    // touching an entry: a destroyed child is null in the snapshot, and a
    // child that was reparented away is simply not painted by this pass.
    QList<QPointer<View>> snapshot;
    snapshot.reserve(static_cast<qsizetype>(views_.size()));
    for (View* view : views_)
        snapshot.append(QPointer<View>(view));

    for (const QPointer<View>& entry : snapshot) {
        View* view = entry.data();
        if (view == nullptr || !view->isVisible())
            continue;

        // The child may have been removed (not destroyed) while an earlier
        // entry painted; it is no longer part of this group's paint order.
        if (std::find(views_.begin(), views_.end(), view) == views_.end())
            continue;

        // Draw only the child itself: a nested group paints its own children
        // from its paintContent/paintChildren hook, so asking render() to walk
        // the subtree would paint them twice.
        view->render(&painter, view->pos(), QRegion(), QWidget::RenderFlags());
    }
}

} // namespace md
