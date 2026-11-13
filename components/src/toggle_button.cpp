// Periodicity — the ToggleButton component.
//
// Geometry, text layout and icon layout follow Button; the checked state and
// the self-deletion discipline follow IconButton and the audit's QPointer rule.
// The checked transition is the one part with no direct precedent in the button
// family: two token sets (unchecked and checked) are resolved per variant and
// one Animator drives a 0..1 fraction between them. The container, the content
// colour and the View state layer all read that same fraction, so the three
// never disagree during the run.
//
// The checked token sets are deliberately distinct for the four variants:
//
//   Filled    surfaceContainer / onSurfaceVariant  ->  primary / onPrimary
//   Tonal     surfaceContainer / onSurfaceVariant  ->  secondaryContainer / onSecondaryContainer
//   Outlined  transparent + outline / onSurfaceVariant -> inverseSurface / inverseOnSurface
//   Text      transparent / onSurfaceVariant       ->  transparent / primary
//
// The disabled presentation is variant-independent in the same way Button and
// IconButton define it: the onSurface role at the state-layer disabled
// opacities, and no state layer or ripple because View clears every interaction
// flag and disables the controller.

#include <md/components/toggle_button.h>

#include <md/components/font.h>
#include <md/event/pointer_event.h>
#include <md/theme/theme.h>
#include <md/tokens/elevation.h>
#include <md/tokens/state_layer.h>
#include <md/view/accessible.h>

#include <QAccessible>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointer>
#include <QRectF>
#include <QSizeF>

#include <algorithm>

