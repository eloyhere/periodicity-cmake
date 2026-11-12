#include <md/components/dropdown.h>

#include <md/components/modal.h>
#include <md/theme/theme.h>

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QScreen>
#include <QWidget>

#include <algorithm>

namespace md {
namespace {

constexpr qreal kShadowMarginDp = 8.0;
constexpr qreal kPanelRadiusDp = 12.0;
constexpr qreal kAnchorGapDp = 4.0;

} // namespace

Dropdown::Dropdown(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setShape(Shape::none());
    setBackgroundColour(QColor(Qt::transparent));
    hide();
}

Dropdown::~Dropdown()
{
    removeFilters();
    if (anchor_ != nullptr)
        anchor_->removeEventFilter(this);
}

void Dropdown::setAnchor(QWidget* anchor)
{
    if (anchor_ == anchor)
        return;

    if (open_)
        close();

    if (anchor_ != nullptr)
        anchor_->removeEventFilter(this);

    anchor_ = anchor;

    if (anchor_ != nullptr)
        anchor_->installEventFilter(this);
}

QWidget* Dropdown::anchor() const
{
    return anchor_.data();
}

void Dropdown::setContent(View* content)
{
    if (content_ == content)
        return;

    if (content_ != nullptr)
        content_->setParent(nullptr);

    content_ = content;

    if (content_ != nullptr) {
        content_->setParent(this);
        content_->hide();
    }
}

View* Dropdown::content() const
{
    return content_;
}

void Dropdown::setPlacement(Placement placement)
{
    if (placement_ == placement)
        return;
    placement_ = placement;
    if (open_)
        reposition();
}

Dropdown::Placement Dropdown::placement() const noexcept
{
    return placement_;
}

Dropdown::Placement Dropdown::effectivePlacement() const noexcept
{
    return effective_;
}

void Dropdown::setOffset(const QPointF& offset)
{
    if (offset_ == offset)
        return;
    offset_ = offset;
    if (open_)
        reposition();
}

QPointF Dropdown::offset() const noexcept
{
    return offset_;
}

void Dropdown::setMatchAnchorWidth(bool match)
{
    if (matchAnchorWidth_ == match)
        return;
    matchAnchorWidth_ = match;
    if (open_)
        reposition();
}

bool Dropdown::matchesAnchorWidth() const noexcept
{
    return matchAnchorWidth_;
}

void Dropdown::setMinimumWidthDp(qreal widthDp)
{
    const qreal value = std::max(qreal(0.0), widthDp);
    if (qFuzzyCompare(minimumWidthDp_ + 1.0, value + 1.0))
        return;
    minimumWidthDp_ = value;
    if (open_)
        reposition();
}

qreal Dropdown::minimumWidthDp() const noexcept
{
    return minimumWidthDp_;
}

void Dropdown::setMaximumHeightDp(qreal heightDp)
{
    const qreal value = std::max(qreal(0.0), heightDp);
    if (qFuzzyCompare(maximumHeightDp_ + 1.0, value + 1.0))
        return;
    maximumHeightDp_ = value;
    if (open_)
        reposition();
}

qreal Dropdown::maximumHeightDp() const noexcept
{
    return maximumHeightDp_;
}

void Dropdown::setDismissOnOutsideClick(bool dismiss)
{
    dismissOnOutsideClick_ = dismiss;
}

bool Dropdown::dismissesOnOutsideClick() const noexcept
{
    return dismissOnOutsideClick_;
}

void Dropdown::setCloseOnAnchorHidden(bool close)
{
    closeOnAnchorHidden_ = close;
}

bool Dropdown::closesOnAnchorHidden() const noexcept
{
    return closeOnAnchorHidden_;
}

void Dropdown::open()
{
    if (open_ || anchor_ == nullptr || content_ == nullptr)
        return;

    host_ = anchor_->window();
    if (host_ == nullptr)
        return;

    if (parentWidget() != host_)
        setParent(host_);

    open_ = true;
    content_->show();
    applyGeometry();

    Modal::registerLayer(this, Modal::Level::Dropdown);
    show();
    Modal::raiseWithinLayer(this);
    installFilters();
    emit opened();
}

void Dropdown::close()
{
    if (!open_)
        return;

    open_ = false;
    removeFilters();
    Modal::unregisterLayer(this);
    hide();

    if (content_ != nullptr)
        content_->hide();

    emit closed();
}

bool Dropdown::isOpen() const noexcept
{
    return open_;
}

QRectF Dropdown::panelRect() const
{
    return panel_;
}

QRectF Dropdown::popupGlobalRect() const
{
    if (panel_.isEmpty())
        return {};
    return QRectF(QPointF(mapToGlobal(panel_.topLeft().toPoint())), panel_.size());
}

QRectF Dropdown::contentRect() const
{
    return panel_;
}

qreal Dropdown::shadowMargin() const
{
    return Theme::forWidget(this).density().dp(kShadowMarginDp);
}

QSizeF Dropdown::contentHint() const
{
    if (content_ == nullptr)
        return {};

    QSize hint = content_->sizeHint();
    if (!hint.isValid() || hint.isEmpty()) {
        const QSizeF measured = content_->measuredSize();
        hint = QSize(qRound(measured.width()), qRound(measured.height()));
    }
    return QSizeF(hint);
}

void Dropdown::reposition()
{
    if (open_)
        applyGeometry();
}

void Dropdown::applyGeometry()
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    const qreal margin = density.dp(kShadowMarginDp);
    const qreal gap = density.dp(kAnchorGapDp);

