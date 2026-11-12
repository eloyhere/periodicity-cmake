// Periodicity — Pager, a horizontally swiped page container.
//
// One continuous offset drives everything. arrangeChildren() places page i at
// (i - anchor + shift) * width, so the drag, the snap and the resting state are
// the same expression evaluated at different values of `shift`; paintContent()
// only adds the indicator band underneath. Keeping the animation in the
// arrangement is what lets a page keep its live children while it moves.
//
// The snap is a QTimer that interpolates `shift` from where the finger left it
// to 0 (snap back) or to -1/+1 (change page) along the theme's standard curve.
// commitAnimation() is the single place that folds the offset back into the
// index, which is why a drag, a released drag and an interrupted next() all end
// in exactly the same state.

#include <md/components/pager.h>

#include <md/theme/theme.h>
#include <md/tokens/motion.h>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRectF>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace md {
namespace {

constexpr qreal kDefaultWidthDp = 360.0;
constexpr qreal kDefaultHeightDp = 260.0;
constexpr qreal kIndicatorBandDp = 28.0;
constexpr qreal kIndicatorCellIdleDp = 20.0;
constexpr qreal kIndicatorCellActiveDp = 28.0;
constexpr qreal kIndicatorDotDp = 6.0;
constexpr qreal kIndicatorPillDp = 18.0;

// A release snaps to the next page past a third of the viewport, or when the
// pointer was still moving faster than this at the moment it was lifted.
constexpr qreal kSnapFraction = 1.0 / 3.0;
constexpr qreal kVelocityPxPerMs = 0.35;
constexpr qreal kDragStartDp = 4.0;

// The rubber band at the ends: the damped offset approaches this many pages and
// never reaches it.
constexpr qreal kEdgeLimit = 0.25;

constexpr int kTickMs = 16;
constexpr qreal kEpsilon = 1e-6;

} // namespace

