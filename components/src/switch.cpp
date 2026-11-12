// Periodicity — the Switch component (framework.md §5.9).
//
// The track, the thumb and the touch target are three different sizes: the
// 52 x 32 dp pill is the visual, the 24 dp thumb is inset 4 dp inside it, and
// the 48 dp widget height is the WCAG touch target. The track is centred in the
// target, so a click anywhere in the target toggles.
//
// The thumb position and the two colour pairs are one interpolated value:
// position_ runs from 0 (unchecked) to 1 (checked) and is advanced by an
// Animator over the short4 motion token. The animator is stopped before every
// run because its retargeting continues from its own previous progress; a fresh
// ramp from 0 lets the interpolation below continue from the painted position
// without a jump. A zero-length token (reduced motion) or a hidden widget sets
// the end state directly.

#include <md/components/switch.h>

#include <md/theme/theme.h>
#include <md/tokens/state_layer.h>

#include <QColor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QSizeF>

#include <algorithm>

namespace md {

namespace {

constexpr qreal kTrackWidthDp = 52.0;
constexpr qreal kTrackHeightDp = 32.0;
constexpr qreal kThumbDiameterDp = 24.0;
constexpr qreal kThumbInsetDp = 4.0;
// WCAG 2.5.5 and the design baseline: the target is 48 dp tall even though the track is 32 dp.
constexpr qreal kTouchHeightDp = 48.0;

QColor withAlpha(const QColor& colour, qreal alpha)
{
    QColor result = colour;
    result.setAlphaF(static_cast<float>(std::clamp(alpha, 0.0, 1.0)));
    return result;
}

// Linear cross-fade of the four channels, used for the track and thumb roles
// while the two states cross-fade. Rounding per channel keeps the endpoint
// colours exact, which the visual tests rely on.
QColor blend(const QColor& from, const QColor& to, qreal fraction)
{
    const qreal amount = std::clamp(fraction, 0.0, 1.0);
    const auto channel = [amount](int start, int end) {
        return static_cast<int>(qRound(start + (end - start) * amount));
    };

    return QColor(channel(from.red(), to.red()), channel(from.green(), to.green()),
                  channel(from.blue(), to.blue()), channel(from.alpha(), to.alpha()));
}

} // namespace

Switch::Switch(QWidget* parent)
    : View(parent)
{
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("Switch"));
    setBackgroundColour(QColor(Qt::transparent));
    // The state layer and the focus ring follow the pill of the whole target;
    // both are painted by View's pipeline, not by this component.
    setShape(Theme::forWidget(this).shapes().full);
}

void Switch::setChecked(bool checked)
{
    applyChecked(checked);
}

bool Switch::isChecked() const noexcept
{
    return checked_;
}

void Switch::applyChecked(bool checked)
{
    if (checked_ == checked)
        return;

    checked_ = checked;
    startTransition();
    emit toggled(checked_);
}

void Switch::startTransition()
{
    const qreal target = checked_ ? 1.0 : 0.0;
    const MotionSpec spec = Theme::forWidget(this).motion().short4();

    // stop() drops the previous callback: left running, it would keep writing
    // the old target into position_ after this state change.
    animator_.stop();

    // A hidden widget has no frames to show and reduced motion asks for the
    // end state directly; either way the value is exact before the next paint.
    if (spec.duration.count() <= 0 || !isVisible()) {
        position_ = target;
        requestUpdate();
        return;
    }

    const qreal from = position_;
    if (from == target) {
        requestUpdate();
        return;
    }

    animator_.start(spec, [this, from, target](qreal fraction) {
        position_ = from + (target - from) * fraction;
        requestUpdate();
    });
}

void Switch::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    setMeasuredSize(QSizeF(width.resolve(density.dp(kTrackWidthDp)),
                           height.resolve(density.dp(kTouchHeightDp))));
}

void Switch::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const bool enabled = isEnabled();

    // The 52 x 32 dp visual, centred in the 48 dp touch target.
    const qreal trackWidth = std::min(content.width(), density.dp(kTrackWidthDp));
    const qreal trackHeight = std::min(content.height(), density.dp(kTrackHeightDp));
    if (trackWidth <= 0.0 || trackHeight <= 0.0)
        return;

    const QRectF track(content.center().x() - trackWidth / 2.0,
                       content.center().y() - trackHeight / 2.0, trackWidth, trackHeight);

    const qreal inset = density.dp(kThumbInsetDp);
    const qreal thumbDiameter = std::max(
        qreal(0.0), std::min(density.dp(kThumbDiameterDp), trackHeight - inset * 2.0));

    const qreal fraction = std::clamp(position_, 0.0, 1.0);
    QColor trackColour = blend(colours.surfaceContainerHighest, colours.primary, fraction);
    QColor thumbColour = blend(colours.outline, colours.onPrimary, fraction);

    if (!enabled) {
        // The disabled switch keeps the design baseline's container/content split: the track is
        // the onSurface container tone and the thumb the content tone, so the
        // control reads as inactive without disappearing.
        trackColour = withAlpha(colours.onSurface, kDisabledContainerOpacity);
        thumbColour = checked_ ? colours.surface
                               : withAlpha(colours.onSurface, kDisabledContentOpacity);
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    painter.setBrush(trackColour);
    painter.drawPath(shape().path(track));

    if (thumbDiameter > 0.0) {
        // The thumb travels between the two 4 dp insets; the gap to the track
        // edge is therefore the same at rest in either state.
        const qreal firstX = track.left() + inset;
        const qreal lastX = track.right() - inset - thumbDiameter;
        const qreal thumbX = firstX + (lastX - firstX) * fraction;

        painter.setBrush(thumbColour);
        painter.drawEllipse(QRectF(thumbX, track.center().y() - thumbDiameter / 2.0,
                                   thumbDiameter, thumbDiameter));
    }

    painter.restore();
}

void Switch::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        pressedInside_ = QRectF(rect()).contains(event->position());

    View::mousePressEvent(event);
}

void Switch::mouseReleaseEvent(QMouseEvent* event)
{
    const bool inside = QRectF(rect()).contains(event->position());
    const bool activate = pressedInside_ && inside && event->button() == Qt::LeftButton;
    pressedInside_ = false;

    View::mouseReleaseEvent(event);

    if (activate && isEnabled())
        applyChecked(!checked_);
}

void Switch::keyPressEvent(QKeyEvent* event)
{
    if (isEnabled() && !event->isAutoRepeat() && event->key() == Qt::Key_Space) {
        applyChecked(!checked_);
        event->accept();
        return;
    }

    View::keyPressEvent(event);
}

void Switch::onHidden()
{
    // A hidden switch would otherwise keep a frame timer alive for a state
    // nobody can see; the end state is what matters when it is shown again.
    animator_.stop();
    position_ = checked_ ? 1.0 : 0.0;
    requestUpdate();
}

void Switch::onThemeChanged()
{
    // A theme can change the shape token, so the pill used for the track, the
    // focus ring and the state layer is re-resolved here.
    setShape(Theme::forWidget(this).shapes().full);

    // Switching reduced motion on while a run is in flight stops the frame
    // timer and jumps to the state the widget already reports.
    const MotionSpec spec = Theme::forWidget(this).motion().short4();
    if (spec.duration.count() <= 0) {
        animator_.stop();
        position_ = checked_ ? 1.0 : 0.0;
    }

    requestUpdate();
}

} // namespace md
