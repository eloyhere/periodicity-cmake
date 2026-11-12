// Periodicity — the IconButton component (framework.md §5.9).
//
// The variant decides the container and content roles; the enabled bit decides
// the disabled opacities (framework.md §3.6) and the checked bit, when the
// button is checkable, switches to the selected roles. The container is drawn
// here rather than by View's background layer because the visual surface is the
// 40 dp circle of the design baseline centred inside the 48 dp touch target, while the background
// layer always fills the whole widget rectangle. The shape still comes from
// View's shape token, so the ripple, the state overlay and the focus ring all
// agree with the painted circle.

#include <md/components/icon_button.h>

#include <md/theme/theme.h>
#include <md/tokens/elevation.h>
#include <md/tokens/state_layer.h>

#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>
#include <QSizeF>

#include <algorithm>

namespace md {

namespace {

constexpr qreal kTouchSizeDp = 48.0;
constexpr qreal kContainerSizeDp = 40.0;
constexpr qreal kOutlineDp = 1.0;

QColor withAlpha(const QColor& colour, qreal alpha)
{
    QColor result = colour;
    result.setAlphaF(static_cast<float>(std::clamp(alpha, 0.0, 1.0)));
    return result;
}

// The selected roles only apply to an enabled button: a disabled toggle keeps
// its disabled presentation, whichever state it is in.
QColor containerColourFor(IconButton::Variant variant, bool enabled, bool selected,
                          const Theme& theme)
{
    const ColorScheme& colours = theme.colours();

    if (!enabled) {
        switch (variant) {
        case IconButton::Variant::Standard:
        case IconButton::Variant::Outlined:
            return QColor(Qt::transparent);
        case IconButton::Variant::Filled:
        case IconButton::Variant::Tonal:
            return withAlpha(colours.onSurface, kDisabledContainerOpacity);
        }
    }

    if (selected) {
        switch (variant) {
        case IconButton::Variant::Standard:
            return colours.primaryContainer;
        case IconButton::Variant::Filled:
        case IconButton::Variant::Tonal:
        case IconButton::Variant::Outlined:
            return colours.primary;
        }
    }

    switch (variant) {
    case IconButton::Variant::Standard:
    case IconButton::Variant::Outlined:
        return QColor(Qt::transparent);
    case IconButton::Variant::Filled:
        return colours.primary;
    case IconButton::Variant::Tonal:
        return colours.secondaryContainer;
    }

    return QColor(Qt::transparent);
}

QColor contentColourFor(IconButton::Variant variant, bool enabled, bool selected,
                        const Theme& theme)
{
    const ColorScheme& colours = theme.colours();

    if (!enabled)
        return withAlpha(colours.onSurface, kDisabledContentOpacity);

    if (selected) {
        switch (variant) {
        case IconButton::Variant::Standard:
            return colours.onPrimaryContainer;
        case IconButton::Variant::Filled:
        case IconButton::Variant::Tonal:
        case IconButton::Variant::Outlined:
            return colours.onPrimary;
        }
    }

    switch (variant) {
    case IconButton::Variant::Standard:
    case IconButton::Variant::Outlined:
        return colours.onSurfaceVariant;
    case IconButton::Variant::Filled:
        return colours.onPrimary;
    case IconButton::Variant::Tonal:
        return colours.onSecondaryContainer;
    }

    return colours.onSurfaceVariant;
}

QColor outlineColourFor(bool enabled, const Theme& theme)
{
    const ColorScheme& colours = theme.colours();
    return enabled ? colours.outline
                   : withAlpha(colours.onSurface, kDisabledContainerOpacity);
}

} // namespace

IconButton::IconButton(QWidget* parent)
    : View(parent)
{
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundColour(QColor(Qt::transparent));
    syncAppearance();
}

IconButton::IconButton(const QIcon& icon, QWidget* parent)
    : IconButton(parent)
{
    setIcon(icon);
}

void IconButton::setIcon(const QIcon& icon)
{
    iconDrawable_.setIcon(icon);
    requestUpdate();
}

QIcon IconButton::icon() const
{
    return iconDrawable_.icon();
}

void IconButton::setIconSize(qreal dp)
{
    const qreal clamped = dp > 0.0 ? dp : 0.0;
    if (iconSizeDp_ == clamped)
        return;

    iconSizeDp_ = clamped;
    requestUpdate();
}

qreal IconButton::iconSize() const noexcept
{
    return iconSizeDp_;
}

void IconButton::setVariant(Variant variant)
{
    if (variant_ == variant)
        return;

    variant_ = variant;
    syncAppearance();
}

IconButton::Variant IconButton::variant() const noexcept
{
    return variant_;
}

void IconButton::setCheckable(bool checkable)
{
    if (checkable_ == checkable)
        return;

    checkable_ = checkable;

    // A non-checkable button cannot stay checked. Clearing the flag deselects
    // it and lets observers see the state they can no longer reach.
    if (!checkable_ && checked_)
        applyChecked(false);
}

bool IconButton::isCheckable() const noexcept
{
    return checkable_;
}

void IconButton::setChecked(bool checked)
{
    if (!checkable_ || checked_ == checked)
        return;

    applyChecked(checked);
}

bool IconButton::isChecked() const noexcept
{
    return checked_;
}

bool IconButton::isSelected() const noexcept
{
    return checkable_ && checked_;
}

void IconButton::applyChecked(bool checked)
{
    checked_ = checked;
    syncAppearance();
    emit toggled(checked_);
}

void IconButton::activate()
{
    // Toggle before clicked(): a handler of clicked() reads the new state.
    if (checkable_)
        applyChecked(!checked_);

    emit clicked();
}

void IconButton::syncAppearance()
{
    const Theme& theme = Theme::forWidget(this);

    setShape(theme.shapes().full);
    setElevation(Elevation::Level0);
    setContentColour(contentColourFor(variant_, isEnabled(), isSelected(), theme));
    requestUpdate();
}

void IconButton::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const qreal side = Theme::forWidget(this).density().dp(kTouchSizeDp);
    setMeasuredSize(QSizeF(width.resolve(side), height.resolve(side)));
}

