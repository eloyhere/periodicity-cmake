// Periodicity — the Carousel component (framework.md §5.9).
//
// The carousel is an arrangement, not a stack of pixmaps. Every slide stays a
// live child View; the transition only interpolates the horizontal offset that
// arrangeChildren() applies, so a moving slide keeps painting itself, keeps its
// focus chain and keeps its accessibility subtree. The slide area and the
// indicator band are disjoint, which is why the dots can be painted from
// paintContent() — the stage that runs before the children — without ever being
// hidden behind a slide.
//
// Auto play is a plain QTimer; it advances the carousel and resynchronises
// itself when the slide count or the interval changes.
//
// The drag is the same arrangement, not a second one: a gesture only adds a
// pixel offset to every slide and decides which of the two neighbouring slides
// is exposed. A release either folds that offset into the index through the
// ordinary transition timer or springs it back to zero, so clicking a button,
// pressing an arrow key and finishing a drag all end in the same resting state.

#include <md/components/carousel.h>

#include <md/theme/theme.h>

#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace md {
namespace {

constexpr qreal kIndicatorBandDp = 28.0;
constexpr qreal kIndicatorCellDp = 16.0;
constexpr qreal kIndicatorGapDp = 4.0;
constexpr qreal kIndicatorActiveDp = 8.0;
constexpr qreal kIndicatorIdleDp = 6.0;
constexpr int kMinTimerIntervalMs = 16;

// A press becomes a drag only after this much horizontal travel, so a click
// that wobbles by a pixel or two still reaches the slide's own controls.
constexpr qreal kDragStartDp = 4.0;
// Releasing past this fraction of a page advances, even without a flick.
constexpr qreal kSnapFraction = 0.35;
// A flick faster than this (in pixels per millisecond) advances regardless of
// distance.
constexpr qreal kVelocityPxPerMs = 0.35;
// The elastic asymptote at an end the carousel cannot travel past.
constexpr qreal kDragLimitDp = 96.0;

} // namespace

Carousel::Carousel(QWidget* parent)
    : ViewGroup(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    // A carousel is an edge-to-edge container: its slides are arranged across
    // the whole content rectangle, so a rounded container shape was only ever
    // visible under the indicator band and was covered by square slide corners
    // everywhere else — a half-rounded surface. The rounding, if wanted, belongs
    // to the slide, not to the container.
    setShape(Shape::none());
    setAccessibleName(tr("Carousel"));

    transitionTimer_ = new QTimer(this);
    transitionTimer_->setObjectName(QStringLiteral("md.components.Carousel.transition"));
    transitionTimer_->setInterval(kMinTimerIntervalMs);
    connect(transitionTimer_, &QTimer::timeout, this, [this] { tickTransition(); });

    autoTimer_ = new QTimer(this);
    autoTimer_->setObjectName(QStringLiteral("md.components.Carousel.autoPlay"));
    autoTimer_->setInterval(intervalMs_);
    connect(autoTimer_, &QTimer::timeout, this, [this] { next(); });
}

int Carousel::wrapIndex(int index) const
{
    const int count = static_cast<int>(slides_.size());
    if (count <= 0)
        return 0;

    int result = index % count;
    if (result < 0)
        result += count;

    return result;
}

int Carousel::slideCount() const
{
    return static_cast<int>(slides_.size());
}

QList<View*> Carousel::slides() const
{
    return slides_;
}

void Carousel::setSlides(const QList<View*>& slides)
{
    clearSlides();

    for (View* slide : slides) {
        if (slide != nullptr)
            addSlide(slide);
    }
}

