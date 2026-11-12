// Periodicity — Scheduler implementation.
//
// The panel is deliberately a snapshot painter: refresh() copies the queue's
// counters into members and paintContent() reads only that snapshot. A queue
// dying between two paints therefore cannot leave the painter dereferencing a
// stale pointer, and hit testing uses the same layout pass as the painter.
//
// The painted geometry is deterministic from contentRect() and the theme's
// density, which is what lets the public control rectangles agree with what is
// on screen.

#include <md/components/scheduler.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QMouseEvent>
#include <QPainter>
#include <QString>
#include <Qt>

#include <algorithm>
#include <chrono>
#include <exception>

namespace md {
namespace {

constexpr qreal kPaddingDp = 16.0;
constexpr qreal kHeaderHeightDp = 40.0;
constexpr qreal kChipWidthDp = 96.0;
constexpr qreal kChipHeightDp = 24.0;
constexpr qreal kSectionGapDp = 8.0;
constexpr qreal kLaneHeightDp = 32.0;
constexpr qreal kLaneGapDp = 4.0;
constexpr qreal kLabelWidthDp = 96.0;
constexpr qreal kCountWidthDp = 48.0;
constexpr qreal kBarHeightDp = 8.0;
constexpr qreal kBarMinFillDp = 4.0;
constexpr qreal kTotalsHeightDp = 20.0;
constexpr qreal kBudgetHeightDp = 40.0;
constexpr qreal kBudgetValueWidthDp = 48.0;
constexpr qreal kControlSizeDp = 32.0;
constexpr qreal kActionHeightDp = 40.0;
constexpr qreal kActionWidthDp = 96.0;
constexpr qreal kInlineGapDp = 8.0;
constexpr qreal kTightGapDp = 4.0;

constexpr qreal kPanelWidthDp = 360.0;
constexpr qreal kPanelHeightDp = 380.0;

constexpr int kDrainRounds = 64;
constexpr int kMinBudgetMs = 0;
constexpr int kMaxBudgetMs = 16;

// One layout pass shared by the painter, the hit test and the public control
// rectangles. An empty return means the content rectangle is too small to
// paint anything.
struct PanelLayout
{
    QRectF header;
    QRectF chip;
    std::array<QRectF, schedule::kPriorityCount> lanes{};
    QRectF totals;
    QRectF budget;
    QRectF budgetMinus;
    QRectF budgetValue;
    QRectF budgetPlus;
    QRectF flush;
    QRectF drain;
};

PanelLayout layoutFor(const QRectF& content, const Density& density)
{
    PanelLayout layout;

    const qreal padding = density.dp(kPaddingDp);
    const QRectF area = content.adjusted(padding, padding, -padding, -padding);
    if (area.width() <= 0.0 || area.height() <= 0.0)
        return layout;

    const qreal headerHeight = density.dp(kHeaderHeightDp);
    const qreal chipWidth = density.dp(kChipWidthDp);
    const qreal chipHeight = density.dp(kChipHeightDp);
    const qreal sectionGap = density.dp(kSectionGapDp);

    qreal y = area.top();
    layout.header = QRectF(area.left(), y, area.width(), headerHeight);
    layout.chip = QRectF(area.right() - chipWidth,
                         y + (headerHeight - chipHeight) / 2.0, chipWidth, chipHeight);
    y += headerHeight + sectionGap;

    const qreal laneHeight = density.dp(kLaneHeightDp);
    const qreal laneGap = density.dp(kLaneGapDp);
    for (int index = 0; index < schedule::kPriorityCount; ++index) {
        if (index > 0)
            y += laneGap;
        layout.lanes[index] = QRectF(area.left(), y, area.width(), laneHeight);
        y += laneHeight;
    }
    y += sectionGap;

    layout.totals = QRectF(area.left(), y, area.width(), density.dp(kTotalsHeightDp));
    y += layout.totals.height() + sectionGap;

    const qreal budgetHeight = density.dp(kBudgetHeightDp);
    layout.budget = QRectF(area.left(), y, area.width(), budgetHeight);

    const qreal control = density.dp(kControlSizeDp);
    const qreal tightGap = density.dp(kTightGapDp);
    const qreal valueWidth = density.dp(kBudgetValueWidthDp);
    const qreal controlTop = y + (budgetHeight - control) / 2.0;
    layout.budgetPlus = QRectF(area.right() - control, controlTop, control, control);
    layout.budgetValue = QRectF(layout.budgetPlus.left() - tightGap - valueWidth,
                                controlTop, valueWidth, control);
    layout.budgetMinus = QRectF(layout.budgetValue.left() - tightGap - control,
                                controlTop, control, control);

    y += budgetHeight + sectionGap;

    const qreal actionHeight = density.dp(kActionHeightDp);
    const qreal actionWidth = density.dp(kActionWidthDp);
    const qreal actionGap = density.dp(kInlineGapDp);
    layout.drain = QRectF(area.right() - actionWidth, y, actionWidth, actionHeight);
    layout.flush = QRectF(layout.drain.left() - actionGap - actionWidth, y,
                          actionWidth, actionHeight);

    return layout;
}

QFont fontFor(const QWidget* widget, const TextStyle& style, const Density& density)
{
    return Font::resolve(widget, style, density);
}

QString priorityName(schedule::Priority priority)
{
    switch (priority) {
    case schedule::Priority::UserInput:
        return QCoreApplication::translate("Scheduler", "User input");
    case schedule::Priority::UiUpdate:
        return QCoreApplication::translate("Scheduler", "UI update");
    case schedule::Priority::UiEvent:
        return QCoreApplication::translate("Scheduler", "UI event");
    case schedule::Priority::Background:
        return QCoreApplication::translate("Scheduler", "Background");
    case schedule::Priority::Idle:
        return QCoreApplication::translate("Scheduler", "Idle");
    }

    return QString();
}

void warnFailure(const char* where, const std::exception* error) noexcept
{
    if (error != nullptr)
        qWarning("md::Scheduler: %s failed: %s", where, error->what());
    else
        qWarning("md::Scheduler: %s failed: unknown exception", where);
}

// A paint helper that throws after save() must not leave the shared painter
// unbalanced, because View continues its own pipeline after paintContent().
class PainterStateGuard
{
public:
    explicit PainterStateGuard(QPainter& painter)
        : painter_(painter)
    {
        painter_.save();
    }

