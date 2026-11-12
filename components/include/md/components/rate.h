// Periodicity — the Rate component (framework.md §5.9).
//
// A star rating: max stars, a value in [0, max] and a step that may be 0.5 for
// half-star ratings. Each star is a five-point QPainterPath filled
// proportionally to the value, so a half star is half a filled path rather
// than a separate glyph. Hovering previews the value under the pointer;
// pressing and releasing inside commits it. The left and right arrows (and up
// and down) adjust by one step, Home and End jump to the ends.
//
// The row occupies a 48 dp touch target while the stars are the 24 dp design baseline size,
// vertically centred inside it.

#pragma once

#include <md/view/view.h>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;

namespace md {

class Rate : public View
{
    Q_OBJECT

public:
    explicit Rate(QWidget* parent = nullptr);

    void setMax(int maximum);
    [[nodiscard]] int maximum() const noexcept;

    void setValue(qreal value);
    [[nodiscard]] qreal value() const noexcept;

    // The granularity of a click and of a keyboard adjustment. Zero or less
    // disables snapping. The default is 1.0; 0.5 enables half stars.
    void setStep(qreal step);
    [[nodiscard]] qreal step() const noexcept;

signals:
    void valueChanged(qreal value);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    [[nodiscard]] qreal snap(qreal value) const;
    [[nodiscard]] qreal valueFromX(qreal x) const;
    void applyValue(qreal value);

    int maximum_ = 5;
    qreal value_ = 0.0;
    qreal step_ = 1.0;
    qreal preview_ = -1.0;
    bool pressedInside_ = false;
};

} // namespace md