void Carousel::addSlide(View* slide)
{
    if (slide == nullptr || slides_.contains(slide))
        return;

    // addView() reparents the slide and, when it was already a child of this
    // group, disconnects its destroyed() handlers on the way — so the pruning
    // connection is installed after the child is in place.
    addView(slide);
    connect(slide, &QObject::destroyed, this, [this, slide] { pruneSlide(slide); });

    // A slide is a widget, so it receives the press itself and the carousel
    // would never see the gesture. The filter watches the slide without
    // claiming the press: a click still reaches the slide, and only a move
    // past the slop turns into a drag owned by the carousel.
    slide->installEventFilter(this);
    slides_.append(slide);

    if (current_ >= slideCount())
        current_ = std::max(0, slideCount() - 1);
    previous_ = std::min(previous_, current_);

    syncAutoPlay();
    syncVisibility();
    arrangeChildren(contentRect());
    requestUpdate();
}

void Carousel::removeSlide(View* slide)
{
    const int index = static_cast<int>(slides_.indexOf(slide));
    if (index < 0)
        return;

    // Drop the bookkeeping entry first: removeView() disconnects every
    // destroyed() handler for the widget, including the one installed above.
    slide->removeEventFilter(this);
    if (dragSlide_ == slide) {
        dragPressed_ = false;
        dragSlide_.clear();
    }
    slides_.removeAt(index);
    removeView(slide);

    if (index < current_)
        --current_;
    if (index < previous_)
        --previous_;

    const int last = std::max(0, slideCount() - 1);
    current_ = std::clamp(current_, 0, last);
    previous_ = std::clamp(previous_, 0, last);
    stopTransitionTimer();
    resetDrag();
    dragging_ = false;
    snapping_ = false;
    dragFromOffset_ = 0.0;
    dragOffset_ = 0.0;
    dragNeighbor_ = -1;
    snapLinear_ = 0.0;
    progress_ = 1.0;

    syncAutoPlay();
    syncVisibility();
    arrangeChildren(contentRect());
    requestUpdate();
}

void Carousel::clearSlides()
{
    const QList<View*> removed = slides_;
    slides_.clear();

    for (View* slide : removed) {
        if (slide == nullptr)
            continue;

        slide->removeEventFilter(this);
        removeView(slide);
    }

    dragPressed_ = false;
    dragSlide_.clear();

    current_ = 0;
    previous_ = 0;
    stopTransitionTimer();
    resetDrag();
    dragging_ = false;
    snapping_ = false;
    dragFromOffset_ = 0.0;
    dragOffset_ = 0.0;
    dragNeighbor_ = -1;
    snapLinear_ = 0.0;
    progress_ = 1.0;

    syncAutoPlay();
    arrangeChildren(contentRect());
    requestUpdate();
}

void Carousel::pruneSlide(View* slide)
{
    const int index = static_cast<int>(slides_.indexOf(slide));
    if (index < 0)
        return;

    if (dragSlide_ == slide) {
        dragPressed_ = false;
        dragSlide_.clear();
    }

    slides_.removeAt(index);

    if (index < current_)
        --current_;
    if (index < previous_)
        --previous_;

    const int last = std::max(0, slideCount() - 1);
    current_ = std::clamp(current_, 0, last);
    previous_ = std::clamp(previous_, 0, last);
    stopTransitionTimer();
    resetDrag();
    dragging_ = false;
    snapping_ = false;
    dragFromOffset_ = 0.0;
    dragOffset_ = 0.0;
    dragNeighbor_ = -1;
    snapLinear_ = 0.0;
    progress_ = 1.0;

    syncAutoPlay();
    requestUpdate();
}

void Carousel::setCurrentIndex(int index)
{
    const int count = slideCount();
    if (count <= 0)
        return;

    // A programmatic change interrupts a gesture: the caller asked for a
    // slide, so the drag's half-finished arrangement is settled first.
    if (dragging_ || snapping_)
        finishAnimation();

    const int next = loop_ ? wrapIndex(index) : std::clamp(index, 0, count - 1);
    if (next == current_)
        return;

    const int from = current_;
    current_ = next;
    previous_ = from;
    progress_ = 0.0;

    syncVisibility();
    emit currentChanged(current_);

    const int duration = transitionDuration();
    if (duration <= 0) {
        finishTransition();
        syncAutoPlay();
        return;
    }

    transitionTimer_->setInterval(std::max(kMinTimerIntervalMs, duration / 30));
    transitionTimer_->start();
    arrangeChildren(contentRect());
    requestUpdate();

    // Arriving at the last slide is the end of an auto-play run that does not
    // loop; syncAutoPlay() stops the timer there.
    syncAutoPlay();
}

