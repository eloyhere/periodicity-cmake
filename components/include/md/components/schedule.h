// Periodicity — the Schedule component (framework.md §5.9).
//
// Schedule is a day/week agenda painted by one widget: a 56 dp time gutter, one
// (Day) or seven (Week) day columns, a weekday header and 48 dp per hour. Events
// are rounded blocks tinted by their accent colour; overlapping events are laid
// out in lanes so two blocks never sit on top of each other.
//
// The calendar surface scrolls inside the widget. There is no Scrollbar child:
// wheelEvent() moves a private offset, every public rectangle is reported in
// viewport coordinates (already scrolled), and a slim themed indicator on the
// trailing edge shows where the viewport is. maximumScrollOffset() is derived
// from the day range and the viewport, so the offset can never leave the range.
//
// Interaction is deliberately split into two paths that produce the same
// signal:
//
//   * pointer — a click selects and activates; with editable() set, a vertical
//     drag moves the event in 15-minute snaps and emits eventMoved();
//   * keyboard (WCAG 2.5.7) — the selected event moves with Alt+Left/Right
//     (15 minutes) and Alt+Up/Down (one day), Enter activates it.
//
// Schedule is a controlled component: a move emits the new start and leaves
// events() untouched. The host applies the change (typically by calling
// setEvents()) and can reject or adjust it; the widget never mutates the
// caller's model behind its back.
//
// All geometry, colours and type styles come from the resolved Theme, through
// density().dp()/sp(); no colour is hard-coded. In a right-to-left layout the
// gutter moves to the right edge and the day order mirrors, so the first column
// is always the reading-order start.

#pragma once

#include <md/view/view.h>

#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QMetaType>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTime>
#include <QVector>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QResizeEvent;
class QWheelEvent;

namespace md {

// One entry of the agenda. `accent` tints the block and its left bar; an empty
// colour falls back to the theme's primary. The value type is copied into the
// widget, so a caller may keep mutating the original afterwards.
struct ScheduleEvent
{
    QString id;
    QString title;
    QDateTime start;
    QDateTime end;
    QColor accent;

    [[nodiscard]] friend bool operator==(const ScheduleEvent& lhs,
                                         const ScheduleEvent& rhs) = default;
};

class Schedule : public View
{
    Q_OBJECT

public:
    // Day shows the column of date(); Week shows date() and the six days after
    // it. date() is therefore always the first visible day, not merely a date
    // inside the visible week.
    enum class Mode {
        Day,
        Week,
    };
    Q_ENUM(Mode)

    explicit Schedule(QWidget* parent = nullptr);

    void setMode(Mode mode);
    [[nodiscard]] Mode mode() const noexcept { return mode_; }

    // Selects the first visible day. An invalid date is ignored. In Week mode
    // the week is date() .. date().addDays(6), so the caller decides which day
    // opens the week.
    void setDate(const QDate& date);
    [[nodiscard]] QDate date() const { return date_; }

    // Replaces the model. Events whose id no longer exists can no longer stay
    // selected, so a removed selection is cleared.
    void setEvents(const QVector<ScheduleEvent>& events);
    [[nodiscard]] const QVector<ScheduleEvent>& events() const { return events_; }

    // The visible time window, in minutes of the day. start must be valid; an
    // end of 00:00 means the end of the day (24:00), which is how a QTime can
    // express the default 00:00–24:00 range. A range whose end is not after its
    // start is ignored.
    void setDayRange(QTime start, QTime end);

    void setNowLineVisible(bool visible);
    [[nodiscard]] bool isNowLineVisible() const noexcept { return nowLineVisible_; }

    void setEditable(bool editable);
    [[nodiscard]] bool isEditable() const noexcept { return editable_; }

    // Selects one event by id. An empty id clears the selection; an id that is
    // not in events() is ignored.
    void setSelectedEvent(const QString& id);
    [[nodiscard]] QString selectedEvent() const { return selectedId_; }