namespace md {

namespace {

constexpr qreal kOutlineWidthDp = 1.0;

// The component's own geometry table. These are dp measurements, not colours or
// motion: the colours, shape, type style, state opacities and duration all come
// from the theme. Regular is Button's scale (40 dp visual inside 48 dp); Small
// and Large keep the same 8 dp step and shrink or grow the icon with the box.
struct SizeMetrics
{
    qreal visualHeightDp;
    qreal touchHeightDp;
    qreal paddingDp;
    qreal iconGapDp;
    qreal minWidthDp;
    qreal iconSizeDp;
};

SizeMetrics metricsFor(ToggleButton::ButtonSize buttonSize) noexcept
{
    switch (buttonSize) {
    case ToggleButton::ButtonSize::Small:
        return SizeMetrics{32.0, 40.0, 12.0, 8.0, 40.0, 18.0};
    case ToggleButton::ButtonSize::Regular:
        return SizeMetrics{40.0, 48.0, 16.0, 8.0, 48.0, 20.0};
    case ToggleButton::ButtonSize::Large:
        return SizeMetrics{48.0, 56.0, 20.0, 8.0, 56.0, 24.0};
    }

    return SizeMetrics{40.0, 48.0, 16.0, 8.0, 48.0, 20.0};
}

QColor withAlpha(const QColor& colour, qreal alpha)
{
    QColor result = colour;
    result.setAlphaF(static_cast<float>(std::clamp(alpha, 0.0, 1.0)));
    return result;
}

// Linear per-channel interpolation, used for the container, the contour and the
// content colour. Rounding per channel keeps the endpoint colours exact, which
// the pixel tests rely on.
QColor blend(const QColor& from, const QColor& to, qreal fraction)
{
    const qreal amount = std::clamp(fraction, 0.0, 1.0);
    const auto channel = [amount](int start, int end) {
        return static_cast<int>(qRound(start + (end - start) * amount));
    };

    return QColor(channel(from.red(), to.red()), channel(from.green(), to.green()),
                  channel(from.blue(), to.blue()), channel(from.alpha(), to.alpha()));
}

bool isActivationKey(int key) noexcept
{
    return key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter;
}

QColor containerColourFor(ToggleButton::ToggleButtonVariant variant, bool enabled,
                          qreal progress, const Theme& theme)
{
    const ColorScheme& colours = theme.colours();

    if (!enabled) {
        switch (variant) {
        case ToggleButton::ToggleButtonVariant::Filled:
        case ToggleButton::ToggleButtonVariant::Tonal:
            return withAlpha(colours.onSurface, kDisabledContainerOpacity);
        case ToggleButton::ToggleButtonVariant::Outlined:
        case ToggleButton::ToggleButtonVariant::Text:
            return QColor(Qt::transparent);
        }
    }

    switch (variant) {
    case ToggleButton::ToggleButtonVariant::Filled:
        return blend(colours.surfaceContainer, colours.primary, progress);
    case ToggleButton::ToggleButtonVariant::Tonal:
        return blend(colours.surfaceContainer, colours.secondaryContainer, progress);
    case ToggleButton::ToggleButtonVariant::Outlined:
        // The unchecked container is transparent. Interpolating the RGB of
        // transparent black would darken the fill on the way in, so the
        // transparent endpoint carries the checked role's RGB at alpha 0.
        return blend(withAlpha(colours.inverseSurface, 0.0), colours.inverseSurface,
                     progress);
    case ToggleButton::ToggleButtonVariant::Text:
        return QColor(Qt::transparent);
    }

    return QColor(Qt::transparent);
}

QColor contentColourFor(ToggleButton::ToggleButtonVariant variant, bool enabled,
                        qreal progress, const Theme& theme)
{
    const ColorScheme& colours = theme.colours();

    if (!enabled)
        return withAlpha(colours.onSurface, kDisabledContentOpacity);

    QColor checked;
    switch (variant) {
    case ToggleButton::ToggleButtonVariant::Filled:
        checked = colours.onPrimary;
        break;
    case ToggleButton::ToggleButtonVariant::Tonal:
        checked = colours.onSecondaryContainer;
        break;
    case ToggleButton::ToggleButtonVariant::Outlined:
        checked = colours.inverseOnSurface;
        break;
    case ToggleButton::ToggleButtonVariant::Text:
        checked = colours.primary;
        break;
    }

    return blend(colours.onSurfaceVariant, checked, progress);
}

QColor outlineColourFor(ToggleButton::ToggleButtonVariant variant, bool enabled,
                        qreal progress, const Theme& theme)
{
    const ColorScheme& colours = theme.colours();

    if (variant != ToggleButton::ToggleButtonVariant::Outlined)
        return QColor(Qt::transparent);

    if (!enabled)
        return withAlpha(colours.onSurface, kDisabledContainerOpacity);

    // The checked outlined button is filled with inverseSurface and has no
    // visible contour; the contour fades out over the same fraction.
    return blend(colours.outline, withAlpha(colours.outline, 0.0), progress);
}

} // namespace

ToggleButton::ToggleButton(QWidget* parent)
    : View(parent)
{
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundColour(QColor(Qt::transparent));

    // A checkable button is a checkbox in the accessibility tree: the role
    // carries the checked state and ViewAccessible reads md-checked for it.
    view::setAccessibleRole(this, QAccessible::CheckBox);
    setProperty("md-checked", false);

    syncAppearance();
}

ToggleButton::ToggleButton(const QString& text, QWidget* parent)
    : ToggleButton(parent)
{
    setText(text);
}

void ToggleButton::setText(const QString& text)
{
    if (text_ == text)
        return;

    // The label is the natural accessible name until a host pins one with
    // setAccessibleName(); after that the host's name wins.
    const bool followsLabel = accessibleName().isEmpty() || accessibleName() == text_;
    text_ = text;

    if (followsLabel)
        setAccessibleName(text_);

    updateGeometry();
    requestUpdate();
}

QString ToggleButton::text() const
{
    return text_;
}

void ToggleButton::setVariant(ToggleButtonVariant variant)
{
    if (variant_ == variant)
        return;

    variant_ = variant;
    syncAppearance();
}

ToggleButton::ToggleButtonVariant ToggleButton::variant() const noexcept
{
    return variant_;
}

void ToggleButton::setIcon(const QIcon& icon)
{
    iconDrawable_.setIcon(icon);
    updateGeometry();
    requestUpdate();
}

QIcon ToggleButton::icon() const
{
    return iconDrawable_.icon();
}

void ToggleButton::setIconSize(qreal dp)
{
    const qreal clamped = dp > 0.0 ? dp : 0.0;
    iconSizeExplicit_ = true;

    if (iconSizeDp_ == clamped)
        return;

    iconSizeDp_ = clamped;
    updateGeometry();
    requestUpdate();
}

qreal ToggleButton::iconSize() const noexcept
{
    return iconSizeDp_;
}

void ToggleButton::setButtonSize(ButtonSize value)
{
    if (buttonSize_ == value)
        return;

    buttonSize_ = value;

    if (!iconSizeExplicit_)
        iconSizeDp_ = defaultIconSizeDp();

    updateGeometry();
    requestUpdate();
}

ToggleButton::ButtonSize ToggleButton::buttonSize() const noexcept
{
    return buttonSize_;
}

void ToggleButton::setCheckable(bool checkable)
{
    if (checkable_ == checkable)
        return;

    checkable_ = checkable;

    // A non-checkable button cannot stay checked. Clearing the flag deselects
    // it and lets observers see the state they can no longer reach.
    if (!checkable_ && checked_)
        applyChecked(false);
}

bool ToggleButton::isCheckable() const noexcept
{
    return checkable_;
}

void ToggleButton::setChecked(bool checked)
{
    if (!checkable_ || checked_ == checked)
        return;

    applyChecked(checked);
}

bool ToggleButton::isChecked() const noexcept
{
    return checked_;
}

void ToggleButton::setManualAnimationClock(bool manual)
{
    animator_.setManualClock(manual);
}

bool ToggleButton::isManualAnimationClock() const noexcept
{
    return animator_.isManualClock();
}

void ToggleButton::advanceAnimation(std::chrono::milliseconds delta)
{
    animator_.advanceTimeline(delta);
}

bool ToggleButton::isAnimating() const noexcept
{
    return animator_.isRunning();
}

qreal ToggleButton::animationProgress() const noexcept
{
    return progress_;
}

void ToggleButton::applyChecked(bool checked)
{
    if (checked_ == checked)
        return;

    checked_ = checked;
    updateAccessibleChecked();
    startTransition();

    // Emitted last in this helper. A handler may delete the button; the caller
    // (activate()) is the one that has more work to do, and it holds the guard.
    emit toggled(checked_);
}

void ToggleButton::activate()
{
    // Toggle before clicked(): a clicked() handler observes the new state. The
    // guard is what makes the second emit safe when a toggled() handler
    // deletes the button.
    const QPointer<ToggleButton> self(this);

    if (checkable_)
        applyChecked(!checked_);

    if (self.isNull())
        return;

    emit clicked();
}

void ToggleButton::syncAppearance()
{
    const Theme& theme = Theme::forWidget(this);

    setShape(theme.shapes().full);
    setElevation(Elevation::Level0);
    setContentColour(contentColour());
    requestUpdate();
}

void ToggleButton::startTransition()
{
    const qreal target = checked_ ? 1.0 : 0.0;
    const MotionSpec spec = Theme::forWidget(this).motion().level(transitionMotionLevel());

    // stop() drops the previous callback; left running, it would keep writing
    // the old target into progress_ after this state change.
    animator_.stop();

    // A hidden or disabled button has no frames to show and reduced motion
    // asks for the end state directly; either way the value is exact before
    // the next paint.
    if (!isEnabled() || spec.duration.count() <= 0 || !isVisible()) {
        progress_ = target;
        applyProgress();
        return;
    }

    const qreal from = std::clamp(progress_, 0.0, 1.0);
    if (qFuzzyCompare(from + 1.0, target + 1.0)) {
        applyProgress();
        return;
    }

    animator_.start(spec, [this, from, target](qreal fraction) {
        progress_ = from + (target - from) * std::clamp(fraction, 0.0, 1.0);
        applyProgress();
    });
}

void ToggleButton::applyProgress()
{
    // setContentColour() republishes the colour to the ripple controller and
    // schedules a repaint; paintContent() resolves the container and contour
    // from the same fraction, so one call updates the whole button.
    setContentColour(contentColour());
    requestUpdate();
}

QColor ToggleButton::containerColour() const
{
    const qreal progress = isEnabled() ? std::clamp(progress_, 0.0, 1.0)
                                       : (checked_ ? 1.0 : 0.0);
    return containerColourFor(variant_, isEnabled(), progress, Theme::forWidget(this));
}

QColor ToggleButton::contentColour() const
{
    const qreal progress = isEnabled() ? std::clamp(progress_, 0.0, 1.0)
                                       : (checked_ ? 1.0 : 0.0);
    return contentColourFor(variant_, isEnabled(), progress, Theme::forWidget(this));
}

QColor ToggleButton::outlineColour() const
{
    const qreal progress = isEnabled() ? std::clamp(progress_, 0.0, 1.0)
                                       : (checked_ ? 1.0 : 0.0);
    return outlineColourFor(variant_, isEnabled(), progress, Theme::forWidget(this));
}

bool ToggleButton::hasIcon() const
{
    return !iconDrawable_.icon().isNull();
}

qreal ToggleButton::touchHeightDp() const noexcept
{
    return metricsFor(buttonSize_).touchHeightDp;
}

qreal ToggleButton::visualHeightDp() const noexcept
{
    return metricsFor(buttonSize_).visualHeightDp;
}

qreal ToggleButton::paddingDp() const noexcept
{
    return metricsFor(buttonSize_).paddingDp;
}

qreal ToggleButton::iconGapDp() const noexcept
{
    return metricsFor(buttonSize_).iconGapDp;
}

qreal ToggleButton::minWidthDp() const noexcept
{
    return metricsFor(buttonSize_).minWidthDp;
}

qreal ToggleButton::defaultIconSizeDp() const noexcept
{
    return metricsFor(buttonSize_).iconSizeDp;
}

void ToggleButton::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal touch = density.dp(touchHeightDp());