void Carousel::next()
{
    if (slideCount() <= 1)
        return;

    if (!loop_ && current_ >= slideCount() - 1)
        return;

    setCurrentIndex(current_ + 1);
}

void Carousel::previous()
{
    if (slideCount() <= 1)
        return;

    if (!loop_ && current_ <= 0)
        return;

    setCurrentIndex(current_ - 1);
}

void Carousel::setLoop(bool loop)
{
    if (loop_ == loop)
        return;

    loop_ = loop;
    // Turning looping off at the last slide leaves auto play with nowhere to
    // go; turning it back on has to let auto play resume.
    syncAutoPlay();
    requestUpdate();
}

void Carousel::setAutoPlay(bool enabled)
{
    if (autoPlay_ == enabled)
        return;

    autoPlay_ = enabled;
    syncAutoPlay();
}

void Carousel::setInterval(int milliseconds)
{
    if (milliseconds <= 0 || intervalMs_ == milliseconds)
        return;

    intervalMs_ = milliseconds;
    syncAutoPlay();
}

void Carousel::setDuration(int milliseconds)
{
    const int next = std::max(0, milliseconds);
    if (durationMs_ == next)
        return;

    durationMs_ = next;
}

bool Carousel::isAnimating() const
{
    return transitionTimer_ != nullptr && transitionTimer_->isActive();
}

void Carousel::finishTransition()
{
    finishAnimation();
}

void Carousel::finishAnimation()
{
    const bool wasDragging = dragging_;
    const bool wasSnapping = snapping_;

    stopTransitionTimer();

    dragging_ = false;
    snapping_ = false;
    dragPressed_ = false;
    dragSlide_.clear();
    dragFromOffset_ = 0.0;
    dragOffset_ = 0.0;
    dragVelocity_ = 0.0;
    dragLastX_ = 0.0;
    dragLastTimestamp_ = 0;
    dragNeighbor_ = -1;
    snapLinear_ = 0.0;

    progress_ = 1.0;
    previous_ = current_;
    syncVisibility();
    arrangeChildren(contentRect());
    requestUpdate();

    if (wasDragging)
        emit dragStateChanged(false);

    // The gesture held auto play off; now that the slides are at rest it may
    // resume, unless the current slide is the last one of a non-looping set.
    if (wasDragging || wasSnapping)
        syncAutoPlay();
}

void Carousel::stopTransitionTimer()
{
    if (transitionTimer_ != nullptr)
        transitionTimer_->stop();
}

int Carousel::transitionDuration() const
{
    if (Theme::forWidget(this).motion().reducedMotion)
        return 0;

    return std::max(0, durationMs_);
}

qreal Carousel::transitionDelta() const
{
    int delta = current_ - previous_;

    // Wrapping must travel one slide, not the whole set: 4 -> 0 is one step
    // forward, not three steps back. Only an even cycle can be ambiguous, and
    // there the signed index difference is already the shortest distance.
    const int count = slideCount();
    if (loop_ && count > 1) {
        if (delta > count / 2)
            delta -= count;
        else if (delta < -(count / 2))
            delta += count;
    }

    return qreal(delta);
}

bool Carousel::canAutoAdvance() const
{
    return slideCount() > 1 && (loop_ || current_ < slideCount() - 1);
}