    // Geometry, all in widget coordinates. The header sits above the body; the
    // gutter is the time strip on the leading edge (the trailing edge in RTL).
    // Day columns are the body cells below the header. Rectangles are already
    // scrolled and clipped horizontally to their column, so they are the same
    // rectangles painting and hit-testing read.
    [[nodiscard]] int columnCount() const noexcept;
    [[nodiscard]] QDate columnDate(int column) const;
    [[nodiscard]] QRectF headerRect() const;
    [[nodiscard]] QRectF bodyRect() const;
    [[nodiscard]] QRectF gutterRect() const;
    [[nodiscard]] QRectF gridRect() const;
    [[nodiscard]] QRectF weekdayHeaderRect(int column) const;
    [[nodiscard]] QRectF dayColumnRect(int column) const;
    [[nodiscard]] QRectF hourLabelRect(int hour) const;

    // The block of one event, or an empty rectangle when the event is not
    // visible. An event is visible when its start date is one of the visible
    // columns and its interval intersects [range start, range end]; an event
    // that spans midnight is clipped to the end of the range (the block is not
    // continued into the next column — see the .cpp for the simplification).
    [[nodiscard]] QRectF eventRect(int index) const;
    [[nodiscard]] QRectF eventRect(const QString& id) const;

    // The topmost event under `point`, or -1. Because overlapping events are
    // painted index order, the last matching index wins.
    [[nodiscard]] int eventAt(const QPointF& point) const;

    // Lane geometry of an event: the zero-based lane within its overlap cluster
    // and the number of lanes that cluster uses. Both return -1/0 when the event
    // is not visible.
    [[nodiscard]] int laneFor(int index) const;
    [[nodiscard]] int laneCount(int index) const;

    // The vertical scroll offset in pixels, clamped to [0, maximumScrollOffset].
    [[nodiscard]] qreal scrollOffset() const noexcept { return scrollOffset_; }
    void setScrollOffset(qreal offset);
    [[nodiscard]] qreal maximumScrollOffset() const;
    [[nodiscard]] qreal contentHeight() const;

signals:
    void eventActivated(const QString& id);
    void eventMoved(const QString& id, const QDateTime& newStart);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    // One visible event resolved into its column and clipped minute interval.
    // laneCount is the size of the overlap cluster, not of the whole column, so
    // an isolated event keeps the full column width.
    struct PlacedEvent
    {
        int index = -1;
        int column = -1;
        int lane = 0;
        int laneCount = 1;
        int startMinutes = 0;
        int endMinutes = 0;
    };

    // Lays out every visible event. The result is rebuilt on demand: the model
    // is small, and a cache would need invalidation on six inputs (events, mode,
    // date, range, size and layout direction) for no measurable win.
    [[nodiscard]] QVector<PlacedEvent> layoutEvents() const;
    [[nodiscard]] QRectF rectFor(const PlacedEvent& placed) const;
    [[nodiscard]] int indexOfEvent(const QString& id) const;
    [[nodiscard]] int columnForDate(const QDate& date) const;

    [[nodiscard]] bool isRightToLeft() const noexcept;
    [[nodiscard]] qreal headerHeight() const;
    [[nodiscard]] qreal gutterWidth() const;
    [[nodiscard]] qreal hourHeight() const;
    [[nodiscard]] qreal eventRadius() const;

    [[nodiscard]] int rangeStartMinutes() const noexcept;
    [[nodiscard]] int rangeEndMinutes() const noexcept;
    [[nodiscard]] qreal yForMinutes(int minutes) const;
    [[nodiscard]] int snappedDeltaMinutes(qreal deltaPixels) const;

    void clampScroll();
    void updateAccessibleDescription();

    QVector<ScheduleEvent> events_;
    QDate date_ = QDate::currentDate();
    Mode mode_ = Mode::Week;
    QTime rangeStart_ = QTime(0, 0);
    int rangeEndMinutes_ = 24 * 60;
    bool nowLineVisible_ = true;
    bool editable_ = false;
    QString selectedId_;
    qreal scrollOffset_ = 0.0;

    // Pointer drag state. pressedEvent_ is the index under the press, and the
    // snapped delta is what the preview and the release signal use.
    QPointF pressedPosition_{-1.0, -1.0};
    int pressedEvent_ = -1;
    int dragDeltaMinutes_ = 0;
    bool dragging_ = false;
};

} // namespace md

// A ScheduleEvent may cross a queued signal or a QVariant; registering it keeps
// the value usable outside the GUI thread.
Q_DECLARE_METATYPE(md::ScheduleEvent)
