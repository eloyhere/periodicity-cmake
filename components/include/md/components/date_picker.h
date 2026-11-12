// Periodicity — the DatePicker component (framework.md §5.9).
//
// An inline, Monday-first Gregorian calendar. The day view always lays out six
// weeks (42 cells) so the sheet does not jump between months, and the header
// navigates months with the arrows while a click on the title switches to the
// month grid and then to the year grid. Minimum and maximum dates are enforced
// both for the pointer and for the keyboard, and the selection, today and the
// disabled cells each have a distinct M3 treatment.
//
// The component paints itself: it has no child widgets, so every hit target is
// described by a public rectangle (dateRect, monthRect, yearRect, the header
// parts) which is also what the tests drive. All colours, type styles and
// metrics come from the resolved Theme; a change of theme simply repaints.

#pragma once

#include <md/view/view.h>

#include <QDate>
#include <QRectF>
#include <QString>

class QKeyEvent;
class QMouseEvent;
class QPainter;

namespace md {

class DatePicker : public View
{
    Q_OBJECT
    Q_PROPERTY(QDate selectedDate READ selectedDate WRITE setSelectedDate NOTIFY selectedDateChanged)

public:
    // The three pages of the sheet. Days is the calendar grid; Months shows the
    // twelve months of the displayed year; Years shows a page of twelve years.
    enum class CalendarView {
        Days,
        Months,
        Years,
    };
    Q_ENUM(CalendarView)

    explicit DatePicker(QWidget* parent = nullptr);

    [[nodiscard]] QDate selectedDate() const { return selected_; }
    [[nodiscard]] QDate displayedMonth() const { return month_; }
    [[nodiscard]] QDate minimumDate() const { return minimum_; }
    [[nodiscard]] QDate maximumDate() const { return maximum_; }

    // Selects a date, clamped to [minimumDate, maximumDate]. An invalid date is
    // ignored. The displayed month follows the selection.
    void setSelectedDate(QDate date);

    // Sets both bounds. An invalid range or minimum > maximum is ignored.
    void setDateRange(QDate minimum, QDate maximum);
    void setMinimumDate(QDate date);
    void setMaximumDate(QDate date);

    // Moves the displayed month, clamped to the month of the bounds.
    void setDisplayedMonth(QDate month);

    void previousMonth();
    void nextMonth();

    [[nodiscard]] CalendarView calendarView() const noexcept { return view_; }
    void setCalendarView(CalendarView view);

    // The text the header shows for the current page ("March 2026", "2026",
    // "2020 - 2031").
    [[nodiscard]] QString titleText() const;

    // Geometry, all in widget coordinates. The day grid is six rows of seven
    // cells starting at gridStart() (the Monday on or before the first of the
    // displayed month).
    [[nodiscard]] QRectF headerRect() const;
    [[nodiscard]] QRectF previousButtonRect() const;
    [[nodiscard]] QRectF nextButtonRect() const;
    [[nodiscard]] QRectF titleRect() const;
    [[nodiscard]] QRectF weekdayRect(int column) const;
    [[nodiscard]] QRectF dateRect(QDate date) const;
    [[nodiscard]] QRectF monthRect(int month) const;
    [[nodiscard]] QRectF yearRect(int year) const;

    [[nodiscard]] bool isDateEnabled(QDate date) const;
    [[nodiscard]] bool isMonthEnabled(int month) const;
    [[nodiscard]] bool isYearEnabled(int year) const;

signals:
    void selectedDateChanged(QDate date);
    void displayedMonthChanged(QDate month);
    void dateActivated(QDate date);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    [[nodiscard]] QDate gridStart() const;
    [[nodiscard]] qreal padding() const;
    [[nodiscard]] QRectF bodyRect() const;
    [[nodiscard]] int yearPageStart() const;
    [[nodiscard]] bool canNavigate(bool forward) const;
    [[nodiscard]] QDate clampDate(QDate date) const;

    // direction is +1 for the next page and -1 for the previous one.
    void navigate(int direction);
    void cycleView();
    void activateDate(QDate date);

    QDate selected_ = QDate::currentDate();
    QDate month_ = QDate(selected_.year(), selected_.month(), 1);
    QDate minimum_{1, 1, 1};
    QDate maximum_{9999, 12, 31};
    CalendarView view_ = CalendarView::Days;
    QPointF pressed_{-1.0, -1.0};
};

} // namespace md
