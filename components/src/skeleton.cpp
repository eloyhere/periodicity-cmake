// Periodicity — Skeleton (media/content batch).
//
// The timer is the only moving part. startTimer() is called from setAnimating()
// when the widget is already visible and from onShown(); stopTimer() from
// setAnimating(false) and onHidden(), so a skeleton in a background page never
// schedules frames. Each timeout advances phase_ by one frame's worth and posts
// a coalesced repaint, which means the paint path itself stays pure and the
// widget can be rendered deterministically when the timer is stopped.
//
// A circle is always a square box centred in the content rectangle; the Rect
// and Text forms fill their measured box and use the corner radius token.

#include <md/components/skeleton.h>

#include <md/theme/theme.h>

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QSizeF>

#include <algorithm>

namespace md {

namespace {

constexpr int kFrameIntervalMs = 16;
constexpr qreal kPhaseStep = 0.02;

qreal clampedRadius(qreal radius, const QRectF& box)
{
    const qreal limit = std::min(box.width(), box.height()) / 2.0;
    return std::clamp(radius, qreal(0.0), std::max(qreal(0.0), limit));
}

} // namespace

Skeleton::Skeleton(QWidget* parent)
    : View(parent)
{
    setBackgroundColour(QColor(Qt::transparent));
    setFocusPolicy(Qt::NoFocus);

    timer_.setInterval(kFrameIntervalMs);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, [this] { advanceFrame(); });
}

Skeleton::Skeleton(SkeletonShape shape, QWidget* parent)
    : Skeleton(parent)
{
    setSkeletonShape(shape);
}

void Skeleton::setSkeletonShape(SkeletonShape shape)
{
    if (shape_ == shape)
        return;

    shape_ = shape;

    // The shape supplies defaults only for the dimensions the caller has not
    // pinned explicitly, so setSkeletonShape(Circle) is immediately round.
    if (shape_ == SkeletonShape::Circle) {
        if (!widthSet_)
            widthDp_ = 48.0;
        if (!heightSet_)
            heightDp_ = 48.0;
        if (!radiusSet_)
            radiusDp_ = 24.0;
    } else {
        if (!widthSet_)
            widthDp_ = 160.0;
        if (!heightSet_)
            heightDp_ = 16.0;
        if (!radiusSet_)
            radiusDp_ = 8.0;
    }

    updateGeometry();
    requestUpdate();
}

Skeleton::SkeletonShape Skeleton::skeletonShape() const noexcept
{
    return shape_;
}

void Skeleton::setSkeletonWidth(qreal dp)
{
    widthSet_ = true;
    const qreal clamped = std::max(qreal(0.0), dp);
    if (qFuzzyCompare(widthDp_ + 1.0, clamped + 1.0))
        return;

    widthDp_ = clamped;
    updateGeometry();
    requestUpdate();
}

qreal Skeleton::skeletonWidth() const noexcept
{
    return widthDp_;
}

void Skeleton::setSkeletonHeight(qreal dp)
{
    heightSet_ = true;
    const qreal clamped = std::max(qreal(0.0), dp);
    if (qFuzzyCompare(heightDp_ + 1.0, clamped + 1.0))
        return;

    heightDp_ = clamped;
    updateGeometry();
    requestUpdate();
}

qreal Skeleton::skeletonHeight() const noexcept
{
    return heightDp_;
}

void Skeleton::setCornerRadius(qreal dp)
{
    radiusSet_ = true;
    const qreal clamped = std::max(qreal(0.0), dp);
    if (qFuzzyCompare(radiusDp_ + 1.0, clamped + 1.0))
        return;

    radiusDp_ = clamped;
    requestUpdate();
}

qreal Skeleton::cornerRadius() const noexcept
{
    return radiusDp_;
}

void Skeleton::setAnimating(bool animating)
{
    if (animating_ == animating)
        return;

    animating_ = animating;

    if (animating_ && shimmerAllowed())
        startTimer();
    else
        stopTimer();

    requestUpdate();
}

bool Skeleton::isAnimating() const noexcept
{
    return animating_ && timer_.isActive();
}

