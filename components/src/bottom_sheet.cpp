// Periodicity — BottomSheet, the design baseline bottom sheet.
//
// The widget owns four visuals the base pipeline cannot place: the scrim, the
// bottom-anchored surface with its two rounded top corners, the drag handle and
// the title. All four read from one resolved geometry (surfaceRect,
// handleRect, titleRect, viewportRect), and hit testing reads the same
// rectangles, so a layout regression shows up in the tests through the same
// entry points the painter uses.
//
// The body is a real child widget inside an internal viewport. Qt clips a child
// to its parent, so the viewport is what stops a scrolled body from painting
// over the handle, the title and the scrim; the sheet itself reserves nothing
// of its own. The content is arranged at its own measured height at the current
// scroll offset, mirrored from Page's viewport so the two scrolling components
// behave identically.
//
// The transition interpolates a single progress value with a QTimer and the
// theme's short motion token. Nothing here is a QPropertyAnimation: the surface
// is arranged and painted from the progress, so the same code path serves the
// slide, the reduced-motion snap and the geometry assertions in the tests.

#include <md/components/bottom_sheet.h>

#include <md/components/font.h>
#include <md/components/modal.h>
#include <md/core/geometry.h>
#include <md/theme/theme.h>
#include <md/tokens/motion.h>
#include <md/view/view_group.h>

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QList>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QSize>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>

