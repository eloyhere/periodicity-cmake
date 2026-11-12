// Periodicity — the TimePicker component (framework.md §5.9).
//
// A three-column time wheel. Hour, minute and second are separate cyclic
// columns; in 12-hour mode a fourth column carries AM/PM between the hour and
// the minute. Every column scrolls one row per wheel notch, follows a drag and
// snaps to the nearest row on release, responds to a click on a visible
// neighbouring row, and steps with Up/Down, PageUp/PageDown, Home and End.
// Left/Right (or a click on another column) moves the active column.
//
// The value is a QTime: the committed, snapped time. During a drag the wheel
// moves continuously but timeChanged() is emitted only when the wheel settles,
// which is what a caller binding the value to a label expects. Hour format is
// a display concern only — the QTime is always 0..23 internally — so switching
// 12/24 preserves the instant.
//
// The component paints itself and has no child widgets: wheelRect(),
// columnRect(), rowRect() and selectionBandRect() are the geometry both the
// painter and the tests use.

#pragma once

#include <md/view/view.h>

#include <QRectF>
#include <QString>
#include <QTime>

#include <array>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QTimer;
class QWheelEvent;

namespace md {

class TimePicker : public View
{
    Q_OBJECT
    Q_PROPERTY(QTime time READ time WRITE setTime NOTIFY timeChanged)

public:
    // The hour column's range. The enumerator values are the clock sizes so
    // that setHourFormat(12) and setHourFormat(24) read naturally.
    enum class HourFormat {
        Hour12 = 12,
        Hour24 = 24,
    };
    Q_ENUM(HourFormat)

    // A semantic column. Meridian only exists in 12-hour mode.
    enum class Field {
        Hour,
        Meridian,
        Minute,
        Second,
    };
    Q_ENUM(Field)

    explicit TimePicker(QWidget* parent = nullptr);

    [[nodiscard]] QTime time() const { return time_; }

    // An invalid time is ignored. The value is committed as given (the
    // millisecond part is preserved and is not editable).
    void setTime(const QTime& time);

    [[nodiscard]] HourFormat hourFormat() const noexcept { return format_; }
    void setHourFormat(HourFormat format);

    // The 12/24 spelling of the same setting. Any other value is ignored.
    void setHourFormat(int format);

    [[nodiscard]] bool secondsVisible() const noexcept { return secondsVisible_; }
    void setSecondVisible(bool visible);

    [[nodiscard]] bool meridianVisible() const noexcept;

    // The visible columns, in painting order.
    [[nodiscard]] int columnCount() const noexcept;
    [[nodiscard]] int currentColumn() const noexcept { return currentColumn_; }
    void setCurrentColumn(int column);

    // The raw column index of a semantic field, or -1 when the field is not
    // shown (Meridian in 24-hour mode, Second when hidden).
    [[nodiscard]] int fieldColumn(Field field) const;

    // Geometry, in widget coordinates. A row offset is -1 for the row above the
    // selection band, 0 for the selected row and +1 for the row below it.
    [[nodiscard]] QRectF wheelRect() const;
    [[nodiscard]] QRectF columnRect(int column) const;
    [[nodiscard]] QRectF rowRect(int column, int rowOffset) const;
    [[nodiscard]] QRectF selectionBandRect() const;

    // The text the column shows in the selection band ("09", "AM", "30").
    [[nodiscard]] QString columnText(int column) const;

    // The value the column is currently centred on (0..23, 0..59, 0/1).
    [[nodiscard]] int columnValue(int column) const;

    // Steps one column by `delta` rows, wrapping cyclically, and commits the
    // value. This is the path the wheel, the keyboard and the tests share.
    void stepColumn(int column, int delta);
    void stepField(Field field, int delta);

    // Steps the current column.
    void step(int delta);

    // The continuous position of a column in index space; a whole number means
    // the column is settled on that row.
    [[nodiscard]] qreal columnOffset(int column) const;

    // The snap animation that follows a drag. Keyboard and wheel input settle
    // immediately; a caller (or a test) may force the animation to its end.
    [[nodiscard]] bool isAnimating() const;
    void finishAnimation();

signals:
    void timeChanged(QTime time);
    void hourFormatChanged(TimePicker::HourFormat format);
    void currentColumnChanged(int column);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void onHidden() override;
    void onThemeChanged() override;

private:
    [[nodiscard]] int valueCount(Field field) const;
    [[nodiscard]] int valueForField(Field field) const;
    [[nodiscard]] int indexOfValue(Field field, int value) const;
    [[nodiscard]] int valueOfIndex(Field field, int index) const;
    [[nodiscard]] QString textOfIndex(Field field, int index) const;
    [[nodiscard]] int columnAt(qreal x) const;
    [[nodiscard]] qreal columnWidth(int column) const;
    [[nodiscard]] qreal rowHeight() const;
    [[nodiscard]] qreal normalized(qreal offset, int count) const;

    // Writes the wheel's centred index into the QTime and emits when it changed.
    void commitColumn(int column, int index);
    void refreshColumns();
    void animateTo(int column, qreal target);
    void tickAnimation();

    struct ColumnState
    {
        qreal offset = 0.0;
        qreal target = 0.0;
    };

    QTime time_{0, 0};
    HourFormat format_ = HourFormat::Hour24;
    bool secondsVisible_ = true;
    int currentColumn_ = 0;

    std::array<Field, 4> columns_{Field::Hour, Field::Minute, Field::Second, Field::Second};
    int columnCount_ = 2;
    std::array<ColumnState, 4> columnStates_{};

    QTimer* animator_ = nullptr;

    bool dragging_ = false;
    int dragColumn_ = -1;
    QPointF dragStart_;
    qreal dragStartOffset_ = 0.0;
    QPointF pressed_{-1.0, -1.0};
};

} // namespace md
