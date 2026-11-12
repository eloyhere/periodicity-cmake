// Periodicity — TabBar.
//
// The bar is one painted surface: tabs are values, not child widgets. That is
// what makes the indicator animation cheap (one moving rectangle) and the
// scrollable mode possible without a viewport widget.

#include <md/components/tabbar.h>

#include <md/theme/theme.h>

#include <QEasingCurve>
#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>
#include <QResizeEvent>
#include <QSizeF>
#include <QWheelEvent>

#include <algorithm>

namespace md {

namespace {

constexpr qreal kBarHeightDp = 48.0;
constexpr qreal kIndicatorHeightDp = 3.0;
constexpr qreal kHorizontalPaddingDp = 16.0;
constexpr qreal kMinScrollableTabWidthDp = 72.0;
constexpr qreal kMaxScrollableTabWidthDp = 264.0;
constexpr qreal kIconSizeDp = 18.0;
constexpr qreal kIconGapDp = 6.0;
constexpr qreal kScrollStepDp = 48.0;

QFont labelFontFor(const Theme& theme)
{
    const TextStyle& style = theme.type().labelLarge;
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(static_cast<QFont::Weight>(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
    return font;
}

QFont badgeFontFor(const Theme& theme)
{
    const TextStyle& style = theme.type().labelSmall;
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(static_cast<QFont::Weight>(style.weight));
    return font;
}

QColor withAlpha(const QColor& colour, qreal alpha)
{
    QColor result = colour;
    result.setAlphaF(static_cast<float>(std::clamp(alpha, qreal(0.0), qreal(1.0))));
    return result;
}

QRectF interpolate(const QRectF& from, const QRectF& to, qreal progress)
{
    const qreal x = from.x() + (to.x() - from.x()) * progress;
    const qreal y = from.y() + (to.y() - from.y()) * progress;
    const qreal width = from.width() + (to.width() - from.width()) * progress;
    const qreal height = from.height() + (to.height() - from.height()) * progress;
    return QRectF(x, y, std::max(qreal(0.0), width), std::max(qreal(0.0), height));
}

} // namespace

TabBar::TabBar(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setBackgroundColour(QColor(Qt::transparent));

    indicatorTimer_.setInterval(16);
    connect(&indicatorTimer_, &QTimer::timeout, this, [this] { tickIndicator(); });
}

void TabBar::invalidateLayout()
{
    layoutDirty_ = true;
}

void TabBar::setTabs(const QList<Tab>& tabs)
{
    tabs_ = tabs;
    hovered_ = -1;

    if (current_ >= count())
        current_ = count() > 0 ? 0 : -1;

    invalidateLayout();
    clampScroll();
    snapIndicator();
    requestUpdate();
}

QList<Tab> TabBar::tabs() const
{
    return tabs_;
}

int TabBar::count() const
{
    return static_cast<int>(tabs_.size());
}

Tab TabBar::tabAt(int index) const
{
    if (index < 0 || index >= count())
        return Tab();

    return tabs_.at(index);
}

void TabBar::addTab(const Tab& tab)
{
    insertTab(count(), tab);
}

void TabBar::insertTab(int index, const Tab& tab)
{
    const int position = std::clamp(index, 0, count());
    tabs_.insert(position, tab);

    if (current_ >= position)
        ++current_;

    invalidateLayout();
    clampScroll();
    snapIndicator();
    requestUpdate();
}

void TabBar::removeTab(int index)
{
    if (index < 0 || index >= count())
        return;

    tabs_.removeAt(index);

    if (current_ == index)
        current_ = -1;
    else if (current_ > index)
        --current_;

    invalidateLayout();
    clampScroll();
    snapIndicator();
    requestUpdate();
}

void TabBar::setTabText(int index, const QString& text)
{
    if (index < 0 || index >= count() || tabs_.at(index).text == text)
        return;

    tabs_[index].text = text;
    invalidateLayout();
    clampScroll();
    snapIndicator();
    requestUpdate();
}

void TabBar::setTabEnabled(int index, bool enabled)
{
    if (index < 0 || index >= count() || tabs_.at(index).enabled == enabled)
        return;

    tabs_[index].enabled = enabled;
    requestUpdate();
}

void TabBar::setTabBadge(int index, const QString& badge)
{
    if (index < 0 || index >= count() || tabs_.at(index).badge == badge)
        return;

    tabs_[index].badge = badge;
    requestUpdate();
}

void TabBar::setMode(Mode mode)
{
    if (mode_ == mode)
        return;

    mode_ = mode;
    if (mode_ == Mode::Fixed)
        scrollOffset_ = 0.0;

    invalidateLayout();
    snapIndicator();
    requestUpdate();
}

TabBar::Mode TabBar::mode() const noexcept
{
    return mode_;
}

void TabBar::setCurrentIndex(int index)
{
    if (index < -1 || index >= count() || index == current_)
        return;

    const QRectF from = indicator_.isEmpty() ? indicatorFor(current_) : indicator_;
    current_ = index;
    ensureCurrentVisible();
    invalidateLayout();

    const QRectF to = indicatorFor(current_);
    if (animateIndicator_ && !from.isEmpty() && !to.isEmpty() && from != to)
        startIndicatorAnimation(from, to);
    else
        snapIndicator();

    requestUpdate();
    emit currentChanged(current_);
}

int TabBar::currentIndex() const noexcept
{
    return current_;
}

void TabBar::setIndicatorAnimationEnabled(bool enabled)
{
    if (animateIndicator_ == enabled)
        return;

    animateIndicator_ = enabled;
    if (!animateIndicator_) {
        indicatorTimer_.stop();
        snapIndicator();
        requestUpdate();
    }
}

bool TabBar::isIndicatorAnimationEnabled() const noexcept
{
    return animateIndicator_;
}

QRectF TabBar::indicatorFor(int index) const
{
    const QList<TabMetrics>& metrics = ensureLayout();
    if (index < 0 || index >= static_cast<int>(metrics.size()))
        return QRectF();

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QRectF rect = metrics.at(index).rect;
    const qreal inset = mode_ == Mode::Scrollable ? density.dp(8.0) : qreal(0.0);
    const qreal height = density.dp(kIndicatorHeightDp);

    return QRectF(rect.left() + inset, rect.bottom() - height,
                  std::max(qreal(0.0), rect.width() - inset * 2.0), height);
}

QRectF TabBar::indicatorRect() const
{
    return indicator_;
}

QRectF TabBar::tabRect(int index) const
{
    const QList<TabMetrics>& metrics = ensureLayout();
    if (index < 0 || index >= static_cast<int>(metrics.size()))
        return QRectF();

    return metrics.at(index).rect;
}

void TabBar::snapIndicator()
{
    indicatorTimer_.stop();
    indicatorProgress_ = 1.0;
    indicator_ = indicatorFor(current_);
}

void TabBar::startIndicatorAnimation(const QRectF& from, const QRectF& to)
{
    const MotionSpec spec = Theme::forWidget(this).motion().medium1();
    if (spec.duration.count() <= 0) {
        indicator_ = to;
        indicatorProgress_ = 1.0;
        indicatorTimer_.stop();
        return;
    }

    indicatorFrom_ = from;
    indicatorTo_ = to;
    indicatorProgress_ = 0.0;
    indicator_ = from;
    indicatorClock_.restart();
    indicatorTimer_.start();
}

void TabBar::tickIndicator()
{
    const MotionSpec spec = Theme::forWidget(this).motion().medium1();
    const qint64 total = std::max<qint64>(1, spec.duration.count());
    const qreal raw = std::clamp(
        static_cast<qreal>(indicatorClock_.elapsed()) / static_cast<qreal>(total), qreal(0.0),
        qreal(1.0));

    const QEasingCurve curve = spec.easing.toEasingCurve();
    const qreal eased = curve.valueForProgress(raw);

    indicator_ = interpolate(indicatorFrom_, indicatorTo_, eased);
    indicatorProgress_ = raw;

    if (raw >= 1.0) {
        indicator_ = indicatorTo_;
        indicatorTimer_.stop();
    }

    requestUpdate();
}

void TabBar::ensureCurrentVisible()
{
    if (mode_ != Mode::Scrollable || current_ < 0)
        return;

    const QRectF viewport = contentRect();
    const QRectF rect = tabRect(current_);
    if (rect.isEmpty())
        return;

    if (rect.left() < viewport.left())
        scrollOffset_ += rect.left() - viewport.left();
    else if (rect.right() > viewport.right())
        scrollOffset_ += rect.right() - viewport.right();

    clampScroll();
    invalidateLayout();
}

void TabBar::clampScroll()
{
    scrollOffset_ = std::clamp(scrollOffset_, qreal(0.0), maximumScrollOffset());
}

void TabBar::setScrollOffset(qreal offset)
{
    const qreal clamped = std::clamp(offset, qreal(0.0), maximumScrollOffset());
    if (qFuzzyCompare(scrollOffset_ + 1.0, clamped + 1.0))
        return;

    scrollOffset_ = clamped;
    invalidateLayout();
    snapIndicator();
    requestUpdate();
}

qreal TabBar::scrollOffset() const noexcept
{
    return scrollOffset_;
}

qreal TabBar::maximumScrollOffset() const
{
    if (mode_ != Mode::Scrollable)
        return 0.0;

    const Theme& theme = Theme::forWidget(this);
    const qreal content = TabLayout::contentWidth(
        tabs_, mode_, static_cast<qreal>(width()), labelFontFor(theme),
        theme.density().dp(kHorizontalPaddingDp), theme.density().dp(kMinScrollableTabWidthDp),
        theme.density().dp(kMaxScrollableTabWidthDp));

    return std::max(qreal(0.0), content - static_cast<qreal>(width()));
}

const QList<TabMetrics>& TabBar::ensureLayout() const
{
    if (!layoutDirty_)
        return metricsCache_;

    const Theme& theme = Theme::forWidget(this);
    metricsCache_ = TabLayout::layout(
        tabs_, contentRect(), mode_, scrollOffset_, labelFontFor(theme),
        theme.density().dp(kHorizontalPaddingDp), theme.density().dp(kMinScrollableTabWidthDp),
        theme.density().dp(kMaxScrollableTabWidthDp));
    layoutDirty_ = false;

    return metricsCache_;
}

int TabBar::indexAtPosition(const QPointF& position) const
{
    const QList<TabMetrics>& metrics = ensureLayout();
    for (int index = 0; index < static_cast<int>(metrics.size()); ++index) {
        if (metrics.at(index).rect.contains(position))
            return index;
    }

    return -1;
}

void TabBar::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    qreal naturalWidth = density.dp(160.0);
    if (count() > 0) {
        if (mode_ == Mode::Fixed) {
            naturalWidth = density.dp(96.0) * static_cast<qreal>(count());
        } else {
            naturalWidth = TabLayout::contentWidth(
                tabs_, mode_, 0.0, labelFontFor(theme), density.dp(kHorizontalPaddingDp),
                density.dp(kMinScrollableTabWidthDp), density.dp(kMaxScrollableTabWidthDp));
        }
    }

    setMeasuredSize(QSizeF(width.resolve(naturalWidth), height.resolve(density.dp(kBarHeightDp))));
}

void TabBar::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const QList<TabMetrics>& metrics = ensureLayout();

