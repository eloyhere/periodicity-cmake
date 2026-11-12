// Periodicity — MenuItem, one row of a navigation menu.
//
// One row layout is computed per measure/paint and shared by the paint pass and
// the geometry accessors, so the rectangle a test asks for is the rectangle the
// text is drawn in. The layout is built from a leading cursor and a trailing
// cursor; the direction of both is mirrored for a right-to-left layout, which
// is what makes the row mirror without a second code path.

#include <md/components/menu_item.h>

#include <md/components/font.h>
#include <md/theme/theme.h>
#include <md/tokens/state_layer.h>

#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <algorithm>

namespace md {

namespace {

// Design baseline expressive vertical menu: a 48 dp row carries a 32 dp selection pill that
// is inset 12 dp from the menu edge and 8 dp from the row's top and bottom.
constexpr qreal kPillInsetHorizontalDp = 12.0;
constexpr qreal kPillInsetVerticalDp = 8.0;
constexpr qreal kContentInsetDp = 12.0;
constexpr qreal kIndentStepDp = 16.0;
constexpr qreal kIconSizeDp = 24.0;
constexpr qreal kIconGapDp = 12.0;
constexpr qreal kBadgeHeightDp = 20.0;
constexpr qreal kBadgePaddingDp = 8.0;
constexpr qreal kTrailingGapDp = 8.0;
constexpr qreal kArrowBoxDp = 24.0;
constexpr qreal kChevronStrokeDp = 2.0;
constexpr int kMaxIndentLevel = 8;

QFont fontFor(const TextStyle& style, const Density& density, const QWidget* context)
{
    return Font::resolve(context, style, density);
}

QColor withAlpha(const QColor& colour, qreal alpha)
{
    QColor result = colour;
    result.setAlphaF(static_cast<float>(std::clamp(alpha, qreal(0.0), qreal(1.0))));
    return result;
}

} // namespace

MenuItem::MenuItem(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundColour(QColor(Qt::transparent));
    setContentColour(Theme::forWidget(this).colours().onSurface);
}

MenuItem::MenuItem(const QString& label, QWidget* parent)
    : MenuItem(parent)
{
    label_ = label;
}

void MenuItem::setLabel(const QString& label)
{
    if (label_ == label)
        return;

    label_ = label;
    updateGeometry();
    requestUpdate();
}

void MenuItem::setIcon(const QIcon& icon)
{
    icon_ = icon;
    updateGeometry();
    requestUpdate();
}

void MenuItem::setBadge(const QString& badge)
{
    if (badge_ == badge)
        return;

    badge_ = badge;
    updateGeometry();
    requestUpdate();
}

void MenuItem::setShortcut(const QString& shortcut)
{
    if (shortcut_ == shortcut)
        return;

    shortcut_ = shortcut;
    updateGeometry();
    requestUpdate();
}

void MenuItem::setTrailingText(const QString& text)
{
    if (trailingText_ == text)
        return;

    trailingText_ = text;
    updateGeometry();
    requestUpdate();
}

void MenuItem::setSelected(bool selected)
{
    if (selected_ == selected)
        return;

    selected_ = selected;
    requestUpdate();
}

void MenuItem::setIndentLevel(int level)
{
    const int value = std::clamp(level, 0, kMaxIndentLevel);
    if (indentLevel_ == value)
        return;

    indentLevel_ = value;
    updateGeometry();
    requestUpdate();
}

void MenuItem::setExpandable(bool expandable)
{
    if (expandable_ == expandable)
        return;

    expandable_ = expandable;
    if (!expandable_)
        arrowRotation_ = 0.0;
    updateGeometry();
    requestUpdate();
}

void MenuItem::setExpanded(bool expanded)
{
    if (expanded_ == expanded && (arrowRotation_ == (expanded ? 90.0 : 0.0)))
        return;

    expanded_ = expanded;
    arrowRotation_ = expanded ? 90.0 : 0.0;
    requestUpdate();
}

void MenuItem::setArrowRotation(qreal degrees)
{
    const qreal value = std::clamp(degrees, qreal(0.0), qreal(90.0));
    if (qFuzzyCompare(arrowRotation_ + 1.0, value + 1.0))
        return;

    arrowRotation_ = value;
    requestUpdate();
}

void MenuItem::setRowHeight(qreal dp)
{
    const qreal value = std::max(qreal(24.0), dp);
    if (qFuzzyCompare(rowHeightDp_ + 1.0, value + 1.0))
        return;

    rowHeightDp_ = value;
    updateGeometry();
    requestUpdate();
}

qreal MenuItem::rowHeightPx() const
{
    return Theme::forWidget(this).density().dp(rowHeightDp_);
}

qreal MenuItem::preferredHeight() const
{
    return rowHeightPx();
}

QRectF MenuItem::rowRect() const
{
    return QRectF(0.0, 0.0, static_cast<qreal>(width()), rowHeightPx());
}

void MenuItem::activate()
{
    if (!isEnabled())
        return;

    emit clicked();
    emit activated();
}

MenuItem::RowLayout MenuItem::computeLayout() const
{
    RowLayout layout;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal widthPx = std::max(qreal(0.0), static_cast<qreal>(width()));
    const qreal rowHeight = rowHeightPx();

    layout.row = QRectF(0.0, 0.0, widthPx, rowHeight);
    if (widthPx <= 0.0 || rowHeight <= 0.0)
        return layout;

    const qreal pillInsetX = density.dp(kPillInsetHorizontalDp);
    const qreal pillInsetY = density.dp(kPillInsetVerticalDp);
    layout.pill = QRectF(pillInsetX, pillInsetY,
                         std::max(qreal(0.0), widthPx - 2.0 * pillInsetX),
                         std::max(qreal(0.0), rowHeight - 2.0 * pillInsetY));
    if (layout.pill.width() <= 0.0 || layout.pill.height() <= 0.0)
        return layout;

    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;
    const qreal direction = rightToLeft ? -1.0 : 1.0;
    const qreal contentInset = density.dp(kContentInsetDp);
    const qreal gap = density.dp(kIconGapDp);
    const qreal trailingGap = density.dp(kTrailingGapDp);
    const qreal iconSize = density.dp(kIconSizeDp);
    const qreal arrowBox = density.dp(kArrowBoxDp);
    const qreal badgeHeight = density.dp(kBadgeHeightDp);
    const qreal badgePadding = density.dp(kBadgePaddingDp);

    const QFontMetricsF smallMetrics(fontFor(theme.type().labelSmall, density, this));
    const QFontMetricsF trailingMetrics(fontFor(theme.type().bodySmall, density, this));

    // The trailing stack is placed first, from the outer edge inwards: the
    // chevron, then the badge, then the shortcut, then free trailing text.
    qreal trailing = (rightToLeft ? layout.pill.left() : layout.pill.right())
        - direction * contentInset;

    const auto placeTrailing = [&](qreal itemWidth, qreal itemHeight) {
        const qreal x = rightToLeft ? trailing : trailing - itemWidth;
        const QRectF rect(x, layout.pill.top() + (layout.pill.height() - itemHeight) / 2.0,
                          itemWidth, itemHeight);
        // Move inwards: left for a left-to-right row, right for a mirrored one.
        trailing -= direction * (itemWidth + trailingGap);
        return rect;
    };

    if (expandable_) {
        layout.arrow = placeTrailing(arrowBox, arrowBox);
    }

    if (!badge_.isEmpty()) {
        const qreal badgeWidth = smallMetrics.horizontalAdvance(badge_) + 2.0 * badgePadding;
        layout.badge = placeTrailing(badgeWidth, badgeHeight);
        layout.hasBadge = true;
    }

    if (!shortcut_.isEmpty()) {
        const qreal shortcutWidth = smallMetrics.horizontalAdvance(shortcut_);
        layout.shortcut = placeTrailing(shortcutWidth, smallMetrics.height());
        layout.hasShortcut = true;
    }

    if (!trailingText_.isEmpty()) {
        const qreal trailingWidth = trailingMetrics.horizontalAdvance(trailingText_);
        layout.trailing = placeTrailing(trailingWidth, trailingMetrics.height());
        layout.hasTrailing = true;
    }

    // The leading side: one indent step per level, then the icon.
    qreal leading = (rightToLeft ? layout.pill.right() : layout.pill.left())
        + direction * (contentInset + density.dp(kIndentStepDp) * static_cast<qreal>(indentLevel_));

    if (!icon_.isNull()) {
        const qreal iconX = rightToLeft ? leading - iconSize : leading;
        layout.icon = QRectF(iconX, layout.pill.top() + (layout.pill.height() - iconSize) / 2.0,
                             iconSize, iconSize);
        layout.hasIcon = true;
        leading += direction * (iconSize + gap);
    }

    const qreal textFrom = rightToLeft ? trailing : leading;
    const qreal textTo = rightToLeft ? leading : trailing;
    const qreal textWidth = std::max(qreal(0.0), textTo - textFrom);
    layout.text = QRectF(rightToLeft ? textTo - textWidth : textFrom, layout.pill.top(),
                         textWidth, layout.pill.height());

    return layout;
}

QString MenuItem::elide(const QString& text, qreal width) const
{
    if (text.isEmpty() || width <= 0.0)
        return {};

    const Theme& theme = Theme::forWidget(this);
    const QFontMetricsF metrics(fontFor(theme.type().bodyLarge, theme.density(), this));
    const Qt::TextElideMode mode =
        layoutDirection() == Qt::RightToLeft ? Qt::ElideLeft : Qt::ElideRight;
    return metrics.elidedText(text, mode, width);
}

QString MenuItem::elidedLabel() const
{
    const RowLayout layout = computeLayout();
    return elide(label_, layout.text.width());
}

QRectF MenuItem::itemRect() const
{
    return computeLayout().row;
}

QRectF MenuItem::pillRect() const
{
    return computeLayout().pill;
}

QRectF MenuItem::iconRect() const
{
    return computeLayout().icon;
}

QRectF MenuItem::textRect() const
{
    return computeLayout().text;
}

QRectF MenuItem::arrowRect() const
{
    return computeLayout().arrow;
}

QRectF MenuItem::badgeRect() const
{
    return computeLayout().badge;
}

void MenuItem::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFontMetricsF labelMetrics(fontFor(theme.type().bodyLarge, density, this));
    const QFontMetricsF smallMetrics(fontFor(theme.type().labelSmall, density, this));
    const QFontMetricsF trailingMetrics(fontFor(theme.type().bodySmall, density, this));

