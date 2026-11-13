// Periodicity — the widget-side container transform.
//
// The whole animation is one QGraphicsEffect on the card. That is deliberate:
// a QWidget's children always paint after the widget's own paintEvent(), so an
// expanding layer painted by a parent can never cover its children, and a
// raised overlay that does cover them contradicts the container transform.
// Rendering the card (and therefore its children) through one effect keeps the
// children inside the moving surface, exactly like the design baseline's card transition.

#include <md/components/container_transform.h>

#include <md/animation/animator.h>
#include <md/theme/theme.h>

#include <QEvent>
#include <QGraphicsEffect>
#include <QPainter>
#include <QPolygonF>
#include <QTransform>
#include <QWidget>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <exception>

namespace md {
namespace {

// The effect that carries scale + rise + opacity for the card. Qt applies one
// effect to a widget and its whole subtree, so no second surface is needed.
class CardEffect final : public QGraphicsEffect
{
public:
    void setOpacity(qreal opacity)
    {
        opacity_ = std::clamp(opacity, qreal(0.0), qreal(1.0));
        update();
    }

    void setScale(qreal scale)
    {
        scale_ = std::max(scale, qreal(0.01));
        update();
    }

    void setRise(qreal rise)
    {
        rise_ = rise;
        update();
    }

protected:
    void draw(QPainter* painter) override
    {
        if (opacity_ <= 0.0)
            return;

        painter->save();
        painter->setOpacity(painter->opacity() * opacity_);

        const QPointF centre = sourceBoundingRect().center();
        painter->translate(0.0, rise_);
        painter->translate(centre);
        painter->scale(scale_, scale_);
        painter->translate(-centre);
        drawSource(painter);
        painter->restore();
    }

