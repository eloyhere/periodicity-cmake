// Periodicity — Drawer, the Material 3 navigation drawer.
//
// The widget owns three visuals that the base pipeline cannot know about: the
// scrim, the panel with its asymmetric trailing edge, and the painted rows. All
// three read from one resolved geometry (panelRect/headerRect/listRect), which
// is also what hit testing uses, so a regression in a rectangle shows up in the
// tests through the same entry points painting uses.
//
// The transition interpolates a single progress value with a QTimer and the
// theme's emphasized curve. Nothing here is a QPropertyAnimation: the drawer is
// arranged and painted from the progress, so the same code path serves the
// pointer, the keyboard and a reduced-motion theme that resolves the duration
// to zero.

#include <md/components/drawer.h>

#include <md/core/geometry.h>
#include <md/components/font.h>
#include <md/theme/theme.h>
#include <md/tokens/motion.h>
#include <md/tokens/state_layer.h>

#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QIcon>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointer>
#include <QRectF>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace md {
namespace {

constexpr qreal kDefaultWidthDp = 360.0;
constexpr qreal kPanelCornerDp = 16.0;
// M3 navigation drawer: the headline block is inset 16 dp horizontally and has
// 24 dp of padding above and below it, with a 4 dp gap between headline and
// supporting line.
constexpr qreal kHeaderSidePaddingDp = 16.0;
constexpr qreal kHeaderTopPaddingDp = 24.0;
constexpr qreal kHeaderGapDp = 4.0;
// M3 drawer item height and active-indicator height are both 56 dp; the pill is
// 336 dp wide inside a 360 dp panel, i.e. 12 dp in from each side, and its
// content is inset a further 16 dp.
constexpr qreal kItemHeightDp = 56.0;
constexpr qreal kSectionHeightDp = 40.0;
constexpr qreal kListPaddingDp = 8.0;
constexpr qreal kItemCornerInsetDp = 12.0;
constexpr qreal kItemVerticalInsetDp = 0.0;
constexpr qreal kItemContentInsetDp = 16.0;
constexpr qreal kIconSizeDp = 24.0;
constexpr qreal kIconGapDp = 12.0;
constexpr qreal kBadgeHeightDp = 16.0;
constexpr qreal kBadgePaddingDp = 10.0;
constexpr qreal kWheelStepDp = 64.0;
constexpr qreal kScrimOpacity = 0.32;
constexpr qreal kShadowSpreadDp = 8.0;
constexpr int kTickMs = 16;

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

// The panel is flush with the window edge it slides from, so only the trailing
// edge is rounded.
CornerRadii panelRadii(Drawer::Edge edge, qreal radius)
{
    if (edge == Drawer::Edge::Start)
        return CornerRadii{0.0, radius, radius, 0.0};

    return CornerRadii{radius, 0.0, 0.0, radius};
}

qreal entryHeightDp(bool section)
{
    return section ? kSectionHeightDp : kItemHeightDp;
}

} // namespace

