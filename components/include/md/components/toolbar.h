// Periodicity — ToolBar (framework.md §6.3, docs/backlog.md batch 7a).
//
// The design baseline's small top app bar: a navigation icon at the leading edge, a
// title next to it, and an action run. Unlike the collapsing AppBar this bar has
// a fixed height; what changes with a scroll is its elevation, which is what
// "scrolled under content" means in the design baseline.
//
// Children are ordinary View children, so a navigation icon and the actions are
// measured and arranged by this container but paint, receive input and expose
// accessibility through their own contracts.

#pragma once

#include <md/view/view_group.h>

#include <QList>
#include <QString>
#include <Qt>

class QPainter;
class QRectF;

namespace md {

class ToolBar : public ViewGroup
{
    Q_OBJECT

public:
    explicit ToolBar(QWidget* parent = nullptr);
    ~ToolBar() override;

    void setTitle(QString title);
    [[nodiscard]] QString title() const { return title_; }

    // The leading icon button. It becomes a child of the bar if it is not one
    // already; null clears the role without deleting the view.
    void setNavigationIcon(View* icon);
    [[nodiscard]] View* navigationIcon() const { return navigationIcon_; }

    // Appends an action to the action run. Every child that is not the
    // navigation icon counts as an action.
    void addAction(View* action);
    [[nodiscard]] QList<View*> actions() const;

    // Where the action run sits: AlignRight (default), AlignLeft or
    // AlignHCenter. Logical, so it mirrors in a right-to-left bar.
    void setActionsAlignment(Qt::Alignment alignment);
    [[nodiscard]] Qt::Alignment actionsAlignment() const noexcept { return actionsAlignment_; }

    // Lifts the bar to elevation level 2 without waiting for a scroll.
    void setElevated(bool elevated);
    [[nodiscard]] bool isElevated() const noexcept { return elevated_; }

    // How far the page beneath the bar has scrolled, 0..1. Any non-zero value
    // lifts the bar; the exact value is kept so a caller can drive a tonal
    // animation later.
    void setScrollFraction(qreal fraction);
    [[nodiscard]] qreal scrollFraction() const noexcept { return scrollFraction_; }

    void setBarHeight(qreal height);
    [[nodiscard]] qreal barHeight() const noexcept { return barHeight_; }

    // The rectangle the title occupies, in the bar's own coordinates.
    [[nodiscard]] QRectF titleRect() const { return titleRect_; }

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeChildren(const QRectF& content) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void changeEvent(QEvent* event) override;

private:
    void syncElevation();
    void relayoutNow();

    QString title_;
    View* navigationIcon_ = nullptr;
    Qt::Alignment actionsAlignment_ = Qt::AlignRight;
    bool elevated_ = false;
    qreal scrollFraction_ = 0.0;
    qreal barHeight_ = 64.0;
    QRectF titleRect_;
};

} // namespace md
