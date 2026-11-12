// Periodicity — NavigationBar.
//
// Like TabBar, the bar is one painted surface: destinations are values rather
// than child widgets. That keeps the hit-test rectangles and the painted ones
// identical, and it makes the RTL mirroring a single index-to-slot mapping
// instead of a separate layout branch in every drawing call.

#include <md/components/navigation_bar.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QColor>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QSizeF>
#include <QStringList>

#include <algorithm>

namespace md {
namespace {

constexpr qreal kBarHeightDp = 80.0;
constexpr qreal kIndicatorSizeDp = 32.0;
constexpr qreal kIconSizeDp = 24.0;
constexpr qreal kIconGapDp = 4.0;
constexpr qreal kLabelInsetDp = 4.0;
constexpr qreal kNaturalSlotWidthDp = 80.0;
constexpr qreal kStateLayerWidthDp = 64.0;
constexpr qreal kStateInsetDp = 4.0;
constexpr qreal kStateHoverAlpha = 0.08;
constexpr qreal kStatePressedAlpha = 0.12;
constexpr qreal kBadgeHeightDp = 16.0;
constexpr qreal kBadgePaddingDp = 4.0;
constexpr qreal kBadgeTrailingOffsetDp = 4.0;
constexpr qreal kBadgeLiftDp = 8.0;
constexpr qreal kDisabledAlpha = 0.38;

QColor withAlpha(const QColor& colour, qreal alpha)
{
    QColor result = colour;
    result.setAlphaF(static_cast<float>(std::clamp(alpha, qreal(0.0), qreal(1.0))));
    return result;
}

// Black or white, whichever reads on `fill` — the rule the Badge component
// uses, because a badge fill is a role, not necessarily a contrast pair.
QColor readableOn(const QColor& fill)
{
    const int luminance = (fill.red() * 299 + fill.green() * 587 + fill.blue() * 114) / 1000;
    return luminance > 150 ? QColor(0, 0, 0) : QColor(255, 255, 255);
}

} // namespace

NavigationBar::NavigationBar(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    // A navigation bar is a flat strip at the bottom of a surface: no rounded
    // container, and no elevation of its own.
    setShape(Shape::none());
    setBackgroundColour(Theme::forWidget(this).colours().surfaceContainer);
    // The shared interaction layers cover the whole view; a bar-wide ripple
    // would wash over every destination at once. The bar paints a state layer
    // per destination instead.
    setInteractionFeedbackEnabled(false);
    setAccessibleName(tr("Navigation"));
    refreshAccessibleDescription();
}

// ---------------------------------------------------------------------------
// Destinations and selection
// ---------------------------------------------------------------------------

int NavigationBar::addDestination(const Destination& destination)
{
    destinations_.append(destination);
    const int index = destinationCount() - 1;

    // A navigation bar always has a current destination while it has one at
    // all; the first destination added takes that role.
    if (current_ < 0)
        current_ = index;

    refreshAccessibleDescription();
    updateGeometry();
    requestUpdate();
    return index;
}

void NavigationBar::clearDestinations()
{
    destinations_.clear();
    current_ = -1;
    hovered_ = -1;
    pressed_ = -1;
    refreshAccessibleDescription();
    updateGeometry();
    requestUpdate();
}

int NavigationBar::destinationCount() const noexcept
{
    return static_cast<int>(destinations_.size());
}

NavigationBar::Destination NavigationBar::destinationAt(int index) const
{
    if (index < 0 || index >= destinationCount())
        return {};

    return destinations_.at(index);
}

void NavigationBar::setCurrentIndex(int index)
{
    const int count = destinationCount();
    const int clamped = count == 0 ? -1 : std::clamp(index, 0, count - 1);
    if (clamped == current_)
        return;

    current_ = clamped;
    refreshAccessibleDescription();
    requestUpdate();
}

int NavigationBar::currentIndex() const noexcept
{
    return current_;
}

QRectF NavigationBar::destinationRect(int index) const
{
    const int count = destinationCount();
    if (index < 0 || index >= count)
        return {};

    const qreal slotWidth = static_cast<qreal>(width()) / static_cast<qreal>(count);
    const qreal left = static_cast<qreal>(visualSlot(index)) * slotWidth;
    return QRectF(left, 0.0, slotWidth, static_cast<qreal>(height()));
}

// ---------------------------------------------------------------------------
// Geometry and painting
// ---------------------------------------------------------------------------

bool NavigationBar::isRightToLeft() const
{
    return layoutDirection() == Qt::RightToLeft;
}

int NavigationBar::visualSlot(int index) const
{
    const int count = destinationCount();
    return isRightToLeft() ? count - 1 - index : index;
}

int NavigationBar::destinationIndexAt(const QPointF& position) const
{
    if (destinationCount() == 0 || !QRectF(rect()).contains(position))
        return -1;

    for (int index = 0; index < destinationCount(); ++index) {
        if (destinationRect(index).contains(position))
            return index;
    }

    return -1;
}

void NavigationBar::activate(int index)
{
    if (index < 0 || index >= destinationCount())
        return;

    setCurrentIndex(index);
    emit destinationActivated(index);
}

void NavigationBar::refreshAccessibleDescription()
{
    // The destinations are painted, so they have no accessible objects of
    // their own. Listing their labels (and marking the current one) keeps every
    // label reachable from the bar's accessible description.
    QStringList labels;
    labels.reserve(destinationCount());
    for (int index = 0; index < destinationCount(); ++index) {
        const QString& label = destinations_.at(index).label;
        if (label.isEmpty())
            continue;
        labels.append(index == current_ ? tr("%1, selected").arg(label) : label);
    }

    setAccessibleDescription(labels.join(QStringLiteral(", ")));
}

void NavigationBar::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    // Three slots is the narrowest shape the bar is designed for; more
    // destinations widen the natural size instead of compressing the columns.
    // `slots` is a Qt macro, hence slotCount.
    const int slotCount = std::max(3, destinationCount());
    const qreal naturalWidth = density.dp(kNaturalSlotWidthDp) * static_cast<qreal>(slotCount);

