// Periodicity — the SeekBar component (framework.md §5.9).
//
// A horizontal slider: a 4 dp track (inactive surfaceContainerHighest, active
// primary), a 20 dp primary thumb and, on hover or while dragging, a value
// label in the inverse surface roles. Ticks are optional and derived from the
// step. Dragging updates the value continuously while the pointer is down;
// clicking anywhere on the track jumps to that position; the arrow keys move
// by one step and Home / End jump to the ends.
//
// The widget occupies a 48 dp touch target with the track low enough that the
// value label fits above the thumb inside the same rectangle.

#pragma once

#include <md/view/view.h>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;

namespace md {

class SeekBar : public View
{
    Q_OBJECT

public:
    explicit SeekBar(QWidget* parent = nullptr);

    void setRange(qreal minimum, qreal maximum);
    [[nodiscard]] qreal minimum() const noexcept;
    [[nodiscard]] qreal maximum() const noexcept;

    void setValue(qreal value);
    [[nodiscard]] qreal value() const noexcept;

    // The snapping granularity. Zero (the default) means continuous.
    void setStep(qreal step);
    [[nodiscard]] qreal step() const noexcept;

    // Draws a tick mark at every step position when the step is positive.
    void setTicksVisible(bool visible);
    [[nodiscard]] bool ticksVisible() const noexcept;

signals:
    void valueChanged(qreal value);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    [[nodiscard]] qreal snap(qreal value) const;
    [[nodiscard]] qreal valueFromX(qreal x) const;
    void applyValue(qreal value);

    qreal minimum_ = 0.0;
    qreal maximum_ = 100.0;
    qreal value_ = 0.0;
    qreal step_ = 0.0;
    bool ticksVisible_ = false;
    bool dragging_ = false;
};

} // namespace md
