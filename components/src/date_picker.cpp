// Periodicity — the DatePicker component (framework.md §5.9).
//
// The sheet is painted in one pass from the theme tokens: the header (arrows
// and title), the weekday row and then either the six-week day grid or one of
// the two coarser pages. Pointer input is validated against the same rectangle
// helpers that paint the pages, so the hit targets and the pixels can never
// drift apart. Keyboard navigation moves the selection by day, week or month
// and clamps it to the date range; Enter activates the current selection.

#include <md/components/date_picker.h>

#include <md/theme/theme.h>

#include <QFont>
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
constexpr qreal kHeaderDp = 48.0;
constexpr qreal kArrowBoxDp = 48.0;
constexpr qreal kWeekdayDp = 28.0;
constexpr qreal kHeaderGapDp = 4.0;
constexpr qreal kBodyGapDp = 8.0;
constexpr qreal kCellInsetDp = 4.0;
constexpr qreal kMaxCircleDp = 40.0;

[[nodiscard]] QFont fontFor(const Theme& theme, const TextStyle& style)
{
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(QFont::Weight(style.weight));
    return font;
}

} // namespace

DatePicker::DatePicker(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    // The M3 date picker container is the extra-large shape (28 dp), not the
    // 16 dp large one: the sheet's outer corners are the most visible curve in
    // the component, and at 16 dp they read as a card rather than a picker.
    setShape(Shape::extraLarge());
    setAccessibleName(tr("Choose date"));
}

bool DatePicker::isDateEnabled(QDate date) const
{
    return date.isValid() && date >= minimum_ && date <= maximum_;
}

bool DatePicker::isMonthEnabled(int month) const
{
    if (month < 1 || month > kMonthsPerPage)
        return false;

    const QDate first(month_.year(), month, 1);
    const QDate last(first.year(), first.month(), first.daysInMonth());
    return first <= maximum_ && last >= minimum_;
}

bool DatePicker::isYearEnabled(int year) const
{
    if (year < 1 || year > 9999)
        return false;

    return QDate(year, 1, 1) <= maximum_ && QDate(year, 12, 31) >= minimum_;
}

QDate DatePicker::clampDate(QDate date) const
{
    if (!date.isValid())
        return selected_;
    if (date < minimum_)
        return minimum_;
    if (date > maximum_)
        return maximum_;
    return date;
}

void DatePicker::setSelectedDate(QDate date)
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

void DatePicker::setDateRange(QDate minimum, QDate maximum)
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

void DatePicker::setMinimumDate(QDate date)
{
    if (!date.isValid() || date > maximum_)
        return;
    setDateRange(date, maximum_);
}

void DatePicker::setMaximumDate(QDate date)
{
    if (!date.isValid() || date < minimum_)
        return;
    setDateRange(minimum_, date);
}

void DatePicker::setDisplayedMonth(QDate date)
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
    emit displayedMonthChanged(month_);
}

void DatePicker::previousMonth()
{
    setDisplayedMonth(month_.addMonths(-1));
}

void DatePicker::nextMonth()
{
    setDisplayedMonth(month_.addMonths(1));
}

void DatePicker::setCalendarView(CalendarView view)
{
    if (view_ == view)
        return;

    view_ = view;
    requestUpdate();
}

QString DatePicker::titleText() const
{
    switch (view_) {
    case CalendarView::Days:
        return locale().toString(month_, QStringLiteral("MMMM yyyy"));
    case CalendarView::Months:
        return QString::number(month_.year());
    case CalendarView::Years: {
        const int start = yearPageStart();
        return QStringLiteral("%1 \u2013 %2").arg(start).arg(start + kYearsPerPage - 1);
    }
    }

    return QString();
}

qreal DatePicker::padding() const
{
    return Theme::forWidget(this).density().dp(kPaddingDp);
}

QRectF DatePicker::headerRect() const
{
    const qreal pad = padding();
    return QRectF(pad, pad, std::max<qreal>(0.0, qreal(width()) - 2.0 * pad),
                  Theme::forWidget(this).density().dp(kHeaderDp));
}

