// Periodicity — Animation, the single-child enter/exit transition container.
//
// Animation holds exactly one child widget and gives that child a declarative
// transition when the container is shown or hidden. The container itself paints
// no content: the child is the only visible thing and remains the accessible
// node, so wrapping a view does not add a second stop to the accessibility
// tree. While a transition runs the container reports that fact through the
// dynamic property "mdAnimationRunning" (true only mid-transition); that
// property is the whole of the extra accessibility state.
//
// A transition is a list of Steps. Each Step names one Property — Opacity,
// Scale or Translate — with the value it has when the transition starts and
// the value it has when the transition ends. The list is evaluated at the
// current progress and applied to the child; a later step for the same property
// replaces an earlier one.
//
//   Opacity    one cached QGraphicsOpacityEffect on the child, setOpacity();
//   Scale      the child's geometry, scaled about its centre;
//   Translate  the child's geometry, offset vertically by dp(value)
//              (positive values move down, as a rise/fall token would).
//
// Rotation is deliberately unsupported. A live QWidget has no transform, and
// rotating a rendered texture would break input delivery, focus and the
// accessibility tree. There is therefore no Rotation property; a Step whose
// Property holds an out-of-range value is rejected with a qWarning and the
// remaining steps are still applied.
//
// Motion comes from the design baseline's motion tokens, never from literals:
//   enter — theme.motion().level(motionLevel()) with the enter easing token,
//           by default Level2 with EmphasizedDecelerate;
//   exit  — the same level with the exit easing token, by default Level2 with
//           EmphasizedAccelerate.
// setDuration() replaces the level's duration. A reduced-motion scheme, a zero
// duration or a zero-duration level snaps to the end without starting an
// Animator, so the target state is reached in the call that asked for it.
//
// Showing starts from progress 0 and animates to 1. Hiding is deferred:
// hide()/setVisible(false) starts the exit transition from the current progress,
// keeps the widget visible (so the frames are actually seen) and calls
// QWidget::setVisible(false) when progress reaches 0. The call never blocks; a
// pending-hide flag carries the request across the event loop. Showing again
// while the exit runs reverses it from the current progress, exactly as hiding
// during an enter does. Every completed transition resets the child to its
// natural geometry and full opacity, so layout, hit-testing and the next run
// start from exact values.
//
// Measurement and arrangement follow the View contract. The container measures
// its child's preferred size and forwards its own contentRect() to a View child
// through measure()/arrange(); a plain QWidget child is placed with
// setGeometry(). While a transition is in flight arrangeOverride() keeps the
// natural rectangle up to date but re-applies the current progress instead of
// resetting the child, so a layout pass can neither cancel the transition nor
// make it jump.
//
// The class is named md::Animation rather than living in the md::animation
// namespace; the animator it drives is md::animation::Animator.

#pragma once

#include <md/tokens/motion.h>
#include <md/view/view.h>

#include <QPointer>
#include <QRect>
#include <QtGlobal>

#include <chrono>
#include <optional>
#include <vector>

class QGraphicsOpacityEffect;
class QPainter;
class QWidget;

namespace md {

namespace animation {
class Animator;
}

class Animation : public View
{
    Q_OBJECT

public:
    // The properties a Step can drive. Rotation is intentionally absent: see
    // the file comment.
    enum class Property {
        Opacity,
        Scale,
        Translate,
    };
    Q_ENUM(Property)

    // One interpolation of one property. `from` is the value at progress 0 and
    // `to` the value at progress 1; Scale is a factor, Translate is in dp and
    // Opacity is a 0..1 factor (values outside the range are clamped when
    // applied).
    struct Step
    {
        Property property;
        qreal from;
        qreal to;
    };

    explicit Animation(QWidget* parent = nullptr);
    ~Animation() override;