Drawer::Drawer(QWidget* parent)
    : ViewGroup(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setBackgroundColour(QColor(Qt::transparent));
    setShape(Shape::rounded(panelRadii(edge_, Theme::forWidget(this).density().dp(kPanelCornerDp))));

    timer_ = new QTimer(this);
    timer_->setInterval(kTickMs);
    connect(timer_, &QTimer::timeout, this, [this] {
        const int duration = transitionDuration();
        if (duration <= 0) {
            finishTransition();
            return;
        }

        elapsedMs_ += kTickMs;
        const qreal linear =
            std::clamp(qreal(elapsedMs_) / qreal(duration), qreal(0.0), qreal(1.0));
        progress_ = startProgress_ + (targetProgress_ - startProgress_) * easedProgress(linear);

        if (linear >= 1.0) {
            finishTransition();
            return;
        }

        syncLayout();
    });

    // An unopened drawer is an overlay that must never paint over its host.
    hide();
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void Drawer::setEdge(Edge edge)
{
    if (edge_ == edge)
        return;

    edge_ = edge;
    setShape(Shape::rounded(panelRadii(edge_, Theme::forWidget(this).density().dp(kPanelCornerDp))));
    syncLayout();
}

Drawer::Edge Drawer::edge() const noexcept
{
    return edge_;
}

void Drawer::setMode(Mode mode)
{
    if (mode_ == mode)
        return;

    if (timer_ != nullptr)
        timer_->stop();

    open_ = false;
    closing_ = false;
    progress_ = 0.0;
    startProgress_ = 0.0;
    targetProgress_ = 0.0;
    removeFilters();
    hide();

    mode_ = mode;
    syncLayout();
}

Drawer::Mode Drawer::mode() const noexcept
{
    return mode_;
}

void Drawer::setWidthDp(qreal widthDp)
{
    const qreal clamped = widthDp > 0.0 ? widthDp : kDefaultWidthDp;
    if (qFuzzyCompare(widthDp_, clamped))
        return;

    widthDp_ = clamped;
    syncLayout();
}

qreal Drawer::widthDp() const noexcept
{
    return widthDp_;
}

void Drawer::setHeader(const QString& title, const QString& subtitle)
{
    if (headerTitle_ == title && headerSubtitle_ == subtitle)
        return;

    headerTitle_ = title;
    headerSubtitle_ = subtitle;
    scrollOffset_ = std::clamp(scrollOffset_, qreal(0.0), maxScrollOffset());
    syncLayout();
}

QString Drawer::headerTitle() const
{
    return headerTitle_;
}

QString Drawer::headerSubtitle() const
{
    return headerSubtitle_;
}

// ---------------------------------------------------------------------------
// Rows
// ---------------------------------------------------------------------------

void Drawer::addSection(const QString& title)
{
    Entry entry;
    entry.kind = Entry::Kind::Section;
    entry.text = title;
    entries_.append(entry);

    syncLayout();
}

void Drawer::addItem(const QString& label, const QIcon& icon, const QString& badge)
{
    Entry entry;
    entry.kind = Entry::Kind::Item;
    entry.text = label;
    entry.icon = icon;
    entry.badge = badge;
    entries_.append(entry);

    syncLayout();
}

int Drawer::itemCount() const
{
    int count = 0;
    for (const Entry& entry : entries_) {
        if (entry.kind == Entry::Kind::Item)
            ++count;
    }

    return count;
}

QString Drawer::itemLabel(int index) const
{
    const int entry = entryForItem(index);
    return entry >= 0 ? entries_.at(entry).text : QString();
}

QString Drawer::itemBadge(int index) const
{
    const int entry = entryForItem(index);
    return entry >= 0 ? entries_.at(entry).badge : QString();
}

void Drawer::clearItems()
{
    if (entries_.isEmpty())
        return;

    entries_.clear();
    hovered_ = -1;
    pressedItem_ = -1;
    scrollOffset_ = 0.0;

    const bool hadSelection = selected_ >= 0;
    selected_ = -1;

    syncLayout();

    if (hadSelection)
        emit selectionChanged(-1);
}

void Drawer::setSelectedIndex(int index)
{
    const int count = itemCount();
    const int clamped = (index >= 0 && index < count) ? index : -1;
    if (selected_ == clamped)
        return;

    selected_ = clamped;

    if (selected_ >= 0)
        ensureItemVisible(selected_);

    requestUpdate();
    emit selectionChanged(selected_);
}

int Drawer::selectedIndex() const noexcept
{
    return selected_;
}

void Drawer::setScrimDismissible(bool dismissible)
{
    scrimDismissible_ = dismissible;
}

bool Drawer::isScrimDismissible() const noexcept
{
    return scrimDismissible_;
}

// ---------------------------------------------------------------------------
// Open / close
// ---------------------------------------------------------------------------

void Drawer::open()
{
    if (open_)
        return;

    open_ = true;
    closing_ = false;

    const bool canOverlay = mode_ == Mode::Modal && parentWidget() != nullptr;
    if (!canOverlay) {
        // A standard drawer, or a modal one without a host, is a resident panel:
        // there is nothing to slide over and nothing to scrim.
        progress_ = 1.0;
        startProgress_ = 1.0;
        targetProgress_ = 1.0;
        show();
        syncLayout();
        if (content_ != nullptr)
        content_->show();

    emit opened();
        return;
    }

    QWidget* host = window();
    if (host != nullptr && host != this && parentWidget() != host) {
        // Leaving a container has to go through its bookkeeping, or the group
        // would keep arranging a widget it no longer owns.
        if (auto* group = qobject_cast<ViewGroup*>(parentWidget()))
            group->removeView(this);

        setParent(host);
    }

    applyOverlayGeometry();
    show();
    raise();
    setFocus(Qt::PopupFocusReason);
    installFilters();
    animateTo(1.0);

    if (content_ != nullptr)
        content_->show();

    emit opened();
}

void Drawer::close()
{
    const bool canOverlay = mode_ == Mode::Modal && parentWidget() != nullptr;

    if (!canOverlay) {
        if (!open_) {
            hide();
            return;
        }

        open_ = false;
        closing_ = false;
        progress_ = 0.0;
        startProgress_ = 0.0;
        targetProgress_ = 0.0;
        hide();
        removeFilters();
        if (content_ != nullptr)
        content_->hide();

    emit closed();
        return;
    }

    if (!open_ && !closing_) {
        hide();
        return;
    }

    open_ = false;
    closing_ = true;
    animateTo(0.0);
}

bool Drawer::isOpen() const noexcept
{
    return open_;
}

qreal Drawer::animationProgress() const noexcept
{
    return progress_;
}

bool Drawer::isAnimating() const
{
    return timer_ != nullptr && timer_->isActive();
}

void Drawer::finishAnimation()
{
    finishTransition();
}

void Drawer::animateTo(qreal target)
{
    startProgress_ = progress_;
    targetProgress_ = std::clamp(target, qreal(0.0), qreal(1.0));
    elapsedMs_ = 0;

    const int duration = transitionDuration();
    if (duration <= 0 || timer_ == nullptr) {
        progress_ = targetProgress_;
        finishTransition();
        return;
    }

    timer_->setInterval(kTickMs);
    timer_->start();
    syncLayout();
}

void Drawer::finishTransition()
{
    if (timer_ != nullptr)
        timer_->stop();

    progress_ = std::clamp(targetProgress_, qreal(0.0), qreal(1.0));

    if (progress_ <= 0.0 && closing_) {
        closing_ = false;
        hide();
        removeFilters();
        syncLayout();
        if (content_ != nullptr)
        content_->hide();

    emit closed();
        return;
    }

    syncLayout();
}

int Drawer::transitionDuration() const
{
    return static_cast<int>(Theme::forWidget(this).motion().emphasizedMedium2().duration.count());
}

qreal Drawer::easedProgress(qreal linear) const
{
    const QEasingCurve curve =
        Theme::forWidget(this).motion().emphasizedMedium2().easing.toEasingCurve();

    return curve.valueForProgress(std::clamp(linear, qreal(0.0), qreal(1.0)));
}

void Drawer::applyOverlayGeometry()
{
    if (parentWidget() == nullptr)
        return;

    arrange(QRect(QPoint(0, 0), parentWidget()->size()));
}

void Drawer::installFilters()
{
    if (filtersInstalled_)
        return;

    if (auto* application = QCoreApplication::instance())
        application->installEventFilter(this);

    if (QWidget* host = parentWidget())
        host->installEventFilter(this);

    filtersInstalled_ = true;
}

void Drawer::removeFilters()
{
    if (!filtersInstalled_)
        return;

    if (auto* application = QCoreApplication::instance())
        application->removeEventFilter(this);

    if (QWidget* host = parentWidget())
        host->removeEventFilter(this);

    filtersInstalled_ = false;
}

void Drawer::syncLayout()
{
    arrangeChildren(contentRect());
    requestUpdate();
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

bool Drawer::isModal() const noexcept
{
    return mode_ == Mode::Modal;
}

QRectF Drawer::scrimRect() const
{
    if (!isModal())
        return QRectF();

    const QRectF area = contentRect();
    if (area.isEmpty() || progress_ <= 0.0)
        return QRectF();

    return area;
}

QRectF Drawer::panelRect() const
{
    const QRectF area = contentRect();
    if (area.isEmpty())
        return QRectF();

    if (!isModal())
        return area;

    const Density& density = Theme::forWidget(this).density();
    const qreal width = std::min(density.dp(widthDp_), std::max(qreal(0.0), area.width()));
    if (width <= 0.0 || area.height() <= 0.0)
        return QRectF();

    const qreal x = edge_ == Edge::Start ? -width * (1.0 - progress_)
                                         : area.width() - width * progress_;

    return QRectF(area.left() + x, area.top(), width, area.height());
}

qreal Drawer::headerHeight() const
{
    if (headerTitle_.isEmpty() && headerSubtitle_.isEmpty())
        return 0.0;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    qreal height = density.dp(kHeaderTopPaddingDp) * 2.0
        + density.sp(theme.type().titleSmall.lineHeightSp);

    if (!headerSubtitle_.isEmpty())
        height += density.dp(kHeaderGapDp) + density.sp(theme.type().bodySmall.lineHeightSp);

    return height;
}

QRectF Drawer::headerRect() const
{
    const QRectF panel = panelRect();
    const qreal height = headerHeight();
    if (panel.isEmpty() || height <= 0.0)
        return QRectF();

    return QRectF(panel.left(), panel.top(), panel.width(), std::min(height, panel.height()));
}

QRectF Drawer::listRect() const
{
    const QRectF panel = panelRect();
    if (panel.isEmpty())
        return QRectF();

    const qreal top = panel.top() + headerHeight();
    return QRectF(panel.left(), top, panel.width(),
                  std::max(qreal(0.0), panel.bottom() - top));
}

qreal Drawer::entriesHeight() const
{
    const Density& density = Theme::forWidget(this).density();
    qreal total = density.dp(kListPaddingDp) * 2.0;

    for (const Entry& entry : entries_) {
        total += density.dp(entryHeightDp(entry.kind == Entry::Kind::Section));
    }

    return total;
}

qreal Drawer::maxScrollOffset() const
{
    return std::max(qreal(0.0), entriesHeight() - listRect().height());
}

qreal Drawer::entryTop(int entry) const
{
    if (entry < 0)
        return listRect().top();

    const Density& density = Theme::forWidget(this).density();
    qreal y = listRect().top() + density.dp(kListPaddingDp) - scrollOffset_;

    const int limit = std::min(entry, static_cast<int>(entries_.size()));
    for (int index = 0; index < limit; ++index) {
        y += density.dp(entryHeightDp(entries_.at(index).kind == Entry::Kind::Section));
    }

    return y;
}

QRectF Drawer::entryRect(int entry) const
{
    if (entry < 0 || entry >= static_cast<int>(entries_.size()))
        return QRectF();

    const QRectF list = listRect();
    if (list.isEmpty())
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const bool section = entries_.at(entry).kind == Entry::Kind::Section;
    const qreal height = density.dp(entryHeightDp(section));

    return QRectF(list.left(), entryTop(entry), list.width(), height);
}

int Drawer::entryForItem(int itemIndex) const
{
    if (itemIndex < 0)
        return -1;

    int seen = 0;
    for (int index = 0; index < static_cast<int>(entries_.size()); ++index) {
        if (entries_.at(index).kind != Entry::Kind::Item)
            continue;

        if (seen == itemIndex)
            return index;

        ++seen;
    }

    return -1;
}

int Drawer::entryAtPosition(const QPointF& position) const
{
    if (!panelRect().contains(position))
        return -1;

    const QRectF list = listRect();
    if (list.isEmpty() || !list.contains(position))
        return -1;

    for (int index = 0; index < static_cast<int>(entries_.size()); ++index) {
        if (entryRect(index).contains(position))
            return index;
    }

    return -1;
}

int Drawer::itemAtPosition(const QPointF& position) const
{
    const int entry = entryAtPosition(position);
    if (entry < 0 || entries_.at(entry).kind != Entry::Kind::Item)
        return -1;

    int item = 0;
    for (int index = 0; index < entry; ++index) {
        if (entries_.at(index).kind == Entry::Kind::Item)
            ++item;
    }

    return item;
}

QRectF Drawer::itemRect(int index) const
{
    const int entry = entryForItem(index);
    if (entry < 0)
        return QRectF();

    return entryRect(entry);
}

qreal Drawer::scrollOffset() const noexcept
{
    return scrollOffset_;
}

void Drawer::setScrollOffset(qreal offset)
{
    const qreal clamped = std::clamp(offset, qreal(0.0), maxScrollOffset());
    if (qFuzzyCompare(clamped + 1.0, scrollOffset_ + 1.0))
        return;

    scrollOffset_ = clamped;
    syncLayout();
}

void Drawer::ensureItemVisible(int index)
{
    const QRectF row = itemRect(index);
    if (row.isEmpty())
        return;

    const QRectF list = listRect();
    if (list.isEmpty())
        return;

    if (row.top() < list.top())
        setScrollOffset(scrollOffset_ - (list.top() - row.top()));
    else if (row.bottom() > list.bottom())
        setScrollOffset(scrollOffset_ + (row.bottom() - list.bottom()));
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

void Drawer::applySelection(int index, bool notify)
{
    const int count = itemCount();
    const int clamped = (index >= 0 && index < count) ? index : -1;
    if (selected_ == clamped)
        return;

    selected_ = clamped;
    requestUpdate();

    if (notify)
        emit selectionChanged(selected_);
}

void Drawer::activateItem(int index)
{
    if (index < 0 || index >= itemCount())
        return;

    applySelection(index, true);
    ensureItemVisible(index);

    const QString label = itemLabel(index);

    // A slot may close the host, or delete the drawer; the guard turns the
    // follow-up close() into a no-op instead of a dangling call.
    const QPointer<Drawer> guard(this);
    emit itemActivated(index, label);

    if (!guard.isNull() && isModal() && open_)
        close();
}

// ---------------------------------------------------------------------------
// Measure / arrange / paint
// ---------------------------------------------------------------------------

void Drawer::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    const qreal naturalWidth = density.dp(widthDp_);
    const qreal naturalHeight = density.dp(640.0);

    setMeasuredSize(QSizeF(width.resolve(naturalWidth), height.resolve(naturalHeight)));
}

void Drawer::arrangeChildren(const QRectF& content)
{
    if (content_ == nullptr)
        return;

    const QRectF list = listRect();
    if (list.isEmpty())
        return;

    content_->measure(MeasureSpec::exactly(list.width()), MeasureSpec::exactly(list.height()));
    content_->arrange(list.toAlignedRect());
}

void Drawer::setContent(View* content)
{
    if (content_ == content)
        return;

    if (content_ != nullptr)
        removeView(content_);

    content_ = content;

    if (content_ != nullptr) {
        addView(content_);
        content_->setVisible(isVisible() && open_);
    }

    requestUpdate();
    syncLayout();
}

void Drawer::paintContent(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF scrim = scrimRect();
    if (!scrim.isEmpty())
        painter.fillRect(scrim, withAlpha(colours.scrim, kScrimOpacity * progress_));

    const QRectF panel = panelRect();
    if (panel.isEmpty()) {
        painter.restore();
        return;
    }

    const CornerRadii radii = panelRadii(edge_, density.dp(kPanelCornerDp));
    const QPainterPath panelPath = radii.toPath(panel);

    if (isModal()) {
        const qreal spread = density.dp(kShadowSpreadDp);
        for (int step = 3; step >= 1; --step) {
            const qreal inset = spread * static_cast<qreal>(step) / 3.0;
            painter.setPen(Qt::NoPen);
            painter.setBrush(withAlpha(colours.shadow, 0.10));
            painter.drawPath(radii.toPath(panel.adjusted(-inset, -inset, inset, inset)));
        }
    }

    // M3 modal drawer: surface container low at elevation level 1, with no
    // outline — the shadow and the scrim already separate it from the page. A
    // full 1 dp outline also drew a border along the three window edges the
    // panel is attached to, which read as a double edge next to the host.
    painter.setPen(Qt::NoPen);
    painter.setBrush(colours.surfaceContainerLow);
    painter.drawPath(panelPath);

    if (!isModal()) {
        // A standard drawer is flush with the page, so it keeps a single
        // hairline on its trailing edge instead of an outline all round. The
        // half-pixel offset keeps the 1 dp line inside the panel instead of
        // being clipped by the widget edge.
        const qreal half = density.dp(0.5);
        const qreal edgeX = edge_ == Edge::Start ? panel.right() - half : panel.left() + half;
        painter.setPen(QPen(colours.outlineVariant, density.dp(1.0)));
        painter.drawLine(QPointF(edgeX, panel.top()), QPointF(edgeX, panel.bottom()));
        painter.setPen(Qt::NoPen);
    }

    painter.save();
    painter.setClipPath(panelPath);
    painter.setClipRect(listRect());

    paintRows(painter);

    painter.restore();

    // The header sits above the list clip, so it is painted outside it.
    painter.save();
    painter.setClipPath(panelPath);

    const QRectF header = headerRect();
    if (!header.isEmpty()) {
        const qreal padding = density.dp(kHeaderSidePaddingDp);
        const qreal width = std::max(qreal(0.0), header.width() - padding * 2.0);

        // M3 nav drawer headline: the small headline role, top-aligned inside a
        // 24 dp block, with the supporting line 4 dp under it.
        const QFont titleFont = fontFor(theme.type().titleSmall, density, this);
        const QFont subtitleFont = fontFor(theme.type().bodySmall, density, this);
        const qreal titleHeight = density.sp(theme.type().titleSmall.lineHeightSp);
        const qreal subtitleHeight =
            headerSubtitle_.isEmpty() ? 0.0 : density.sp(theme.type().bodySmall.lineHeightSp);
        const qreal y = header.top() + density.dp(kHeaderTopPaddingDp);

        if (!headerTitle_.isEmpty() && width > 0.0) {
            const QString elided = QFontMetricsF(titleFont).elidedText(
                headerTitle_, Qt::ElideRight, width);
            painter.setFont(titleFont);
            painter.setPen(colours.onSurface);
            painter.drawText(QRectF(header.left() + padding, y, width, titleHeight),
                             static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), elided);
        }

        if (!headerSubtitle_.isEmpty() && width > 0.0) {
            const QString elided = QFontMetricsF(subtitleFont).elidedText(
                headerSubtitle_, Qt::ElideRight, width);
            painter.setFont(subtitleFont);
            painter.setPen(colours.onSurfaceVariant);
            painter.drawText(
                QRectF(header.left() + padding, y + titleHeight + density.dp(kHeaderGapDp),
                       width, subtitleHeight),
                static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), elided);
        }
    }

    painter.restore();
    painter.restore();
}

