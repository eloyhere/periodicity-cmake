// Periodicity — the Pagination component (framework.md §5.9).
//
// One row of pill buttons: first, previous, the compressed page run, next and
// last. The run is laid out from the same slot table the painter and the
// pointer path read, so "which page is under this click" cannot disagree with
// "which page was drawn there".
//
// The compression keeps a constant slot count: first, an ellipsis, a window of
// pages around the current one, an ellipsis and last. Only when the page count
// fits entirely does the row show every page.

#include <md/components/pagination.h>

#include <md/theme/theme.h>

#include <QFont>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPolygonF>

#include <algorithm>

namespace md {
namespace {

constexpr qreal kButtonDp = 40.0;
constexpr qreal kGapDp = 4.0;
constexpr qreal kInsetDp = 2.0;
constexpr int kMinVisiblePages = 5;
constexpr int kMaxVisiblePages = 21;

[[nodiscard]] QFont fontFor(const Theme& theme, const TextStyle& style)
{
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(QFont::Weight(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
    return font;
}

} // namespace

Pagination::Pagination(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setShape(Shape::large());
    setAccessibleName(tr("Pagination"));
    rebuildPages();
}

qreal Pagination::gap() const
{
    return Theme::forWidget(this).density().dp(kGapDp);
}

int Pagination::slotCount() const
{
    return 4 + static_cast<int>(items_.size());
}

qreal Pagination::buttonSize() const
{
    const Density& density = Theme::forWidget(this).density();
    const qreal preferred = density.dp(kButtonDp);
    const int total = slotCount();
    if (total <= 0)
        return preferred;

    const QRectF area = contentRect();
    const qreal space = std::max<qreal>(0.0, area.width() - (total - 1) * gap());
    return std::min(preferred, space / total);
}

QRectF Pagination::rowRect() const
{
    const QRectF area = contentRect();
    const qreal size = std::min(buttonSize(), area.height());
    const int total = slotCount();
    const qreal width = total * size + (total - 1) * gap();
    const qreal left = area.center().x() - width / 2.0;
    const qreal top = area.center().y() - size / 2.0;

    return QRectF(left, top, std::max<qreal>(0.0, width), std::max<qreal>(0.0, size));
}

QRectF Pagination::slotRect(int index) const
{
    if (index < 0 || index >= slotCount())
        return QRectF();

    const QRectF row = rowRect();
    const qreal step = row.height() + gap();

    return QRectF(row.left() + index * step, row.top(), row.height(), row.height());
}

QRectF Pagination::firstButtonRect() const
{
    return slotRect(0);
}

QRectF Pagination::previousButtonRect() const
{
    return slotRect(1);
}

QRectF Pagination::nextButtonRect() const
{
    return slotRect(2 + static_cast<int>(items_.size()));
}

QRectF Pagination::lastButtonRect() const
{
    return slotRect(3 + static_cast<int>(items_.size()));
}

QRectF Pagination::itemRect(int slot) const
{
    if (slot < 0 || slot >= static_cast<int>(items_.size()))
        return QRectF();

    return slotRect(2 + slot);
}

QRectF Pagination::pageButtonRect(int page) const
{
    for (int slot = 0; slot < static_cast<int>(items_.size()); ++slot) {
        if (items_.at(slot) == page)
            return itemRect(slot);
    }

    return QRectF();
}

bool Pagination::isPageEnabled(int page) const
{
    return page >= 1 && page <= pageCount_;
}

void Pagination::updatePages(const QList<int>& pages)
{
    items_ = pages;
    requestUpdate();
}

void Pagination::rebuildPages()
{
    QList<int> pages;
    const int count = std::max(1, pageCount_);
    const int total = std::max(kMinVisiblePages, maxVisible_);

    if (count <= total) {
        for (int page = 1; page <= count; ++page)
            pages.append(page);
    } else {
        // first, an ellipsis, the window, an ellipsis, last.
        const int window = total - 4;
        int start = current_ - (window - 1) / 2;
        start = std::clamp(start, 2, count - window - 1);

        pages.append(1);
        pages.append(-1);
        for (int offset = 0; offset < window; ++offset)
            pages.append(start + offset);
        pages.append(-1);
        pages.append(count);
    }

    updatePages(pages);
}

void Pagination::setPageCount(int count)
{
    const int next = std::max(1, count);
    if (next == pageCount_)
        return;

    pageCount_ = next;
    const int previousPage = current_;
    current_ = std::clamp(current_, 1, pageCount_);

    rebuildPages();
    requestUpdate();

    if (current_ != previousPage)
        emit pageChanged(current_);
}

void Pagination::setCurrentPage(int page)
{
    const int next = std::clamp(page, 1, pageCount_);
    if (next == current_)
        return;

    current_ = next;
    rebuildPages();
    requestUpdate();
    emit pageChanged(current_);
}

void Pagination::first()
{
    setCurrentPage(1);
}

void Pagination::last()
{
    setCurrentPage(pageCount_);
}

void Pagination::previousPage()
{
    setCurrentPage(current_ - 1);
}

void Pagination::nextPage()
{
    setCurrentPage(current_ + 1);
}

void Pagination::setMaxVisiblePages(int count)
{
    const int next = std::clamp(count, kMinVisiblePages, kMaxVisiblePages);
    if (next == maxVisible_)
        return;

    maxVisible_ = next;
    rebuildPages();
}

void Pagination::activateSlot(int index)
{
    const int total = slotCount();
    if (index < 0 || index >= total)
        return;

    const int itemCount = static_cast<int>(items_.size());
    if (index == 0) {
        first();
        return;
    }

    if (index == 1) {
        previousPage();
        return;
    }

    if (index == 2 + itemCount) {
        nextPage();
        return;
    }

    if (index == 3 + itemCount) {
        last();
        return;
    }

    const int page = items_.at(index - 2);
    if (page > 0)
        setCurrentPage(page);
}

void Pagination::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    setMeasuredSize(QSizeF(width.resolve(density.dp(360.0)), height.resolve(density.dp(48.0))));
}

void Pagination::paintContent(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);

