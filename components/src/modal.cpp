#include <md/components/modal.h>

#include <md/animation/animator.h>
#include <md/components/container_transform.h>
#include <md/theme/theme.h>

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPoint>
#include <QPointer>
#include <QSizeF>
#include <QWidget>

#include <algorithm>

namespace md {
namespace {

constexpr const char* kLayerProperty = "md.layerLevel";

bool isLayer(const QWidget* widget)
{
    return widget != nullptr && widget->property(kLayerProperty).isValid();
}

} // namespace

Modal::Modal(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setShape(Shape::none());
    setBackgroundColour(QColor(Qt::transparent));
    setScrimColour(Theme::forWidget(this).colours().scrim);

    scrimAnimator_ = new animation::Animator(this);
    connect(scrimAnimator_, &animation::Animator::finished, this, [this] {
        scrimDone_ = true;
        if (phase_ == Phase::Opening)
            maybeFinishOpen();
        else if (phase_ == Phase::Closing)
            maybeFinishClose();
    });

    hide();
}

Modal::~Modal()
{
    removeFilters();

    // The card is a child of the modal and would be deleted after this body;
    // detach the effect while the card is still alive so the transform never
    // touches a half-destroyed widget.
    delete cardTransform_;
    cardTransform_ = nullptr;
}

void Modal::setLevel(Level level)
{
    setLevelValue(static_cast<int>(level));
}

void Modal::setLevelValue(int level)
{
    if (level_ == level)
        return;

    level_ = level;
    if (open_) {
        registerLayer(this, level_);
        raiseWithinLayer(this);
    }
}

int Modal::levelValue() const
{
    return level_;
}

void Modal::setDim(qreal dim)
{
    const qreal clamped = std::clamp(dim, qreal(0.0), qreal(1.0));
    if (qFuzzyCompare(dim_ + 1.0, clamped + 1.0))
        return;

    dim_ = clamped;
    requestUpdate();
}

qreal Modal::dim() const noexcept
{
    return dim_;
}

void Modal::setScrimColour(const QColor& colour)
{
    if (scrimColour_ == colour)
        return;
    scrimColour_ = colour;
    requestUpdate();
}

QColor Modal::scrimColour() const
{
    return scrimColour_;
}

void Modal::setDismissOnClick(bool dismiss)
{
    dismissOnClick_ = dismiss;
}

bool Modal::dismissesOnClick() const noexcept
{
    return dismissOnClick_;
}

void Modal::setDismissOnEscape(bool dismiss)
{
    dismissOnEscape_ = dismiss;
}

bool Modal::dismissesOnEscape() const noexcept
{
    return dismissOnEscape_;
}

void Modal::setCard(QWidget* card)
{
    if (card_ == card)
        return;

    delete cardTransform_;
    cardTransform_ = nullptr;

    if (card_ != nullptr)
        delete card_.data();

    card_ = card;
    if (card_ == nullptr)
        return;

    card_->setParent(this);
    card_->hide();

    cardTransform_ = new ContainerTransform(card_, this);
    connect(cardTransform_, &ContainerTransform::finished, this, [this](bool entered) {
        if (entered && phase_ == Phase::Opening) {
            cardDone_ = true;
            maybeFinishOpen();
        } else if (!entered && phase_ == Phase::Closing) {
            cardDone_ = true;
            maybeFinishClose();
        }
    });

    if (open_) {
        card_->show();
        positionCard();
    }
}

QWidget* Modal::card() const noexcept
{
    return card_.data();
}

void Modal::setCardRect(const QRectF& rect)
{
    hasExplicitCardRect_ = !rect.isEmpty();
    explicitCardRect_ = rect;
    if (open_)
        positionCard();
}

QRectF Modal::cardRect() const
{
    if (hasExplicitCardRect_)
        return explicitCardRect_;
    if (card_ != nullptr)
        return QRectF(card_->geometry());
    return {};
}

void Modal::setAnchor(QWidget* anchor)
{
    anchor_ = anchor;
    hasAnchorRect_ = false;
    if (open_)
        positionCard();
}

void Modal::setAnchorRect(const QRectF& globalRect)
{
    anchorRect_ = globalRect;
    hasAnchorRect_ = !globalRect.isEmpty();
    anchor_ = nullptr;
    if (open_)
        positionCard();
}

QWidget* Modal::anchor() const noexcept
{
    return anchor_.data();
}

void Modal::setFocusReturn(QWidget* widget)
{
    focusReturn_ = widget;
}

QWidget* Modal::focusReturn() const noexcept
{
    return focusReturn_.data();
}

void Modal::setAnimated(bool animated)
{
    animated_ = animated;
}

bool Modal::isAnimated() const noexcept
{
    return animated_;
}

bool Modal::isAnimating() const noexcept
{
    return phase_ == Phase::Opening || phase_ == Phase::Closing;
}

qreal Modal::scrimOpacity() const noexcept
{
    return scrimOpacity_;
}

void Modal::open()
{
    if (open_ || phase_ == Phase::Closing)
        return;

    host_ = window();
    if (host_ != nullptr && host_ != this && parentWidget() != host_)
        setParent(host_);

    open_ = true;
    closeEmitted_ = false;
    phase_ = Phase::Idle;
    if (focusReturn_.isNull())
        previousFocus_ = QApplication::focusWidget();

    applyWindowGeometry();
    if (card_ != nullptr) {
        card_->show();
        positionCard();
    }

    registerLayer(this, level_);
    show();
    raiseWithinLayer(this);
    installFilters();

    setFocus(Qt::PopupFocusReason);
    const QList<QWidget*> focusables = focusableWidgets();
    if (!focusables.isEmpty() && focusables.first() != this)
        focusables.first()->setFocus(Qt::PopupFocusReason);

    QPointer<Modal> self(this);
    emit opened();
    if (self.isNull() || !open_ || phase_ == Phase::Closing)
        return;

    // The phase must not be Opening while the card is snapped to its start,
    // otherwise the synchronous finished(false) would be mistaken for an exit.
    if (cardTransform_ != nullptr)
        cardTransform_->snapTo(false);

    startEntrance();
}

void Modal::close()
{
    if (!open_ || phase_ == Phase::Closing)
        return;

    open_ = false;
    phase_ = Phase::Closing;

    QPointer<Modal> self(this);
    if (!closeEmitted_) {
        closeEmitted_ = true;
        emit closed();
    }
    if (self.isNull())
        return;

    restoreFocus();

    if (self.isNull() || phase_ != Phase::Closing)
        return;
    startExit();
}

bool Modal::isOpen() const noexcept
{
    return open_;
}

QRectF Modal::scrimRect() const
{
    return QRectF(rect());
}

void Modal::applyWindowGeometry()
{
    if (host_ == nullptr)
        return;
    setGeometry(host_->rect());
    positionCard();
}

void Modal::positionCard()
{
    if (card_ == nullptr)
        return;

    const Density& density = Theme::forWidget(this).density();
    QSizeF size = hasExplicitCardRect_ ? explicitCardRect_.size() : QSizeF(card_->sizeHint());
    if (size.isEmpty())
        size = QSizeF(card_->size());
    if (size.isEmpty())
        size = QSizeF(card_->minimumSize());
    if (size.isEmpty())
        size = QSizeF(density.dp(320.0), density.dp(240.0));

    const QRectF modalRect(rect());
    QRectF target;

    if (hasExplicitCardRect_) {
        target = explicitCardRect_;
    } else {
        const qreal margin = density.dp(16.0);
        const QRectF anchor = effectiveAnchorRect();

        if (anchor.isValid() && !anchor.isEmpty()) {
            const qreal gap = density.dp(8.0);
            const qreal width = qMax(size.width(), anchor.width());
            const qreal height = size.height();

            qreal x = anchor.center().x() - width / 2.0;
            qreal y = anchor.bottom() + gap;
            if (y + height > modalRect.bottom() - margin)
                y = anchor.top() - gap - height;

            const qreal minX = modalRect.left() + margin;
            const qreal maxX = qMax(minX, modalRect.right() - margin - width);
            const qreal minY = modalRect.top() + margin;
            const qreal maxY = qMax(minY, modalRect.bottom() - margin - height);
            x = std::clamp(x, minX, maxX);
            y = std::clamp(y, minY, maxY);

            target = QRectF(QPointF(x, y), QSizeF(width, height));
        } else {
            target = QRectF(modalRect.center() - QPointF(size.width() / 2.0,
                                                          size.height() / 2.0),
                            size);
        }
    }

    card_->setGeometry(target.toRect());
}

QRectF Modal::effectiveAnchorRect() const
{
    QRectF global;
    if (hasAnchorRect_)
        global = anchorRect_;
    else if (anchor_ != nullptr)
        global = QRectF(anchor_->mapToGlobal(QPoint(0, 0)), QSizeF(anchor_->size()));

    if (!global.isValid() || global.isEmpty())
        return {};

    const QPoint topLeft = mapFromGlobal(global.topLeft().toPoint());
    const QPoint bottomRight = mapFromGlobal(global.bottomRight().toPoint());
    return QRectF(QPointF(topLeft), QPointF(bottomRight)).normalized();
}

void Modal::startEntrance()
{
    phase_ = Phase::Opening;
    scrimDone_ = false;
    cardDone_ = (cardTransform_ == nullptr);
    scrimOpacity_ = 0.0;
    requestUpdate();

    const bool animate = animated_;
    animateScrim(true, animate);
    if (cardTransform_ != nullptr)
        cardTransform_->enter(animate);

    maybeFinishOpen();
}

void Modal::startExit()
{
    if (phase_ != Phase::Closing)
        return;

    scrimDone_ = false;
    cardDone_ = (cardTransform_ == nullptr);

    const bool animate = animated_;
    animateScrim(false, animate);
    if (cardTransform_ != nullptr)
        cardTransform_->exit(animate);

    maybeFinishClose();
}

void Modal::animateScrim(bool visible, bool animate)
{
    scrimAnimator_->stop();

    const qreal target = visible ? 1.0 : 0.0;
    MotionSpec spec = Theme::forWidget(this).motion().level(MotionLevel::Level1);
    // The entrance is a plain standard fade; the exit shares the card's
    // emphasized-accelerate curve so the whole surface leaves together.
    if (!visible)
        spec.easing = easingCurve(Easing::EmphasizedAccelerate);

    if (!animate || spec.duration.count() <= 0 || !isVisible()) {
        scrimOpacity_ = target;
        scrimDone_ = true;
        requestUpdate();
        return;
    }

    const qreal from = scrimOpacity_;
    scrimDone_ = false;
    scrimAnimator_->start(spec, [this, from, target](qreal fraction) {
        const qreal amount = std::clamp(fraction, qreal(0.0), qreal(1.0));
        scrimOpacity_ = from + (target - from) * amount;
        requestUpdate();
    });
}

void Modal::maybeFinishOpen()
{
    if (phase_ != Phase::Opening || !scrimDone_ || !cardDone_)
        return;

    phase_ = Phase::Open;
    emit openFinished();
}

void Modal::maybeFinishClose()
{
    if (phase_ != Phase::Closing || !scrimDone_ || !cardDone_)
        return;

    finishClose();
}

void Modal::finishClose()
{
    if (phase_ != Phase::Closing)
        return;

    // The phase is cleared before hide() so hideEvent() sees an intentional
    // hide and does not abort a second time.
    phase_ = Phase::Idle;
    scrimAnimator_->stop();
    if (cardTransform_ != nullptr)
        cardTransform_->stop();
    scrimOpacity_ = 0.0;
    open_ = false;

    removeFilters();
    unregisterLayer(this);
    hide();

    emit closeFinished();
}

void Modal::abort()
{
    if (phase_ == Phase::Idle)
        return;

    phase_ = Phase::Idle;
    scrimAnimator_->stop();
    if (cardTransform_ != nullptr) {
        cardTransform_->stop();
        cardTransform_->snapTo(false);
    }
    scrimOpacity_ = 0.0;
    open_ = false;

    if (!closeEmitted_) {
        closeEmitted_ = true;
        emit closed();
    }

    removeFilters();
    unregisterLayer(this);
    restoreFocus();

    emit closeFinished();
}

void Modal::restoreFocus()
{
    QWidget* target = focusReturn_.data();
    if (target == nullptr)
        target = previousFocus_.data();

    if (target != nullptr && (target == this || isAncestorOf(target)))
        target = nullptr;

    if (target != nullptr && target->isVisible() && target->isEnabled()) {
        target->setFocus(Qt::OtherFocusReason);
        return;
    }

    if (host_ != nullptr && host_ != this && host_->isVisible())
        host_->setFocus(Qt::OtherFocusReason);
}

QList<QWidget*> Modal::focusableWidgets() const
{
    QList<QWidget*> widgets;
    const auto add = [&widgets, this](QWidget* widget) {
        if (widget != nullptr && widget->isVisibleTo(this) && widget->isEnabled()
            && (widget->focusPolicy() & Qt::TabFocus)) {
            widgets.append(widget);
        }
    };

    if (card_ != nullptr) {
        add(card_);
        for (QWidget* child : card_->findChildren<QWidget*>())
            add(child);
    }

    if (widgets.isEmpty() && (focusPolicy() & Qt::TabFocus))
        widgets.append(const_cast<Modal*>(this));

    return widgets;
}

bool Modal::trapFocus(QKeyEvent* event)
{
    if (event->key() != Qt::Key_Tab && event->key() != Qt::Key_Backtab)
        return false;

    const QList<QWidget*> widgets = focusableWidgets();
    if (widgets.isEmpty()) {
        setFocus(Qt::TabFocusReason);
        return true;
    }

    QWidget* current = QApplication::focusWidget();
    int index = widgets.indexOf(current);
    const bool backwards = event->key() == Qt::Key_Backtab
                           || event->modifiers().testFlag(Qt::ShiftModifier);

    if (index < 0)
        index = 0;
    else
        index = (index + (backwards ? -1 : 1) + widgets.size()) % widgets.size();

    widgets.at(index)->setFocus(Qt::TabFocusReason);
    return true;
}

void Modal::installFilters()
{
    if (host_ != nullptr && host_ != this)
        host_->installEventFilter(this);
    if (QApplication::instance() != nullptr)
        QApplication::instance()->installEventFilter(this);
}

void Modal::removeFilters()
{
    if (host_ != nullptr && host_ != this)
        host_->removeEventFilter(this);
    if (QApplication::instance() != nullptr)
        QApplication::instance()->removeEventFilter(this);
}

void Modal::measureOverride(MeasureSpec width, MeasureSpec height)
{
    setMeasuredSize(QSizeF(width.resolve(host_ != nullptr ? host_->width() : 0.0),
                           height.resolve(host_ != nullptr ? host_->height() : 0.0)));
}

void Modal::paintContent(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);

