// Periodicity — Recycler, a widget-recycling virtual list.
//
// A Recycler is the widget-based counterpart of List: rows are real child
// widgets, but only the rows that intersect the viewport plus a prefetch
// margin exist at any moment. A scrolled-away row is returned to a pool keyed
// by view type and reused for the next row of the same type, so the steady
// scrolling path creates no widgets and allocates no item objects.
//
// The data source is a plain QAbstractItemModel*. Painting of a realised item
// is either delegated to a QStyledItemDelegate (shared, not owned) or done by
// the built-in row renderer. A caller that needs a real custom widget per item
// supplies an ItemFactory plus an ItemBinder; the factory receives the view
// type and the parent widget, the binder receives the widget it returned.
//
// Scrollbars are drawn by the Recycler itself as a thin overlay thumb on the
// right edge; no QScrollArea and no QScrollBar child is involved.

#pragma once

#include <md/view/view.h>

#include <QHash>
#include <QModelIndex>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QVector>
#include <Qt>

#include <functional>

class QAbstractItemModel;
class QKeyEvent;
class QPainter;
class QPointF;
class QRectF;
class QResizeEvent;
class QStyledItemDelegate;
class QWheelEvent;
class QWidget;

namespace md {

class Recycler : public View
{
    Q_OBJECT

public:
    // Creates the widget that represents one item of `viewType`. The returned
    // widget is reparented to the item host and fills it; returning nullptr
    // keeps the built-in row renderer.
    using ItemFactory = std::function<QWidget*(int viewType, QWidget* parent)>;

    // Binds `item` (the factory's widget, or the item host when no factory is
    // installed) to `index`.
    using ItemBinder = std::function<void(QWidget* item, const QModelIndex& index)>;

    explicit Recycler(QWidget* parent = nullptr);

    // The model is not owned. The recycler observes dataChanged, rowsInserted,
    // rowsRemoved, modelReset and layoutChanged. Removing the selected row (or
    // resetting the model) clears the selection and reports it once through
    // currentIndexChanged() with an invalid index.
    void setModel(QAbstractItemModel* model);
    [[nodiscard]] QAbstractItemModel* model() const;

    // The delegate is not owned; setting nullptr restores the built-in row.
    void setItemDelegate(QStyledItemDelegate* delegate);
    [[nodiscard]] QStyledItemDelegate* itemDelegate() const;

    // Custom item widgets. Install both before the first row is realised; a
    // change releases the widgets that are currently pooled.
    void setItemFactory(ItemFactory factory);
    void setItemBinder(ItemBinder binder);

    void setRowHeight(qreal dp);
    [[nodiscard]] qreal rowHeight() const noexcept;

    // How many rows before and after the viewport stay realised. Two is what
    // keeps a wheel notch from flashing an empty band.
    void setPrefetchRows(int rows);
    [[nodiscard]] int prefetchRows() const noexcept;

    // The model role that reports the view type. Items of different view types
    // never share a pool entry, so a two-type list keeps two small pools.
    void setViewTypeRole(int role);
    [[nodiscard]] int viewTypeRole() const noexcept;

    [[nodiscard]] QModelIndex currentIndex() const;
    void setCurrentIndex(const QModelIndex& index);
    [[nodiscard]] int currentRow() const;

    [[nodiscard]] int rowCount() const;
    [[nodiscard]] QModelIndex indexAtRow(int row) const;
    [[nodiscard]] QRectF itemRect(int row) const;

    [[nodiscard]] qreal scrollOffset() const noexcept;
    [[nodiscard]] qreal maximumScrollOffset() const;
    void setScrollOffset(qreal offset);
    void scrollToRow(int row);
    void scrollTo(const QModelIndex& index);

    // The realisation counters. `instantiatedItemCount()` is the number of item
    // widgets that currently exist (realised, not pooled); `createdItemCount()`
    // and `reusedItemCount()` are monotonic totals and are what a recycling
    // test asserts against.
    [[nodiscard]] int instantiatedItemCount() const;
    [[nodiscard]] int pooledItemCount() const;
    [[nodiscard]] int createdItemCount() const noexcept;
    [[nodiscard]] int reusedItemCount() const noexcept;

signals:
    void currentIndexChanged(const QModelIndex& index);
    void itemClicked(const QModelIndex& index);
    void itemDoubleClicked(const QModelIndex& index);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    // The arrangement hook is authoritative: a widget that is arranged before
    // it is shown never receives a resize event, so the realisation window is
    // recomputed here rather than only from resizeEvent().
    void arrangeOverride(const QRectF& content) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    // The realised item host. Defined in the implementation, because it is an
    // implementation detail: callers see widgets, never this type.
    struct Item;

    void connectModel();
    void rebuild();
    void relayout();
    void clampScroll();
    void ensureVisible(int row);
    Item* acquireItem(int viewType);
    void releaseItem(Item* item);
    void applyItemGeometry(Item* item);
    void bindItem(Item* item, const QModelIndex& index);
    void updateItems();
    void paintItem(QPainter& painter, Item& item);
    void handleItemClick(Item* item);
    void handleItemDoubleClick(Item* item);

    [[nodiscard]] int viewTypeFor(const QModelIndex& index) const;
    [[nodiscard]] qreal rowHeightPx() const;
    [[nodiscard]] qreal scrollbarWidthPx() const;
    [[nodiscard]] int rowAtPosition(const QPointF& position) const;

    QPointer<QAbstractItemModel> model_;
    QStyledItemDelegate* delegate_ = nullptr;
    ItemFactory factory_;
    ItemBinder binder_;
    QPersistentModelIndex current_;
    // beginResetModel() invalidates current_ before modelReset() is emitted,
    // so the reset notification needs the selection state cached here.
    bool hasSelection_ = false;
    // The selected row cached by rowsAboutToBeRemoved(): QPersistentModelIndex
    // has already invalidated itself by the time rowsRemoved() runs.
    int removedCurrentRow_ = -1;
    QHash<int, QVector<Item*>> pool_;
    QVector<Item*> active_;
    int prefetchRows_ = 2;
    int viewTypeRole_ = Qt::UserRole + 1;
    qreal rowHeightDp_ = 56.0;
    qreal scrollOffset_ = 0.0;
    int created_ = 0;
    int reused_ = 0;
};

} // namespace md
