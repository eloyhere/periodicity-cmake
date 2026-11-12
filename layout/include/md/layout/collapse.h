// Periodicity — Collapse (framework.md §6.3, docs/backlog.md batch 4).
//
// A single expandable section: a header row with a title and a chevron, and one
// content view below it. The section's height is a function of an animated
// 0..1 expansion value, so the motion is a number the widget can be queried
// for rather than a QPropertyAnimation hidden inside paint code.
//
// expansion() drives both the section's own preferred height and the content
// rectangle, which means a parent layout that re-measures on updateGeometry()
// animates for free. A standalone section (no parent) resizes itself, so it is
// usable outside a container as well.
//
// The animation uses the theme's medium1 motion token, so reduced motion (a
// zero-duration spec) snaps instead of animating.

#pragma once

#include <md/view/view_group.h>

#include <QString>
#include <QVariantAnimation>
#include <Qt>

class QPainter;
class QRectF;

namespace md {

class Collapse : public ViewGroup
{
    Q_OBJECT

public:
    explicit Collapse(QWidget* parent = nullptr);
    ~Collapse() override;

    void setTitle(QString title);
    [[nodiscard]] QString title() const { return title_; }

    void setHeaderHeight(qreal height);
    [[nodiscard]] qreal headerHeight() const noexcept { return headerHeight_; }

    // The single collapsible child. The section reparents it and, when it is
    // replaced, hands the old one back without deleting it.
    void setContentWidget(View* content);
    [[nodiscard]] View* contentWidget() const noexcept { return content_; }

    void setExpanded(bool expanded);
    [[nodiscard]] bool isExpanded() const noexcept { return expanded_; }
    void toggle();

    // The animated progress: 0 fully collapsed, 1 fully expanded. Equals the
    // target as soon as the animation finishes.
    [[nodiscard]] qreal expansion() const noexcept { return expansion_; }

    // The height the section wants at its current expansion.
    [[nodiscard]] qreal preferredHeight() const;

    // The header row, in the section's own coordinates. Empty until the first
    // arrangement.
    [[nodiscard]] QRectF headerRect() const { return headerRect_; }

    // When disabled, setExpanded() snaps instead of animating.
    void setAnimated(bool animated) { animated_ = animated; }
    [[nodiscard]] bool isAnimated() const noexcept { return animated_; }

signals:
    // Emitted as soon as the target state changes, before the animation runs.
    void expandedChanged(bool expanded);

    // Emitted once the height animation settled (immediately for a snap).
    void animationFinished();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeChildren(const QRectF& content) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void onHidden() override;

private:
    void applyExpansion();
    void syncContentVisibility();
    void relayoutNow();
    [[nodiscard]] qreal contentNaturalHeight(qreal width);
    [[nodiscard]] qreal headerNaturalWidth() const;

    QString title_;
    View* content_ = nullptr;
    qreal headerHeight_ = 48.0;
    qreal expansion_ = 0.0;
    bool expanded_ = false;
    bool animated_ = true;
    qreal contentHeight_ = 0.0;
    QRectF headerRect_;
    QVariantAnimation animation_;
};

} // namespace md
