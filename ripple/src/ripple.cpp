// Periodicity — ripple controller implementation (framework.md §5.11).
//
// Every ripple is one shot: it expands from the pointer position until it
// covers the component, then fades out and is removed. A hover and a press
// differ only in opacity — neither follows the pointer, because feedback should
// acknowledge an interaction once rather than paint a marker that trails it.
//
// The controller stores geometry and motion state only; the circles themselves
// are painted from the current theme, so a theme change never needs a cached
// raster. Each ripple drives one animator, created as a child of the controller:
// that lifetime link lets the public header stay free of md/animation while the
// implementation still reuses the shared animation layer. A ripple that ends or
// is dropped returns its animator to an idle pool instead of destroying it, so
// the eight-press cap also bounds the number of animators that ever exist.
//
// repaintRequested is coalesced onto the next event-loop turn and is never
// emitted from inside an animation callback, so a host slot may disable,
// suspend or destroy the controller without re-entering the state that
// notified it.

#include <md/ripple/ripple.h>

#include <md/animation/animator.h>
#include <md/core/geometry.h>

#include <QDebug>
#include <QLineF>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace md::ripple {

namespace {

// True when `rect` encloses a finite, positive area — the only case in which a
// ripple circle can produce a pixel.
bool hasDrawableArea(const QRectF& rect)
{
    return rect.width() > 0.0 && rect.height() > 0.0 && qIsFinite(rect.x())
        && qIsFinite(rect.y()) && qIsFinite(rect.width()) && qIsFinite(rect.height());
}

} // namespace

RippleSpec RippleSpec::fromTheme(const Theme& theme)
{
    RippleSpec spec;

    // Durations and curves are semantic tokens, never millisecond literals, so
    // a revision of the motion scheme changes this mapping in one place. A
    // reduced-motion scheme resolves to zero-duration specs, which the animator
    // delivers as a snap on the next event-loop turn.
    spec.expansion = theme.motion().spec(Duration::Medium2, Easing::StandardDecelerate);
    spec.fade = theme.motion().spec(Duration::Long1, Easing::Standard);

    // The opacities come from the state-layer tokens, not from a component
    // colour: the ripple is the content colour at a lower alpha.
    spec.hoverOpacity = stateLayerOpacity(ViewState::Hovered);
    spec.pressedOpacity = stateLayerOpacity(ViewState::Pressed);

    return spec;
}

class RippleController::Impl
{
public:
    enum class Kind { Hover, Press };
    enum class Phase { Expanding, Fading };

    // One circle and its animator. A run starts as an expansion; the same
    // animator later drives the fade, so a ripple never owns two timers. The
    // animator is borrowed from the controller's pool and returned when the
    // ripple goes away.
    struct Ripple
    {
        Kind kind = Kind::Hover;
        QPointF origin;
        qreal expansion = 0.0;
        qreal fade = 0.0;
        Phase phase = Phase::Expanding;
        animation::Animator* animator = nullptr;
    };

    explicit Impl(RippleController* owner)
        : owner_(owner)
        , theme_(Theme::baselineLight())
        , spec_(RippleSpec::fromTheme(theme_))
        , contentColour_(theme_.colours().onSurface)
    {
    }

