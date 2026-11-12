#include <md/components/tooltip.h>

#include <md/components/modal.h>
#include <md/theme/theme.h>

#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTimer>
#include <QWidget>

#include <algorithm>

namespace md {
namespace {

constexpr qreal kPaddingHorizontalDp = 8.0;
constexpr qreal kPaddingVerticalDp = 4.0;
constexpr qreal kAnchorGapDp = 8.0;
constexpr qreal kCornerDp = 4.0;
// M3 plain tooltip: max container width 200 dp.
constexpr qreal kMaxWidthDp = 200.0;

} // namespace

Tooltip::Tooltip(QWidget* parent)
    : View(parent)
{
    setObjectName(QStringLiteral("md.tooltip"));
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFocusPolicy(Qt::NoFocus);
    setShape(Shape::none());
    setBackgroundColour(QColor(Qt::transparent));

    delayTimer_ = new QTimer(this);
    delayTimer_->setSingleShot(true);
    connect(delayTimer_, &QTimer::timeout, this, [this] {
        if (anchor_ != nullptr && !text_.isEmpty())
            show();
    });

    hide();
}

Tooltip::~Tooltip()
{
    removeFilters();
    if (anchor_ != nullptr)
        anchor_->removeEventFilter(this);
}

void Tooltip::setAnchor(QWidget* anchor)
{
    if (anchor_ == anchor)
        return;

    if (open_)
        hide();

    if (anchor_ != nullptr)
        anchor_->removeEventFilter(this);

    anchor_ = anchor;

    if (anchor_ != nullptr) {
        anchor_->installEventFilter(this);
        host_ = anchor_->window();
    }
}

QWidget* Tooltip::anchor() const
{
    return anchor_.data();
}

void Tooltip::setText(const QString& text)
{
    if (text_ == text)
        return;

    text_ = text;
    updateGeometry();
    requestUpdate();
    if (open_)
        reposition();
}

QString Tooltip::text() const
{
    return text_;
}

void Tooltip::setPlacement(Placement placement)
{
    if (placement_ == placement)
        return;
    placement_ = placement;
    if (open_)
        reposition();
}

Tooltip::Placement Tooltip::placement() const noexcept
{
    return placement_;
}

Tooltip::Placement Tooltip::effectivePlacement() const noexcept
{
    return effective_;
}

void Tooltip::setDelay(int milliseconds)
{
    delayMs_ = std::max(0, milliseconds);
}

int Tooltip::delay() const noexcept
{
    return delayMs_;
}

void Tooltip::setOffset(const QPointF& offset)
{
    if (offset_ == offset)
        return;
    offset_ = offset;
    if (open_)
        reposition();
}

QPointF Tooltip::offset() const noexcept
{
    return offset_;
}

void Tooltip::setMaximumWidthDp(qreal widthDp)
{
    const qreal value = std::max(qreal(0.0), widthDp);
    if (qFuzzyCompare(maximumWidthDp_ + 1.0, value + 1.0))
        return;
    maximumWidthDp_ = value;
    if (open_)
        reposition();
}

qreal Tooltip::maximumWidthDp() const noexcept
{
    return maximumWidthDp_;
}

void Tooltip::hover()
{
    if (anchor_ == nullptr || text_.isEmpty())
        return;

    if (delayMs_ <= 0) {
        show();
        return;
    }

    delayTimer_->start(delayMs_);
}

void Tooltip::show()
{
    if (open_ || anchor_ == nullptr || text_.isEmpty())
        return;

    delayTimer_->stop();
    host_ = anchor_->window();
    if (host_ == nullptr)
        return;

    if (parentWidget() != host_)
        setParent(host_);

    open_ = true;
    applyGeometry();
    Modal::registerLayer(this, Modal::Level::Tooltip);
    QWidget::show();
    Modal::raiseWithinLayer(this);
    installFilters();
    emit opened();
}

void Tooltip::hide()
{
    if (delayTimer_ != nullptr)
        delayTimer_->stop();

    if (!open_)
        return;

    open_ = false;
    removeFilters();
    Modal::unregisterLayer(this);
    QWidget::hide();
    emit closed();
}

bool Tooltip::isOpen() const noexcept
{
    return open_;
}

void Tooltip::reposition()
{
    if (open_)
        applyGeometry();
}

QRectF Tooltip::panelRect() const
{
    return panel_;
}

QRectF Tooltip::panelGlobalRect() const
{
    if (panel_.isEmpty())
        return {};
    return QRectF(QPointF(mapToGlobal(panel_.topLeft().toPoint())), panel_.size());
}

QFont Tooltip::textFont() const
{
    const Theme& theme = Theme::forWidget(this);
    const TextStyle& style = theme.type().bodySmall;
    const Density& density = theme.density();

    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(density.sp(style.sizeSp)))));
    font.setWeight(static_cast<QFont::Weight>(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, density.sp(style.trackingSp));
    return font;
}

QSizeF Tooltip::preferredSize() const
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    const QFontMetricsF metrics(textFont());
    const qreal paddingX = density.dp(kPaddingHorizontalDp);
    // The cap applies to the container, not to the text run: a 200 dp tooltip
    // is 200 dp wide including its 8 dp of horizontal padding.
    const qreal maxWidth =
        std::min(density.dp(maximumWidthDp_), density.dp(kMaxWidthDp));
    const qreal textWidth = std::min(metrics.horizontalAdvance(text_),
                                     std::max(qreal(0.0), maxWidth - 2.0 * paddingX));
    const qreal width = textWidth + 2.0 * paddingX;
    const qreal height = metrics.height() + 2.0 * density.dp(kPaddingVerticalDp);
    return QSizeF(std::ceil(width), std::ceil(height));
}

