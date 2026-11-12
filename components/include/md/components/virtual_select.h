// Periodicity — VirtualSelect: a dropdown over a VirtualSource.
//
// The closed control is a design baseline filled field showing the current label and
// a chevron. Opening it paints a menu that is a child of the same window — on
// an md::Dropdown surface owned by this control, which places the panel under
// the field and dismisses it on an outside press or Escape — and the menu reads
// only the rows it is about to draw: it asks the source for a window of labels
// plus a couple of prefetched neighbours, never for the whole list. A source
// with a million rows therefore costs the same as one with ten.
//
// Scrolling is part of the menu: the wheel, a draggable scrollbar and
// PageUp/PageDown all move the painted window. The keyboard (up/down, Home/End,
// Enter, Escape) moves the highlight, and Enter commits it.
//
// With setFilterable(true) the field becomes a text editor while the menu is
// open: typing filters the rows by label, case-insensitively, and only the
// matches are painted. Filtering is the one path that has to read every label
// of the source, because a virtual source has no index to filter against.

#pragma once

#include <md/structure/virtual_source.h>
#include <md/view/view.h>

#include <QHash>
#include <QString>
#include <QVariant>
#include <QVector>

class QEvent;
class QHideEvent;
class QKeyEvent;
class QLineEdit;
class QMouseEvent;
class QPainter;
class QRectF;
class QWheelEvent;

namespace md {

class Dropdown;

class VirtualSelect : public View
{
    Q_OBJECT

public:
    explicit VirtualSelect(QWidget* parent = nullptr);
    ~VirtualSelect() override;

    // Non-owning. Replacing the source clears the selection and the filter.
    void setSource(structure::VirtualSource* source);
    [[nodiscard]] structure::VirtualSource* source() const noexcept;

    // A filterable field hosts a transparent QLineEdit: it is read-only while
    // the menu is closed (it shows the current label) and editable while the
    // menu is open (it is the filter). The field surface is still painted here.
    void setFilterable(bool filterable);
    [[nodiscard]] bool isFilterable() const noexcept;
    [[nodiscard]] QLineEdit* editor() const noexcept;

    void setPlaceholderText(const QString& text);
    [[nodiscard]] QString placeholderText() const;

    // The number of rows the menu paints before it scrolls.
    void setMaxVisibleRows(int rows);
    [[nodiscard]] int maxVisibleRows() const noexcept;

    // currentIndex() is the index in the source, never in the filtered order,
    // so a filter never changes what "current" means.
    [[nodiscard]] int currentIndex() const noexcept;
    void setCurrentIndex(int index);
    [[nodiscard]] QVariant currentValue() const;
    [[nodiscard]] QString currentText() const;

    [[nodiscard]] int itemCount() const;
    // The rows the menu's order contains: the source count without a filter,
    // the number of matches with one.
    [[nodiscard]] int filteredCount() const;
    // The rows the menu paints right now (0 while it is closed).
    [[nodiscard]] int visibleRowCount() const;
    [[nodiscard]] int firstVisibleRow() const;
    [[nodiscard]] int sourceIndexAt(int position) const;
    [[nodiscard]] QString labelAt(int position) const;
    // The order position of the highlight; it differs from currentIndex() as
    // soon as a filter is active, because a filter never changes the selection.
    [[nodiscard]] int highlightedPosition() const noexcept;
    // The order position of the selection, or -1 when nothing is selected or
    // the filter hides it.
    [[nodiscard]] int currentPosition() const;

    // Menu geometry, in the menu widget's own coordinates. rowRect() addresses
    // the painted window: 0 is the first row on screen.
    [[nodiscard]] int rowAt(const QPointF& popupPoint) const;
    [[nodiscard]] QRectF rowRect(int visibleRow) const;
    [[nodiscard]] QRectF scrollBarRect() const;
    [[nodiscard]] QRectF fieldRect() const;

    void showPopup();
    void hidePopup();
    [[nodiscard]] bool isPopupVisible() const;
    // The row surface: a child of the Dropdown, so its coordinates are the ones
    // rowAt() and rowRect() use.
    [[nodiscard]] QWidget* popupWidget() const noexcept;

    // The anchored surface the row widget lives in. Every control owns exactly
    // one: the menu can never be another control's, and two controls never
    // share a popup.
    [[nodiscard]] Dropdown* dropdown() const noexcept;

    // The click/Enter action for a position in the menu's order: it becomes the
    // current selection, selected() is emitted and the menu closes.
    void activatePosition(int position);

signals:
    void currentChanged(int index);
    void selected(const QVariant& value);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void arrangeOverride(const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void onThemeChanged() override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    [[nodiscard]] bool focusable() const override;

private:
    [[nodiscard]] int positionCount() const;
    [[nodiscard]] bool filtering() const;
    [[nodiscard]] QString labelForSourceIndex(int index) const;
    [[nodiscard]] int positionOfSourceIndex(int index) const;
    void applyFilter();
    void moveHighlight(int position);
    void ensurePositionVisible(int position);
    void syncEditor();
    void syncPopupLayout();
    void handlePopupClosed();
    void restoreFocus();
    void prefetchLabels();
    bool handleKey(QKeyEvent* event);

    structure::VirtualSource* source_ = nullptr;
    QLineEdit* editor_ = nullptr;
    QWidget* popup_ = nullptr;
    Dropdown* dropdown_ = nullptr;
    QString placeholder_ = QStringLiteral("Select an item");
    QString filterText_;
    QVector<int> filtered_;
    mutable QHash<int, QString> labelCache_;
    int current_ = -1;
    int highlighted_ = -1;
    int maxVisibleRows_ = 8;
    bool filterable_ = false;
    bool popupOpen_ = false;
    bool pressed_ = false;
};

} // namespace md