    const Theme& theme = Theme::forWidget(this);
    QColor scrim = scrimColour_;
    if (!scrim.isValid())
        scrim = theme.colours().scrim;
    if (!scrim.isValid())
        scrim = theme.colours().onSurface;

    scrim.setAlphaF(static_cast<float>(std::clamp(dim_ * scrimOpacity_, qreal(0.0), qreal(1.0))));

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(scrim);
    painter.drawRect(rect());
    painter.restore();
}

void Modal::mousePressEvent(QMouseEvent* event)
{
    View::mousePressEvent(event);

    if (event->button() != Qt::LeftButton)
        return;

    emit clicked();
    event->accept();

    if (dismissOnClick_)
        close();
}

void Modal::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && dismissOnEscape_ && open_) {
        close();
        event->accept();
        return;
    }

    View::keyPressEvent(event);
}

void Modal::hideEvent(QHideEvent* event)
{
    View::hideEvent(event);

    // A host or parent hiding the modal mid-flight ends the transition at
    // once: no animator keeps ticking against a hidden widget.
    if (phase_ != Phase::Idle)
        abort();
}

bool Modal::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == host_ && event->type() == QEvent::Resize && phase_ != Phase::Idle) {
        applyWindowGeometry();
        return false;
    }

    if (open_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);

        if (trapFocus(key))
            return true;

        if (key->key() == Qt::Key_Escape && dismissOnEscape_
            && (host_ == nullptr || topLayer(host_) == this)) {
            close();
            return true;
        }
    }

    if (watched == host_ && event->type() == QEvent::Hide && phase_ != Phase::Idle) {
        abort();
        return false;
    }

    return View::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// Layer registry