    painter.save();
    painter.fillRect(content, colours.surface);
    painter.setClipRect(content);

    const QFont font = labelFontFor(theme);
    const QFontMetricsF fontMetrics(font);
    const QFont badgeFont = badgeFontFor(theme);
    const QFontMetricsF badgeMetrics(badgeFont);
    const qreal iconSize = density.dp(kIconSizeDp);
    const qreal iconGap = density.dp(kIconGapDp);

    for (int index = 0; index < static_cast<int>(metrics.size()); ++index) {
        const TabMetrics& entry = metrics.at(index);
        if (!entry.visible)
            continue;

        const Tab& tab = tabs_.at(index);
        const bool selected = index == current_;
        const bool enabled = tab.enabled;
        const QRectF rect = entry.rect;

        if (index == hovered_ && enabled)
            painter.fillRect(rect, colours.stateLayer(colours.onSurface, ViewState::Hovered));

        QColor contentColour = selected ? colours.primary : colours.onSurfaceVariant;
        if (!enabled)
            contentColour = withAlpha(colours.onSurface, 0.38);

        painter.save();
        painter.setClipRect(rect);

        const bool hasIcon = !tab.icon.isNull();
        const qreal textWidth = tab.text.isEmpty() ? 0.0 : fontMetrics.horizontalAdvance(tab.text);
        qreal groupWidth = textWidth;
        if (hasIcon)
            groupWidth += iconSize + (textWidth > 0.0 ? iconGap : 0.0);

        qreal x = rect.center().x() - groupWidth / 2.0;

        if (hasIcon) {
            const QRectF iconRect(x, rect.center().y() - iconSize / 2.0, iconSize, iconSize);
            iconDrawable_.setIcon(tab.icon);
            iconDrawable_.setSize(QSizeF(iconSize, iconSize));
            iconDrawable_.setColour(contentColour);
            iconDrawable_.draw(painter, iconRect);
            if (textWidth > 0.0)
                x += iconSize + iconGap;
        }

        if (textWidth > 0.0) {
            const qreal available = std::max(rect.right() - density.dp(8.0) - x, qreal(0.0));
            const QString label = fontMetrics.elidedText(tab.text, Qt::ElideRight, available);

            painter.setFont(font);
            painter.setPen(contentColour);
            painter.drawText(QRectF(x, rect.top(), available, rect.height()),
                             static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), label);
        }