namespace md {
namespace {

constexpr qreal kDefaultWidthDp = 360.0;
// The surface may use nine tenths of the host at most; the body scrolls past
// that instead of pushing the sheet over the whole window.
constexpr qreal kMaxHeightRatio = 0.90;
// Design baseline large shape: a bottom sheet rounds only its two top corners.
constexpr qreal kTopCornerDp = 28.0;
// The drag handle is 32x4 dp, centred, 22 dp below the top edge (design baseline).
constexpr qreal kHandleTopPaddingDp = 22.0;
constexpr qreal kHandleWidthDp = 32.0;
constexpr qreal kHandleHeightDp = 4.0;
// Space between the handle and the title block, and below the title.
constexpr qreal kHandleToTitleGapDp = 16.0;
constexpr qreal kTitleSidePaddingDp = 24.0;
constexpr qreal kTitleBottomPaddingDp = 16.0;
constexpr qreal kScrimOpacity = 0.32;
constexpr qreal kShadowSpreadDp = 8.0;
constexpr qreal kWheelStepDp = 64.0;
// WCAG 2.5.7: the drag has a keyboard equivalent (Escape). The pointer path
// closes the sheet once the handle has travelled more than a quarter of it.
constexpr qreal kDragCloseRatio = 0.25;
constexpr int kTickMs = 16;

QColor withAlpha(const QColor& colour, qreal alpha)
{
    QColor result = colour;
    result.setAlphaF(static_cast<float>(std::clamp(alpha, qreal(0.0), qreal(1.0))));
    return result;
}

} // namespace

BottomSheet::BottomSheet(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundColour(QColor(Qt::transparent));
    setShape(Shape::none());

    // The sheet is an overlay, not one control: a ripple or a state layer over
    // the whole scrim would read as a giant button. The controls inside the
    // content keep their own feedback because they are separate widgets.
    setInteractionFeedbackEnabled(false);

    viewport_ = new QWidget(this);
    viewport_->setFocusPolicy(Qt::NoFocus);
    viewport_->setAutoFillBackground(false);
    viewport_->hide();

    timer_ = new QTimer(this);
    timer_->setInterval(kTickMs);
    connect(timer_, &QTimer::timeout, this, [this] {
        const int duration = transitionDuration();
        if (duration <= 0) {
            finishTransition();
            return;
        }

        elapsedMs_ += kTickMs;
        const qreal linear =
            std::clamp(qreal(elapsedMs_) / qreal(duration), qreal(0.0), qreal(1.0));
        progress_ = startProgress_ + (targetProgress_ - startProgress_) * easedProgress(linear);

        if (linear >= 1.0) {
            finishTransition();
            return;
        }

        syncLayout();
    });

    // An unopened sheet is an overlay that must never paint over its host.
    hide();
}

BottomSheet::~BottomSheet()
{
    if (timer_ != nullptr)
        timer_->stop();

    // The filters live on the application and on the current host; both are
    // still valid here, and leaving them behind would deliver events to a
    // half-destroyed widget.
    removeFilters();
    detachLayer();

    if (content_ != nullptr) {
        // The content is not owned by the sheet. Hiding before detaching keeps
        // a widget that was visible inside the viewport from reappearing as a
        // stray top-level window.
        content_->hide();
        content_->setParent(nullptr);
    }
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void BottomSheet::setTitle(const QString& title)
{
    if (title_ == title)
        return;

    title_ = title;

    if (open_)
        relayout();
    else {
        updateGeometry();
        requestUpdate();
    }
}

QString BottomSheet::title() const
{
    return title_;
}

void BottomSheet::setContent(QWidget* content)
{
    if (content_.data() == content)
        return;

    // Reparenting the host, the viewport or an ancestor into the viewport would
    // destroy the widget tree this sheet lives in.
    if (content != nullptr
        && (content == this || content == viewport_ || content->isAncestorOf(this))) {
        return;
    }

    if (content_ != nullptr) {
        QWidget* previous = content_.data();
        content_ = nullptr;
        previous->hide();
        previous->setParent(nullptr);
    }

    contentHeight_ = 0.0;
    scrollOffset_ = 0.0;

    if (content != nullptr) {
        content_ = content;
        content->setParent(viewport_);
        content->hide();

        if (open_)
            content->show();

        connect(content, &QObject::destroyed, this, [this, content] {
            if (content_.data() == content) {
                content_ = nullptr;
                contentHeight_ = 0.0;
                scrollOffset_ = 0.0;

                if (open_)
                    relayout();
                else {
                    updateGeometry();
                    requestUpdate();
                }
            }
        });
    }

    if (open_)
        relayout();
    else {
        updateGeometry();
        requestUpdate();
    }
}

QWidget* BottomSheet::content() const
{
    return content_.data();
}

void BottomSheet::setModal(bool modal)
{
    if (modal_ == modal)
        return;

    if (!open_) {
        modal_ = modal;
        return;
    }

    // Rebinding the filters has to happen before a possible reparent:
    // removeFilters() removes them from the widget that is still the parent.
    removeFilters();
    detachLayer();

    modal_ = modal;

    if (modal_) {
        QWidget* host = window();
        if (host != nullptr && host != this && parentWidget() != host) {
            if (auto* group = qobject_cast<ViewGroup*>(parentWidget()))
                group->removeView(this);

            setParent(host);
            // setParent() hides the widget; an open sheet has to stay visible.
            show();
        }

        setFocus(Qt::PopupFocusReason);
    } else {
        clearFocus();
    }

    // The rung follows the mode: Drawer for a modal sheet, Content for a
    // resident one. Attaching after the reparent restacks the new window.
    attachLayer();
    installFilters();
    applyOverlayGeometry();
    syncLayout();
}

bool BottomSheet::isModal() const noexcept
{
    return modal_;
}

void BottomSheet::setDismissOnBackdrop(bool dismiss)
{
    dismissOnBackdrop_ = dismiss;
}

bool BottomSheet::dismissesOnBackdrop() const noexcept
{
    return dismissOnBackdrop_;
}

// ---------------------------------------------------------------------------
// Open / close
// ---------------------------------------------------------------------------

void BottomSheet::open()
{
    if (open_)
        return;

    open_ = true;
    closing_ = false;
    dragOffset_ = 0.0;
    scrollOffset_ = 0.0;
    pressedBackdrop_ = false;
    dragging_ = false;
    progress_ = 0.0;
    startProgress_ = 0.0;
    targetProgress_ = 0.0;
    elapsedMs_ = 0;

    const bool canOverlay = modal_ && window() != this;
    if (canOverlay && parentWidget() != window()) {
        // Leaving a container has to go through its bookkeeping, or the group
        // would keep arranging a widget it no longer owns.
        if (auto* group = qobject_cast<ViewGroup*>(parentWidget()))
            group->removeView(this);

        setParent(window());
    }

    if (content_ != nullptr)
        content_->show();

    viewport_->show();

    applyOverlayGeometry();
    installFilters();

    if (!modal_) {
        // A non-modal sheet is a resident surface: there is nothing to slide
        // and nothing to scrim, so it snaps open without touching focus. It is
        // still registered, at the Content rung, so a Dialog that opens later
        // restacks above it instead of being covered.
        progress_ = 1.0;
        startProgress_ = 1.0;
        targetProgress_ = 1.0;
        show();
        attachLayer();
        syncLayout();
        emit opened();
        return;
    }

    // The layer is attached after show() so restack() orders a visible widget;
    // attachLayer() raises the sheet within its rung and then re-raises every
    // higher layer, so a dialog that is already open stays above the sheet.
    show();
    attachLayer();
    setFocus(Qt::PopupFocusReason);
    animateTo(1.0);

    emit opened();
}

void BottomSheet::close()
{
    if (!open_ && !closing_) {
        hide();
        return;
    }

    if (!open_ && closing_)
        return;

    open_ = false;
    pressedBackdrop_ = false;
    dragging_ = false;

    if (!modal_) {
        closing_ = false;
        progress_ = 0.0;
        startProgress_ = 0.0;
        targetProgress_ = 0.0;
        dragOffset_ = 0.0;
        hide();
        removeFilters();
        detachLayer();
        if (content_ != nullptr)
            content_->hide();
        syncLayout();
        emit closed();
        return;
    }

    closing_ = true;
    animateTo(0.0);
}

bool BottomSheet::isOpen() const noexcept
{
    return open_;
}

qreal BottomSheet::animationProgress() const noexcept
{
    return progress_;
}

bool BottomSheet::isAnimating() const
{
    return timer_ != nullptr && timer_->isActive();
}

void BottomSheet::finishAnimation()
{
    finishTransition();
}

void BottomSheet::animateTo(qreal target)
{
    startProgress_ = progress_;
    targetProgress_ = std::clamp(target, qreal(0.0), qreal(1.0));
    elapsedMs_ = 0;

    const int duration = transitionDuration();
    if (duration <= 0 || timer_ == nullptr) {
        progress_ = targetProgress_;
        finishTransition();
        return;
    }

    timer_->setInterval(kTickMs);
    timer_->start();
    syncLayout();
}

void BottomSheet::finishTransition()
{
    if (timer_ != nullptr)
        timer_->stop();

    progress_ = std::clamp(targetProgress_, qreal(0.0), qreal(1.0));
    dragOffset_ = 0.0;

    if (closing_) {
        closing_ = false;
        hide();
        removeFilters();
        detachLayer();
        if (content_ != nullptr)
            content_->hide();
        syncLayout();
        emit closed();
        return;
    }

    syncLayout();
}

int BottomSheet::transitionDuration() const
{
    return static_cast<int>(Theme::forWidget(this).motion().short4().duration.count());
}

qreal BottomSheet::easedProgress(qreal linear) const
{
    const QEasingCurve curve =
        Theme::forWidget(this).motion().short4().easing.toEasingCurve();

    return curve.valueForProgress(std::clamp(linear, qreal(0.0), qreal(1.0)));
}

// ---------------------------------------------------------------------------
// Overlay plumbing
// ---------------------------------------------------------------------------

QWidget* BottomSheet::hostWidget() const
{
    QWidget* host = parentWidget();
    // A top-level sheet is its own host: it still paints a scrim and anchors
    // the surface to the bottom of its window.
    return host != nullptr ? host : const_cast<BottomSheet*>(this);
}

qreal BottomSheet::maxSurfaceHeight() const
{
    const QWidget* host = hostWidget();
    const qreal hostHeight = host != nullptr ? qreal(host->height()) : qreal(height());
    return std::max(qreal(0.0), hostHeight * kMaxHeightRatio);
}

void BottomSheet::applyOverlayGeometry()
{
    QWidget* host = hostWidget();
    // A plain QWidget parent is not a View container, so hostWidget() may be
    // null even though there is a host to dock the surface to. Falling back to
    // the parent keeps the non-modal sheet anchored to the bottom of whatever
    // widget owns it.
    if (host == nullptr)
        host = parentWidget();
    if (host == nullptr || host->width() <= 0 || host->height() <= 0)
        return;

    // The surface height depends on the body, so the size pass always runs
    // before the geometry pass.
    measure(MeasureSpec::exactly(qreal(host->width())), MeasureSpec::unspecified());

    if (modal_) {
        // The overlay covers the host so the scrim can swallow a backdrop
        // press anywhere outside the surface.
        arrange(QRect(QPoint(0, 0), host->size()));
        return;
    }

    const int surfaceHeight =
        std::min(qCeil(measuredSize().height()),
                 std::min(host->height(), static_cast<int>(maxSurfaceHeight())));
    arrange(QRect(0, host->height() - surfaceHeight, host->width(), surfaceHeight));
}

void BottomSheet::relayout()
{
    updateGeometry();
    applyOverlayGeometry();
    syncLayout();
}

void BottomSheet::syncLayout()
{
    updateViewportGeometry();
    requestUpdate();
}

void BottomSheet::attachLayer()
{
    if (layerAttached_)
        return;

    // A modal sheet takes the Drawer rung: it is above Content and below
    // Dialog. A non-modal sheet is only a surface, so it registers at Content:
    // that keeps it above ordinary page widgets while a dialog that opens later
    // still restacks above it.
    const auto level =
        modal_ ? Modal::Level::Drawer : Modal::Level::Content;
    Modal::registerLayer(this, level);
    Modal::raiseWithinLayer(this);
    layerAttached_ = true;
}

void BottomSheet::detachLayer()
{
    if (!layerAttached_)
        return;

    Modal::unregisterLayer(this);
    layerAttached_ = false;
}

void BottomSheet::installFilters()
{
    if (filtersInstalled_)
        return;

    if (auto* application = QCoreApplication::instance())
        application->installEventFilter(this);

    if (QWidget* host = parentWidget())
        host->installEventFilter(this);

    filtersInstalled_ = true;
}

void BottomSheet::removeFilters()
{
    if (!filtersInstalled_)
        return;

    if (auto* application = QCoreApplication::instance())
        application->removeEventFilter(this);

    if (QWidget* host = parentWidget())
        host->removeEventFilter(this);

    filtersInstalled_ = false;
}

bool BottomSheet::isTopLayer() const
{
    QWidget* host = window();
    if (host == nullptr || host == this)
        return true;

    return Modal::topLayer(host) == this;
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

QRectF BottomSheet::surfaceRect() const
{
    // A closed sheet has no surface. open() sets the flag before it lays the
    // widget out, and the close animation keeps closing_ true until the slide
    // ends, so the panel is still painted while it is going away.
    if (!open_ && !closing_)
        return QRectF();

    const QRectF area = contentRect();
    if (area.isEmpty())
        return QRectF();

    if (!modal_) {
        // A non-modal sheet is the surface; the drag only displaces the
        // painted panel until the release either snaps it back or closes.
        if (dragOffset_ <= 0.0)
            return area;

        return area.translated(0.0, dragOffset_);
    }

    const qreal height = std::min(measuredSize().height(), area.height());
    if (height <= 0.0)
        return QRectF();

    const qreal slide = (1.0 - std::clamp(progress_, qreal(0.0), qreal(1.0))) * height;
    const qreal top = area.bottom() - height + slide + dragOffset_;

    return QRectF(area.left(), top, area.width(), height);
}

QRectF BottomSheet::scrimRect() const
{
    if (!modal_ || progress_ <= 0.0)
        return QRectF();

    return contentRect();
}

qreal BottomSheet::headerHeight() const
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    qreal height = density.dp(kHandleTopPaddingDp) + density.dp(kHandleHeightDp)
        + density.dp(kHandleToTitleGapDp);

    if (!title_.isEmpty()) {
        height += density.sp(theme.type().titleLarge.lineHeightSp)
            + density.dp(kTitleBottomPaddingDp);
    }

    return height;
}

QRectF BottomSheet::handleRect() const
{
    const QRectF surface = surfaceRect();
    if (surface.isEmpty())
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const qreal width = density.dp(kHandleWidthDp);
    const qreal height = density.dp(kHandleHeightDp);

    return QRectF(surface.center().x() - width / 2.0,
                  surface.top() + density.dp(kHandleTopPaddingDp), width, height);
}

QRectF BottomSheet::titleRect() const
{
    const QRectF surface = surfaceRect();
    if (surface.isEmpty() || title_.isEmpty())
        return QRectF();

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal padding = density.dp(kTitleSidePaddingDp);

    return QRectF(surface.left() + padding,
                  surface.top() + headerHeight() - density.dp(kTitleBottomPaddingDp)
                      - density.sp(theme.type().titleLarge.lineHeightSp),
                  std::max(qreal(0.0), surface.width() - padding * 2.0),
                  density.sp(theme.type().titleLarge.lineHeightSp));
}

QRectF BottomSheet::viewportRect() const
{
    const QRectF surface = surfaceRect();
    if (surface.isEmpty())
        return QRectF();

    const qreal header = std::min(headerHeight(), surface.height());

    return QRectF(surface.left(), surface.top() + header, surface.width(),
                  std::max(qreal(0.0), surface.height() - header));
}

QRectF BottomSheet::dragAreaRect() const
{
    const QRectF surface = surfaceRect();
    if (surface.isEmpty())
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const qreal height = std::min(
        surface.height(),
        density.dp(kHandleTopPaddingDp + kHandleHeightDp + kHandleToTitleGapDp));

    return QRectF(surface.left(), surface.top(), surface.width(),
                  std::max(qreal(0.0), height));
}

qreal BottomSheet::contentHeightForWidth(qreal width) const
{
    if (content_ == nullptr || content_->isHidden())
        return 0.0;

    const qreal available = std::max(qreal(0.0), width);

    if (auto* view = qobject_cast<View*>(content_.data())) {
        view->measure(MeasureSpec::exactly(available), MeasureSpec::unspecified());
        return std::max(qreal(0.0), view->measuredSize().height());
    }

    // A plain Qt widget has no measure contract. Its size hint is the natural
    // height; the minimum sizes are the floor, so a container with an explicit
    // minimum is not collapsed to a zero-height viewport.
    const QSize hint = content_->sizeHint();
    const QSize minimum = content_->minimumSize();
    const QSize minimumHint = content_->minimumSizeHint();
    const int height = std::max({hint.height(), minimum.height(), minimumHint.height(), 0});

    return qreal(height);
}

qreal BottomSheet::maxScrollOffset() const
{
    const QRectF viewport = viewportRect();
    if (viewport.height() <= 0.0)
        return 0.0;

    return std::max(qreal(0.0), contentHeight_ - viewport.height());
}

void BottomSheet::updateViewportGeometry()
{
    const QRectF viewport = viewportRect();
    if (viewport.isEmpty()) {
        viewport_->setVisible(false);
        contentHeight_ = 0.0;
        scrollOffset_ = 0.0;
        return;
    }

    viewport_->setVisible(true);
    viewport_->setGeometry(viewport.toAlignedRect());

    if (content_ == nullptr) {
        contentHeight_ = 0.0;
        scrollOffset_ = 0.0;
        return;
    }

    const qreal width = viewport.width();
    qreal height = contentHeightForWidth(width);

    if (qobject_cast<View*>(content_.data()) == nullptr) {
        // A plain Qt widget has no measure contract; a body that reports no
        // height fills the viewport instead of collapsing to nothing.
        height = std::max(height, viewport.height());
    }

    contentHeight_ = height;

    const qreal maximum = std::max(qreal(0.0), contentHeight_ - viewport.height());
    scrollOffset_ = std::clamp(scrollOffset_, qreal(0.0), maximum);

    const QRect target(0, qRound(-scrollOffset_), qRound(width), qRound(contentHeight_));
    if (auto* view = qobject_cast<View*>(content_.data()))
        view->arrange(target);
    else
        content_->setGeometry(target);
}

void BottomSheet::setScrollOffset(qreal offset)
{
    const qreal clamped = std::clamp(offset, qreal(0.0), maxScrollOffset());
    if (qFuzzyCompare(clamped + 1.0, scrollOffset_ + 1.0))
        return;

    scrollOffset_ = clamped;
    updateViewportGeometry();
    requestUpdate();
}

// ---------------------------------------------------------------------------
// Measure / arrange / paint
// ---------------------------------------------------------------------------

void BottomSheet::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const QWidget* host = hostWidget();
    const qreal fallbackWidth = Theme::forWidget(this).density().dp(kDefaultWidthDp);
    const qreal hostWidth = host != nullptr ? qreal(host->width()) : fallbackWidth;
    const qreal resolvedWidth = width.resolve(hostWidth > 0.0 ? hostWidth : fallbackWidth);

    const qreal maximum = maxSurfaceHeight();
    const qreal natural = headerHeight() + contentHeightForWidth(resolvedWidth);
    const qreal resolvedHeight = std::min(height.resolve(std::min(natural, maximum)), maximum);

    setMeasuredSize(QSizeF(resolvedWidth, std::max(qreal(0.0), resolvedHeight)));
}

void BottomSheet::arrangeOverride(const QRectF& content)
{
    Q_UNUSED(content);

    updateViewportGeometry();
}

void BottomSheet::paintContent(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF scrim = scrimRect();
    if (!scrim.isEmpty())
        painter.fillRect(scrim, withAlpha(colours.scrim, kScrimOpacity * progress_));

    const QRectF surface = surfaceRect();
    if (surface.isEmpty() || surface.height() <= 0.0) {
        painter.restore();
        return;
    }

    const qreal corner = density.dp(kTopCornerDp);
    const CornerRadii radii{corner, corner, 0.0, 0.0};
    const QPainterPath surfacePath = radii.toPath(surface);

    // The overlay has no parent surface behind it, so the elevation is painted
    // directly: three soft rings of shadow under the panel edge. A modal sheet
    // needs it to separate from the scrim; a non-modal one from the page.
    const qreal spread = density.dp(kShadowSpreadDp);
    painter.setPen(Qt::NoPen);
    for (int step = 3; step >= 1; --step) {
        const qreal inset = spread * static_cast<qreal>(step) / 3.0;
        painter.setBrush(withAlpha(colours.shadow, 0.10));
        painter.drawPath(radii.toPath(surface.adjusted(-inset, -inset, inset, inset)));
    }

    // Design baseline bottom sheet: surface container low. There is no outline: the shape,
    // the shadow and the scrim are what separate it from the page.
    painter.setPen(Qt::NoPen);
    painter.setBrush(colours.surfaceContainerLow);
    painter.drawPath(surfacePath);

    painter.save();
    painter.setClipPath(surfacePath);

    const QRectF handle = handleRect();
    if (!handle.isEmpty() && handle.width() > 0.0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colours.onSurfaceVariant);
        painter.drawPath(theme.shapes().full.path(handle));
    }