// ---------------------------------------------------------------------------

void Modal::registerLayer(QWidget* layer, Level level)
{
    registerLayer(layer, static_cast<int>(level));
}

void Modal::registerLayer(QWidget* layer, int level)
{
    if (layer == nullptr)
        return;

    layer->setProperty(kLayerProperty, level);

    QWidget* host = layer->window();
    if (host != nullptr && host != layer)
        restack(host);
}

void Modal::unregisterLayer(QWidget* layer)
{
    if (layer == nullptr)
        return;

    layer->setProperty(kLayerProperty, QVariant());
    QWidget* host = layer->window();
    if (host != nullptr && host != layer)
        restack(host);
}

int Modal::layerOf(const QWidget* layer)
{
    if (!isLayer(layer))
        return static_cast<int>(Level::Content);
    return layer->property(kLayerProperty).toInt();
}

void Modal::raiseWithinLayer(QWidget* layer)
{
    if (!isLayer(layer))
        return;

    QWidget* host = layer->window();
    if (host == nullptr || host == layer)
        return;

    const int level = layerOf(layer);
    layer->raise();

    QList<QWidget*> higher;
    for (QWidget* child : host->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
        if (child != layer && isLayer(child) && layerOf(child) > level)
            higher.append(child);
    }

    std::stable_sort(higher.begin(), higher.end(), [](const QWidget* lhs, const QWidget* rhs) {
        return layerOf(lhs) < layerOf(rhs);
    });

    for (QWidget* child : higher)
        child->raise();
}

QWidget* Modal::topLayer(QWidget* window)
{
    if (window == nullptr)
        return nullptr;

    QWidget* top = nullptr;
    int topLevel = static_cast<int>(Level::Content);
    for (QWidget* child : window->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
        if (!isLayer(child) || !child->isVisible())
            continue;

        const int level = layerOf(child);
        if (level >= topLevel) {
            topLevel = level;
            top = child;
        }
    }
    return top;
}

void Modal::restack(QWidget* window)
{
    if (window == nullptr)
        return;

    QList<QWidget*> layers;
    for (QWidget* child : window->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
        if (isLayer(child))
            layers.append(child);
    }

    std::stable_sort(layers.begin(), layers.end(), [](const QWidget* lhs, const QWidget* rhs) {
        return layerOf(lhs) < layerOf(rhs);
    });

    for (QWidget* layer : layers)
        layer->raise();
}

} // namespace md
