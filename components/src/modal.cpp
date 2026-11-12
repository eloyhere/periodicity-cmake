#include <md/components/modal.h>

#include <md/theme/theme.h>

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
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
    hide();
}

Modal::~Modal()
{
    removeFilters();
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

void Modal::open()
{
    if (open_)
        return;

    host_ = window();
    if (host_ != nullptr && parentWidget() != host_)
        setParent(host_);

    open_ = true;
    applyWindowGeometry();
    registerLayer(this, level_);
    show();
    raiseWithinLayer(this);
    installFilters();
    setFocus(Qt::PopupFocusReason);
    emit opened();
}

void Modal::close()
{
    if (!open_)
        return;

    open_ = false;
    removeFilters();
    unregisterLayer(this);
    hide();
    emit closed();
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
}

void Modal::installFilters()
{
    if (host_ != nullptr)
        host_->installEventFilter(this);
    if (QApplication::instance() != nullptr)
        QApplication::instance()->installEventFilter(this);
}

void Modal::removeFilters()
{
    if (host_ != nullptr)
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

    QColor scrim = scrimColour_;
    if (!scrim.isValid())
        scrim = QColor(Qt::black);
    scrim.setAlphaF(static_cast<float>(std::clamp(dim_, qreal(0.0), qreal(1.0))));

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
    if (event->key() == Qt::Key_Escape && dismissOnEscape_) {
        close();
        event->accept();
        return;
    }

    View::keyPressEvent(event);
}

bool Modal::eventFilter(QObject* watched, QEvent* event)
{
    if (open_ && watched == host_ && event->type() == QEvent::Resize) {
        applyWindowGeometry();
        return false;
    }

    if (open_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape && dismissOnEscape_
            && (host_ == nullptr || topLayer(host_) == this)) {
            close();
            return true;
        }
    }

    if (open_ && event->type() == QEvent::Hide && watched == host_) {
        close();
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
