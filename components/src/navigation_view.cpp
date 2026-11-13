#include <md/components/navigation_view.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPointer>
#include <QWheelEvent>

#include <algorithm>

namespace md {
namespace {

constexpr qreal kWidthDp = 320.0;
constexpr qreal kHeaderHeightDp = 96.0;
constexpr qreal kHeaderPaddingDp = 16.0;
constexpr qreal kSectionHeightDp = 40.0;
constexpr qreal kItemHeightDp = 48.0;
constexpr qreal kDividerHeightDp = 9.0;
constexpr qreal kItemInsetDp = 12.0;
constexpr qreal kItemVerticalInsetDp = 2.0;
constexpr qreal kItemContentInsetDp = 12.0;
constexpr qreal kIconSizeDp = 24.0;
constexpr qreal kIconGapDp = 12.0;
constexpr qreal kBadgeHeightDp = 18.0;
constexpr qreal kBadgePaddingDp = 8.0;
constexpr qreal kWheelStepDp = 64.0;

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

NavigationView::NavigationView(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    // The kernel ripple would cover the whole panel; this view paints a row
    // state layer and a row-scoped ripple instead.
    setInteractionFeedbackEnabled(false);
    setBackgroundColour(Theme::forWidget(this).colours().surface);
    rowRipple_.setEnabled(true);
    rowRipple_.setTheme(Theme::forWidget(this));
    rowRipple_.setContentColour(Theme::forWidget(this).colours().onSurface);
    // The controller animates expanding/fading; without a repaint request for
    // every tick the row ripple would only show its final frame.
    connect(&rowRipple_, &ripple::RippleController::repaintRequested, this,
            [this] { requestUpdate(); });
    setAccessibleName(tr("Navigation"));
}

// ---------------------------------------------------------------------------
// Entries
// ---------------------------------------------------------------------------

int NavigationView::addSection(const QString& title)
{
    Entry entry;
    entry.kind = Entry::Kind::Header;
    entry.label = title;
    entries_.append(entry);
    updateGeometry();
    requestUpdate();
    return entryCount() - 1;
}

int NavigationView::addItem(const QString& label, const QIcon& icon, const QString& badge)
{
    Entry entry;
    entry.kind = Entry::Kind::Item;
    entry.label = label;
    entry.icon = icon;
    entry.badge = badge;
    entries_.append(entry);
    updateGeometry();
    requestUpdate();
    return entryCount() - 1;
}

int NavigationView::addDivider()
{
    Entry entry;
    entry.kind = Entry::Kind::Divider;
    entries_.append(entry);
    updateGeometry();
    requestUpdate();
    return entryCount() - 1;
}

void NavigationView::setEntries(const QList<Entry>& entries)
{
    entries_ = entries;

    if (selected_ >= entryCount() || selected_ < -1
        || (selected_ >= 0
            && (entries_.at(selected_).kind != Entry::Kind::Item
                || !entries_.at(selected_).enabled))) {
        selected_ = -1;
    }

    clampScroll();
    updateGeometry();
    requestUpdate();
}

void NavigationView::clearEntries()
{
    entries_.clear();
    selected_ = -1;
    scroll_ = 0.0;
    updateGeometry();
    requestUpdate();
}

int NavigationView::itemCount() const
{
    int count = 0;
    for (const Entry& entry : entries_) {
        if (entry.kind == Entry::Kind::Item)
            ++count;
    }
    return count;
}

NavigationView::Entry NavigationView::entry(int index) const
{
    if (index < 0 || index >= entryCount())
        return {};
    return entries_.at(index);
}

void NavigationView::setEntryEnabled(int index, bool enabled)
{
    if (index < 0 || index >= entryCount())
        return;
    if (entries_.at(index).kind != Entry::Kind::Item)
        return;
    if (entries_.at(index).enabled == enabled)
        return;

    entries_[index].enabled = enabled;
    if (!enabled && selected_ == index) {
        selected_ = -1;
        emit selectionChanged(-1);
    }
    requestUpdate();
}

bool NavigationView::isEntryEnabled(int index) const
{
    if (index < 0 || index >= entryCount())
        return false;
    return entries_.at(index).enabled;
}

void NavigationView::setEntryBadge(int index, const QString& badge)
{
    if (index < 0 || index >= entryCount())
        return;
    if (entries_.at(index).badge == badge)
        return;

    entries_[index].badge = badge;
    requestUpdate();
}

void NavigationView::setSelectedIndex(int index)
{
    if (index < -1 || index >= entryCount())
        return;

    if (index >= 0) {
        const Entry& entry = entries_.at(index);
        if (entry.kind != Entry::Kind::Item || !entry.enabled)
            return;
    }

    if (selected_ == index)
        return;

    selected_ = index;
    requestUpdate();
    emit selectionChanged(selected_);
}

QString NavigationView::selectedLabel() const
{
    if (selected_ < 0 || selected_ >= entryCount())
        return {};
    return entries_.at(selected_).label;
}

void NavigationView::clearSelection()
{
    setSelectedIndex(-1);
}

// ---------------------------------------------------------------------------
// Header and scrolling
// ---------------------------------------------------------------------------

void NavigationView::setHeaderTitle(const QString& title)
{
    if (headerTitle_ == title)
        return;
    headerTitle_ = title;
    updateGeometry();
    requestUpdate();
}

void NavigationView::setHeaderSubtitle(const QString& subtitle)
{
    if (headerSubtitle_ == subtitle)
        return;
    headerSubtitle_ = subtitle;
    updateGeometry();
    requestUpdate();
}

void NavigationView::setHeaderVisible(bool visible)
{
    if (headerVisible_ == visible)
        return;
    headerVisible_ = visible;
    clampScroll();
    updateGeometry();
    requestUpdate();
}

void NavigationView::setScrollOffset(qreal offset)
{
    const qreal clamped = std::max(qreal(0.0), offset);
    if (qFuzzyCompare(scroll_ + 1.0, clamped + 1.0))
        return;
    scroll_ = clamped;
    clampScroll();
    requestUpdate();
}

void NavigationView::clampScroll()
{
    const qreal viewport = std::max(qreal(0.0), height() - entriesTop());
    const qreal maximum = std::max(qreal(0.0), contentHeight() - entriesTop() - viewport);
    scroll_ = std::clamp(scroll_, qreal(0.0), maximum);
}

qreal NavigationView::contentHeight() const
{
    qreal total = entriesTop();
    for (const Entry& entry : entries_)
        total += entryHeight(entry);
    return total;
}

qreal NavigationView::entryHeight(const Entry& entry) const
{
    const Density& density = Theme::forWidget(this).density();
    switch (entry.kind) {
    case Entry::Kind::Header:
        return density.dp(kSectionHeightDp);
    case Entry::Kind::Divider:
        return density.dp(kDividerHeightDp);
    case Entry::Kind::Item:
    default:
        return density.dp(kItemHeightDp);
    }
}

qreal NavigationView::entriesTop() const
{
    const Density& density = Theme::forWidget(this).density();
    return headerVisible_ ? density.dp(kHeaderHeightDp) : 0.0;
}

QRectF NavigationView::headerRect() const
{
    if (!headerVisible_)
        return {};
    const Density& density = Theme::forWidget(this).density();
    return QRectF(0.0, 0.0, width(), density.dp(kHeaderHeightDp));
}

QRectF NavigationView::entryRect(int index) const
{
    if (index < 0 || index >= entryCount())
        return {};

    qreal y = entriesTop() - scroll_;
    for (int i = 0; i < index; ++i)
        y += entryHeight(entries_.at(i));

    return QRectF(0.0, y, width(), entryHeight(entries_.at(index)));
}

int NavigationView::entryIndexAt(const QPointF& position) const
{
    for (int i = 0; i < entryCount(); ++i) {
        if (entryRect(i).contains(position))
            return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Selection helpers
// ---------------------------------------------------------------------------

int NavigationView::firstItem() const
{
    for (int i = 0; i < entryCount(); ++i) {
        const Entry& entry = entries_.at(i);
        if (entry.kind == Entry::Kind::Item && entry.enabled)
            return i;
    }
    return -1;
}

int NavigationView::nextItem(int from, int direction) const
{
    if (entries_.isEmpty())
        return -1;

    int index = from;
    for (int guard = 0; guard < entryCount(); ++guard) {
        index += direction;
        if (index < 0 || index >= entryCount())
            return -1;

        const Entry& entry = entries_.at(index);
        if (entry.kind == Entry::Kind::Item && entry.enabled)
            return index;
    }
    return -1;
}

void NavigationView::activate(int index)
{
    if (index < 0 || index >= entryCount())
        return;

    const Entry& entry = entries_.at(index);
    if (entry.kind != Entry::Kind::Item || !entry.enabled)
        return;

    // Copy everything read after the notification before causing it: a
    // selectionChanged handler may call setEntries()/clearEntries(), which
    // frees the entry this reference points into.
    const QString label = entry.label;

    QPointer<NavigationView> guard(this);
    setSelectedIndex(index);
    if (guard.isNull())
        return;

    emit itemActivated(index, label);
}

// ---------------------------------------------------------------------------
// Measuring, painting and input
// ---------------------------------------------------------------------------

void NavigationView::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    setMeasuredSize(QSizeF(width.resolve(density.dp(kWidthDp)),
                           height.resolve(contentHeight())));
}

void NavigationView::paintContent(QPainter& painter, const QRectF& content)
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    if (headerVisible_ && !headerRect().isEmpty()) {
        const QRectF header = headerRect();
        const qreal padding = density.dp(kHeaderPaddingDp);

        painter.setFont(fontFor(theme.type().titleLarge, density, this));
        painter.setPen(colours.onSurface);
        const QFontMetricsF titleMetrics(painter.font());
        const qreal titleWidth = std::max(qreal(0.0), header.width() - 2.0 * padding);
        const QString title = titleMetrics.elidedText(headerTitle_,
                                                      rightToLeft ? Qt::ElideLeft : Qt::ElideRight,
                                                      titleWidth);
        qreal baseline = header.top() + padding + titleMetrics.ascent();
        painter.drawText(QPointF(header.left() + padding, baseline), title);

        if (!headerSubtitle_.isEmpty()) {
            painter.setFont(fontFor(theme.type().bodyMedium, density, this));
            painter.setPen(colours.onSurfaceVariant);
            const QFontMetricsF subtitleMetrics(painter.font());
            baseline += subtitleMetrics.height() + density.dp(2.0);
            const QString subtitle = subtitleMetrics.elidedText(
                headerSubtitle_, rightToLeft ? Qt::ElideLeft : Qt::ElideRight, titleWidth);
            painter.drawText(QPointF(header.left() + padding, baseline), subtitle);
        }
    }

    const qreal entriesTop = this->entriesTop();
    painter.setClipRect(QRectF(content.left(), entriesTop, content.width(),
                               std::max(qreal(0.0), content.height() - entriesTop)));

    for (int index = 0; index < entryCount(); ++index) {
        const Entry& entry = entries_.at(index);
        const QRectF row = entryRect(index);
        if (!row.intersects(painter.clipBoundingRect()))
            continue;

        if (entry.kind == Entry::Kind::Divider) {
            const qreal inset = density.dp(kItemInsetDp);
            const qreal y = row.center().y();
            painter.setPen(QPen(colours.outlineVariant, density.dp(1.0)));
            painter.drawLine(QPointF(row.left() + inset, y),
                             QPointF(row.right() - inset, y));
            continue;
        }

        if (entry.kind == Entry::Kind::Header) {
            painter.setFont(fontFor(theme.type().labelSmall, density, this));
            painter.setPen(colours.onSurfaceVariant);
            painter.drawText(row.adjusted(density.dp(kItemInsetDp), 0,
                                          -density.dp(kItemInsetDp), 0),
                             Qt::AlignLeft | Qt::AlignVCenter, entry.label);
            continue;
        }

        // A destination: an optional pill, a leading icon, the label and a
        // trailing badge.
        const qreal inset = density.dp(kItemInsetDp);
        const QRectF pill = row.adjusted(inset, density.dp(kItemVerticalInsetDp),
                                         -inset, -density.dp(kItemVerticalInsetDp));
        if (index == selected_) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colours.secondaryContainer);
            painter.drawRoundedRect(pill, pill.height() / 2.0, pill.height() / 2.0);
        }

