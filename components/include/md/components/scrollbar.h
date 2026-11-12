// Periodicity — Scrollbar, the scrolling indicator and its controller.
//
// A Scrollbar is standalone: it owns a value inside [minimum, maximum], a page
// step and a viewport extent, and it reports every change with
// valueChanged(int). The host connects the value to whatever scrolls — the
// gallery's navigation drawer drives its menu column with it — so the
// component never needs to know its viewport.
//
// Geometry follows the design baseline: an 8 dp rounded track in
// surfaceContainerHighest and a thumb whose colour is onSurfaceVariant at rest
// and primary while hovered or dragged. The thumb widens on hover, which is
// what makes the bar readable without a permanent fat rail. The track length
// and the thumb length are proportional to `viewportExtent`; when the extent
// is not set the thumb keeps a visible minimum instead of collapsing.
//
// Interaction: pressing the thumb grabs it and moving drags the value
// proportionally, pressing the track pages toward the click, and the wheel
// steps the value. Dragging calls setValue() and converges immediately —
// there is no QPropertyAnimation in the value path, so the painted thumb is
// always the thumb the hit test would find.
//
// Keyboard: arrows step by singleStep(), PageUp/PageDown by pageStep(),
// Home/End jump to the ends.

#pragma once

#include <md/view/view.h>

#include <Qt>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;
class QWheelEvent;

namespace md {

class Scrollbar final : public View
{
    Q_OBJECT

public:
    enum class Orientation {
        Horizontal,
        Vertical,
    };
    Q_ENUM(Orientation)

    explicit Scrollbar(QWidget* parent = nullptr);
    explicit Scrollbar(Orientation orientation, QWidget* parent = nullptr);

    void setOrientation(Orientation orientation);
    [[nodiscard]] Orientation orientation() const noexcept { return orientation_; }

    // The value range, in the host's content units. An inverted range is
    // normalised (min/max swapped), and the value is clamped into the new
    // range; a change of the value is reported with valueChanged().
    void setRange(int minimum, int maximum);
    [[nodiscard]] int minimum() const noexcept { return minimum_; }
    [[nodiscard]] int maximum() const noexcept { return maximum_; }

    // The current value. Set it to scroll: the value is clamped and
    // valueChanged() is emitted only when the number actually changed.
    void setValue(int value);
    [[nodiscard]] int value() const noexcept { return value_; }

    // How far one page press and one wheel notch move the value. A page press
    // is the track click; the wheel uses singleStep() (auto when 0).
    void setPageStep(int step);
    [[nodiscard]] int pageStep() const noexcept { return pageStep_; }
    void setSingleStep(int step);
    [[nodiscard]] int singleStep() const;
    [[nodiscard]] int effectiveSingleStep() const;

    // How much of the content the viewport shows, in the same units as the
    // range. It decides the thumb length: extent / (extent + (max - min)).
    // Zero (the default) keeps the thumb at its minimum length.
    void setViewportExtent(int extent);
    [[nodiscard]] int viewportExtent() const noexcept { return viewportExtent_; }

    // Design baseline metrics, in dp. The track and thumb are painted at these
    // sizes; the hovered thumb grows to `hoverThicknessDp`.
    void setThickness(qreal dp);
    [[nodiscard]] qreal thickness() const noexcept { return thicknessDp_; }
    void setHoverThickness(qreal dp);
    [[nodiscard]] qreal hoverThickness() const noexcept { return hoverThicknessDp_; }

    // The track and the thumb, in widget coordinates. The track spans the
    // widget's cross axis and runs the full length; the thumb is centred in
    // it. Both are empty before the first arrangement.
    [[nodiscard]] QRectF trackRect() const;
    [[nodiscard]] QRectF thumbRect() const;

    [[nodiscard]] bool isDragging() const noexcept { return dragging_; }

signals:
    void valueChanged(int value);
    void sliderPressed();
    void sliderReleased();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void onThemeChanged() override;
    [[nodiscard]] bool focusable() const override;

private:
    [[nodiscard]] qreal length() const;
    [[nodiscard]] qreal thicknessPx() const;
    [[nodiscard]] qreal hoverThicknessPx() const;
    [[nodiscard]] qreal thumbLength(qreal trackLength) const;
    [[nodiscard]] qreal thumbOffset(qreal trackLength) const;
    [[nodiscard]] bool isVertical() const noexcept;
    [[nodiscard]] qreal positionOnTrack(const QPointF& position) const;
    void valueFromTrackPosition(qreal position, qreal grabOffset);
    void pageBy(int direction);

    Orientation orientation_ = Orientation::Vertical;
    int minimum_ = 0;
    int maximum_ = 0;
    int value_ = 0;
    int pageStep_ = 0;
    int singleStep_ = 0;
    int viewportExtent_ = 0;

    qreal thicknessDp_ = 8.0;
    qreal hoverThicknessDp_ = 12.0;

    bool dragging_ = false;
    qreal grabOffset_ = 0.0;
};

} // namespace md
