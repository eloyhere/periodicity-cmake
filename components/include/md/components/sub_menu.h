// Periodicity — SubMenu, a menu row that expands into child rows.
//
// A SubMenu is a MenuItem whose activation opens instead of selecting: it owns
// a list of child MenuItems, arranges them below its own row and grows its
// height to reveal them. The children are real widgets and are indented one
// level deeper than the header, so a nested level is readable by its geometry
// alone. Activating a child reports childActivated(index) with the child's
// position in this submenu.
//
// The expansion is one number — expansion_, 0 fully closed, 1 fully open — and
// the row height, the children's visibility and the chevron rotation are all
// derived from it. A QTimer moves that number over the theme's short4 motion
// spec; when the theme asks for reduced motion (a zero duration) the submenu
// snaps to the end state instead, and finishAnimation() is the deterministic
// way for a test or a screenshot pass to settle it immediately.
//
// Keyboard: the header row is an ordinary MenuItem (Enter/Space toggles, via
// activate()); Right/Left open and close it. Up/Down bubble to the owning
// MenuGroup, which walks the visible rows of the whole group.

#pragma once

#include <md/components/menu_item.h>

#include <QEasingCurve>
#include <QElapsedTimer>
#include <QList>
#include <QTimer>
#include <Qt>

class QPainter;
class QRectF;

namespace md {

class SubMenu final : public MenuItem
{
    Q_OBJECT

public:
    explicit SubMenu(QWidget* parent = nullptr);
    explicit SubMenu(const QString& label, QWidget* parent = nullptr);
    ~SubMenu() override;

    // Children. The submenu reparents an item, indents it one level deeper
    // than the header and takes part in its lifetime only through Qt's widget
    // tree: removeItem() detaches it without deleting it.
    int addItem(MenuItem* item);
    int addChild(MenuItem* item);
    int insertItem(int index, MenuItem* item);
    void removeItem(MenuItem* item);
    void clearItems();
    [[nodiscard]] int childCount() const;
    [[nodiscard]] MenuItem* childAt(int index) const;
    [[nodiscard]] int indexOfChild(const MenuItem* item) const;
    [[nodiscard]] QList<MenuItem*> childItems() const;

    // Opening and closing. setExpanded() animates unless the motion scheme is
    // reduced; finishAnimation() snaps to the target and stops the timer.
    void setExpanded(bool expanded) override;
    [[nodiscard]] qreal expansion() const noexcept { return expansion_; }
    [[nodiscard]] bool isAnimating() const noexcept { return timer_.isActive(); }
    void finishAnimation();
    void setAnimated(bool animated);
    [[nodiscard]] bool isAnimated() const noexcept { return animated_; }

    // The height of the revealed child block at the current expansion.
    [[nodiscard]] qreal childrenHeight() const;
    [[nodiscard]] qreal preferredHeight() const override;

    // The row activation is an expansion toggle; clicked() and activated() are
    // still emitted so a host can observe the press.
    void activate() override;

    // A child is always one indent step deeper than its header, so moving the
    // header re-indents the whole block.
    void setIndentLevel(int level) override;

signals:
    void expandedChanged(bool expanded);
    void animationFinished();
    void childActivated(int index);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeOverride(const QRectF& content) override;

    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;
    void onHidden() override;

private:
    [[nodiscard]] qreal spacingPx() const;
    void applyExpansion();
    void syncChildVisibility();
    void relayoutNow();

    QList<MenuItem*> children_;
    QTimer timer_;
    QElapsedTimer elapsed_;
    QEasingCurve easing_;
    int durationMs_ = 0;
    qreal expansion_ = 0.0;
    qreal startExpansion_ = 0.0;
    bool animated_ = true;
};

} // namespace md