void IconButton::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const bool enabled = isEnabled();
    const bool selected = isSelected();

    // The 40 dp visual surface, centred in the 48 dp touch target. It never
    // grows beyond 40 dp, because the container is a fixed design baseline size rather than
    // a fill.
    const qreal side =
        std::min({density.dp(kContainerSizeDp), content.width(), content.height()});
    if (side <= 0.0)
        return;

    const QRectF container(content.center().x() - side / 2.0,
                           content.center().y() - side / 2.0, side, side);

    const QColor containerColour = containerColourFor(variant_, enabled, selected, theme);
    if (containerColour.alpha() > 0) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(containerColour);
        painter.drawPath(shape().path(container));
        painter.restore();
    }

    // The outline is an unselected-state role: a selected outlined button is
    // filled with primary and has no visible contour.
    if (variant_ == Variant::Outlined && !(enabled && selected)) {
        const qreal inset = density.dp(kOutlineDp);
        QPen pen(outlineColourFor(enabled, theme), inset);
        pen.setJoinStyle(Qt::RoundJoin);

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(shape().path(container.adjusted(inset, inset, -inset, -inset)));
        painter.restore();
    }

    if (iconDrawable_.icon().isNull())
        return;

    const qreal iconSide = std::min(density.dp(iconSizeDp_), side);
    if (iconSide <= 0.0)
        return;

    const QRectF iconRect(content.center().x() - iconSide / 2.0,
                          content.center().y() - iconSide / 2.0, iconSide, iconSide);
    const QColor contentColour = contentColourFor(variant_, enabled, selected, theme);
    const QSizeF iconBox(iconSide, iconSide);

    if (iconDrawable_.size() != iconBox)
        iconDrawable_.setSize(iconBox);
    if (iconDrawable_.colour() != contentColour)
        iconDrawable_.setColour(contentColour);

    iconDrawable_.draw(painter, iconRect);
}

void IconButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        pressedInside_ = isEnabled() && QRectF(rect()).contains(event->position());

    View::mousePressEvent(event);
}

void IconButton::mouseReleaseEvent(QMouseEvent* event)
{
    const bool inside = QRectF(rect()).contains(event->position());
    const bool shouldActivate = pressedInside_ && inside && event->button() == Qt::LeftButton;
    pressedInside_ = false;

    // The base class dispatches PointerUp first so the pressed state is gone
    // before a listener reacts to clicked().
    View::mouseReleaseEvent(event);

    if (shouldActivate && isEnabled())
        activate();
}

void IconButton::keyReleaseEvent(QKeyEvent* event)
{
    const bool shouldActivate = isEnabled() && !event->isAutoRepeat()
        && (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return
            || event->key() == Qt::Key_Enter);

    View::keyReleaseEvent(event);

    if (shouldActivate)
        activate();
}

void IconButton::changeEvent(QEvent* event)
{
    View::changeEvent(event);

    if (event->type() == QEvent::EnabledChange || event->type() == QEvent::ThemeChange)
        syncAppearance();
}

} // namespace md