    const QSizeF hint = contentHint();
    const QRectF anchorGlobal(QPointF(anchor_->mapToGlobal(QPoint(0, 0))),
                              QSizeF(anchor_->size()));

    QRect available;
    if (QScreen* screen = anchor_->screen(); screen != nullptr) {
        available = screen->availableGeometry();
    } else {
        available = QRect(QPoint(0, 0), anchorGlobal.size().toSize());
    }

    const qreal minWidth = density.dp(minimumWidthDp_);
    qreal width = std::max({hint.width(), minWidth,
                            matchAnchorWidth_ ? anchorGlobal.width() : 0.0});
    width = std::clamp(width, 0.0, std::max(qreal(0.0), available.width() - 2.0 * margin));

    qreal height = std::min(hint.height(), density.dp(maximumHeightDp_));
    height = std::clamp(height, 0.0, std::max(qreal(0.0), available.height() - 2.0 * margin));

    const bool belowFits = anchorGlobal.bottom() + gap + height <= available.bottom() - margin;
    const bool aboveFits = anchorGlobal.top() - gap - height >= available.top() + margin;

    effective_ = placement_;
    if (placement_ == Placement::Auto)
        effective_ = belowFits ? Placement::Below : (aboveFits ? Placement::Above : Placement::Below);
    else if (placement_ == Placement::Below && !belowFits && aboveFits)
        effective_ = Placement::Above;
    else if (placement_ == Placement::Above && !aboveFits && belowFits)
        effective_ = Placement::Below;

    QPointF topLeft;
    switch (effective_) {
    case Placement::Above:
        topLeft = QPointF(anchorGlobal.left(), anchorGlobal.top() - gap - height);
        break;
    case Placement::Right:
        topLeft = QPointF(anchorGlobal.right() + gap, anchorGlobal.top());
        break;
    case Placement::Left:
        topLeft = QPointF(anchorGlobal.left() - gap - width, anchorGlobal.top());
        break;
    case Placement::Below:
    case Placement::Auto:
    default:
        topLeft = QPointF(anchorGlobal.left(), anchorGlobal.bottom() + gap);
        break;
    }

    topLeft += offset_;

    // Clamp the panel to the screen's available geometry, with the shadow
    // margin kept on screen.
    const qreal minX = available.left() + margin;
    const qreal maxX = available.right() - margin - width;
    const qreal minY = available.top() + margin;
    const qreal maxY = available.bottom() - margin - height;
    topLeft.setX(std::clamp(topLeft.x(), minX, std::max(minX, maxX)));
    topLeft.setY(std::clamp(topLeft.y(), minY, std::max(minY, maxY)));