void Drawer::paintRows(QPainter& painter) const
{
    if (content_ != nullptr)
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    const qreal cornerInset = density.dp(kItemCornerInsetDp);
    const qreal verticalInset = density.dp(kItemVerticalInsetDp);
    const qreal contentInset = density.dp(kItemContentInsetDp);
    const qreal iconSize = density.dp(kIconSizeDp);
    const qreal iconGap = density.dp(kIconGapDp);
    const qreal badgeHeight = density.dp(kBadgeHeightDp);

    const QFont labelFont = fontFor(theme.type().bodyLarge, density, this);
    const QFont sectionFont = fontFor(theme.type().titleSmall, density, this);
    const QFont badgeFont = fontFor(theme.type().labelSmall, density, this);
    const QFontMetricsF labelMetrics(labelFont);
    const QFontMetricsF badgeMetrics(badgeFont);

    int item = 0;
    for (int index = 0; index < static_cast<int>(entries_.size()); ++index) {
        const Entry& entry = entries_.at(index);
        const QRectF row = entryRect(index);
        if (row.isEmpty())
            continue;

        if (entry.kind == Entry::Kind::Section) {
            if (index > 0) {
                painter.setPen(QPen(colours.outlineVariant, density.dp(1.0)));
                const qreal y = row.top() + density.dp(8.0);
                painter.drawLine(QPointF(row.left() + contentInset, y),
                                 QPointF(row.right() - contentInset, y));
            }

            const QRectF labelRect(row.left() + contentInset, row.bottom() - density.dp(28.0),
                                   std::max(qreal(0.0), row.width() - contentInset * 2.0),
                                   density.dp(20.0));
            if (labelRect.width() > 0.0) {
                const QString elided =
                    QFontMetricsF(sectionFont).elidedText(entry.text, Qt::ElideRight,
                                                          labelRect.width());
                painter.setFont(sectionFont);
                painter.setPen(colours.onSurfaceVariant);
                painter.drawText(labelRect,
                                 static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), elided);
            }

            continue;
        }

        const QRectF pill = row.adjusted(cornerInset, verticalInset, -cornerInset,
                                         -verticalInset);
        const bool selected = item == selected_;
        const bool hovered = item == hovered_;

        if (selected) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colours.secondaryContainer);
            painter.drawPath(theme.shapes().full.path(pill));
        } else if (hovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colours.stateLayer(colours.onSurface, ViewState::Hovered));
            painter.drawPath(theme.shapes().full.path(pill));
        }

        const QColor contentColour = selected ? colours.onSecondaryContainer : colours.onSurface;

        qreal left = pill.left() + contentInset;
        qreal right = pill.right() - contentInset;

        if (!entry.badge.isEmpty() && right > left) {
            const qreal badgeWidth =
                std::max(badgeHeight,
                         badgeMetrics.horizontalAdvance(entry.badge) + density.dp(kBadgePaddingDp));
            const QRectF badgeRect(right - badgeWidth, pill.center().y() - badgeHeight / 2.0,
                                   badgeWidth, badgeHeight);
            painter.setPen(Qt::NoPen);
            painter.setBrush(colours.error);
            painter.drawPath(theme.shapes().full.path(badgeRect));
            painter.setFont(badgeFont);
            painter.setPen(colours.onError);
            painter.drawText(badgeRect, static_cast<int>(Qt::AlignCenter), entry.badge);

            right -= badgeWidth + density.dp(kIconGapDp);
        }

        if (!entry.icon.isNull() && right > left) {
            const QRect iconRect(qRound(left), qRound(pill.center().y() - iconSize / 2.0),
                                 qRound(iconSize), qRound(iconSize));
            entry.icon.paint(&painter, iconRect, Qt::AlignCenter,
                             selected ? QIcon::Selected : QIcon::Normal);
            left += iconSize + iconGap;
        }

        const qreal labelWidth = std::max(qreal(0.0), right - left);
        if (labelWidth > 0.0) {
            const QString elided =
                labelMetrics.elidedText(entry.text, Qt::ElideRight, labelWidth);
            painter.setFont(labelFont);
            painter.setPen(contentColour);
            painter.drawText(QRectF(left, pill.top(), labelWidth, pill.height()),
                             static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), elided);
        }

        ++item;
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void Drawer::mousePressEvent(QMouseEvent* event)
{
    ViewGroup::mousePressEvent(event);

    if (event->button() != Qt::LeftButton)
        return;

    setFocus(Qt::MouseFocusReason);

    const QPointF position = event->position();
    pressedScrim_ = isModal() && !panelRect().contains(position);
    pressedItem_ = pressedScrim_ ? -1 : itemAtPosition(position);
}