    if (slotCount() <= 0)
        return;

    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();
    const Density& density = theme.density();
    const qreal unit = density.dp(1.0);
    const qreal baseOpacity = isEnabled() ? 1.0 : 0.38;
    const qreal inset = density.dp(kInsetDp);
    const bool atFirst = current_ <= 1;
    const bool atLast = current_ >= pageCount_;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setOpacity(baseOpacity);

    const auto paintChevron = [&](const QRectF& box, bool forward, bool toEnd, bool available) {
        if (box.isEmpty())
            return;

        const QRectF pill = box.adjusted(inset, inset, -inset, -inset);
        const qreal radius = pill.height() / 2.0;
        const QColor tint = available ? colours.onSurfaceVariant : colours.outline;

        painter.save();
        painter.setOpacity(baseOpacity * (available ? 1.0 : 0.38));

        if (toEnd) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(available ? colours.secondaryContainer : colours.surfaceContainerHigh);
            painter.drawRoundedRect(pill, radius, radius);
        }

        painter.setPen(QPen(tint, unit * 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPointF centre = pill.center();
        const qreal sign = forward ? 1.0 : -1.0;
        QPolygonF chevron;
        chevron << QPointF(centre.x() - sign * unit * 2.5, centre.y() - unit * 4.0)
                << QPointF(centre.x() + sign * unit * 2.0, centre.y())
                << QPointF(centre.x() - sign * unit * 2.5, centre.y() + unit * 4.0);
        painter.drawPolyline(chevron);

        if (toEnd) {
            const qreal barX = centre.x() + sign * unit * 4.5;
            painter.drawLine(QPointF(barX, centre.y() - unit * 4.0),
                             QPointF(barX, centre.y() + unit * 4.0));
        }

        painter.restore();
    };

    paintChevron(firstButtonRect(), false, true, !atFirst);
    paintChevron(previousButtonRect(), false, false, !atFirst);
    paintChevron(nextButtonRect(), true, false, !atLast);
    paintChevron(lastButtonRect(), true, true, !atLast);

    const QFont font = fontFor(theme, theme.type().labelLarge);
    painter.setFont(font);

    for (int slot = 0; slot < static_cast<int>(items_.size()); ++slot) {
        const QRectF box = itemRect(slot);
        if (box.isEmpty())
            continue;

        const QRectF pill = box.adjusted(inset, inset, -inset, -inset);
        const qreal radius = pill.height() / 2.0;
        const int page = items_.at(slot);

        if (page < 0) {
            painter.setPen(colours.onSurfaceVariant);
            painter.drawText(pill, Qt::AlignCenter, QStringLiteral("\u2026"));
            continue;
        }

        const bool active = page == current_;
        if (active) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colours.primary);
            painter.drawRoundedRect(pill, radius, radius);
        }

        painter.setPen(active ? colours.onPrimary : colours.onSurface);
        painter.drawText(pill, Qt::AlignCenter, QString::number(page));
    }

    painter.restore();
}

void Pagination::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        pressed_ = event->position();

    View::mousePressEvent(event);
}

void Pagination::mouseReleaseEvent(QMouseEvent* event)
{
    const QPointF press = pressed_;
    pressed_ = QPointF(-1.0, -1.0);
    View::mouseReleaseEvent(event);

    if (!isEnabled() || event->button() != Qt::LeftButton || press.x() < 0.0)
        return;

    const QPointF release = event->position();
    for (int slot = 0; slot < slotCount(); ++slot) {
        const QRectF box = slotRect(slot);
        if (box.isEmpty() || !box.contains(press) || !box.contains(release))
            continue;

        activateSlot(slot);
        return;
    }
}

void Pagination::keyPressEvent(QKeyEvent* event)
{
    if (!isEnabled()) {
        View::keyPressEvent(event);
        return;
    }

    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Up:
    case Qt::Key_PageUp:
        previousPage();
        break;
    case Qt::Key_Right:
    case Qt::Key_Down:
    case Qt::Key_PageDown:
        nextPage();
        break;
    case Qt::Key_Home:
        first();
        break;
    case Qt::Key_End:
        last();
        break;
    default:
        View::keyPressEvent(event);
        return;
    }

    event->accept();
}

} // namespace md
