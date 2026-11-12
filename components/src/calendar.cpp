// Periodicity — the Calendar component (framework.md §5.9).
//
// The sheet is painted in one pass from the theme tokens: the header (two
// arrows and the month/year buttons), the weekday row and then either the
// six-week day grid or one of the two coarser pages. The day grid is data
// driven — a CalendarCell supplies a label, a marker dot and an enabled flag —
// while the selected, today and current-month treatments stay the component's
// own so a caller cannot paint an inconsistent grid by accident.
//
// Pointer input is validated against the same rectangle helpers that paint the
// pages, so the hit targets and the pixels cannot drift apart. Keyboard
// navigation moves the selection by day, week or month and clamps it to the
// date range; Enter activates the current selection.

#include <md/components/calendar.h>

#include <md/theme/theme.h>

#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace md {
namespace {

constexpr int kWeekColumns = 7;
constexpr int kWeekRows = 6;
constexpr int kDayCells = kWeekColumns * kWeekRows;
constexpr int kMonthsPerPage = 12;
constexpr int kYearsPerPage = 12;

constexpr qreal kPaddingDp = 12.0;
constexpr qreal kHeaderDp = 44.0;
constexpr qreal kArrowBoxDp = 40.0;
constexpr qreal kButtonGapDp = 8.0;
constexpr qreal kWeekdayDp = 26.0;
constexpr qreal kHeaderGapDp = 2.0;
constexpr qreal kBodyGapDp = 6.0;
constexpr qreal kCellInsetDp = 4.0;
constexpr qreal kMaxCircleDp = 36.0;
constexpr qreal kMarkerDp = 4.0;
constexpr qreal kLabelGapDp = 1.0;

[[nodiscard]] QFont fontFor(const Theme& theme, const TextStyle& style)
{
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(QFont::Weight(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
    return font;
}

} // namespace

Calendar::Calendar(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    // The calendar is a picker surface, so it takes the extra-large shape of the
    // design baseline (28 dp) rather than the large one it shared with cards.
    setShape(Shape::extraLarge());
    setAccessibleName(tr("Calendar"));
}

qreal Calendar::padding() const
{
    return Theme::forWidget(this).density().dp(kPaddingDp);
}

QDate Calendar::gridStart() const
{
    // Monday-first: dayOfWeek() is 1 for Monday, so the grid starts on the
    // Monday on or before the first of the displayed month.
    return month_.addDays(1 - month_.dayOfWeek());
}

QDate Calendar::clampDate(QDate date) const
{
    if (!date.isValid())
        return selected_;
    if (date < minimum_)
        return minimum_;
    if (date > maximum_)
        return maximum_;
    return date;
}

QRectF Calendar::headerRect() const
{
    const qreal pad = padding();
    return QRectF(pad, pad, std::max<qreal>(0.0, qreal(width()) - 2.0 * pad),
                  Theme::forWidget(this).density().dp(kHeaderDp));
}

QRectF Calendar::previousButtonRect() const
{
    const QRectF header = headerRect();
    const qreal side = std::min(Theme::forWidget(this).density().dp(kArrowBoxDp),
                                header.width() / 3.0);
    return QRectF(header.left(), header.top(), std::max<qreal>(0.0, side), header.height());
}

QRectF Calendar::nextButtonRect() const
{
    const QRectF header = headerRect();
    const qreal side = std::min(Theme::forWidget(this).density().dp(kArrowBoxDp),
                                header.width() / 3.0);
    return QRectF(header.right() - std::max<qreal>(0.0, side), header.top(),
                  std::max<qreal>(0.0, side), header.height());
}

QRectF Calendar::monthButtonRect() const
{
    const QRectF header = headerRect();
    const qreal gap = Theme::forWidget(this).density().dp(kButtonGapDp);
    const qreal left = previousButtonRect().right() + gap;
    const qreal right = nextButtonRect().left() - gap;
    const qreal available = std::max<qreal>(0.0, right - left);
    const qreal share = page_ == Page::Days ? 0.55 : 1.0;

    return QRectF(left, header.top(), available * share, header.height());
}

QRectF Calendar::yearButtonRect() const
{
    if (page_ != Page::Days)
        return QRectF();

    const QRectF header = headerRect();
    const QRectF monthBox = monthButtonRect();
    const qreal gap = Theme::forWidget(this).density().dp(kButtonGapDp);
    const qreal left = monthBox.right() + gap;
    const qreal right = nextButtonRect().left() - gap;

    return QRectF(left, header.top(), std::max<qreal>(0.0, right - left), header.height());
}

QRectF Calendar::weekdayRect(int column) const
{
    if (column < 0 || column >= kWeekColumns)
        return QRectF();

    const QRectF header = headerRect();
    const Density& density = Theme::forWidget(this).density();
    const qreal cellWidth = header.width() / kWeekColumns;
    const qreal top = header.bottom() + density.dp(kHeaderGapDp);

    return QRectF(header.left() + column * cellWidth, top, std::max<qreal>(0.0, cellWidth),
                  density.dp(kWeekdayDp));
}

QRectF Calendar::cellRect(QDate date) const
{
    if (!date.isValid())
        return QRectF();

    const qint64 offset = gridStart().daysTo(date);
    if (offset < 0 || offset >= kDayCells)
        return QRectF();

    const QRectF header = headerRect();
    const Density& density = Theme::forWidget(this).density();
    const qreal cellWidth = header.width() / kWeekColumns;
    const qreal gridTop = weekdayRect(0).bottom() + density.dp(kHeaderGapDp);
    const qreal gridHeight = std::max<qreal>(0.0, qreal(height()) - gridTop - padding());
    const qreal cellHeight = gridHeight / kWeekRows;

    const int row = static_cast<int>(offset / kWeekColumns);
    const int column = static_cast<int>(offset % kWeekColumns);

    return QRectF(header.left() + column * cellWidth, gridTop + row * cellHeight,
                  std::max<qreal>(0.0, cellWidth), std::max<qreal>(0.0, cellHeight));
}

QRectF Calendar::bodyRect() const
{
    const QRectF header = headerRect();
    const qreal top = header.bottom() + Theme::forWidget(this).density().dp(kBodyGapDp);
    return QRectF(header.left(), top, header.width(),
                  std::max<qreal>(0.0, qreal(height()) - top - padding()));
}

QRectF Calendar::monthRect(int month) const
{
    if (month < 1 || month > kMonthsPerPage)
        return QRectF();

    const QRectF area = bodyRect();
    const int index = month - 1;
    const qreal cellWidth = area.width() / 3.0;
    const qreal cellHeight = area.height() / 4.0;

    return QRectF(area.left() + (index % 3) * cellWidth, area.top() + (index / 3) * cellHeight,
                  std::max<qreal>(0.0, cellWidth), std::max<qreal>(0.0, cellHeight));
}

int Calendar::yearPageStart() const
{
    return std::max(1, (month_.year() / kYearsPerPage) * kYearsPerPage);
}

QRectF Calendar::yearRect(int year) const
{
    const int start = yearPageStart();
    const int index = year - start;
    if (index < 0 || index >= kYearsPerPage)
        return QRectF();

    const QRectF area = bodyRect();
    const qreal cellWidth = area.width() / 3.0;
    const qreal cellHeight = area.height() / 4.0;

    return QRectF(area.left() + (index % 3) * cellWidth, area.top() + (index / 3) * cellHeight,
                  std::max<qreal>(0.0, cellWidth), std::max<qreal>(0.0, cellHeight));
}

int Calendar::indexOfCell(QDate date) const
{
    for (int index = 0; index < static_cast<int>(cells_.size()); ++index) {
        if (cells_.at(index).date == date)
            return index;
    }

    return -1;
}

CalendarCell Calendar::storedCell(QDate date) const
{
    const int index = indexOfCell(date);
    return index >= 0 ? cells_.at(index) : CalendarCell{date, true, false, false, QString(),
                                                        QColor(), true};
}

bool Calendar::isDateEnabled(QDate date) const
{
    if (!date.isValid() || date < minimum_ || date > maximum_)
        return false;

    const int index = indexOfCell(date);
    return index < 0 || cells_.at(index).enabled;
}

CalendarCell Calendar::cellForDate(QDate date) const
{
    CalendarCell cell = storedCell(date);
    cell.date = date;
    cell.enabled = isDateEnabled(date);
    cell.selected = cell.selected || date == selected_;
    cell.today = cell.today || date == QDate::currentDate();
    cell.currentMonth = date.year() == month_.year() && date.month() == month_.month();
    return cell;
}

void Calendar::setCells(const QList<CalendarCell>& cells)
{
    cells_ = cells;
    requestUpdate();
}

void Calendar::setCell(const CalendarCell& cell)
{
    if (!cell.date.isValid())
        return;

    const int index = indexOfCell(cell.date);
    if (index >= 0)
        cells_[index] = cell;
    else
        cells_.append(cell);

    requestUpdate();
}

void Calendar::clearCells()
{
    if (cells_.isEmpty())
        return;

    cells_.clear();
    requestUpdate();
}

void Calendar::setMarkedDates(const QList<QDate>& dates, const QColor& marker)
{
    const QColor colour = marker.isValid() ? marker
                                           : Theme::forWidget(this).colours().tertiary;
    for (const QDate& date : dates) {
        if (!date.isValid())
            continue;

        CalendarCell cell = storedCell(date);
        cell.date = date;
        cell.marker = colour;
        setCell(cell);
    }
}

void Calendar::clearMarkedDates()
{
    bool changed = false;
    for (CalendarCell& cell : cells_) {
        if (!cell.marker.isValid())
            continue;

        cell.marker = QColor();
        changed = true;
    }

    if (changed)
        requestUpdate();
}

void Calendar::setSelectedDate(QDate date)
{
    if (!date.isValid())
        return;

    const QDate next = clampDate(date);
    const bool changed = next != selected_;
    selected_ = next;
    setDisplayedMonth(next);

    if (changed) {
        requestUpdate();
        emit selectedDateChanged(selected_);
    }
}

void Calendar::setDateRange(QDate minimum, QDate maximum)
{
    if (!minimum.isValid() || !maximum.isValid() || minimum > maximum)
        return;

    minimum_ = minimum;
    maximum_ = maximum;

    const QDate next = clampDate(selected_);
    const bool changed = next != selected_;
    selected_ = next;

    setDisplayedMonth(next);
    requestUpdate();

    if (changed)
        emit selectedDateChanged(selected_);
}

void Calendar::setMinimumDate(QDate date)
{
    if (!date.isValid() || date > maximum_)
        return;

    setDateRange(date, maximum_);
}

void Calendar::setMaximumDate(QDate date)
{
    if (!date.isValid() || date < minimum_)
        return;

    setDateRange(minimum_, date);
}

void Calendar::setDisplayedMonth(QDate date)
{
    if (!date.isValid())
        return;

    const QDate firstMonth(minimum_.year(), minimum_.month(), 1);
    const QDate lastMonth(maximum_.year(), maximum_.month(), 1);
    QDate next(date.year(), date.month(), 1);

    if (next < firstMonth)
        next = firstMonth;
    if (next > lastMonth)
        next = lastMonth;

    if (month_ == next)
        return;

    month_ = next;
    requestUpdate();
    emit monthChanged(month_);
}

void Calendar::previousMonth()
{
    setDisplayedMonth(month_.addMonths(-1));
}

void Calendar::nextMonth()
{
    setDisplayedMonth(month_.addMonths(1));
}

void Calendar::setPage(Page page)
{
    if (page_ == page)
        return;

    page_ = page;
    requestUpdate();
    emit pageChanged(page_);
}

QString Calendar::monthButtonText() const
{
    switch (page_) {
    case Page::Days:
        return locale().standaloneMonthName(month_.month(), QLocale::LongFormat);
    case Page::Months:
        return QString::number(month_.year());
    case Page::Years:
        return QStringLiteral("%1 \u2013 %2")
            .arg(yearPageStart())
            .arg(yearPageStart() + kYearsPerPage - 1);
    }

    return QString();
}

QString Calendar::yearButtonText() const
{
    if (page_ != Page::Days)
        return QString();

    return QString::number(month_.year());
}

bool Calendar::isMonthEnabled(int month) const
{
    if (month < 1 || month > kMonthsPerPage)
        return false;

    const QDate first(month_.year(), month, 1);
    const QDate last(first.year(), first.month(), first.daysInMonth());
    return first <= maximum_ && last >= minimum_;
}

bool Calendar::isYearEnabled(int year) const
{
    if (year < 1 || year > 9999)
        return false;

    return QDate(year, 1, 1) <= maximum_ && QDate(year, 12, 31) >= minimum_;
}

bool Calendar::canNavigate(bool forward) const
{
    const QDate firstMonth(minimum_.year(), minimum_.month(), 1);
    const QDate lastMonth(maximum_.year(), maximum_.month(), 1);

    switch (page_) {
    case Page::Days:
        return forward ? month_ < lastMonth : month_ > firstMonth;
    case Page::Months:
        return forward ? month_.year() < maximum_.year() : month_.year() > minimum_.year();
    case Page::Years: {
        const int start = yearPageStart();
        return forward ? start + kYearsPerPage <= maximum_.year() : start > minimum_.year();
    }
    }

    return false;
}

void Calendar::navigate(int direction)
{
    if (direction == 0 || !canNavigate(direction > 0))
        return;

    switch (page_) {
    case Page::Days:
        setDisplayedMonth(month_.addMonths(direction));
        break;
    case Page::Months:
        setDisplayedMonth(month_.addYears(direction));
        break;
    case Page::Years: {
        const int start = std::clamp(yearPageStart() + kYearsPerPage * direction, minimum_.year(),
                                     maximum_.year());
        setDisplayedMonth(QDate(start, month_.month(), 1));
        break;
    }
    }
}

void Calendar::activateDate(QDate date)
{
    if (!isEnabled() || !isDateEnabled(date))
        return;

    setSelectedDate(date);
    emit cellClicked(cellForDate(date));
    emit dateSelected(date);
}

void Calendar::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    setMeasuredSize(QSizeF(width.resolve(density.dp(340.0)), height.resolve(density.dp(336.0))));
}

void Calendar::paintContent(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);

    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();
    const Density& density = theme.density();
    const qreal unit = density.dp(1.0);
    const qreal baseOpacity = isEnabled() ? 1.0 : 0.38;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setOpacity(baseOpacity);

