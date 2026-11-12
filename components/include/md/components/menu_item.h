// Periodicity — MenuItem, one row of a navigation menu.
//
// A MenuItem is a focusable, self-painted row: a leading icon, a label, an
// optional trailing stack (a badge, a trailing text, a shortcut) and, when the
// row expands something, a chevron. It is the building block of MenuGroup and
// the base class of SubMenu; both use the same row geometry, so a leaf and a
// parent row line up pixel for pixel.
//
// States follow Material 3: hover draws a state layer inside the row pill,
// press deepens it, and selected fills the pill with secondaryContainer and
// paints the label in onSecondaryContainer. Indentation is part of the row,
// not of the container: setIndentLevel() shifts the leading edge by 16 dp per
// level, which is how a nested menu keeps its hierarchy readable.
//
// The chevron is a rotation, not a second image: setExpanded() moves it to the
// 90-degree endpoint and setArrowRotation() drives the in-between values, so a
// SubMenu can animate the arrow with the same number it animates its height
// with. A right-to-left layout mirrors the row (icon on the trailing side,
// text right-aligned) and the chevron's base direction.
//
// Activation is one path for mouse and keyboard: activate() emits clicked()
// and activated(). A SubMenu overrides it to toggle its expansion.

#pragma once

#include <md/view/view.h>

#include <QIcon>
#include <QString>
#include <Qt>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;

namespace md {

class MenuItem : public View
{
    Q_OBJECT

public:
    explicit MenuItem(QWidget* parent = nullptr);
    explicit MenuItem(const QString& label, QWidget* parent = nullptr);

    void setLabel(const QString& label);
    [[nodiscard]] QString label() const { return label_; }

    void setIcon(const QIcon& icon);
    [[nodiscard]] QIcon icon() const { return icon_; }

    // A short count or status pill after the label.
    void setBadge(const QString& badge);
    [[nodiscard]] QString badge() const { return badge_; }

    // A keyboard accelerator hint, painted in labelSmall on the trailing side.
    void setShortcut(const QString& shortcut);
    [[nodiscard]] QString shortcut() const { return shortcut_; }

    // Free trailing text (a value, a state) painted before the shortcut.
    void setTrailingText(const QString& text);
    [[nodiscard]] QString trailingText() const { return trailingText_; }

    // The selected row: a secondaryContainer pill. Selection is a property of
    // the row; MenuGroup keeps at most one selected row.
    void setSelected(bool selected);
    [[nodiscard]] bool isSelected() const noexcept { return selected_; }

    // How many 16 dp steps the content is indented. Negative values are
    // clamped to zero.
    virtual void setIndentLevel(int level);
    [[nodiscard]] int indentLevel() const noexcept { return indentLevel_; }

    // An expandable row reserves the chevron box and shows it.
    void setExpandable(bool expandable);
    [[nodiscard]] bool isExpandable() const noexcept { return expandable_; }
    virtual void setExpanded(bool expanded);
    [[nodiscard]] bool isExpanded() const noexcept { return expanded_; }

    // The chevron rotation in degrees: 0 points at the trailing edge, 90 at the
    // expanded direction. setExpanded() uses the endpoints; a SubMenu animates
    // the values in between.
    void setArrowRotation(qreal degrees);
    [[nodiscard]] qreal arrowRotation() const noexcept { return arrowRotation_; }

    // The row height in dp. The M3 menu row is 48 dp; a dense navigation
    // drawer can ask for less.
    void setRowHeight(qreal dp);
    [[nodiscard]] qreal rowHeight() const noexcept { return rowHeightDp_; }

    // The height the row wants at its current expansion, in pixels.
    [[nodiscard]] virtual qreal preferredHeight() const;

    // Geometry, in widget coordinates. itemRect() is the whole row (the pill
    // is inset from it); the other rectangles are empty when the part is not
    // shown.
    [[nodiscard]] QRectF itemRect() const;
    [[nodiscard]] QRectF pillRect() const;
    [[nodiscard]] QRectF iconRect() const;
    [[nodiscard]] QRectF textRect() const;
    [[nodiscard]] QRectF arrowRect() const;
    [[nodiscard]] QRectF badgeRect() const;

    // The label as the paint pass elides it at the current width.
    [[nodiscard]] QString elidedLabel() const;

    // Activates the row exactly like a click or Enter: clicked() then
    // activated(). A disabled row does nothing. Virtual so that a SubMenu can
    // turn the activation into an expansion toggle.
    virtual void activate();

signals:
    void clicked();
    void activated();

protected:
    // The row in widget coordinates: the top `rowHeight()` band of the widget.
    // A SubMenu is taller because its children follow the row.
    [[nodiscard]] QRectF rowRect() const;
    [[nodiscard]] qreal rowHeightPx() const;

    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;
    void onThemeChanged() override;
    [[nodiscard]] bool focusable() const override;
    bool onEvent(Event& event) override;

private:
    // One resolved row layout. The paint pass and the geometry accessors share
    // it so that what is drawn and what a hit test finds cannot drift apart.
    struct RowLayout
    {
        QRectF row;
        QRectF pill;
        QRectF icon;
        QRectF text;
        QRectF arrow;
        QRectF badge;
        QRectF shortcut;
        QRectF trailing;
        bool hasIcon = false;
        bool hasBadge = false;
        bool hasShortcut = false;
        bool hasTrailing = false;
    };

    [[nodiscard]] RowLayout computeLayout() const;
    [[nodiscard]] QString elide(const QString& text, qreal width) const;

    QString label_;
    QIcon icon_;
    QString badge_;
    QString shortcut_;
    QString trailingText_;
    int indentLevel_ = 0;
    qreal rowHeightDp_ = 48.0;
    qreal arrowRotation_ = 0.0;
    bool selected_ = false;
    bool expandable_ = false;
    bool expanded_ = false;
    bool pressedInside_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
};

} // namespace md
