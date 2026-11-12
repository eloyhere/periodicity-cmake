// Periodicity �?the component kernel (framework.md §5.7, §6, §7).
//
// View is where Qt stops and the framework starts. Qt's geometry questions are
// answered by the measure contract, Qt's events are converted into md::Events
// and dispatched through the EventDispatcher, and Qt's paint event is replaced
// by the fixed pipeline. Everything a subclass can influence is a virtual hook
// called by that pipeline; nothing lets it skip a stage.
//
// The Qt event handlers accept their event after dispatch. Qt would otherwise
// keep propagating a press to the parent widget, and the same press would reach
// the framework twice �?once through the child's EventTarget chain and once
// through Qt's own parent chain.

#include <md/view/view.h>

#include <md/view/error_guard.h>
#include <md/event/event_dispatcher.h>
#include <md/event/key_event.h>
#include <md/event/pointer_event.h>
#include <md/event/qt_bridge.h>
#include <md/layer/surface_context.h>
#include <md/schedule/task_queue.h>
#include <md/schedule/frame_scheduler.h>
#include <md/store/store.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>

#include <QEnterEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QResizeEvent>
#include <QScopeGuard>
#include <QtMath>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>

namespace md {
namespace {

// The coalescing key space belongs to the process-wide task queue, so every
// View draws from one monotonically increasing sequence. A per-instance key is
// what makes "a burst of state changes paints once" hold without a timer.
std::atomic<quint64> nextUpdateKey{1};

[[nodiscard]] quint64 allocateUpdateKey() noexcept
{
    return nextUpdateKey.fetch_add(1, std::memory_order_relaxed);
}

// Qt timestamps are milliseconds since an unspecified epoch and QEvent does
// not expose one; the framework only needs a monotonic value when a caller has
// one. Events synthesised from Qt's handlers therefore carry zero, and the
// bridge remains a pure function of the event payload.
constexpr std::chrono::milliseconds kNoTimestamp{0};

// One bit per guarded entry point of View. The bits live in the view's
// GuardReport, so a failure that repeats in paintEvent() warns once while a
// failure in arrange() still reports.
constexpr quint32 kPaintSite = 1u << 0;
constexpr quint32 kMeasureSite = 1u << 1;
constexpr quint32 kArrangeSite = 1u << 2;
constexpr quint32 kEventSite = 1u << 3;

// The ripple controller clips to a single radius, so an asymmetric shape uses
// its largest corner as the clip. That keeps a pill and a rounded card clean;
// a shape whose corners differ by more than the clip radius is an edge case
// the controller's contract does not cover.
[[nodiscard]] qreal largestRadius(const CornerRadii& radii) noexcept
{
    return std::max({radii.topStart, radii.topEnd, radii.bottomEnd, radii.bottomStart});
}

// Space, Return and Enter activate a focused component (framework.md §6.4).
[[nodiscard]] bool isActivationKey(int key) noexcept
{
    return key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter;
}

} // namespace

View::View(QWidget* parent)
    : QWidget(parent)
    , state_(ViewState::Enabled)
    , ripple_(this)
    , updateKey_(allocateUpdateKey())
{
    // The state overlay is context-driven and stateless, so it can be built
    // once. The focus ring stays out of the stack: it paints last, above the
    // ripple, and only on the paint path that checks both focusable() and
    // hasFocus(). The contour is not part of the default pipeline: the reference
    // design system outlines only the surfaces that ask for one (an Outlined
    // button, a card), and a 1 dp ring around every label is noise. Components
    // draw their own contour; OutlineLayer stays available for a surface that
    // wants it.
    foreground_.add(std::make_unique<layer::StateOverlayLayer>());

    syncRippleAppearance();
    syncRippleEnabled();
    updateRippleGeometry();

    connect(&ripple_, &ripple::RippleController::repaintRequested, this,
            [this] { requestUpdate(); });
}

View::~View() = default;

void View::measure(MeasureSpec width, MeasureSpec height)
{
    // This is the only place that marks a measurement as explicit: a Qt
    // geometry query goes through hintSize() and must not displace the size a
    // container reads through measuredSize().
    const bool measured = view::guard(
        "View::measure", kMeasureSite, guardReport_,
        [this, width, height] { measureOverride(width, height); });

    if (measured)
        measuredExplicitly_ = true;
}

void View::arrange(const QRect& geometry)
{
    static_cast<void>(view::guard(
        "View::arrange", kArrangeSite, guardReport_, [this, geometry] {
            // setGeometry() emits a resize synchronously, and resizeEvent()
            // arranges. The depth marker suppresses that duplicate call, so
            // exactly one arrangeOverride() runs per arrange().
            ++arrangeDepth_;
            const auto depthGuard = qScopeGuard([this] { --arrangeDepth_; });

            if (this->geometry() != geometry)
                setGeometry(geometry);

            // The arrangement must run even when the rectangle did not change:
            // a container re-arranges its children after a measure pass that
            // changed a child's size but not the parent's geometry.
            arrangeOverride(contentRect());
        }));
}

QSizeF View::measuredSize() const noexcept
{
    return measuredSize_;
}

QRectF View::contentRect() const
{
    // A View has no padding of its own yet; the content rectangle is the whole
    // widget. A subclass that reserves a shadow inset or padding overrides this
    // rectangle; the paint pipeline, the ripple bounds and arrangeOverride()
    // all follow the override.
    return QRectF(rect());
}

ViewStates View::state() const noexcept
{
    return state_;
}

void View::setState(ViewState flag, bool on)
{
    if (flag == ViewState::Enabled) {
        // Enabled mirrors the widget, not the other way round: QWidget owns
        // that bit and changeEvent() copies it back into the state set.
        if (isEnabled() != on)
            setEnabled(on);
        else if (state_.testFlag(ViewState::Enabled) != on) {
            state_.setFlag(ViewState::Enabled, on);
            requestUpdate();
        }
        return;
    }

    if (state_.testFlag(flag) == on)
        return;

    state_.setFlag(flag, on);
    requestUpdate();
}

void View::setElevation(Elevation level)
{
    if (elevation_ == level)
        return;

    elevation_ = level;

    // The elevation layer reads the level from the SurfaceContext on every
    // paint, so there is no layer state to synchronise here.
    requestUpdate();
}

Elevation View::elevation() const noexcept
{
    return elevation_;
}

void View::setShape(Shape shape)
{
    if (shape_ == shape)
        return;

    shape_ = shape;
    updateRippleGeometry();
    requestUpdate();
}

Shape View::shape() const noexcept
{
    return shape_;
}

void View::setBackgroundColour(const QColor& colour)
{
    if (backgroundColour_ == colour)
        return;

    backgroundColour_ = colour;
    requestUpdate();
}

void View::setContentColour(const QColor& colour)
{
    if (contentColour_ == colour)
        return;

    contentColour_ = colour;

    // The controller keeps its colour explicit once set, so the View always
    // republishes the resolved value instead of leaving it to the theme.
    const Theme& theme = Theme::forWidget(this);
    ripple_.setContentColour(colour.isValid() ? colour : theme.colours().onSurface);
    requestUpdate();
}

QSize View::sizeHint() const
{
    // The measure contract is non-const because it caches; the hint path is the
    // only thing this const method changes, so the cast is safe.
    const QSizeF hint = const_cast<View*>(this)->hintSize(
        "View::sizeHint", MeasureSpec::unspecified(), MeasureSpec::unspecified());
    return QSize(qCeil(hint.width()), qCeil(hint.height()));
}

QSize View::minimumSizeHint() const
{
    const QSizeF hint = const_cast<View*>(this)->hintSize(
        "View::minimumSizeHint", MeasureSpec::atMost(0.0), MeasureSpec::atMost(0.0));
    return QSize(qCeil(hint.width()), qCeil(hint.height()));
}

bool View::hasHeightForWidth() const
{
    return true;
}

int View::heightForWidth(int width) const
{
    const QSizeF hint = const_cast<View*>(this)->hintSize(
        "View::heightForWidth", MeasureSpec::exactly(qreal(width)),
        MeasureSpec::unspecified());
    return qCeil(hint.height());
}

QSizeF View::hintSize(const char* where, MeasureSpec width, MeasureSpec height)
{
    // A Qt geometry query is not a measurement: it must not overwrite the size
    // a container already read through measuredSize(). Before the first
    // explicit measure there is nothing to protect, and the query result is the
    // only measurement the view has, so it stays visible there.
    const QSizeF savedSize = measuredSize_;
    const bool hadExplicit = measuredExplicitly_;

    const bool measured = view::guard(
        where, kMeasureSite, guardReport_,
        [this, width, height] { measureOverride(width, height); });
    const QSizeF hint = measuredSize_;

    if (hadExplicit || !measured)
        measuredSize_ = savedSize;

    return measured ? hint : savedSize;
}

EventTarget* View::parentTarget() const noexcept
{
    for (QWidget* widget = parentWidget(); widget != nullptr;
         widget = widget->parentWidget()) {
        if (auto* view = qobject_cast<View*>(widget))
            return view;
    }

    return nullptr;
}

QPointF View::mapPointFrom(const EventTarget& source, const QPointF& point) const
{
    if (&source == this)
        return point;

    // The visual tree is the widget tree, so Qt's own conversion is the
    // authoritative mapping; it accounts for the scroll offset of every
    // ancestor and for the device pixel ratio. Direction matters: mapFrom()
    // only accepts an ancestor of this widget, so a point coming from a child
    // (the normal case while an event bubbles) goes through the child's
    // mapTo(). Mapping between unrelated trees has no meaning and is a no-op.
    const auto* sourceView = dynamic_cast<const View*>(&source);
    if (sourceView == nullptr)
        return point;

    if (sourceView->isAncestorOf(this))
        return mapFrom(sourceView, point);

    if (isAncestorOf(sourceView))
        return sourceView->mapTo(this, point);

    return point;
}

bool View::onEvent(Event& event)
{
    // Capture and bubble see the same event on their way to and from the
    // target. A View has no gesture of its own to add there, and accepting in
    // the capture phase would suppress the bubble for everyone else.
    if (event.phase() != EventPhase::Target)
        return event.isAccepted();

    if (!isEnabled())
        return false;

    // Only a focusable control carries the pointer ripple; a passive surface
    // (card, banner, page) still gets the static state overlay, but no animated
    // ripple on hover or press.
    syncRippleEnabled();

    bool handled = false;
    switch (event.type()) {
    case EventType::PointerEnter:
        if (auto* pointer = dynamic_cast<PointerEvent*>(&event)) {
            lastPointerPosition_ = pointer->localPosition();
            setState(ViewState::Hovered, true);
            ripple_.pointerEntered(pointer->localPosition());
            handled = true;
        }
        break;

    case EventType::PointerLeave:
        setState(ViewState::Pressed, false);
        setState(ViewState::Hovered, false);
        setState(ViewState::Dragged, false);
        ripple_.pointerLeft();
        handled = true;
        break;

    case EventType::PointerDown:
        if (auto* pointer = dynamic_cast<PointerEvent*>(&event)) {
            lastPointerPosition_ = pointer->localPosition();
            if (pointer->button() == Qt::LeftButton) {
                setState(ViewState::Pressed, true);
                ripple_.pointerPressed(pointer->localPosition());
                handled = true;
            }
        }
        break;

    case EventType::PointerUp:
        setState(ViewState::Pressed, false);
        ripple_.pointerReleased(lastPointerPosition_);
        handled = true;
        break;

    case EventType::PointerMove:
        if (auto* pointer = dynamic_cast<PointerEvent*>(&event)) {
            lastPointerPosition_ = pointer->localPosition();
            ripple_.pointerMoved(pointer->localPosition());
            handled = true;
        }
        break;

    case EventType::KeyDown:
        if (auto* key = dynamic_cast<KeyEvent*>(&event)) {
            if (isActivationKey(key->key())) {
                setState(ViewState::Pressed, true);
                handled = true;
            }
        }
        break;

    case EventType::KeyUp:
        if (auto* key = dynamic_cast<KeyEvent*>(&event); key && isActivationKey(key->key())) {
            setState(ViewState::Pressed, false);
            handled = true;
        }
        break;

    case EventType::FocusIn:
        setState(ViewState::Focused, focusable());
        handled = true;
        break;

    case EventType::FocusOut:
        setState(ViewState::Focused, false);
        handled = true;
        break;

    case EventType::Wheel:
    case EventType::None:
        break;
    }

    if (handled)
        event.accept();
    return handled;
}

bool View::onInterceptEvent(Event&)
{
    // The base View never claims a descendant's gesture. A container that
    // scrolls will override this to steal a drag.
    return false;
}

void View::paintEvent(QPaintEvent*)
{
    // A content hook that throws is caught here. The background has already
    // been painted by stage 2 when that happens, the remaining stages are
    // skipped, and the next update paints a complete frame again; guard()
    // reports the failure once per entry point.
    const auto paintStarted = schedule::UiThread::Clock::now();

    static_cast<void>(view::guard(
        "View::paintEvent", kPaintSite, guardReport_, [this] {
            // Keep the ripple's clip in step with the widget even if the
            // geometry changed without a resize event reaching this path.
            updateRippleGeometry();

            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setRenderHint(QPainter::TextAntialiasing, true);

            // The theme is a snapshot, not a reference into the scope that
            // owns it: a store write in paintContent() installs a ThemeScope
            // (onStoreChanged -> ThemeScope::install), which deletes the scope
            // Theme::forWidget() returned. Every stage of one pass must read
            // the theme the pass started with; the next pass resolves the new
            // one.
            const Theme theme = Theme::forWidget(this);
            const QRectF content = contentRect();
            const layer::SurfaceContext context{theme, state_, shape_, elevation_,
                                                backgroundColour_, contentColour_};

            // 1. The shadow, outside the bounds and unclipped by the layer.
            if (elevation_ != Elevation::Level0)
                elevationLayer_.paint(painter, content, context);

            // 2. The container, clipped to the shape.
            backgroundLayer_.paint(painter, content, context);

            // 3. The component's content.
            paintContent(painter, content);

            // 4. The children, for a container.
            paintChildren(painter);

            // 5. The state overlay and the outline.
            if (interactionFeedbackEnabled_)
                foreground_.paint(painter, content, context);

            // 6. Pointer feedback.
            if (interactionFeedbackEnabled_)
                ripple_.paint(painter);

            // 7. The focus ring last, so neither the state layer nor a ripple
            //    can obscure the keyboard indicator. FocusRingLayer itself
            //    checks the Focused bit; the extra focusable() guard keeps a
            //    state flag set by hand on a non-interactive view from drawing
            //    a ring. The ring is painted inside `content` (the layer's
            //    Inside placement): Qt clips a widget's painter to the widget
            //    rectangle, so a ring stroked outside the bounds would show up
            //    only as fragments of its corner arcs. It is a keyboard
            //    indicator: a focus gained by a mouse press uses the state
            //    layer alone, so clicking a component never draws an outline.
            if (hasFocus() && focusable() && keyboardFocus_)
                focusRing_.paint(painter, content, context);
        }));

    if (auto* uiThread = schedule::UiThread::instance()) {
        uiThread->recordPaint(std::chrono::duration_cast<schedule::UiThread::Duration>(
            schedule::UiThread::Clock::now() - paintStarted));
    }
}

void View::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateRippleGeometry();

