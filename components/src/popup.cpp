// Periodicity — PopUp, an anchored floating surface.
//
// Everything is painted by this component: the surface is not a View
// background, because the background layer fills the whole widget rectangle
// while a popup's visible panel is smaller (arrow band and shadow margin are
// part of the widget). The panel and the arrow are united into one path so the
// seam between them never shows.

#include <md/components/popup.h>

#include <md/theme/theme.h>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRect>
#include <QRectF>
#include <QScreen>
#include <QSizeF>
#include <QWidget>

#include <algorithm>

namespace md {

namespace {

constexpr qreal kShadowMarginDp = 6.0;
constexpr qreal kGapDp = 4.0;
constexpr qreal kScreenMarginDp = 4.0;
constexpr qreal kDefaultWidthDp = 200.0;
constexpr qreal kDefaultHeightDp = 80.0;
constexpr qreal kMinWidthDp = 48.0;

QColor withAlpha(const QColor& colour, qreal alpha)
{
    QColor result = colour;
    result.setAlphaF(static_cast<float>(std::clamp(alpha, qreal(0.0), qreal(1.0))));
    return result;
}

QRect availableGeometryFor(const QPoint& globalCentre)
{
    const QScreen* screen = QGuiApplication::screenAt(globalCentre);
    if (screen == nullptr)
        screen = QGuiApplication::primaryScreen();

    if (screen == nullptr)
        return QRect(0, 0, 1024, 768);

    return screen->availableGeometry();
}

bool fitsInside(const QRectF& rect, const QRect& available)
{
    return rect.left() >= qreal(available.left()) && rect.right() <= qreal(available.right())
        && rect.top() >= qreal(available.top()) && rect.bottom() <= qreal(available.bottom());
}

PopUp::Placement oppositeOf(PopUp::Placement placement)
{
    switch (placement) {
    case PopUp::Placement::Below:
        return PopUp::Placement::Above;
    case PopUp::Placement::Above:
        return PopUp::Placement::Below;
    case PopUp::Placement::Right:
        return PopUp::Placement::Left;
    case PopUp::Placement::Left:
        break;
    }

    return PopUp::Placement::Right;
}

} // namespace

PopUp::PopUp(QWidget* parent)
    : ViewGroup(parent)
{
    setBackgroundColour(QColor(Qt::transparent));
    setFocusPolicy(Qt::NoFocus);
    syncAppearance();
}

void PopUp::syncAppearance()
{
    setShape(Theme::forWidget(this).shapes().medium);
    requestUpdate();
}

void PopUp::setAnchor(QWidget* anchor)
{
    if (anchor_.data() == anchor)
        return;

    if (anchor_ != nullptr && open_)
        anchor_->removeEventFilter(this);

    anchor_ = anchor;

    if (anchor_ != nullptr && open_)
        anchor_->installEventFilter(this);

    if (anchor_ != nullptr) {
        QObject::connect(anchor_, &QObject::destroyed, this, [this](QObject* object) {
            if (anchor_.isNull() || static_cast<QObject*>(anchor_.data()) == object)
                close();
        });
    }

    if (open_)
        reposition();
}

QWidget* PopUp::anchor() const
{
    return anchor_.data();
}

void PopUp::setPlacement(Placement placement)
{
    if (placement_ == placement)
        return;

    placement_ = placement;
    if (open_)
        reposition();

    requestUpdate();
}

PopUp::Placement PopUp::placement() const noexcept
{
    return placement_;
}

PopUp::Placement PopUp::effectivePlacement() const noexcept
{
    return effective_;
}

void PopUp::setContent(View* content)
{
    if (content_.data() == content)
        return;

    if (content_ != nullptr) {
        removeView(content_);
        content_->hide();
    }

    content_ = content;

    if (content_ != nullptr) {
        addView(content_);
        content_->show();
    }

    requestUpdate();
    if (open_)
        reposition();
}

View* PopUp::content() const
{
    return content_.data();
}

void PopUp::setArrowVisible(bool visible)
{
    if (arrowVisible_ == visible)
        return;

    arrowVisible_ = visible;
    if (open_)
        reposition();

    requestUpdate();
}

bool PopUp::isArrowVisible() const noexcept
{
    return arrowVisible_;
}

void PopUp::setContentPadding(qreal dp)
{
    const qreal clamped = std::max(qreal(0.0), dp);
    if (qFuzzyCompare(contentPaddingDp_, clamped))
        return;

    contentPaddingDp_ = clamped;
    if (open_)
        reposition();

    requestUpdate();
}

qreal PopUp::contentPadding() const noexcept
{
    return contentPaddingDp_;
}

void PopUp::setArrowSize(qreal dp)
{
    const qreal clamped = std::max(qreal(0.0), dp);
    if (qFuzzyCompare(arrowSizeDp_, clamped))
        return;

    arrowSizeDp_ = clamped;
    if (open_)
        reposition();

    requestUpdate();
}

qreal PopUp::arrowSize() const noexcept
{
    return arrowSizeDp_;
}

QSizeF PopUp::desiredSize()
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    const qreal shadow = density.dp(kShadowMarginDp) * 2.0;
    const qreal arrow = arrowVisible_ ? density.dp(arrowSizeDp_) : 0.0;
    const qreal padding = density.dp(contentPaddingDp_) * 2.0;
    const qreal chrome = shadow + arrow + padding;