        if (!tab.badge.isEmpty()) {
            const qreal badgeHeight = density.dp(16.0);
            const qreal badgeWidth =
                std::max(badgeHeight, badgeMetrics.horizontalAdvance(tab.badge) + density.dp(8.0));
            const QRectF badgeRect(rect.right() - badgeWidth - density.dp(4.0),
                                   rect.top() + density.dp(6.0), badgeWidth, badgeHeight);

            painter.setPen(Qt::NoPen);
            painter.setBrush(colours.error);
            painter.drawRoundedRect(badgeRect, badgeHeight / 2.0, badgeHeight / 2.0);

            painter.setFont(badgeFont);
            painter.setPen(colours.onError);
            painter.drawText(badgeRect, static_cast<int>(Qt::AlignCenter), tab.badge);
        }

        painter.restore();
    }

    if (!indicator_.isEmpty()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colours.primary);
        painter.drawPath(CornerRadii{3.0, 3.0, 0.0, 0.0}.toPath(indicator_));
    }

    painter.setPen(QPen(colours.outlineVariant, density.dp(1.0)));
    painter.drawLine(QPointF(content.left(), content.bottom() - density.dp(0.5)),
                     QPointF(content.right(), content.bottom() - density.dp(0.5)));

    painter.restore();
}

void TabBar::mousePressEvent(QMouseEvent* event)
{
    View::mousePressEvent(event);

    if (event->button() != Qt::LeftButton)
        return;

    const int index = indexAtPosition(event->position());
    if (index < 0 || index >= count() || !tabs_.at(index).enabled)
        return;

    setFocus(Qt::MouseFocusReason);
    setCurrentIndex(index);
    event->accept();
}

