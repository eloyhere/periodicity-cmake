// Periodicity — the Timeline component (framework.md §5.9).
//
// The rail is one line from the first marker to the last one — not a line per
// item — which is what makes the last entry terminate the timeline instead of
// leaving a stub below it. Markers are painted after the rail so a marker is
// never cut by the line, and the text block is elided inside the row so a long
// description cannot push the following item down.
//
// Scrolling is a pure offset into the item list: itemRect() reports the
// scrolled row, and maximumScrollOffset() is derived from contentHeight() and
// the viewport, so the wheel and the keyboard cannot scroll past the end.

#include <md/components/timeline.h>

#include <md/theme/theme.h>

#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QWheelEvent>

#include <algorithm>

namespace md {
namespace {

constexpr qreal kPaddingXDp = 16.0;
constexpr qreal kPaddingYDp = 8.0;
constexpr qreal kItemHeightDp = 72.0;
constexpr qreal kTimeColumnDp = 52.0;
constexpr qreal kRailInsetDp = 12.0;
constexpr qreal kRailWidthDp = 2.0;
constexpr qreal kMarkerDp = 12.0;
constexpr qreal kIconMarkerDp = 24.0;
constexpr qreal kMarkerTopDp = 18.0;
constexpr qreal kTextGapDp = 16.0;
constexpr qreal kTextTopDp = 8.0;

[[nodiscard]] QFont fontFor(const Theme& theme, const TextStyle& style)
{
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(QFont::Weight(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
    return font;
}

} // namespace

Timeline::Timeline(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setShape(Shape::large());
    setAccessibleName(tr("Timeline"));
}

qreal Timeline::padding() const
{
    return Theme::forWidget(this).density().dp(kPaddingYDp);
}

qreal Timeline::itemHeight() const
{
    return Theme::forWidget(this).density().dp(kItemHeightDp);
}

qreal Timeline::timeColumnWidth() const
{
    return Theme::forWidget(this).density().dp(kTimeColumnDp);
}

qreal Timeline::railX() const
{
    const Density& density = Theme::forWidget(this).density();
    return contentRect().left() + density.dp(kPaddingXDp) + timeColumnWidth()
           + density.dp(kRailInsetDp);
}

int Timeline::itemCount() const
{
    return static_cast<int>(items_.size());
}

qreal Timeline::contentHeight() const
{
    return 2.0 * padding() + qreal(itemCount()) * itemHeight();
}

qreal Timeline::maximumScrollOffset() const
{
    const qreal viewport = contentRect().height();
    return std::max<qreal>(0.0, contentHeight() - viewport);
}

void Timeline::clampScroll()
{
    scrollOffset_ = std::clamp(scrollOffset_, qreal(0.0), maximumScrollOffset());
}

void Timeline::setScrollOffset(qreal offset)
{
    const qreal next = std::clamp(offset, qreal(0.0), maximumScrollOffset());
    if (qFuzzyCompare(next + 1.0, scrollOffset_ + 1.0))
        return;

    scrollOffset_ = next;
    requestUpdate();
}

void Timeline::setItems(const QList<TimelineItem>& items)
{
    items_ = items;
    clampScroll();
    requestUpdate();
}

void Timeline::addItem(const TimelineItem& item)
{
    items_.append(item);
    clampScroll();
    requestUpdate();
}

void Timeline::insertItem(int index, const TimelineItem& item)
{
    const int position = std::clamp(index, 0, itemCount());
    items_.insert(position, item);
    clampScroll();
    requestUpdate();
}

void Timeline::removeItem(int index)
{
    if (index < 0 || index >= itemCount())
        return;

    items_.removeAt(index);
    clampScroll();
    requestUpdate();
}

void Timeline::clear()
{
    if (items_.isEmpty())
        return;

    items_.clear();
    scrollOffset_ = 0.0;
    requestUpdate();
}

QRectF Timeline::itemRect(int index) const
{
    if (index < 0 || index >= itemCount())
        return QRectF();

    const QRectF area = contentRect();
    const qreal top = area.top() + padding() + qreal(index) * itemHeight() - scrollOffset_;

    return QRectF(area.left(), top, std::max<qreal>(0.0, area.width()),
                  std::max<qreal>(0.0, itemHeight()));
}

QRectF Timeline::markerRect(int index) const
{
    const QRectF row = itemRect(index);
    if (row.isEmpty())
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const qreal side = density.dp(kIconMarkerDp);
    const qreal centreY = row.top() + density.dp(kMarkerTopDp);

    return QRectF(railX() - side / 2.0, centreY - side / 2.0, side, side);
}

QRectF Timeline::timeRect(int index) const
{
    const QRectF row = itemRect(index);
    if (row.isEmpty())
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const qreal left = row.left() + density.dp(kPaddingXDp);

    return QRectF(left, row.top() + density.dp(kTextTopDp), std::max<qreal>(0.0, timeColumnWidth()),
                  density.dp(kMarkerTopDp));
}

QRectF Timeline::textRect(int index) const
{
    const QRectF row = itemRect(index);
    if (row.isEmpty())
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const qreal left = railX() + density.dp(kTextGapDp);
    const qreal right = row.right() - density.dp(kPaddingXDp);

    return QRectF(left, row.top() + density.dp(kTextTopDp),
                  std::max<qreal>(0.0, right - left),
                  std::max<qreal>(0.0, row.height() - 2.0 * density.dp(kTextTopDp)));
}

int Timeline::itemAt(const QPointF& point) const
{
    if (!contentRect().contains(point))
        return -1;

    for (int index = 0; index < itemCount(); ++index) {
        if (itemRect(index).contains(point))
            return index;
    }

    return -1;
}

void Timeline::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    setMeasuredSize(QSizeF(width.resolve(density.dp(320.0)), height.resolve(contentHeight())));
}

void Timeline::paintContent(QPainter& painter, const QRectF& content)
{
    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();
    const Density& density = theme.density();
    const qreal unit = density.dp(1.0);
    const qreal baseOpacity = isEnabled() ? 1.0 : 0.38;
    const QRectF area = contentRect();

    if (area.isEmpty())
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setOpacity(baseOpacity);
    painter.setClipRect(content);

    const QFont timeFont = fontFor(theme, theme.type().labelMedium);
    const QFont titleFont = fontFor(theme, theme.type().titleSmall);
    const QFont bodyFont = fontFor(theme, theme.type().bodySmall);
    const QFontMetricsF titleMetrics(titleFont);
    const QFontMetricsF bodyMetrics(bodyFont);

    // The rail: one segment from the first marker to the last, so the last item
    // ends the line rather than a trailing stub below it.
    if (itemCount() > 1) {
        const QRectF first = markerRect(0);
        const QRectF last = markerRect(itemCount() - 1);
        QColor railColour = colours.outlineVariant;
        railColour.setAlphaF(0.9f);

        painter.setPen(QPen(railColour, density.dp(kRailWidthDp), Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(railX(), first.center().y()), QPointF(railX(), last.center().y()));
    }

    for (int index = 0; index < itemCount(); ++index) {
        const QRectF row = itemRect(index);
        if (row.bottom() < area.top() || row.top() > area.bottom())
            continue;

        const TimelineItem& item = items_.at(index);

        // Time, right-aligned against the rail.
        painter.setFont(timeFont);
        painter.setPen(colours.onSurfaceVariant);
        painter.drawText(timeRect(index), Qt::AlignRight | Qt::AlignTop, item.time);

        // Marker: a dot, or a tonal disc carrying the icon text.
        const QRectF marker = markerRect(index);
        const QColor tint = item.color.isValid() ? item.color : colours.primary;
        const bool hasIcon = !item.icon.isEmpty();
        const qreal side = density.dp(hasIcon ? kIconMarkerDp : kMarkerDp);
        const QRectF disc(marker.center().x() - side / 2.0, marker.center().y() - side / 2.0,
                          side, side);

        painter.setPen(Qt::NoPen);
        painter.setBrush(tint);
        painter.drawEllipse(disc);

        if (hasIcon) {
            painter.setFont(timeFont);
            painter.setPen(colours.onPrimary);
            painter.drawText(disc, Qt::AlignCenter, item.icon);
        }

        // Title and description.
        const QRectF text = textRect(index);
        const qreal titleHeight = titleMetrics.height();
        const QRectF titleBox(text.left(), text.top(), text.width(), titleHeight);

        painter.setFont(titleFont);
        painter.setPen(colours.onSurface);
        painter.drawText(titleBox, Qt::AlignLeft | Qt::AlignVCenter,
                         titleMetrics.elidedText(item.title, Qt::ElideRight, titleBox.width()));

        if (!item.description.isEmpty()) {
            const QRectF bodyBox(text.left(), titleBox.bottom() + unit * 2.0, text.width(),
                                 std::max<qreal>(0.0, text.bottom() - titleBox.bottom() - unit * 2.0));
            painter.setFont(bodyFont);
            painter.setPen(colours.onSurfaceVariant);
            painter.drawText(bodyBox, Qt::AlignLeft | Qt::AlignTop,
                             bodyMetrics.elidedText(item.description, Qt::ElideRight,
                                                    bodyBox.width()));
        }
    }

    painter.restore();
}

void Timeline::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        pressed_ = event->position();

    View::mousePressEvent(event);
}

void Timeline::mouseReleaseEvent(QMouseEvent* event)
{
    const QPointF press = pressed_;
    pressed_ = QPointF(-1.0, -1.0);
    View::mouseReleaseEvent(event);

    if (!isEnabled() || event->button() != Qt::LeftButton || press.x() < 0.0)
        return;

    const int pressedItem = itemAt(press);
    if (pressedItem < 0)
        return;

    const int releasedItem = itemAt(event->position());
    if (pressedItem == releasedItem)
        emit itemClicked(releasedItem);
}

void Timeline::keyPressEvent(QKeyEvent* event)
{
    if (!isEnabled()) {
        View::keyPressEvent(event);
        return;
    }

    switch (event->key()) {
    case Qt::Key_Up:
        setScrollOffset(scrollOffset_ - itemHeight());
        break;
    case Qt::Key_Down:
        setScrollOffset(scrollOffset_ + itemHeight());
        break;
    case Qt::Key_PageUp:
        setScrollOffset(scrollOffset_ - contentRect().height() * 0.9);
        break;
    case Qt::Key_PageDown:
        setScrollOffset(scrollOffset_ + contentRect().height() * 0.9);
        break;
    case Qt::Key_Home:
        setScrollOffset(0.0);
        break;
    case Qt::Key_End:
        setScrollOffset(maximumScrollOffset());
        break;
    default:
        View::keyPressEvent(event);
        return;
    }

    event->accept();
}

void Timeline::wheelEvent(QWheelEvent* event)
{
    if (!isEnabled()) {
        event->ignore();
        return;
    }

    qreal delta = 0.0;
    const QPoint angle = event->angleDelta();
    if (!angle.isNull())
        delta = -qreal(angle.y()) / 120.0 * itemHeight() * 0.75;
    else
        delta = -event->pixelDelta().y();

    if (!qFuzzyIsNull(delta))
        setScrollOffset(scrollOffset_ + delta);

    event->accept();
}

} // namespace md