    ~PainterStateGuard()
    {
        painter_.restore();
    }

    PainterStateGuard(const PainterStateGuard&) = delete;
    PainterStateGuard& operator=(const PainterStateGuard&) = delete;

private:
    QPainter& painter_;
};

} // namespace

Scheduler::Scheduler(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setInteractionFeedbackEnabled(false);
    setShape(Shape::medium());
    setElevation(Elevation::Level1);
    setAccessibleName(tr("Scheduler"));

    refreshTimer_.setInterval(refreshIntervalMs_);
    connect(&refreshTimer_, &QTimer::timeout, this, &Scheduler::refreshTimerFired);

    setQueue(&schedule::TaskQueue::main());
}

void Scheduler::setQueue(schedule::TaskQueue* nextQueue)
{
    if (nextQueue == queue_.data())
        return;

    if (drainedConnection_)
        QObject::disconnect(drainedConnection_);
    if (destroyedConnection_)
        QObject::disconnect(destroyedConnection_);
    drainedConnection_ = QMetaObject::Connection();
    destroyedConnection_ = QMetaObject::Connection();

    queue_ = nextQueue;
    boundQueue_ = nextQueue;

    if (nextQueue != nullptr) {
        drainedConnection_ = connect(nextQueue, &schedule::TaskQueue::drained, this,
                                     &Scheduler::handleQueueDrained);
        destroyedConnection_ = connect(
            nextQueue, &QObject::destroyed, this,
            [this, nextQueue] { handleQueueDestroyed(nextQueue); });
    }

    hovered_ = Control::None;
    pressed_ = Control::None;
    refresh();
    emit queueChanged(queue_.data());
}

schedule::TaskQueue* Scheduler::queue() const noexcept
{
    return queue_.data();
}

void Scheduler::setRefreshInterval(int ms)
{
    const int interval = std::max(0, ms);
    if (interval == refreshIntervalMs_)
        return;

    refreshIntervalMs_ = interval;
    refreshTimer_.setInterval(interval);

    if (interval == 0) {
        refreshTimer_.stop();
    } else if (isVisible()) {
        refreshTimer_.start(interval);
    }
}

int Scheduler::refreshInterval() const noexcept
{
    return refreshIntervalMs_;
}

int Scheduler::batchBudgetMs() const noexcept
{
    return batchBudgetMs_;
}

void Scheduler::refresh()
{
    try {
        schedule::TaskQueue* boundQueue = queue_.data();

        if (boundQueue == nullptr) {
            pendingByPriority_.fill(0);
            pendingTotal_ = 0;
            retiredTotal_ = 0;
            hovered_ = Control::None;
            pressed_ = Control::None;
        } else {
            int total = 0;
            for (int index = 0; index < schedule::kPriorityCount; ++index) {
                const int pending =
                    boundQueue->pending(static_cast<schedule::Priority>(index));
                pendingByPriority_[index] = pending;
                total += pending;
            }
            pendingTotal_ = total;
            retiredTotal_ = boundQueue->retiredCount();
        }

        updateAccessibleDescription();
        update();
    } catch (const std::exception& error) {
        warnFailure("Scheduler::refresh", &error);
    } catch (...) {
        warnFailure("Scheduler::refresh", nullptr);
    }
}

void Scheduler::flushQueue()
{
    try {
        schedule::TaskQueue* boundQueue = queue_.data();
        if (boundQueue != nullptr)
            boundQueue->flush();
        refresh();
    } catch (const std::exception& error) {
        warnFailure("Scheduler::flushQueue", &error);
    } catch (...) {
        warnFailure("Scheduler::flushQueue", nullptr);
    }
}

void Scheduler::drainQueue()
{
    try {
        for (int round = 0; round < kDrainRounds; ++round) {
            schedule::TaskQueue* boundQueue = queue_.data();
            if (boundQueue == nullptr || boundQueue->isEmpty())
                break;

            // Zero progress means the queue is already inside its own flush
            // (a task called Drain) or shutting down; another round would only
            // spin. The round cap above still bounds a queue that keeps
            // reposting work.
            if (boundQueue->flush() == 0)
                break;
        }

        refresh();
    } catch (const std::exception& error) {
        warnFailure("Scheduler::drainQueue", &error);
    } catch (...) {
        warnFailure("Scheduler::drainQueue", nullptr);
    }
}

void Scheduler::refreshTimerFired()
{
    try {
        refresh();
    } catch (const std::exception& error) {
        warnFailure("Scheduler::refreshTimerFired", &error);
    } catch (...) {
        warnFailure("Scheduler::refreshTimerFired", nullptr);
    }
}

void Scheduler::handleQueueDrained()
{
    try {
        refresh();
    } catch (const std::exception& error) {
        warnFailure("Scheduler::handleQueueDrained", &error);
    } catch (...) {
        warnFailure("Scheduler::handleQueueDrained", nullptr);
    }
}

void Scheduler::handleQueueDestroyed(schedule::TaskQueue* dying)
{
    try {
        if (dying == nullptr || dying != boundQueue_)
            return;

        // QPointer has already been cleared by the time destroyed() is
        // delivered, so the identity check above is what makes this handler
        // specific to the bound queue.
        queue_ = nullptr;
        boundQueue_ = nullptr;
        drainedConnection_ = QMetaObject::Connection();
        destroyedConnection_ = QMetaObject::Connection();

        pendingByPriority_.fill(0);
        pendingTotal_ = 0;
        retiredTotal_ = 0;
        hovered_ = Control::None;
        pressed_ = Control::None;

        updateAccessibleDescription();
        update();
        emit queueChanged(nullptr);
    } catch (const std::exception& error) {
        warnFailure("Scheduler::handleQueueDestroyed", &error);
    } catch (...) {
        warnFailure("Scheduler::handleQueueDestroyed", nullptr);
    }
}

void Scheduler::stepBudget(int delta)
{
    try {
        if (queue_.isNull())
            return;

        const int next =
            std::clamp(batchBudgetMs_ + delta, kMinBudgetMs, kMaxBudgetMs);
        if (next == batchBudgetMs_)
            return;

        batchBudgetMs_ = next;
        queue_->setBatchTimeBudget(std::chrono::milliseconds(batchBudgetMs_));
        update();
    } catch (const std::exception& error) {
        warnFailure("Scheduler::stepBudget", &error);
    } catch (...) {
        warnFailure("Scheduler::stepBudget", nullptr);
    }
}

void Scheduler::updateAccessibleDescription()
{
    const QString state = queue_.isNull() ? tr("unbound") : tr("bound");
    setAccessibleDescription(tr("Scheduler, %1, %2 tasks pending, %3 retired")
                                 .arg(state)
                                 .arg(pendingTotal_)
                                 .arg(retiredTotal_));
}

void Scheduler::onShown()
{
    try {
        refresh();
        if (refreshIntervalMs_ > 0)
            refreshTimer_.start(refreshIntervalMs_);
    } catch (const std::exception& error) {
        warnFailure("Scheduler::onShown", &error);
    } catch (...) {
        warnFailure("Scheduler::onShown", nullptr);
    }
}

void Scheduler::onHidden()
{
    try {
        refreshTimer_.stop();
        hovered_ = Control::None;
        pressed_ = Control::None;
    } catch (const std::exception& error) {
        warnFailure("Scheduler::onHidden", &error);
    } catch (...) {
        warnFailure("Scheduler::onHidden", nullptr);
    }
}

void Scheduler::measureOverride(MeasureSpec width, MeasureSpec height)
{
    try {
        const Density& density = Theme::forWidget(this).density();
        setMeasuredSize(QSizeF(width.resolve(density.dp(kPanelWidthDp)),
                               height.resolve(density.dp(kPanelHeightDp))));
    } catch (const std::exception& error) {
        warnFailure("Scheduler::measureOverride", &error);
    } catch (...) {
        warnFailure("Scheduler::measureOverride", nullptr);
    }
}

void Scheduler::paintContent(QPainter& painter, const QRectF& content)
{
    try {
        [[maybe_unused]] const PainterStateGuard painterGuard(painter);

        const Theme& theme = Theme::forWidget(this);
        const ColorScheme& colours = theme.colours();
        const Density& density = theme.density();
        const PanelLayout layout = layoutFor(content, density);
        if (layout.header.isEmpty())
            return;

        const bool bound = !queue_.isNull();
        const bool interactive = bound && isEnabled();

        const QFont titleFont = fontFor(this, theme.type().titleMedium, density);
        const QFont chipFont = fontFor(this, theme.type().labelMedium, density);
        const QFont bodyFont = fontFor(this, theme.type().bodyMedium, density);
        const QFont labelFont = fontFor(this, theme.type().labelMedium, density);
        const QFont valueFont = fontFor(this, theme.type().titleSmall, density);
        const QFont actionFont = fontFor(this, theme.type().labelLarge, density);

        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);

        const auto paintButton = [&](const QRectF& rect, const QString& label,
                                     const QFont& font, const QColor& fill,
                                     const QColor& textColour, bool enabled,
                                     bool hovered, bool pressed) {
            if (rect.isEmpty())
                return;

            [[maybe_unused]] const PainterStateGuard buttonState(painter);
            if (!enabled)
                painter.setOpacity(0.38);

            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
            painter.drawPath(theme.shapes().full.path(rect));

            if (enabled && (hovered || pressed)) {
                painter.setBrush(colours.stateLayer(
                    textColour, pressed ? ViewState::Pressed : ViewState::Hovered));
                painter.drawPath(theme.shapes().full.path(rect));
            }

            painter.setFont(font);
            painter.setPen(textColour);
            painter.drawText(rect, Qt::AlignCenter, label);
        };

        // Header: title on the left, bound/unbound chip on the right.
        const qreal inlineGap = density.dp(kInlineGapDp);
        painter.setFont(titleFont);
        painter.setPen(colours.onSurface);
        painter.drawText(
            layout.header.adjusted(0.0, 0.0, -(layout.chip.width() + inlineGap), 0.0),
            Qt::AlignLeft | Qt::AlignVCenter, tr("Scheduler"));

        painter.setPen(Qt::NoPen);
        painter.setBrush(bound ? colours.secondaryContainer
                               : colours.surfaceContainerHighest);
        painter.drawPath(theme.shapes().full.path(layout.chip));
        painter.setFont(chipFont);
        painter.setPen(bound ? colours.onSecondaryContainer : colours.onSurfaceVariant);
        painter.drawText(layout.chip, Qt::AlignCenter,
                         bound ? tr("Bound") : tr("Unbound"));

        // One lane per priority: label, proportional bar, pending count.
        const qreal labelWidth = density.dp(kLabelWidthDp);
        const qreal countWidth = density.dp(kCountWidthDp);
        const qreal barHeight = density.dp(kBarHeightDp);

        for (int lane = 0; lane < schedule::kPriorityCount; ++lane) {
            const QRectF row = layout.lanes[lane];
            if (row.isEmpty())
                continue;

            [[maybe_unused]] const PainterStateGuard laneState(painter);
            if (!bound)
                painter.setOpacity(0.38);

            const QRectF labelRect(row.left(), row.top(), labelWidth, row.height());
            const QRectF countRect(row.right() - countWidth, row.top(), countWidth,
                                   row.height());
            const qreal barLeft = labelRect.right() + inlineGap;
            const qreal barRight = countRect.left() - inlineGap;
            const qreal barWidth = std::max<qreal>(0.0, barRight - barLeft);
            const QRectF track(barLeft, row.center().y() - barHeight / 2.0, barWidth,
                               barHeight);

            painter.setPen(Qt::NoPen);
            painter.setBrush(colours.surfaceContainerHighest);
            if (track.width() > 0.0)
                painter.drawRoundedRect(track, barHeight / 2.0, barHeight / 2.0);

            const int pending = pendingByPriority_[lane];
            if (bound && pending > 0 && pendingTotal_ > 0 && track.width() > 0.0) {
                const qreal share =
                    static_cast<qreal>(pending) / static_cast<qreal>(pendingTotal_);
                const qreal fill =
                    std::min(track.width(),
                             std::max(density.dp(kBarMinFillDp), track.width() * share));
                painter.setBrush(colours.primary);
                painter.drawRoundedRect(QRectF(track.left(), track.top(), fill, barHeight),
                                        barHeight / 2.0, barHeight / 2.0);
            }

            painter.setFont(bodyFont);
            painter.setPen(colours.onSurface);
            painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter,
                             priorityName(static_cast<schedule::Priority>(lane)));

            painter.setFont(labelFont);
            painter.setPen(colours.onSurfaceVariant);
            painter.drawText(countRect, Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(pending));
        }