    if (content_ == nullptr)
        return QSizeF(density.dp(kDefaultWidthDp), density.dp(kDefaultHeightDp));

    content_->measure(MeasureSpec::unspecified(), MeasureSpec::unspecified());
    const QSizeF contentSize = content_->measuredSize();

    return QSizeF(std::max(contentSize.width() + chrome, density.dp(kMinWidthDp)),
                  contentSize.height() + chrome);
}

void PopUp::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const QSizeF natural = desiredSize();
    setMeasuredSize(QSizeF(width.resolve(natural.width()), height.resolve(natural.height())));
}

QRectF PopUp::bodyFor(const QRectF& full) const
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    const qreal margin = density.dp(kShadowMarginDp);
    const qreal arrow = arrowVisible_ ? density.dp(arrowSizeDp_) : 0.0;
    const QRectF panel = full.adjusted(margin, margin, -margin, -margin);

    switch (effective_) {
    case Placement::Below:
        return panel.adjusted(0.0, arrow, 0.0, 0.0);
    case Placement::Above:
        return panel.adjusted(0.0, 0.0, 0.0, -arrow);
    case Placement::Right:
        return panel.adjusted(arrow, 0.0, 0.0, 0.0);
    case Placement::Left:
        break;
    }

    return panel.adjusted(0.0, 0.0, -arrow, 0.0);
}

QRectF PopUp::bodyRect() const
{
    return bodyFor(QRectF(rect()));
}

QRectF PopUp::contentArea() const
{
    const Density& density = Theme::forWidget(this).density();
    const qreal padding = density.dp(contentPaddingDp_);
    return bodyRect().adjusted(padding, padding, -padding, -padding);
}

QRectF PopUp::arrowRect() const
{
    return arrowPath(QRectF(rect())).boundingRect();
}

QPainterPath PopUp::arrowPath(const QRectF& full) const
{
    QPainterPath path;
    if (!arrowVisible_)
        return path;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal size = density.dp(arrowSizeDp_);
    if (size <= 0.0)
        return path;

    const QRectF body = bodyFor(full);
    if (body.isEmpty())
        return path;

    const qreal half = size * 0.75;
    const qreal margin = density.dp(12.0);

    // The arrow points at the anchor's centre, clamped so it never runs off
    // the edge of the panel.
    qreal tip = body.center().x();
    if (effective_ == Placement::Left || effective_ == Placement::Right)
        tip = body.center().y();

    if (anchor_ != nullptr) {
        const QPoint anchorCentre =
            mapFromGlobal(anchor_->mapToGlobal(anchor_->rect().center()));
        tip = (effective_ == Placement::Left || effective_ == Placement::Right)
            ? static_cast<qreal>(anchorCentre.y())
            : static_cast<qreal>(anchorCentre.x());
    }

    switch (effective_) {
    case Placement::Below:
    case Placement::Above: {
        const qreal low = body.left() + half + margin;
        const qreal high = body.right() - half - margin;
        if (high > low)
            tip = std::clamp(tip, low, high);
        else
            tip = body.center().x();
        break;
    }
    case Placement::Left:
    case Placement::Right: {
        const qreal low = body.top() + half + margin;
        const qreal high = body.bottom() - half - margin;
        if (high > low)
            tip = std::clamp(tip, low, high);
        else
            tip = body.center().y();
        break;
    }
    }

    switch (effective_) {
    case Placement::Below:
        path.moveTo(tip, body.top() - size);
        path.lineTo(tip - half, body.top() + 0.5);
        path.lineTo(tip + half, body.top() + 0.5);
        break;
    case Placement::Above:
        path.moveTo(tip, body.bottom() + size);
        path.lineTo(tip - half, body.bottom() - 0.5);
        path.lineTo(tip + half, body.bottom() - 0.5);
        break;
    case Placement::Right:
        path.moveTo(body.left() - size, tip);
        path.lineTo(body.left() + 0.5, tip - half);
        path.lineTo(body.left() + 0.5, tip + half);
        break;
    case Placement::Left:
        path.moveTo(body.right() + size, tip);
        path.lineTo(body.right() - 0.5, tip - half);
        path.lineTo(body.right() - 0.5, tip + half);
        break;
    }

    path.closeSubpath();
    return path;
}