Pager::Pager(QWidget* parent)
    : ViewGroup(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setBackgroundColour(QColor(Qt::transparent));
    setAccessibleName(tr("Pager"));

    timer_ = new QTimer(this);
    timer_->setInterval(kTickMs);
    connect(timer_, &QTimer::timeout, this, [this] {
        const int duration = transitionDuration();
        if (duration <= 0) {
            shift_ = animationTo_;
            commitAnimation();
            return;
        }

        elapsedMs_ += kTickMs;
        const qreal linear =
            std::clamp(qreal(elapsedMs_) / qreal(duration), qreal(0.0), qreal(1.0));
        shift_ = animationFrom_ + (animationTo_ - animationFrom_) * easedProgress(linear);

        if (linear >= 1.0) {
            shift_ = animationTo_;
            commitAnimation();
            return;
        }

        syncLayout();
    });
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------

void Pager::setPages(const QList<View*>& pages)
{
    clearPages();

    for (View* page : pages) {
        if (page != nullptr)
            addPage(page);
    }
}

void Pager::addPage(View* page)
{
    if (page == nullptr || page == this || pages_.contains(page))
        return;

    if (isAnimating())
        finishAnimation();

    // addView() reparents the page; the destroy hook is installed afterwards so
    // removeView() does not disconnect it on the way in.
    addView(page);
    connect(page, &QObject::destroyed, this, [this, page] { prunePage(page); });
    pages_.append(page);

    const int last = std::max(0, pageCount() - 1);
    current_ = std::clamp(current_, 0, last);
    anchor_ = current_;
    shift_ = 0.0;

    syncLayout();
}

void Pager::removePage(View* page)
{
    const int index = static_cast<int>(pages_.indexOf(page));
    if (index < 0)
        return;

    if (isAnimating())
        finishAnimation();

    pages_.removeAt(index);
    removeView(page);

    if (index < current_)
        --current_;

    const int last = std::max(0, pageCount() - 1);
    current_ = std::clamp(current_, 0, last);
    anchor_ = current_;
    shift_ = 0.0;

    syncLayout();
}

void Pager::clearPages()
{
    if (isAnimating())
        finishAnimation();

    const QList<View*> removed = pages_;
    pages_.clear();

    for (View* page : removed) {
        if (page != nullptr)
            removeView(page);
    }

    current_ = 0;
    anchor_ = 0;
    shift_ = 0.0;

    syncLayout();
}

void Pager::prunePage(View* page)
{
    const int index = static_cast<int>(pages_.indexOf(page));
    if (index < 0)
        return;

    pages_.removeAt(index);

    if (index < current_)
        --current_;

    const int last = std::max(0, pageCount() - 1);
    current_ = std::clamp(current_, 0, last);
    anchor_ = current_;
    shift_ = 0.0;

    syncLayout();
}

QList<View*> Pager::pages() const
{
    return pages_;
}

int Pager::pageCount() const
{
    return static_cast<int>(pages_.size());
}

int Pager::currentIndex() const noexcept
{
    return current_;
}

int Pager::wrapIndex(int index) const
{
    const int count = pageCount();
    if (count <= 0)
        return 0;

    int result = index % count;
    if (result < 0)
        result += count;

    return result;
}

int Pager::clampIndex(int index) const
{
    return std::clamp(index, 0, std::max(0, pageCount() - 1));
}

bool Pager::canGoForward() const
{
    return loop_ || current_ < pageCount() - 1;
}

bool Pager::canGoBackward() const
{
    return loop_ || current_ > 0;
}

void Pager::setCurrentIndex(int index, bool animated)
{
    if (pageCount() <= 0)
        return;

    if (isAnimating())
        finishAnimation();

    const int target = loop_ ? wrapIndex(index) : clampIndex(index);
    if (target == current_) {
        if (std::abs(shift_) > kEpsilon)
            settle(animated);
        return;
    }

    anchor_ = current_;
    stepTo(target, animated);
}

void Pager::next()
{
    if (pageCount() <= 1 || !canGoForward())
        return;

    if (isAnimating())
        finishAnimation();

    anchor_ = current_;
    stepTo(wrapIndex(current_ + 1), true);
}

void Pager::previous()
{
    if (pageCount() <= 1 || !canGoBackward())
        return;

    if (isAnimating())
        finishAnimation();

    anchor_ = current_;
    stepTo(wrapIndex(current_ - 1), true);
}

void Pager::stepTo(int target, bool animated)
{
    if (pageCount() <= 0 || target < 0 || target >= pageCount())
        return;

    if (target == current_) {
        settle(animated);
        return;
    }

    current_ = target;
    emit pageChanged(current_);

    const int distance = current_ - anchor_;
    const bool canAnimate =
        animated && transitionDuration() > 0 && std::abs(distance) == 1;

    if (!canAnimate) {
        anchor_ = current_;
        shift_ = 0.0;
        if (timer_ != nullptr)
            timer_->stop();
        syncLayout();
        return;
    }

    beginAnimation(-qreal(distance));
    syncLayout();
}

void Pager::settle(bool animated)
{
    if (std::abs(shift_) <= kEpsilon) {
        if (timer_ != nullptr)
            timer_->stop();
        anchor_ = current_;
        shift_ = 0.0;
        syncLayout();
        return;
    }

    if (!animated || transitionDuration() <= 0) {
        shift_ = 0.0;
        anchor_ = current_;
        if (timer_ != nullptr)
            timer_->stop();
        syncLayout();
        return;
    }

    beginAnimation(0.0);
    syncLayout();
}

void Pager::beginAnimation(qreal target)
{
    animationFrom_ = shift_;
    animationTo_ = target;
    elapsedMs_ = 0;

    const int duration = transitionDuration();
    if (duration <= 0 || timer_ == nullptr) {
        shift_ = target;
        commitAnimation();
        return;
    }

    timer_->setInterval(kTickMs);
    timer_->start();
}

void Pager::commitAnimation()
{
    if (timer_ != nullptr)
        timer_->stop();

    // The offset is folded back into the index: current_ is already the page
    // the snap was heading for, and anchor_ becomes the page at rest.
    anchor_ = current_;
    shift_ = 0.0;
    syncLayout();
}

void Pager::finishAnimation()
{
    if (timer_ != nullptr && timer_->isActive()) {
        shift_ = animationTo_;
        commitAnimation();
        return;
    }

    if (std::abs(shift_) > kEpsilon) {
        shift_ = 0.0;
        anchor_ = current_;
        syncLayout();
    }
}

bool Pager::isAnimating() const
{
    return timer_ != nullptr && timer_->isActive();
}

int Pager::transitionDuration() const
{
    return static_cast<int>(Theme::forWidget(this).motion().medium1().duration.count());
}

qreal Pager::easedProgress(qreal linear) const
{
    const QEasingCurve curve = Theme::forWidget(this).motion().medium1().easing.toEasingCurve();
    return curve.valueForProgress(std::clamp(linear, qreal(0.0), qreal(1.0)));
}

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------

void Pager::setLoop(bool loop)
{
    if (loop_ == loop)
        return;

    loop_ = loop;
    requestUpdate();
}

bool Pager::loop() const noexcept
{
    return loop_;
}

void Pager::setSwipeEnabled(bool enabled)
{
    if (swipeEnabled_ == enabled)
        return;

    swipeEnabled_ = enabled;

    if (!swipeEnabled_ && dragging_) {
        dragging_ = false;
        settle(true);
        emit dragStateChanged(false);
    }

    requestUpdate();
}

bool Pager::isSwipeEnabled() const noexcept
{
    return swipeEnabled_;
}

void Pager::setIndicatorVisible(bool visible)
{
    if (indicatorVisible_ == visible)
        return;

    indicatorVisible_ = visible;
    syncLayout();
}

bool Pager::isIndicatorVisible() const noexcept
{
    return indicatorVisible_;
}

void Pager::activatePage(int index)
{
    setCurrentIndex(index, true);
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

bool Pager::indicatorsShown() const
{
    return indicatorVisible_ && pageCount() > 1;
}

QRectF Pager::indicatorBarRect() const
{
    if (!indicatorsShown())
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const QRectF area = contentRect();
    const qreal height = std::min(density.dp(kIndicatorBandDp), area.height());

    return QRectF(area.left(), area.bottom() - height, std::max(qreal(0.0), area.width()),
                  std::max(qreal(0.0), height));
}

QRectF Pager::pageArea() const
{
    const QRectF area = contentRect();
    const QRectF bar = indicatorBarRect();
    if (bar.isEmpty())
        return area;

    return QRectF(area.left(), area.top(), std::max(qreal(0.0), area.width()),
                  std::max(qreal(0.0), bar.top() - area.top()));
}

qreal Pager::pageWidth() const
{
    return pageArea().width();
}

QRectF Pager::pageRect(int index) const
{
    const QRectF area = pageArea();
    if (area.isEmpty() || index < 0 || index >= pageCount())
        return QRectF();

    const qreal width = area.width();
    const qreal x = area.left() + (qreal(index - anchor_) + shift_) * width;

    return QRectF(x, area.top(), width, area.height());
}

qreal Pager::dragOffset() const noexcept
{
    return shift_ * pageArea().width();
}

bool Pager::isDragging() const noexcept
{
    return dragging_;
}

QRectF Pager::indicatorRect(int index) const
{
    const QRectF bar = indicatorBarRect();
    if (bar.isEmpty() || index < 0 || index >= pageCount())
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const qreal idle = density.dp(kIndicatorCellIdleDp);
    const qreal active = density.dp(kIndicatorCellActiveDp);

    qreal total = 0.0;
    for (int dot = 0; dot < pageCount(); ++dot)
        total += dot == current_ ? active : idle;

    qreal x = bar.center().x() - total / 2.0;
    for (int dot = 0; dot < index; ++dot)
        x += dot == current_ ? active : idle;

    const qreal width = index == current_ ? active : idle;
    return QRectF(x, bar.top(), width, bar.height());
}

int Pager::indicatorAt(const QPointF& position) const
{
    if (!indicatorsShown())
        return -1;

    for (int index = 0; index < pageCount(); ++index) {
        if (indicatorRect(index).contains(position))
            return index;
    }

    return -1;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void Pager::syncVisibility()
{
    const int count = pageCount();

    for (int index = 0; index < count; ++index) {
        View* page = pages_.at(index);
        if (page == nullptr)
            continue;

        const qreal distance = qreal(index - anchor_) + shift_;
        bool wanted = std::abs(distance) < 1.0 - kEpsilon;

        // While a transition runs, the page being left and the page being
        // entered are both on screen: at the very first frame shift is still
        // zero, so the target would otherwise be hidden until the first tick.
        if (isAnimating() && (index == anchor_ || index == current_))
            wanted = true;

        if (wanted == !page->isHidden())
            continue;

        page->setVisible(wanted);
    }
}

void Pager::syncLayout()
{
    syncVisibility();
    arrangeChildren(contentRect());
    requestUpdate();
}

void Pager::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();

    setMeasuredSize(QSizeF(width.resolve(density.dp(kDefaultWidthDp)),
                           height.resolve(density.dp(kDefaultHeightDp))));
}

void Pager::arrangeChildren(const QRectF& content)
{
    Q_UNUSED(content);

    const QRectF area = pageArea();
    if (area.isEmpty())
        return;

    const qreal width = area.width();
    const qreal height = area.height();

    for (int index = 0; index < pageCount(); ++index) {
        View* page = pages_.at(index);
        if (page == nullptr || page->isHidden())
            continue;

        const qreal x = area.left() + (qreal(index - anchor_) + shift_) * width;

        page->measure(MeasureSpec::exactly(width), MeasureSpec::exactly(height));
        page->arrange(QRect(qRound(x), qRound(area.top()), qRound(width),
                            qRound(height)));
    }
}

void Pager::paintContent(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);

    const QRectF bar = indicatorBarRect();
    if (bar.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const qreal opacity = isEnabled() ? 1.0 : 0.38;

    const qreal dot = density.dp(kIndicatorDotDp);
    const qreal pill = density.dp(kIndicatorPillDp);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setOpacity(opacity);
    painter.setPen(Qt::NoPen);

    for (int index = 0; index < pageCount(); ++index) {
        const QRectF cell = indicatorRect(index);
        if (cell.isEmpty())
            continue;

        const bool active = index == current_;
        const qreal width = active ? pill : dot;
        const qreal height = dot;

        const QRectF marker(cell.center().x() - width / 2.0,
                            cell.center().y() - height / 2.0, width, height);

        painter.setBrush(active ? colours.primary : colours.outlineVariant);
        painter.drawPath(theme.shapes().full.path(marker));
    }

    painter.restore();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void Pager::mousePressEvent(QMouseEvent* event)
{
    ViewGroup::mousePressEvent(event);

    pressedIndicator_ = -1;
    pressed_ = false;

    if (event->button() != Qt::LeftButton || !isEnabled())
        return;

    setFocus(Qt::MouseFocusReason);

    const QPointF position = event->position();

    if (indicatorsShown() && indicatorBarRect().contains(position)) {
        pressedIndicator_ = indicatorAt(position);
        return;
    }

    if (!swipeEnabled_ || pageCount() <= 1 || !pageArea().contains(position))
        return;

    if (isAnimating())
        finishAnimation();

    pressed_ = true;
    dragging_ = false;
    pressPosition_ = position;
    pressShift_ = shift_;
    lastMoveX_ = position.x();
    lastMoveTimestamp_ = event->timestamp();
    velocity_ = 0.0;
}

qreal Pager::dampedShift(qreal shift) const
{
    if (shift < 0.0 && !canGoForward())
        return -(kEdgeLimit * -shift) / (kEdgeLimit + -shift);

    if (shift > 0.0 && !canGoBackward())
        return (kEdgeLimit * shift) / (kEdgeLimit + shift);

    return shift;
}

void Pager::mouseMoveEvent(QMouseEvent* event)
{
    ViewGroup::mouseMoveEvent(event);

    if (!pressed_)
        return;

    const qreal width = pageWidth();
    if (width <= 0.0)
        return;

    const Density& density = Theme::forWidget(this).density();
    const qreal dx = event->position().x() - pressPosition_.x();

    if (!dragging_) {
        if (std::abs(dx) < density.dp(kDragStartDp))
            return;

        dragging_ = true;
        emit dragStateChanged(true);
    }

    const qreal raw = pressShift_ + dx / width;
    shift_ = std::clamp(dampedShift(raw), qreal(-1.0), qreal(1.0));

    const quint64 now = event->timestamp();
    const qreal elapsed = qreal(now - lastMoveTimestamp_);
    if (elapsed > 0.0)
        velocity_ = (event->position().x() - lastMoveX_) / elapsed;

    lastMoveTimestamp_ = now;
    lastMoveX_ = event->position().x();

    syncLayout();
}

void Pager::mouseReleaseEvent(QMouseEvent* event)
{
    ViewGroup::mouseReleaseEvent(event);

    const int indicator = pressedIndicator_;
    pressedIndicator_ = -1;

    if (event->button() != Qt::LeftButton) {
        if (dragging_) {
            dragging_ = false;
            emit dragStateChanged(false);
            settle(true);
        }
        pressed_ = false;
        return;
    }

    const QPointF position = event->position();

    if (indicator >= 0) {
        if (indicatorAt(position) == indicator)
            activatePage(indicator);

        pressed_ = false;
        return;
    }

    if (!pressed_) {
        if (dragging_) {
            dragging_ = false;
            emit dragStateChanged(false);
            settle(true);
        }
        return;
    }

    const bool wasDragging = dragging_;
    dragging_ = false;
    pressed_ = false;

    if (wasDragging)
        emit dragStateChanged(false);

    if (!wasDragging)
        return;

    const bool forward = shift_ < -kSnapFraction || velocity_ < -kVelocityPxPerMs;
    const bool backward = shift_ > kSnapFraction || velocity_ > kVelocityPxPerMs;

    if (forward && !backward && canGoForward())
        stepTo(wrapIndex(current_ + 1), true);
    else if (backward && !forward && canGoBackward())
        stepTo(wrapIndex(current_ - 1), true);
    else
        settle(true);
}

void Pager::keyPressEvent(QKeyEvent* event)
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
        setCurrentIndex(pageCount() - 1);
        break;
    default:
        ViewGroup::keyPressEvent(event);
        return;
    }

    event->accept();
}

void Pager::onThemeChanged()
{
    updateGeometry();
    syncLayout();
}

} // namespace md
