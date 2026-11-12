// Periodicity — NavigationBar, the Material 3 bottom navigation bar.
//
// A navigation bar is a single 80 dp strip of three to five destinations. Each
// destination is one column: a 32 dp indicator pill around a 24 dp icon, a 4 dp
// gap and a labelMedium label. The bar owns its destinations as values and
// paints them itself, so the painted geometry and the hit-testing geometry are
// the same pass, exactly as TabBar does for its tabs.
//
// Selection and focus coincide on a navigation bar: currentIndex() is both the
// highlighted destination and the one the keyboard acts on. Clicking a
// destination, or pressing Space or Return on it, changes the selection and
// emits destinationActivated(); the arrow keys only move the highlight.
//
// The layout mirrors when the widget's layout direction is right-to-left:
// destination 0 is painted in the rightmost slot, and Left/Right are swapped so
// that "next" is always the reading direction. Up/Down stay verbatim (Down
// advances, Up retreats) because a horizontal bar whose slots have a visual
// order keeps the vertical axis meaning what it says.
//
// Accessibility: the destinations are painted, not child widgets, so the bar
// reports its accessible name ("Navigation") and keeps an accessible
// description that lists every destination label in logical order, marking the
// current one. A screen reader therefore hears each destination's label when it
// reads the bar, and destinationAt() exposes the labels to native tooling.

#pragma once

#include <md/drawable/icon_drawable.h>
#include <md/view/view.h>

#include <QIcon>
#include <QList>
#include <QRectF>
#include <QString>

class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;

namespace md {

class NavigationBar final : public View
{
    Q_OBJECT

public:
    struct Destination
    {
        QIcon icon;
        QIcon activeIcon; // Optional; falls back to icon when empty.
        QString label;
        QString badge;    // Optional text, e.g. "3".
    };

    explicit NavigationBar(QWidget* parent = nullptr);

    // Appends a destination and returns its index. The first destination added
    // to an empty bar becomes the current one; later ones do not change the
    // selection. The bar lays out any count evenly — three to five is the M3
    // recommendation, not an enforced limit.
    int addDestination(const Destination& destination);
    void clearDestinations();

    [[nodiscard]] int destinationCount() const noexcept;

    // A copy of the destination at `index`, or a default-built one when the
    // index is out of range (mirroring NavigationView::entry()).
    [[nodiscard]] Destination destinationAt(int index) const;

    // Clamps the selection into [-1, destinationCount() - 1]: -1 while the bar
    // is empty, otherwise the nearest valid slot. Selecting is not activating;
    // this method never emits destinationActivated().
    void setCurrentIndex(int index);
    [[nodiscard]] int currentIndex() const noexcept;

    // The widget-space slot of one destination, including the full bar height.
    // `index` is logical; the visual position follows the layout direction.
    [[nodiscard]] QRectF destinationRect(int index) const;

signals:
    // A click, Space, Return or Enter on `index`. Emitted even when the
    // destination was already current.
    void destinationActivated(int index);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;
    void onThemeChanged() override;

private:
    [[nodiscard]] bool isRightToLeft() const;

    // The slot `index` occupies from the left edge. The mapping is the single
    // place where the logical order becomes a visual order.
    [[nodiscard]] int visualSlot(int index) const;

    [[nodiscard]] int destinationIndexAt(const QPointF& position) const;

    void activate(int index);
    void paintDestination(QPainter& painter, int index, const QRectF& slot);
    void refreshAccessibleDescription();

    QList<Destination> destinations_;
    int current_ = -1;
    int hovered_ = -1;
    int pressed_ = -1;
    drawable::IconDrawable iconDrawable_;
};

} // namespace md