    const qreal gap = density.dp(kIconGapDp);
    const qreal trailingGap = density.dp(kTrailingGapDp);

    qreal natural = 2.0 * (density.dp(kPillInsetHorizontalDp) + density.dp(kContentInsetDp))
        + density.dp(kIndentStepDp) * static_cast<qreal>(indentLevel_)
        + labelMetrics.horizontalAdvance(label_);

    if (!icon_.isNull())
        natural += density.dp(kIconSizeDp) + gap;
    if (!badge_.isEmpty())
        natural += smallMetrics.horizontalAdvance(badge_) + 2.0 * density.dp(kBadgePaddingDp)
            + trailingGap;
    if (!shortcut_.isEmpty())
        natural += smallMetrics.horizontalAdvance(shortcut_) + trailingGap;
    if (!trailingText_.isEmpty())
        natural += trailingMetrics.horizontalAdvance(trailingText_) + trailingGap;
    if (expandable_)
        natural += density.dp(kArrowBoxDp) + trailingGap;

    setMeasuredSize(QSizeF(width.resolve(natural), height.resolve(preferredHeight())));
}

void MenuItem::paintContent(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);

    const RowLayout layout = computeLayout();
    if (layout.pill.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();
    const bool enabled = isEnabled();
    const bool pressed = pressed_;
    const bool hovered = hovered_;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const qreal radius = layout.pill.height() / 2.0;
    painter.setPen(Qt::NoPen);

    if (selected_) {
        painter.setBrush(colours.secondaryContainer);
        painter.drawRoundedRect(layout.pill, radius, radius);
    }

    if (enabled && (pressed || hovered)) {
        const ViewState layerState = pressed ? ViewState::Pressed : ViewState::Hovered;
        const QColor base = selected_ ? colours.onSecondaryContainer : colours.onSurface;
        painter.setBrush(colours.stateLayer(base, layerState));
        painter.drawRoundedRect(layout.pill, radius, radius);
    }

    const QColor contentColour = !enabled
        ? withAlpha(colours.onSurface, kDisabledContentOpacity)
        : (selected_ ? colours.onSecondaryContainer : colours.onSurface);
    const QColor secondaryColour = !enabled
        ? withAlpha(colours.onSurface, kDisabledContentOpacity)
        : colours.onSurfaceVariant;

    if (layout.hasIcon) {
        icon_.paint(&painter, layout.icon.toRect(), Qt::AlignCenter,
                    enabled ? QIcon::Normal : QIcon::Disabled);
    }

    const Qt::Alignment textAlignment =
        (layoutDirection() == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft)
        | Qt::AlignVCenter;

    if (!label_.isEmpty() && layout.text.width() > 0.0) {
        painter.setFont(fontFor(theme.type().bodyLarge, theme.density(), this));
        painter.setPen(contentColour);
        painter.drawText(layout.text, static_cast<int>(textAlignment),
                         elide(label_, layout.text.width()));
    }

    if (layout.hasTrailing) {
        painter.setFont(fontFor(theme.type().bodySmall, theme.density(), this));
        painter.setPen(secondaryColour);
        painter.drawText(layout.trailing, static_cast<int>(textAlignment), trailingText_);
    }

    if (layout.hasShortcut) {
        painter.setFont(fontFor(theme.type().labelSmall, theme.density(), this));
        painter.setPen(enabled ? withAlpha(colours.onSurfaceVariant, 0.75) : secondaryColour);
        painter.drawText(layout.shortcut, static_cast<int>(textAlignment), shortcut_);
    }

    if (layout.hasBadge) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(enabled ? colours.primaryContainer : colours.surfaceContainerHighest);
        painter.drawRoundedRect(layout.badge, layout.badge.height() / 2.0,
                                layout.badge.height() / 2.0);

        painter.setFont(fontFor(theme.type().labelSmall, theme.density(), this));
        painter.setPen(enabled ? colours.onPrimaryContainer
                               : withAlpha(colours.onSurface, kDisabledContentOpacity));
        painter.drawText(layout.badge, Qt::AlignCenter, badge_);
    }

    if (expandable_ && !layout.arrow.isEmpty()) {
        // The chevron is a rotation of one path: 0 degrees points at the
        // trailing edge, 90 at the expanded direction, and a right-to-left
        // layout starts from the mirrored base.
        const QPointF centre = layout.arrow.center();
        const qreal half = layout.arrow.width() / 4.0;
        const qreal angle = layoutDirection() == Qt::RightToLeft
            ? 180.0 - arrowRotation_
            : arrowRotation_;

        QPainterPath chevron;
        chevron.moveTo(-half / 2.0, -half);
        chevron.lineTo(half / 2.0, 0.0);
        chevron.lineTo(-half / 2.0, half);

        QPen pen(selected_ ? colours.onSecondaryContainer : contentColour,
                 std::max(qreal(1.0), theme.density().dp(kChevronStrokeDp)));
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);

        painter.save();
        painter.translate(centre);
        painter.rotate(angle);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(chevron);
        painter.restore();
    }

    painter.restore();
}

