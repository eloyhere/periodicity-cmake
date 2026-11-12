// Periodicity — ToolBar (framework.md §6.3).
//
// A fixed-height top app bar: navigation icon, title and action run, all placed
// in logical coordinates and mirrored once, exactly like the collapsing AppBar.
// The only thing a scroll changes is the elevation.

#include <md/components/toolbar.h>

#include <md/layout/alignment.h>
#include <md/theme/theme.h>
#include <md/tokens/elevation.h>

#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QRectF>

#include <algorithm>
#include <cmath>

namespace md {

namespace {

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

ToolBar::ToolBar(QWidget* parent)
    : ViewGroup(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setBackgroundColour(QColor(Qt::transparent));
}

ToolBar::~ToolBar() = default;

void ToolBar::setTitle(QString title)
{
    if (title_ == title)
        return;

    title_ = std::move(title);
    relayoutNow();
}

void ToolBar::setNavigationIcon(View* icon)
{
    if (icon != nullptr && !views().contains(icon))
        addView(icon);

    if (icon != nullptr) {
        connect(icon, &QObject::destroyed, this, [this, icon] {
            if (navigationIcon_ == icon)
                navigationIcon_ = nullptr;
        });
    }

    if (navigationIcon_ == icon) {
        relayoutNow();
        return;
    }

    navigationIcon_ = icon;
    relayoutNow();
}

void ToolBar::addAction(View* action)
{
    if (action == nullptr || action == navigationIcon_)
        return;

    addView(action);
    relayoutNow();
}

QList<View*> ToolBar::actions() const
{
    QList<View*> result;
    for (View* child : views()) {
        if (child != nullptr && child != navigationIcon_)
            result.append(child);
    }
    return result;
}

void ToolBar::setActionsAlignment(Qt::Alignment alignment)
{
    const Qt::Alignment value = alignment & (Qt::AlignLeft | Qt::AlignRight | Qt::AlignHCenter);
    if (actionsAlignment_ == value)
        return;

    actionsAlignment_ = value;
    relayoutNow();
}

void ToolBar::setElevated(bool elevated)
{
    if (elevated_ == elevated)
        return;

    elevated_ = elevated;
    syncElevation();
    requestUpdate();
}

void ToolBar::setScrollFraction(qreal fraction)
{
    const qreal value = fraction < 0.0 ? 0.0 : (fraction > 1.0 ? 1.0 : fraction);
    if (qFuzzyCompare(scrollFraction_ + 1.0, value + 1.0))
        return;

    scrollFraction_ = value;
    syncElevation();
    requestUpdate();
}

void ToolBar::setBarHeight(qreal height)
{
    const qreal value = layout_detail::length(height, 64.0);
    if (barHeight_ == value)
        return;

    barHeight_ = value;
    relayoutNow();
}

void ToolBar::syncElevation()
{
    // A bar that is scrolled under, or explicitly elevated, sits one step above
    // the page.
    setElevation(elevated_ || scrollFraction_ > 0.0 ? Elevation::Level2 : Elevation::Level0);
}

void ToolBar::relayoutNow()
{
    updateGeometry();
    requestUpdate();
    arrangeChildren(contentRect());
}

void ToolBar::changeEvent(QEvent* event)
{
    ViewGroup::changeEvent(event);

    if (event->type() == QEvent::LayoutDirectionChange)
        relayoutNow();
}

void ToolBar::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);

    const qreal edgeInset = theme.density().dp(4.0);
    const qreal gap = theme.density().dp(4.0);
    const qreal titlePadding = theme.density().dp(8.0);

    qreal naturalWidth = 2.0 * edgeInset + titlePadding;

    if (navigationIcon_ != nullptr && !navigationIcon_->isHidden()) {
        navigationIcon_->measure(MeasureSpec::unspecified(), MeasureSpec::unspecified());
        naturalWidth += navigationIcon_->measuredSize().width();
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

    setMeasuredSize(QSizeF(width.resolve(naturalWidth), height.resolve(barHeight_)));
}

void ToolBar::arrangeChildren(const QRectF& content)
{
    const Theme& theme = Theme::forWidget(this);
    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;

    const qreal width = qMax(qreal(0.0), content.width());
    const qreal height = qMax(qreal(0.0), content.height());
    const qreal edgeInset = theme.density().dp(4.0);
    const qreal gap = theme.density().dp(4.0);
    const qreal titlePadding = theme.density().dp(8.0);

    qreal leading = edgeInset;

    auto place = [&](const QSizeF& size, qreal logical) -> QRectF {
        const qreal x = content.left()
                        + layout_detail::physicalFromLeading(logical, size.width(), width,
                                                             rightToLeft);
        const qreal y = content.top() + (height - size.height()) / 2.0;
        return QRectF(x, y, size.width(), size.height());
    };

    // 1. The navigation icon.
    if (navigationIcon_ != nullptr && !navigationIcon_->isHidden()) {
        navigationIcon_->measure(MeasureSpec::unspecified(), MeasureSpec::atMost(height));
        QSizeF size = navigationIcon_->measuredSize();
        size.setWidth(qMin(size.width(), width));
        size.setHeight(qMin(size.height(), height));

        const QRectF geometry = place(size, leading);
        navigationIcon_->measure(MeasureSpec::exactly(size.width()),
                                 MeasureSpec::exactly(size.height()));
        navigationIcon_->arrange(geometry.toAlignedRect());

        leading += size.width() + gap;
    }

    // 2. The action run.
    const QList<View*> actionViews = actions();
    QList<QSizeF> actionSizes;
    qreal actionsWidth = 0.0;
    for (View* action : actionViews) {
        action->measure(MeasureSpec::unspecified(), MeasureSpec::atMost(height));
        QSizeF size = action->measuredSize();
        size.setWidth(qMin(size.width(), width));
        size.setHeight(qMin(size.height(), height));
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

    // 3. The title, between the navigation icon and the action run.
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

void ToolBar::paintContent(QPainter& painter, const QRectF& content)
{
    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

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

} // namespace md