    // Navigation arrows.
    for (int index = 0; index < 2; ++index) {
        const bool forward = index == 1;
        const QRectF box = forward ? nextButtonRect() : previousButtonRect();
        const bool available = canNavigate(forward);

        painter.save();
        painter.setOpacity(baseOpacity * (available ? 1.0 : 0.38));
        painter.setPen(QPen(colours.onSurfaceVariant, unit * 2.0, Qt::SolidLine, Qt::RoundCap,
                            Qt::RoundJoin));
        const QPointF centre = box.center();
        const qreal sign = forward ? 1.0 : -1.0;
        QPolygonF chevron;
        chevron << QPointF(centre.x() - sign * unit * 3.0, centre.y() - unit * 5.0)
                << QPointF(centre.x() + sign * unit * 3.0, centre.y())
                << QPointF(centre.x() - sign * unit * 3.0, centre.y() + unit * 5.0);
        painter.drawPolyline(chevron);
        painter.restore();
    }

    // The month / year buttons: a tonal pill each, so they read as buttons and
    // not as a heading.
    painter.setFont(fontFor(theme, theme.type().titleSmall));
    const auto paintHeaderButton = [&](const QRectF& box, const QString& text, bool active) {
        if (box.isEmpty() || text.isEmpty())
            return;

        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(active ? colours.secondaryContainer : colours.surfaceContainerHigh);
        // The small shape token (8 dp); the header pill used a literal 10 dp,
        // which is not one of the design baseline shape steps.
        painter.drawPath(theme.shapes().small.path(box));
        painter.setPen(active ? colours.onSecondaryContainer : colours.onSurface);
        painter.drawText(box, Qt::AlignCenter, text);
        painter.restore();
    };

