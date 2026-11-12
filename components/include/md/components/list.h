// Periodicity — List, a flat list view backed by any QAbstractItemModel.
//
// List is the "display" half of the model/view pair: the caller owns the data
// (a QAbstractItemModel*) and, optionally, the painter (a
// QStyledItemDelegate*); List owns selection, hover, keyboard navigation,
// scrolling and painting. No item is ever instantiated as a widget — rows are
// painted, and the paint pass only visits the rows that intersect the
// viewport, so a model with ten thousand rows costs the same as one with
// twenty.
//
// The delegate is never owned by the List: it is shared, exactly as
// QAbstractItemView shares one. Without a delegate, List paints a default row
// (decoration plus display text) in the theme's bodyLarge style.

#pragma once

#include <md/view/view.h>

#include <QModelIndex>
#include <QPersistentModelIndex>
#include <QPointer>

class QAbstractItemModel;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QPointF;
class QRectF;
class QResizeEvent;
class QStyledItemDelegate;
class QWheelEvent;

namespace md {

class List : public View
{
    Q_OBJECT

public:
    explicit List(QWidget* parent = nullptr);

    // The model is not owned. Passing nullptr empties the list and clears the
    // selection. All model change signals are observed, so insertion, removal,
    // resets and data changes keep the selection and the scroll offset valid.
    // Removing the selected row clears the selection and reports it once
    // through currentIndexChanged() with an invalid index.
    void setModel(QAbstractItemModel* model);
    [[nodiscard]] QAbstractItemModel* model() const;

    // The delegate is not owned; setting nullptr restores the default row.
    void setItemDelegate(QStyledItemDelegate* delegate);
    [[nodiscard]] QStyledItemDelegate* itemDelegate() const;

    // The uniform row height, in dp. The design baseline one-line list item is 56 dp.
    void setRowHeight(qreal dp);
    [[nodiscard]] qreal rowHeight() const noexcept;

    // The design baseline row metrics: a one-, two- or three-line list item is 56, 72 or
    // 88 dp tall. setRowLines() is a token-named shorthand for setRowHeight();
    // an explicit setRowHeight() stays authoritative and may be called at any
    // time.
    enum class RowLines {
        OneLine,
        TwoLine,
        ThreeLine,
    };
    Q_ENUM(RowLines)

    void setRowLines(RowLines lines);
    [[nodiscard]] static qreal rowHeightFor(RowLines lines);

    [[nodiscard]] int rowCount() const;
    // The top-level index of `row`, or an invalid index when out of range.
    [[nodiscard]] QModelIndex indexAt(int row) const;

    // Single selection: the current index is the selected index.
    [[nodiscard]] QModelIndex currentIndex() const;
    void setCurrentIndex(const QModelIndex& index);
    [[nodiscard]] int currentRow() const;
    void clearSelection();

    // Vertical scrolling, in device-independent pixels from the top.
    [[nodiscard]] qreal scrollOffset() const noexcept;
    void setScrollOffset(qreal offset);
    void scrollToRow(int row);

    // The first and the count of rows that the viewport currently shows. They
    // are the numbers the paint pass iterates over.
    [[nodiscard]] int firstVisibleRow() const;
    [[nodiscard]] int visibleRowCount() const;

    // The rectangle of `row` in widget coordinates. A row outside the viewport
    // reports a rectangle that is still valid, just clipped away.
    [[nodiscard]] QRectF rowRect(int row) const;

signals:
    void clicked(const QModelIndex& index);
    void doubleClicked(const QModelIndex& index);
    void currentIndexChanged(const QModelIndex& index);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    // A list arranged before it is shown receives no resize event, so the
    // scroll range is clamped on the arrangement path as well.
    void arrangeOverride(const QRectF& content) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    [[nodiscard]] qreal rowHeightPx() const;
    [[nodiscard]] int rowAtPosition(const QPointF& position) const;
    [[nodiscard]] qreal maximumScroll() const;
    void clampScroll();
    void ensureVisible(int row);
    void connectModel();
    void paintRow(QPainter& painter, int row, const QRectF& rect, bool selected,
                  bool hovered);
    void paintDefaultRow(QPainter& painter, const QModelIndex& index, const QRectF& rect,
                         bool selected);

    QPointer<QAbstractItemModel> model_;
    QStyledItemDelegate* delegate_ = nullptr;
    QPersistentModelIndex current_;
    // beginResetModel() invalidates current_ before modelReset() is emitted,
    // so the reset notification needs the selection state cached here.
    bool hasSelection_ = false;
    int hoveredRow_ = -1;
    int pressedRow_ = -1;
    // The selected row cached by rowsAboutToBeRemoved(): QPersistentModelIndex
    // has already invalidated itself by the time rowsRemoved() runs.
    int removedCurrentRow_ = -1;
    qreal rowHeightDp_ = 56.0;
    qreal scrollOffset_ = 0.0;
};

} // namespace md