    // A view can receive a new rectangle from Qt (a QLayout, a QSplitter, or a
    // plain resize) as well as from View::arrange(). The content has to follow
    // both paths: without this, a ViewGroup placed in an ordinary Qt layout
    // would never place its children, because only the framework's own
    // arrange() entry point used to reach arrangeOverride().
    //
    // arrange() already arranges after its setGeometry(); the depth marker
    // suppresses this second call so one arrange() places the content once.
    if (arrangeDepth_ == 0)
        arrangeOverride(contentRect());
}

void View::enterEvent(QEnterEvent* event)
{
    QWidget::enterEvent(event);

    lastPointerPosition_ = event->position();
    dispatchPointer(EventType::PointerEnter, event->position(), event->globalPosition(),
                    Qt::NoButton, Qt::NoButton, event->modifiers(), kNoTimestamp);
    event->accept();
}

void View::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);

    // A Qt leave event carries no position. The last known position gives the
    // framework a meaningful local point; no ripple uses it, but a listener
    // that does must not see a bogus origin.
    dispatchPointer(EventType::PointerLeave, lastPointerPosition_,
                    mapToGlobal(lastPointerPosition_), Qt::NoButton, Qt::NoButton,
                    Qt::NoModifier, kNoTimestamp);
    event->accept();
}