    setMeasuredSize(QSizeF(width.resolve(naturalWidth),
                           height.resolve(density.dp(kBarHeightDp))));
}

void NavigationBar::paintContent(QPainter& painter, const QRectF& content)
{
    const int count = destinationCount();
    if (count == 0 || content.isEmpty())
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const qreal slotWidth = content.width() / static_cast<qreal>(count);
    for (int index = 0; index < count; ++index) {
        const qreal left = content.left() + static_cast<qreal>(visualSlot(index)) * slotWidth;
        paintDestination(painter, index, QRectF(left, content.top(), slotWidth,
                                                content.height()));
    }

    painter.restore();
}

void NavigationBar::paintDestination(QPainter& painter, int index, const QRectF& slot)
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const Destination& destination = destinations_.at(index);
    const bool selected = index == current_;
    const bool enabled = isEnabled();
    const bool rightToLeft = isRightToLeft();

    const qreal indicatorSize = density.dp(kIndicatorSizeDp);
    const qreal iconSize = density.dp(kIconSizeDp);
    const qreal gap = density.dp(kIconGapDp);
    const QFont labelFont = Font::resolve(this, theme.type().labelMedium, density);
    const QFontMetricsF labelMetrics(labelFont);

    const qreal stackHeight = indicatorSize + gap + labelMetrics.height();
    const qreal top = slot.top() + std::max(qreal(0.0), (slot.height() - stackHeight) / 2.0);
    const QRectF indicatorRect(slot.center().x() - indicatorSize / 2.0, top,
                               indicatorSize, indicatorSize);
    const QRectF iconRect(indicatorRect.center().x() - iconSize / 2.0,
                          indicatorRect.center().y() - iconSize / 2.0, iconSize, iconSize);
    const qreal labelInset = density.dp(kLabelInsetDp);
    const QRectF labelRect(slot.left() + labelInset, indicatorRect.bottom() + gap,
                           std::max(qreal(0.0), slot.width() - 2.0 * labelInset),
                           labelMetrics.height());

    // 1. The interaction state layer, a 64 x 32 dp pill under the indicator.
    if (enabled && (index == hovered_ || index == pressed_)) {
        const qreal stateWidth = std::min(density.dp(kStateLayerWidthDp),
                                          slot.width() - 2.0 * density.dp(kStateInsetDp));
        if (stateWidth > 0.0) {
            const QRectF stateRect(indicatorRect.center().x() - stateWidth / 2.0, top,
                                   stateWidth, indicatorSize);
            painter.save();
            painter.setClipRect(slot);
            painter.setPen(Qt::NoPen);
            painter.setBrush(withAlpha(colours.onSurface, index == pressed_ ? kStatePressedAlpha
                                                                           : kStateHoverAlpha));
            painter.drawRoundedRect(stateRect, indicatorSize / 2.0, indicatorSize / 2.0);
            painter.restore();
        }
    }

    // 2. The active indicator.
    if (selected) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(enabled ? colours.secondaryContainer
                                 : withAlpha(colours.secondaryContainer, kDisabledAlpha));
        painter.drawRoundedRect(indicatorRect, indicatorSize / 2.0, indicatorSize / 2.0);
    }

    const QColor ink = !enabled
        ? withAlpha(colours.onSurface, kDisabledAlpha)
        : (selected ? colours.onSurface : colours.onSurfaceVariant);

    // 3. The icon, tinted with the destination's ink. The active icon is a
    //    filled variant in M3 and falls back to the normal one.
    const QIcon& icon = selected && !destination.activeIcon.isNull() ? destination.activeIcon
                                                                    : destination.icon;
    if (!icon.isNull()) {
        iconDrawable_.setIcon(icon);
        iconDrawable_.setSize(QSizeF(iconSize, iconSize));
        iconDrawable_.setColour(ink);
        iconDrawable_.draw(painter, iconRect);
    }

    // 4. The label.
    if (!destination.label.isEmpty()) {
        painter.save();
        painter.setClipRect(slot);
        painter.setFont(labelFont);
        painter.setPen(ink);
        const QString label = labelMetrics.elidedText(
            destination.label, rightToLeft ? Qt::ElideLeft : Qt::ElideRight,
            labelRect.width());
        painter.drawText(labelRect, static_cast<int>(Qt::AlignHCenter | Qt::AlignTop), label);
        painter.restore();
    }

    // 5. The badge, at the icon's top-trailing corner. Its width follows the
    //    text, with a circular minimum — the same rule the Badge component uses.
    if (!destination.badge.isEmpty()) {
        const QFont badgeFont = Font::resolve(this, theme.type().labelSmall, density);
        const QFontMetricsF badgeMetrics(badgeFont);
        const qreal badgeHeight = density.dp(kBadgeHeightDp);
        const qreal badgeWidth = std::max(
            badgeHeight,
            badgeMetrics.horizontalAdvance(destination.badge)
                + 2.0 * density.dp(kBadgePaddingDp));
        const qreal badgeX = rightToLeft
            ? iconRect.left() - density.dp(kBadgeTrailingOffsetDp)
            : iconRect.right() + density.dp(kBadgeTrailingOffsetDp) - badgeWidth;
        const QRectF badgeRect(badgeX, iconRect.top() - density.dp(kBadgeLiftDp),
                               badgeWidth, badgeHeight);

        painter.save();
        painter.setClipRect(slot.adjusted(-badgeWidth, 0.0, badgeWidth, 0.0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(enabled ? colours.error : withAlpha(colours.error, kDisabledAlpha));
        painter.drawRoundedRect(badgeRect, badgeHeight / 2.0, badgeHeight / 2.0);
        painter.setFont(badgeFont);
        painter.setPen(readableOn(colours.error));
        painter.drawText(badgeRect, static_cast<int>(Qt::AlignCenter), destination.badge);
        painter.restore();
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void NavigationBar::mousePressEvent(QMouseEvent* event)
{
    View::mousePressEvent(event);

    if (!isEnabled() || event->button() != Qt::LeftButton)
        return;

    const int index = destinationIndexAt(event->position());
    if (index < 0)
        return;

    setFocus(Qt::MouseFocusReason);
    pressed_ = index;
    activate(index);
    event->accept();
}

void NavigationBar::mouseReleaseEvent(QMouseEvent* event)
{
    if (pressed_ >= 0) {
        pressed_ = -1;
        requestUpdate();
    }

    View::mouseReleaseEvent(event);
}

void NavigationBar::mouseMoveEvent(QMouseEvent* event)
{
    View::mouseMoveEvent(event);

    const int index = isEnabled() ? destinationIndexAt(event->position()) : -1;
    if (index == hovered_)
        return;

    hovered_ = index;
    requestUpdate();
}

void NavigationBar::leaveEvent(QEvent* event)
{
    if (hovered_ >= 0 || pressed_ >= 0) {
        hovered_ = -1;
        pressed_ = -1;
        requestUpdate();
    }

    View::leaveEvent(event);
}

void NavigationBar::keyPressEvent(QKeyEvent* event)
{
    if (!isEnabled() || destinationCount() == 0) {
        View::keyPressEvent(event);
        return;
    }

    const bool rightToLeft = isRightToLeft();
    int target = current_;
    bool handled = true;

    switch (event->key()) {
    case Qt::Key_Right:
        // In RTL the next destination is painted to the left.
        target = current_ + (rightToLeft ? -1 : 1);
        break;
    case Qt::Key_Left:
        target = current_ + (rightToLeft ? 1 : -1);
        break;
    case Qt::Key_Down:
        target = current_ + 1;
        break;
    case Qt::Key_Up:
        target = current_ - 1;
        break;
    case Qt::Key_Home:
        target = 0;
        break;
    case Qt::Key_End:
        target = destinationCount() - 1;
        break;
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        activate(current_);
        event->accept();
        return;
    default:
        handled = false;
        break;
    }

    if (!handled) {
        View::keyPressEvent(event);
        return;
    }

    // setCurrentIndex() clamps, so the highlight stops at the first and last
    // destination rather than wrapping around.
    setCurrentIndex(target);
    event->accept();
}

void NavigationBar::changeEvent(QEvent* event)
{
    View::changeEvent(event);

    if (event->type() == QEvent::LayoutDirectionChange)
        requestUpdate();
}

void NavigationBar::onThemeChanged()
{
    setBackgroundColour(Theme::forWidget(this).colours().surfaceContainer);
    updateGeometry();
    requestUpdate();
}

} // namespace md