void MenuItem::mousePressEvent(QMouseEvent* event)
{
    View::mousePressEvent(event);

    if (!isEnabled() || event->button() != Qt::LeftButton)
        return;

    pressedInside_ = rowRect().contains(event->position());
    pressed_ = pressedInside_;
    requestUpdate();
}

void MenuItem::mouseReleaseEvent(QMouseEvent* event)
{
    const bool wasPressed = pressedInside_;
    pressedInside_ = false;
    pressed_ = false;

    View::mouseReleaseEvent(event);

    if (!isEnabled() || event->button() != Qt::LeftButton)
        return;

    if (wasPressed && rowRect().contains(event->position()))
        activate();
}

void MenuItem::enterEvent(QEnterEvent* event)
{
    View::enterEvent(event);

    hovered_ = true;
    requestUpdate();
}

void MenuItem::leaveEvent(QEvent* event)
{
    pressedInside_ = false;
    hovered_ = false;
    pressed_ = false;
    View::leaveEvent(event);
}

void MenuItem::keyPressEvent(QKeyEvent* event)
{
    if (isEnabled()) {
        switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            activate();
            event->accept();
            return;
        default:
            break;
        }
    }

    // Up/Down/Home/End are not consumed here on purpose: Qt propagates an
    // ignored key event to the parent widget, which is how a row hands the
    // cursor to its MenuGroup.
    View::keyPressEvent(event);
}

void MenuItem::changeEvent(QEvent* event)
{
    View::changeEvent(event);

    switch (event->type()) {
    case QEvent::EnabledChange:
        if (!isEnabled()) {
            hovered_ = false;
            pressed_ = false;
            pressedInside_ = false;
        }
        requestUpdate();
        break;
    case QEvent::LayoutDirectionChange:
        requestUpdate();
        break;
    default:
        break;
    }
}

bool MenuItem::onEvent(Event& event)
{
    // The row paints its own pill: the kernel's full-bleed state overlay would
    // be a rectangle around that pill, and on a SubMenu it would also tint the
    // revealed child block. Hover and press are tracked in the mouse handlers
    // instead; focus still goes through the kernel so the focus ring works.
    switch (event.type()) {
    case EventType::PointerEnter:
    case EventType::PointerLeave:
    case EventType::PointerDown:
    case EventType::PointerUp:
        return false;
    default:
        break;
    }

    return View::onEvent(event);
}

void MenuItem::onThemeChanged()
{
    updateGeometry();
    requestUpdate();
}

bool MenuItem::focusable() const
{
    return isEnabled();
}

} // namespace md
