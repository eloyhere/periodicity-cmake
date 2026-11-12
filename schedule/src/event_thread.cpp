// Periodicity — the event thread's input pipeline implementation.

#include <md/schedule/event_thread.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QThread>

#include <algorithm>
#include <exception>
#include <memory>
#include <utility>

namespace md::schedule {
namespace {

EventThread* g_instance = nullptr;

} // namespace

EventThread::EventThread(QObject* parent) : QObject(parent) {}

EventThread::~EventThread()
{
    detach();

    std::lock_guard lock(mutex_);
    discrete_.clear();
    moves_.clear();
    wheels_.clear();
    gestures_.clear();
    handler_ = {};
}

EventThread* EventThread::instance() noexcept
{
    return g_instance;
}

EventThread& EventThread::install()
{
    Q_ASSERT_X(!QCoreApplication::instance()
                   || QThread::currentThread() == QCoreApplication::instance()->thread(),
               "EventThread::install", "the event runtime belongs to the GUI thread");
    if (g_instance == nullptr) {
        g_instance = new EventThread;
        g_instance->installed_ = true;
    }
    if (UiThread* ui = UiThread::instance(); ui != nullptr && g_instance->uiThread() != ui)
        g_instance->attach(ui);

    // A lightweight marker lets the synchronous event dispatcher refuse a
    // dispatch that arrives on the wrong thread while input batching is active,
    // without the headless event module depending on this one.
    if (QCoreApplication* app = QCoreApplication::instance())
        app->setProperty("md.schedule.eventThreadInstalled", true);

    return *g_instance;
}

void EventThread::uninstall()
{
    if (g_instance == nullptr)
        return;

    if (QCoreApplication* app = QCoreApplication::instance())
        app->setProperty("md.schedule.eventThreadInstalled", false);

    EventThread* dying = g_instance;
    g_instance = nullptr;
    dying->installed_ = false;
    delete dying;
}

bool EventThread::isInstalled() const noexcept
{
    return installed_;
}

void EventThread::attach(UiThread* uiThread)
{
    if (uiDestroyedConnection_)
        QObject::disconnect(uiDestroyedConnection_);

    ui_.store(uiThread, std::memory_order_release);
    boundaryArmed_.store(false, std::memory_order_release);

    if (uiThread != nullptr) {
        // A UI runtime can be uninstalled before this object. Clear the raw
        // pointer synchronously so a late producer never sees freed memory.
        uiDestroyedConnection_ = QObject::connect(
            uiThread, &QObject::destroyed, this,
            [this] {
                ui_.store(nullptr, std::memory_order_release);
                boundaryArmed_.store(false, std::memory_order_release);
                uiDestroyedConnection_ = QMetaObject::Connection();
            },
            Qt::DirectConnection);
    }

    // A pending queue at attach time still needs a boundary. The ordinary
    // path arms lazily on the first postInput().
    if (uiThread != nullptr && depth() > 0)
        ensureBoundary();
}

void EventThread::detach()
{
    if (uiDestroyedConnection_) {
        QObject::disconnect(uiDestroyedConnection_);
        uiDestroyedConnection_ = QMetaObject::Connection();
    }

    boundaryArmed_.store(false, std::memory_order_release);
    ui_.store(nullptr, std::memory_order_release);
}

UiThread* EventThread::uiThread() const noexcept
{
    return ui_.load(std::memory_order_acquire);
}

int EventThread::priorityOf(InputKind kind) noexcept
{
    switch (kind) {
    case InputKind::PointerDown:
    case InputKind::PointerUp:
    case InputKind::Key:
        return 0;
    case InputKind::Wheel:
        return 1;
    case InputKind::PointerMove:
        return 2;
    case InputKind::Gesture:
        return 3;
    }
    return 3;
}

void EventThread::postInput(InputKind kind, quint64 target, QPointF position)
{
    InputPayload payload;
    payload.target = target;
    payload.position = position;
    payload.globalPosition = position;
    postInput(kind, std::move(payload));
}

void EventThread::postInput(InputKind kind, InputPayload payload)
{
    {
        std::lock_guard lock(mutex_);

        InputEvent event;
        event.kind = kind;
        event.payload = std::move(payload);
        event.sequence = nextSequence_++;

        switch (kind) {
        case InputKind::PointerDown:
        case InputKind::PointerUp:
        case InputKind::Key:
            discrete_.push_back(std::move(event));
            break;

        case InputKind::PointerMove: {
            const auto found = moves_.find(event.payload.target);
            if (found != moves_.end()) {
                ++stats_.coalesced;
                found->second = std::move(event);
            } else {
                moves_.emplace(event.payload.target, std::move(event));
            }
            break;
        }

        case InputKind::Wheel: {
            const auto found = wheels_.find(event.payload.target);
            if (found != wheels_.end()) {
                ++stats_.coalesced;
                found->second = std::move(event);
            } else {
                wheels_.emplace(event.payload.target, std::move(event));
            }
            break;
        }

        case InputKind::Gesture:
            gestures_.push_back(std::move(event));
            break;
        }

        ++stats_.posted;
        stats_.depth = discrete_.size() + moves_.size() + wheels_.size()
            + gestures_.size();
    }

    // The callback is armed lazily and the frame request itself coalesces, so
    // a hundred samples in one turn still cost one boundary and one frame.
    ensureBoundary();
}

void EventThread::ensureBoundary()
{
    UiThread* ui = ui_.load(std::memory_order_acquire);
    if (ui == nullptr)
        return;

    bool expected = false;
    if (boundaryArmed_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        QPointer<EventThread> guard(this);
        ui->addFrameCallback([guard] {
            if (guard)
                guard->onFrameBoundary();
        });
    }

    // Even when the callback was already armed, this request is what makes the
    // boundary happen. requestFrame() collapses repeated calls.
    ui->requestFrame(FrameRequest::Input);
}

void EventThread::onFrameBoundary()
{
    boundaryArmed_.store(false, std::memory_order_release);
    flushToUiThread();
}

void EventThread::flushToUiThread()
{
    Batch batch = takeBatch();
    if (batch.empty())
        return;

    UiThread* ui = ui_.load(std::memory_order_acquire);
    if (ui == nullptr) {
        // No frame runtime exists: preserve the events by delivering them on
        // the calling thread. With a runtime installed this branch is never
        // taken, so widget work always stays on the UI thread.
        deliver(batch);
        return;
    }

    auto shared = std::make_shared<Batch>(std::move(batch));
    QPointer<EventThread> guard(this);
    ui->postTask(Priority::UiEvent, [guard, shared] {
        if (guard)
            guard->deliver(*shared);
    });

    std::lock_guard lock(mutex_);
    ++stats_.handoffs;
}

EventThread::Batch EventThread::takeBatch()
{
    std::deque<InputEvent> discrete;
    std::unordered_map<quint64, InputEvent> moves;
    std::unordered_map<quint64, InputEvent> wheels;
    std::deque<InputEvent> gestures;

    {
        std::lock_guard lock(mutex_);
        discrete.swap(discrete_);
        moves.swap(moves_);
        wheels.swap(wheels_);
        gestures.swap(gestures_);
    }

    // Priority order is the contract: discrete input first, then the coalesced
    // continuous samples in the order they last arrived, then gestures built
    // from those moves.
    auto bySequence = [](const InputEvent& left, const InputEvent& right) {
        return left.sequence < right.sequence;
    };

    std::vector<InputEvent> wheelBatch;
    wheelBatch.reserve(wheels.size());
    for (auto& [target, event] : wheels) {
        Q_UNUSED(target);
        wheelBatch.push_back(std::move(event));
    }
    std::sort(wheelBatch.begin(), wheelBatch.end(), bySequence);

    std::vector<InputEvent> moveBatch;
    moveBatch.reserve(moves.size());
    for (auto& [target, event] : moves) {
        Q_UNUSED(target);
        moveBatch.push_back(std::move(event));
    }
    std::sort(moveBatch.begin(), moveBatch.end(), bySequence);

    Batch batch;
    batch.reserve(discrete.size() + wheelBatch.size() + moveBatch.size()
                  + gestures.size());
    for (auto& event : discrete)
        batch.push_back(std::move(event));
    for (auto& event : wheelBatch)
        batch.push_back(std::move(event));
    for (auto& event : moveBatch)
        batch.push_back(std::move(event));
    for (auto& event : gestures)
        batch.push_back(std::move(event));

    {
        std::lock_guard lock(mutex_);
        if (!batch.empty())
            ++stats_.batches;
        stats_.depth = discrete_.size() + moves_.size() + wheels_.size()
            + gestures_.size();
    }

    return batch;
}

void EventThread::setBatchHandler(BatchHandler handler)
{
    std::lock_guard lock(mutex_);
    handler_ = std::move(handler);
}

void EventThread::deliver(const Batch& batch)
{
    if (batch.empty())
        return;

    BatchHandler handler;
    {
        std::lock_guard lock(mutex_);
        handler = handler_;
    }
    if (!handler)
        return;

    try {
        handler(batch);
    } catch (...) {
        // A consumer that throws must not break the boundary or lose the
        // next frame's batch.
    }

    std::lock_guard lock(mutex_);
    stats_.delivered += batch.size();
}

EventThread::Stats EventThread::stats() const
{
    std::lock_guard lock(mutex_);
    Stats result = stats_;
    result.running = ui_.load(std::memory_order_acquire) != nullptr;
    return result;
}

std::size_t EventThread::depth() const
{
    std::lock_guard lock(mutex_);
    return discrete_.size() + moves_.size() + wheels_.size() + gestures_.size();
}

} // namespace md::schedule