void View::mousePressEvent(QMouseEvent* event)
{
    if (!isEnabled()) {
        event->ignore();
        return;
    }

    PointerEvent mdEvent =
        pointerEventFromMouse(*event, event->position(), kNoTimestamp);
    EventDispatcher::dispatch(mdEvent, *this);
    event->accept();
}

void View::mouseReleaseEvent(QMouseEvent* event)
{
    if (!isEnabled()) {
        event->ignore();
        return;
    }

    PointerEvent mdEvent =
        pointerEventFromMouse(*event, event->position(), kNoTimestamp);
    EventDispatcher::dispatch(mdEvent, *this);
    event->accept();
}

void View::mouseMoveEvent(QMouseEvent* event)
{
    if (!isEnabled()) {
        event->ignore();
        return;
    }

    lastPointerPosition_ = event->position();
    PointerEvent mdEvent =
        pointerEventFromMouse(*event, event->position(), kNoTimestamp);
    EventDispatcher::dispatch(mdEvent, *this);
    event->accept();
}

void View::keyPressEvent(QKeyEvent* event)
{
    KeyEvent mdEvent = keyEventFrom(*event, kNoTimestamp);
    EventDispatcher::dispatch(mdEvent, *this);

    // Only the activation keys are consumed here. Swallowing every key would
    // break Tab traversal, which Qt implements by letting the event bubble out
    // of keyPressEvent.
    if (isActivationKey(event->key()))
        event->accept();
    else
        QWidget::keyPressEvent(event);
}