    // drawSource() is clipped to the effect's bounding rectangle by Qt, so the
    // translation and the scaled corners must be part of it or the card would
    // be cut off while it is small/risen.
    [[nodiscard]] QRectF boundingRectFor(const QRectF& source) const override
    {
        if (source.isEmpty())
            return source;

        const QPointF centre = source.center();
        QPolygonF corners;
        corners.reserve(4);
        for (const QPointF& corner :
             {source.topLeft(), source.topRight(), source.bottomRight(), source.bottomLeft()}) {
            QPointF point = centre + (corner - centre) * scale_;
            point.ry() += rise_;
            corners.append(point);
        }
        return corners.boundingRect().adjusted(-1.0, -1.0, 1.0, 1.0);
    }

private:
    qreal opacity_ = 1.0;
    qreal scale_ = 1.0;
    qreal rise_ = 0.0;
};

} // namespace

ContainerTransform::ContainerTransform(QWidget* surface, QObject* parent)
    : QObject(parent)
    , surface_(surface)
{
    animator_ = new animation::Animator(this);

    if (surface_ != nullptr) {
        surface_->installEventFilter(this);
        installEffect();
    }

    // A freshly constructed transform leaves the surface at its rest state;
    // an owner that opens with an entrance calls snapTo(false) first.
    progress_ = 1.0;
    entered_ = true;
    targetEntered_ = true;
    apply(progress_);

    connect(animator_, &animation::Animator::finished, this, [this] {
        if (surface_.isNull())
            return;

        entered_ = targetEntered_;
        progress_ = entered_ ? 1.0 : 0.0;
        apply(progress_);
        emit finished(entered_);
    });
}

ContainerTransform::~ContainerTransform()
{
    if (surface_ != nullptr && effect_ != nullptr && surface_->graphicsEffect() == effect_)
        surface_->setGraphicsEffect(nullptr);
    if (surface_ != nullptr)
        surface_->removeEventFilter(this);
}

QWidget* ContainerTransform::surface() const noexcept
{
    return surface_.data();
}

void ContainerTransform::setScaleRange(qreal from, qreal to)
{
    scaleFrom_ = std::max(from, qreal(0.01));
    scaleTo_ = std::max(to, qreal(0.01));
    apply(progress_);
}

qreal ContainerTransform::scaleFrom() const noexcept
{
    return scaleFrom_;
}

qreal ContainerTransform::scaleTo() const noexcept
{
    return scaleTo_;
}

void ContainerTransform::setRise(qreal dp)
{
    riseDp_ = std::max(dp, qreal(0.0));
    apply(progress_);
}

qreal ContainerTransform::rise() const noexcept
{
    return riseDp_;
}

void ContainerTransform::enter(bool animate)
{
    start(true, animate);
}

void ContainerTransform::exit(bool animate)
{
    start(false, animate);
}

void ContainerTransform::start(bool entered, bool animate)
{
    targetEntered_ = entered;

    if (surface_.isNull()) {
        entered_ = entered;
        progress_ = entered ? 1.0 : 0.0;
        return;
    }

    const MotionScheme& motion = Theme::forWidget(surface_).motion();
    MotionSpec spec = motion.level(entered ? MotionLevel::Level2 : MotionLevel::Level1);
    spec.easing = easingCurve(entered ? Easing::EmphasizedDecelerate
                                      : Easing::EmphasizedAccelerate);

    animator_->stop();

    const qreal target = entered ? 1.0 : 0.0;
    if (!animate || spec.duration.count() <= 0 || !surface_->isVisible()
        || qFuzzyCompare(progress_ + 1.0, target + 1.0)) {
        settle(entered);
        return;
    }

    // The Animator restarts from zero after stop(); the callback maps its
    // fresh 0..1 fraction onto the remaining interval [progress_, target], so
    // interrupting a run never makes the card jump.
    const qreal from = std::clamp(progress_, qreal(0.0), qreal(1.0));
    animator_->start(spec, [this, from, target](qreal fraction) {
        // The Animator already swallows a throwing callback; the local guard
        // keeps this helper's state consistent if the effect throws.
        try {
            if (surface_.isNull())
                return;

            const qreal amount = std::clamp(fraction, qreal(0.0), qreal(1.0));
            progress_ = from + (target - from) * amount;
            apply(progress_);
        } catch (const std::exception& error) {
            qWarning("md::ContainerTransform: callback failed: %s", error.what());
        } catch (...) {
            qWarning("md::ContainerTransform: callback failed with an unknown exception");
        }
    });
}

void ContainerTransform::snapTo(bool entered)
{
    settle(entered);
}

void ContainerTransform::settle(bool entered)
{
    animator_->stop();
    targetEntered_ = entered;
    entered_ = entered;
    progress_ = entered ? 1.0 : 0.0;
    apply(progress_);
    emit finished(entered_);
}

void ContainerTransform::stop()
{
    animator_->stop();
}

void ContainerTransform::setManualAnimationClock(bool manual)
{
    animator_->setManualClock(manual);
}

bool ContainerTransform::isManualAnimationClock() const noexcept
{
    return animator_->isManualClock();
}

void ContainerTransform::advanceAnimation(std::chrono::milliseconds delta)
{
    animator_->advanceTimeline(delta);
}

bool ContainerTransform::isEntered() const noexcept
{
    return entered_;
}

bool ContainerTransform::isRunning() const noexcept
{
    return animator_ != nullptr && animator_->isRunning();
}

qreal ContainerTransform::progress() const noexcept
{
    return progress_;
}

qreal ContainerTransform::painterOpacity() const noexcept
{
    return painterOpacity_;
}

qreal ContainerTransform::painterScale() const noexcept
{
    return painterScale_;
}

qreal ContainerTransform::painterRise() const noexcept
{
    return painterRise_;
}

void ContainerTransform::installEffect()
{
    if (surface_.isNull())
        return;

    effect_ = new CardEffect;
    surface_->setGraphicsEffect(static_cast<QGraphicsEffect*>(effect_.data()));
    syncEffect();
}

void ContainerTransform::apply(qreal progress)
{
    const qreal amount = std::clamp(progress, qreal(0.0), qreal(1.0));
    painterOpacity_ = amount;
    painterScale_ = scaleFrom_ + (scaleTo_ - scaleFrom_) * amount;

    const qreal dp = surface_.isNull()
                         ? riseDp_
                         : Theme::forWidget(surface_).density().dp(riseDp_);
    painterRise_ = dp * (1.0 - amount);

    syncEffect();
}

void ContainerTransform::syncEffect()
{
    if (surface_.isNull() || effect_.isNull())
        return;
    if (surface_->graphicsEffect() != effect_)
        return;

    auto* effect = static_cast<CardEffect*>(effect_.data());
    effect->setOpacity(painterOpacity_);
    effect->setScale(painterScale_);
    effect->setRise(painterRise_);
}

bool ContainerTransform::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == surface_ && event->type() == QEvent::Hide) {
        // A hidden card has no frames to show. Stop without emitting finished:
        // the owner (Modal) decides which end state the hidden card keeps.
        animator_->stop();
    }
    return QObject::eventFilter(watched, event);
}

} // namespace md
