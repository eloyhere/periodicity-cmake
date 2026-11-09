// Periodicity — the Rate component (framework.md §5.9).
//
// Every star is a ten-point QPainterPath (five outer and five inner vertices)
// filled proportionally: the base path is painted in onSurfaceVariant and the
// value fraction is clipped to the same path and filled with primary, so a
// half star is genuinely half a path rather than a second glyph. Hover paints
// the prospective value at the state-layer opacity; a click commits it.

#include <md/components/rate.h>

#include <md/theme/theme.h>
#include <md/tokens/state_layer.h>

#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>

#include <algorithm>
#include <cmath>

namespace md {

namespace {

constexpr qreal kStarDp = 24.0;
constexpr qreal kGapDp = 4.0;
constexpr qreal kTouchHeightDp = 48.0;
constexpr qreal kPi = 3.14159265358979323846;

QColor withAlpha(const QColor& colour, qreal alpha)
{
    QColor result = colour;
    result.setAlphaF(static_cast<float>(std::clamp(alpha, 0.0, 1.0)));
    return result;
}

QPainterPath starPath(const QRectF& bounds)
{
    const QPointF centre = bounds.center();
    const qreal outer = std::min(bounds.width(), bounds.height()) / 2.0;
    const qreal inner = outer * 0.382;

    QPainterPath path;
    for (int i = 0; i < 10; ++i) {
        const qreal radius = (i % 2 == 0) ? outer : inner;
        const qreal radians = (-90.0 + static_cast<qreal>(i) * 36.0) * kPi / 180.0;
        const QPointF point(centre.x() + radius * std::cos(radians),
                            centre.y() + radius * std::sin(radians));
        if (i == 0)
            path.moveTo(point);
        else
            path.lineTo(point);
    }
    path.closeSubpath();
    return path;
}

} // namespace

Rate::Rate(QWidget* parent)
    : View(parent)
{
    setAttribute(Qt::WA_Hover, true);
    // The hover preview needs moves without a button held down.
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundColour(QColor(Qt::transparent));
}

void Rate::setMax(int maximum)
{
    const int next = std::max(1, maximum);
    if (maximum_ == next)
        return;

    maximum_ = next;
    if (value_ > static_cast<qreal>(maximum_))
        applyValue(static_cast<qreal>(maximum_));
    requestUpdate();
}

int Rate::maximum() const noexcept
{
    return maximum_;
}

void Rate::setValue(qreal value)
{
    applyValue(value);
}

qreal Rate::value() const noexcept
{
    return value_;
}

void Rate::setStep(qreal step)
{
    const qreal next = step > 0.0 ? step : 0.0;
    if (step_ == next)
        return;

    step_ = next;
    requestUpdate();
}

qreal Rate::step() const noexcept
{
    return step_;
}

qreal Rate::snap(qreal value) const
{
    const qreal clamped = std::clamp(value, 0.0, static_cast<qreal>(maximum_));
    if (step_ <= 0.0)
        return clamped;

    const qreal stepped = static_cast<qreal>(qRound(clamped / step_)) * step_;
    return std::clamp(stepped, 0.0, static_cast<qreal>(maximum_));
}

qreal Rate::valueFromX(qreal x) const
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal star = density.dp(kStarDp);
    const qreal pitch = star + density.dp(kGapDp);
    if (pitch <= 0.0)
        return 0.0;

    // A pointer at the centre of star i means value i + 1; the half-star
    // boundary therefore falls on the star centre.
    const qreal raw = (x - contentRect().left() - star / 2.0) / pitch + 1.0;
    return snap(raw);
}

void Rate::applyValue(qreal value)
{
    const qreal next = std::clamp(value, 0.0, static_cast<qreal>(maximum_));
    if (next == value_)
        return;

    value_ = next;
    requestUpdate();
    emit valueChanged(value_);
}

void Rate::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal star = density.dp(kStarDp);
    const qreal pitch = star + density.dp(kGapDp);

    const qreal desiredWidth = star + static_cast<qreal>(maximum_ - 1) * pitch;
    setMeasuredSize(QSizeF(width.resolve(desiredWidth),
                           height.resolve(density.dp(kTouchHeightDp))));
}

void Rate::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const bool enabled = isEnabled();

    const qreal star = density.dp(kStarDp);
    const qreal pitch = star + density.dp(kGapDp);
    const qreal top = content.center().y() - star / 2.0;

    const QColor base = enabled ? colours.onSurfaceVariant
                                : withAlpha(colours.onSurface, kDisabledContentOpacity);
    const QColor fill = enabled ? colours.primary
                                : withAlpha(colours.onSurface, kDisabledContentOpacity);
    const QColor previewFill = withAlpha(fill, 0.38);

    const bool previewing = preview_ >= 0.0 && preview_ > value_;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    for (int i = 0; i < maximum_; ++i) {
        const QRectF starRect(content.left() + static_cast<qreal>(i) * pitch, top, star, star);
        const QPainterPath path = starPath(starRect);
        painter.fillPath(path, base);

        if (previewing) {
            const qreal previewFraction = std::clamp(preview_ - static_cast<qreal>(i), 0.0, 1.0);
            if (previewFraction > 0.0) {
                painter.save();
                painter.setClipPath(path);
                painter.fillRect(QRectF(starRect.left(), starRect.top(),
                                        starRect.width() * previewFraction, starRect.height()),
                                 previewFill);
                painter.restore();
            }
        }

        const qreal fraction = std::clamp(value_ - static_cast<qreal>(i), 0.0, 1.0);
        if (fraction > 0.0) {
            painter.save();
            painter.setClipPath(path);
            painter.fillRect(QRectF(starRect.left(), starRect.top(),
                                    starRect.width() * fraction, starRect.height()),
                             fill);
            painter.restore();
        }
    }

    painter.restore();
}

void Rate::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        pressedInside_ = QRectF(rect()).contains(event->position());
        preview_ = valueFromX(event->position().x());
        requestUpdate();
    }

    View::mousePressEvent(event);
}

void Rate::mouseMoveEvent(QMouseEvent* event)
{
    if (isEnabled()) {
        preview_ = valueFromX(event->position().x());
        requestUpdate();
    }

    View::mouseMoveEvent(event);
}

void Rate::mouseReleaseEvent(QMouseEvent* event)
{
    const bool inside = QRectF(rect()).contains(event->position());
    const bool activate = pressedInside_ && inside && event->button() == Qt::LeftButton;

    if (activate)
        applyValue(valueFromX(event->position().x()));

    pressedInside_ = false;
    preview_ = -1.0;
    requestUpdate();

    View::mouseReleaseEvent(event);
}

void Rate::leaveEvent(QEvent* event)
{
    preview_ = -1.0;
    requestUpdate();

    View::leaveEvent(event);
}

void Rate::keyPressEvent(QKeyEvent* event)
{
    const qreal step = step_ > 0.0 ? step_ : 1.0;
    bool handled = true;

    switch (event->key()) {
    case Qt::Key_Right:
    case Qt::Key_Up:
        applyValue(snap(value_ + step));
        break;
    case Qt::Key_Left:
    case Qt::Key_Down:
        applyValue(snap(value_ - step));
        break;
    case Qt::Key_Home:
        applyValue(0.0);
        break;
    case Qt::Key_End:
        applyValue(static_cast<qreal>(maximum_));
        break;
    default:
        handled = false;
        break;
    }

    if (handled) {
        event->accept();
        return;
    }

    View::keyPressEvent(event);
}

} // namespace md
