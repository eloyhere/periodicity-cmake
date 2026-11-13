#include <md/components/switching_overlay.h>

#include <md/animation/animator.h>
#include <md/components/font.h>
#include <md/components/modal.h>
#include <md/components/row_feedback.h>
#include <md/theme/theme.h>
#include <md/tokens/motion.h>
#include <md/tokens/state_layer.h>
#include <md/view/view.h>

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QList>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointer>
#include <QWidget>

#include <algorithm>
#include <exception>
#include <utility>

namespace md {
namespace {

// The indicator's 0..1 cycle. Level2 is the same rung as the picker's card
// entrance, so the busy surface reads as the same motion family.
constexpr MotionLevel kIndicatorLevel = MotionLevel::Level2;

// The minimum visible hold. Duration::Medium4 is exactly 400 ms and is the
// closest token to the requested 300–400 ms band. It is deliberately resolved
// directly, not through MotionScheme::level(), because reduced motion collapses
// every token to zero and the busy feedback must still be visible for the
// minimum time (the overlay's entrance/exit still snap under reduced motion).
constexpr Duration kMinimumVisible = Duration::Medium4;

constexpr qreal kWidthDp = 280.0;
constexpr qreal kHeightDp = 180.0;
constexpr qreal kSpinnerDp = 40.0;

QFont labelFont(const Theme& theme, const TextStyle& style, const QWidget* context)
{
    return Font::resolve(context, style, theme.density());
}

} // namespace

// ---------------------------------------------------------------------------
// SwitchingCard — the opaque busy card above the scrim.
// ---------------------------------------------------------------------------
class SwitchingCard final : public View
{
public:
    explicit SwitchingCard(QWidget* parent = nullptr)
        : View(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
        setBackgroundColour(Theme::forWidget(this).colours().surfaceContainerHigh);
        setShape(Theme::forWidget(this).shapes().extraLarge);
        setElevation(Elevation::Level2);
        setAccessibleName(SwitchingOverlay::tr("Switching…"));

        animator_ = new animation::Animator(this);
        connect(animator_, &animation::Animator::finished, this, [this] {
            if (!running_)
                return;
            // An indeterminate indicator never reaches an end state; start the
            // next cycle on the next turn, never inside Animator's own finished
            // emission.
            QMetaObject::invokeMethod(
                this,
                [this] {
                    if (running_)
                        startProgress();
                },
                Qt::QueuedConnection);
        });
    }

    void startProgress()
    {
        running_ = true;
        progress_ = 0.0;
        update();
        animator_->start(Theme::forWidget(this).motion().level(kIndicatorLevel),
                         [this](qreal value) {
                             QPointer<SwitchingCard> self(this);
                             if (self.isNull() || !running_)
                                 return;
                             progress_ = std::clamp(value, qreal(0.0), qreal(1.0));
                             update();
                         });
    }

    void stopProgress()
    {
        running_ = false;
        animator_->stop();
    }

    [[nodiscard]] qreal progress() const noexcept { return progress_; }

    void setManualAnimationClock(bool enabled) { animator_->setManualClock(enabled); }

    void advanceAnimation(std::chrono::milliseconds delta)
    {
        animator_->advanceTimeline(delta);
    }

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override
    {
        const Density& density = Theme::forWidget(this).density();
        setMeasuredSize(QSizeF(width.resolve(density.dp(kWidthDp)),
                               height.resolve(density.dp(kHeightDp))));
    }

    void paintContent(QPainter& painter, const QRectF& content) override
    {
        if (content.isEmpty())
            return;

        const Theme& theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const ColorScheme& colours = theme.colours();

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setClipPath(shape().path(content));

        const qreal spinner = density.dp(kSpinnerDp);
        const QRectF spinnerRect(content.center().x() - spinner / 2.0,
                                 content.top() + density.dp(36.0), spinner, spinner);

        QPen track(colours.onSurface, density.dp(3.0), Qt::SolidLine, Qt::RoundCap);
        QColor trackColour = track.color();
        trackColour.setAlphaF(0.12f);
        track.setColor(trackColour);
        painter.setPen(track);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(spinnerRect.adjusted(1.5, 1.5, -1.5, -1.5));

        QPen arc(colours.primary, density.dp(3.0), Qt::SolidLine, Qt::RoundCap);
        painter.setPen(arc);
        const int startAngle = static_cast<int>(qRound((-90.0 - progress_ * 360.0) * 16.0));
        painter.drawArc(spinnerRect.adjusted(1.5, 1.5, -1.5, -1.5), startAngle,
                        static_cast<int>(qRound(270.0 * 16.0)));

        const QFont font = labelFont(theme, theme.type().titleMedium, this);
        painter.setFont(font);
        painter.setPen(colours.onSurface);
        painter.drawText(QRectF(content.left() + density.dp(16.0),
                                spinnerRect.bottom() + density.dp(16.0),
                                content.width() - density.dp(32.0), density.dp(28.0)),
                         static_cast<int>(Qt::AlignCenter),
                         SwitchingOverlay::tr("Switching…"));

        painter.restore();
    }