    const QRectF title = titleRect();
    if (!title_.isEmpty() && !title.isEmpty() && title.width() > 0.0) {
        const QFont titleFont = Font::resolve(this, theme.type().titleLarge, density);
        const QString elided =
            QFontMetricsF(titleFont).elidedText(title_, Qt::ElideRight, title.width());

        painter.setFont(titleFont);
        painter.setPen(colours.onSurface);
        painter.drawText(title, static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), elided);
    }

    // A hairline under the title keeps the viewport visually separate once the
    // body scrolls beneath it.
    if (!title_.isEmpty()) {
        const qreal y = title.bottom() + density.dp(kTitleBottomPaddingDp) - density.dp(1.0);
        painter.setPen(QPen(colours.outlineVariant, density.dp(1.0)));
        painter.drawLine(QPointF(surface.left(), y), QPointF(surface.right(), y));
    }

    painter.restore();
    painter.restore();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void BottomSheet::mousePressEvent(QMouseEvent* event)
{
    View::mousePressEvent(event);

    if (event->button() != Qt::LeftButton || !open_ || closing_)
        return;

    const QPointF position = event->position();
    const QRectF surface = surfaceRect();

    // A non-modal sheet is only the surface, so it never sees a backdrop
    // press; the check is guarded by modal_ rather than by geometry alone.
    pressedBackdrop_ = modal_ && !surface.contains(position);

    if (!pressedBackdrop_ && dragAreaRect().contains(position)) {
        dragging_ = true;
        dragStartY_ = position.y();
        dragOffset_ = 0.0;
    }
}

