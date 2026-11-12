// Periodicity — Scrollbar, the scrolling indicator and its controller.
//
// The value is the only state: the thumb rectangle, the page directions and
// the hit test are all derived from it on demand, so a drag cannot leave the
// painted thumb and the interactive thumb in different places. There is no
// animation in the value path by design — a scrollbar follows the pointer, not
// a curve.

#include <md/components/scrollbar.h>

#include <md/theme/theme.h>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace md {

namespace {

constexpr qreal kThumbRadiusDp = 4.0;
constexpr qreal kMinThumbDp = 32.0;
constexpr qreal kDefaultLengthDp = 240.0;

} // namespace

Scrollbar::Scrollbar(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundColour(QColor(Qt::transparent));
    // The thumb already changes colour on hover/drag; the shared state overlay
    // and ripple would paint a full-width wash over the track.
    setInteractionFeedbackEnabled(false);
    setAccessibleName(tr("Scrollbar"));
}

Scrollbar::Scrollbar(Orientation orientation, QWidget* parent)
    : Scrollbar(parent)
{
    orientation_ = orientation;
}

void Scrollbar::setOrientation(Orientation orientation)
{
    if (orientation_ == orientation)
        return;

    orientation_ = orientation;
    updateGeometry();
    requestUpdate();
}

void Scrollbar::setRange(int minimum, int maximum)
{
    const int low = std::min(minimum, maximum);
    const int high = std::max(minimum, maximum);
    if (minimum_ == low && maximum_ == high)
        return;

    minimum_ = low;
    maximum_ = high;
    requestUpdate();

    const int clamped = std::clamp(value_, minimum_, maximum_);
    if (clamped != value_) {
        value_ = clamped;
        emit valueChanged(value_);
    }
}

void Scrollbar::setValue(int value)
{
    const int clamped = std::clamp(value, minimum_, maximum_);
    if (clamped == value_)
        return;

    value_ = clamped;
    requestUpdate();
    emit valueChanged(value_);
}

void Scrollbar::setPageStep(int step)
{
    const int value = std::max(0, step);
    if (pageStep_ == value)
        return;

    pageStep_ = value;
    requestUpdate();
}

void Scrollbar::setSingleStep(int step)
{
    const int value = std::max(0, step);
    if (singleStep_ == value)
        return;

    singleStep_ = value;
    requestUpdate();
}

int Scrollbar::singleStep() const
{
    return singleStep_;
}

int Scrollbar::effectiveSingleStep() const
{
    if (singleStep_ > 0)
        return singleStep_;

    if (pageStep_ > 0)
        return std::max(1, pageStep_ / 4);

    return std::max(1, (maximum_ - minimum_) / 10);
}

void Scrollbar::setViewportExtent(int extent)
{
    const int value = std::max(0, extent);
    if (viewportExtent_ == value)
        return;

    viewportExtent_ = value;
    requestUpdate();
}

void Scrollbar::setThickness(qreal dp)
{
    const qreal value = std::max(qreal(1.0), dp);
    if (qFuzzyCompare(thicknessDp_, value))
        return;

    thicknessDp_ = value;
    updateGeometry();
    requestUpdate();
}

void Scrollbar::setHoverThickness(qreal dp)
{
    const qreal value = std::max(qreal(1.0), dp);
    if (qFuzzyCompare(hoverThicknessDp_, value))
        return;

    hoverThicknessDp_ = value;
    updateGeometry();
    requestUpdate();
}

bool Scrollbar::isVertical() const noexcept
{
    return orientation_ == Orientation::Vertical;
}

qreal Scrollbar::length() const
{
    return isVertical() ? static_cast<qreal>(height()) : static_cast<qreal>(width());
}

qreal Scrollbar::thicknessPx() const
{
    return Theme::forWidget(this).density().dp(thicknessDp_);
}

qreal Scrollbar::hoverThicknessPx() const
{
    return Theme::forWidget(this).density().dp(std::max(thicknessDp_, hoverThicknessDp_));
}

