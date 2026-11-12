// Periodicity — Animation (the single-child transition container).
//
// One Animator drives one 0..1 stream per transition. The stream is mapped onto
// the interval between the current progress and the new target, so a reversal
// never rewinds or jumps: hiding during an enter starts from exactly the value
// the enter had reached. The Animator owns the frame timer; this file never
// constructs a QPropertyAnimation or a QTimeLine.
//
// Hiding is the interesting half. QWidget::setVisible() is virtual, so the
// false path is intercepted: the exit starts and the base implementation is
// called only from finishTransition(), once progress reaches 0. The widget
// therefore stays visible while it animates out, and the call itself returns
// immediately. A pending-hide flag carries that request; onHidden() catches the
// other way a widget disappears (an ancestor was hidden, the window closed) and
// settles the stream so no timer keeps running off screen.
//
// Applying a step is deliberately small: opacity through one cached
// QGraphicsOpacityEffect, scale by rewriting the child's geometry about its
// centre, translate by offsetting that geometry with the theme's dp scale.
// finishTransition() always restores the natural rectangle and opacity 1, so
// the next measurement, layout and hit-test are exact.

#include <md/components/animation.h>

#include <md/animation/animator.h>
#include <md/theme/theme.h>

#include <QColor>
#include <QDebug>
#include <QGraphicsEffect>
#include <QGraphicsOpacityEffect>
#include <QWidget>

#include <algorithm>
#include <exception>
#include <utility>

