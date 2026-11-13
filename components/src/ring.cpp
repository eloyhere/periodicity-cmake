// Periodicity — the Ring component (framework.md §5.9).
//
// The ring is a stroked circle whose stroke width is a dp parameter, so the
// outer diameter of the indicator stays inside the widget. Determinate mode
// draws the arc from twelve o'clock clockwise; indeterminate mode rotates a
// trimmed arc, with both the start angle and the sweep derived from the
// Animator phase. The animator restarts itself from finished() and is stopped
// on hide, which keeps a hidden ring free of timers.

#include <md/components/ring.h>

#include <md/theme/theme.h>

#include <QHideEvent>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QShowEvent>

#include <algorithm>
#include <cmath>

namespace md {

namespace {

constexpr qreal kDefaultSizeDp = 48.0;
constexpr qreal kPi = 3.14159265358979323846;

} // namespace

Ring::Ring(QWidget* parent)
    : View(parent)
{
    setBackgroundColour(QColor(Qt::transparent));
    setFocusPolicy(Qt::NoFocus);

    connect(&animator_, &animation::Animator::finished, this, [this] {
        if (indeterminate_ && isVisible())
            startAnimation();
    });
}

void Ring::setValue(qreal value)
{
    const qreal next = std::clamp(value, 0.0, 1.0);
    if (value_ == next)
        return;

    value_ = next;
    requestUpdate();
}

qreal Ring::value() const noexcept
{
    return value_;
}

void Ring::setIndeterminate(bool indeterminate)
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

bool Ring::isIndeterminate() const noexcept
{
    return indeterminate_;
}

void Ring::setStrokeWidthDp(qreal dp)
{
    const qreal next = dp > 0.0 ? dp : 0.0;
    if (strokeWidthDp_ == next)
        return;

    strokeWidthDp_ = next;
    requestUpdate();
}

qreal Ring::strokeWidthDp() const noexcept
{
    return strokeWidthDp_;
}

bool Ring::isAnimating() const noexcept
{
    return animator_.isRunning();
}

void Ring::startAnimation()
{
    if (!indeterminate_)
        return;

    // The new run replaces the old one; stopping here is also what makes the
    // reduced-motion branch cancel a loop started under a previous theme.
    animator_.stop();

    const MotionSpec spec = Theme::forWidget(this).motion().level(MotionLevel::Level3);
    if (spec.duration.count() <= 0) {
        phase_ = 0.25;
        requestUpdate();
        return;
    }

    animator_.start(spec, [this](qreal value) {
        phase_ = value;
        requestUpdate();
    });
}

void Ring::stopAnimation()
{
    animator_.stop();
}

void Ring::onShown()
{
    if (indeterminate_)
        startAnimation();
}

void Ring::onHidden()
{
    stopAnimation();
}

void Ring::onThemeChanged()
{
    if (!indeterminate_ || !isVisible())
        return;

    // Switching reduced motion on stops the rotation; switching it off starts
    // it again. startAnimation() owns both halves of that decision.
    startAnimation();
}

void Ring::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const qreal side = Theme::forWidget(this).density().dp(kDefaultSizeDp);
    setMeasuredSize(QSizeF(width.resolve(side), height.resolve(side)));
}

void Ring::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    const qreal stroke = density.dp(strokeWidthDp_);
    const qreal side = std::min(content.width(), content.height());
    if (stroke <= 0.0 || side <= stroke)
        return;

    const QRectF circle(content.center().x() - (side - stroke) / 2.0,
                        content.center().y() - (side - stroke) / 2.0,
                        side - stroke, side - stroke);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);

    // The track only makes sense for the determinate form; the indeterminate
    // arc is the whole story.
    if (!indeterminate_) {
        painter.setPen(QPen(colours.surfaceContainerHighest, stroke, Qt::SolidLine,
                            Qt::RoundCap));
        painter.drawEllipse(circle);
    }

    painter.setPen(QPen(colours.primary, stroke, Qt::SolidLine, Qt::RoundCap));

    constexpr qreal kSixteenths = 16.0;
    if (indeterminate_) {
        // The trim breathes between a quarter and a three-quarter turn while
        // the whole arc rotates once per phase.
        const qreal sweep = 90.0 + 180.0 * (0.5 + 0.5 * std::sin(2.0 * kPi * phase_));
        const qreal start = 90.0 + 360.0 * phase_;
        painter.drawArc(circle, static_cast<int>(qRound(start * kSixteenths)),
                        static_cast<int>(qRound(-sweep * kSixteenths)));
    } else if (value_ > 0.0) {
        painter.drawArc(circle, 90 * 16,
                        static_cast<int>(qRound(-value_ * 360.0 * kSixteenths)));
    }

    painter.restore();
}

bool Ring::focusable() const
{
    return false;
}

} // namespace md