void Drawer::mouseReleaseEvent(QMouseEvent* event)
{
    ViewGroup::mouseReleaseEvent(event);

    if (event->button() != Qt::LeftButton) {
        pressedScrim_ = false;
        pressedItem_ = -1;
        return;
    }

    const QPointF position = event->position();
    const bool onScrim = pressedScrim_ && isModal() && !panelRect().contains(position)
                         && rect().contains(position.toPoint());
    const int item =
        (pressedItem_ >= 0 && itemAtPosition(position) == pressedItem_) ? pressedItem_ : -1;

    pressedScrim_ = false;
    pressedItem_ = -1;

    if (onScrim) {
        if (scrimDismissible_)
            close();
        return;
    }

    if (item >= 0)
        activateItem(item);
}

void Drawer::mouseMoveEvent(QMouseEvent* event)
{
    ViewGroup::mouseMoveEvent(event);

    const int hovered = itemAtPosition(event->position());
    if (hovered == hovered_)
        return;

    hovered_ = hovered;
    requestUpdate();
}

void Drawer::leaveEvent(QEvent* event)
{
    ViewGroup::leaveEvent(event);

    if (hovered_ < 0)
        return;

    hovered_ = -1;
    requestUpdate();
}

void Drawer::keyPressEvent(QKeyEvent* event)
{
    if (!isEnabled()) {
        ViewGroup::keyPressEvent(event);
        return;
    }

    const int count = itemCount();

    switch (event->key()) {
    case Qt::Key_Escape:
        if (isModal()) {
            close();
            event->accept();
            return;
        }
        break;

    case Qt::Key_Up:
        applySelection(selected_ <= 0 ? 0 : selected_ - 1, true);
        ensureItemVisible(selected_);
        event->accept();
        return;

    case Qt::Key_Down:
        applySelection(selected_ < 0 ? 0 : std::min(count - 1, selected_ + 1), true);
        ensureItemVisible(selected_);
        event->accept();
        return;

    case Qt::Key_Home:
        applySelection(0, true);
        ensureItemVisible(0);
        event->accept();
        return;

    case Qt::Key_End:
        applySelection(count - 1, true);
        ensureItemVisible(count - 1);
        event->accept();
        return;

    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        if (selected_ >= 0)
            activateItem(selected_);
        event->accept();
        return;

    default:
        break;
    }

    ViewGroup::keyPressEvent(event);
}