namespace md {

namespace {

// The rectangle the single child should occupy when no custom content rect is
// in play: the whole widget, rounded to the device-independent pixel grid.
QRect naturalRectFor(const QRectF& content)
{
    return content.toAlignedRect();
}

} // namespace

Animation::Animation(QWidget* parent)
    : View(parent)
    , animator_(new animation::Animator(this))
{
    // A transition container is a wrapper, not a surface: it adds no fill, no
    // ripple and no focus stop to the child's own rendering and accessibility.
    setBackgroundColour(QColor(Qt::transparent));
    setInteractionFeedbackEnabled(false);
    setFocusPolicy(Qt::NoFocus);

    enterSteps_.push_back(Step{Property::Opacity, 0.0, 1.0});
    exitSteps_.push_back(Step{Property::Opacity, 1.0, 0.0});

    setAnimationRunning(false);

    connect(animator_, &animation::Animator::finished, this,
            [this] { finishTransition(shown_); });
}

Animation::~Animation()
{
    if (animator_ != nullptr) {
        QObject::disconnect(animator_, nullptr, this, nullptr);
        animator_->stop();
    }
}

bool Animation::setChild(QWidget* child)
{
    if (child == child_.data())
        return false;

    if (child == this || (child != nullptr && isAncestorOf(child))) {
        qWarning("md::Animation::setChild: a container cannot hold itself or an ancestor");
        return false;
    }

    const bool wasAnimating = isAnimating();
    const bool target = pendingHide_ ? false : shown_;

    if (animator_ != nullptr)
        animator_->stop();

    if (QWidget* previous = child_.data()) {
        QObject::disconnect(previous, &QObject::destroyed, this, nullptr);

        if (opacityEffect_ != nullptr && previous->graphicsEffect() == opacityEffect_)
            previous->setGraphicsEffect(nullptr);

        // The container owns the child it replaced. Hidden first so deleteLater
        // cannot leave a stray visible surface between now and the event-loop
        // turn that destroys it.
        previous->hide();
        previous->deleteLater();
    }

    child_ = nullptr;
    opacityEffect_ = nullptr;
    warnedForeignEffect_ = false;
    naturalGeometry_ = QRect();

    child_ = child;

    if (child_ != nullptr) {
        if (child_->parentWidget() != this)
            child_->setParent(this);

        // setParent() hides the widget; a container child must be visible when
        // the container is, exactly as ViewGroup::addView() does it.
        child_->show();

        QObject::connect(child_, &QObject::destroyed, this, [this, child] {
            if (child_.data() != child)
                return;

            child_ = nullptr;
            opacityEffect_ = nullptr;
            naturalGeometry_ = QRect();
            pendingHide_ = false;
            setAnimationRunning(false);
        });

        naturalGeometry_ = naturalRectFor(contentRect());
        placeChildNatural();

        // A visible container at rest shows its child exactly; a collapsed or
        // mid-transition one applies the step values for the current progress.
        if (progress_ < 1.0)
            applyProgress(progress_);
    }

    updateGeometry();
    requestUpdate();

    if (wasAnimating)
        finishTransition(target);
    else
        pendingHide_ = false;

    return true;
}

QWidget* Animation::child() const
{
    return child_.data();
}

void Animation::setEnter(std::vector<Step> steps)
{
    enterSteps_.clear();
    enterSteps_.reserve(steps.size());

    // Reject an out-of-range Property where the caller supplied it, rather than
    // warning once per frame. The remaining steps are still installed.
    for (const Step& step : steps) {
        switch (step.property) {
        case Property::Opacity:
        case Property::Scale:
        case Property::Translate:
            enterSteps_.push_back(step);
            break;
        default:
            qWarning("md::Animation::setEnter: unsupported step property %d ignored",
                     static_cast<int>(step.property));
            break;
        }
    }
}

void Animation::setExit(std::vector<Step> steps)
{
    exitSteps_.clear();
    exitSteps_.reserve(steps.size());

    for (const Step& step : steps) {
        switch (step.property) {
        case Property::Opacity:
        case Property::Scale:
        case Property::Translate:
            exitSteps_.push_back(step);
            break;
        default:
            qWarning("md::Animation::setExit: unsupported step property %d ignored",
                     static_cast<int>(step.property));
            break;
        }
    }
}

void Animation::setMotionLevel(MotionLevel level)
{
    motionLevel_ = level;
}

MotionLevel Animation::motionLevel() const noexcept
{
    return motionLevel_;
}

void Animation::setEasing(Easing easing)
{
    enterEasing_ = easing;
    exitEasing_ = easing;
}

void Animation::setDuration(std::optional<std::chrono::milliseconds> duration)
{
    duration_ = duration;
}

qreal Animation::progress() const noexcept
{
    return progress_;
}

bool Animation::isShown() const noexcept
{
    return shown_;
}

void Animation::setVisible(bool visible)
{
    if (visible) {
        pendingHide_ = false;

        const bool alreadyVisible = isVisible();
        QWidget::setVisible(true);

        // A show during the exit reverses it from the current progress; a plain
        // first show is started by showEvent()/onShown().
        if (alreadyVisible && !shown_)
            startTransition(true);

        return;
    }

    // The exit is already represented by a pending hide; a second hide() while
    // it runs is a no-op rather than a restart.
    if (pendingHide_)
        return;

    if (!isVisible() || progress_ <= 0.0) {
        // Nothing to animate: the widget is already effectively hidden, or the
        // stream is at the collapsed end. Settle without emitting finished()
        // because no transition was started.
        if (animator_ != nullptr)
            animator_->stop();

        shown_ = false;
        pendingHide_ = false;

        if (progress_ != 0.0) {
            progress_ = 0.0;
            emit progressChanged(progress_);
        }

        placeChildNatural();
        setAnimationRunning(false);
        QWidget::setVisible(false);
        return;
    }

    pendingHide_ = true;
    startTransition(false);
}

void Animation::measureOverride(MeasureSpec width, MeasureSpec height)
{
    QSize preferred;

    if (!child_.isNull()) {
        preferred = child_->sizeHint();

        if (!preferred.isValid())
            preferred = child_->minimumSizeHint();

        if (!preferred.isValid())
            preferred = child_->size();
    }

    const qreal resolvedWidth =
        std::max(qreal(0.0), width.resolve(static_cast<qreal>(preferred.width())));
    const qreal resolvedHeight =
        std::max(qreal(0.0), height.resolve(static_cast<qreal>(preferred.height())));

    setMeasuredSize(QSizeF(resolvedWidth, resolvedHeight));
}

void Animation::arrangeOverride(const QRectF& content)
{
    naturalGeometry_ = naturalRectFor(content);

    if (child_.isNull())
        return;

    if (isAnimating()) {
        // A layout pass during a run must not reset the child to its natural
        // rectangle (that would make the transition jump); keep the new natural
        // rectangle and re-apply the current frame to it.
        applyProgress(progress_);
        return;
    }

    placeChildNatural();
}

void Animation::paintContent(QPainter& painter, const QRectF& content)
{
    // The child is the content. View's background is transparent and the
    // interaction layers are disabled, so the wrapper paints nothing of its own.
    Q_UNUSED(painter);
    Q_UNUSED(content);
}

void Animation::onShown()
{
    pendingHide_ = false;
    startTransition(true);
}

void Animation::onHidden()
{
    // A hide that did not go through setVisible() (an ancestor hid, the window
    // closed, the widget was reparented) has no frames to show. Settle the
    // stream at the collapsed end so the next show starts from a clean state.
    pendingHide_ = false;

    if (animator_ != nullptr)
        animator_->stop();

    shown_ = false;

    if (progress_ != 0.0) {
        progress_ = 0.0;
        emit progressChanged(progress_);
    }

    placeChildNatural();
    setAnimationRunning(false);
}

void Animation::startTransition(bool shown)
{
    if (animator_ != nullptr)
        animator_->stop();

    shown_ = shown;

    const qreal target = shown ? 1.0 : 0.0;
    const qreal from = std::clamp(progress_, qreal(0.0), qreal(1.0));

    if (qFuzzyCompare(from + 1.0, target + 1.0)) {
        // Already at the target. A pending hide still owes the actual hide.
        if (!shown && pendingHide_)
            finishTransition(false);
        else
            setAnimationRunning(false);
        return;
    }

    const MotionScheme motion = Theme::forWidget(this).motion();
    MotionSpec spec = motion.level(motionLevel_);
    spec.easing = easingCurve(shown ? enterEasing_ : exitEasing_);

    if (duration_.has_value()) {
        spec.duration = *duration_;
        if (spec.duration.count() < 0)
            spec.duration = std::chrono::milliseconds{0};
    }

    if (motion.reducedMotion || spec.duration.count() <= 0) {
        // The accessible / instantaneous path: the target state is reached in
        // the caller's turn and no frame timer is created.
        finishTransition(shown);
        return;
    }

    setAnimationRunning(true);

    // stop() reset the Animator to a fresh 0..1 run, so map that fraction onto
    // the remaining interval. Interrupting an enter therefore starts the exit
    // at exactly the progress the enter had reached.
    animator_->start(spec, [this, from, target](qreal fraction) {
        // The Animator already isolates a throwing callback; this local guard
        // keeps the component's own state consistent if applying a step throws.
        try {
            const qreal amount = std::clamp(fraction, qreal(0.0), qreal(1.0));
            const qreal value = from + (target - from) * amount;

            if (qFuzzyCompare(value + 1.0, progress_ + 1.0))
                return;

            progress_ = value;
            emit progressChanged(progress_);
            applyProgress(progress_);
        } catch (const std::exception& error) {
            qWarning("md::Animation: transition callback failed: %s", error.what());
        } catch (...) {
            qWarning("md::Animation: transition callback failed with an unknown exception");
        }
    });
}

void Animation::finishTransition(bool shown)
{
    if (animator_ != nullptr && animator_->isRunning())
        animator_->stop();

    shown_ = shown;

    const qreal target = shown ? 1.0 : 0.0;

    if (!qFuzzyCompare(progress_ + 1.0, target + 1.0)) {
        progress_ = target;
        emit progressChanged(progress_);
    }

    // Exact end state: natural rectangle, opacity 1, no transform left over.
    placeChildNatural();
    setAnimationRunning(false);

    const bool hideNow = !shown && pendingHide_;
    pendingHide_ = false;

    if (hideNow)
        QWidget::setVisible(false);

    emit finished(shown);
}

void Animation::applyProgress(qreal value)
{
    if (child_.isNull())
        return;

    const qreal amount = std::clamp(value, qreal(0.0), qreal(1.0));

    qreal opacity = 1.0;
    qreal scale = 1.0;
    qreal translate = 0.0;
    bool hasOpacity = false;

    for (const Step& step : activeSteps()) {
        const qreal current = step.from + (step.to - step.from) * amount;

        switch (step.property) {
        case Property::Opacity:
            opacity = current;
            hasOpacity = true;
            break;
        case Property::Scale:
            scale = current;
            break;
        case Property::Translate:
            translate = current;
            break;
        default:
            // Rotation has no representation on a live QWidget; anything else
            // is just as unsupported. Warn once per unsupported value and keep
            // the remaining steps working.
            qWarning("md::Animation: unsupported step property %d ignored",
                     static_cast<int>(step.property));
            break;
        }
    }

    if (hasOpacity) {
        if (QGraphicsOpacityEffect* effect = ensureOpacityEffect())
            effect->setOpacity(std::clamp(opacity, qreal(0.0), qreal(1.0)));
    }

    QRectF rect(naturalGeometry_);

    if (!qFuzzyCompare(scale + 1.0, 2.0)) {
        const QPointF centre = rect.center();
        const qreal scaledWidth = rect.width() * scale;
        const qreal scaledHeight = rect.height() * scale;
        rect = QRectF(centre.x() - scaledWidth / 2.0, centre.y() - scaledHeight / 2.0,
                      scaledWidth, scaledHeight);
    }

    if (!qFuzzyIsNull(translate)) {
        const qreal pixels = Theme::forWidget(this).density().dp(translate);
        rect.translate(0.0, pixels);
    }

    child_->setGeometry(rect.toAlignedRect());
}

void Animation::placeChildNatural()
{
    if (child_.isNull())
        return;

    QRect natural = naturalGeometry_;

    if (natural.isNull())
        natural = naturalRectFor(contentRect());

    try {
        if (auto* view = qobject_cast<View*>(child_.data())) {
            view->measure(MeasureSpec::exactly(static_cast<qreal>(natural.width())),
                          MeasureSpec::exactly(static_cast<qreal>(natural.height())));
            view->arrange(natural);
        } else {
            child_->setGeometry(natural);
        }

        if (opacityEffect_ != nullptr)
            opacityEffect_->setOpacity(1.0);
    } catch (const std::exception& error) {
        qWarning("md::Animation: could not reset the child: %s", error.what());
    } catch (...) {
        qWarning("md::Animation: could not reset the child with an unknown exception");
    }
}

QGraphicsOpacityEffect* Animation::ensureOpacityEffect()
{
    if (child_.isNull())
        return nullptr;

    if (opacityEffect_ != nullptr && child_->graphicsEffect() == opacityEffect_)
        return opacityEffect_;

    opacityEffect_ = nullptr;

    if (child_->graphicsEffect() != nullptr) {
        if (!warnedForeignEffect_) {
            warnedForeignEffect_ = true;
            qWarning("md::Animation: the child already has a graphics effect;"
                     " opacity steps are skipped");
        }
        return nullptr;
    }

    auto* effect = new QGraphicsOpacityEffect(child_.data());
    child_->setGraphicsEffect(effect);
    opacityEffect_ = effect;
    warnedForeignEffect_ = false;

    return effect;
}

bool Animation::isAnimating() const
{
    return animator_ != nullptr && animator_->isRunning();
}

const std::vector<Animation::Step>& Animation::activeSteps() const
{
    return shown_ ? enterSteps_ : exitSteps_;
}

void Animation::setAnimationRunning(bool running)
{
    setProperty("mdAnimationRunning", running);
}

} // namespace md