        const QColor labelColour = !entry.enabled
            ? withAlpha(colours.onSurface, 0.38)
            : (index == selected_ ? colours.onSecondaryContainer : colours.onSurface);

        const qreal contentInset = density.dp(kItemContentInsetDp);
        const qreal iconSize = density.dp(kIconSizeDp);
        const qreal iconGap = density.dp(kIconGapDp);

        QRectF iconRect;
        if (rightToLeft) {
            iconRect = QRectF(pill.right() - contentInset - iconSize,
                              pill.center().y() - iconSize / 2.0, iconSize, iconSize);
        } else {
            iconRect = QRectF(pill.left() + contentInset,
                              pill.center().y() - iconSize / 2.0, iconSize, iconSize);
        }

        if (!entry.icon.isNull()) {
            entry.icon.paint(&painter, iconRect.toRect(), Qt::AlignCenter,
                             entry.enabled ? QIcon::Normal : QIcon::Disabled);
        }

        // Badge first: its width decides how much room the label has.
        qreal badgeWidth = 0.0;
        QRectF badgeRect;
        if (!entry.badge.isEmpty()) {
            painter.setFont(fontFor(theme.type().labelSmall, density, this));
            const QFontMetricsF badgeMetrics(painter.font());
            badgeWidth = badgeMetrics.horizontalAdvance(entry.badge)
                + 2.0 * density.dp(kBadgePaddingDp);
            const qreal badgeHeight = density.dp(kBadgeHeightDp);
            const qreal x = rightToLeft ? pill.left() + contentInset
                                        : pill.right() - contentInset - badgeWidth;
            badgeRect = QRectF(x, pill.center().y() - badgeHeight / 2.0, badgeWidth,
                               badgeHeight);
        }

