// Periodicity — Drawer, the Material 3 navigation drawer.
//
// A Drawer is an in-window overlay, exactly like Dialog and Menu: it creates no
// platform window, it reparents itself to window() while it is modal and paints
// the scrim and the sliding panel into the same backing store as its host. That
// keeps it testable under the offscreen platform and lets the panel composite
// under a real compositor without a second top-level window.
//
// Two modes share one widget:
//
//   Modal     — open() covers the window, animates the panel in from the edge
//               and paints a 32% scrim behind it. A press outside the panel
//               dismisses it when scrimDismissible() is true, and Escape
//               dismisses it always.
//   Standard  — nothing is reparented and no scrim is painted. The panel is the
//               widget itself, so the host arranges it once as a resident side
//               column; open() and close() only toggle its visibility.
//
// The rows are painted, not built from child widgets: a navigation drawer is a
// list of a few dozen labels and painting them keeps the widget tree flat. The
// list scrolls with the wheel and with Up/Down, Home and End, and Return, Enter
// or Space activates the selected row.
//
// The transition is a QTimer that interpolates one progress value from 0 to 1;
// arrangeChildren() and paintContent() read it. A reduced-motion theme resolves
// the duration to zero, so the drawer snaps open or shut.

#pragma once

#include <md/view/view_group.h>

#include <QIcon>
#include <QList>
#include <QString>

class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;
class QTimer;
class QWheelEvent;

namespace md {

class Drawer : public ViewGroup
{
    Q_OBJECT

public:
    // The window edge the panel is attached to. Start is the leading edge in a
    // left-to-right layout, End the trailing one.
    enum class Edge {
        Start,
        End,
    };
    Q_ENUM(Edge)

    // Modal draws over the window; Standard is a resident side column.
    enum class Mode {
        Modal,
        Standard,
    };
    Q_ENUM(Mode)

    explicit Drawer(QWidget* parent = nullptr);

    void setEdge(Edge edge);
    [[nodiscard]] Edge edge() const noexcept;

    void setMode(Mode mode);
    [[nodiscard]] Mode mode() const noexcept;

    // The preferred panel width. It is clamped to the window width when the
    // window is narrower.
    void setWidthDp(qreal widthDp);
    [[nodiscard]] qreal widthDp() const noexcept;

    // The heading block at the top of the panel. An empty title and subtitle
    // remove the block, and the list takes the whole panel.
    void setHeader(const QString& title, const QString& subtitle = QString());
    [[nodiscard]] QString headerTitle() const;
    [[nodiscard]] QString headerSubtitle() const;

    // Appends a section label. A section owns the items that follow it until the
    // next section; the label is painted above them and carries the divider that
    // separates the groups.
    void addSection(const QString& title);

    // Appends a navigation row and returns nothing; item indices are dense and
    // independent of the sections. The icon and the badge are optional.
    void addItem(const QString& label, const QIcon& icon = QIcon(),
                 const QString& badge = QString());

    [[nodiscard]] int itemCount() const;
    [[nodiscard]] QString itemLabel(int index) const;
    [[nodiscard]] QString itemBadge(int index) const;

    // Removes every section and every item and clears the selection.
    void clearItems();
    // A custom scrollable body, e.g. a NavigationView. It replaces the painted
    // rows (the header and the panel stay) and is arranged in listRect().
    void setContent(View* content);
    [[nodiscard]] View* content() const noexcept { return content_; }

    // -1 means "nothing selected". An out-of-range index clears the selection.
    void setSelectedIndex(int index);
    [[nodiscard]] int selectedIndex() const noexcept;

    // Whether a press on the scrim dismisses a modal drawer.
    void setScrimDismissible(bool dismissible);
    [[nodiscard]] bool isScrimDismissible() const noexcept;

    // Modal: open() covers the window and slides the panel in; close() slides it
    // out and hides it when the transition ends. Standard: both only toggle the
    // visibility, and they complete synchronously.
    void open();
    void close();
    [[nodiscard]] bool isOpen() const noexcept;

    // 0 — fully hidden, 1 — fully open. Standard drawers are always 0 or 1.
    [[nodiscard]] qreal animationProgress() const noexcept;
    [[nodiscard]] bool isAnimating() const;

    // Ends a running transition at its final state, including the hide() and the
    // closed() signal of a closing drawer. Tests use it to assert a settled
    // layout without waiting for the timer.
    void finishAnimation();

    // Geometry in widget coordinates. A modal drawer covers the window, so the
    // scrim is the whole widget; a standard drawer has no scrim. Panel edges are
    // clamped to the window width.
    [[nodiscard]] QRectF panelRect() const;
    [[nodiscard]] QRectF scrimRect() const;
    [[nodiscard]] QRectF headerRect() const;

    // The hit target of one item, including its scroll position and the slide
    // offset of a running transition.
    [[nodiscard]] QRectF itemRect(int index) const;

    // The list scroll position, clamped to [0, contentHeight - listHeight].
    [[nodiscard]] qreal scrollOffset() const noexcept;
    void setScrollOffset(qreal offset);

signals:
    // A press, or Return/Enter/Space, on `index`. A modal drawer closes after
    // the signal has been emitted.
    void itemActivated(int index, QString label);
    void selectionChanged(int index);
    void opened();
    void closed();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeChildren(const QRectF& content) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

    void onThemeChanged() override;
    [[nodiscard]] bool focusable() const override;

private:
    struct Entry
    {
        enum class Kind {
            Section,
            Item,
        };

        Kind kind = Kind::Item;
        QString text;
        QIcon icon;
        QString badge;
    };

    [[nodiscard]] bool isModal() const noexcept;
    [[nodiscard]] qreal headerHeight() const;
    [[nodiscard]] QRectF listRect() const;
    [[nodiscard]] qreal entriesHeight() const;
    [[nodiscard]] qreal maxScrollOffset() const;
    [[nodiscard]] qreal entryTop(int entry) const;
    [[nodiscard]] QRectF entryRect(int entry) const;
    [[nodiscard]] int entryForItem(int itemIndex) const;
    [[nodiscard]] int entryAtPosition(const QPointF& position) const;
    [[nodiscard]] int itemAtPosition(const QPointF& position) const;

    void ensureItemVisible(int index);
    void activateItem(int index);
    void applySelection(int index, bool notify);

    void setProgress(qreal progress);
    void animateTo(qreal target);
    void finishTransition();
    [[nodiscard]] int transitionDuration() const;
    [[nodiscard]] qreal easedProgress(qreal linear) const;

    void applyOverlayGeometry();
    void installFilters();
    void removeFilters();
    void syncLayout();
    void paintRows(QPainter& painter) const;

    QList<Entry> entries_;
    View* content_ = nullptr;
    QString headerTitle_;
    QString headerSubtitle_;

    Edge edge_ = Edge::Start;
    Mode mode_ = Mode::Modal;
    qreal widthDp_ = 360.0;

    int selected_ = -1;
    int hovered_ = -1;
    int pressedItem_ = -1;
    qreal scrollOffset_ = 0.0;

    bool open_ = false;
    bool closing_ = false;
    bool scrimDismissible_ = true;
    bool pressedScrim_ = false;
    bool filtersInstalled_ = false;

    qreal progress_ = 0.0;
    qreal startProgress_ = 0.0;
    qreal targetProgress_ = 0.0;
    int elapsedMs_ = 0;

    QTimer* timer_ = nullptr;
};

} // namespace md