    const QPoint local = host_->mapFromGlobal(topLeft.toPoint());

    // panel_ is widget-local; the widget reserves the shadow margin around it.
    const int inset = qRound(margin);
    const QRect widgetRect(local.x() - inset, local.y() - inset,
                           qRound(width) + 2 * inset, qRound(height) + 2 * inset);
    setGeometry(widgetRect);
    panel_ = QRectF(QPointF(inset, inset), QSizeF(width, height));

    if (content_ != nullptr) {
        content_->measure(MeasureSpec::exactly(panel_.width()),
                          MeasureSpec::exactly(panel_.height()));
        content_->arrange(panel_.toAlignedRect());
    }

    updateGeometry();
    requestUpdate();
}

void Dropdown::installFilters()
{
    if (host_ != nullptr)
        host_->installEventFilter(this);
    if (QApplication::instance() != nullptr)
        QApplication::instance()->installEventFilter(this);
}

void Dropdown::removeFilters()
{
    if (host_ != nullptr)
        host_->removeEventFilter(this);
    if (QApplication::instance() != nullptr)
        QApplication::instance()->removeEventFilter(this);
}

bool Dropdown::containsGlobal(const QPoint& global) const
{
    if (popupGlobalRect().contains(global))
        return true;

    if (anchor_ != nullptr) {
        const QRectF anchorGlobal(QPointF(anchor_->mapToGlobal(QPoint(0, 0))),
                                  QSizeF(anchor_->size()));
        if (anchorGlobal.contains(global))
            return true;
    }
    return false;
}

void Dropdown::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const QSizeF hint = contentHint();
    const qreal margin = shadowMargin();
    setMeasuredSize(QSizeF(width.resolve(hint.width() + 2.0 * margin),
                           height.resolve(hint.height() + 2.0 * margin)));
}

void Dropdown::paintContent(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    const QRectF panel = panel_;
    if (panel.isEmpty())
        return;

    const qreal radius = density.dp(kPanelRadiusDp);
    const qreal margin = density.dp(kShadowMarginDp);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Soft elevation: a few very transparent rounded rectangles behind the
    // panel; the widget rectangle already reserves the margin they need.
    painter.setPen(Qt::NoPen);
    for (int step = 4; step >= 1; --step) {
        const qreal spread = margin * (qreal(step) / 4.0);
        QColor shadow(Qt::black);
        shadow.setAlphaF(0.05F * static_cast<float>(5 - step) / 4.0F + 0.02F);
        painter.setBrush(shadow);
        painter.drawRoundedRect(panel.adjusted(-spread, -spread + 1.0, spread, spread + 1.0),
                                radius + spread, radius + spread);
    }

    // A design baseline menu-like surface is separated from the page by its elevation
    // shadow, not by a stroke: drawing outline-variant on top of the shadow
    // stacked two borders on the same edge.
    painter.setPen(Qt::NoPen);
    painter.setBrush(colours.surfaceContainer);
    painter.drawRoundedRect(panel, radius, radius);
    painter.restore();
}

void Dropdown::mousePressEvent(QMouseEvent* event)
{
    View::mousePressEvent(event);
    event->accept();
}

void Dropdown::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        event->accept();
        return;
    }
    View::keyPressEvent(event);
}

bool Dropdown::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == anchor_.data()) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
            if (open_)
                reposition();
            break;
        case QEvent::Hide:
            if (open_ && closeOnAnchorHidden_)
                close();
            break;
        case QEvent::Destroy:
            if (open_)
                close();
            anchor_ = nullptr;
            break;
        default:
            break;
        }
        return false;
    }

    if (open_ && watched == host_
        && (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
        reposition();
        return false;
    }

    if (open_ && event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (!containsGlobal(mouse->globalPosition().toPoint())) {
            if (dismissOnOutsideClick_)
                close();
            return false;
        }
    }

    if (open_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            close();
            return true;
        }
    }

    return View::eventFilter(watched, event);
}

} // namespace md