qreal Scrollbar::thumbLength(qreal trackLength) const
{
    if (trackLength <= 0.0)
        return 0.0;

    const int span = maximum_ - minimum_;
    if (span <= 0)
        return trackLength;

    const qreal extent = viewportExtent_ > 0 ? static_cast<qreal>(viewportExtent_)
                                             : static_cast<qreal>(span) / 3.0;
    const qreal ratio = extent / (extent + static_cast<qreal>(span));

    const qreal minimumLength = std::min(Theme::forWidget(this).density().dp(kMinThumbDp),
                                         trackLength);
    return std::clamp(ratio * trackLength, minimumLength, trackLength);
}

qreal Scrollbar::thumbOffset(qreal trackLength) const
{
    const qreal thumb = thumbLength(trackLength);
    const qreal travel = std::max(qreal(0.0), trackLength - thumb);
    const int span = maximum_ - minimum_;
    if (span <= 0 || travel <= 0.0)
        return 0.0;

    const qreal position = static_cast<qreal>(value_ - minimum_) / static_cast<qreal>(span);
    return std::clamp(position * travel, qreal(0.0), travel);
}

QRectF Scrollbar::trackRect() const
{
    const qreal cross = thicknessPx();
    if (isVertical()) {
        const qreal x = (static_cast<qreal>(width()) - cross) / 2.0;
        return QRectF(x, 0.0, cross, static_cast<qreal>(height()));
    }

    const qreal y = (static_cast<qreal>(height()) - cross) / 2.0;
    return QRectF(0.0, y, static_cast<qreal>(width()), cross);
}

QRectF Scrollbar::thumbRect() const
{
    const QRectF track = trackRect();
    if (track.isEmpty())
        return {};

    const qreal trackLength = isVertical() ? track.height() : track.width();
    const qreal thumb = thumbLength(trackLength);
    const qreal offset = thumbOffset(trackLength);
    const qreal cross = (dragging_ || state().testFlag(ViewState::Hovered))
        ? hoverThicknessPx()
        : thicknessPx();

    if (isVertical()) {
        return QRectF((static_cast<qreal>(width()) - cross) / 2.0, track.top() + offset,
                      cross, thumb);
    }

    return QRectF(track.left() + offset, (static_cast<qreal>(height()) - cross) / 2.0, thumb,
                  cross);
}

qreal Scrollbar::positionOnTrack(const QPointF& position) const
{
    const QRectF track = trackRect();
    return isVertical() ? position.y() - track.top() : position.x() - track.left();
}

void Scrollbar::valueFromTrackPosition(qreal position, qreal grabOffset)
{
    const QRectF track = trackRect();
    const qreal trackLength = isVertical() ? track.height() : track.width();
    const qreal thumb = thumbLength(trackLength);
    const qreal travel = std::max(qreal(0.0), trackLength - thumb);
    const int span = maximum_ - minimum_;
    if (travel <= 0.0 || span <= 0)
        return;

    const qreal relative = std::clamp(position - grabOffset, qreal(0.0), travel);
    const int target = minimum_
        + static_cast<int>(qRound(relative / travel * static_cast<qreal>(span)));
    setValue(target);
}

void Scrollbar::pageBy(int direction)
{
    const int step = std::max(1, pageStep_ > 0 ? pageStep_ : effectiveSingleStep());
    setValue(value_ + direction * step);
}

void Scrollbar::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);
    const qreal cross = theme.density().dp(std::max(thicknessDp_, hoverThicknessDp_));
    const qreal naturalLength = theme.density().dp(kDefaultLengthDp);

    if (isVertical()) {
        setMeasuredSize(QSizeF(width.resolve(cross), height.resolve(0.0)));
    } else {
        setMeasuredSize(QSizeF(width.resolve(naturalLength), height.resolve(cross)));
    }
}

