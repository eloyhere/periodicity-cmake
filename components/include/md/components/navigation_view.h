// Periodicity — NavigationView, the Material 3 navigation panel content.
//
// A drawer or a navigation rail needs a reusable list of destinations: an
// optional account header, section labels, dividers and selectable rows with
// an icon, a label and a badge. NavigationView is that content on its own: it
// paints the rows, owns the selection and reports activation, and it can be
// placed inside a Drawer, inside a rail or on a page.
//
// It deliberately does not animate: opening, closing and scrimming belong to
// the surface that hosts it (Drawer for the modal case). That keeps the
// painted geometry identical between hit testing and rendering.

#pragma once

#include <md/ripple/ripple.h>
#include <md/view/view.h>

#include <QIcon>
#include <QList>
#include <QString>
#include <QVariant>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;
class QWheelEvent;

namespace md {

class NavigationView final : public View
{
    Q_OBJECT

public:
    struct Entry
    {
        enum class Kind {
            Header,  // a section label, never selectable
            Item,    // a destination
            Divider, // a hairline
        };

        Kind kind = Kind::Item;
        QString label;
        QIcon icon;
        QString badge;
        bool enabled = true;
        QVariant value;

        [[nodiscard]] friend bool operator==(const Entry&, const Entry&) = default;
    };

    explicit NavigationView(QWidget* parent = nullptr);

    // Account header -----------------------------------------------------
    void setHeaderTitle(const QString& title);
    [[nodiscard]] QString headerTitle() const { return headerTitle_; }
    void setHeaderSubtitle(const QString& subtitle);
    [[nodiscard]] QString headerSubtitle() const { return headerSubtitle_; }
    void setHeaderVisible(bool visible);
    [[nodiscard]] bool isHeaderVisible() const noexcept { return headerVisible_; }

    // Entries ------------------------------------------------------------
    int addSection(const QString& title);
    int addItem(const QString& label, const QIcon& icon = QIcon(),
                const QString& badge = QString());
    int addDivider();
    void setEntries(const QList<Entry>& entries);
    [[nodiscard]] QList<Entry> entries() const { return entries_; }
    void clearEntries();
    [[nodiscard]] int entryCount() const { return static_cast<int>(entries_.size()); }
    [[nodiscard]] int itemCount() const;
    [[nodiscard]] Entry entry(int index) const;
    void setEntryEnabled(int index, bool enabled);
    [[nodiscard]] bool isEntryEnabled(int index) const;
    void setEntryBadge(int index, const QString& badge);

    // Selection ----------------------------------------------------------
    // Only an enabled Item can be selected; a header or divider index is
    // ignored. -1 clears the selection.
    void setSelectedIndex(int index);
    [[nodiscard]] int selectedIndex() const noexcept { return selected_; }
    [[nodiscard]] QString selectedLabel() const;
    void clearSelection();

    // Scrolling and geometry --------------------------------------------
    void setScrollOffset(qreal offset);
    [[nodiscard]] qreal scrollOffset() const noexcept { return scroll_; }
    [[nodiscard]] qreal contentHeight() const;
    [[nodiscard]] QRectF headerRect() const;
    [[nodiscard]] QRectF entryRect(int index) const;
    [[nodiscard]] int entryIndexAt(const QPointF& position) const;
    [[nodiscard]] int highlightedIndex() const noexcept { return selected_; }

signals:
    // A click, Return, Enter or Space on `index`.
    void itemActivated(int index, QString label);
    void selectionChanged(int index);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void changeEvent(QEvent* event) override;
    void onThemeChanged() override;
    [[nodiscard]] bool focusable() const override;

private:
    [[nodiscard]] qreal entryHeight(const Entry& entry) const;
    [[nodiscard]] qreal entriesTop() const;
    [[nodiscard]] int nextItem(int from, int direction) const;
    [[nodiscard]] int firstItem() const;
    void clampScroll();
    void activate(int index);

    QList<Entry> entries_;
    QString headerTitle_;
    QString headerSubtitle_;
    bool headerVisible_ = true;
    int selected_ = -1;
    qreal scroll_ = 0.0;
    int hovered_ = -1;
    ripple::RippleController rowRipple_;
};

} // namespace md