QRectF PopUp::anchorRectGlobal() const
{
    if (anchor_ == nullptr)
        return QRectF();

    return QRectF(QPointF(anchor_->mapToGlobal(QPoint(0, 0))), QSizeF(anchor_->size()));
}

QRectF PopUp::geometryFor(Placement placement, const QRectF& anchorRect,
                          const QSizeF& size) const
{
    const qreal gap = Theme::forWidget(this).density().dp(kGapDp);

    QPointF topLeft;
    switch (placement) {
    case Placement::Below:
        topLeft = QPointF(anchorRect.center().x() - size.width() / 2.0,
                          anchorRect.bottom() + gap);
        break;
    case Placement::Above:
        topLeft = QPointF(anchorRect.center().x() - size.width() / 2.0,
                          anchorRect.top() - gap - size.height());
        break;
    case Placement::Right:
        topLeft = QPointF(anchorRect.right() + gap,
                          anchorRect.center().y() - size.height() / 2.0);
        break;
    case Placement::Left:
        topLeft = QPointF(anchorRect.left() - gap - size.width(),
                          anchorRect.center().y() - size.height() / 2.0);
        break;
    }

    return QRectF(topLeft, size);
}

PopUp::Placement PopUp::choosePlacement(Placement requested, const QRectF& anchorRect,
                                        const QRect& available, const QSizeF& size) const
{
    if (!anchorRect.isValid())
        return requested;

    if (fitsInside(geometryFor(requested, anchorRect, size), available))
        return requested;

    const Placement opposite = oppositeOf(requested);
    if (fitsInside(geometryFor(opposite, anchorRect, size), available))
        return opposite;

    // A popup wider than the screen fits nowhere; keep the caller's choice and
    // let the clamping in reposition() do what it can.
    return requested;
}

void PopUp::reposition()
{
    if (!open_)
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    const QRectF anchorRect = anchorRectGlobal();
    QPoint centre;
    if (anchorRect.isValid())
        centre = anchorRect.center().toPoint();
    else if (parentWidget() != nullptr)
        centre = parentWidget()->mapToGlobal(parentWidget()->rect().center());
    else
        centre = QPoint(0, 0);

    const QRect available = availableGeometryFor(centre);
    const qreal limitWidth =
        std::max(qreal(0.0), static_cast<qreal>(available.width()) - density.dp(kScreenMarginDp) * 2.0);
    const qreal limitHeight =
        std::max(qreal(0.0), static_cast<qreal>(available.height()) - density.dp(kScreenMarginDp) * 2.0);

    measure(MeasureSpec::atMost(limitWidth), MeasureSpec::atMost(limitHeight));

    const QSizeF size = measuredSize();
    effective_ = choosePlacement(placement_, anchorRect, available, size);

    QRectF global;
    if (anchorRect.isValid()) {
        global = geometryFor(effective_, anchorRect, size);
    } else {
        global = QRectF(QPointF(available.center()) - QPointF(size.width() / 2.0,
                                                              size.height() / 2.0),
                        size);
    }

    const qreal margin = density.dp(kScreenMarginDp);
    const qreal minX = static_cast<qreal>(available.left()) + margin;
    const qreal maxX = std::max(minX, static_cast<qreal>(available.right()) - margin - size.width());
    const qreal minY = static_cast<qreal>(available.top()) + margin;
    const qreal maxY = std::max(minY, static_cast<qreal>(available.bottom()) - margin - size.height());

    global.moveLeft(std::clamp(global.left(), minX, maxX));
    global.moveTop(std::clamp(global.top(), minY, maxY));

    QPoint topLeft = global.topLeft().toPoint();
    if (parentWidget() != nullptr)
        topLeft = parentWidget()->mapFromGlobal(topLeft);

    arrange(QRect(topLeft, QSize(qCeil(size.width()), qCeil(size.height()))));
}

