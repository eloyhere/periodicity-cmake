// Periodicity — Marquee.
//
// The timer owns the motion; the widget only paints the current offset. That
// split is what makes pause, hover and stop cheap: they change whether the
// timer runs, not the text position.

#include <md/components/marquee.h>

#include <md/theme/theme.h>

#include <QEnterEvent>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QRectF>
#include <QResizeEvent>
#include <QSizeF>

#include <algorithm>
#include <cmath>

namespace md {

namespace {

constexpr int kTimerIntervalMs = 16;

QFont buildFont(const TextStyle& style, const Density& density)
{
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(density.sp(style.sizeSp)))));
    font.setWeight(static_cast<QFont::Weight>(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, density.sp(style.trackingSp));
    return font;
}

} // namespace

Marquee::Marquee(QWidget* parent)
    : View(parent)
{
    setBackgroundColour(QColor(Qt::transparent));
    setFocusPolicy(Qt::NoFocus);

    timer_.setInterval(kTimerIntervalMs);
    connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
}

Marquee::Marquee(const QString& text, QWidget* parent)
    : Marquee(parent)
{
    setText(text);
}

void Marquee::setText(const QString& text)
{
    if (text_ == text)
        return;

    text_ = text;
    offset_ = 0.0;
    // The preferred width is the text width, so a text change invalidates the
    // measured size, not just the paint.
    updateGeometry();
    updateTimer();
    requestUpdate();
}

QString Marquee::text() const
{
    return text_;
}

void Marquee::setTextStyle(const TextStyle& style)
{
    explicitStyle_ = style;
    customStyle_ = true;
    updateGeometry();
    updateTimer();
    requestUpdate();
}

TextStyle Marquee::textStyle() const
{
    return resolvedStyle();
}

TextStyle Marquee::resolvedStyle() const
{
    if (customStyle_)
        return explicitStyle_;

    return Theme::forWidget(this).type().labelLarge;
}

void Marquee::setColour(const QColor& colour)
{
    if (colour_ == colour)
        return;

    colour_ = colour;
    requestUpdate();
}

QColor Marquee::colour() const
{
    return colour_;
}

void Marquee::setAlignment(Qt::Alignment alignment)
{
    if (alignment_ == alignment)
        return;

    alignment_ = alignment;
    requestUpdate();
}

Qt::Alignment Marquee::alignment() const noexcept
{
    return alignment_;
}

void Marquee::setSpeed(qreal dpPerSecond)
{
    const qreal clamped = std::max(qreal(0.0), dpPerSecond);
    if (qFuzzyCompare(speedDp_, clamped))
        return;

    speedDp_ = clamped;
    updateTimer();
    requestUpdate();
}

qreal Marquee::speed() const noexcept
{
    return speedDp_;
}

void Marquee::setDirection(Direction direction)
{
    if (direction_ == direction)
        return;

    direction_ = direction;
    requestUpdate();
}

Marquee::Direction Marquee::direction() const noexcept
{
    return direction_;
}

void Marquee::setGap(qreal dp)
{
    const qreal clamped = std::max(qreal(0.0), dp);
    if (qFuzzyCompare(gapDp_, clamped))
        return;

    gapDp_ = clamped;
    requestUpdate();
}

qreal Marquee::gap() const noexcept
{
    return gapDp_;
}

void Marquee::setPauseOnHover(bool pause)
{
    if (pauseOnHover_ == pause)
        return;

    pauseOnHover_ = pause;
    updateTimer();
    requestUpdate();
}

bool Marquee::pauseOnHover() const noexcept
{
    return pauseOnHover_;
}

void Marquee::setPaused(bool paused)
{
    if (paused_ == paused)
        return;

    paused_ = paused;
    updateTimer();
    requestUpdate();
}

bool Marquee::isPaused() const noexcept
{
    return paused_;
}

void Marquee::start()
{
    if (!running_)
        offset_ = 0.0;

    running_ = true;
    updateTimer();
    requestUpdate();
}

void Marquee::stop()
{
    running_ = false;
    offset_ = 0.0;
    updateTimer();
    requestUpdate();
}