void Carousel::tickTransition()
{
    // A drag settle runs on the theme's medium1 spec and folds the pixel
    // offset into the page index as the eased progress reaches one. An
    // ordinary transition keeps its own configured duration and linear step,
    // exactly as before the gesture existed.
    const int duration = snapping_ ? snapDurationMs() : transitionDuration();
    if (duration <= 0) {
        finishAnimation();
        return;
    }

    const qreal step = qreal(transitionTimer_->interval()) / qreal(duration);
    if (snapping_) {
        snapLinear_ = std::min(1.0, snapLinear_ + step);
        const qreal eased = snapEasedProgress(snapLinear_);
        progress_ = eased;
        dragOffset_ = dragFromOffset_ * (1.0 - eased);
        if (snapLinear_ >= 1.0) {
            finishAnimation();
            return;
        }
    } else {
        progress_ += step;
        if (progress_ >= 1.0) {
            finishAnimation();
            return;
        }
    }

    arrangeChildren(contentRect());
    requestUpdate();
}

void Carousel::syncVisibility()
{
    const bool animating = progress_ < 1.0;
    const bool dragVisible = dragNeighbor_ >= 0 && (dragging_ || snapping_);

    for (int index = 0; index < slideCount(); ++index) {
        View* slide = slides_.at(index);
        if (slide == nullptr)
            continue;

        // At most two slides ever exist on screen: the current one and the
        // neighbour a gesture or a transition is exposing. Everything else is
        // hidden, which is what keeps a carousel of heavy slides cheap.
        const bool wanted = index == current_
            || (animating && index == previous_)
            || (dragVisible && index == dragNeighbor_);
        if (wanted && slide->isHidden())
            slide->setVisible(true);
        else if (!wanted && !slide->isHidden())
            slide->setVisible(false);
    }
}

void Carousel::syncAutoPlay()
{
    if (autoTimer_ == nullptr)
        return;

    // LifecycleState::Hidden is "was shown, now hidden": a carousel in that
    // state must not advance. A never-shown carousel keeps its timer, because
    // auto play is a property of the component, not of the window. A gesture
    // holds the timer off from the moment the drag starts until the slides
    // have settled.
    const bool allowed = autoPlay_ && canAutoAdvance() && !dragging_ && !snapping_
                         && lifecycleState() != LifecycleState::Hidden;

    if (!allowed) {
        autoTimer_->stop();
        return;
    }

    if (autoTimer_->interval() != intervalMs_)
        autoTimer_->setInterval(intervalMs_);

    if (!autoTimer_->isActive())
        autoTimer_->start();
}

bool Carousel::indicatorsVisible() const
{
    return slideCount() > 1;
}

QRectF Carousel::indicatorBarRect() const
{
    if (!indicatorsVisible())
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const QRectF area = contentRect();
    const qreal height = std::min(density.dp(kIndicatorBandDp), area.height());

    return QRectF(area.left(), area.bottom() - height, std::max<qreal>(0.0, area.width()),
                  std::max<qreal>(0.0, height));
}

QRectF Carousel::slideArea() const
{
    const QRectF area = contentRect();
    const QRectF bar = indicatorBarRect();
    if (bar.isEmpty())
        return area;

    return QRectF(area.left(), area.top(), std::max<qreal>(0.0, area.width()),
                  std::max<qreal>(0.0, bar.top() - area.top()));
}

QRectF Carousel::indicatorRect(int index) const
{
    const QRectF bar = indicatorBarRect();
    if (bar.isEmpty() || index < 0 || index >= slideCount())
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const qreal cell = density.dp(kIndicatorCellDp);
    const qreal gap = density.dp(kIndicatorGapDp);
    const qreal total = slideCount() * cell + (slideCount() - 1) * gap;
    const qreal left = bar.center().x() - total / 2.0 + index * (cell + gap);

    return QRectF(left, bar.top(), cell, bar.height());
}

void Carousel::handleActivation(int index)
{
    if (index < 0 || index >= slideCount())
        return;

    setCurrentIndex(index);
}

void Carousel::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    setMeasuredSize(QSizeF(width.resolve(density.dp(360.0)), height.resolve(density.dp(240.0))));
}

