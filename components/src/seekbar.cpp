// Periodicity — the SeekBar component (framework.md §5.9).
//
// The track and thumb geometry is derived from a 48 dp touch target: the thumb
// radius insets the usable track, the track sits low enough that the hover /
// drag value label fits above it, and every dimension is a dp conversion of a
// design baseline token. Dragging updates the value on every move while the pointer
// is down; a plain click jumps to the pressed position.

#include <md/components/seekbar.h>

#include <md/theme/theme.h>
#include <md/tokens/elevation.h>
#include <md/tokens/state_layer.h>

#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <utility>

namespace md {

namespace {

constexpr qreal kThumbRadiusDp = 10.0;
constexpr qreal kTrackHeightDp = 4.0;
constexpr qreal kTouchHeightDp = 48.0;
constexpr qreal kDefaultWidthDp = 200.0;
constexpr qreal kTrackBottomInsetDp = 16.0;
constexpr qreal kLabelHeightDp = 16.0;
constexpr qreal kMaxTicks = 200.0;

QColor withAlpha(const QColor& colour, qreal alpha)
{
    QColor result = colour;
    result.setAlphaF(static_cast<float>(std::clamp(alpha, 0.0, 1.0)));
    return result;
}

QFont labelFontFor(const Theme& theme)
{
    const TextStyle& style = theme.type().labelSmall;
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(static_cast<QFont::Weight>(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
    return font;
}

// How many decimals a value at this step deserves. A continuous slider shows
// whole numbers unless the range says otherwise; the step decides otherwise.
int decimalsFor(qreal step)
{
    if (step <= 0.0 || step >= 1.0)
        return 0;
    if (step >= 0.1)
        return 1;
    return 2;
}

} // namespace

SeekBar::SeekBar(QWidget* parent)
    : View(parent)
{
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundColour(QColor(Qt::transparent));
}

void SeekBar::setRange(qreal minimum, qreal maximum)
{
    qreal low = minimum;
    qreal high = maximum;
    if (low > high)
        std::swap(low, high);

    if (minimum_ == low && maximum_ == high)
        return;

    minimum_ = low;
    maximum_ = high;

    const qreal clamped = std::clamp(value_, minimum_, maximum_);
    if (clamped != value_) {
        value_ = clamped;
        emit valueChanged(value_);
    }

    requestUpdate();
}

qreal SeekBar::minimum() const noexcept
{
    return minimum_;
}

qreal SeekBar::maximum() const noexcept
{
    return maximum_;
}

void SeekBar::setValue(qreal value)
{
    applyValue(value);
}

qreal SeekBar::value() const noexcept
{
    return value_;
}

void SeekBar::setStep(qreal step)
{
    const qreal next = step > 0.0 ? step : 0.0;
    if (step_ == next)
        return;

    step_ = next;
    requestUpdate();
}

qreal SeekBar::step() const noexcept
{
    return step_;
}

void SeekBar::setTicksVisible(bool visible)
{
    if (ticksVisible_ == visible)
        return;

    ticksVisible_ = visible;
    requestUpdate();
}

bool SeekBar::ticksVisible() const noexcept
{
    return ticksVisible_;
}

qreal SeekBar::snap(qreal value) const
{
    const qreal clamped = std::clamp(value, minimum_, maximum_);
    if (step_ <= 0.0)
        return clamped;

    const qreal stepped = minimum_ + static_cast<qreal>(qRound((clamped - minimum_) / step_)) * step_;
    return std::clamp(stepped, minimum_, maximum_);
}

qreal SeekBar::valueFromX(qreal x) const
{
    const qreal radius = Theme::forWidget(this).density().dp(kThumbRadiusDp);
    const QRectF content = contentRect();
    const qreal left = content.left() + radius;
    const qreal right = content.right() - radius;
    if (right <= left)
        return minimum_;

    const qreal fraction = std::clamp((x - left) / (right - left), 0.0, 1.0);
    return snap(minimum_ + fraction * (maximum_ - minimum_));
}

void SeekBar::applyValue(qreal value)
{
    const qreal next = snap(value);
    if (next == value_)
        return;

    value_ = next;
    requestUpdate();
    emit valueChanged(value_);
}

void SeekBar::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    setMeasuredSize(QSizeF(width.resolve(density.dp(kDefaultWidthDp)),
                           height.resolve(density.dp(kTouchHeightDp))));
}

void SeekBar::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const bool enabled = isEnabled();

