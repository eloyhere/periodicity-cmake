// Periodicity — the Anchor component (framework.md §5.9).
//
// A vertical anchor list: the table of contents of a scrolled page. Each entry
// carries a key, a label and the document offset of the section it names, so
// the component can be driven from either side:
//
//   * setScrollOffset() follows the page and highlights the last section whose
//     offset has been reached; it only reports the change (activeAnchorChanged),
//     it never claims the user navigated;
//   * a click or Enter emits anchorActivated(key, offset), which is the signal
//     a scroll view connects to in order to jump.
//
// Keeping those two apart is what stops a two-way binding from looping: the
// scroll side cannot fabricate an activation, and an activation is not filtered
// by whether the entry was already highlighted.

#pragma once

#include <md/view/view.h>

#include <QList>
#include <QRectF>
#include <QString>

class QKeyEvent;
class QMouseEvent;
class QPainter;

namespace md {

// One entry. `offset` is the document offset of the section the entry names;
// the anchor list itself lays the entries out top to bottom in list order.
struct AnchorItem
{
    QString key;
    QString label;
    qreal offset = 0.0;

    [[nodiscard]] friend bool operator==(const AnchorItem& lhs,
                                         const AnchorItem& rhs) = default;
};

class Anchor : public View
{
    Q_OBJECT

public:
    explicit Anchor(QWidget* parent = nullptr);

    void setAnchors(const QList<AnchorItem>& anchors);

    // Appends an entry. An entry whose key already exists replaces the stored
    // label and offset in place, so a key is unique by construction.
    void addAnchor(const AnchorItem& anchor);
    void addAnchor(const QString& key, const QString& label, qreal offset = 0.0);
    void insertAnchor(int index, const AnchorItem& anchor);
    void removeAnchor(const QString& key);
    void clearAnchors();

    [[nodiscard]] QList<AnchorItem> anchors() const { return anchors_; }
    [[nodiscard]] int anchorCount() const;
    [[nodiscard]] int indexOfAnchor(const QString& key) const;
    [[nodiscard]] AnchorItem anchorAt(int index) const;

    // The highlighted entry, as a key (empty when the list is empty) and as an
    // index (-1 when the list is empty).
    [[nodiscard]] QString activeAnchor() const;
    [[nodiscard]] int activeIndex() const noexcept { return active_; }

    // Highlights `key` without emitting anchorActivated(). An unknown key is
    // ignored.
    void setActiveAnchor(const QString& key);

    [[nodiscard]] qreal scrollOffset() const noexcept { return scrollOffset_; }

    // Follows the page: highlights the last entry whose offset the scroll has
    // reached and emits activeAnchorChanged() when the highlight moves.
    void setScrollOffset(qreal offset);

    // Geometry, in widget coordinates. Rows are evenly stacked from the top.
    [[nodiscard]] QRectF anchorRect(int index) const;
    [[nodiscard]] QRectF indicatorRect() const;
    [[nodiscard]] qreal rowHeight() const;
    [[nodiscard]] int anchorIndexAt(const QPointF& point) const;

signals:
    void anchorActivated(const QString& key, qreal offset);
    void activeAnchorChanged(const QString& key);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    [[nodiscard]] int indexForOffset(qreal offset) const;
    void setActiveIndex(int index);
    void activate(int index);
    void clampActive();

    QList<AnchorItem> anchors_;
    int active_ = -1;
    qreal scrollOffset_ = 0.0;
    QPointF pressed_{-1.0, -1.0};
};

} // namespace md