void TabBar::mouseMoveEvent(QMouseEvent* event)
{
    View::mouseMoveEvent(event);

    const int index = indexAtPosition(event->position());
    if (index == hovered_)
        return;

    hovered_ = index;
    requestUpdate();
}

void TabBar::leaveEvent(QEvent* event)
{
    View::leaveEvent(event);

    if (hovered_ < 0)
        return;

    hovered_ = -1;
    requestUpdate();
}

void TabBar::keyPressEvent(QKeyEvent* event)
{
    if (count() == 0) {
        View::keyPressEvent(event);
        return;
    }

    int target = current_;
    bool handled = true;

    switch (event->key()) {
    case Qt::Key_Left: {
        target = current_ < 0 ? count() - 1 : current_ - 1;
        while (target >= 0 && !tabs_.at(target).enabled)
            --target;
        if (target < 0)
            target = current_;
        break;
    }
    case Qt::Key_Right: {
        target = current_ < 0 ? 0 : current_ + 1;
        while (target < count() && !tabs_.at(target).enabled)
            ++target;
        if (target >= count())
            target = current_;
        break;
    }
    case Qt::Key_Home:
        target = 0;
        break;
    case Qt::Key_End:
        target = count() - 1;
        break;
    default:
        handled = false;
        break;
    }

    if (!handled) {
        View::keyPressEvent(event);
        return;
    }

    if (target >= 0 && target != current_)
        setCurrentIndex(target);

    event->accept();
}

void TabBar::wheelEvent(QWheelEvent* event)
{
    if (mode_ != Mode::Scrollable) {
        event->ignore();
        return;
    }

    const qreal steps = static_cast<qreal>(event->angleDelta().y()) / 120.0;
    if (qFuzzyIsNull(steps)) {
        event->ignore();
        return;
    }

    setScrollOffset(scrollOffset_
                    - steps * Theme::forWidget(this).density().dp(kScrollStepDp));
    event->accept();
}

void TabBar::resizeEvent(QResizeEvent* event)
{
    View::resizeEvent(event);

    clampScroll();
    invalidateLayout();
    snapIndicator();
    requestUpdate();
}

void TabBar::onThemeChanged()
{
    invalidateLayout();
    snapIndicator();
    requestUpdate();
}

} // namespace md
