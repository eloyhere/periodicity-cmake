// Periodicity — Page, a header / body / footer skeleton.
//
// The page paints the app bar and the back affordance itself; the body View and
// the footer View are real children. The body sits inside an internal viewport
// widget whose rectangle is exactly bodyRect(): Qt clips a child to its parent,
// so the viewport is what makes a scrolled body stop at the app bar and the
// footer instead of painting over them. The viewport reserves nothing of its
// own — it is transparent and has no focus — so it never shows through.
//
// Every dimension is derived from the theme on demand (header 64 dp, the two
// type roles, the action gap), which is why onThemeChanged() simply re-measures
// and re-arranges: there is no cached pixel geometry to invalidate.

#include <md/components/page.h>

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
#include <QRectF>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace md {
namespace {

constexpr qreal kHeaderHeightDp = 64.0;
constexpr qreal kHeaderPaddingDp = 16.0;
constexpr qreal kHeaderGapDp = 8.0;
constexpr qreal kBackButtonDp = 48.0;
constexpr qreal kBackEdgeDp = 4.0;
constexpr qreal kBackStateLayerDp = 40.0;
constexpr qreal kActionMaxWidthDp = 96.0;
constexpr qreal kActionGapDp = 4.0;
constexpr qreal kActionVerticalInsetDp = 8.0;
constexpr qreal kWheelStepDp = 48.0;

QFont fontFor(const TextStyle& style, const Density& density, const QWidget* context)
{
    return Font::resolve(context, style, density);
}

} // namespace

// ---------------------------------------------------------------------------
// PageViewport — the scrolling window the body lives in.
// ---------------------------------------------------------------------------

// An internal child, not part of the public surface: it owns exactly one layout
// rule (place the body at the scroll offset, at least as tall as the viewport)
// and the clip that makes the scroll invisible outside the body.
class PageViewport final : public ViewGroup
{
    Q_OBJECT

public:
    explicit PageViewport(QWidget* parent = nullptr)
        : ViewGroup(parent)
    {
        setFocusPolicy(Qt::NoFocus);
        setBackgroundColour(QColor(Qt::transparent));
    }

    void setScrollMetrics(qreal contentHeight, qreal offset)
    {
        contentHeight_ = std::max(qreal(0.0), contentHeight);
        offset_ = std::max(qreal(0.0), offset);
    }

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override
    {
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    }

    void arrangeChildren(const QRectF& content) override
    {
        const qreal width = std::max(qreal(0.0), content.width());
        // The content is arranged at its own height, not stretched to the
        // viewport: a short page reports contentHeight() and geometry that
        // agree, and the surplus area is painted by Page itself.
        const qreal height = std::max(qreal(0.0), contentHeight_);

        for (View* child : views()) {
            if (child == nullptr || child->isHidden())
                continue;

            child->measure(MeasureSpec::exactly(width), MeasureSpec::exactly(height));
            child->arrange(QRect(qRound(content.left()), qRound(content.top() - offset_),
                                 qRound(width), qRound(height)));
        }
    }

    void paintChildren(QPainter& painter) override
    {
        painter.save();
        painter.setClipRect(QRectF(rect()));
        ViewGroup::paintChildren(painter);
        painter.restore();
    }

private:
    qreal contentHeight_ = 0.0;
    qreal offset_ = 0.0;
};

// ---------------------------------------------------------------------------
// Page
// ---------------------------------------------------------------------------

Page::Page(QWidget* parent)
    : ViewGroup(parent)
{
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(true);
    setShape(Shape::none());
    setBackgroundColour(Theme::forWidget(this).colours().surface);

    viewport_ = new PageViewport(this);
    addView(viewport_);
}

Page::~Page()
{
    // The page reparents its children but does not own them: detaching here
    // keeps a stack-owned content or footer alive when the page goes first.
    if (content_ != nullptr)
        viewport_->removeView(content_);

    if (footer_ != nullptr)
        removeView(footer_);

    for (View* action : actions_) {
        if (action != nullptr)
            removeView(action);
    }

    actions_.clear();
}