QRectF DatePicker::previousButtonRect() const
{
    const QRectF header = headerRect();
    const qreal side = std::min(Theme::forWidget(this).density().dp(kArrowBoxDp),
                                header.width() / 3.0);
    return QRectF(header.left(), header.top(), std::max<qreal>(0.0, side), header.height());
}

QRectF DatePicker::nextButtonRect() const
{
    const QRectF header = headerRect();
    const qreal side = std::min(Theme::forWidget(this).density().dp(kArrowBoxDp),
                                header.width() / 3.0);
    return QRectF(header.right() - std::max<qreal>(0.0, side), header.top(),
                  std::max<qreal>(0.0, side), header.height());
}

QRectF DatePicker::titleRect() const
{
    const QRectF header = headerRect();
    const qreal gap = Theme::forWidget(this).density().dp(kHeaderGapDp);
    return header.adjusted(previousButtonRect().width() + gap, 0.0,
                           -(nextButtonRect().width() + gap), 0.0);
}

QRectF DatePicker::weekdayRect(int column) const
{
    const QRectF header = headerRect();
    const Density& density = Theme::forWidget(this).density();
    const qreal cellWidth = header.width() / kWeekColumns;
    const qreal top = header.bottom() + density.dp(kHeaderGapDp);

    return QRectF(header.left() + column * cellWidth, top,
                  std::max<qreal>(0.0, cellWidth), density.dp(kWeekdayDp));
}

QDate DatePicker::gridStart() const
{
    // Monday-first: dayOfWeek() is 1 for Monday, so the grid starts on the
    // Monday on or before the first of the displayed month.
    return month_.addDays(1 - month_.dayOfWeek());
}

QRectF DatePicker::dateRect(QDate date) const
{
    if (!date.isValid())
        return QRectF();

    const qint64 offset = gridStart().daysTo(date);
    if (offset < 0 || offset >= kDayCells)
        return QRectF();

    const QRectF header = headerRect();
    const Density& density = Theme::forWidget(this).density();
    const qreal unit = density.dp(1.0);
    const qreal cellWidth = header.width() / kWeekColumns;
    const qreal gridTop = weekdayRect(0).bottom() + unit * 4.0;
    const qreal gridHeight = std::max<qreal>(0.0, qreal(height()) - gridTop - padding());
    const qreal cellHeight = gridHeight / kWeekRows;

    const int row = static_cast<int>(offset / kWeekColumns);
    const int column = static_cast<int>(offset % kWeekColumns);

    return QRectF(header.left() + column * cellWidth, gridTop + row * cellHeight,
                  std::max<qreal>(0.0, cellWidth), std::max<qreal>(0.0, cellHeight));
}

QRectF DatePicker::bodyRect() const
{
    const QRectF header = headerRect();
    const qreal top = header.bottom() + Theme::forWidget(this).density().dp(kBodyGapDp);
    return QRectF(header.left(), top, header.width(),
                  std::max<qreal>(0.0, qreal(height()) - top - padding()));
}

QRectF DatePicker::monthRect(int month) const
{
    if (month < 1 || month > kMonthsPerPage)
        return QRectF();

    const QRectF area = bodyRect();
    const int index = month - 1;
    const qreal cellWidth = area.width() / 3.0;
    const qreal cellHeight = area.height() / 4.0;

    return QRectF(area.left() + (index % 3) * cellWidth,
                  area.top() + (index / 3) * cellHeight,
                  std::max<qreal>(0.0, cellWidth), std::max<qreal>(0.0, cellHeight));
}

int DatePicker::yearPageStart() const
{
    return std::max(1, (month_.year() / kYearsPerPage) * kYearsPerPage);
}

QRectF DatePicker::yearRect(int year) const
{
    const int start = yearPageStart();
    const int index = year - start;
    if (index < 0 || index >= kYearsPerPage)
        return QRectF();

    const QRectF area = bodyRect();
    const qreal cellWidth = area.width() / 3.0;
    const qreal cellHeight = area.height() / 4.0;

    return QRectF(area.left() + (index % 3) * cellWidth,
                  area.top() + (index / 3) * cellHeight,
                  std::max<qreal>(0.0, cellWidth), std::max<qreal>(0.0, cellHeight));
}