void View::keyReleaseEvent(QKeyEvent* event)
{
    KeyEvent mdEvent = keyEventFrom(*event, kNoTimestamp);
    EventDispatcher::dispatch(mdEvent, *this);

    if (isActivationKey(event->key()))
        event->accept();
    else
        QWidget::keyReleaseEvent(event);
}

void View::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);

    // The focus ring is a keyboard indicator in the design baseline. A focus
    // that arrived from a mouse press or a popup keeps the state layer as its
    // only feedback, so a click never draws an outline around the component.
    keyboardFocus_ = event->reason() != Qt::MouseFocusReason
        && event->reason() != Qt::PopupFocusReason;

    FocusEvent focusEvent(true);
    EventDispatcher::dispatch(focusEvent, *this);
    event->accept();
}

void View::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);

    keyboardFocus_ = false;

    FocusEvent focusEvent(false);
    EventDispatcher::dispatch(focusEvent, *this);
    event->accept();
}

void View::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);

    switch (event->type()) {
    case QEvent::EnabledChange: {
        const bool on = isEnabled();
        state_.setFlag(ViewState::Enabled, on);

        if (!on) {
            // A disabled view must not keep a highlight whose result the user
            // can no longer see.
            state_.setFlag(ViewState::Hovered, false);
            state_.setFlag(ViewState::Pressed, false);
            state_.setFlag(ViewState::Focused, false);
            state_.setFlag(ViewState::Dragged, false);
        }

        // isEnabled() already reflects the new value: changeEvent runs after
        // QWidget has applied it.
        syncRippleEnabled();
        onEnabledChanged(on);
        requestUpdate();
        break;
    }

    case QEvent::ThemeChange:
        // A new theme may come from a ThemeScope as well as from the
        // application, so the controller is refreshed from forWidget().
        syncRippleAppearance();
        onThemeChanged();
        requestUpdate();
        break;

    default:
        break;
    }
}

