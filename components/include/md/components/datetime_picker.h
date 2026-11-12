// Periodicity — the DateTimePicker component (framework.md §5.9).
//
// A date sheet with a 24-hour time row underneath. The calendar is the shared
// DatePicker child so date navigation, range validation and painting are the
// same code path as the standalone calendar; this component owns the value (a
// QDateTime), clamps it to [minimumDateTime, maximumDateTime] and paints the
// time row itself.
//
// The time row is divided into hour / minute / second segments. A segment is
// selected by clicking it or by Left/Right (and Tab/Shift+Tab); Up/Down or +/-
// steps the selected segment, and digits type a value directly (two digits, or
// one digit when a second digit could not form a valid value, commit the entry
// and move to the next segment). The whole composite is still a single
// md::View: paintEvent stays final and the time row is painted from
// paintContent(); the calendar is a child widget arranged by arrangeOverride().

#pragma once

#include <md/components/date_picker.h>

#include <QDateTime>
#include <QString>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QResizeEvent;

namespace md {

class DateTimePicker : public View
{
    Q_OBJECT
    Q_PROPERTY(QDateTime dateTime READ dateTime WRITE setDateTime NOTIFY dateTimeChanged)

public:
    explicit DateTimePicker(QWidget* parent = nullptr);

    [[nodiscard]] QDateTime dateTime() const { return value_; }
    [[nodiscard]] QTime time() const { return value_.time(); }
    [[nodiscard]] DatePicker* datePicker() const { return calendar_; }

    // Invalid values are ignored; a valid value is clamped to the range.
    void setDateTime(const QDateTime& value);
    void setTime(QTime time);

    // An invalid range or minimum > maximum is ignored.
    void setDateTimeRange(const QDateTime& minimum, const QDateTime& maximum);
    [[nodiscard]] QDateTime minimumDateTime() const { return minimum_; }
    [[nodiscard]] QDateTime maximumDateTime() const { return maximum_; }

    [[nodiscard]] bool secondsVisible() const noexcept { return secondsVisible_; }
    void setSecondsVisible(bool visible);

    // The segment that receives stepping and digit entry: 0 = hour,
    // 1 = minute, 2 = second.
    [[nodiscard]] int currentField() const noexcept { return field_; }
    void setCurrentField(int field);

    // Geometry of the time row and of one segment, in widget coordinates.
    [[nodiscard]] QRectF timeRect() const;
    [[nodiscard]] QRectF fieldRect(int field) const;

signals:
    void dateTimeChanged(QDateTime value);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeOverride(const QRectF& content) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;

private:
    [[nodiscard]] int fieldCount() const;
    [[nodiscard]] int fieldMaximum(int field) const;
    [[nodiscard]] int fieldValue(int field) const;
    void setFieldValue(int field, int value);
    void commitDigits();
    void typeDigit(int digit);
    void step(int amount);
    void moveField(int delta);
    void applyValue(const QDateTime& candidate);
    void syncCalendar();
    [[nodiscard]] QDateTime clampDateTime(const QDateTime& value) const;

    DatePicker* calendar_ = nullptr;
    QDateTime value_ = QDateTime::currentDateTime();
    QDateTime minimum_{QDate(1, 1, 1), QTime(0, 0)};
    QDateTime maximum_{QDate(9999, 12, 31), QTime(23, 59, 59, 999)};
    int field_ = 0;
    QString digits_;
    bool secondsVisible_ = true;
    bool updating_ = false;
};

} // namespace md