void Scrollbar::paintContent(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);

    const QRectF track = trackRect();
    if (track.width() <= 0.0 || track.height() <= 0.0)
        return;

    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    // A Material scrollbar is a thumb, not a rail: the track only appears while
    // the pointer is over the bar (or the thumb is being dragged), so an idle
    // scrollbar is a 8 dp pill rather than a permanent groove in the surface.
    const bool active = dragging_ || state().testFlag(ViewState::Hovered);
    if (active) {
        painter.setBrush(colours.surfaceContainerHighest);
        const qreal trackRadius = theme.density().dp(kThumbRadiusDp);
        painter.drawRoundedRect(track, trackRadius, trackRadius);
    }

    const QRectF thumb = thumbRect();
    if (!thumb.isEmpty()) {
        QColor thumbColour = active ? colours.primary : colours.onSurfaceVariant;
        if (!isEnabled())
            thumbColour.setAlphaF(0.38F);

        painter.setBrush(thumbColour);
        const qreal radius = std::min(thumb.width(), thumb.height()) / 2.0;
        painter.drawRoundedRect(thumb, radius, radius);
    }

    painter.restore();
}

void Scrollbar::mousePressEvent(QMouseEvent* event)
{
    View::mousePressEvent(event);

    if (!isEnabled() || event->button() != Qt::LeftButton)
        return;

    setFocus(Qt::MouseFocusReason);

    if (thumbRect().contains(event->position())) {
        const QRectF track = trackRect();
        const qreal trackLength = isVertical() ? track.height() : track.width();
        dragging_ = true;
        grabOffset_ = positionOnTrack(event->position()) - thumbOffset(trackLength);
        emit sliderPressed();
        requestUpdate();
        return;
    }

    if (trackRect().contains(event->position())) {
        const QRectF thumb = thumbRect();
        const qreal position = positionOnTrack(event->position());
        const qreal thumbStart = isVertical() ? thumb.top() - trackRect().top()
                                              : thumb.left() - trackRect().left();
        pageBy(position < thumbStart ? -1 : 1);
    }
}

void Scrollbar::mouseMoveEvent(QMouseEvent* event)
{
    View::mouseMoveEvent(event);

    if (!dragging_ || !isEnabled())
        return;

    valueFromTrackPosition(positionOnTrack(event->position()), grabOffset_);
}

void Scrollbar::mouseReleaseEvent(QMouseEvent* event)
{
    const bool wasDragging = dragging_;
    dragging_ = false;

    View::mouseReleaseEvent(event);

    if (wasDragging) {
        emit sliderReleased();
        requestUpdate();
    }
}

void Scrollbar::leaveEvent(QEvent* event)
{
    // A drag continues outside the widget: Qt keeps the implicit mouse grab
    // until the button is released, and the value must follow it.
    View::leaveEvent(event);
}

void Scrollbar::keyPressEvent(QKeyEvent* event)
{
    if (!isEnabled()) {
        View::keyPressEvent(event);
        return;
    }

    switch (event->key()) {
    case Qt::Key_Up:
    case Qt::Key_Left:
        setValue(value_ - effectiveSingleStep());
        event->accept();
        return;
    case Qt::Key_Down:
    case Qt::Key_Right:
        setValue(value_ + effectiveSingleStep());
        event->accept();
        return;
    case Qt::Key_PageUp:
        pageBy(-1);
        event->accept();
        return;
    case Qt::Key_PageDown:
        pageBy(1);
        event->accept();
        return;
    case Qt::Key_Home:
        setValue(minimum_);
        event->accept();
        return;
    case Qt::Key_End:
        setValue(maximum_);
        event->accept();
        return;
    default:
        break;
    }

    View::keyPressEvent(event);
}

void Scrollbar::wheelEvent(QWheelEvent* event)
{
    if (!isEnabled()) {
        View::wheelEvent(event);
        return;
    }

    const int delta = isVertical() ? event->angleDelta().y() : event->angleDelta().x();
    if (delta == 0) {
        View::wheelEvent(event);
        return;
    }

    const int notches = delta / 120;
    const int step = effectiveSingleStep() * (notches == 0 ? (delta > 0 ? 1 : -1) : notches);
    setValue(value_ - step);
    event->accept();
}

void Scrollbar::onThemeChanged()
{
    updateGeometry();
    requestUpdate();
}

bool Scrollbar::focusable() const
{
    return isEnabled();
}

} // namespace md