void BottomSheet::mouseMoveEvent(QMouseEvent* event)
{
    View::mouseMoveEvent(event);

    if (!dragging_ || !event->buttons().testFlag(Qt::LeftButton))
        return;

    // Upwards movement is ignored: the sheet has no expanded state to grow
    // into, and a negative offset would pull it off the bottom edge.
    dragOffset_ = std::max(qreal(0.0), event->position().y() - dragStartY_);
    syncLayout();
}

void BottomSheet::mouseReleaseEvent(QMouseEvent* event)
{
    View::mouseReleaseEvent(event);

    if (event->button() != Qt::LeftButton) {
        pressedBackdrop_ = false;
        dragging_ = false;
        dragOffset_ = 0.0;
        return;
    }

    const bool wasDragging = dragging_;
    const bool onBackdrop =
        pressedBackdrop_ && modal_ && !surfaceRect().contains(event->position());

    pressedBackdrop_ = false;
    dragging_ = false;

    if (wasDragging) {
        const qreal height = surfaceRect().height();
        const bool dismissed = height > 0.0 && dragOffset_ > height * kDragCloseRatio;

        if (dismissed) {
            // The offset stays for the slide-out so the panel does not jump
            // back up before it starts moving down; finishTransition() clears
            // it once the sheet is hidden.
            close();
            return;
        }

        dragOffset_ = 0.0;
        syncLayout();
        return;
    }

    if (onBackdrop && dismissOnBackdrop_)
        close();
}