bool DatePicker::canNavigate(bool forward) const
{
    const QDate firstMonth(minimum_.year(), minimum_.month(), 1);
    const QDate lastMonth(maximum_.year(), maximum_.month(), 1);

    switch (view_) {
    case CalendarView::Days:
        return forward ? month_ < lastMonth : month_ > firstMonth;
    case CalendarView::Months:
        return forward ? month_.year() < maximum_.year() : month_.year() > minimum_.year();
    case CalendarView::Years: {
        const int start = yearPageStart();
        return forward ? start + kYearsPerPage <= maximum_.year() : start > minimum_.year();
    }
    }

    return false;
}

void DatePicker::navigate(int direction)
{
    if (direction == 0 || !canNavigate(direction > 0))
        return;

    switch (view_) {
    case CalendarView::Days:
        setDisplayedMonth(month_.addMonths(direction));
        break;
    case CalendarView::Months:
        setDisplayedMonth(month_.addYears(direction));
        break;
    case CalendarView::Years: {
        const int start = std::clamp(yearPageStart() + kYearsPerPage * direction,
                                     minimum_.year(), maximum_.year());
        setDisplayedMonth(QDate(start, month_.month(), 1));
        break;
    }
    }
}

void DatePicker::cycleView()
{
    switch (view_) {
    case CalendarView::Days:
        view_ = CalendarView::Months;
        break;
    case CalendarView::Months:
        view_ = CalendarView::Years;
        break;
    case CalendarView::Years:
        view_ = CalendarView::Days;
        break;
    }

    requestUpdate();
}

void DatePicker::activateDate(QDate date)
{
    if (!isEnabled() || !isDateEnabled(date))
        return;

    setSelectedDate(date);
    emit dateActivated(date);
}

void DatePicker::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    setMeasuredSize(QSizeF(width.resolve(density.dp(360.0)),
                           height.resolve(density.dp(396.0))));
}

void DatePicker::paintContent(QPainter& painter, const QRectF& content)
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

    // Header navigation arrows.
    for (int index = 0; index < 2; ++index) {
        const bool forward = index == 1;
        const QRectF box = forward ? nextButtonRect() : previousButtonRect();
        const bool available = canNavigate(forward);

        painter.save();
        painter.setOpacity(baseOpacity * (available ? 1.0 : 0.38));
        painter.setPen(QPen(colours.onSurfaceVariant, unit * 2.0, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin));
        const QPointF centre = box.center();
        const qreal sign = forward ? 1.0 : -1.0;
        QPolygonF chevron;
        chevron << QPointF(centre.x() - sign * unit * 3.0, centre.y() - unit * 6.0)
                << QPointF(centre.x() + sign * unit * 3.0, centre.y())
                << QPointF(centre.x() - sign * unit * 3.0, centre.y() + unit * 6.0);
        painter.drawPolyline(chevron);
        painter.restore();
    }

    painter.setFont(fontFor(theme, theme.type().titleMedium));
    painter.setPen(colours.onSurface);
    painter.drawText(titleRect(), Qt::AlignCenter, titleText());

    if (view_ == CalendarView::Days) {
        painter.setFont(fontFor(theme, theme.type().labelSmall));
        painter.setPen(colours.onSurfaceVariant);
        for (int column = 0; column < kWeekColumns; ++column) {
            painter.drawText(weekdayRect(column), Qt::AlignCenter,
                             locale().dayName(column + 1, QLocale::ShortFormat));
        }

        painter.setFont(fontFor(theme, theme.type().bodyMedium));
        const QDate today = QDate::currentDate();

        for (int index = 0; index < kDayCells; ++index) {
            const QDate date = gridStart().addDays(index);
            const QRectF cell = dateRect(date);
            if (cell.isEmpty())
                continue;

            const bool inMonth = date.year() == month_.year() && date.month() == month_.month();
            const bool available = isDateEnabled(date);
            const bool selected = date == selected_;
            const bool isToday = date == today;
            const qreal diameter = std::min({cell.width() - unit * kCellInsetDp,
                                             cell.height() - unit * kCellInsetDp,
                                             unit * kMaxCircleDp});
            if (diameter <= 0.0)
                continue;

            const QRectF circle(cell.center().x() - diameter / 2.0,
                                cell.center().y() - diameter / 2.0, diameter, diameter);

            painter.save();
            painter.setOpacity(baseOpacity
                               * (available ? (inMonth ? 1.0 : 0.55) : 0.32));

            if (selected) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(colours.primary);
                painter.drawEllipse(circle);
            } else if (isToday) {
                // M3 marks today with a 1 dp primary outline around the same
                // 40 dp circle the selection would fill.
                painter.setPen(QPen(colours.primary, unit * 1.0));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(circle);
            }

            // The focus indicator is a separate, offset ring. It used to be
            // primary on a primary-filled circle — invisible — so it is drawn
            // in on-surface, which reads against both the fill and the sheet.
            if (selected && hasFocus()) {
                painter.setPen(QPen(colours.onSurface, unit * 1.5));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(circle.adjusted(-unit * 2.0, -unit * 2.0,
                                                    unit * 2.0, unit * 2.0));
            }

            painter.setPen(selected ? colours.onPrimary
                                    : (available ? colours.onSurface
                                                 : colours.onSurfaceVariant));
            painter.drawText(cell, Qt::AlignCenter, QString::number(date.day()));
            painter.restore();
        }
    } else if (view_ == CalendarView::Months) {
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
                // The M3 year (and month) selection is a tonal primary-container
                // pill with on-primary-container content; the primary/on-primary
                // pair belongs to the day circle, which is a different emphasis.
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

void DatePicker::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        pressed_ = event->position();

    View::mousePressEvent(event);
}

