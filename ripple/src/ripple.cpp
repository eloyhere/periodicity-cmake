// Periodicity — ripple controller implementation (framework.md §5.11).
//
// Every ripple is one shot: it expands from the pointer position until it
// covers the component, then fades out and is removed. A hover and a press
// differ only in opacity — neither follows the pointer, because feedback should
// acknowledge an interaction once rather than paint a marker that trails it.
//
// The controller stores geometry and motion state only; the circles themselves
// are painted from the current theme, so a theme change never needs a cached
// raster. Each ripple owns one animator, created as a child of the controller:
// that lifetime link lets the public header stay free of md/animation while the
// implementation still reuses the shared animation layer.

#include <md/ripple/ripple.h>

#include <md/animation/animator.h>
#include <md/core/geometry.h>

#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>

#include <algorithm>
#include <utility>
#include <vector>

namespace md::ripple {

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

    // One circle and its single animator. A run starts as an expansion; the
    // same animator later drives the fade, so a ripple never owns two timers.
    struct Ripple
    {
        Kind kind = Kind::Hover;
        QPointF origin;
        qreal expansion = 0.0;
        qreal fade = 0.0;
        Phase phase = Phase::Expanding;
        quint64 serial = 0;
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
        // The animators are children of the controller and are deleted by its
        // QObject destructor after this member; stopping them here clears every
        // callback that captures this Impl before any of them could run.
        for (const auto& ripple : ripples_)
            ripple->animator->stop();
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    // The radius that covers the whole bounds from `origin`: the greatest
    // distance to the four corners, scaled by the spec. Recomputed rather than
    // cached so a resize takes effect on the next paint.
    [[nodiscard]] qreal maximumRadius(const QPointF& origin) const
    {
        if (!bounds_.isValid() || bounds_.isEmpty() || spec_.maxRadiusScale <= 0.0)
            return 0.0;

        const QPointF corners[4] = {bounds_.topLeft(), bounds_.topRight(),
                                    bounds_.bottomLeft(), bounds_.bottomRight()};

        qreal farthest = 0.0;
        for (const QPointF& corner : corners)
            farthest = std::max(farthest, QLineF(origin, corner).length());

        return farthest * spec_.maxRadiusScale;
    }

    [[nodiscard]] Ripple* hoverRipple() const
    {
        for (const auto& ripple : ripples_) {
            if (ripple->kind == Kind::Hover)
                return ripple.get();
        }
        return nullptr;
    }

    [[nodiscard]] Ripple* findBySerial(quint64 serial) const
    {
        for (const auto& ripple : ripples_) {
            if (ripple->serial == serial)
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

    void notify() { Q_EMIT owner_->repaintRequested(); }

    Ripple* addRipple(Kind kind, const QPointF& origin)
    {
        auto ripple = std::make_unique<Ripple>();
        ripple->kind = kind;
        ripple->origin = origin;
        ripple->serial = nextSerial_++;

        auto* animator = new animation::Animator(owner_);
        ripple->animator = animator;

        const quint64 serial = ripple->serial;
        QObject::connect(animator, &animation::Animator::finished, owner_,
                         [this, serial] { onAnimatorFinished(serial); });

        Ripple* raw = ripple.get();
        ripples_.push_back(std::move(ripple));

        startExpansion(*raw);
        notify();
        return raw;
    }

    void startExpansion(Ripple& ripple)
    {
        ripple.phase = Phase::Expanding;
        ripple.expansion = 0.0;
        ripple.fade = 0.0;

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

    void onAnimatorFinished(quint64 serial)
    {
        Ripple* ripple = findBySerial(serial);
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

        // A removal can happen inside the animator's own finished() signal, so
        // the animator is stopped (which clears the callback) and destroyed on
        // a later turn rather than deleted under the emission.
        animator->stop();
        animator->deleteLater();

        ripples_.erase(it);
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
        for (const auto& ripple : ripples_) {
            ripple->animator->stop();
            ripple->animator->deleteLater();
        }
        ripples_.clear();
    }

    void setTheme(Theme theme)
    {
        theme_ = std::move(theme);
        spec_ = RippleSpec::fromTheme(theme_);

        if (!contentColourExplicit_)
            contentColour_ = theme_.colours().onSurface;

        notify();
    }

    void setBounds(const QRectF& bounds)
    {
        if (bounds_ == bounds)
            return;

        bounds_ = bounds;
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
        contentColour_ = colour;
        contentColourExplicit_ = true;
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

    void setSpec(const RippleSpec& spec)
    {
        spec_ = spec;
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
        if (!enabled_)
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
        if (!enabled_)
            return;

        // Bound the animator count during a stuck or repeated press.
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
        if (!enabled_ || ripples_.empty())
            return;
        if (!bounds_.isValid() || bounds_.isEmpty())
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
    quint64 nextSerial_ = 0;
    std::vector<std::unique_ptr<Ripple>> ripples_;

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
