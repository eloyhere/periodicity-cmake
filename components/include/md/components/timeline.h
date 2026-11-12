// Periodicity — the Timeline component (framework.md §5.9).
//
// A vertical timeline: every item is a row with a time on the left, a marker on
// the rail and a title/description block on the right. The rail is drawn from
// marker to marker, so the last item terminates it instead of trailing off into
// empty space (frameworks that draw one long line always look unfinished when
// the list is short).
//
// The list scrolls inside the widget: scrollOffset() is the distance from the
// top of the content, the wheel and Up/PageUp/Home move it, and every item
// rectangle is reported in viewport coordinates, which is exactly what the
// painter and the hit test read. A scroll request is clamped to
// maximumScrollOffset(), so the component can never scroll into blank space.

#pragma once

#include <md/view/view.h>

#include <QColor>
#include <QList>
#include <QRectF>
#include <QString>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QWheelEvent;

namespace md {

// One entry of the list. `color` tints the marker; an empty colour means the
// theme's primary. `icon` is optional text drawn inside the marker (a glyph or
// an emoji) — when it is empty the marker is a plain dot.
struct TimelineItem
{
    QString time;
    QString title;
    QString description;
    QColor color;
    QString icon;

    [[nodiscard]] friend bool operator==(const TimelineItem& lhs,
                                         const TimelineItem& rhs) = default;
};

class Timeline : public View
{
    Q_OBJECT

public:
    explicit Timeline(QWidget* parent = nullptr);

    void setItems(const QList<TimelineItem>& items);
    void addItem(const TimelineItem& item);
    void insertItem(int index, const TimelineItem& item);
    void removeItem(int index);
    void clear();

    [[nodiscard]] QList<TimelineItem> items() const { return items_; }
    [[nodiscard]] int itemCount() const;

    // The scroll position in pixels from the top of the content. The setter
    // clamps to [0, maximumScrollOffset()].
    [[nodiscard]] qreal scrollOffset() const noexcept { return scrollOffset_; }
    void setScrollOffset(qreal offset);

    // The full height of the items plus the vertical padding, independent of
    // the viewport, and the largest legal scroll position.
    [[nodiscard]] qreal contentHeight() const;
    [[nodiscard]] qreal maximumScrollOffset() const;

    [[nodiscard]] qreal itemHeight() const;

    // Geometry, in viewport (widget) coordinates and already scrolled. An item
    // outside the viewport still has a rectangle; it simply has no visible
    // intersection with the widget.
    [[nodiscard]] QRectF itemRect(int index) const;
    [[nodiscard]] QRectF markerRect(int index) const;
    [[nodiscard]] QRectF timeRect(int index) const;
    [[nodiscard]] QRectF textRect(int index) const;

    // The item under `point`, or -1.
    [[nodiscard]] int itemAt(const QPointF& point) const;

signals:
    void itemClicked(int index);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    [[nodiscard]] qreal padding() const;
    [[nodiscard]] qreal railX() const;
    [[nodiscard]] qreal timeColumnWidth() const;
    void clampScroll();

    QList<TimelineItem> items_;
    qreal scrollOffset_ = 0.0;
    QPointF pressed_{-1.0, -1.0};
};

} // namespace md
