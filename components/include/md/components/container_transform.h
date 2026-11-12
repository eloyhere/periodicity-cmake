// Periodicity — ContainerTransform, the enter/exit motion of a container
// surface such as a modal card.
//
// The transform is applied to the surface widget itself: one QGraphicsEffect
// on that widget animates opacity, scale and a small vertical rise. The
// surface's children are rendered as part of that one effect, so the card and
// everything inside it move together. Nothing is painted on top of the card
// and no sibling is raised above it — the "animation covers the children"
// failure cannot happen here, because the animation *is* the children.
//
// This is the widget-side form of the design baseline's container transform for a surface
// whose rectangle does not change: the card does not grow out of a painted
// overlay, it fades/scales/rises as one composited widget.
//
// Motion comes from the host's MotionScheme, never from literals:
//   entry — Level2 (500 ms) with the EmphasizedDecelerate curve token;
//   exit  — Level1 (250 ms) with the EmphasizedAccelerate curve token.
// Reduced motion (or a zero-duration spec) settles synchronously and never
// starts an Animator.
//
// A View-painted surface can use the same progress stream through
// painterOpacity()/painterScale()/painterRise(): those values are kept in sync
// whether or not the surface is a widget with an effect.

#pragma once

#include <QObject>
#include <QPointer>
#include <QtGlobal>

class QGraphicsEffect;
class QWidget;

namespace md {

namespace animation {
class Animator;
}

class ContainerTransform final : public QObject
{
    Q_OBJECT

public:
    // `surface` is the card/container to animate. The transform does not take
    // ownership of it and becomes inert when the surface is destroyed.
    explicit ContainerTransform(QWidget* surface, QObject* parent = nullptr);
    ~ContainerTransform() override;

    [[nodiscard]] QWidget* surface() const noexcept;

    // Scale at the collapsed end and at rest (full size). The default is
    // 0.92 → 1.0, M3's container-transform card scale.
    void setScaleRange(qreal from, qreal to);
    [[nodiscard]] qreal scaleFrom() const noexcept;
    [[nodiscard]] qreal scaleTo() const noexcept;

    // Downward displacement in dp at the collapsed end; it reaches zero as the
    // surface reaches full size. The default is 12 dp, the M3 rise.
    void setRise(qreal dp);
    [[nodiscard]] qreal rise() const noexcept;

    // Animate towards the visible/entered state, or back to the collapsed
    // state. `animate == false` applies the end state synchronously.
    void enter(bool animate = true);
    void exit(bool animate = true);

    // Apply one end state without animating. Unlike enter()/exit() this is
    // always synchronous and always emits finished().
    void snapTo(bool entered);

    // Cancels the current run without emitting finished(). The last painted
    // value is kept; a later enter()/exit() starts from it without a jump.
    void stop();

    [[nodiscard]] bool isEntered() const noexcept;
    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] qreal progress() const noexcept;

    // Painter-side mirror of the effect for View-painted surfaces.
    [[nodiscard]] qreal painterOpacity() const noexcept;
    [[nodiscard]] qreal painterScale() const noexcept;
    // Pixels, converted with the surface theme's Density.
    [[nodiscard]] qreal painterRise() const noexcept;

signals:
    // Emitted when a run reaches its end, or synchronously from a snap. Not
    // emitted by stop().
    void finished(bool entered);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void start(bool entered, bool animate);
    void settle(bool entered);
    void apply(qreal progress);
    void installEffect();
    void syncEffect();

    QPointer<QWidget> surface_;
    animation::Animator* animator_ = nullptr;
    QPointer<QObject> effect_;
    bool entered_ = true;
    bool targetEntered_ = false;
    qreal progress_ = 1.0;
    qreal scaleFrom_ = 0.92;
    qreal scaleTo_ = 1.0;
    qreal riseDp_ = 12.0;
    qreal painterOpacity_ = 1.0;
    qreal painterScale_ = 1.0;
    qreal painterRise_ = 0.0;
};

} // namespace md
