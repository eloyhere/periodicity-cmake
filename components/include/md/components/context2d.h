// Periodicity — the 2D painter façade used by Canvas.
//
// Context2D is a small, stateful wrapper around QPainter for the 2D mode of
// md::Canvas. Drawing calls are retained as commands together with the style
// and data transform that were current when they were issued; Canvas replays
// them into its painter on every paint. That keeps the public API immediate-
// looking (set a pen, draw a line) while still allowing a Canvas to be resized,
// repainted or rendered offscreen without the host having to re-issue the
// drawing commands.
//
// Coordinates are in "data" space and are mapped through the transform set by
// setTransform(). The default transform is the identity, so data coordinates
// are widget coordinates. Clip rectangles and paths are also expressed in data
// coordinates and are applied after the data transform. `save()`/`restore()`
// only save the drawing state (pen, brush, font, transform and clips); they do
// not remove already recorded commands.
//
// The context is intentionally defensive: an empty host is allowed, every
// drawing/state entry point catches exceptions and reports them with qWarning,
// and replay clips each command to the Canvas content rectangle. The default
// pen and brush come from the current design baseline theme, scaled by its Density.

#pragma once

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QTransform>
#include <QtGlobal>

#include <memory>

class QPainter;
class QPainterPath;
class QPolygonF;
class QString;
class QWidget;

namespace md {

class Context2D
{
public:
    // `host` is optional; it is used for theme resolution and for scheduling a
    // repaint after a state or command change. Canvas passes itself.
    explicit Context2D(QWidget* host = nullptr);
    ~Context2D();

    Context2D(const Context2D&) = delete;
    Context2D& operator=(const Context2D&) = delete;

    // Drawing state -------------------------------------------------------
    void save();
    void restore();

    // Clips are data-space and stay in effect until clearClip(), restore() or
    // reset().
    void setClipRect(const QRectF& rect);
    void setClipPath(const QPainterPath& path);
    void clearClip();

    void setPen(const QPen& pen);
    void setPen(const QColor& colour);
    void setBrush(const QBrush& brush);
    void setBrush(const QColor& colour);
    void setFont(const QFont& font);

    [[nodiscard]] QPen pen() const;
    [[nodiscard]] QBrush brush() const;
    [[nodiscard]] QFont font() const;

    // The data-to-widget transform. (0, 0) maps to (offsetX, offsetY); the
    // default is the identity map. Non-finite arguments are rejected with a
    // warning. A zero scale is legal (the drawing collapses to a point/line),
    // but mapToData() then cannot invert it.
    void setTransform(qreal scaleX, qreal scaleY, qreal offsetX = 0.0,
                      qreal offsetY = 0.0);
    void setTransform(const QTransform& transform);
    [[nodiscard]] QTransform transform() const;

    [[nodiscard]] QPointF mapToWidget(const QPointF& point) const;
    [[nodiscard]] QPointF mapToData(const QPointF& point) const;

    // Primitives ----------------------------------------------------------
    void drawLine(const QPointF& from, const QPointF& to);
    void drawPolyline(const QPolygonF& points);
    void drawRect(const QRectF& rect);
    void drawRoundedRect(const QRectF& rect, qreal radiusX, qreal radiusY);
    void drawEllipse(const QRectF& rect);
    void drawEllipse(const QPointF& centre, qreal radiusX, qreal radiusY);
    void drawArc(const QRectF& rect, int startAngle, int spanAngle);
    void drawPath(const QPainterPath& path);
    void drawText(const QPointF& position, const QString& text);
    void drawText(const QRectF& rect, int flags, const QString& text);

    // Convenience aliases matching the short names used in the design notes.
    void line(const QPointF& from, const QPointF& to) { drawLine(from, to); }
    void polyline(const QPolygonF& points) { drawPolyline(points); }
    void rect(const QRectF& rect) { drawRect(rect); }
    void roundedRect(const QRectF& rect, qreal radiusX, qreal radiusY)
    {
        drawRoundedRect(rect, radiusX, radiusY);
    }
    void ellipse(const QRectF& rect) { drawEllipse(rect); }
    void ellipse(const QPointF& centre, qreal radiusX, qreal radiusY)
    {
        drawEllipse(centre, radiusX, radiusY);
    }
    void arc(const QRectF& rect, int startAngle, int spanAngle)
    {
        drawArc(rect, startAngle, spanAngle);
    }
    void path(const QPainterPath& path) { drawPath(path); }
    void text(const QPointF& position, const QString& value)
    {
        drawText(position, value);
    }
    void text(const QRectF& rect, int flags, const QString& value)
    {
        drawText(rect, flags, value);
    }

    // Replay --------------------------------------------------------------
    // Replays every retained command, clipped to `viewport` (the Canvas content
    // rectangle). Normally called by Canvas::paintContent(). Safe to call with
    // an inactive painter; such a call is ignored.
    void paint(QPainter& painter, const QRectF& viewport);

    // Drops the retained commands but keeps the current style and transform.
    void clear();
    // Drops the commands, the state stack and restores the theme defaults.
    void reset();

    // Schedules an update of the host widget.
    void requestDraw();

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace md