void Carousel::arrangeChildren(const QRectF& content)
{
    Q_UNUSED(content);

    const QRectF area = slideArea();
    const qreal width = area.width();
    const qreal delta = transitionDelta();
    const qreal shift = progress_ * delta;

    for (int index = 0; index < slideCount(); ++index) {
        View* slide = slides_.at(index);
        if (slide == nullptr)
            continue;

        // Every slide has a stable slot in index space, except the incoming
        // one: on a loop wrap it sits one step from the outgoing slide rather
        // than at its raw index, which would be on the far side of the set.
        qreal slot = qreal(index) - qreal(previous_);
        if (index == current_)
            slot = delta;

        const qreal offset = slot - shift;
        const qreal x = area.left() + offset * width + dragOffset_;
        slide->arrange(QRect(qRound(x), qRound(area.top()), qRound(width),
                             qRound(area.height())));
    }
}

void Carousel::paintContent(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);

    const QRectF bar = indicatorBarRect();
    if (bar.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();
    const Density& density = theme.density();
    const qreal baseOpacity = isEnabled() ? 1.0 : 0.38;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setOpacity(baseOpacity);

    for (int index = 0; index < slideCount(); ++index) {
        const QRectF cell = indicatorRect(index);
        if (cell.isEmpty())
            continue;

        const bool active = index == current_;
        const qreal diameter = density.dp(active ? kIndicatorActiveDp : kIndicatorIdleDp);
        const QRectF dot(cell.center().x() - diameter / 2.0, cell.center().y() - diameter / 2.0,
                         diameter, diameter);

        painter.setPen(Qt::NoPen);
        painter.setBrush(active ? colours.primary : colours.outlineVariant);
        painter.drawEllipse(dot);
    }

    painter.restore();
}

// ---------------------------------------------------------------------------
// Drag gesture
// ---------------------------------------------------------------------------

bool Carousel::draggingEnabled() const
{
    return isEnabled() && slideCount() > 1 && !slideArea().isEmpty();
}

bool Carousel::canDragForward() const
{
    return slideCount() > 1 && (loop_ || current_ < slideCount() - 1);
}

bool Carousel::canDragBackward() const
{
    return slideCount() > 1 && (loop_ || current_ > 0);
}

qreal Carousel::dampedDragOffset(qreal raw) const
{
    // Past the first or last slide (when loop is off) the travel asymptotes:
    // the gesture answers, but never exposes a slide that does not exist.
    const qreal width = slideArea().width();
    const qreal limit = std::max(Theme::forWidget(this).density().dp(kDragLimitDp),
                                 width * 0.5);

    if (raw < 0.0 && !canDragForward())
        return -(limit * -raw) / (limit + -raw);

    if (raw > 0.0 && !canDragBackward())
        return (limit * raw) / (limit + raw);

    return raw;
}

int Carousel::neighborIndex(qreal offset) const
{
    if (offset < 0.0)
        return canDragForward() ? wrapIndex(current_ + 1) : -1;

    if (offset > 0.0)
        return canDragBackward() ? wrapIndex(current_ - 1) : -1;

    return -1;
}

int Carousel::snapDurationMs() const
{
    return static_cast<int>(Theme::forWidget(this).motion().medium1().duration.count());
}

qreal Carousel::snapEasedProgress(qreal linear) const
{
    const QEasingCurve curve =
        Theme::forWidget(this).motion().medium1().easing.toEasingCurve();
    return curve.valueForProgress(std::clamp(linear, qreal(0.0), qreal(1.0)));
}

void Carousel::beginDragPress(const QPointF& position, quint64 timestamp)
{
    if (!draggingEnabled())
        return;

    dragPressed_ = true;
    dragPressPosition_ = position;
    dragLastX_ = position.x();
    dragLastTimestamp_ = timestamp;
    dragVelocity_ = 0.0;
}

void Carousel::beginDrag()
{
    if (dragging_)
        return;

    dragging_ = true;
    dragOffset_ = 0.0;
    dragNeighbor_ = -1;

    if (autoTimer_ != nullptr)
        autoTimer_->stop();

    // The slide accepted the press and set its own pressed state. Once the
    // gesture belongs to the carousel, the slide is told the pointer left so
    // it does not stay highlighted for a click that will never arrive.
    if (dragSlide_ != nullptr) {
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(dragSlide_.data(), &leave);
    }

    emit dragStateChanged(true);
}

