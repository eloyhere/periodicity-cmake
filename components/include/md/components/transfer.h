// Periodicity — Transfer (media/content batch).
//
// A two-pane transfer box: "Available" on the leading side, "Selected" on the
// trailing side, with a button column between them (add selected, remove
// selected, add all, remove all). Both panes scroll independently and keep
// their own selection; the widget reports every list mutation through
// itemsChanged().
//
// The four buttons are real md::Button children — the transfer owns the layout,
// not the button look — while the panes are painted directly: a viewport, a
// header with a live count, a row per entry and an empty state. Multi-selection
// follows the desktop convention: a plain click replaces the selection, Ctrl
// toggles one row, Shift extends from the anchor, double-click moves the row
// across, and Ctrl+A selects the focused pane. The public move/select methods
// exist so a host and a test can drive the widget without synthesising input.

#pragma once

#include <md/view/view_group.h>

#include <QList>
#include <QSet>
#include <QStringList>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QPointF;
class QRectF;
class QWheelEvent;

namespace md {

class Button;

class Transfer : public ViewGroup
{
    Q_OBJECT

public:
    explicit Transfer(QWidget* parent = nullptr);

    // Replaces the available list and clears the selected one.
    void setItems(const QStringList& items);
    void addItems(const QStringList& items);
    void addItem(const QString& item);

    [[nodiscard]] QStringList availableItems() const;
    [[nodiscard]] QStringList selectedItems() const;
    [[nodiscard]] int availableCount() const;
    [[nodiscard]] int selectedCount() const;

    void setAvailableLabel(const QString& label);
    [[nodiscard]] QString availableLabel() const;
    void setSelectedLabel(const QString& label);
    [[nodiscard]] QString selectedLabel() const;

    // Replaces the pane selection. With extend the anchor is kept and the
    // range between the anchor and `row` is selected (Shift-click semantics).
    void selectAvailable(int row, bool extend = false);
    void selectSelected(int row, bool extend = false);
    [[nodiscard]] QList<int> availableSelection() const;
    [[nodiscard]] QList<int> selectedSelection() const;
    void clearAvailableSelection();
    void clearSelectedSelection();

    // Move every selected row to the other pane, preserving order.
    void moveSelectedRight();
    void moveSelectedLeft();

    // Move the whole pane to the other side.
    void moveAllRight();
    void moveAllLeft();

    [[nodiscard]] qreal availableScrollOffset() const;
    void setAvailableScrollOffset(qreal offset);
    [[nodiscard]] qreal selectedScrollOffset() const;
    void setSelectedScrollOffset(qreal offset);

signals:
    void itemsChanged(const QStringList& available, const QStringList& selected);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeChildren(const QRectF& content) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    [[nodiscard]] bool focusable() const override;

private:
    enum class Pane {
        Available,
        Selected,
    };

    [[nodiscard]] QRectF paneRect(Pane pane) const;
    [[nodiscard]] QRectF buttonColumnRect() const;
    [[nodiscard]] qreal rowHeightPx() const;
    [[nodiscard]] qreal headerHeightPx() const;
    [[nodiscard]] qreal paneViewportHeight(const QRectF& pane) const;
    [[nodiscard]] int rowAt(Pane pane, const QPointF& position) const;
    [[nodiscard]] qreal maximumScroll(Pane pane) const;
    [[nodiscard]] const QStringList& itemsFor(Pane pane) const;
    [[nodiscard]] QStringList& itemsFor(Pane pane);
    [[nodiscard]] const QSet<int>& selectionFor(Pane pane) const;
    [[nodiscard]] QSet<int>& selectionFor(Pane pane);
    [[nodiscard]] int& anchorFor(Pane pane);

    void applySelection(Pane pane, int row, bool extend);
    void handlePaneClick(Pane pane, const QPointF& position, Qt::KeyboardModifiers modifiers);
    void scrollPane(Pane pane, qreal delta);
    void clampScroll();
    void syncButtons();
    void emitItemsChanged();
    void moveSelected(Pane from);
    void moveAll(Pane from);
    void paintPane(QPainter& painter, Pane pane, const QRectF& paneGeometry);
    void focusPane(Pane pane);

    QStringList available_;
    QStringList selected_;

    QSet<int> availableSelection_;
    QSet<int> selectedSelection_;
    int availableAnchor_ = -1;
    int selectedAnchor_ = -1;

    qreal availableScroll_ = 0.0;
    qreal selectedScroll_ = 0.0;

    QString availableLabel_ = QStringLiteral("Available");
    QString selectedLabel_ = QStringLiteral("Selected");
    Pane focusedPane_ = Pane::Available;

    Button* addButton_ = nullptr;
    Button* removeButton_ = nullptr;
    Button* addAllButton_ = nullptr;
    Button* removeAllButton_ = nullptr;
};

} // namespace md
