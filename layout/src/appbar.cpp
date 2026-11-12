// Periodicity — AppBar (framework.md §6.3).
//
// The bar owns three things: the title rectangle, the leading navigation button
// and the trailing action run. All three are placed in logical coordinates —
// distance from the leading edge — and mirrored once at the end, which is what
// makes a right-to-left bar a layout question rather than a second code path.

#include <md/layout/appbar.h>

#include <md/layout/alignment.h>
#include <md/theme/theme.h>

#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <exception>

namespace md {

namespace {

// The M3 small top app bar title style.
QFont titleFont(const Theme& theme)
{
    const TextStyle& style = theme.type().titleLarge;
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(static_cast<QFont::Weight>(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
    return font;
}

} // namespace

AppBar::AppBar(QWidget* parent)
    : ViewGroup(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setBackgroundColour(QColor(Qt::transparent));
}

AppBar::~AppBar() = default;

void AppBar::setTitle(QString title)
{
    if (title_ == title)
        return;

    title_ = std::move(title);
    updateGeometry();
    requestUpdate();
}

void AppBar::setNavigationButton(View* button)
{
    if (button != nullptr && !views().contains(button))
        addView(button);

    if (button != nullptr) {
        connect(button, &QObject::destroyed, this, [this, button] {
            if (navigationButton_ == button)
                navigationButton_ = nullptr;
        });
    }

    View* const previous = navigationButton_;
    if (previous == button) {
        relayoutNow();
        return;
    }

    navigationButton_ = button;

    // The role is visibility: a button that loses it is hidden so that it is
    // neither painted at its last rectangle nor mistaken for a trailing action,
    // and a button that takes it is shown again. The bar never deletes the
    // view; it stays the caller's.
    if (previous != nullptr)
        previous->hide();
    if (button != nullptr)
        button->show();

    relayoutNow();
}

void AppBar::addAction(View* action)
{
    if (action == nullptr || action == navigationButton_)
        return;

    addView(action);
    relayoutNow();
}

QList<View*> AppBar::actions() const
{
    // The run is the visible children apart from the navigation button: a
    // hidden action takes no space, and a hidden child is never an action.
    QList<View*> result;
    for (View* child : views()) {
        if (child != nullptr && child != navigationButton_ && !child->isHidden())
            result.append(child);
    }
    return result;
}

void AppBar::setActionsAlignment(Qt::Alignment alignment)
{
    const Qt::Alignment value =
        alignment & (Qt::AlignLeft | Qt::AlignRight | Qt::AlignHCenter);
    if (actionsAlignment_ == value)
        return;

    actionsAlignment_ = value;
    relayoutNow();
}

void AppBar::setExpandedHeight(qreal height)
{
    // Zero is a legal height (a bar that occupies nothing); a negative or
    // non-finite value is ignored instead of being replaced by a different
    // number the caller never asked for.
    const qreal value = layout_detail::length(height, expandedHeight_);
    if (expandedHeight_ == value)
        return;

    expandedHeight_ = value;
    relayoutNow();
}

void AppBar::setCollapsedHeight(qreal height)
{
    const qreal value = layout_detail::length(height, collapsedHeight_);
    if (collapsedHeight_ == value)
        return;

    collapsedHeight_ = value;
    relayoutNow();
}

void AppBar::setScrollRange(qreal range)
{
    const qreal value = layout_detail::length(range);
    if (scrollRange_ == value)
        return;

    scrollRange_ = value;

    // A shorter range can leave the offset outside it. The clamp goes through
    // setScrollOffset(), so the repair is announced exactly like an explicit
    // offset change. A longer range can expand the bar without moving the
    // offset at all, so the collapsed state is re-derived here as well.
    setScrollOffset(scrollOffset_);
    syncCollapsed();
    relayoutNow();
}

void AppBar::setScrollOffset(qreal offset)
{
    const qreal clamped = qBound(qreal(0.0), offset, scrollRange_);
    if (qFuzzyCompare(scrollOffset_ + 1.0, clamped + 1.0))
        return;

    scrollOffset_ = clamped;
    syncElevation();
    relayoutNow();
    syncCollapsed();

    emit offsetChanged(scrollOffset_);
}

qreal AppBar::collapseFraction() const noexcept
{
    if (scrollRange_ <= 0.0)
        return 0.0;

    const qreal fraction = scrollOffset_ / scrollRange_;
    return fraction < 0.0 ? 0.0 : (fraction > 1.0 ? 1.0 : fraction);
}

qreal AppBar::currentHeight() const noexcept
{
    return barHeight();
}

void AppBar::setScrolledElevation(Elevation level)
{
    if (scrolledElevation_ == level)
        return;

    scrolledElevation_ = level;
    syncElevation();
}

void AppBar::syncElevation()
{
    // At rest the bar belongs to the surface; once it is scrolled it lifts by
    // one step of the elevation ramp.
    setElevation(scrollOffset_ > 0.0 ? scrolledElevation_ : Elevation::Level0);
}

void AppBar::syncCollapsed()
{
    // The flag is derived from the fraction, never stored by the setters: a
    // range change can flip it while the offset itself stays where it was.
    const bool collapsedNow = collapseFraction() >= 1.0;
    if (collapsedNow == collapsed_)
        return;

    collapsed_ = collapsedNow;
    emit collapsedChanged(collapsed_);
}

void AppBar::relayoutNow()
{
    updateGeometry();
    requestUpdate();
    arrangeChildren(contentRect());
}

qreal AppBar::barHeight() const noexcept
{
    const qreal fraction = collapseFraction();
    return expandedHeight_ + fraction * (collapsedHeight_ - expandedHeight_);
}

void AppBar::measureOverride(MeasureSpec width, MeasureSpec height)
{
    // The bar measures its children through their own hooks, which is user
    // code: one throw must not unwind through Qt's frame.
    try {
        measureBar(width, height);
    } catch (const std::exception& error) {
        layout_detail::reportFailure("AppBar::measureOverride", error.what());
    } catch (...) {
        layout_detail::reportFailure("AppBar::measureOverride", nullptr);
    }
}

void AppBar::measureBar(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);

    const qreal edgeInset = theme.density().dp(4.0);
    const qreal gap = theme.density().dp(4.0);
    const qreal titlePadding = theme.density().dp(8.0);

    qreal naturalWidth = 2.0 * edgeInset + titlePadding;

    if (navigationButton_ != nullptr && !navigationButton_->isHidden()) {
        navigationButton_->measure(MeasureSpec::unspecified(), MeasureSpec::unspecified());
        naturalWidth += navigationButton_->measuredSize().width();
    }

    const QList<View*> actionViews = actions();
    for (View* action : actionViews) {
        action->measure(MeasureSpec::unspecified(), MeasureSpec::unspecified());
        naturalWidth += action->measuredSize().width();
    }
    if (actionViews.size() > 1)
        naturalWidth += gap * static_cast<qreal>(actionViews.size() - 1);

    if (!title_.isEmpty())
        naturalWidth += QFontMetricsF(titleFont(theme)).horizontalAdvance(title_);

    setMeasuredSize(QSizeF(width.resolve(naturalWidth), height.resolve(barHeight())));
}

void AppBar::arrangeChildren(const QRectF& content)
{
    // Placing a child runs its measure()/arrange() hooks, which is user code:
    // one throw must not unwind through Qt's frame.
    try {
        arrangeBar(content);
    } catch (const std::exception& error) {
        layout_detail::reportFailure("AppBar::arrangeChildren", error.what());
    } catch (...) {
        layout_detail::reportFailure("AppBar::arrangeChildren", nullptr);
    }
}

void AppBar::arrangeBar(const QRectF& content)
{
    const Theme& theme = Theme::forWidget(this);
    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;

    const qreal width = qMax(qreal(0.0), content.width());
    const qreal height = qMax(qreal(0.0), content.height());
    const qreal edgeInset = theme.density().dp(4.0);
    const qreal gap = theme.density().dp(4.0);
    const qreal titlePadding = theme.density().dp(8.0);

    // A logical cursor: the distance from the leading edge. It becomes an x
    // only when a rectangle is handed to a child.
    qreal leading = edgeInset;
    auto place = [&](const QSizeF& size, qreal logical) -> QRectF {
        const qreal x = content.left()
                        + layout_detail::physicalFromLeading(logical, size.width(), width,
                                                             rightToLeft);
        const qreal y = content.top() + (height - size.height()) / 2.0;
        return QRectF(x, y, size.width(), size.height());
    };

    // 1. The navigation button, at the leading edge.
    if (navigationButton_ != nullptr && !navigationButton_->isHidden()) {
        navigationButton_->measure(MeasureSpec::unspecified(),
                                   MeasureSpec::atMost(height));
        QSizeF size = navigationButton_->measuredSize();
        size.setHeight(qMin(size.height(), height));
        size.setWidth(qMin(size.width(), width));

        const QRectF geometry = place(size, leading);
        navigationButton_->measure(MeasureSpec::exactly(size.width()),
                                   MeasureSpec::exactly(size.height()));
        navigationButton_->arrange(geometry.toAlignedRect());

        leading += size.width() + gap;
    }

    // 2. The action run. It is anchored on the trailing edge by default; the
    //    alignment may move it to the leading edge or the centre.
    const QList<View*> actionViews = actions();
    QList<QSizeF> actionSizes;
    qreal actionsWidth = 0.0;
    for (View* action : actionViews) {
        action->measure(MeasureSpec::unspecified(), MeasureSpec::atMost(height));
        QSizeF size = action->measuredSize();
        size.setHeight(qMin(size.height(), height));
        size.setWidth(qMin(size.width(), width));
        actionSizes.append(size);
        actionsWidth += size.width();
    }
    if (actionViews.size() > 1)
        actionsWidth += gap * static_cast<qreal>(actionViews.size() - 1);

    // The alignment is logical: AlignRight means the trailing edge, which is
    // the left one in a right-to-left bar.
    const bool actionsTrailing = actionsAlignment_.testFlag(Qt::AlignRight);
    const bool actionsCentred = actionsAlignment_.testFlag(Qt::AlignHCenter);

    qreal groupStart = qMax(qreal(0.0), width - edgeInset - actionsWidth);
    if (actionsCentred)
        groupStart = qMax(qreal(0.0), (width - actionsWidth) / 2.0);
    else if (!actionsTrailing)
        groupStart = leading;

    // The run is placed as one physical block: it mirrors as a whole, and the
    // actions keep their insertion order inside it.
    qreal cursor = content.left()
                   + layout_detail::physicalFromLeading(groupStart, actionsWidth, width,
                                                        rightToLeft);
    for (qsizetype index = 0; index < actionViews.size(); ++index) {
        View* action = actionViews.at(index);
        const QSizeF size = actionSizes.at(index);

        const QRectF geometry(cursor, content.top() + (height - size.height()) / 2.0,
                              size.width(), size.height());
        action->measure(MeasureSpec::exactly(size.width()),
                        MeasureSpec::exactly(size.height()));
        action->arrange(geometry.toAlignedRect());

        cursor += size.width() + gap;
    }

    // 3. The title, in whatever the navigation button and the actions left
    //    over. An overlapping action run (centred) leaves the full span.
    qreal titleStart = leading + titlePadding;
    qreal titleEnd = width - edgeInset;
    if (actionsTrailing)
        titleEnd = qMin(titleEnd, groupStart - gap);
    else if (!actionsCentred)
        titleStart = qMax(titleStart, groupStart + actionsWidth + gap);

    const qreal titleWidth = qMax(qreal(0.0), titleEnd - titleStart);
    const qreal titleX = content.left()
                         + layout_detail::physicalFromLeading(titleStart, titleWidth, width,
                                                              rightToLeft);
    titleRect_ = QRectF(titleX, content.top(), titleWidth, height);
}

void AppBar::paintContent(QPainter& painter, const QRectF& content)
{
    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    // The bar owns its surface: the View background is transparent so that the
    // shadow of the elevation layer can wrap the tonal fill.
    painter.fillRect(content, colours.surfaceContainer);

    if (!title_.isEmpty() && titleRect_.width() > 0.0) {
        const QFont font = titleFont(theme);
        painter.setFont(font);

        QColor colour = colours.onSurface;
        if (!isEnabled())
            colour.setAlphaF(0.38F);
        painter.setPen(colour);

        const QString elided =
            QFontMetricsF(font).elidedText(title_, Qt::ElideRight, titleRect_.width());
        const Qt::Alignment alignment =
            Qt::AlignVCenter
            | (layoutDirection() == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft);
        painter.drawText(titleRect_, static_cast<int>(alignment), elided);
    }

    painter.restore();
}

void AppBar::changeEvent(QEvent* event)
{
    ViewGroup::changeEvent(event);

    // Leading and trailing swap with the direction, so the whole bar has to be
    // placed again rather than merely repainted.
    if (event->type() == QEvent::LayoutDirectionChange)
        relayoutNow();
}

AppBarBehavior* AppBar::attachTo(Coordinator* coordinator)
{
    if (coordinator == nullptr)
        return nullptr;

    // addBehaviour() keeps an existing behaviour for the child, so the cast is
    // what tells the caller whether this call attached a new AppBarBehavior.
    return dynamic_cast<AppBarBehavior*>(
        coordinator->addBehaviour(this, std::make_unique<AppBarBehavior>(this)));
}

AppBarBehavior::AppBarBehavior(AppBar* appBar)
    : appBar_(appBar)
{
}

bool AppBarBehavior::onStartNestedScroll(Qt::Orientation axis)
{
    return appBar_ != nullptr && axis == Qt::Vertical;
}

void AppBarBehavior::onNestedPreScroll(const QPointF& delta, QPointF* consumed)
{
    if (appBar_ == nullptr || consumed == nullptr || delta.y() == 0.0)
        return;

    // The bar collapses before the content scrolls, so the content never
    // scrolls under a bar that has not finished collapsing.
    const qreal before = appBar_->scrollOffset();
    appBar_->setScrollOffset(before + delta.y());
    consumed->setY(consumed->y() + (appBar_->scrollOffset() - before));

    // The bar's height changed, so the coordinator has to place it again.
    if (Coordinator* coordinator = this->coordinator(); coordinator != nullptr)
        coordinator->requestLayout();
}

void AppBarBehavior::onNestedScroll(const QPointF&, const QPointF& unconsumed)
{
    if (appBar_ == nullptr || unconsumed.y() >= 0.0)
        return;

    // An overscroll at the top reveals the bar again.
    appBar_->setScrollOffset(appBar_->scrollOffset() + unconsumed.y());

    if (Coordinator* coordinator = this->coordinator(); coordinator != nullptr)
        coordinator->requestLayout();
}

void AppBarBehavior::onStopNestedScroll()
{
    // The bar has no snap: it keeps whatever offset the gesture reached.
}

void AppBarBehavior::onLayoutChild(View* child, const QRectF& content, QRectF* geometry)
{
    if (appBar_ == nullptr || geometry == nullptr || child != appBar_.data())
        return;

    // The bar is a top strip, not a full-page overlay: it keeps its own height
    // instead of taking the coordinator's.
    *geometry = QRectF(content.left(), content.top(), content.width(),
                       appBar_->currentHeight());
}

} // namespace md