        const qreal textLeft = rightToLeft
            ? pill.left() + contentInset + badgeWidth
            : iconRect.right() + (entry.icon.isNull() ? 0.0 : iconGap);
        const qreal textRight = rightToLeft
            ? iconRect.left() - (entry.icon.isNull() ? 0.0 : iconGap)
            : pill.right() - contentInset - badgeWidth;
        const QRectF textRect(textLeft, pill.top(), std::max(qreal(0.0), textRight - textLeft),
                              pill.height());

        painter.setFont(fontFor(theme.type().bodyLarge, density, this));
        painter.setPen(labelColour);
        const QFontMetricsF labelMetrics(painter.font());
        const QString label = labelMetrics.elidedText(
            entry.label, rightToLeft ? Qt::ElideLeft : Qt::ElideRight, textRect.width());
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, label);

        if (!entry.badge.isEmpty()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(entry.enabled ? colours.primaryContainer
                                           : colours.surfaceContainerHighest);
            painter.drawRoundedRect(badgeRect, badgeRect.height() / 2.0,
                                    badgeRect.height() / 2.0);
            painter.setFont(fontFor(theme.type().labelSmall, density, this));
            painter.setPen(entry.enabled ? colours.onPrimaryContainer
                                         : withAlpha(colours.onSurface, 0.38));
            painter.drawText(badgeRect, Qt::AlignCenter, entry.badge);
        }
    }

    if (hovered_ >= 0 && isEnabled()) {
        const QRectF hoveredRow = entryRect(hovered_);
        if (!hoveredRow.isEmpty()) {
            const Theme& theme = Theme::forWidget(this);
            painter.save();
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(Qt::NoPen);
            painter.setBrush(withAlpha(theme.colours().onSurface, 0.08));
            const qreal inset = theme.density().dp(4.0);
            const qreal radius = hoveredRow.height() / 2.0;
            painter.drawRoundedRect(
                hoveredRow.adjusted(inset, 0.0, -inset, 0.0), radius, radius);
            painter.restore();
        }
    }

    rowRipple_.paint(painter);

    painter.restore();
}