    void onThemeChanged() override
    {
        const Theme& theme = Theme::forWidget(this);
        setBackgroundColour(theme.colours().surfaceContainerHigh);
        setShape(theme.shapes().extraLarge);
        setElevation(Elevation::Level2);
        update();
    }

private:
    animation::Animator* animator_ = nullptr;
    qreal progress_ = 0.0;
    bool running_ = false;
};

// ---------------------------------------------------------------------------
// SwitchingOverlay::ContentGuard — QPointer-guarded RAII restore.
//
// The guard saves the enabled state of every direct child of the window except
// the overlay, disables the enabled ones, and restores exactly those states on
// destruction (success, throw, shutdown, or the overlay being deleted while
// open). The captured focus widget is restored if it is still alive, visible
// and enabled; otherwise focus falls back to the invoking trigger, then the
// window.
// ---------------------------------------------------------------------------
class SwitchingOverlay::ContentGuard
{
public:
    ContentGuard(QWidget* window, QWidget* overlay, QWidget* invoker, QWidget* focus)
        : window_(window)
        , fallback_(invoker)
        , focus_(focus)
    {
        if (window_ == nullptr)
            return;

        const QList<QWidget*> children =
            window_->findChildren<QWidget*>(Qt::FindDirectChildrenOnly);
        for (QWidget* child : children) {
            if (child == nullptr || child == overlay)
                continue;
            // A transient popup/tooltip that is a window of its own is not part
            // of the content tree.
            if (child->isWindow())
                continue;

            entries_.append(Entry{child, child->isEnabled()});
            if (child->isEnabled())
                child->setEnabled(false);
        }
    }

    ~ContentGuard() { restore(); }

    ContentGuard(const ContentGuard&) = delete;
    ContentGuard& operator=(const ContentGuard&) = delete;

    void restore()
    {
        if (restored_)
            return;
        restored_ = true;

        for (const Entry& entry : std::as_const(entries_)) {
            if (entry.widget != nullptr)
                entry.widget->setEnabled(entry.enabled);
        }

        QWidget* target = focus_.data();
        if (target == nullptr || !target->isVisible() || !target->isEnabled())
            target = fallback_.data();
        if (target == nullptr || !target->isVisible() || !target->isEnabled())
            target = window_.data();
        if (target != nullptr && target->isVisible() && target->isEnabled())
            target->setFocus(Qt::OtherFocusReason);
    }

private:
    struct Entry
    {
        QPointer<QWidget> widget;
        bool enabled = true;
    };

