// Periodicity — AppBar (framework.md §6.3, docs/backlog.md batch 4).
//
// A Material 3 top app bar that collapses as its content scrolls. It carries a
// title, an optional navigation button and a set of actions, and it owns the
// two things that make a collapsing bar more than a styled row:
//
//   * a height that interpolates between `expandedHeight` and `collapsedHeight`
//     as `scrollOffset` grows towards `scrollRange`, and
//   * an elevation that is flat at the top and lifts to `scrolledElevation` as
//     soon as the bar is scrolled at all.
//
// The bar does not scroll anything itself. A Coordinator's AppBarBehavior turns
// nested scroll deltas into setScrollOffset() calls, which is why the class is
// usable both standalone and as a coordinator child.

#pragma once

#include <md/layout/coordinator.h>
#include <md/tokens/elevation.h>
#include <md/view/view_group.h>

#include <QList>
#include <QPointer>
#include <QString>
#include <Qt>

class QPainter;
class QRectF;

namespace md {

class AppBarBehavior;

class AppBar : public ViewGroup
{
    Q_OBJECT

public:
    explicit AppBar(QWidget* parent = nullptr);
    ~AppBar() override;

    void setTitle(QString title);
    [[nodiscard]] QString title() const { return title_; }

    // The leading button. It must be a child of the bar; the bar takes it into
    // the child list when it is not one yet. Null clears the role without
    // deleting the view.
    void setNavigationButton(View* button);
    [[nodiscard]] View* navigationButton() const { return navigationButton_; }

    // Adds a trailing action. Every child that is not the navigation button is
    // an action, so a child added with addView() participates too.
    void addAction(View* action);
    [[nodiscard]] QList<View*> actions() const;

    void setActionsAlignment(Qt::Alignment alignment);
    [[nodiscard]] Qt::Alignment actionsAlignment() const noexcept { return actionsAlignment_; }

    // The height of the bar at rest (default 112) and at full collapse
    // (default 64).
    void setExpandedHeight(qreal height);
    [[nodiscard]] qreal expandedHeight() const noexcept { return expandedHeight_; }
    void setCollapsedHeight(qreal height);
    [[nodiscard]] qreal collapsedHeight() const noexcept { return collapsedHeight_; }

    // How far the bar may collapse, in pixels. A range of zero disables the
    // collapse entirely, which is what a static bar wants.
    void setScrollRange(qreal range);
    [[nodiscard]] qreal scrollRange() const noexcept { return scrollRange_; }

    // The current scroll distance, clamped to [0, scrollRange].
    void setScrollOffset(qreal offset);
    [[nodiscard]] qreal scrollOffset() const noexcept { return scrollOffset_; }

    // 0 while expanded, 1 at full collapse.
    [[nodiscard]] qreal collapseFraction() const noexcept;

    // The height the bar wants right now: between the expanded and the
    // collapsed height, depending on the collapse fraction.
    [[nodiscard]] qreal currentHeight() const noexcept;

    void setScrolledElevation(Elevation level);
    [[nodiscard]] Elevation scrolledElevation() const noexcept { return scrolledElevation_; }

    // The rectangle the title occupies, in the bar's own coordinates. Empty
    // until the first arrangement.
    [[nodiscard]] QRectF titleRect() const { return titleRect_; }

    // Attaches an AppBarBehavior to `coordinator` and returns it. Returns null
    // when the coordinator does not accept the bar or already has a behaviour
    // for it that is not an AppBarBehavior.
    AppBarBehavior* attachTo(Coordinator* coordinator);

signals:
    void offsetChanged(qreal offset);
    void collapsedChanged(bool collapsed);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeChildren(const QRectF& content) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void changeEvent(QEvent* event) override;

private:
    void syncElevation();
    void relayoutNow();
    [[nodiscard]] qreal barHeight() const noexcept;

    QString title_;
    View* navigationButton_ = nullptr;
    qreal expandedHeight_ = 112.0;
    qreal collapsedHeight_ = 64.0;
    qreal scrollRange_ = 0.0;
    qreal scrollOffset_ = 0.0;
    bool collapsed_ = false;
    Elevation scrolledElevation_ = Elevation::Level2;
    Qt::Alignment actionsAlignment_ = Qt::AlignRight;
    QRectF titleRect_;
};

// The standard coordinator behaviour for a collapsing top app bar: the
// pre-scroll phase collapses the bar before the content moves, and an
// overscroll at the top (a negative unconsumed delta) expands it again.
class AppBarBehavior : public CoordinatorBehaviour
{
public:
    explicit AppBarBehavior(AppBar* appBar);

    bool onStartNestedScroll(Qt::Orientation axis) override;
    void onNestedPreScroll(const QPointF& delta, QPointF* consumed) override;
    void onNestedScroll(const QPointF& consumed, const QPointF& unconsumed) override;
    void onStopNestedScroll() override;

    // Keeps the bar at the top of the coordinator, at the height its collapse
    // fraction asks for, instead of letting the coordinator stretch it.
    void onLayoutChild(View* child, const QRectF& content, QRectF* geometry) override;

    [[nodiscard]] AppBar* appBar() const noexcept { return appBar_.data(); }

private:
    QPointer<AppBar> appBar_;
};

} // namespace md
