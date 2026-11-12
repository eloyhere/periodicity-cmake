// Periodicity — the Calendar component (framework.md §5.9).
//
// Calendar is the data-driven member of the date family. DatePicker owns a
// QDate and answers only for itself; Calendar additionally accepts a list of
// CalendarCell values so a caller can decorate individual days with a short
// label ("角标"), a coloured marker dot, or an explicit disabled flag — which is
// what a schedule, a booking sheet or a habit tracker needs.
//
// The grid contract is the same as DatePicker's: Monday is the first column and
// the day page always lays out six weeks (42 cells), so switching months never
// changes the sheet's height. The two pages above it — Months and Years — are
// reached from the two header buttons, not by cycling through the title, which
// keeps "jump to a year" and "jump to a month" as separate, discoverable
// actions.
//
// The component paints itself: it has no child widgets, so every hit target is
// described by a public rectangle (cellRect, monthButtonRect, yearRect, ...)
// and that rectangle is what the tests drive. All colours, type styles and
// metrics come from the resolved Theme; a theme change simply repaints.

#pragma once

#include <md/view/view.h>

#include <QColor>
#include <QDate>
#include <QList>
#include <QRectF>
#include <QString>

class QKeyEvent;
class QMouseEvent;
class QPainter;

namespace md {

// One day of the grid.
//
// `label`, `marker` and `enabled` are the caller's data; `today`, `selected`
// and `currentMonth` are recomputed by Calendar from its own state, so the
// values a caller stores in those three fields are OR-ed with the computed ones
// rather than replacing them. A cell value is a plain value type: it is copied
// into the signal, compared by the tests and never owned by the widget.
struct CalendarCell
{
    QDate date;
    bool enabled = true;
    bool selected = false;
    bool today = false;
    QString label;
    QColor marker;
    bool currentMonth = true;

    [[nodiscard]] friend bool operator==(const CalendarCell& lhs,
                                         const CalendarCell& rhs) = default;
};

class Calendar : public View
{
    Q_OBJECT
    Q_PROPERTY(QDate selectedDate READ selectedDate WRITE setSelectedDate NOTIFY selectedDateChanged)

public:
    // The three pages of the sheet. Days is the six-week grid; Months shows the
    // twelve months of the displayed year; Years shows a page of twelve years.
    enum class Page {
        Days,
        Months,
        Years,
    };
    Q_ENUM(Page)

    explicit Calendar(QWidget* parent = nullptr);

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

    [[nodiscard]] Page page() const noexcept { return page_; }
    void setPage(Page page);

    // The header labels: the month button shows the month name on the day page
    // and the year on the month page; the year button shows the year on the day
    // page and the year range on the year page.
    [[nodiscard]] QString monthButtonText() const;
    [[nodiscard]] QString yearButtonText() const;

    // The data overlay. setCells() replaces the whole list; setCell() inserts or
    // replaces one date. A cell whose date falls outside the current grid is
    // simply not painted until it scrolls back into view.
    void setCells(const QList<CalendarCell>& cells);
    void setCell(const CalendarCell& cell);
    void clearCells();
    [[nodiscard]] QList<CalendarCell> cells() const { return cells_; }

    // The convenience path for markers: every date in `dates` gets `marker`
    // (default: the theme's tertiary) and an empty label.
    void setMarkedDates(const QList<QDate>& dates, const QColor& marker = QColor());

    // Removes every marker, including one a caller set through setCell(); the
    // label and the enabled flag of each cell are kept.
    void clearMarkedDates();

    // The merged value painting and hit-testing use: the stored label, marker
    // and enabled flag combined with the range, the selection and today.
    [[nodiscard]] CalendarCell cellForDate(QDate date) const;

    // Geometry, all in widget coordinates. The day grid is six rows of seven
    // cells starting at gridStart() (the Monday on or before the first of the
    // displayed month).
    [[nodiscard]] QDate gridStart() const;
    [[nodiscard]] QRectF headerRect() const;
    [[nodiscard]] QRectF previousButtonRect() const;
    [[nodiscard]] QRectF nextButtonRect() const;
    [[nodiscard]] QRectF monthButtonRect() const;
    [[nodiscard]] QRectF yearButtonRect() const;
    [[nodiscard]] QRectF weekdayRect(int column) const;
    [[nodiscard]] QRectF cellRect(QDate date) const;
    [[nodiscard]] QRectF monthRect(int month) const;
    [[nodiscard]] QRectF yearRect(int year) const;

    [[nodiscard]] bool isDateEnabled(QDate date) const;
    [[nodiscard]] bool isMonthEnabled(int month) const;
    [[nodiscard]] bool isYearEnabled(int year) const;
    [[nodiscard]] int yearPageStart() const;

signals:
    void cellClicked(CalendarCell cell);
    void dateSelected(QDate date);
    void monthChanged(QDate month);
    void selectedDateChanged(QDate date);
    void pageChanged(Calendar::Page page);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    [[nodiscard]] qreal padding() const;
    [[nodiscard]] QRectF bodyRect() const;
    [[nodiscard]] int indexOfCell(QDate date) const;
    [[nodiscard]] CalendarCell storedCell(QDate date) const;
    [[nodiscard]] QDate clampDate(QDate date) const;
    [[nodiscard]] bool canNavigate(bool forward) const;
    void navigate(int direction);
    void activateDate(QDate date);

    QDate selected_ = QDate::currentDate();
    QDate month_ = QDate(selected_.year(), selected_.month(), 1);
    QDate minimum_{1, 1, 1};
    QDate maximum_{9999, 12, 31};
    Page page_ = Page::Days;
    QList<CalendarCell> cells_;
    QPointF pressed_{-1.0, -1.0};
};

} // namespace md

// The cell crosses a signal boundary, so QSignalSpy and a queued connection
// both need a metatype for it.
Q_DECLARE_METATYPE(md::CalendarCell)