bool View::event(QEvent* event)
{
    bool handled = false;

    static_cast<void>(view::guard(
        "View::event", kEventSite, guardReport_, [this, event, &handled] {
            // Attach/detach are parent transitions, not show/hide transitions:
            // a widget can be hidden and shown again without re-resolving its
            // theme or caches.
            if (event->type() == QEvent::ParentChange) {
                const bool attachedNow = parentWidget() != nullptr;

                if (attachedNow && lifecycle_ == LifecycleState::Detached) {
                    lifecycle_ = LifecycleState::Attached;
                    onAttached();
                } else if (!attachedNow && lifecycle_ != LifecycleState::Created
                           && lifecycle_ != LifecycleState::Detached) {
                    lifecycle_ = LifecycleState::Detached;
                    onDetached();
                }
            }

            handled = QWidget::event(event);
        }));

    return handled;
}

void View::showEvent(QShowEvent* event)
{
    // The constructor may already have a parent, in which case no ParentChange
    // arrives; the first show is the point at which the view becomes usable.
    if (lifecycle_ == LifecycleState::Created || lifecycle_ == LifecycleState::Detached) {
        lifecycle_ = LifecycleState::Attached;
        onAttached();
    }

    if (lifecycle_ != LifecycleState::Shown) {
        lifecycle_ = LifecycleState::Shown;
        onShown();
    }

    // hideEvent() suspended the ripple surface so no animation survives the
    // hidden state; showing lifts the suspension and re-applies the full
    // interaction condition.
    ripple_.setSurfaceVisible(true);
    syncRippleEnabled();

    QWidget::showEvent(event);
}

