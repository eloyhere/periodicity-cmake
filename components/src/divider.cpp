// Periodicity — the Divider component (framework.md §5.9, §7).
//
// The component has no state beyond its geometry. Orientation, insets and
// thickness are read directly by measureOverride() and paintContent(), and the
// colour is resolved from the theme on every paint, so a ThemeScope change
// repaints the rule in the new outlineVariant without a cached value.
//
// Painting is deliberately not the base class's antialiased path. A 1 dp rule
// drawn with antialiasing is either a soft two-pixel smear or, when the
// thickness falls below one device pixel, an invisible one. paintContent()
// disables antialiasing, snaps the rectangle to the device-pixel grid and
// fills it, which makes the line exactly the number of whole device pixels the
// dp thickness resolves to at the current ratio.

#include <md/components/divider.h>

#include <md/theme/theme.h>

#include <QColor>
#include <QPainter>
#include <QRectF>
#include <QSizeF>
#include <QSizePolicy>

#include <cmath>

namespace md {

namespace {

// Public setters accept a qreal, so negative, NaN and infinite values are
// normalised once here: they would otherwise turn into an invalid paint
// rectangle or a negative measure.
qreal nonNegativeLength(qreal value) noexcept
{
    return std::isfinite(value) && value > 0.0 ? value : 0.0;
}

qreal painterDevicePixelRatio(const QPainter& painter) noexcept
{
    const QPaintDevice* device = painter.device();
    if (device == nullptr)
        return 1.0;

    const qreal ratio = device->devicePixelRatioF();
    return ratio > 0.0 ? ratio : 1.0;
}

// Rounds both edges of `rect` onto the device-pixel grid. With antialiasing
// off Qt covers the pixels whose centres fall inside the rectangle, so an edge
// at a fractional device coordinate can be rounded differently per platform
// and per paint; snapping first makes the rule land on exactly the intended
// rows. A sub-pixel extent is widened to one device pixel rather than
// disappearing.
QRectF snapToDevicePixels(const QPainter& painter, const QRectF& rect)
{
    const qreal ratio = painterDevicePixelRatio(painter);
    const qreal minimum = 1.0 / ratio;

    const auto snap = [ratio](qreal value) { return std::round(value * ratio) / ratio; };

    qreal left = snap(rect.left());
    qreal top = snap(rect.top());
    qreal right = snap(rect.left() + rect.width());
    qreal bottom = snap(rect.top() + rect.height());

    if (right - left < minimum)
        right = left + minimum;
    if (bottom - top < minimum)
        bottom = top + minimum;

    return QRectF(left, top, right - left, bottom - top);
}

} // namespace

Divider::Divider(QWidget* parent)
    : Divider(Orientation::Horizontal, parent)
{
}

Divider::Divider(Orientation orientation, QWidget* parent)
    : View(parent)
    , orientation_(orientation)
{
    // The base class would otherwise paint the tonal surface of the current
    // elevation behind the rule; a divider is only the rule.
    setBackgroundColour(QColor(Qt::transparent));
    setFocusPolicy(Qt::NoFocus);
    // No hover overlay and no ripple: a hairline must not react to the pointer.
    setInteractionFeedbackEnabled(false);
    applyOrientationTraits();
}

void Divider::setOrientation(Orientation orientation)
{
    if (orientation_ == orientation)
        return;

    orientation_ = orientation;
    applyOrientationTraits();
    updateGeometry();
    requestUpdate();
}

Divider::Orientation Divider::orientation() const noexcept
{
    return orientation_;
}

void Divider::setInset(qreal startDp, qreal endDp)
{
    const qreal start = nonNegativeLength(startDp);
    const qreal end = nonNegativeLength(endDp);
    if (startInset_ == start && endInset_ == end)
        return;

    startInset_ = start;
    endInset_ = end;
    // The measured rectangle is deliberately unchanged: an inset divider keeps
    // the space it reserved and only shortens the line inside it.
    requestUpdate();
}

qreal Divider::insetStart() const noexcept
{
    return startInset_;
}

qreal Divider::insetEnd() const noexcept
{
    return endInset_;
}

void Divider::setThickness(qreal dp)
{
    const qreal thickness = nonNegativeLength(dp);
    if (thickness_ == thickness)
        return;

    thickness_ = thickness;
    // The thin axis is part of the natural size, so a thickness change is a
    // geometry request as well as a repaint.
    updateGeometry();
    requestUpdate();
}

qreal Divider::thickness() const noexcept
{
    return thickness_;
}

void Divider::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const qreal line = Theme::forWidget(this).density().dp(thickness_);
    const bool horizontal = orientation_ == Orientation::Horizontal;

    // The fill axis has no natural length, but an offered bound is the length
    // to fill: Exactly forces it and AtMost is adopted as-is (the bound is
    // still respected). Only a completely unspecified spec leaves it at zero,
    // because the parent has then expressed no length at all.
    const MeasureSpec fillSpec = horizontal ? width : height;
    const qreal fill =
        fillSpec.mode() == MeasureSpec::Mode::Unspecified ? 0.0 : fillSpec.size();

    setMeasuredSize(horizontal ? QSizeF(width.resolve(fill), height.resolve(line))
                               : QSizeF(width.resolve(line), height.resolve(fill)));
}

void Divider::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty() || thickness_ <= 0.0)
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal line = density.dp(thickness_);
    if (line <= 0.0)
        return;

    const bool horizontal = orientation_ == Orientation::Horizontal;
    // The insets are logical and expressed in dp: the leading inset sits on
    // the right of a horizontal divider in a right-to-left layout. A vertical
    // divider has no horizontal direction to mirror, so its leading end is
    // always the top.
    const bool mirrored = horizontal && layoutDirection() == Qt::RightToLeft;
    const qreal leading = density.dp(mirrored ? endInset_ : startInset_);
    const qreal trailing = density.dp(mirrored ? startInset_ : endInset_);

    QRectF rule;
    if (horizontal) {
        const qreal span = content.width() - leading - trailing;
        if (span <= 0.0)
            return;

        rule = QRectF(content.left() + leading, content.center().y() - line / 2.0, span,
                      line);
    } else {
        const qreal span = content.height() - leading - trailing;
        if (span <= 0.0)
            return;

        rule = QRectF(content.center().x() - line / 2.0, content.top() + leading, line,
                      span);
    }

    rule = snapToDevicePixels(painter, rule);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(theme.colours().outlineVariant);
    painter.drawRect(rule);
    painter.restore();
}

bool Divider::focusable() const
{
    return false;
}

void Divider::applyOrientationTraits()
{
    const bool horizontal = orientation_ == Orientation::Horizontal;

    setAccessibleName(horizontal ? tr("Divider") : tr("Separator"));

    // The fill axis expands in a plain Qt layout; the thin axis stays at its
    // natural thickness. The framework's own measure contract already adopts
    // an AtMost bound, so this only matters to hosts that use QLayout.
    setSizePolicy(horizontal ? QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed)
                             : QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding));
}

} // namespace md