    QPointer<QWidget> window_;
    QPointer<QWidget> fallback_;
    QPointer<QWidget> focus_;
    QList<Entry> entries_;
    bool restored_ = false;
};

// ---------------------------------------------------------------------------
// SwitchingOverlay
// ---------------------------------------------------------------------------

SwitchingOverlay::SwitchingOverlay(QWidget* invoker, QObject* parent)
    : QObject(parent != nullptr ? parent : (invoker != nullptr ? invoker->window() : nullptr))
    , invoker_(invoker)
{
    hold_ = new animation::Animator(this);
    connect(hold_, &animation::Animator::finished, this, [this] {
        holdDone_ = true;
        maybeClose();
    });
}

SwitchingOverlay::~SwitchingOverlay()
{
    // The RAII guard restores the content tree and focus even if the overlay is
    // deleted while the transition is still open.
    contentGuard_.reset();
    if (modal_ != nullptr) {
        modal_->disconnect(this);
        delete modal_.data();
    }
}

void SwitchingOverlay::run(std::function<void()> work)
{
    if (busy_)
        return;

    QWidget* host = invoker_ != nullptr ? invoker_->window() : nullptr;
    if (host == nullptr) {
        // No surface to show. Do not pretend a transition happened: report the
        // failure and let the caller keep the old value.
        lastError_ = tr("The switching surface has no window.");
        emit finished();
        return;
    }

    busy_ = true;
    workDone_ = false;
    holdDone_ = false;
    closing_ = false;
    lastError_.clear();

    // Capture the focus before the overlay's Modal steals it on open. The guard
    // is created later, once the scrim is opaque, but it must restore this
    // widget, not the busy card that currently owns focus.
    focusBefore_ = QApplication::focusWidget();
    work_ = std::move(work);

    ensureSurface();
    if (modal_ == nullptr || card_ == nullptr) {
        busy_ = false;
        lastError_ = tr("The switching surface could not be created.");
        emit finished();
        return;
    }

    // The scrim is deliberately opaque: disabling the content root repaints it
    // with disabled tokens, and those tokens must not flash through the busy
    // surface. The scrim is therefore a functional cover, not decoration. It
    // is applied after the entrance reaches full opacity, so the disabled
    // repaint happens entirely behind an opaque frame.
    modal_->setDim(1.0);
    modal_->setDismissOnClick(false);
    modal_->setDismissOnEscape(false);
    modal_->setManualAnimationClock(manualClock_);

    hold_->setManualClock(manualClock_);
    MotionSpec holdSpec;
    holdSpec.duration = durationMs(kMinimumVisible);
    holdSpec.easing = easingCurve(Easing::Linear);
    hold_->start(holdSpec, [](qreal) {});

    card_->startProgress();
    card_->setManualAnimationClock(manualClock_);

    modal_->open();
}

bool SwitchingOverlay::isBusy() const noexcept
{
    return busy_;
}

bool SwitchingOverlay::isVisible() const
{
    return modal_ != nullptr && modal_->isOpen();
}

QString SwitchingOverlay::lastError() const
{
    return lastError_;
}

qreal SwitchingOverlay::progressValue() const
{
    return card_ != nullptr ? card_->progress() : 0.0;
}

void SwitchingOverlay::setManualAnimationClock(bool enabled)
{
    manualClock_ = enabled;
    if (hold_ != nullptr)
        hold_->setManualClock(enabled);
    if (modal_ != nullptr)
        modal_->setManualAnimationClock(enabled);
    if (card_ != nullptr)
        card_->setManualAnimationClock(enabled);
}

bool SwitchingOverlay::isManualAnimationClock() const
{
    return manualClock_;
}

void SwitchingOverlay::advanceAnimation(std::chrono::milliseconds delta)
{
    if (modal_ != nullptr)
        modal_->advanceAnimation(delta);
    if (card_ != nullptr)
        card_->advanceAnimation(delta);
    if (hold_ != nullptr)
        hold_->advanceTimeline(delta);
}

Modal* SwitchingOverlay::modal() const
{
    return modal_.data();
}

int SwitchingOverlay::minimumVisibleMs() const
{
    return static_cast<int>(durationMs(kMinimumVisible).count());
}

void SwitchingOverlay::performWork()
{
    if (!busy_ || workDone_)
        return;

    if (contentGuard_ == nullptr) {
        contentGuard_ = std::make_unique<ContentGuard>(
            modal_ != nullptr ? modal_->window() : nullptr, modal_.data(),
            invoker_.data(), focusBefore_.data());
    }

    try {
        if (work_)
            work_();
    } catch (const std::exception& error) {
        lastError_ = QString::fromUtf8(error.what());
    } catch (...) {
        lastError_ = tr("The switch failed with an unknown exception.");
    }

    workDone_ = true;
    maybeClose();
}

void SwitchingOverlay::maybeClose()
{
    if (!busy_ || workDone_ == false || holdDone_ == false || closing_)
        return;

    closing_ = true;
    if (modal_ != nullptr && modal_->isOpen()) {
        modal_->close();
        return;
    }

    finishClose();
}

void SwitchingOverlay::finishClose()
{
    if (!busy_ && !closing_)
        return;

    busy_ = false;
    closing_ = false;
    // The hold has already finished on every path that reaches here; stopping
    // it from inside its own finished() emission would re-enter the animation.
    if (card_ != nullptr)
        card_->stopProgress();

    // Restore only after the scrim has hidden: an enabled repaint must not
    // appear underneath the exit fade.
    contentGuard_.reset();
    emit finished();
}

void SwitchingOverlay::ensureSurface()
{
    QWidget* host = invoker_ != nullptr ? invoker_->window() : nullptr;
    if (host == nullptr)
        return;

    if (modal_ != nullptr) {
        modal_->disconnect(this);
        delete modal_.data();
        modal_.clear();
        card_.clear();
    }

    auto* modal = new Modal(host);
    modal_ = modal;
    modal->setLevel(Modal::Level::Dialog);

    auto* card = new SwitchingCard(nullptr);
    card_ = card;
    modal->setCard(card);
    modal->setAnchor(invoker_.data());
    modal->setFocusReturn(invoker_.data());

    connect(modal, &Modal::openFinished, this, [this] {
        if (busy_)
            performWork();
    });
    connect(modal, &Modal::closeFinished, this, [this] {
        if (busy_ || closing_)
            finishClose();
    });
}

} // namespace md