void Page::setTitle(const QString& title)
{
    if (title_ == title)
        return;

    title_ = title;
    relayout();
}

QString Page::title() const
{
    return title_;
}

void Page::setSubtitle(const QString& subtitle)
{
    if (subtitle_ == subtitle)
        return;

    subtitle_ = subtitle;
    relayout();
}

QString Page::subtitle() const
{
    return subtitle_;
}

void Page::setHeaderVisible(bool visible)
{
    if (headerVisible_ == visible)
        return;

    headerVisible_ = visible;
    relayout();
}

bool Page::isHeaderVisible() const noexcept
{
    return headerVisible_;
}

void Page::setBackVisible(bool visible)
{
    if (backVisible_ == visible)
        return;

    backVisible_ = visible;
    requestUpdate();
}

bool Page::isBackVisible() const noexcept
{
    return backVisible_;
}

void Page::addHeaderAction(View* action)
{
    if (action == nullptr || action == this || action == content_ || action == footer_
        || actions_.contains(action)) {
        return;
    }

    addView(action);
    actions_.append(action);

    connect(action, &QObject::destroyed, this, [this, action] {
        actions_.removeAll(action);
        relayout();
    });

    relayout();
}

void Page::clearHeaderActions()
{
    if (actions_.isEmpty())
        return;

    const QList<View*> removed = actions_;
    actions_.clear();

    for (View* action : removed) {
        if (action != nullptr) {
            QObject::disconnect(action, nullptr, this, nullptr);
            removeView(action);
        }
    }

    relayout();
}

QList<View*> Page::headerActions() const
{
    return actions_;
}

void Page::setContent(View* content)
{
    if (content_ == content)
        return;

    if (content_ != nullptr) {
        viewport_->removeView(content_);
        content_->hide();
    }

    content_ = content;

    if (content_ != nullptr) {
        viewport_->addView(content_);
        content_->show();

        connect(content_, &QObject::destroyed, this, [this, content] {
            if (content_ == content) {
                content_ = nullptr;
                clampScroll();
                relayout();
            }
        });
    }

    clampScroll();
    relayout();
}

View* Page::content() const
{
    return content_;
}

void Page::setFooter(View* footer)
{
    if (footer_ == footer)
        return;

    if (footer_ != nullptr) {
        QObject::disconnect(footer_, nullptr, this, nullptr);
        removeView(footer_);
        footer_->hide();
    }

    footer_ = footer;

    if (footer_ != nullptr) {
        addView(footer_);
        footer_->show();

        connect(footer_, &QObject::destroyed, this, [this, footer] {
            if (footer_ == footer) {
                footer_ = nullptr;
                relayout();
            }
        });
    }

    clampScroll();
    relayout();
}

View* Page::footer() const
{
    return footer_;
}

void Page::setScrollable(bool scrollable)
{
    if (scrollable_ == scrollable)
        return;

    scrollable_ = scrollable;
    clampScroll();
    syncLayout();
}

bool Page::isScrollable() const noexcept
{
    return scrollable_;
}

void Page::setScrollOffset(qreal offset)
{
    const qreal clamped = std::clamp(offset, qreal(0.0), maxScrollOffset());
    if (qFuzzyCompare(clamped + 1.0, scrollOffset_ + 1.0))
        return;

    scrollOffset_ = clamped;
    syncLayout();
}

qreal Page::scrollOffset() const noexcept
{
    return scrollOffset_;
}