void PopUp::open()
{
    if (open_)
        return;

    if (parentWidget() == nullptr && anchor_ != nullptr)
        setParent(anchor_->window());

    open_ = true;
    syncAppearance();
    reposition();

    show();
    raise();

    if (auto* app = QCoreApplication::instance())
        app->installEventFilter(this);
    if (anchor_ != nullptr)
        anchor_->installEventFilter(this);

    emit opened();
}

void PopUp::detachFilters()
{
    if (auto* app = QCoreApplication::instance())
        app->removeEventFilter(this);
    if (anchor_ != nullptr)
        anchor_->removeEventFilter(this);
}

void PopUp::close()
{
    if (!open_) {
        hide();
        return;
    }

    open_ = false;
    detachFilters();
    hide();
    emit closed();
}

bool PopUp::isOpen() const noexcept
{
    return open_;
}

void PopUp::hideEvent(QHideEvent* event)
{
    View::hideEvent(event);

    if (!open_)
        return;

    // Hiding the popup behind the component's back (a parent hide, for
    // example) still has to look like a close to a listener.
    open_ = false;
    detachFilters();
    emit closed();
}

bool PopUp::eventFilter(QObject* watched, QEvent* event)
{
    if (!open_)
        return ViewGroup::eventFilter(watched, event);

    if (anchor_ != nullptr && watched == anchor_.data()) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
            reposition();
            break;
        case QEvent::Hide:
            close();
            break;
        default:
            break;
        }
        return false;
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        const QRectF selfRect(QPointF(mapToGlobal(QPoint(0, 0))), QSizeF(size()));
        if (!selfRect.contains(mouse->globalPosition()))
            close();
        break;
    }
    case QEvent::KeyPress: {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            close();
            return true;
        }
        break;
    }
    default:
        break;
    }

    return ViewGroup::eventFilter(watched, event);
}

void PopUp::arrangeChildren(const QRectF& content)
{
    Q_UNUSED(content);

    if (content_ == nullptr)
        return;

    const QRectF area = contentArea();
    if (area.isEmpty())
        return;

    content_->measure(MeasureSpec::atMost(area.width()), MeasureSpec::atMost(area.height()));
    const QSizeF size = content_->measuredSize();
    content_->arrange(QRect(qRound(area.left()), qRound(area.top()),
                            qRound(std::min(size.width(), area.width())),
                            qRound(std::min(size.height(), area.height()))));
}

void PopUp::paintContent(QPainter& painter, const QRectF& content)
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    const QRectF body = bodyFor(content);
    if (body.isEmpty())
        return;

    QPainterPath panel = theme.shapes().extraSmall.path(body);
    const QPainterPath arrow = arrowPath(content);
    if (!arrow.isEmpty())
        panel = panel.united(arrow);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // A soft, theme-derived shadow: three expanding rounded rectangles, each
    // fainter than the previous one. It is clipped by the widget rectangle,
    // which is why the surface reserves a shadow margin.
    const qreal spreadUnit = density.dp(kShadowMarginDp);
    for (int step = 3; step >= 1; --step) {
        const qreal spread = spreadUnit * static_cast<qreal>(step) / 3.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(withAlpha(colours.shadow, 0.07));
        painter.drawPath(theme.shapes().medium.path(
            body.adjusted(-spread, -spread + density.dp(1.5), spread, spread + density.dp(1.5))));
    }

    // Design baseline menus/popups separate the panel with a level-2 shadow only; an
    // outlineVariant stroke would double the border on top of it.
    painter.setPen(Qt::NoPen);
    painter.setBrush(colours.surfaceContainer);
    painter.drawPath(panel);

    painter.restore();
}

void PopUp::onThemeChanged()
{
    syncAppearance();
    if (open_)
        reposition();
}

} // namespace md
