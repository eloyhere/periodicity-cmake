// Periodicity — the 1D strip context used by Canvas.
//
// Context1D renders a compact, horizontal strip of scalar samples: an optional
// tick grid, an optional zero baseline, a polyline through the samples and
// optional dots at each sample. Values are clamped at ingestion to the current
// [minimum, maximum] range; a reversed range is normalised. The strip is
// deliberately free of axes and labels — it is the sparkline-sized primitive a
// Canvas composes into a larger chart, not a full plot widget.
//
// The context retains its data; Canvas repaints it on demand. All dimensions
// are density-scaled from Material dp, and the default colours come from the
// active theme.

#pragma once

#include <QColor>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QVector>
#include <QWidget>
#include <QtGlobal>

class QPainter;

namespace md {

class Context1D
{
public:
    explicit Context1D(QWidget* host = nullptr);
    ~Context1D();

    Context1D(const Context1D&) = delete;
    Context1D& operator=(const Context1D&) = delete;

    // A reversed range is normalised. A non-finite bound is replaced by 0/1;
    // an equal range maps every value to the middle of the strip. Stored
    // samples are re-clamped to the new range.
    void setRange(qreal minimum, qreal maximum);
    [[nodiscard]] qreal minimum() const noexcept;
    [[nodiscard]] qreal maximum() const noexcept;

    // Every sample is clamped to [minimum(), maximum()] as it is stored. A
    // non-finite sample becomes minimum().
    void setValues(QVector<qreal> values);
    [[nodiscard]] const QVector<qreal>& values() const noexcept;

    // Number of horizontal grid lines, including both ends. Zero disables the
    // grid; the value is clamped to 0..1000.
    void setTicks(int count);
    [[nodiscard]] int ticks() const noexcept;

    void setBaselineVisible(bool visible);
    [[nodiscard]] bool baselineVisible() const noexcept;
    [[nodiscard]] bool isBaselineVisible() const noexcept { return baselineVisible(); }

    void setPolylineVisible(bool visible);
    [[nodiscard]] bool polylineVisible() const noexcept;
    [[nodiscard]] bool isPolylineVisible() const noexcept { return polylineVisible(); }

    void setDotsVisible(bool visible);
    [[nodiscard]] bool dotsVisible() const noexcept;
    [[nodiscard]] bool isDotsVisible() const noexcept { return dotsVisible(); }

    // Invalid colours restore the theme default for that element.
    void setLineColour(const QColor& colour);
    [[nodiscard]] QColor lineColour() const;
    void setDotColour(const QColor& colour);
    [[nodiscard]] QColor dotColour() const;
    void setBaselineColour(const QColor& colour);
    [[nodiscard]] QColor baselineColour() const;
    void setTickColour(const QColor& colour);
    [[nodiscard]] QColor tickColour() const;

    // Stroke width and dot radius in dp. The stroke width is clamped to >= 0;
    // the radius is clamped to >= 0.5 dp so a dot remains visible.
    void setLineWidth(qreal widthDp);
    [[nodiscard]] qreal lineWidth() const noexcept;
    void setDotRadius(qreal radiusDp);
    [[nodiscard]] qreal dotRadius() const noexcept;

    // Maps a value into the strip: maximum at the top, minimum at the bottom.
    // A value outside the range is clamped; an equal range maps to the middle.
    [[nodiscard]] qreal valueToY(qreal value, const QRectF& area) const noexcept;

    // Maps sample `index` to (x, valueToY(...)). The x axis is evenly spaced;
    // a single sample is centered.
    [[nodiscard]] QPointF pointAt(int index, const QRectF& area) const noexcept;

    // Paints the retained strip inside `content`, clipped to that rectangle.
    void paint(QPainter& painter, const QRectF& content);

    // Drops the samples but keeps range, appearance and ticks.
    void clear();

    // Schedules an update of the host widget.
    void requestDraw();

private:
    [[nodiscard]] qreal clampValue(qreal value) const noexcept;
    void clampStoredValues() noexcept;

    QPointer<QWidget> host_;
    qreal minimum_ = 0.0;
    qreal maximum_ = 1.0;
    QVector<qreal> values_;
    int ticks_ = 0;
    bool baselineVisible_ = true;
    bool polylineVisible_ = true;
    bool dotsVisible_ = true;
    QColor lineColour_;
    QColor dotColour_;
    QColor baselineColour_;
    QColor tickColour_;
    qreal lineWidthDp_ = 2.0;
    qreal dotRadiusDp_ = 2.5;
};

} // namespace md