    paintHeaderButton(monthButtonRect(), monthButtonText(), page_ != Page::Days);
    paintHeaderButton(yearButtonRect(), yearButtonText(), false);

    if (page_ == Page::Days) {
        painter.setFont(fontFor(theme, theme.type().labelSmall));
        painter.setPen(colours.onSurfaceVariant);
        for (int column = 0; column < kWeekColumns; ++column) {
            painter.drawText(weekdayRect(column), Qt::AlignCenter,
                             locale().dayName(column + 1, QLocale::ShortFormat));
        }

        const QFont dayFont = fontFor(theme, theme.type().bodyMedium);
        const QFont labelFont = fontFor(theme, theme.type().labelSmall);
        const QFontMetricsF labelMetrics(labelFont);

        for (int index = 0; index < kDayCells; ++index) {
            const QDate date = gridStart().addDays(index);
            const QRectF cell = cellRect(date);
            if (cell.isEmpty())
                continue;

            const CalendarCell value = cellForDate(date);
            const qreal diameter = std::min({cell.width() - unit * kCellInsetDp,
                                             cell.height() - unit * kCellInsetDp,
                                             unit * kMaxCircleDp});
            if (diameter <= 0.0)
                continue;

            const bool hasLabel = !value.label.isEmpty();
            const qreal labelHeight = hasLabel ? std::min(labelMetrics.height(), cell.height() / 4.0)
                                               : 0.0;
            const qreal centreY = cell.center().y() - labelHeight / 2.0;
            const QRectF circle(cell.center().x() - diameter / 2.0, centreY - diameter / 2.0,
                                diameter, diameter);

            painter.save();
            painter.setOpacity(baseOpacity * (value.enabled ? (value.currentMonth ? 1.0 : 0.55)
                                                            : 0.32));

            if (value.selected) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(colours.primary);
                painter.drawEllipse(circle);
            } else if (value.today) {
                // Today is a 1 dp primary ring on the 40 dp day circle.
                painter.setPen(QPen(colours.primary, unit * 1.0));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(circle);
            }

            // Offset focus ring in on-surface: a primary ring on the primary
            // fill of the selected day was invisible, so keyboard focus had no
            // visible indicator at all.
            if (value.selected && hasFocus()) {
                painter.setPen(QPen(colours.onSurface, unit * 1.5));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(circle.adjusted(-unit * 2.0, -unit * 2.0, unit * 2.0,
                                                    unit * 2.0));
            }

            painter.setFont(dayFont);
            painter.setPen(value.selected ? colours.onPrimary
                                          : (value.enabled ? colours.onSurface
                                                           : colours.onSurfaceVariant));
            painter.drawText(circle, Qt::AlignCenter, QString::number(date.day()));

            if (hasLabel) {
                const QRectF labelBox(cell.left(), centreY + diameter / 2.0,
                                      cell.width(), labelHeight + unit * kLabelGapDp);
                painter.setFont(labelFont);
                painter.setPen(value.selected ? colours.onPrimary : colours.tertiary);
                painter.drawText(labelBox, Qt::AlignHCenter | Qt::AlignTop,
                                 labelMetrics.elidedText(value.label, Qt::ElideRight,
                                                         labelBox.width()));
            }

            if (value.marker.isValid()) {
                const qreal side = unit * kMarkerDp;
                const qreal markerY = std::min(cell.bottom() - side, centreY + diameter / 2.0
                                                                          + labelHeight
                                                                          + unit * 2.0);
                painter.setPen(Qt::NoPen);
                painter.setBrush(value.marker);
                painter.drawEllipse(QRectF(cell.center().x() - side / 2.0, markerY, side, side));
            }

            painter.restore();
        }
    } else if (page_ == Page::Months) {
        painter.setFont(fontFor(theme, theme.type().titleSmall));

        for (int month = 1; month <= kMonthsPerPage; ++month) {
            const QRectF cell = monthRect(month);
            if (cell.isEmpty())
                continue;

            const bool available = isMonthEnabled(month);
            const bool selected = month == month_.month();
            const QRectF pill = cell.adjusted(unit * kCellInsetDp, unit * kCellInsetDp,
                                              -unit * kCellInsetDp, -unit * kCellInsetDp);

            painter.save();
            painter.setOpacity(baseOpacity * (available ? 1.0 : 0.32));
            if (selected && available) {
                // The design baseline coarse-page selection is a tonal primary-container
                // pill; primary/on-primary stays reserved for the day circle.
                painter.setPen(Qt::NoPen);
                painter.setBrush(colours.primaryContainer);
                painter.drawRoundedRect(pill, unit * 8.0, unit * 8.0);
            }

            painter.setPen(selected && available ? colours.onPrimaryContainer
                                                 : colours.onSurface);
            painter.drawText(cell, Qt::AlignCenter, locale().standaloneMonthName(month));
            painter.restore();
        }
    } else {
        painter.setFont(fontFor(theme, theme.type().titleSmall));
        const int start = yearPageStart();

        for (int index = 0; index < kYearsPerPage; ++index) {
            const int year = start + index;
            if (year < 1)
                continue;

            const QRectF cell = yearRect(year);
            if (cell.isEmpty())
                continue;

            const bool available = isYearEnabled(year);
            const bool selected = year == month_.year();
            const QRectF pill = cell.adjusted(unit * kCellInsetDp, unit * kCellInsetDp,
                                              -unit * kCellInsetDp, -unit * kCellInsetDp);

            painter.save();
            painter.setOpacity(baseOpacity * (available ? 1.0 : 0.32));
            if (selected && available) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(colours.primaryContainer);
                painter.drawRoundedRect(pill, unit * 8.0, unit * 8.0);
            }

            painter.setPen(selected && available ? colours.onPrimaryContainer
                                                 : colours.onSurface);
            painter.drawText(cell, Qt::AlignCenter, QString::number(year));
            painter.restore();
        }
    }

    painter.restore();
}