void NavigationView::mousePressEvent(QMouseEvent* event)
{
    View::mousePressEvent(event);

    if (!isEnabled() || event->button() != Qt::LeftButton)
        return;

    const int index = entryIndexAt(event->position());
    if (index < 0)
        return;

    const Entry& entry = entries_.at(index);
    if (entry.kind != Entry::Kind::Item || !entry.enabled)
        return;

    const Theme& theme = Theme::forWidget(this);
    rowRipple_.setBounds(entryRect(index));
    rowRipple_.setTheme(theme);
    rowRipple_.setCornerRadius(theme.density().dp(28.0));
    rowRipple_.setContentColour(theme.colours().onSurface);
    rowRipple_.pointerPressed(event->position());

    activate(index);
}

void NavigationView::mouseMoveEvent(QMouseEvent* event)
{
    View::mouseMoveEvent(event);

    const int index = entryIndexAt(event->position());
    int hovered = -1;
    if (index >= 0) {
        const Entry& entry = entries_.at(index);
        if (entry.kind == Entry::Kind::Item && entry.enabled)
            hovered = index;
    }

    if (hovered != hovered_) {
        hovered_ = hovered;
        requestUpdate();
    }
}

void NavigationView::mouseReleaseEvent(QMouseEvent* event)
{
    rowRipple_.pointerReleased(event->position());
    View::mouseReleaseEvent(event);
}