    // Icon-only buttons are square, exactly as IconButton is: the untouched
    // target is the layout size, whatever the icon and label widths are.
    if (text_.isEmpty()) {
        setMeasuredSize(QSizeF(width.resolve(touch), height.resolve(touch)));
        return;
    }

    const QFontMetricsF metrics(Font::resolve(this, theme.type().labelLarge, density));
    qreal desired = metrics.horizontalAdvance(text_) + density.dp(paddingDp()) * 2.0;
    if (hasIcon())
        desired += density.dp(iconSizeDp_) + density.dp(iconGapDp());

    desired = std::max(desired, density.dp(minWidthDp()));
    setMeasuredSize(QSizeF(width.resolve(desired), height.resolve(touch)));
}

void ToggleButton::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    const qreal visualHeight = std::min(content.height(), density.dp(visualHeightDp()));
    if (visualHeight <= 0.0)
        return;

    // A text button paints the whole content width as one pill. An icon-only
    // button paints the square visual centred inside its square target, leaving
    // the same transparent padding IconButton has.
    QRectF visual;
    if (text_.isEmpty()) {
        const qreal side =
            std::min({density.dp(visualHeightDp()), content.width(), content.height()});
        if (side <= 0.0)
            return;
        visual = QRectF(content.center().x() - side / 2.0,
                        content.center().y() - side / 2.0, side, side);
    } else {
        visual = QRectF(content.left(), content.center().y() - visualHeight / 2.0,
                        content.width(), visualHeight);
    }

    const QColor container = containerColour();
    if (container.alpha() > 0) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(container);
        painter.drawPath(shape().path(visual));
        painter.restore();
    }

    if (variant_ == ToggleButtonVariant::Outlined) {
        const QColor outline = outlineColour();
        if (outline.alpha() > 0) {
            const qreal inset = density.dp(kOutlineWidthDp);
            QPen pen(outline, inset);
            pen.setJoinStyle(Qt::RoundJoin);

            painter.save();
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(
                shape().path(visual.adjusted(inset, inset, -inset, -inset)));
            painter.restore();
        }
    }

    const QColor ink = contentColour();
    const auto drawIcon = [this, &painter](const QRectF& iconRect, const QColor& colour) {
        if (iconRect.width() <= 0.0 || iconRect.height() <= 0.0)
            return;

        const QSizeF iconBox(iconRect.width(), iconRect.height());
        if (iconDrawable_.size() != iconBox)
            iconDrawable_.setSize(iconBox);
        if (iconDrawable_.colour() != colour)
            iconDrawable_.setColour(colour);

        iconDrawable_.draw(painter, iconRect);
    };

    if (text_.isEmpty()) {
        if (!hasIcon())
            return;

        const qreal iconSide =
            std::min({density.dp(iconSizeDp_), visual.width(), visual.height()});
        if (iconSide <= 0.0)
            return;

        drawIcon(QRectF(visual.center().x() - iconSide / 2.0,
                        visual.center().y() - iconSide / 2.0, iconSide, iconSide),
                 ink);
        return;
    }

    const QFont font = Font::resolve(this, theme.type().labelLarge, density);
    const QFontMetricsF metrics(font);
    const qreal gap = density.dp(iconGapDp());
    const qreal iconSide =
        std::max(qreal(0.0), std::min(density.dp(iconSizeDp_), visual.height()));
    const bool icon = hasIcon() && iconSide > 0.0;
    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;
    const qreal startPadding = density.dp(paddingDp());
    const qreal endPadding = startPadding;

    const qreal groupAvailable =
        std::max(qreal(0.0), visual.width() - startPadding - endPadding);
    const qreal labelBudget = icon
        ? std::max(qreal(0.0), groupAvailable - iconSide - gap)
        : groupAvailable;

    QString label = text_;
    if (!label.isEmpty() && metrics.horizontalAdvance(label) > labelBudget) {
        label = labelBudget > 0.0
            ? metrics.elidedText(label, rightToLeft ? Qt::ElideLeft : Qt::ElideRight,
                                 labelBudget)
            : QString();
    }

    const qreal textWidth = label.isEmpty() ? 0.0 : metrics.horizontalAdvance(label);
    qreal groupWidth = textWidth;
    if (icon)
        groupWidth += iconSide + (textWidth > 0.0 ? gap : 0.0);

    // The group is centred inside the padded box, in logical order: icon first,
    // then the label. Mirroring the finished rectangles puts the icon on the
    // leading edge in a right-to-left button.
    const qreal groupLeft = startPadding + (groupAvailable - groupWidth) / 2.0;
    const qreal centreY = visual.center().y();
    const auto mirrorX = [&](qreal logicalX, qreal widthX) {
        return visual.left()
            + (rightToLeft ? visual.width() - logicalX - widthX : logicalX);
    };

    qreal logicalX = groupLeft;
    QRectF iconRect;
    QRectF textRect;

    if (icon) {
        iconRect = QRectF(mirrorX(logicalX, iconSide), centreY - iconSide / 2.0,
                          iconSide, iconSide);
        logicalX += iconSide + (textWidth > 0.0 ? gap : 0.0);
    }

    if (textWidth > 0.0) {
        textRect = QRectF(mirrorX(logicalX, textWidth), visual.top(), textWidth,
                          visual.height());
    }

    if (icon)
        drawIcon(iconRect, ink);

    if (!label.isEmpty()) {
        painter.save();
        painter.setFont(font);
        painter.setPen(ink);
        painter.drawText(textRect, static_cast<int>(Qt::AlignCenter), label);
        painter.restore();
    }
}

void ToggleButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        pressedInside_ = isEnabled() && QRectF(rect()).contains(event->position());

        // View keeps Hovered set on a pointer down; the pressed layer would
        // then be composited over a hover that is already stale. Drop it here
        // and restore it on a release that ends inside.
        hoverBeforePress_ = state().testFlag(ViewState::Hovered);
        if (hoverBeforePress_)
            setState(ViewState::Hovered, false);
    }

    View::mousePressEvent(event);
}

void ToggleButton::mouseReleaseEvent(QMouseEvent* event)
{
    const bool inside = QRectF(rect()).contains(event->position());
    const bool shouldActivate =
        pressedInside_ && inside && event->button() == Qt::LeftButton;
    const bool restoreHover = hoverBeforePress_;
    pressedInside_ = false;
    hoverBeforePress_ = false;

    // The base class dispatches PointerUp first so the pressed state is gone
    // before a listener reacts to clicked().
    View::mouseReleaseEvent(event);

    if (!shouldActivate || !isEnabled())
        return;

    if (restoreHover)
        setState(ViewState::Hovered, true);

    // activate() is the last statement: its signals may delete the button.
    activate();
}

void ToggleButton::keyPressEvent(QKeyEvent* event)
{
    if (isEnabled() && !event->isAutoRepeat() && isActivationKey(event->key()))
        startKeyboardFeedback();

    View::keyPressEvent(event);
}