bool Marquee::isRunning() const
{
    if (!running_ || paused_ || (pauseOnHover_ && hovering_) || !isVisible())
        return false;

    // A zero speed is "hold the text still": keeping the frame timer alive for
    // it would burn 60 wake-ups a second and never move a pixel.
    if (speedDp_ <= 0.0)
        return false;

    // Reduced motion means no scrolling at all; the text simply stays at the
    // leading edge (and is still painted, just not moved).
    if (Theme::forWidget(this).motion().reducedMotion)
        return false;

    return isOverflowing();
}

bool Marquee::isOverflowing() const
{
    const qreal available = contentRect().width();
    return available > 0.0 && contentWidth() > available;
}

qreal Marquee::scrollOffset() const noexcept
{
    return offset_;
}

qreal Marquee::contentWidth() const
{
    if (text_.isEmpty())
        return 0.0;

    return QFontMetricsF(textFont()).horizontalAdvance(text_);
}

QFont Marquee::textFont() const
{
    return buildFont(resolvedStyle(), Theme::forWidget(this).density());
}

void Marquee::updateTimer()
{
    const bool shouldRun = isRunning();

    if (shouldRun && !timer_.isActive()) {
        clock_.restart();
        timer_.start();
    } else if (!shouldRun && timer_.isActive()) {
        timer_.stop();
    }
}

void Marquee::tick()
{
    // A tick can still arrive in the same turn as a pause or a hide; the
    // conditions that started the timer are re-checked before moving.
    if (!isRunning()) {
        timer_.stop();
        return;
    }

    const qint64 elapsed = clock_.restart();
    const qreal seconds = static_cast<qreal>(elapsed) / 1000.0;

    const Density& density = Theme::forWidget(this).density();
    offset_ += density.dp(speedDp_) * seconds;

    const qreal travel = contentWidth() + density.dp(gapDp_);
    if (travel > 0.0) {
        offset_ = std::fmod(offset_, travel);
        if (offset_ < 0.0)
            offset_ += travel;
    }

    requestUpdate();
}

void Marquee::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal lineHeight = density.sp(resolvedStyle().lineHeightSp);

    setMeasuredSize(QSizeF(width.resolve(contentWidth()), height.resolve(lineHeight)));
}

void Marquee::paintContent(QPainter& painter, const QRectF& content)
{
    if (text_.isEmpty() || content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QColor ink = colour_.isValid() ? colour_ : theme.colours().onSurface;

    painter.save();
    painter.setClipRect(content);
    painter.setFont(textFont());
    painter.setPen(ink);

    if (!isOverflowing()) {
        painter.drawText(content, static_cast<int>(alignment_), text_);
        painter.restore();
        return;
    }

    const qreal textWidth = contentWidth();
    const qreal travel = textWidth + density.dp(gapDp_);

    qreal x = content.left();
    if (direction_ == Direction::Left)
        x -= offset_;
    else
        x += offset_ - travel;

    const QRectF lineRect(x, content.top(), textWidth + 2.0, content.height());
    const int flags = static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);

    painter.drawText(lineRect, flags, text_);
    painter.drawText(lineRect.translated(travel, 0.0), flags, text_);

    painter.restore();
}

void Marquee::enterEvent(QEnterEvent* event)
{
    View::enterEvent(event);

    hovering_ = true;
    updateTimer();
    requestUpdate();
}

void Marquee::leaveEvent(QEvent* event)
{
    View::leaveEvent(event);

    hovering_ = false;
    updateTimer();
    requestUpdate();
}

void Marquee::resizeEvent(QResizeEvent* event)
{
    // The overflow decision depends on the viewport width. When a resize flips
    // it, the old scroll position is meaningless: start again at the leading
    // edge instead of resuming mid-cycle the next time the text overflows.
    const qreal oldWidth = event->oldSize().width();
    const bool wasOverflowing = oldWidth > 0.0 && contentWidth() > oldWidth;

    View::resizeEvent(event);

    if (wasOverflowing != isOverflowing())
        offset_ = 0.0;

    updateTimer();
    requestUpdate();
}

void Marquee::onShown()
{
    updateTimer();
}

void Marquee::onHidden()
{
    timer_.stop();
}

void Marquee::onThemeChanged()
{
    // The resolved font comes from the theme, so its metrics (and therefore
    // both the preferred width and the overflow decision) changed.
    updateGeometry();
    updateTimer();
    requestUpdate();
}

bool Marquee::focusable() const
{
    return false;
}

} // namespace md
