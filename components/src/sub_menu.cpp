// Periodicity — SubMenu, a menu row that expands into child rows.
//
// The row is inherited; the block below it is this class's own. Children are
// real widgets, arranged under the row and clipped by nothing but the parent's
// geometry, so the reveal is a height animation of one widget rather than a
// list of show/hide operations.
//
// The QTimer drives `expansion_` through the theme's motion spec. A reduced
// motion scheme resolves that spec to zero milliseconds, which snaps instead of
// animating — the same contract Collapse follows, only with the timer the
// navigation drawer asked for instead of a QVariantAnimation.

#include <md/components/sub_menu.h>

#include <md/components/menu_group.h>
#include <md/theme/theme.h>

#include <QEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QRegion>

#include <algorithm>
#include <cmath>

namespace md {

namespace {

constexpr qreal kSpacingDp = 2.0;
constexpr int kAnimationIntervalMs = 16;

} // namespace

SubMenu::SubMenu(QWidget* parent)
    : MenuItem(parent)
{
    setExpandable(true);
    setExpanded(false);

    timer_.setInterval(kAnimationIntervalMs);
    timer_.setSingleShot(false);

    QObject::connect(&timer_, &QTimer::timeout, this, [this] {
        const qreal target = isExpanded() ? 1.0 : 0.0;
        if (durationMs_ <= 0) {
            finishAnimation();
            return;
        }

        const qreal elapsed = static_cast<qreal>(elapsed_.elapsed());
        const qreal progress = std::clamp(elapsed / static_cast<qreal>(durationMs_), 0.0, 1.0);
        const qreal eased = easing_.valueForProgress(progress);
        expansion_ = std::clamp(startExpansion_ + (target - startExpansion_) * eased, 0.0, 1.0);
        applyExpansion();

        if (progress >= 1.0)
            finishAnimation();
    });
}

SubMenu::SubMenu(const QString& label, QWidget* parent)
    : SubMenu(parent)
{
    setLabel(label);
}

SubMenu::~SubMenu()
{
    // The children are destroyed by Qt after this class's members are gone.
    // Disconnecting first keeps their destroyed() notification from touching a
    // list that no longer exists (and from relayouting a dying widget).
    timer_.stop();
    for (MenuItem* child : children_) {
        if (child != nullptr)
            QObject::disconnect(child, nullptr, this, nullptr);
    }
    children_.clear();
}

// ---------------------------------------------------------------------------
// Children
// ---------------------------------------------------------------------------

int SubMenu::addItem(MenuItem* item)
{
    return insertItem(childCount(), item);
}

int SubMenu::addChild(MenuItem* item)
{
    return addItem(item);
}

int SubMenu::insertItem(int index, MenuItem* item)
{
    if (item == nullptr || item == this)
        return -1;

    const int existing = indexOfChild(item);
    if (existing >= 0) {
        children_.removeAt(existing);
        if (existing < index)
            --index;
    } else if (auto* oldSubMenu = qobject_cast<SubMenu*>(item->parentWidget())) {
        oldSubMenu->removeItem(item);
    } else if (auto* oldGroup = qobject_cast<MenuGroup*>(item->parentWidget())) {
        oldGroup->removeItem(item);
    }

    const int position = std::clamp(index, 0, childCount());
    item->setParent(this);
    children_.insert(position, item);
    item->setIndentLevel(indentLevel() + 1);
    item->show();

    QObject::connect(item, &MenuItem::activated, this, [this, item] {
        const int current = children_.indexOf(item);
        if (current >= 0)
            emit childActivated(current);
    });
    QObject::connect(item, &QObject::destroyed, this, [this, item] {
        children_.removeAll(item);
        relayoutNow();
    });

    relayoutNow();
    return position;
}

void SubMenu::removeItem(MenuItem* item)
{
    const int index = indexOfChild(item);
    if (index < 0)
        return;

    QObject::disconnect(item, nullptr, this, nullptr);
    children_.removeAt(index);
    item->setParent(nullptr);

    relayoutNow();
}

void SubMenu::clearItems()
{
    const QList<MenuItem*> children = children_;
    for (MenuItem* child : children) {
        QObject::disconnect(child, nullptr, this, nullptr);
        child->setParent(nullptr);
    }

    children_.clear();
    relayoutNow();
}

int SubMenu::childCount() const
{
    return static_cast<int>(children_.size());
}

MenuItem* SubMenu::childAt(int index) const
{
    if (index < 0 || index >= childCount())
        return nullptr;

    return children_.at(index);
}

int SubMenu::indexOfChild(const MenuItem* item) const
{
    for (int index = 0; index < childCount(); ++index) {
        if (children_.at(index) == item)
            return index;
    }
    return -1;
}

QList<MenuItem*> SubMenu::childItems() const
{
    return children_;
}

void SubMenu::setIndentLevel(int level)
{
    MenuItem::setIndentLevel(level);
    for (MenuItem* child : children_)
        child->setIndentLevel(indentLevel() + 1);

    relayoutNow();
}

// ---------------------------------------------------------------------------
// Expansion
// ---------------------------------------------------------------------------

qreal SubMenu::spacingPx() const
{
    return Theme::forWidget(this).density().dp(kSpacingDp);
}

qreal SubMenu::childrenHeight() const
{
    if (children_.isEmpty())
        return 0.0;

    qreal total = 0.0;
    for (int index = 0; index < childCount(); ++index) {
        if (index > 0)
            total += spacingPx();
        total += children_.at(index)->preferredHeight();
    }
    return total;
}

qreal SubMenu::preferredHeight() const
{
    return MenuItem::preferredHeight() + expansion_ * childrenHeight();
}

void SubMenu::setExpanded(bool expanded)
{
    if (isExpanded() == expanded && !timer_.isActive())
        return;

    const Theme& theme = Theme::forWidget(this);
    const MotionSpec spec = theme.motion().spec(Duration::Short4, Easing::EmphasizedDecelerate);
    durationMs_ = animated_ ? static_cast<int>(spec.duration.count()) : 0;
    easing_ = spec.easing.toEasingCurve();

    MenuItem::setExpanded(expanded);
    MenuItem::setArrowRotation(expansion_ * 90.0);

    if (durationMs_ <= 0) {
        finishAnimation();
        emit expandedChanged(expanded);
        return;
    }

    timer_.stop();
    startExpansion_ = expansion_;
    elapsed_.start();
    timer_.start();

    emit expandedChanged(expanded);
}

void SubMenu::finishAnimation()
{
    const bool wasActive = timer_.isActive();
    timer_.stop();

    const qreal target = isExpanded() ? 1.0 : 0.0;
    const bool changed = !qFuzzyCompare(expansion_ + 1.0, target + 1.0);

    expansion_ = target;
    applyExpansion();

    if (wasActive || changed)
        emit animationFinished();
}

void SubMenu::setAnimated(bool animated)
{
    animated_ = animated;
}

void SubMenu::activate()
{
    if (!isEnabled())
        return;

    setExpanded(!isExpanded());
    MenuItem::activate();
}

void SubMenu::applyExpansion()
{
    MenuItem::setArrowRotation(expansion_ * 90.0);
    syncChildVisibility();

    if (parentWidget() == nullptr) {
        // A standalone submenu owns its height; inside a group the parent
        // measures it from preferredHeight().
        const int target = static_cast<int>(qRound(preferredHeight()));
        if (target != height())
            resize(width(), target);
    }

    updateGeometry();
    if (QWidget* parent = parentWidget())
        parent->updateGeometry();

    arrangeOverride(contentRect());
    requestUpdate();
}

void SubMenu::syncChildVisibility()
{
    // isHidden() is the explicit bit; a child of a submenu that has not been
    // shown yet reports isVisible() == false while still being marked shown.
    const bool visible = isExpanded() || expansion_ > 0.0;
    for (MenuItem* child : children_) {
        if (visible && child->isHidden())
            child->show();
        else if (!visible && !child->isHidden())
            child->hide();
    }
}

void SubMenu::relayoutNow()
{
    applyExpansion();
}

// ---------------------------------------------------------------------------
// Measuring, arranging and painting
// ---------------------------------------------------------------------------

void SubMenu::measureOverride(MeasureSpec width, MeasureSpec height)
{
    // The header row first: it knows its own natural width from the label,
    // icon and trailing stack.
    MenuItem::measureOverride(width, MeasureSpec::unspecified());

    qreal natural = measuredSize().width();
    for (MenuItem* child : children_) {
        child->measure(MeasureSpec::unspecified(), MeasureSpec::unspecified());
        natural = std::max(natural, child->measuredSize().width());
    }

    const qreal widthPx = width.resolve(natural);
    for (MenuItem* child : children_)
        child->measure(MeasureSpec::exactly(widthPx), MeasureSpec::unspecified());

    setMeasuredSize(QSizeF(widthPx, height.resolve(preferredHeight())));
}

void SubMenu::arrangeOverride(const QRectF& content)
{
    const int width = std::max(0, static_cast<int>(qRound(content.width())));
    const int left = static_cast<int>(qRound(content.left()));
    int y = static_cast<int>(qRound(content.top() + MenuItem::preferredHeight()));

    for (MenuItem* child : children_) {
        if (child->isHidden())
            continue;

        const int childHeight = std::max(1, static_cast<int>(qRound(child->preferredHeight())));
        child->arrange(QRect(left, y, width, childHeight));
        y += childHeight + static_cast<int>(qRound(spacingPx()));
    }
}

void SubMenu::keyPressEvent(QKeyEvent* event)
{
    if (isEnabled()) {
        switch (event->key()) {
        case Qt::Key_Right:
            setExpanded(true);
            event->accept();
            return;
        case Qt::Key_Left:
            setExpanded(false);
            event->accept();
            return;
        default:
            break;
        }
    }

    MenuItem::keyPressEvent(event);
}

void SubMenu::changeEvent(QEvent* event)
{
    MenuItem::changeEvent(event);

    if (event->type() == QEvent::LayoutDirectionChange)
        requestUpdate();
}

void SubMenu::onHidden()
{
    // A hidden submenu has nothing to animate: settle on the target so the
    // next show starts from a consistent height.
    timer_.stop();
    const qreal target = isExpanded() ? 1.0 : 0.0;
    if (!qFuzzyCompare(expansion_ + 1.0, target + 1.0)) {
        expansion_ = target;
        applyExpansion();
    }
}

} // namespace md
