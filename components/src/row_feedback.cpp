#include <md/components/row_feedback.h>

#include <md/animation/animator.h>
#include <md/core/view_state.h>
#include <md/theme/theme.h>
#include <md/tokens/motion.h>
#include <md/tokens/state_layer.h>

#include <QColor>
#include <QLineF>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <utility>

namespace md {

struct RowFeedback::Ripple
{
    int row = -1;
    QRectF rect;
    QPointF origin;
    qreal progress = 0.0;
    animation::Animator* animator = nullptr;
};

RowFeedback::RowFeedback(QWidget* context, QObject* parent)
    : QObject(parent)
    , context_(context)
{
}

RowFeedback::~RowFeedback()
{
    resetRipples();
}

void RowFeedback::pointerMoved(int row)
{
    if (row < 0)
        row = -1;
    if (row == hovered_)
        return;

    hovered_ = row;
    emit repaintRequested();
}

void RowFeedback::pointerPressed(int row, const QPointF& origin, const QRectF& rowRect)
{
    pressed_ = row;
    if (row >= 0)
        startRipple(row, origin, rowRect);
    emit repaintRequested();
}

void RowFeedback::pointerReleased()
{
    if (pressed_ < 0)
        return;

    pressed_ = -1;
    emit repaintRequested();
}

void RowFeedback::pointerLeft()
{
    if (hovered_ < 0 && pressed_ < 0)
        return;

    hovered_ = -1;
    pressed_ = -1;
    emit repaintRequested();
}

void RowFeedback::triggerRipple(int row, const QPointF& origin, const QRectF& rowRect)
{
    if (row < 0)
        return;
    startRipple(row, origin, rowRect);
}

void RowFeedback::clear()
{
    resetRipples();
    if (hovered_ < 0 && pressed_ < 0)
        return;

    hovered_ = -1;
    pressed_ = -1;
    emit repaintRequested();
}

int RowFeedback::hoveredRow() const noexcept
{
    return hovered_;
}

int RowFeedback::pressedRow() const noexcept
{
    return pressed_;
}

int RowFeedback::rippleCount() const noexcept
{
    return static_cast<int>(ripples_.size());
}

qreal RowFeedback::rippleProgress() const noexcept
{
    qreal progress = 0.0;
    for (const Ripple* ripple : ripples_)
        progress = std::max(progress, ripple->progress);
    return progress;
}

void RowFeedback::setManualAnimationClock(bool enabled)
{
    if (manualClock_ == enabled)
        return;

    manualClock_ = enabled;
    // A run belongs to one clock. Existing ripples are dropped rather than
    // migrated; the host calls this before the list is used.
    resetRipples();
}

bool RowFeedback::isManualAnimationClock() const noexcept
{
    return manualClock_;
}

void RowFeedback::advanceAnimation(std::chrono::milliseconds delta)
{
    const QList<Ripple*> current = ripples_;
    for (Ripple* ripple : current) {
        if (!ripples_.contains(ripple) || ripple->animator == nullptr)
            continue;
        ripple->animator->advanceTimeline(delta);
    }
}

void RowFeedback::paintRow(QPainter& painter, int row, const QRectF& rowRect,
                           bool highlighted, qreal cornerRadius,
                           const QColor& contentColour) const
{
    if (context_.isNull() || row < 0 || rowRect.isEmpty())
        return;

    qreal stateOpacity = 0.0;
    ViewState state = ViewState::Hovered;
    if (row == pressed_) {
        state = ViewState::Pressed;
        stateOpacity = stateLayerOpacity(state);
    } else if (row == hovered_ || highlighted) {
        state = ViewState::Hovered;
        stateOpacity = stateLayerOpacity(state);
    }

    const qreal radius = std::max(cornerRadius, qreal(0.0));

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath clip;
    clip.addRoundedRect(rowRect, radius, radius);
    painter.setClipPath(clip, Qt::IntersectClip);

    if (stateOpacity > 0.0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(applyStateLayer(contentColour, state));
        painter.drawRoundedRect(rowRect, radius, radius);
    }

    for (const Ripple* ripple : ripples_) {
        if (ripple->row != row || ripple->progress <= 0.0)
            continue;

        const QPointF origin = ripple->origin;
        const qreal farthest = std::max(
            {QLineF(origin, rowRect.topLeft()).length(),
             QLineF(origin, rowRect.topRight()).length(),
             QLineF(origin, rowRect.bottomLeft()).length(),
             QLineF(origin, rowRect.bottomRight()).length()});

        QColor ink = applyStateLayer(contentColour, ViewState::Pressed);
        ink.setAlphaF(ink.alphaF() * (1.0 - std::clamp(ripple->progress, qreal(0.0), qreal(1.0))));
        if (ink.alpha() <= 0)
            continue;

        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        painter.drawEllipse(origin, farthest * ripple->progress, farthest * ripple->progress);
    }

    painter.restore();
}

void RowFeedback::startRipple(int row, const QPointF& origin, const QRectF& rowRect)
{
    if (context_.isNull() || row < 0 || rowRect.isEmpty())
        return;

    // At most eight simultaneously visible ripples, the same cap the shared
    // ripple controller uses. Rapid clicks recycle the oldest run instead of
    // leaking an animator per click.
    while (ripples_.size() >= 8)
        retireRipple(ripples_.first());

    auto* ripple = new Ripple;
    ripple->row = row;
    ripple->rect = rowRect;
    ripple->origin = origin;

    auto* animator = new animation::Animator(this);
    animator->setManualClock(manualClock_);
    ripple->animator = animator;

    const MotionScheme& motion = Theme::forWidget(context_.data()).motion();
    MotionSpec spec = motion.level(MotionLevel::Level2);

    connect(animator, &animation::Animator::finished, this, [this, ripple] {
        QPointer<RowFeedback> self(this);
        if (self.isNull())
            return;

        if (ripple->animator != nullptr) {
            ripple->animator->deleteLater();
            ripple->animator = nullptr;
        }
        ripples_.removeOne(ripple);
        QMetaObject::invokeMethod(
            this, [ripple] { delete ripple; }, Qt::QueuedConnection);
        emit repaintRequested();
    });

    // A const callback, like ContainerTransform's, keeps the painted state
    // consistent if an observer throws while a frame is being delivered.
    animator->start(spec, [this, ripple](qreal value) {
        QPointer<RowFeedback> self(this);
        if (self.isNull() || !ripples_.contains(ripple))
            return;
        ripple->progress = std::clamp(value, qreal(0.0), qreal(1.0));
        emit repaintRequested();
    });

    ripples_.append(ripple);
    emit repaintRequested();
}

void RowFeedback::retireRipple(Ripple* ripple)
{
    if (ripple == nullptr)
        return;

    ripples_.removeOne(ripple);
    if (ripple->animator != nullptr) {
        ripple->animator->stop();
        ripple->animator->deleteLater();
        ripple->animator = nullptr;
    }
    delete ripple;
}

void RowFeedback::resetRipples()
{
    const QList<Ripple*> current = ripples_;
    ripples_.clear();
    for (Ripple* ripple : current) {
        if (ripple->animator != nullptr) {
            ripple->animator->stop();
            ripple->animator->deleteLater();
            ripple->animator = nullptr;
        }
        delete ripple;
    }
}

} // namespace md