void Calendar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        pressed_ = event->position();

    View::mousePressEvent(event);
}

void Calendar::mouseReleaseEvent(QMouseEvent* event)
{
    const QPointF press = pressed_;
    pressed_ = QPointF(-1.0, -1.0);
    View::mouseReleaseEvent(event);

    if (!isEnabled() || event->button() != Qt::LeftButton || press.x() < 0.0)
        return;

    const QPointF release = event->position();
    const auto inside = [&press, &release](const QRectF& rect) {
        return !rect.isEmpty() && rect.contains(press) && rect.contains(release);
    };

    if (inside(previousButtonRect())) {
        navigate(-1);
        return;
    }

    if (inside(nextButtonRect())) {
        navigate(1);
        return;
    }

    if (inside(monthButtonRect())) {
        setPage(page_ == Page::Days ? Page::Months : Page::Days);
        return;
    }

    if (inside(yearButtonRect())) {
        setPage(Page::Years);
        return;
    }

    switch (page_) {
    case Page::Days:
        for (int index = 0; index < kDayCells; ++index) {
            const QDate date = gridStart().addDays(index);
            if (inside(cellRect(date))) {
                activateDate(date);
                return;
            }
        }
        break;
    case Page::Months:
        for (int month = 1; month <= kMonthsPerPage; ++month) {
            if (inside(monthRect(month)) && isMonthEnabled(month)) {
                setDisplayedMonth(QDate(month_.year(), month, 1));
                setPage(Page::Days);
                return;
            }
        }
        break;
    case Page::Years: {
        const int start = yearPageStart();
        for (int index = 0; index < kYearsPerPage; ++index) {
            const int year = start + index;
            if (year >= 1 && inside(yearRect(year)) && isYearEnabled(year)) {
                setDisplayedMonth(QDate(year, month_.month(), 1));
                setPage(Page::Months);
                return;
            }
        }
        break;
    }
    }
}

