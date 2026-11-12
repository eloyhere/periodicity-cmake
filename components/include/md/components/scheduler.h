// Periodicity — Scheduler, a panel over the task priority queue.
//
// The panel visualises one md::schedule::TaskQueue: a header with a bound or
// unbound chip, one lane per Priority (label, pending count and a bar whose
// length is that lane's share of all pending work), a totals row, a batch
// budget row and the two pump actions. It is a read-out with three controls,
// not a second scheduler: the queue remains the authority, and the panel never
// runs work on its own beyond the Flush and Drain buttons.
//
// Ownership. A Scheduler binds exactly one queue and never owns or deletes it.
// The default binding is the process-wide TaskQueue::main(); setQueue(nullptr)
// leaves the panel unbound, which paints the empty state and disables every
// control without touching a queue pointer.
//
// Lifetime. TaskQueue is a QObject, so the binding is guarded twice: a QPointer
// makes every dereference safe, and a destroyed() connection unbinds the panel
// (emitting queueChanged(nullptr)) if the queue dies first. Rebinding
// disconnects both the old drained() and old destroyed() connections, so a
// stale queue can neither refresh the panel nor unbind the new one.
//
// Refresh. Counts are cached by refresh(), which the timer, the queue's
// drained() signal and the two actions all call; painting and hit testing read
// the cache and never the queue. The timer runs at refreshInterval() while the
// panel is shown and is stopped when it is hidden. An interval of zero disables
// it, leaving drained() as the only automatic refresh.
//
// The controls are painted rather than child widgets, because Scheduler is a
// plain View (not a ViewGroup). Their hit rectangles are exposed so a host or a
// test can drive them without duplicating the layout.

#pragma once

#include <md/schedule/task_queue.h>
#include <md/view/view.h>

#include <QMetaObject>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QTimer>

#include <array>

class QEvent;
class QMouseEvent;
class QPainter;

namespace md {

class Scheduler : public View
{
    Q_OBJECT

public:
    explicit Scheduler(QWidget* parent = nullptr);

    // Binds `nextQueue` and refreshes immediately. A null queue leaves the panel
    // unbound: every lane reads zero, the chip says Unbound and the controls
    // are disabled. The queue is not reparented and is never deleted. Like
    // TaskQueue::main(), it must belong to the GUI thread, because the panel
    // reads its counters from the GUI thread.
    void setQueue(schedule::TaskQueue* nextQueue);

    // The bound queue, or nullptr when unbound. A queue destroyed while bound
    // reads as nullptr from then on.
    [[nodiscard]] schedule::TaskQueue* queue() const noexcept;

    // How often the panel re-reads the queue while shown. The default is
    // 250 ms; zero (or a negative value, clamped to zero) disables the timer.
    // Changing it while the panel is shown restarts the timer.
    void setRefreshInterval(int ms);
    [[nodiscard]] int refreshInterval() const noexcept;

    // Re-reads the pending counts and the retired count, updates the accessible
    // description and repaints. Safe to call while unbound.
    void refresh();

    // The batch budget the -/+ controls currently display, in milliseconds.
    // The panel's model starts at 8 ms, the TaskQueue default. A step applies
    // the new value with setBatchTimeBudget(); TaskQueue exposes no getter, so
    // the panel cannot discover a budget set elsewhere on a newly bound queue.
    [[nodiscard]] int batchBudgetMs() const noexcept;

    // Geometry of the painted controls, in widget coordinates. Exposed for
    // hosts and tests, exactly like Pagination's button rectangles.
    [[nodiscard]] QRectF flushButtonRect() const;
    [[nodiscard]] QRectF drainButtonRect() const;
    [[nodiscard]] QRectF budgetMinusRect() const;
    [[nodiscard]] QRectF budgetPlusRect() const;

public slots:
    // One flush at the queue's default bound, then a refresh.
    void flushQueue();

    // Flushes until the queue is empty or no work ran, bounded to 64 rounds so
    // a task that reposts itself cannot pin the GUI thread.
    void drainQueue();

signals:
    // Emitted on every rebind and when the bound queue is destroyed (with
    // nullptr). The signal never carries a dangling pointer.
    void queueChanged(schedule::TaskQueue* boundQueue);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

    // View's lifecycle hooks. The refresh timer starts on show and stops on
    // hide; the panel refreshes once as it becomes visible.
    void onShown() override;
    void onHidden() override;

private slots:
    void refreshTimerFired();
    void handleQueueDrained();

private:
    enum class Control {
        None,
        Flush,
        Drain,
        BudgetMinus,
        BudgetPlus,
    };

    // The destroyed() handler runs after QPointer has been cleared, so it gets
    // the queue identity as an argument and compares it with boundQueue_.
    void handleQueueDestroyed(schedule::TaskQueue* dying);

    void stepBudget(int delta);
    void updateAccessibleDescription();
    [[nodiscard]] Control controlAt(const QPointF& position) const;

    QPointer<schedule::TaskQueue> queue_;

    // Identity of the bound queue, used only by handleQueueDestroyed() to
    // recognise its sender once queue_ is already null. Never dereferenced:
    // every read of the queue goes through queue_.
    schedule::TaskQueue* boundQueue_ = nullptr;

    QTimer refreshTimer_;
    int refreshIntervalMs_ = 250;
    int batchBudgetMs_ = 8;

    // The last snapshot taken by refresh(): painting never touches the queue.
    std::array<int, schedule::kPriorityCount> pendingByPriority_{};
    int pendingTotal_ = 0;
    int retiredTotal_ = 0;

    QMetaObject::Connection drainedConnection_;
    QMetaObject::Connection destroyedConnection_;

    Control hovered_ = Control::None;
    Control pressed_ = Control::None;
};

} // namespace md