        // Totals row.
        {
            [[maybe_unused]] const PainterStateGuard totalsState(painter);
            if (!bound)
                painter.setOpacity(0.38);
            painter.setFont(labelFont);
            painter.setPen(colours.onSurfaceVariant);
            painter.drawText(layout.totals, Qt::AlignLeft | Qt::AlignVCenter,
                             tr("Pending: %1").arg(pendingTotal_));
            painter.drawText(layout.totals, Qt::AlignRight | Qt::AlignVCenter,
                             tr("Retired: %1").arg(retiredTotal_));
        }

        // Budget row: label, - / value / +.
        const bool minusEnabled = interactive && batchBudgetMs_ > kMinBudgetMs;
        const bool plusEnabled = interactive && batchBudgetMs_ < kMaxBudgetMs;

        painter.setFont(bodyFont);
        painter.setPen(bound ? colours.onSurface : colours.outline);
        const qreal budgetLabelWidth =
            std::max<qreal>(0.0, layout.budgetMinus.left() - layout.budget.left()
                                     - inlineGap);
        painter.drawText(QRectF(layout.budget.left(), layout.budget.top(),
                                budgetLabelWidth, layout.budget.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, tr("Batch budget"));

        paintButton(layout.budgetMinus, QStringLiteral("-"), actionFont,
                    colours.surfaceContainerHighest, colours.onSurface, minusEnabled,
                    hovered_ == Control::BudgetMinus, pressed_ == Control::BudgetMinus);

        painter.setFont(valueFont);
        painter.setPen(bound ? colours.onSurface : colours.outline);
        painter.drawText(layout.budgetValue, Qt::AlignCenter,
                         tr("%1 ms").arg(batchBudgetMs_));

        paintButton(layout.budgetPlus, QStringLiteral("+"), actionFont,
                    colours.surfaceContainerHighest, colours.onSurface, plusEnabled,
                    hovered_ == Control::BudgetPlus, pressed_ == Control::BudgetPlus);

        // Action row: a tonal Flush and a filled Drain, right aligned.
        paintButton(layout.flush, tr("Flush"), actionFont, colours.secondaryContainer,
                    colours.onSecondaryContainer, interactive,
                    hovered_ == Control::Flush, pressed_ == Control::Flush);
        paintButton(layout.drain, tr("Drain"), actionFont, colours.primary,
                    colours.onPrimary, interactive, hovered_ == Control::Drain,
                    pressed_ == Control::Drain);
    } catch (const std::exception& error) {
        warnFailure("Scheduler::paintContent", &error);
    } catch (...) {
        warnFailure("Scheduler::paintContent", nullptr);
    }
}