void Tooltip::applyGeometry()
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal gap = density.dp(kAnchorGapDp);

    const QSizeF size = preferredSize();
    const QRectF anchorGlobal(QPointF(anchor_->mapToGlobal(QPoint(0, 0))),
                              QSizeF(anchor_->size()));

    QRect available;
    if (QScreen* screen = anchor_->screen(); screen != nullptr)
        available = screen->availableGeometry();
    else
        available = QRect(QPoint(0, 0), anchorGlobal.size().toSize());

    const bool aboveFits = anchorGlobal.top() - gap - size.height() >= available.top();
    const bool belowFits = anchorGlobal.bottom() + gap + size.height() <= available.bottom();

    effective_ = placement_;
    if (placement_ == Placement::Auto)
        effective_ = aboveFits ? Placement::Above : Placement::Below;
    else if (placement_ == Placement::Above && !aboveFits && belowFits)
        effective_ = Placement::Below;
    else if (placement_ == Placement::Below && !belowFits && aboveFits)
        effective_ = Placement::Above;

    QPointF topLeft;
    switch (effective_) {
    case Placement::Below:
        topLeft = QPointF(anchorGlobal.center().x() - size.width() / 2.0,
                          anchorGlobal.bottom() + gap);
        break;
    case Placement::Left:
        topLeft = QPointF(anchorGlobal.left() - gap - size.width(),
                          anchorGlobal.center().y() - size.height() / 2.0);
        break;
    case Placement::Right:
        topLeft = QPointF(anchorGlobal.right() + gap,
                          anchorGlobal.center().y() - size.height() / 2.0);
        break;
    case Placement::Above:
    case Placement::Auto:
    default:
        topLeft = QPointF(anchorGlobal.center().x() - size.width() / 2.0,
                          anchorGlobal.top() - gap - size.height());
        break;
    }

    topLeft += offset_;
    topLeft.setX(std::clamp(topLeft.x(), qreal(available.left()),
                            std::max(qreal(available.left()),
                                     qreal(available.right()) - size.width())));
    topLeft.setY(std::clamp(topLeft.y(), qreal(available.top()),
                            std::max(qreal(available.top()),
                                     qreal(available.bottom()) - size.height())));

    const QPoint local = host_->mapFromGlobal(topLeft.toPoint());
    panel_ = QRectF(QPointF(0, 0), size);
    setGeometry(QRect(local, size.toSize()));

    updateGeometry();
    requestUpdate();
}

void Tooltip::installFilters()
{
    if (host_ != nullptr)
        host_->installEventFilter(this);
}

void Tooltip::removeFilters()
{
    if (host_ != nullptr)
        host_->removeEventFilter(this);
}

void Tooltip::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const QSizeF size = preferredSize();
    setMeasuredSize(QSizeF(width.resolve(size.width()), height.resolve(size.height())));
}

void Tooltip::paintContent(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);

    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();
    const Density& density = theme.density();

    const QRectF panel = panel_;
    if (panel.isEmpty())
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal radius = density.dp(kCornerDp);
    painter.setPen(Qt::NoPen);
    // M3 plain tooltip: inverse surface, 4 dp corner radius, and no elevation
    // shadow — the tooltip is separated from the page by its inverted tone, not
    // by a drop shadow, so the widget stays a transparent overlay.
    painter.setBrush(colours.inverseSurface);
    painter.drawRoundedRect(panel, radius, radius);

    painter.setFont(textFont());
    painter.setPen(colours.inverseOnSurface);
    const qreal paddingX = density.dp(kPaddingHorizontalDp);
    painter.drawText(panel.adjusted(paddingX, 0, -paddingX, 0),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QFontMetricsF(painter.font()).elidedText(
                         text_, layoutDirection() == Qt::RightToLeft ? Qt::ElideLeft
                                                                     : Qt::ElideRight,
                         std::max(qreal(0.0), panel.width() - 2.0 * paddingX)));
    painter.restore();
}

void Tooltip::mousePressEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
    hide();
}

bool Tooltip::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == anchor_.data()) {
        switch (event->type()) {
        case QEvent::Enter:
            hover();
            break;
        case QEvent::Leave:
        case QEvent::MouseButtonPress:
        case QEvent::Wheel:
            hide();
            break;
        case QEvent::Hide:
        case QEvent::Destroy:
            hide();
            if (event->type() == QEvent::Destroy)
                anchor_ = nullptr;
            break;
        default:
            break;
        }
        return false;
    }

    if (open_ && watched == host_ && event->type() == QEvent::Resize) {
        reposition();
        return false;
    }

    return View::eventFilter(watched, event);
}

Tooltip* Tooltip::showText(QWidget* anchor, const QString& text, int delayMs)
{
    if (anchor == nullptr)
        return nullptr;

    QWidget* host = anchor->window();
    if (host == nullptr)
        return nullptr;

    auto* tooltip = host->findChild<Tooltip*>(QStringLiteral("md.tooltip"),
                                              Qt::FindDirectChildrenOnly);
    if (tooltip == nullptr)
        tooltip = new Tooltip(host);

    tooltip->setAnchor(anchor);
    tooltip->setText(text);
    tooltip->setDelay(delayMs);
    tooltip->hover();
    return tooltip;
}

} // namespace md