void Drawer::wheelEvent(QWheelEvent* event)
{
    const qreal steps = static_cast<qreal>(event->angleDelta().y()) / 120.0;
    if (qFuzzyIsNull(steps) || maxScrollOffset() <= 0.0) {
        event->ignore();
        return;
    }

    const Density& density = Theme::forWidget(this).density();
    setScrollOffset(scrollOffset_ - steps * density.dp(kWheelStepDp));
    event->accept();
}

bool Drawer::eventFilter(QObject* watched, QEvent* event)
{
    if (isModal() && (open_ || closing_)) {
        if (watched == parentWidget()) {
            switch (event->type()) {
            case QEvent::Resize:
                applyOverlayGeometry();
                break;
            case QEvent::Hide:
                close();
                break;
            default:
                break;
            }
        }

        // Escape reaches the host through the application filter when focus is
        // elsewhere; a key event already aimed at the drawer is left to
        // keyPressEvent() so it is handled exactly once.
        if (event->type() == QEvent::KeyPress && watched != this) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Escape) {
                close();
                return true;
            }
        }
    }

    return ViewGroup::eventFilter(watched, event);
}

void Drawer::onThemeChanged()
{
    setShape(Shape::rounded(panelRadii(edge_, Theme::forWidget(this).density().dp(kPanelCornerDp))));
    updateGeometry();
    syncLayout();
}

bool Drawer::focusable() const
{
    // The keyboard indicator is the selected row; a ring around the overlay
    // would suggest the scrim itself is one control.
    return false;
}

} // namespace md