void Calendar::keyPressEvent(QKeyEvent* event)
{
    if (!isEnabled()) {
        View::keyPressEvent(event);
        return;
    }

    if (page_ == Page::Months || page_ == Page::Years) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
            || event->key() == Qt::Key_Escape) {
            setPage(page_ == Page::Years ? Page::Months : Page::Days);
            event->accept();
            return;
        }
    }

    QDate date = selected_;
    switch (event->key()) {
    case Qt::Key_Left:
        date = page_ == Page::Days ? date.addDays(-1) : date.addMonths(-1);
        break;
    case Qt::Key_Right:
        date = page_ == Page::Days ? date.addDays(1) : date.addMonths(1);
        break;
    case Qt::Key_Up:
        date = page_ == Page::Days ? date.addDays(-kWeekColumns) : date.addMonths(-3);
        break;
    case Qt::Key_Down:
        date = page_ == Page::Days ? date.addDays(kWeekColumns) : date.addMonths(3);
        break;
    case Qt::Key_PageUp:
        date = event->modifiers().testFlag(Qt::ControlModifier) ? date.addYears(-1)
                                                                : date.addMonths(-1);
        break;
    case Qt::Key_PageDown:
        date = event->modifiers().testFlag(Qt::ControlModifier) ? date.addYears(1)
                                                                : date.addMonths(1);
        break;
    case Qt::Key_Home:
        date = QDate(date.year(), date.month(), 1);
        break;
    case Qt::Key_End:
        date = QDate(date.year(), date.month(), date.daysInMonth());
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        activateDate(selected_);
        event->accept();
        return;
    default:
        View::keyPressEvent(event);
        return;
    }

    if (!date.isValid()) {
        event->accept();
        return;
    }

    const QDate target = clampDate(date);
    if (page_ == Page::Days) {
        setSelectedDate(target);
    } else {
        setDisplayedMonth(target);
    }

    event->accept();
}

} // namespace md