void View::hideEvent(QHideEvent* event)
{
    if (lifecycle_ == LifecycleState::Shown) {
        lifecycle_ = LifecycleState::Hidden;
        onHidden();
    }

    // Hidden means idle: suspension drops every running ripple without
    // changing the interaction state, and a view hidden mid-press must not
    // resume as still pressed.
    ripple_.suspend();
    setState(ViewState::Hovered, false);
    setState(ViewState::Pressed, false);
    setState(ViewState::Dragged, false);

    QWidget::hideEvent(event);
}

LifecycleState View::lifecycleState() const noexcept
{
    return lifecycle_;
}

QString View::lastError() const
{
    return guardReport_.lastError;
}

void View::setMeasuredSize(QSizeF size)
{
    measuredSize_ = QSizeF(std::max(qreal(0.0), size.width()),
                           std::max(qreal(0.0), size.height()));
}

void View::requestUpdate()
{
    // With a UI runtime installed, invalidation is frame work: request the
    // next frame and queue the actual QWidget::update() in its Build phase.
    // requestFrame() coalesces any number of requests in one turn into one
    // frame. Without a runtime the historical behaviour is preserved exactly:
    // update() directly, so headless tests and embedders are unaffected.
    if (auto* uiThread = schedule::UiThread::instance()) {
        const QPointer<View> guard(this);
        uiThread->requestFrame(schedule::FrameRequest::Update);
        uiThread->postTask(schedule::Priority::UiUpdate, [guard] {
            if (guard)
                guard->update();
        });
        return;
    }

    // The guard makes a destroyed view a no-op instead of a dangling update.
    // The direct update() is the fallback contract; the coalesced queue task
    // keeps the historical deferred path (and its pending-count accounting)
    // for embedders that pump the queue explicitly.
    update();
    const QPointer<View> guard(this);
    schedule::TaskQueue::main().postCoalesced(schedule::Priority::UiUpdate, updateKey_,
                                              [guard] {
                                                  if (guard)
                                                      guard->update();
                                              });
}