Scheduler::Control Scheduler::controlAt(const QPointF& position) const
{
    if (queue_.isNull() || !isEnabled())
        return Control::None;

    const PanelLayout layout =
        layoutFor(contentRect(), Theme::forWidget(this).density());

    if (layout.flush.contains(position))
        return Control::Flush;
    if (layout.drain.contains(position))
        return Control::Drain;
    if (batchBudgetMs_ > kMinBudgetMs && layout.budgetMinus.contains(position))
        return Control::BudgetMinus;
    if (batchBudgetMs_ < kMaxBudgetMs && layout.budgetPlus.contains(position))
        return Control::BudgetPlus;

    return Control::None;
}

QRectF Scheduler::flushButtonRect() const
{
    return layoutFor(contentRect(), Theme::forWidget(this).density()).flush;
}

QRectF Scheduler::drainButtonRect() const
{
    return layoutFor(contentRect(), Theme::forWidget(this).density()).drain;
}

QRectF Scheduler::budgetMinusRect() const
{
    return layoutFor(contentRect(), Theme::forWidget(this).density()).budgetMinus;
}

QRectF Scheduler::budgetPlusRect() const
{
    return layoutFor(contentRect(), Theme::forWidget(this).density()).budgetPlus;
}

void Scheduler::mousePressEvent(QMouseEvent* event)
{
    try {
        if (event->button() == Qt::LeftButton) {
            pressed_ = controlAt(event->position());
            if (pressed_ != Control::None)
                update();
        }

        View::mousePressEvent(event);
    } catch (const std::exception& error) {
        warnFailure("Scheduler::mousePressEvent", &error);
    } catch (...) {
        warnFailure("Scheduler::mousePressEvent", nullptr);
    }
}