void Carousel::updateDrag(const QPointF& position, quint64 timestamp)
{
    const qreal width = slideArea().width();
    if (width <= 0.0)
        return;

    const qreal raw = std::clamp(position.x() - dragPressPosition_.x(), -width, width);
    dragOffset_ = dampedDragOffset(raw);

    const int neighbor = neighborIndex(dragOffset_);
    if (neighbor >= 0)
        dragNeighbor_ = neighbor;

    const qreal elapsed = qreal(timestamp) - qreal(dragLastTimestamp_);
    if (elapsed > 0.0)
        dragVelocity_ = (position.x() - dragLastX_) / elapsed;
    dragLastX_ = position.x();
    dragLastTimestamp_ = timestamp;

    syncVisibility();
    arrangeChildren(contentRect());
    requestUpdate();
}

void Carousel::endDrag(const QPointF& position, quint64 timestamp)
{
    if (!dragging_) {
        dragPressed_ = false;
        dragSlide_.clear();
        return;
    }

    updateDrag(position, timestamp);

    dragging_ = false;
    emit dragStateChanged(false);

    const qreal width = slideArea().width();
    const qreal fraction = width > 0.0 ? dragOffset_ / width : 0.0;
    const bool forward = fraction < -kSnapFraction || dragVelocity_ < -kVelocityPxPerMs;
    const bool backward = fraction > kSnapFraction || dragVelocity_ > kVelocityPxPerMs;

    int target = current_;
    if (forward && !backward && canDragForward())
        target = wrapIndex(current_ + 1);
    else if (backward && !forward && canDragBackward())
        target = wrapIndex(current_ - 1);

    dragPressed_ = false;
    dragSlide_.clear();
    dragVelocity_ = 0.0;

    beginSnap(target);
}

void Carousel::beginSnap(int target)
{
    const qreal start = dragOffset_;
    const bool changesPage =
        target >= 0 && target < slideCount() && target != current_;

    previous_ = current_;
    if (changesPage) {
        // The new index is committed before the animation runs, exactly like
        // setCurrentIndex(): listeners update their indicator immediately and
        // the arrangement catches up.
        current_ = target;
        emit currentChanged(current_);
    }
    progress_ = 0.0;

    dragFromOffset_ = start;
    snapping_ = true;
    snapLinear_ = 0.0;

    const int duration = snapDurationMs();
    if (duration <= 0) {
        finishAnimation();
        return;
    }

    transitionTimer_->setInterval(std::max(kMinTimerIntervalMs, duration / 30));
    transitionTimer_->start();
    syncVisibility();
    arrangeChildren(contentRect());
    requestUpdate();
}

void Carousel::resetDrag()
{
    dragPressed_ = false;
    dragSlide_.clear();
    dragVelocity_ = 0.0;
    dragLastTimestamp_ = 0;
}

void Carousel::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        const QPointF position = event->position();
        pressed_ = position;

        // The indicator band is a click target: pressing it must not start a
        // drag, or a dot click would become a one-pixel swipe.
        if (isEnabled() && slideArea().contains(position)
            && !indicatorBarRect().contains(position)) {
            // A gesture starts from a settled arrangement: an in-flight
            // transition would otherwise fight the pointer for the offset.
            if (isAnimating() || dragging_ || snapping_)
                finishAnimation();
            beginDragPress(position, event->timestamp());
        }
    }

    ViewGroup::mousePressEvent(event);
}

void Carousel::mouseMoveEvent(QMouseEvent* event)
{
    ViewGroup::mouseMoveEvent(event);

    if (!dragPressed_ || !(event->buttons() & Qt::LeftButton))
        return;

    const QPointF position = event->position();
    const qreal dx = position.x() - dragPressPosition_.x();
    const qreal dy = position.y() - dragPressPosition_.y();

    if (!dragging_) {
        const qreal slop = Theme::forWidget(this).density().dp(kDragStartDp);
        if (std::abs(dx) < slop || std::abs(dx) <= std::abs(dy))
            return;
        beginDrag();
    }

    updateDrag(position, event->timestamp());
    event->accept();
}