    // Installs the single child and takes ownership of it through the widget
    // tree. Calling it with the widget already installed returns false and does
    // nothing; any other call replaces the previous child, which is hidden and
    // then deleted with deleteLater(). Passing nullptr detaches and deletes the
    // current child. Returns true when the child changed. A widget cannot be
    // its own child or an ancestor of the container; such a call warns and
    // returns false.
    bool setChild(QWidget* child);
    [[nodiscard]] QWidget* child() const;

    // The declarative transitions. The defaults are a plain fade: opacities
    // 0 → 1 on enter and 1 → 0 on exit. An empty list is allowed: the progress
    // stream still runs and finished() still fires, but the child is not
    // transformed. Changing the lists affects the next run; an in-flight run
    // keeps the list it started with.
    void setEnter(std::vector<Step> steps);
    void setExit(std::vector<Step> steps);

    // The motion level used to resolve the duration for both directions.
    // Default Level2. Changing it affects the next run.
    void setMotionLevel(MotionLevel level);
    [[nodiscard]] MotionLevel motionLevel() const noexcept;

    // Sets the easing token for both directions. Until this is called, enter
    // uses EmphasizedDecelerate and exit uses EmphasizedAccelerate. Changing it
    // affects the next run.
    void setEasing(Easing easing);

    // Overrides the duration the motion level would give. nullopt restores the
    // level; a value <= 0 makes both directions snap. Changing it affects the
    // next run.
    void setDuration(std::optional<std::chrono::milliseconds> duration);

    // 0 = collapsed, 1 = shown, with all intermediate values while a run is in
    // flight. progressChanged() is emitted for every change.
    [[nodiscard]] qreal progress() const noexcept;

    // The state the container is in or is animating towards: true after a show
    // starts (immediately, not only at progress 1), false as soon as an exit
    // starts. A widget that is hidden by its parent while an enter runs reports
    // false and progress 0.
    [[nodiscard]] bool isShown() const noexcept;

signals:
    void progressChanged(qreal progress);

    // Emitted when a transition reaches its target, including a synchronous
    // snap (reduced motion, zero duration, or a call that starts from the
    // target). Not emitted when a run is replaced, interrupted by an implicit
    // hide, or stopped because the widget was hidden by an ancestor.
    void finished(bool shown);

public:
    // QWidget::setVisible is virtual. The false path is the deferred hide: the
    // widget stays visible while the exit runs and is hidden from
    // finishTransition(). The true path shows immediately and lets showEvent()
    // start the enter; if the widget is already visible (an exit is in flight)
    // it reverses that exit instead.
    void setVisible(bool visible) override;

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeOverride(const QRectF& content) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

    // View lifecycle. onShown() starts the enter; onHidden() settles progress 0
    // when the hide happened without the deferred path (an ancestor was hidden,
    // the window closed, ...), so no timer keeps running off-screen.
    void onShown() override;
    void onHidden() override;

private:
    void startTransition(bool shown);
    void finishTransition(bool shown);
    void applyProgress(qreal value);
    void placeChildNatural();
    void removeChild(QWidget* previous);
    [[nodiscard]] QGraphicsOpacityEffect* ensureOpacityEffect();
    [[nodiscard]] bool isAnimating() const;
    [[nodiscard]] const std::vector<Step>& activeSteps() const;
    void setAnimationRunning(bool running);

    QPointer<QWidget> child_;
    QPointer<QGraphicsOpacityEffect> opacityEffect_;
    animation::Animator* animator_ = nullptr;
    QRect naturalGeometry_;

    std::vector<Step> enterSteps_;
    std::vector<Step> exitSteps_;

    MotionLevel motionLevel_ = MotionLevel::Level2;
    Easing enterEasing_ = Easing::EmphasizedDecelerate;
    Easing exitEasing_ = Easing::EmphasizedAccelerate;
    std::optional<std::chrono::milliseconds> duration_;

    qreal progress_ = 0.0;
    bool shown_ = false;
    bool pendingHide_ = false;
    bool warnedForeignEffect_ = false;
};

} // namespace md