void Skeleton::setBaseColour(const QColor& colour)
{
    if (baseColour_ == colour)
        return;

    baseColour_ = colour;
    requestUpdate();
}

QColor Skeleton::baseColour() const
{
    if (baseColour_.isValid())
        return baseColour_;

    return Theme::forWidget(this).colours().surfaceContainerHighest;
}

void Skeleton::setHighlightColour(const QColor& colour)
{
    if (highlightColour_ == colour)
        return;

    highlightColour_ = colour;
    requestUpdate();
}

QColor Skeleton::highlightColour() const
{
    if (highlightColour_.isValid())
        return highlightColour_;

    return Theme::forWidget(this).colours().surfaceContainerLow;
}

void Skeleton::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    const qreal requestedWidth = density.dp(widthDp_);
    const qreal requestedHeight = density.dp(heightDp_);

    if (shape_ == SkeletonShape::Circle) {
        const qreal side = std::min(requestedWidth, requestedHeight);
        setMeasuredSize(QSizeF(width.resolve(side), height.resolve(side)));
        return;
    }

    setMeasuredSize(
        QSizeF(width.resolve(requestedWidth), height.resolve(requestedHeight)));
}

void Skeleton::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Density& density = Theme::forWidget(this).density();

    QRectF box = content;
    QPainterPath path;

    if (shape_ == SkeletonShape::Circle) {
        const qreal side = std::min(content.width(), content.height());
        box = QRectF(content.center().x() - side / 2.0,
                     content.center().y() - side / 2.0, side, side);
        path.addEllipse(box);
    } else {
        path = Shape::rounded(CornerRadii::uniform(
                                  clampedRadius(density.dp(radiusDp_), box)))
                   .path(box);
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.setPen(Qt::NoPen);
    painter.setBrush(baseColour());
    painter.drawPath(path);

    if (animating_ && timer_.isActive() && box.width() > 0.0) {
        QColor highlight = highlightColour();
        highlight.setAlpha(190);

        const qreal peak = 0.001 + std::clamp(phase_, 0.0, 1.0) * 0.998;
        QLinearGradient gradient(box.left(), box.top(), box.right(), box.top());
        gradient.setColorAt(0.0, QColor(highlight.red(), highlight.green(), highlight.blue(), 0));
        gradient.setColorAt(peak, highlight);
        gradient.setColorAt(1.0, QColor(highlight.red(), highlight.green(), highlight.blue(), 0));

        painter.setBrush(gradient);
        painter.drawPath(path);
    }

    painter.restore();
}

void Skeleton::onShown()
{
    if (animating_ && shimmerAllowed())
        startTimer();
}

void Skeleton::onHidden()
{
    stopTimer();
}

void Skeleton::onThemeChanged()
{
    // Reduced motion is part of the theme snapshot: switching it on has to
    // stop a shimmer that is already running, switching it off to resume it.
    if (animating_ && shimmerAllowed())
        startTimer();
    else
        stopTimer();

    requestUpdate();
}

void Skeleton::onEnabledChanged(bool enabled)
{
    // A disabled skeleton is a flat placeholder: there is nothing to animate,
    // so it must not keep a 16 ms timer alive (which is what this component's
    // contract promises).
    if (enabled && animating_ && shimmerAllowed())
        startTimer();
    else
        stopTimer();

    requestUpdate();
}

bool Skeleton::shimmerAllowed() const
{
    // The request (animating_) is separate from the conditions under which the
    // shimmer may actually run. A hidden or disabled view, and a theme that
    // asks for reduced motion, all paint the flat fill without a timer.
    if (!animating_ || !isVisible() || !isEnabled())
        return false;

    return !Theme::forWidget(this).motion().reducedMotion;
}

bool Skeleton::focusable() const
{
    return false;
}

void Skeleton::startTimer()
{
    if (timer_.isActive() || !shimmerAllowed())
        return;

    timer_.start();
}

void Skeleton::stopTimer()
{
    if (!timer_.isActive())
        return;

    timer_.stop();
    requestUpdate();
}

void Skeleton::advanceFrame()
{
    phase_ += kPhaseStep;
    if (phase_ >= 1.0)
        phase_ -= 1.0;

    requestUpdate();
}

} // namespace md