void Carousel::mouseReleaseEvent(QMouseEvent* event)
{
    const QPointF press = pressed_;
    pressed_ = QPointF(-1.0, -1.0);
    ViewGroup::mouseReleaseEvent(event);

    if (event->button() != Qt::LeftButton) {
        resetDrag();
        return;
    }

    if (dragging_) {
        endDrag(event->position(), event->timestamp());
        return;
    }

    resetDrag();

    if (!isEnabled() || press.x() < 0.0)
        return;

    const QPointF release = event->position();
    for (int index = 0; index < slideCount(); ++index) {
        const QRectF cell = indicatorRect(index);
        if (cell.isEmpty() || !cell.contains(press) || !cell.contains(release))
            continue;

        handleActivation(index);
        return;
    }
}

bool Carousel::eventFilter(QObject* watched, QEvent* event)
{
    auto* slide = qobject_cast<View*>(watched);
    const bool ours = slide != nullptr && slides_.contains(slide) && isEnabled();

    if (!ours)
        return ViewGroup::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton)
            break;

        const QPointF position = mapFromGlobal(mouse->globalPosition());
        if (!slideArea().contains(position) || indicatorBarRect().contains(position))
            break;

        if (isAnimating() || dragging_ || snapping_)
            finishAnimation();

        dragSlide_ = slide;
        beginDragPress(position, mouse->timestamp());
        // The press still reaches the slide, so a small movement remains a
        // click on whatever the slide contains.
        break;
    }

    case QEvent::MouseMove: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (!dragPressed_ || !(mouse->buttons() & Qt::LeftButton))
            break;

        const QPointF position = mapFromGlobal(mouse->globalPosition());
        const qreal dx = position.x() - dragPressPosition_.x();
        const qreal dy = position.y() - dragPressPosition_.y();

        if (!dragging_) {
            const qreal slop = Theme::forWidget(this).density().dp(kDragStartDp);
            if (std::abs(dx) < slop || std::abs(dx) <= std::abs(dy))
                break;
            beginDrag();
        }

        updateDrag(position, mouse->timestamp());
        // The slide must not react to a gesture that no longer belongs to it.
        return true;
    }

    case QEvent::MouseButtonRelease: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton)
            break;

        const bool wasDragging = dragging_;
        if (wasDragging)
            endDrag(mapFromGlobal(mouse->globalPosition()), mouse->timestamp());

        resetDrag();
        if (wasDragging)
            return true;
        break;
    }

    case QEvent::Hide:
    case QEvent::UngrabMouse:
        if (dragging_ || snapping_)
            finishAnimation();
        resetDrag();
        break;

    default:
        break;
    }

    return ViewGroup::eventFilter(watched, event);
}

void Carousel::keyPressEvent(QKeyEvent* event)
{
    if (!isEnabled()) {
        ViewGroup::keyPressEvent(event);
        return;
    }

    switch (event->key()) {
    case Qt::Key_Left:
        previous();
        break;
    case Qt::Key_Right:
        next();
        break;
    case Qt::Key_Home:
        setCurrentIndex(0);
        break;
    case Qt::Key_End:
        setCurrentIndex(std::max(0, slideCount() - 1));
        break;
    case Qt::Key_Space:
        next();
        break;
    default:
        ViewGroup::keyPressEvent(event);
        return;
    }

    event->accept();
}

void Carousel::onShown()
{
    syncAutoPlay();
}

void Carousel::onHidden()
{
    if (autoTimer_ != nullptr)
        autoTimer_->stop();

    // A transition behind a hidden carousel is invisible work; settle it so no
    // timer keeps arranging off-screen slides. A drag is interrupted the same
    // way: a gesture cannot continue on a widget the user can no longer see.
    if (isAnimating() || dragging_)
        finishAnimation();
}

} // namespace md
