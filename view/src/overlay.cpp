#include <md/view/overlay.h>

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QList>
#include <QMetaObject>
#include <QPointer>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace md {
namespace {

// The events that carry an interaction. Everything else — paint, resize,
// enter/leave, hover, focus, show/hide, enabled changes — is a notification
// and is forwarded; blocking those would corrupt the widget state rather than
// protect the top surface.
bool isInteractionEvent(QEvent::Type type)
{
    switch (type) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::ContextMenu:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease:
    case QEvent::DragEnter:
    case QEvent::DragMove:
    case QEvent::DragLeave:
    case QEvent::Drop:
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::ShortcutOverride:
    case QEvent::Shortcut:
        return true;
    default:
        return false;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

Overlay* Overlay::forWidget(QWidget* widget)
{
    if (widget == nullptr)
        return nullptr;

    QWidget* window = widget->window();
    if (window == nullptr)
        return nullptr;

    if (auto* existing = window->findChild<Overlay*>(QStringLiteral("md.overlay"),
                                                      Qt::FindDirectChildrenOnly)) {
        return existing;
    }

    return new Overlay(window);
}

Overlay::Overlay(QWidget* window)
    : QObject(window)
    , window_(window)
{
    setObjectName(QStringLiteral("md.overlay"));

    if (window != nullptr) {
        // A QObject child of the window already dies with it. The flag only
        // tells the surface-destroyed callbacks that the widget tree is on its
        // way out, so they must not touch it.
        connect(window, &QObject::destroyed, this, [this] {
            windowDying_ = true;
            removeApplicationFilter();
        });
    }
}

Overlay::~Overlay()
{
    // This object is normally destroyed by the window it is parented to. By
    // then the window's QWidget half is already gone, and so is most of the
    // content tree: nothing here may call a method on window_, host_ or a
    // content widget. The application filter, which outlives both, must go.
    removeApplicationFilter();
    entries_.clear();
    content_.clear();
    window_ = nullptr;
    host_ = nullptr;
}

// ---------------------------------------------------------------------------
// Stack
// ---------------------------------------------------------------------------

bool Overlay::push(QWidget* surface, Options options)
{
    QWidget* window = window_.data();
    if (window == nullptr || windowDying_ || surface == nullptr || surface == window)
        return false;

    if (contains(surface))
        return false;

    // A surface belongs to the window it is pushed onto. Rejecting a foreign
    // surface is safer than silently stealing it from another window.
    if (surface->window() != window)
        return false;

    const bool outermost = entries_.isEmpty();
    if (outermost)
        captureFocus(options);

    if (outermost) {
        ensureHost();
        if (host_ == nullptr)
            return false;

        host_->setGeometry(window->rect());
        host_->show();
        // Raised once per overlay session: this is the z-order floor every
        // surface is lifted above. Individual pushes never raise it again.
        host_->raise();
    }

    if (surface->parentWidget() != window)
        surface->setParent(window);

    Entry entry;
    entry.id = nextId_++;
    entry.surface = surface;
    entry.options = options;
    entries_.append(entry);

    if (options.fillWindow)
        surface->setGeometry(window->rect());

    // The stack order is the z order: the newest surface is lifted above the
    // host, the content and every older surface. Creation order and the
    // sibling list are irrelevant.
    surface->raise();

    connect(surface, &QObject::destroyed, this, [this, id = entry.id] {
        removeEntry(id);
    });

    updateContentDisabled();
    installApplicationFilter();

    emit changed();
    return true;
}

bool Overlay::pop(QWidget* surface)
{
    const int index = indexOf(surface);
    if (index < 0)
        return false;

    entries_.removeAt(index);
    updateContentDisabled();

    if (entries_.isEmpty()) {
        if (restoreFocusOnPop_)
            restoreFocus();

        focusBefore_.clear();
        invoker_.clear();
        restoreFocusOnPop_ = true;
        invokerFirst_ = false;

        // A destruction queued a restore for this same empty window; it is
        // stale now that the pop has restored synchronously.
        ++restoreToken_;
        restorePending_ = false;

        if (host_ != nullptr)
            host_->hide();
        removeApplicationFilter();
    }

    emit changed();
    return true;
}

void Overlay::removeEntry(quint64 id)
{
    for (int index = 0; index < static_cast<int>(entries_.size()); ++index) {
        if (entries_.at(index).id != id)
            continue;

        entries_.removeAt(index);

        if (windowDying_ || window_.isNull()) {
            emit changed();
            return;
        }

        // QWidget emits destroyed() from inside ~QWidget, before the widget's
        // children are deleted. Restoring focus or the content root here would
        // call into a tree that is still being taken apart (and the dying
        // widget is still a visible child of the window, so the focus fallback
        // could target it). Leave the restore to the next event-loop turn,
        // when the surface is really gone.
        if (!restorePending_) {
            restorePending_ = true;
            const quint64 token = ++restoreToken_;
            QMetaObject::invokeMethod(
                this,
                [this, token] {
                    if (token != restoreToken_)
                        return;
                    finishAfterSurfaceGone();
                },
                Qt::QueuedConnection);
        }

        emit changed();
        return;
    }
}

void Overlay::finishAfterSurfaceGone()
{
    restorePending_ = false;

    QWidget* window = window_.data();
    if (window == nullptr || windowDying_)
        return;

    updateContentDisabled();

    // A surface can have been pushed while the restore was queued; in that
    // case the stack never became empty and nothing is restored early.
    if (!entries_.isEmpty())
        return;

    if (restoreFocusOnPop_)
        restoreFocus();

    focusBefore_.clear();
    invoker_.clear();
    restoreFocusOnPop_ = true;
    invokerFirst_ = false;

    if (host_ != nullptr)
        host_->hide();
    removeApplicationFilter();
}

bool Overlay::setContentDisabled(QWidget* surface, bool disabled)
{
    const int index = indexOf(surface);
    if (index < 0)
        return false;

    if (entries_.at(index).options.disableContent == disabled)
        return true;

    // QList::at() returns a const reference in every case, so the write needs
    // operator[] here; the index was validated above and the list is not
    // reallocated between the two statements.
    entries_[index].options.disableContent = disabled;
    if (!windowDying_)
        updateContentDisabled();
    return true;
}

QWidget* Overlay::top() const
{
    if (entries_.isEmpty())
        return nullptr;
    return entries_.constLast().surface.data();
}

bool Overlay::contains(const QWidget* surface) const
{
    return indexOf(surface) >= 0;
}

int Overlay::count() const
{
    return static_cast<int>(entries_.size());
}

int Overlay::depthOf(const QWidget* surface) const
{
    return indexOf(surface);
}

QWidget* Overlay::hostWidget() const
{
    return host_.data();
}

QWidget* Overlay::window() const
{
    return window_.data();
}

bool Overlay::blocksInput() const
{
    return !entries_.isEmpty() && entries_.constLast().options.blockInput;
}

int Overlay::indexOf(const QWidget* surface) const
{
    if (surface == nullptr)
        return -1;

    for (int index = 0; index < static_cast<int>(entries_.size()); ++index) {
        if (entries_.at(index).surface.data() == surface)
            return index;
    }
    return -1;
}

bool Overlay::isSurface(const QWidget* widget) const
{
    return indexOf(widget) >= 0;
}

// ---------------------------------------------------------------------------
// The host widget
// ---------------------------------------------------------------------------

void Overlay::ensureHost()
{
    QWidget* window = window_.data();
    if (window == nullptr || windowDying_ || host_ != nullptr)
        return;

    auto* host = new QWidget(window);
    host->setObjectName(QStringLiteral("md.overlayHost"));
    // The host is a z-order floor, not a catcher: input blocking is the event
    // filter's job, so the host must never take a mouse event away from the
    // surface or (when nothing blocks) from the content.
    host->setAttribute(Qt::WA_NoSystemBackground, true);
    host->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    host->setFocusPolicy(Qt::NoFocus);
    host->setGeometry(window->rect());
    host_ = host;
}

void Overlay::updateHostGeometry()
{
    QWidget* window = window_.data();
    if (window == nullptr || windowDying_)
        return;

    if (host_ != nullptr) {
        if (host_->geometry() != window->rect())
            host_->setGeometry(window->rect());
        if (!entries_.isEmpty() && host_->isHidden())
            host_->show();
    }

    for (const Entry& entry : std::as_const(entries_)) {
        QWidget* surface = entry.surface.data();
        if (surface == nullptr || !entry.options.fillWindow)
            continue;
        if (surface->geometry() != window->rect())
            surface->setGeometry(window->rect());
    }
}

// ---------------------------------------------------------------------------
// Content root
// ---------------------------------------------------------------------------

void Overlay::updateContentDisabled()
{
    QWidget* window = window_.data();
    if (window == nullptr || windowDying_)
        return;

    bool wanted = false;
    for (const Entry& entry : std::as_const(entries_)) {
        if (entry.options.disableContent) {
            wanted = true;
            break;
        }
    }

    if (wanted == contentDisabled_)
        return;

    if (wanted) {
        content_.clear();

        const QList<QWidget*> children =
            window->findChildren<QWidget*>(Qt::FindDirectChildrenOnly);
        for (QWidget* child : children) {
            if (child == nullptr || child == host_.data())
                continue;
            // A transient popup is a window of its own, not part of the
            // content tree.
            if (child->isWindow())
                continue;
            // A surface that is already on the stack is not content. It was
            // reparented to the window by push() before this runs.
            if (isSurface(child))
                continue;

            content_.append(ContentEntry{child, child->isEnabled()});
            if (child->isEnabled())
                child->setEnabled(false);
        }

        contentDisabled_ = true;
        return;
    }

    // The exact per-root state goes back. Descendants were never touched, so a
    // child that was already disabled keeps its own disabled flag and stays
    // disabled when its root is re-enabled.
    for (const ContentEntry& entry : std::as_const(content_)) {
        if (entry.widget != nullptr)
            entry.widget->setEnabled(entry.wasEnabled);
    }
    content_.clear();
    contentDisabled_ = false;
}

// ---------------------------------------------------------------------------
// Focus
// ---------------------------------------------------------------------------

void Overlay::captureFocus(const Options& options)
{
    restoreFocusOnPop_ = options.restoreFocus;
    invokerFirst_ = options.invokerFirst;
    focusBefore_.clear();
    invoker_.clear();

    if (!options.restoreFocus)
        return;

    QWidget* window = window_.data();
    QWidget* focused = QApplication::focusWidget();
    if (focused != nullptr && window != nullptr && focused->window() == window)
        focusBefore_ = focused;

    invoker_ = options.invoker;
}

bool Overlay::isFocusTarget(QWidget* widget) const
{
    QWidget* window = window_.data();
    if (widget == nullptr || window == nullptr || windowDying_)
        return false;
    if (widget->window() != window)
        return false;
    if (isSurface(widget))
        return false;
    return widget->isVisible() && widget->isEnabled();
}

void Overlay::restoreFocus()
{
    QWidget* window = window_.data();
    if (window == nullptr || windowDying_)
        return;

    QWidget* target = invokerFirst_ ? invoker_.data() : focusBefore_.data();
    if (!isFocusTarget(target))
        target = invokerFirst_ ? focusBefore_.data() : invoker_.data();
    if (!isFocusTarget(target)) {
        // The content root: the first visible, focusable direct child of the
        // window that is not part of the overlay machinery.
        const QList<QWidget*> children =
            window->findChildren<QWidget*>(Qt::FindDirectChildrenOnly);
        for (QWidget* child : children) {
            if (child == nullptr || child == host_.data() || child->isWindow())
                continue;
            if (isSurface(child))
                continue;
            if (child->isVisible() && child->isEnabled()
                && child->focusPolicy() != Qt::NoFocus) {
                target = child;
                break;
            }
        }
    }
    if (!isFocusTarget(target))
        target = window;

    if (target != nullptr && target->isVisible() && target->isEnabled())
        target->setFocus(Qt::OtherFocusReason);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void Overlay::installApplicationFilter()
{
    if (filterInstalled_)
        return;

    QCoreApplication* application = QCoreApplication::instance();
    if (application == nullptr)
        return;

    application->installEventFilter(this);
    filterInstalled_ = true;
}

void Overlay::removeApplicationFilter()
{
    if (!filterInstalled_)
        return;

    filterInstalled_ = false;
    QCoreApplication* application = QCoreApplication::instance();
    if (application != nullptr)
        application->removeEventFilter(this);
}

bool Overlay::eventFilter(QObject* watched, QEvent* event)
{
    QWidget* window = window_.data();

    if (watched == window && window != nullptr) {
        if (event->type() == QEvent::Resize) {
            updateHostGeometry();
            return false;
        }
        if (event->type() == QEvent::Hide) {
            // Not a teardown: the surfaces decide whether a hidden window
            // closes them or merely settles their motion.
            emit windowHidden();
            return false;
        }
        if (event->type() == QEvent::Destroy) {
            windowDying_ = true;
            // The filter is application-wide; drop it before the widget tree
            // is taken apart so no event is filtered against a dying window.
            removeApplicationFilter();
            return false;
        }
    }

    if (windowDying_ || entries_.isEmpty())
        return false;

    auto* target = qobject_cast<QWidget*>(watched);
    if (target == nullptr || target->window() != window)
        return false;

    QWidget* surface = entries_.constLast().surface.data();
    if (surface == nullptr)
        return false;

    // The top surface and everything inside it keep their own event handling:
    // Tab reaches the surface's focus trap, Escape its dismissal policy.
    if (target == surface || surface->isAncestorOf(target))
        return false;

    if (!entries_.constLast().options.blockInput)
        return false;

    if (!isInteractionEvent(event->type()))
        return false;

    event->accept();
    return true;
}

} // namespace md