void NavigationView::leaveEvent(QEvent* event)
{
    hovered_ = -1;
    rowRipple_.pointerLeft();
    requestUpdate();
    View::leaveEvent(event);
}

void NavigationView::keyPressEvent(QKeyEvent* event)
{
    if (!isEnabled()) {
        View::keyPressEvent(event);
        return;
    }

    switch (event->key()) {
    case Qt::Key_Down: {
        const int next = nextItem(selected_, 1);
        setSelectedIndex(next >= 0 ? next : selected_);
        event->accept();
        return;
    }
    case Qt::Key_Up: {
        const int previous = nextItem(selected_, -1);
        setSelectedIndex(previous >= 0 ? previous : selected_);
        event->accept();
        return;
    }
    case Qt::Key_Home:
        setSelectedIndex(firstItem());
        event->accept();
        return;
    case Qt::Key_End: {
        const int last = nextItem(entryCount(), -1);
        setSelectedIndex(last);
        event->accept();
        return;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        activate(selected_ >= 0 ? selected_ : firstItem());
        event->accept();
        return;
    default:
        break;
    }

    View::keyPressEvent(event);
}

void NavigationView::wheelEvent(QWheelEvent* event)
{
    if (!isEnabled()) {
        View::wheelEvent(event);
        return;
    }

    const int delta = event->angleDelta().y();
    if (delta != 0) {
        const qreal step = Theme::forWidget(this).density().dp(kWheelStepDp);
        setScrollOffset(scroll_ - (delta / 120.0) * step);
        event->accept();
        return;
    }

    View::wheelEvent(event);
}

void NavigationView::changeEvent(QEvent* event)
{
    View::changeEvent(event);

    if (event->type() == QEvent::LayoutDirectionChange)
        requestUpdate();
}

void NavigationView::onThemeChanged()
{
    setBackgroundColour(Theme::forWidget(this).colours().surface);
    rowRipple_.setTheme(Theme::forWidget(this));
    rowRipple_.setContentColour(Theme::forWidget(this).colours().onSurface);
    updateGeometry();
    requestUpdate();
}

bool NavigationView::focusable() const
{
    return isEnabled();
}

} // namespace md