void View::updateRippleGeometry()
{
    const QRectF bounds = contentRect();
    ripple_.setBounds(bounds);
    ripple_.setCornerRadius(largestRadius(shape_.resolved(bounds)));
}

void View::syncRippleAppearance()
{
    const Theme& theme = Theme::forWidget(this);

    ripple_.setTheme(theme);
    ripple_.setContentColour(contentColour_.isValid() ? contentColour_
                                                      : theme.colours().onSurface);
}

void View::syncRippleEnabled()
{
    // One condition, applied everywhere an input can change: the caller's
    // feedback switch, Qt's enabled state and whether the view can take focus
    // at all. Switching feedback back on must not resurrect an animation on a
    // disabled or NoFocus view.
    ripple_.setEnabled(interactionFeedbackEnabled_ && isEnabled()
                       && focusPolicy() != Qt::NoFocus);
}

void View::dispatchPointer(EventType type, const QPointF& local, const QPointF& global,
                           Qt::MouseButton button, Qt::MouseButtons buttons,
                           Qt::KeyboardModifiers modifiers,
                           std::chrono::milliseconds timestamp)
{
    PointerEvent event(type, local, global, button, buttons, modifiers, timestamp);
    EventDispatcher::dispatch(event, *this);
}

// ---------------------------------------------------------------------------
// Store bindings
// ---------------------------------------------------------------------------

void View::bindStore(Store* store)
{
    if (store == nullptr)
        return;

    for (const QPointer<Store>& bound : stores_) {
        if (bound == store)
            return;
    }

    stores_.append(store);

    connect(store, &Store::valueChanged, this,
            [this, store](const QString& key, const QVariant& value) {
                onStoreChanged(store, key, value);
                emit storeChanged(key, value);
            });

    // Apply the values the store already carries, without re-emitting one
    // storeChanged per key: the binding itself is the notification.
    const QVariantMap values = store->values();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        onStoreChanged(store, it.key(), it.value());

    emit storeBindingChanged();
}

void View::bindStore(const QString& storeId)
{
    bindStore(Store::open(storeId));
}

void View::unbindStore(Store* store)
{
    for (int index = static_cast<int>(stores_.size()) - 1; index >= 0; --index) {
        Store* bound = stores_.at(index).data();
        if (store != nullptr && bound != store)
            continue;
        if (bound != nullptr)
            disconnect(bound, nullptr, this, nullptr);
        stores_.removeAt(index);
    }
    emit storeBindingChanged();
}

void View::unbindAllStores()
{
    unbindStore(nullptr);
}

QList<Store*> View::boundStores() const
{
    QList<Store*> result;
    for (const QPointer<Store>& store : stores_) {
        if (store != nullptr)
            result.append(store.data());
    }
    return result;
}

QString View::localized(const QString& key, const QString& fallback) const
{
    if (Store* language = Store::find(QStringLiteral("md.language")); language != nullptr) {
        const QVariantMap strings =
            language->value(QStringLiteral("strings")).toMap();
        const auto it = strings.constFind(key);
        if (it != strings.constEnd() && !it.value().toString().isEmpty())
            return it.value().toString();
    }
    return fallback;
}

void View::setInteractionFeedbackEnabled(bool enabled)
{
    if (interactionFeedbackEnabled_ == enabled)
        return;

    interactionFeedbackEnabled_ = enabled;
    syncRippleEnabled();
    requestUpdate();
}

bool View::isInteractionFeedbackEnabled() const noexcept
{
    return interactionFeedbackEnabled_;
}

void View::onStoreChanged(Store* store, const QString& key, const QVariant& value)
{
    Q_UNUSED(store);

    if (key == QStringLiteral("theme") && value.canConvert<Theme>())
        ThemeScope::install(this, value.value<Theme>());
}

} // namespace md
