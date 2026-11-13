// Periodicity — the ToggleButton component.
//
// A checkable button: one container, a leading icon and a label, with the
// Filled, Tonal, Outlined and Text variants of the button family. The checked
// state is not a separate widget state — it is the whole point of the
// component. Every variant has a distinct checked token set, and the switch
// between the unchecked and the checked set is interpolated over the motion
// ladder by md::animation::Animator, so the container, the content colour and
// the state layer change together.
//
// Geometry follows Button: the layout size is a 48 dp touch target (the
// Regular button size) while the painted surface is the 40 dp visual centred
// inside it. Small and Large move both numbers by 8 dp. With no text the
// button is icon-only and square, like IconButton: the visual is a 40 dp
// square centred in the square target. The icon box, label type, padding, gap
// and minimum width all come from the button-size table in toggle_button.cpp;
// they are component geometry, while every colour, shape, state opacity,
// duration and type style comes from a token.
//
// Interaction follows Button and IconButton: a left-button press that starts
// and ends inside activates, and Space / Return / Enter activate the focused
// button. Activation toggles the state when checkable (toggled() reports the
// new value) and emits clicked() last when the button survives toggled(), so
// a clicked() handler observes the settled state; a toggled() handler that
// deletes the button suppresses the rest of the activation. A non-checkable
// button reports clicks without ever changing state. The checked transition is
// snap-only for a hidden, disabled or reduced-motion button.
//
// The state layer, the ripple and the keyboard focus ring come from View's
// paint pipeline. The component only chooses the container, outline and
// content colours from the theme and, on activation, tells the pipeline which
// content colour the current checked fraction resolves to. A press clears the
// hover bit so the stronger pressed layer is the only one visible; a release
// that ends inside restores the hover bit if the pointer was hovering before.
//
// Lifetime: every path that does work after a signal guards `this` with a
// QPointer, because a toggled() or clicked() handler may delete the button.
// The toggled() signal is emitted only for an actual state change.
//
// Deterministic animation: setManualAnimationClock()/advanceAnimation() expose
// the Animator's manual clock, the same seam ContainerTransform offers, so a
// test or a host that renders on its own cadence never has to sleep.

#pragma once

#include <md/animation/animator.h>
#include <md/drawable/icon_drawable.h>
#include <md/tokens/motion.h>
#include <md/view/view.h>

#include <QColor>
#include <QIcon>
#include <QString>

#include <chrono>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;

namespace md {

class ToggleButton : public View
{
    Q_OBJECT

public:
    enum class ToggleButtonVariant {
        Filled,
        Tonal,
        Outlined,
        Text,
    };

    // The button-height scale. Regular is exactly Button's scale: a 40 dp
    // painted surface centred in a 48 dp touch target. Small and Large keep the
    // same 8 dp relationship (32/40 and 48/56).
    enum class ButtonSize {
        Small,
        Regular,
        Large,
    };

    explicit ToggleButton(QWidget* parent = nullptr);
    explicit ToggleButton(const QString& text, QWidget* parent = nullptr);

    void setText(const QString& text);
    [[nodiscard]] QString text() const;

    void setVariant(ToggleButtonVariant variant);
    [[nodiscard]] ToggleButtonVariant variant() const noexcept;

    void setIcon(const QIcon& icon);
    [[nodiscard]] QIcon icon() const;

    // The icon box in dp. The default follows buttonSize() — 18 dp for Small,
    // 20 dp for Regular and 24 dp for Large — until an explicit value is set,
    // after which the explicit value survives a button-size change. Both the
    // setter and the getter speak dp; the resolved pixel value is
    // theme.density().dp(iconSize()).
    void setIconSize(qreal dp);
    [[nodiscard]] qreal iconSize() const noexcept;

    // The accessor is called buttonSize() so that QWidget::size() keeps its
    // meaning; use width() and height() for the resolved geometry.
    void setButtonSize(ButtonSize value);
    [[nodiscard]] ButtonSize buttonSize() const noexcept;