    ~Impl()
    {
        // The animators are children of the controller and are destroyed by its
        // QObject destructor right after this member. Detach every connection
        // and callback first, so no late signal can reach the state that is
        // about to disappear.
        for (const auto& ripple : ripples_)
            detachAnimator(ripple->animator);
        for (animation::Animator* animator : idleAnimators_)
            detachAnimator(animator);
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    // The radius that covers the whole bounds from `origin`: the greatest
    // distance to the four corners, scaled by the spec. Recomputed rather than
    // cached so a resize takes effect on the next paint.
    [[nodiscard]] qreal maximumRadius(const QPointF& origin) const
    {
        if (!hasDrawableArea(bounds_) || !qIsFinite(spec_.maxRadiusScale)
            || !(spec_.maxRadiusScale > 0.0))
            return 0.0;

        const QPointF corners[4] = {bounds_.topLeft(), bounds_.topRight(),
                                    bounds_.bottomLeft(), bounds_.bottomRight()};

        qreal farthest = 0.0;
        for (const QPointF& corner : corners)
            farthest = std::max(farthest, QLineF(origin, corner).length());

        return farthest * spec_.maxRadiusScale;
    }

    // True when `kind` has an opacity that paints a pixel at the current spec.
    [[nodiscard]] bool specCanShow(Kind kind) const
    {
        if (!qIsFinite(spec_.maxRadiusScale) || !(spec_.maxRadiusScale > 0.0))
            return false;

        const qreal opacity =
            kind == Kind::Hover ? spec_.hoverOpacity : spec_.pressedOpacity;
        if (!qIsFinite(opacity) || !(opacity > 0.0))
            return false;

        return contentColour_.alphaF() > 0.0;
    }

    // A ripple that cannot be seen must not animate: the host would keep asking
    // for frames for a circle that paint() never draws.
    [[nodiscard]] bool canShow(Kind kind) const
    {
        return enabled_ && surfaceVisible_ && hasDrawableArea(bounds_)
            && specCanShow(kind);
    }

    [[nodiscard]] Ripple* hoverRipple() const
    {
        for (const auto& ripple : ripples_) {
            if (ripple->kind == Kind::Hover)
                return ripple.get();
        }
        return nullptr;
    }

    [[nodiscard]] Ripple* findByAnimator(const animation::Animator* animator) const
    {
        for (const auto& ripple : ripples_) {
            if (ripple->animator == animator)
                return ripple.get();
        }
        return nullptr;
    }

    [[nodiscard]] int pressCount() const
    {
        int count = 0;
        for (const auto& ripple : ripples_) {
            if (ripple->kind == Kind::Press)
                ++count;
        }
        return count;
    }

    // Queues one repaint request for the next event-loop turn. The signal is
    // deliberately not emitted synchronously: the callers include the animator
    // callbacks, and a host slot that reacts by disabling or destroying the
    // controller must not do so while the animation that reported the frame is
    // still on the stack.
    void notify()
    {
        if (notifyQueued_)
            return;

        notifyQueued_ = true;
        QMetaObject::invokeMethod(owner_, [this] {
            if (!notifyQueued_)
                return;
            notifyQueued_ = false;

            try {
                Q_EMIT owner_->repaintRequested();
            } catch (...) {
                // An observer that throws must not unwind through the event
                // loop; the host loses one repaint request, nothing else.
                qWarning() << "md::ripple::RippleController: a repaintRequested"
                              " observer threw; the exception was isolated";
            }
        }, Qt::QueuedConnection);
    }

    animation::Animator* acquireAnimator()
    {
        if (!idleAnimators_.empty()) {
            animation::Animator* animator = idleAnimators_.back();
            idleAnimators_.pop_back();
            return animator;
        }

        auto* animator = new animation::Animator(owner_);
        // One connection per animator, looking the ripple up by animator rather
        // than by a serial: a pooled animator needs no reconnection, and a late
        // signal from a dropped run simply finds no ripple.
        QObject::connect(animator, &animation::Animator::finished, owner_,
                         [this, animator] { onAnimatorFinished(animator); });
        return animator;
    }

    void releaseAnimator(animation::Animator* animator)
    {
        if (animator == nullptr)
            return;

        // stop() detaches the value callback, which captures the Ripple that is
        // going away, and leaves the animator ready for the next run.
        animator->stop();
        idleAnimators_.push_back(animator);
    }

    void detachAnimator(animation::Animator* animator)
    {
        if (animator == nullptr)
            return;

        QObject::disconnect(animator, nullptr, owner_, nullptr);
        animator->stop();
    }

    void addRipple(Kind kind, const QPointF& origin)
    {
        auto ripple = std::make_unique<Ripple>();
        ripple->kind = kind;
        ripple->origin = origin;
        ripple->animator = acquireAnimator();

        Ripple* raw = ripple.get();
        ripples_.push_back(std::move(ripple));

        startExpansion(*raw);
        notify();
    }

    void startExpansion(Ripple& ripple)
    {
        ripple.phase = Phase::Expanding;
        ripple.expansion = 0.0;
        ripple.fade = 0.0;

        // The callback only updates the geometry and queues a repaint; no host
        // code runs inside the animation, so the ripple cannot be destroyed
        // from under the function that is reporting the frame.
        Ripple* raw = &ripple;
        ripple.animator->start(spec_.expansion, [this, raw](qreal value) {
            raw->expansion = qBound(qreal(0.0), value, qreal(1.0));
            notify();
        });
    }

    void startFade(Ripple& ripple)
    {
        ripple.phase = Phase::Fading;
        ripple.fade = 0.0;

        Ripple* raw = &ripple;
        ripple.animator->start(spec_.fade, [this, raw](qreal value) {
            raw->fade = qBound(qreal(0.0), value, qreal(1.0));
            notify();
        });
    }

    void onAnimatorFinished(animation::Animator* animator)
    {
        Ripple* ripple = findByAnimator(animator);
        if (ripple == nullptr)
            return;

        // A ripple has exactly two acts: grow to fill the component, then fade
        // away. There is no holding state, which is what keeps a hover from
        // becoming a static marker that sits under the pointer.
        if (ripple->phase == Phase::Expanding) {
            startFade(*ripple);
            return;
        }

        removeRipple(ripple);
    }

    void removeRipple(Ripple* ripple)
    {
        const auto it = std::find_if(ripples_.begin(), ripples_.end(),
                                     [ripple](const auto& candidate) {
                                         return candidate.get() == ripple;
                                     });
        if (it == ripples_.end())
            return;

        animation::Animator* animator = (*it)->animator;
        ripples_.erase(it);
        releaseAnimator(animator);
        notify();
    }

    void removeOldestPress()
    {
        Ripple* oldest = nullptr;
        for (const auto& ripple : ripples_) {
            if (ripple->kind == Kind::Press) {
                oldest = ripple.get();
                break;
            }
        }

        if (oldest != nullptr)
            removeRipple(oldest);
    }

    void removeAll()
    {
        for (const auto& ripple : ripples_)
            releaseAnimator(ripple->animator);
        ripples_.clear();
    }

    // Drops every ripple the surface can no longer show: empty bounds, a hidden
    // or disabled surface, or a spec/colour that paints nothing.
    void dropInvisibleRipples()
    {
        for (auto it = ripples_.begin(); it != ripples_.end();) {
            Ripple* ripple = it->get();
            if (canShow(ripple->kind)) {
                ++it;
                continue;
            }

            animation::Animator* animator = ripple->animator;
            it = ripples_.erase(it);
            releaseAnimator(animator);
        }
    }

    void setTheme(Theme theme)
    {
        theme_ = std::move(theme);
        spec_ = RippleSpec::fromTheme(theme_);

        if (!contentColourExplicit_)
            contentColour_ = theme_.colours().onSurface;

        dropInvisibleRipples();
        notify();
    }

    void setBounds(const QRectF& bounds)
    {
        if (bounds_ == bounds)
            return;

        bounds_ = bounds;

        // A surface without a drawable area cannot show a ripple: stop the runs
        // instead of animating them invisibly until they finish.
        dropInvisibleRipples();
        notify();
    }

    void setCornerRadius(qreal radius)
    {
        const qreal clamped = radius > 0.0 ? radius : 0.0;
        if (cornerRadius_ == clamped)
            return;

        cornerRadius_ = clamped;
        notify();
    }

    void setContentColour(const QColor& colour)
    {
        contentColourExplicit_ = colour.isValid();
        contentColour_ = contentColourExplicit_ ? colour : theme_.colours().onSurface;
        dropInvisibleRipples();
        notify();
    }

    void setEnabled(bool enabled)
    {
        if (enabled_ == enabled)
            return;

        enabled_ = enabled;

        if (!enabled_)
            removeAll();

        notify();
    }

    void setSurfaceVisible(bool visible)
    {
        if (surfaceVisible_ == visible)
            return;

        surfaceVisible_ = visible;

        // A hidden surface stops animating: the runs are dropped rather than
        // paused, so showing the surface again cannot reveal a stale circle.
        if (!surfaceVisible_)
            removeAll();

        notify();
    }

    void setSpec(const RippleSpec& spec)
    {
        spec_ = spec;
        dropInvisibleRipples();
        notify();
    }

    [[nodiscard]] bool isAnimating() const
    {
        for (const auto& ripple : ripples_) {
            if (ripple->animator->isRunning())
                return true;
        }
        return false;
    }

    void pointerEntered(const QPointF& position)
    {
        if (!canShow(Kind::Hover))
            return;

        // One hover ripple at a time: a second entry replaces the first rather
        // than stacking two circles on the same surface.
        if (Ripple* existing = hoverRipple(); existing != nullptr)
            removeRipple(existing);

        addRipple(Kind::Hover, position);
    }

    void pointerMoved(const QPointF&)
    {
        // Hover feedback is one shot; the ripple does not track the pointer.
    }

    void pointerPressed(const QPointF& position)
    {
        if (!canShow(Kind::Press))
            return;

        // Bound the animator count during a stuck or repeated press. An evicted
        // ripple's animator goes back to the pool, so this loop reuses one
        // animator instead of allocating a new one per press.
        while (pressCount() >= kMaxPressRipples)
            removeOldestPress();

        addRipple(Kind::Press, position);
    }

    void pointerReleased(const QPointF&)
    {
        // Press ripples are one shot and do not wait for the release.
    }

    void pointerLeft()
    {
        // Hover ripples are one shot and do not wait for the pointer to leave.
    }

    void paint(QPainter& painter) const
    {
        if (!enabled_ || !surfaceVisible_ || ripples_.empty())
            return;
        if (!hasDrawableArea(bounds_))
            return;

        const QPainterPath clip =
            CornerRadii::uniform(cornerRadius_).toPath(bounds_);
        if (clip.isEmpty())
            return;

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setClipPath(clip, Qt::IntersectClip);
        painter.setPen(Qt::NoPen);

        for (const auto& ripple : ripples_) {
            const qreal fade = qBound(qreal(0.0), ripple->fade, qreal(1.0));
            const qreal baseOpacity =
                ripple->kind == Kind::Hover ? spec_.hoverOpacity : spec_.pressedOpacity;
            const qreal opacity = baseOpacity * (1.0 - fade);
            if (opacity <= 0.0)
                continue;

            const qreal expansion = qBound(qreal(0.0), ripple->expansion, qreal(1.0));
            const qreal circleRadius = maximumRadius(ripple->origin) * expansion;
            if (circleRadius <= 0.0)
                continue;

            QColor colour = contentColour_;
            const qreal alpha =
                qBound(qreal(0.0), static_cast<qreal>(colour.alphaF()) * opacity, qreal(1.0));
            colour.setAlphaF(static_cast<float>(alpha));

            painter.setBrush(colour);
            painter.drawEllipse(ripple->origin, circleRadius, circleRadius);
        }

        painter.restore();
    }

    RippleController* owner_;
    Theme theme_;
    RippleSpec spec_;
    QRectF bounds_;
    qreal cornerRadius_ = 0.0;
    QColor contentColour_;
    bool contentColourExplicit_ = false;
    bool enabled_ = true;
    // False while the host has suspended the surface (hidden window or tab).
    // Unlike enabled_, this says nothing about whether interaction is allowed.
    bool surfaceVisible_ = true;
    bool notifyQueued_ = false;
    std::vector<std::unique_ptr<Ripple>> ripples_;
    // Stopped animators waiting for the next ripple. The list never grows past
    // the peak number of simultaneous ripples, so the cap bounds allocations.
    std::vector<animation::Animator*> idleAnimators_;

    // More simultaneous ripples than this cannot be produced by a single
    // pointer and would only obscure the surface.
    static constexpr int kMaxPressRipples = 8;
};

RippleController::RippleController(QObject* parent)
    : QObject(parent)
    , impl_(std::make_unique<Impl>(this))
{
}

RippleController::~RippleController() = default;

void RippleController::setTheme(Theme theme)
{
    impl_->setTheme(std::move(theme));
}

void RippleController::setBounds(const QRectF& bounds)
{
    impl_->setBounds(bounds);
}

void RippleController::setCornerRadius(qreal radius)
{
    impl_->setCornerRadius(radius);
}

void RippleController::setContentColour(const QColor& colour)
{
    impl_->setContentColour(colour);
}

void RippleController::setEnabled(bool enabled)
{
    impl_->setEnabled(enabled);
}

bool RippleController::isEnabled() const noexcept
{
    return impl_->enabled_;
}

void RippleController::suspend()
{
    impl_->setSurfaceVisible(false);
}

void RippleController::setSurfaceVisible(bool visible)
{
    impl_->setSurfaceVisible(visible);
}

bool RippleController::isSurfaceVisible() const noexcept
{
    return impl_->surfaceVisible_;
}

void RippleController::setSpec(const RippleSpec& spec)
{
    impl_->setSpec(spec);
}

const RippleSpec& RippleController::spec() const noexcept
{
    return impl_->spec_;
}

void RippleController::pointerEntered(const QPointF& position)
{
    impl_->pointerEntered(position);
}

void RippleController::pointerMoved(const QPointF& position)
{
    impl_->pointerMoved(position);
}

void RippleController::pointerPressed(const QPointF& position)
{
    impl_->pointerPressed(position);
}

void RippleController::pointerReleased(const QPointF& position)
{
    impl_->pointerReleased(position);
}

void RippleController::pointerLeft()
{
    impl_->pointerLeft();
}

bool RippleController::isAnimating() const noexcept
{
    return impl_->isAnimating();
}

void RippleController::paint(QPainter& painter) const
{
    impl_->paint(painter);
}

} // namespace md::ripple
