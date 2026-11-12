// Periodicity — the Pagination component (framework.md §5.9).
//
// A pager drawn as one row: first, previous, the page numbers, next, last. The
// number run is compressed with ellipses once the page count no longer fits
// maxVisiblePages(), so the row keeps a stable slot count instead of growing
// with the data — the widget never needs a second line.
//
// The page model is one-based, matching what a user reads: page 1 is the first
// page and pageCount() is the last. setCurrentPage() clamps and emits
// pageChanged() only on a real move, so a binding cannot loop.
//
// The component paints itself; every button is described by a public rectangle
// (firstButtonRect, pageButtonRect, itemRect, ...) which is also the hit target
// the mouse path and the tests use.

#pragma once

#include <md/view/view.h>

#include <QList>
#include <QRectF>

class QKeyEvent;
class QMouseEvent;
class QPainter;

namespace md {

class Pagination : public View
{
    Q_OBJECT
    Q_PROPERTY(int currentPage READ currentPage WRITE setCurrentPage NOTIFY pageChanged)

public:
    explicit Pagination(QWidget* parent = nullptr);

    [[nodiscard]] int pageCount() const noexcept { return pageCount_; }

    // Sets the number of pages. At least one page always exists; the current
    // page is clamped and pageChanged() is emitted when it moves.
    void setPageCount(int count);

    [[nodiscard]] int currentPage() const noexcept { return current_; }

    // Moves to `page` (clamped to [1, pageCount]) and emits pageChanged() when
    // the page actually changes.
    void setCurrentPage(int page);

    void first();
    void last();
    void previousPage();
    void nextPage();

    // How many slots the number run may occupy, ellipses included. Values below
    // five are raised to five: fewer cannot express "first ... current ... last".
    [[nodiscard]] int maxVisiblePages() const noexcept { return maxVisible_; }
    void setMaxVisiblePages(int count);

    // The number run, left to right. A negative entry is an ellipsis slot.
    [[nodiscard]] QList<int> visiblePages() const { return items_; }

    // Geometry, in widget coordinates. `slot` indexes visiblePages(); the four
    // navigation rectangles are separate because they do not appear in it.
    [[nodiscard]] QRectF itemRect(int slot) const;
    [[nodiscard]] QRectF pageButtonRect(int page) const;
    [[nodiscard]] QRectF firstButtonRect() const;
    [[nodiscard]] QRectF previousButtonRect() const;
    [[nodiscard]] QRectF nextButtonRect() const;
    [[nodiscard]] QRectF lastButtonRect() const;
    [[nodiscard]] QRectF rowRect() const;

signals:
    void pageChanged(int page);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    [[nodiscard]] qreal gap() const;
    [[nodiscard]] qreal buttonSize() const;
    [[nodiscard]] int slotCount() const;
    [[nodiscard]] QRectF slotRect(int index) const;
    [[nodiscard]] bool isPageEnabled(int page) const;
    void rebuildPages();
    void activateSlot(int index);
    void updatePages(const QList<int>& pages);

    int pageCount_ = 1;
    int current_ = 1;
    int maxVisible_ = 7;
    QList<int> items_;
    QPointF pressed_{-1.0, -1.0};
};

} // namespace md
