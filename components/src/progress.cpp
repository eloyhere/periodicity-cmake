// Periodicity — the Progress component (framework.md §5.9).
//
// Two geometries, one state machine. The linear form rounds its track and
// indicator by half the track height so the caps are semicircles; the circular
// form strokes an arc in sixteenths of a degree starting at twelve o'clock.
// Both read their colours from the theme on every paint, and the indeterminate
// phase comes from an Animator that is restarted from finished() and stopped
// whenever the widget hides.

#include <md/components/progress.h>

#include <md/theme/theme.h>

#include <QHideEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>
#include <QShowEvent>

#include <algorithm>
#include <cmath>

namespace md {

namespace {

constexpr qreal kLinearHeightDp = 4.0;
constexpr qreal kCircularSizeDp = 48.0;
constexpr qreal kLinearWidthDp = 200.0;
constexpr qreal kCircularStrokeDp = 4.0;
constexpr qreal kPi = 3.14159265358979323846;

} // namespace

Progress::Progress(QWidget* parent)
    : View(parent)
{
    setBackgroundColour(QColor(Qt::transparent));
    setFocusPolicy(Qt::NoFocus);

    connect(&animator_, &animation::Animator::finished, this, [this] {
        if (indeterminate_ && isVisible())
            startAnimation();
    });
}

Progress::Progress(ProgressType type, QWidget* parent)
    : Progress(parent)
{
    setType(type);
}

void Progress::setType(ProgressType type)
{
    if (type_ == type)
        return;

    type_ = type;
    requestUpdate();
}

Progress::ProgressType Progress::type() const noexcept
{
    return type_;
}

void Progress::setValue(qreal value)
{
    const qreal next = std::clamp(value, 0.0, 1.0);
    if (value_ == next)
        return;

    value_ = next;
    requestUpdate();
}

qreal Progress::value() const noexcept
{
    return value_;
}

void Progress::setIndeterminate(bool indeterminate)
{
    if (indeterminate_ == indeterminate)
        return;

    indeterminate_ = indeterminate;
    phase_ = 0.0;

    if (indeterminate_) {
        if (isVisible())
            startAnimation();
    } else {
        stopAnimation();
    }

    requestUpdate();
}

bool Progress::isIndeterminate() const noexcept
{
    return indeterminate_;
}

void Progress::setBuffer(qreal buffer)
{
    const qreal next = std::clamp(buffer, 0.0, 1.0);
    if (buffer_ == next)
        return;

    buffer_ = next;
    requestUpdate();
}

qreal Progress::buffer() const noexcept
{
    return buffer_;
}

bool Progress::isAnimating() const noexcept
{
    return animator_.isRunning();
}

void Progress::startAnimation()
{
    if (!indeterminate_)
        return;

    const MotionSpec spec = Theme::forWidget(this).motion().level(MotionLevel::Level3);
    if (spec.duration.count() <= 0) {
        // Reduced motion: a static trimmed phase instead of a busy loop.
        phase_ = 0.25;
        requestUpdate();
        return;
    }

    // stop() clears the running flag, so the restart starts from a phase of
    // zero rather than resuming at the finished value of 1.0.
    animator_.stop();
    animator_.start(spec, [this](qreal value) {
        phase_ = value;
        requestUpdate();
    });
}

void Progress::stopAnimation()
{
    animator_.stop();
}

void Progress::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    if (indeterminate_)
        startAnimation();
}

void Progress::hideEvent(QHideEvent* event)
{
    stopAnimation();

    QWidget::hideEvent(event);
}

void Progress::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    const bool circular = type_ == ProgressType::Circular;
    const qreal desiredWidth = circular ? density.dp(kCircularSizeDp) : density.dp(kLinearWidthDp);
    const qreal desiredHeight = circular ? density.dp(kCircularSizeDp) : density.dp(kLinearHeightDp);

    setMeasuredSize(QSizeF(width.resolve(desiredWidth), height.resolve(desiredHeight)));
}

void Progress::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    if (type_ == ProgressType::Linear)
        paintLinear(painter, content);
    else
        paintCircular(painter, content);
}

void Progress::paintLinear(QPainter& painter, const QRectF& content)
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    const qreal height = std::min(content.height(), density.dp(kLinearHeightDp));
    if (height <= 0.0)
        return;

    const qreal radius = height / 2.0;
    const QRectF track(content.left(), content.center().y() - height / 2.0,
                       content.width(), height);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    painter.setBrush(colours.surfaceContainerHighest);
    painter.drawRoundedRect(track, radius, radius);

    if (buffer_ > 0.0 && buffer_ > value_) {
        painter.setBrush(colours.primaryContainer);
        painter.drawRoundedRect(QRectF(track.left(), track.top(),
                                       track.width() * buffer_, track.height()),
                                radius, radius);
    }

    painter.setBrush(colours.primary);

    if (indeterminate_) {
        // A 35 % segment that sweeps from just outside the leading edge to
        // just outside the trailing edge over one phase.
        const qreal segment = track.width() * 0.35;
        const qreal start = track.left() + phase_ * (track.width() + segment) - segment;
        painter.drawRoundedRect(QRectF(start, track.top(), segment, track.height()),
                                radius, radius);
    } else if (value_ > 0.0) {
        painter.drawRoundedRect(QRectF(track.left(), track.top(),
                                       track.width() * value_, track.height()),
                                radius, radius);
    }

    painter.restore();
}

void Progress::paintCircular(QPainter& painter, const QRectF& content)
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    const qreal stroke = density.dp(kCircularStrokeDp);
    const qreal side = std::min(content.width(), content.height());
    if (stroke <= 0.0 || side <= stroke)
        return;

    const QRectF circle(content.center().x() - (side - stroke) / 2.0,
                        content.center().y() - (side - stroke) / 2.0,
                        side - stroke, side - stroke);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);

    painter.setPen(QPen(colours.surfaceContainerHighest, stroke, Qt::SolidLine, Qt::RoundCap));
    painter.drawEllipse(circle);

    painter.setPen(QPen(colours.primary, stroke, Qt::SolidLine, Qt::RoundCap));

    constexpr qreal kSixteenths = 16.0;
    if (indeterminate_) {
        const qreal sweep = 90.0 + 90.0 * (0.5 + 0.5 * std::sin(2.0 * kPi * phase_));
        const qreal start = 90.0 + 360.0 * phase_;
        painter.drawArc(circle, static_cast<int>(qRound(start * kSixteenths)),
                        static_cast<int>(qRound(-sweep * kSixteenths)));
    } else if (value_ > 0.0) {
        painter.drawArc(circle, 90 * 16,
                        static_cast<int>(qRound(-value_ * 360.0 * kSixteenths)));
    }

    painter.restore();
}

bool Progress::focusable() const
{
    return false;
}

} // namespace md