void DatePicker::mouseReleaseEvent(QMouseEvent* event)
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
        if (canNavigate(false))
            navigate(-1);
        return;
    }

    if (inside(nextButtonRect())) {
        if (canNavigate(true))
            navigate(1);
        return;
    }

    if (inside(titleRect())) {
        cycleView();
        return;
    }

    switch (view_) {
    case CalendarView::Days:
        for (int index = 0; index < kDayCells; ++index) {
            const QDate date = gridStart().addDays(index);
            if (inside(dateRect(date))) {
                activateDate(date);
                return;
            }
        }
        break;
    case CalendarView::Months:
        for (int month = 1; month <= kMonthsPerPage; ++month) {
            if (inside(monthRect(month)) && isMonthEnabled(month)) {
                setDisplayedMonth(QDate(month_.year(), month, 1));
                setCalendarView(CalendarView::Days);
                return;
            }
        }
        break;
    case CalendarView::Years: {
        const int start = yearPageStart();
        for (int index = 0; index < kYearsPerPage; ++index) {
            const int year = start + index;
            if (year >= 1 && inside(yearRect(year)) && isYearEnabled(year)) {
                setDisplayedMonth(QDate(year, month_.month(), 1));
                setCalendarView(CalendarView::Months);
                return;
            }
        }
        break;
    }
    }
}

void DatePicker::keyPressEvent(QKeyEvent* event)
{
    if (!isEnabled()) {
        View::keyPressEvent(event);
        return;
    }

    if (view_ != CalendarView::Days) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
            || event->key() == Qt::Key_Space) {
            emit dateActivated(selected_);
            event->accept();
            return;
        }

        View::keyPressEvent(event);
        return;
    }

    QDate date = selected_;
    switch (event->key()) {
    case Qt::Key_Left:
        date = date.addDays(-1);
        break;
    case Qt::Key_Right:
        date = date.addDays(1);
        break;
    case Qt::Key_Up:
        date = date.addDays(-kWeekColumns);
        break;
    case Qt::Key_Down:
        date = date.addDays(kWeekColumns);
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
        emit dateActivated(selected_);
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

    setSelectedDate(date);
    event->accept();
}

} // namespace md