    const qreal thumbRadius = density.dp(kThumbRadiusDp);
    const qreal trackHeight = std::min(content.height(), density.dp(kTrackHeightDp));
    const qreal trackY = content.top() + content.height() - density.dp(kTrackBottomInsetDp);
    const qreal trackStart = content.left() + thumbRadius;
    const qreal trackEnd = content.right() - thumbRadius;
    if (trackEnd <= trackStart || trackHeight <= 0.0)
        return;

    const qreal range = maximum_ - minimum_;
    const qreal fraction = range > 0.0 ? (value_ - minimum_) / range : 0.0;
    const qreal thumbX = trackStart + fraction * (trackEnd - trackStart);

    // Design baseline slider colour roles: active track and handle are primary, the inactive
    // track is secondaryContainer (not a surface container), and the stop
    // markers are onSecondaryContainer.
    const QColor inactive = enabled
        ? colours.secondaryContainer
        : withAlpha(colours.onSurface, kDisabledContainerOpacity);
    const QColor active = enabled
        ? colours.primary
        : withAlpha(colours.onSurface, kDisabledContentOpacity);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    const QRectF trackRect(trackStart, trackY - trackHeight / 2.0,
                           trackEnd - trackStart, trackHeight);
    painter.setBrush(inactive);
    painter.drawRoundedRect(trackRect, trackHeight / 2.0, trackHeight / 2.0);

    if (thumbX > trackStart) {
        painter.setBrush(active);
        painter.drawRoundedRect(QRectF(trackStart, trackY - trackHeight / 2.0,
                                       thumbX - trackStart, trackHeight),
                                trackHeight / 2.0, trackHeight / 2.0);
    }

    if (ticksVisible_ && step_ > 0.0 && range > 0.0) {
        const qreal steps = range / step_;
        if (steps <= kMaxTicks) {
            const int tickCount = static_cast<int>(std::floor(steps)) + 1;
            painter.setBrush(enabled ? colours.onSecondaryContainer : inactive);
            for (int i = 0; i < tickCount; ++i) {
                const qreal tickValue = minimum_ + static_cast<qreal>(i) * step_;
                const qreal tickX = trackStart
                    + ((tickValue - minimum_) / range) * (trackEnd - trackStart);
                painter.drawEllipse(QPointF(tickX, trackY), density.dp(1.5), density.dp(1.5));
            }
        }
    }

    painter.setBrush(active);
    painter.drawEllipse(QPointF(thumbX, trackY), thumbRadius, thumbRadius);

    if (state().testFlag(ViewState::Hovered) || dragging_) {
        const QString label = QString::number(value_, 'f', decimalsFor(step_));
        const QFont font = labelFontFor(theme);
        const QFontMetricsF metrics(font);
        const qreal labelWidth = metrics.horizontalAdvance(label) + density.dp(16.0);
        const qreal labelHeight = density.dp(kLabelHeightDp);
        const qreal labelY = std::max(content.top(),
                                      trackY - thumbRadius - density.dp(4.0) - labelHeight);
        const qreal labelX = std::clamp(thumbX - labelWidth / 2.0, content.left(),
                                        std::max(content.left(), content.right() - labelWidth));
        const QRectF labelRect(labelX, labelY, labelWidth, labelHeight);

        painter.setBrush(enabled ? colours.inverseSurface
                                 : withAlpha(colours.onSurface, kDisabledContainerOpacity));
        painter.drawRoundedRect(labelRect, density.dp(4.0), density.dp(4.0));

        painter.setFont(font);
        painter.setPen(enabled ? colours.inverseOnSurface
                               : withAlpha(colours.onSurface, kDisabledContentOpacity));
        painter.drawText(labelRect, static_cast<int>(Qt::AlignCenter), label);
    }

    painter.restore();
}

void SeekBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && isEnabled()) {
        dragging_ = true;
        applyValue(valueFromX(event->position().x()));
    }

    View::mousePressEvent(event);
}

void SeekBar::mouseMoveEvent(QMouseEvent* event)
{
    if (dragging_ && isEnabled())
        applyValue(valueFromX(event->position().x()));

    View::mouseMoveEvent(event);
}

void SeekBar::mouseReleaseEvent(QMouseEvent* event)
{
    if (dragging_) {
        if (isEnabled())
            applyValue(valueFromX(event->position().x()));
        dragging_ = false;
    }

    View::mouseReleaseEvent(event);
}

void SeekBar::keyPressEvent(QKeyEvent* event)
{
    const qreal step = step_ > 0.0 ? step_ : (maximum_ - minimum_) / 100.0;
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
        applyValue(snap(minimum_));
        break;
    case Qt::Key_End:
        applyValue(snap(maximum_));
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