void ToggleButton::keyReleaseEvent(QKeyEvent* event)
{
    const bool shouldActivate =
        isEnabled() && !event->isAutoRepeat() && isActivationKey(event->key());

    if (shouldActivate)
        stopKeyboardFeedback();

    View::keyReleaseEvent(event);

    if (shouldActivate)
        activate();
}

void ToggleButton::startKeyboardFeedback()
{
    const QPointF centre = QRectF(rect()).center();
    PointerEvent press(EventType::PointerDown, centre,
                       QPointF(mapToGlobal(centre.toPoint())), Qt::LeftButton,
                       Qt::LeftButton, Qt::NoModifier);

    // Target phase is the default phase of a fresh Event, and View::onEvent()
    // is the same entry point the Qt pointer bridge dispatches into. This is
    // the only public way to start the shared ripple from a key event.
    static_cast<void>(View::onEvent(press));
}

void ToggleButton::stopKeyboardFeedback()
{
    const QPointF centre = QRectF(rect()).center();
    PointerEvent release(EventType::PointerUp, centre,
                         QPointF(mapToGlobal(centre.toPoint())), Qt::LeftButton,
                         Qt::NoButton, Qt::NoModifier);

    static_cast<void>(View::onEvent(release));
}

void ToggleButton::onHidden()
{
    // A hidden button has no frames to show; the end state is what matters
    // when it is shown again.
    animator_.stop();
    pressedInside_ = false;
    hoverBeforePress_ = false;
    progress_ = checked_ ? 1.0 : 0.0;
    applyProgress();
}

void ToggleButton::onThemeChanged()
{
    // A theme can change the shape token, the colours and the reduced-motion
    // rule, so all three are re-resolved here; syncAppearance() sets the shape.
    const MotionSpec spec = Theme::forWidget(this).motion().level(transitionMotionLevel());
    if (spec.duration.count() <= 0) {
        animator_.stop();
        progress_ = checked_ ? 1.0 : 0.0;
    }

    syncAppearance();
}

void ToggleButton::onEnabledChanged(bool enabled)
{
    if (!enabled) {
        // A disabled button keeps no run and no pending press: there is no
        // feedback to animate and no release that may still activate it.
        animator_.stop();
        pressedInside_ = false;
        hoverBeforePress_ = false;
        progress_ = checked_ ? 1.0 : 0.0;
    }

    syncAppearance();
}

void ToggleButton::updateAccessibleChecked()
{
    setProperty("md-checked", checked_);

    // The framework property is read lazily by ViewAccessible, but an active
    // screen reader has to be told to query it again. StateChanged has its own
    // event class; the generic QAccessibleEvent asserts against that type.
    QAccessibleStateChangeEvent event(this, QAccessible::State());
    QAccessible::updateAccessibility(&event);
}

} // namespace md