qreal Page::contentHeight() const
{
    if (content_ == nullptr)
        return 0.0;

    const QRectF body = bodyRect();
    const qreal width = body.width() > 0.0 ? body.width()
                                           : std::max(qreal(0.0), contentRect().width());

    content_->measure(MeasureSpec::atMost(width), MeasureSpec::unspecified());
    return content_->measuredSize().height();
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

qreal Page::headerHeight() const
{
    if (!headerVisible_)
        return 0.0;

    return Theme::forWidget(this).density().dp(kHeaderHeightDp);
}

QRectF Page::headerRect() const
{
    const QRectF area = contentRect();
    const qreal height = headerHeight();
    if (area.isEmpty() || height <= 0.0)
        return QRectF();

    return QRectF(area.left(), area.top(), area.width(), std::min(height, area.height()));
}

qreal Page::footerHeightForWidth(qreal width) const
{
    if (footer_ == nullptr || footer_->isHidden())
        return 0.0;

    footer_->measure(MeasureSpec::exactly(std::max(qreal(0.0), width)),
                     MeasureSpec::unspecified());
    return footer_->measuredSize().height();
}

QRectF Page::footerRect() const
{
    const QRectF area = contentRect();
    if (area.isEmpty() || footer_ == nullptr || footer_->isHidden())
        return QRectF();

    const qreal height = std::min(footerHeightForWidth(area.width()), area.height());
    if (height <= 0.0)
        return QRectF();

    return QRectF(area.left(), area.bottom() - height, area.width(), height);
}

QRectF Page::bodyRect() const
{
    const QRectF area = contentRect();
    if (area.isEmpty())
        return QRectF();

    const qreal top = area.top() + headerHeight();
    const QRectF footer = footerRect();
    const qreal bottom = footer.isEmpty() ? area.bottom() : footer.top();

    return QRectF(area.left(), top, area.width(), std::max(qreal(0.0), bottom - top));
}

QRectF Page::backButtonRect() const
{
    const QRectF header = headerRect();
    if (header.isEmpty() || !backVisible_)
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const qreal size = std::min(density.dp(kBackButtonDp), header.height());
    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;
    const qreal edge = density.dp(kBackEdgeDp);
    const qreal x = rightToLeft ? header.right() - edge - size : header.left() + edge;

    return QRectF(x, header.center().y() - size / 2.0, size, size);
}

qreal Page::headerActionsWidth() const
{
    if (actions_.isEmpty())
        return 0.0;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal maxWidth = density.dp(kActionMaxWidthDp);
    const qreal maxHeight = std::max(qreal(0.0), headerHeight() - density.dp(kActionVerticalInsetDp));

    qreal total = 0.0;
    int count = 0;

    for (View* action : actions_) {
        if (action == nullptr || action->isHidden())
            continue;

        action->measure(MeasureSpec::atMost(maxWidth), MeasureSpec::atMost(maxHeight));
        total += action->measuredSize().width();
        ++count;
    }

    if (count > 1)
        total += density.dp(kActionGapDp) * static_cast<qreal>(count - 1);

    return total;
}

QRectF Page::titleRect() const
{
    const QRectF header = headerRect();
    if (header.isEmpty())
        return QRectF();

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    const QRectF back = backButtonRect();
    const qreal left = back.isEmpty() ? header.left() + density.dp(kHeaderPaddingDp)
                                      : back.right() + density.dp(kHeaderGapDp);

    const qreal actions = headerActionsWidth();
    const qreal right = header.right() - density.dp(kHeaderPaddingDp)
                        - (actions > 0.0 ? actions + density.dp(kHeaderGapDp) : 0.0);

    const qreal titleHeight = density.sp(theme.type().titleLarge.lineHeightSp);
    const qreal subtitleHeight =
        subtitle_.isEmpty() ? 0.0 : density.sp(theme.type().bodySmall.lineHeightSp);
    const qreal top = header.center().y() - (titleHeight + subtitleHeight) / 2.0;

    return QRectF(left, top, std::max(qreal(0.0), right - left), titleHeight);
}

QRectF Page::subtitleRect() const
{
    const QRectF title = titleRect();
    if (title.isEmpty() || subtitle_.isEmpty())
        return QRectF();

    const Theme& theme = Theme::forWidget(this);
    const qreal height = theme.density().sp(theme.type().bodySmall.lineHeightSp);

    return QRectF(title.left(), title.bottom(), title.width(), height);
}

qreal Page::maxScrollOffset() const
{
    if (!scrollable_)
        return 0.0;

    const QRectF body = bodyRect();
    if (body.height() <= 0.0)
        return 0.0;

    return std::max(qreal(0.0), contentHeight() - body.height());
}

void Page::clampScroll()
{
    scrollOffset_ = std::clamp(scrollOffset_, qreal(0.0), maxScrollOffset());
}

void Page::relayout()
{
    updateGeometry();
    clampScroll();
    syncLayout();
}

void Page::syncLayout()
{
    arrangeChildren(contentRect());
    requestUpdate();
}

// ---------------------------------------------------------------------------
// Measure / arrange / paint
// ---------------------------------------------------------------------------

void Page::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    const qreal resolvedWidth = width.resolve(density.dp(360.0));
    const qreal footerHeight = footerHeightForWidth(resolvedWidth);
    const qreal bodyHeight = density.dp(240.0);

    setMeasuredSize(QSizeF(resolvedWidth,
                           height.resolve(headerHeight() + bodyHeight + footerHeight)));
}