    // Turns the button into a toggle. Clearing the flag also clears a checked
    // state, because a non-checkable button cannot stay checked. Default true.
    void setCheckable(bool checkable);
    [[nodiscard]] bool isCheckable() const noexcept;

    // Sets the checked state in code. It is ignored while the button is not
    // checkable, and it emits toggled() only when the value actually changes.
    // Unlike the interaction paths, a programmatic call is still honoured while
    // the button is disabled and emits toggled() for a real change.
    void setChecked(bool checked);
    [[nodiscard]] bool isChecked() const noexcept;

    // The motion ladder rung the checked transition uses: Level1 (250 ms,
    // standard easing). Exposed so a test can advance exactly one transition.
    [[nodiscard]] static constexpr MotionLevel transitionMotionLevel() noexcept
    {
        return MotionLevel::Level1;
    }

    // Deterministic clock for the checked transition. While enabled, a state
    // change arms no timer and advanceAnimation() consumes exactly the time it
    // is given; the painted colours and the final value are the same as a
    // wall-clock run. Enable it before changing the state.
    void setManualAnimationClock(bool manual);
    [[nodiscard]] bool isManualAnimationClock() const noexcept;
    void advanceAnimation(std::chrono::milliseconds delta);

    [[nodiscard]] bool isAnimating() const noexcept;

    // The painted checked fraction: 0 is unchecked, 1 is checked.
    [[nodiscard]] qreal animationProgress() const noexcept;

signals:
    // Emitted on every activation, after the checked state has been updated
    // and after toggled() has been emitted.
    void clicked();

    // Emitted only when the checked state actually changes.
    void toggled(bool checked);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

    void onHidden() override;
    void onThemeChanged() override;
    void onEnabledChanged(bool enabled) override;

private:
    // Re-resolves the theme-dependent shape, elevation and content colour.
    void syncAppearance();

    // Applies the new checked state, starts the transition and emits toggled().
    void applyChecked(bool checked);

    // Emits toggled() (when checkable) and then clicked(), with a self-guard
    // between the two because a toggled() handler may delete the button.
    void activate();

    // Starts the Animator towards the state checked_ reports. A hidden,
    // disabled or reduced-motion button snaps to the end state.
    void startTransition();

    // Republishes the interpolated content colour (the ripple and the state
    // layer both read it) and schedules a repaint.
    void applyProgress();

    [[nodiscard]] QColor containerColour() const;
    [[nodiscard]] QColor contentColour() const;
    [[nodiscard]] QColor outlineColour() const;
    [[nodiscard]] bool hasIcon() const;

    [[nodiscard]] qreal touchHeightDp() const noexcept;
    [[nodiscard]] qreal visualHeightDp() const noexcept;
    [[nodiscard]] qreal paddingDp() const noexcept;
    [[nodiscard]] qreal iconGapDp() const noexcept;
    [[nodiscard]] qreal minWidthDp() const noexcept;
    [[nodiscard]] qreal defaultIconSizeDp() const noexcept;

    // Keyboard activation gets the same ripple as a pointer press. View's
    // ripple only listens to pointer events, so these two helpers dispatch a
    // target-phase PointerDown/PointerUp at the centre of the button through
    // the same onEvent() pipeline the mouse uses.
    void startKeyboardFeedback();
    void stopKeyboardFeedback();

    void updateAccessibleChecked();

    QString text_;
    ToggleButtonVariant variant_ = ToggleButtonVariant::Filled;
    ButtonSize buttonSize_ = ButtonSize::Regular;
    drawable::IconDrawable iconDrawable_;
    qreal iconSizeDp_ = 20.0;
    bool iconSizeExplicit_ = false;
    bool checkable_ = true;
    bool checked_ = false;
    bool pressedInside_ = false;
    bool hoverBeforePress_ = false;
    // Painted checked fraction, 0..1, advanced by animator_.
    qreal progress_ = 0.0;
    animation::Animator animator_;
};

} // namespace md