void BottomSheet::keyPressEvent(QKeyEvent* event)
{
    if (modal_ && open_) {
        switch (event->key()) {
        case Qt::Key_Escape:
            close();
            event->accept();
            return;
        case Qt::Key_Tab:
            advanceFocus(true);
            event->accept();
            return;
        case Qt::Key_Backtab:
            advanceFocus(false);
            event->accept();
            return;
        default:
            break;
        }
    }

    View::keyPressEvent(event);
}

void BottomSheet::wheelEvent(QWheelEvent* event)
{
    const qreal steps = qreal(event->angleDelta().y()) / 120.0;
    if (qFuzzyIsNull(steps) || maxScrollOffset() <= 0.0) {
        event->ignore();
        return;
    }

    setScrollOffset(scrollOffset_ - steps * Theme::forWidget(this).density().dp(kWheelStepDp));
    event->accept();
}

bool BottomSheet::eventFilter(QObject* watched, QEvent* event)
{
    if (open_ || closing_) {
        if (watched == parentWidget()) {
            switch (event->type()) {
            case QEvent::Resize:
                applyOverlayGeometry();
                syncLayout();
                break;
            case QEvent::Hide:
                close();
                break;
            default:
                break;
            }
        }

        // Tab is trapped from here even when the sheet itself has focus; a key
        // event aimed at the sheet is left to keyPressEvent() so it is handled
        // exactly once. Escape aimed at a child closes from here because the
        // child would otherwise consume it.
        if (modal_ && event->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(event);
            auto* receiver = qobject_cast<QWidget*>(watched);

            if (receiver != nullptr && receiver->window() == window() && isTopLayer()) {
                switch (key->key()) {
                case Qt::Key_Escape:
                    if (watched != this) {
                        close();
                        return true;
                    }
                    break;
                case Qt::Key_Tab:
                    advanceFocus(true);
                    return true;
                case Qt::Key_Backtab:
                    advanceFocus(false);
                    return true;
                default:
                    break;
                }
            }
        }
    }

    return View::eventFilter(watched, event);
}

