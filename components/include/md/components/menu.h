// Periodicity — Menu, an anchored command panel.
//
// A Menu is a child of the anchor's window, like PopUp, but it is an
// independent implementation: it owns its items, its highlight, its keyboard
// model and its own submenu panels. The panel is painted by the menu itself
// (the widget rectangle includes a small shadow margin, the visible panel is
// panelRect()).
//
// Placement is a request. The panel prefers to open under the anchor, aligned
// with the anchor's leading edge; when that side has no room inside the
// window's available area the panel opens above the anchor instead, and the
// final rectangle is clamped to the window and to the screen's available
// geometry. A submenu opens to the trailing side of its parent item and flips
// to the leading side when it would leave the window.
//
// Keyboard: Up/Down move the highlight, Home/End jump, Return/Enter activates,
// Right opens a submenu, Left returns to the parent panel and Escape closes the
// whole tree. A press outside the panel tree closes it; a press on the anchor is
// left to the anchor so a toggle-style host can close the panel itself. Clicking
// a leaf emits itemTriggered(); a checkable leaf toggles its check state and
// leaves the menu open, every other activation closes the tree.

#pragma once

#include <md/view/view_group.h>

#include <QIcon>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <Qt>

#include <vector>

class QEvent;
class QHideEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;
class QWidget;

namespace md {

class Menu : public ViewGroup
{
    Q_OBJECT

public:
    // One row of a menu. `separator` ignores the other fields; `submenu` turns
    // the row into a parent that opens the nested panel on the trailing side.
    struct MenuItem
    {
        QString text;
        QString shortcut;
        bool enabled = true;
        bool checkable = false;
        bool checked = false;
        bool separator = false;
        QIcon icon;
        std::vector<MenuItem> submenu;
    };

    // The side the panel picked for the last open.
    enum class Placement {
        Below,
        Above,
    };
    Q_ENUM(Placement)

    explicit Menu(QWidget* parent = nullptr);
    ~Menu() override;

    // Appends a row and returns its index. A separator may also be appended
    // with addSeparator().
    int addItem(const MenuItem& item);
    void addSeparator();
    void clear();

    [[nodiscard]] int count() const;
    [[nodiscard]] MenuItem itemAt(int index) const;

    void setItemEnabled(int index, bool enabled);
    [[nodiscard]] bool isItemEnabled(int index) const;

    void setItemChecked(int index, bool checked);
    [[nodiscard]] bool isItemChecked(int index) const;

    // The widget the panel is attached to. Not owned; destroying the anchor
    // closes the menu.
    void setAnchor(QWidget* anchor);
    [[nodiscard]] QWidget* anchor() const;

    // Opens under/over the anchor, or at an explicit panel top-left in global
    // coordinates. openAt() is also how a parent panel places its submenu.
    void open();
    void openAt(const QPoint& globalPanelTopLeft);
    void close();
    [[nodiscard]] bool isOpen() const noexcept;

    [[nodiscard]] Placement effectivePlacement() const noexcept;

    // Widget geometry of the visible panel (the widget rectangle is larger by
    // the shadow margin).
    [[nodiscard]] QRectF panelRect() const;
    [[nodiscard]] QRectF panelGlobalRect() const;

    // The row rectangle for `index`, including separator rows. Coordinates are
    // widget coordinates.
    [[nodiscard]] QRectF itemRect(int index) const;

    [[nodiscard]] int highlightedIndex() const noexcept;
    void setHighlightedIndex(int index);

    // The open submenu panel, if `index` is the row it belongs to. The panel is
    // destroyed when it or its parent closes; the pointer is not stable across
    // a close.
    [[nodiscard]] Menu* submenuAt(int index) const;
    [[nodiscard]] bool isSubmenuOpen() const noexcept;

signals:
    void itemTriggered(int index, QString text);
    void opened();
    void closed();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeChildren(const QRectF& content) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

    void onThemeChanged() override;
    [[nodiscard]] bool focusable() const override;

private:
    [[nodiscard]] bool isRoot() const;
    [[nodiscard]] Menu* rootMenu();
    [[nodiscard]] const Menu* rootMenu() const;

    void syncAppearance();
    void positionPanel();
    void installFilters();
    void removeFilters();

    void buildSubmenu(int index);
    void openSubmenu(int index);
    void closeSubmenu();
    void placeSubmenu();
    void shutdown();

    [[nodiscard]] bool containsGlobal(const QPoint& globalPoint) const;

    void moveHighlight(int step);
    [[nodiscard]] bool activateIndex(int index);
    [[nodiscard]] int firstEnabledIndex() const;
    [[nodiscard]] int lastEnabledIndex() const;
    [[nodiscard]] int indexAt(const QPointF& point) const;
    [[nodiscard]] bool isSeparator(int index) const;

    [[nodiscard]] qreal rowHeight(int index) const;
    [[nodiscard]] qreal panelPadding() const;
    [[nodiscard]] qreal naturalPanelWidth() const;
    [[nodiscard]] qreal naturalPanelHeight() const;
    [[nodiscard]] QRectF availableBoundsGlobal() const;

    std::vector<MenuItem> items_;
    QPointer<QWidget> anchor_;

    Menu* parentMenu_ = nullptr;
    Menu* submenu_ = nullptr;
    int submenuIndex_ = -1;

    bool open_ = false;
    bool explicitPosition_ = false;
    QPoint explicitTopLeft_;
    Placement effective_ = Placement::Below;

    int highlighted_ = -1;
    int pressedIndex_ = -1;
};

} // namespace md