void Scheduler::mouseReleaseEvent(QMouseEvent* event)
{
    try {
        const Control pressed = pressed_;
        pressed_ = Control::None;
        View::mouseReleaseEvent(event);

        if (pressed == Control::None || event->button() != Qt::LeftButton)
            return;
        if (controlAt(event->position()) != pressed)
            return;

        switch (pressed) {
        case Control::Flush:
            flushQueue();
            break;
        case Control::Drain:
            drainQueue();
            break;
        case Control::BudgetMinus:
            stepBudget(-1);
            break;
        case Control::BudgetPlus:
            stepBudget(1);
            break;
        case Control::None:
            break;
        }

        update();
    } catch (const std::exception& error) {
        warnFailure("Scheduler::mouseReleaseEvent", &error);
    } catch (...) {
        warnFailure("Scheduler::mouseReleaseEvent", nullptr);
    }
}

void Scheduler::mouseMoveEvent(QMouseEvent* event)
{
    try {
        View::mouseMoveEvent(event);

        const Control hovered = controlAt(event->position());
        if (hovered != hovered_) {
            hovered_ = hovered;
            update();
        }
    } catch (const std::exception& error) {
        warnFailure("Scheduler::mouseMoveEvent", &error);
    } catch (...) {
        warnFailure("Scheduler::mouseMoveEvent", nullptr);
    }
}

void Scheduler::leaveEvent(QEvent* event)
{
    try {
        View::leaveEvent(event);

        if (hovered_ != Control::None || pressed_ != Control::None) {
            hovered_ = Control::None;
            pressed_ = Control::None;
            update();
        }
    } catch (const std::exception& error) {
        warnFailure("Scheduler::leaveEvent", &error);
    } catch (...) {
        warnFailure("Scheduler::leaveEvent", nullptr);
    }
}

} // namespace md