void BottomSheet::advanceFocus(bool forward)
{
    // The sheet is the first stop; every focusable descendant follows in
    // widget-tree order. Tab wraps inside the sheet, which is what "trap focus"
    // means for a modal overlay.
    QList<QWidget*> chain;
    if (focusPolicy() != Qt::NoFocus && isVisible() && isEnabled())
        chain.append(this);

    const QList<QWidget*> descendants = findChildren<QWidget*>();
    for (QWidget* widget : descendants) {
        if (widget == nullptr || widget == viewport_ || widget->isWindow())
            continue;

        if (widget->focusPolicy() == Qt::NoFocus)
            continue;

        if (!widget->isVisibleTo(this) || !widget->isEnabled())
            continue;

        chain.append(widget);
    }

    if (chain.isEmpty())
        return;

    const int count = static_cast<int>(chain.size());
    int index = -1;
    QWidget* current = QApplication::focusWidget();

    for (int candidate = 0; candidate < count; ++candidate) {
        if (chain.at(candidate) == current) {
            index = candidate;
            break;
        }
    }

    int next = 0;
    if (index < 0)
        next = forward ? 0 : count - 1;
    else
        next = (index + (forward ? 1 : count - 1)) % count;

    chain.at(next)->setFocus(forward ? Qt::TabFocusReason : Qt::BacktabFocusReason);
}

void BottomSheet::onThemeChanged()
{
    updateGeometry();

    if (open_ || closing_)
        relayout();
    else
        syncLayout();
}

bool BottomSheet::focusable() const
{
    return false;
}

} // namespace md