void Page::arrangeChildren(const QRectF& content)
{
    Q_UNUSED(content);

    if (viewport_ == nullptr)
        return;

    const QRectF body = bodyRect();
    viewport_->setScrollMetrics(contentHeight(), scrollOffset_);
    viewport_->arrange(body.toAlignedRect());

    if (footer_ != nullptr && !footer_->isHidden()) {
        const QRectF footer = footerRect();
        if (!footer.isEmpty()) {
            footer_->measure(MeasureSpec::exactly(footer.width()),
                             MeasureSpec::exactly(footer.height()));
            footer_->arrange(footer.toAlignedRect());
        }
    }

    if (actions_.isEmpty())
        return;

    const QRectF header = headerRect();
    if (header.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal maxWidth = density.dp(kActionMaxWidthDp);
    const qreal maxHeight = std::max(qreal(0.0), header.height() - density.dp(kActionVerticalInsetDp));
    const qreal gap = density.dp(kActionGapDp);
    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;

    qreal cursor = rightToLeft ? header.left() + density.dp(kHeaderPaddingDp)
                               : header.right() - density.dp(kHeaderPaddingDp);

    for (View* action : actions_) {
        if (action == nullptr || action->isHidden())
            continue;

        action->measure(MeasureSpec::atMost(maxWidth), MeasureSpec::atMost(maxHeight));

        QSizeF size = action->measuredSize();
        size.setWidth(std::min(std::max(qreal(0.0), size.width()), maxWidth));
        size.setHeight(std::min(std::max(qreal(0.0), size.height()), maxHeight));
        if (size.isEmpty())
            continue;

        const qreal x = rightToLeft ? cursor : cursor - size.width();
        const qreal y = header.center().y() - size.height() / 2.0;

        action->measure(MeasureSpec::exactly(size.width()), MeasureSpec::exactly(size.height()));
        action->arrange(QRect(qRound(x), qRound(y), qRound(size.width()), qRound(size.height())));

        cursor += rightToLeft ? size.width() + gap : -(size.width() + gap);
    }
}

void Page::paintContent(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF header = headerRect();
    if (!header.isEmpty()) {
        const QRectF button = backButtonRect();

        if (!button.isEmpty()) {
            if (backHovered_ || backPressed_) {
                const qreal diameter = std::min(density.dp(kBackStateLayerDp),
                                                std::min(button.width(), button.height()));
                const QRectF circle(button.center().x() - diameter / 2.0,
                                    button.center().y() - diameter / 2.0, diameter, diameter);
                painter.setPen(Qt::NoPen);
                painter.setBrush(colours.stateLayer(
                    colours.onSurface,
                    backPressed_ ? ViewState::Pressed : ViewState::Hovered));
                painter.drawEllipse(circle);
            }

            drawBackArrow(painter, button);
        }

        const QRectF title = titleRect();
        if (!title_.isEmpty() && !title.isEmpty() && title.width() > 0.0) {
            const QFont titleFont = fontFor(theme.type().titleLarge, density, this);
            const QString elided =
                QFontMetricsF(titleFont).elidedText(title_, Qt::ElideRight, title.width());

            painter.setFont(titleFont);
            painter.setPen(colours.onSurface);
            painter.drawText(title, static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                             elided);
        }

        const QRectF subtitle = subtitleRect();
        if (!subtitle.isEmpty() && subtitle.width() > 0.0) {
            const QFont subtitleFont = fontFor(theme.type().bodySmall, density, this);
            const QString elided =
                QFontMetricsF(subtitleFont).elidedText(subtitle_, Qt::ElideRight,
                                                       subtitle.width());

            painter.setFont(subtitleFont);
            painter.setPen(colours.onSurfaceVariant);
            painter.drawText(subtitle, static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                             elided);
        }

        painter.setPen(QPen(colours.outlineVariant, density.dp(1.0)));
        painter.drawLine(QPointF(header.left(), header.bottom() - density.dp(0.5)),
                         QPointF(header.right(), header.bottom() - density.dp(0.5)));
    }

    painter.restore();
}

void Page::drawBackArrow(QPainter& painter, const QRectF& button) const
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    const qreal direction = layoutDirection() == Qt::RightToLeft ? -1.0 : 1.0;
    const qreal reach = button.width() * 0.22;
    const qreal head = density.dp(7.0);

    const QPointF tip(button.center().x() - direction * reach, button.center().y());
    const QPointF tail(button.center().x() + direction * reach, button.center().y());

    QPen pen(colours.onSurface, density.dp(2.0));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    QPainterPath arrow;
    arrow.moveTo(tail);
    arrow.lineTo(tip);
    arrow.moveTo(tip + QPointF(direction * head, -head));
    arrow.lineTo(tip);
    arrow.lineTo(tip + QPointF(direction * head, head));
    painter.drawPath(arrow);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void Page::mousePressEvent(QMouseEvent* event)
{
    ViewGroup::mousePressEvent(event);

    if (event->button() != Qt::LeftButton)
        return;

    const QRectF button = backButtonRect();
    backPressed_ = !button.isEmpty() && button.contains(event->position());

    if (backPressed_) {
        setFocus(Qt::MouseFocusReason);
        requestUpdate();
    }
}

void Page::mouseReleaseEvent(QMouseEvent* event)
{
    ViewGroup::mouseReleaseEvent(event);

    const bool pressed = backPressed_;
    backPressed_ = false;

    if (event->button() != Qt::LeftButton) {
        requestUpdate();
        return;
    }

    const QRectF button = backButtonRect();
    const bool inside = !button.isEmpty() && button.contains(event->position());

    requestUpdate();

    if (pressed && inside)
        emit backRequested();
}

void Page::mouseMoveEvent(QMouseEvent* event)
{
    ViewGroup::mouseMoveEvent(event);

    const QRectF button = backButtonRect();
    const bool hovered = !button.isEmpty() && button.contains(event->position());
    if (hovered == backHovered_)
        return;

    backHovered_ = hovered;
    requestUpdate();
}

void Page::leaveEvent(QEvent* event)
{
    ViewGroup::leaveEvent(event);

    if (!backHovered_)
        return;

    backHovered_ = false;
    requestUpdate();
}

void Page::keyPressEvent(QKeyEvent* event)
{
    if (!isEnabled()) {
        ViewGroup::keyPressEvent(event);
        return;
    }

    const qreal maximum = maxScrollOffset();
    if (maximum <= 0.0) {
        ViewGroup::keyPressEvent(event);
        return;
    }

    const QRectF body = bodyRect();
    const qreal page = std::max(qreal(1.0), body.height() * 0.9);

    switch (event->key()) {
    case Qt::Key_PageDown:
        setScrollOffset(scrollOffset_ + page);
        break;
    case Qt::Key_PageUp:
        setScrollOffset(scrollOffset_ - page);
        break;
    case Qt::Key_Home:
        setScrollOffset(0.0);
        break;
    case Qt::Key_End:
        setScrollOffset(maximum);
        break;
    default:
        ViewGroup::keyPressEvent(event);
        return;
    }

    event->accept();
}

void Page::wheelEvent(QWheelEvent* event)
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

void Page::onThemeChanged()
{
    setBackgroundColour(Theme::forWidget(this).colours().surface);
    updateGeometry();
    clampScroll();
    syncLayout();
}

bool Page::focusable() const
{
    // A page scrolls with the keyboard once it has focus, but a focus ring
    // around the whole screen would be noise.
    return false;
}

} // namespace md

#include "page.moc"
