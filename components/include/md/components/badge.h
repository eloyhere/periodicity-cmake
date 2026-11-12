// Periodicity — Badge (framework.md §6.3, docs/backlog.md batch 7a).
//
// A compact count/status marker with three shapes: a dot, a number, or a short
// text. It has two ways of living:
//
//   * standalone — an ordinary View the caller resizes and places, and
//   * attached to a target widget — the badge parents itself next to the target
//     and follows its move and resize events through an event filter, sitting
//     over the target's top-trailing corner.
//
// The attached form is the one a list row wants: BadgeView::setTarget() and no
// layout arithmetic in the application. A count of zero hides the badge, which
// is the Material rule for "nothing unread".

#pragma once

#include <md/view/view.h>

#include <QColor>
#include <QPointer>
#include <QString>
#include <Qt>

class QPainter;
class QRectF;
class QWidget;

namespace md {

class Badge : public View
{
    Q_OBJECT

public:
    explicit Badge(QWidget* parent = nullptr);
    ~Badge() override;

    // Explicit text. It replaces the number until setCount() is called again.
    void setText(QString text);
    [[nodiscard]] QString text() const { return text_; }

    // The number the badge shows. Values above maxCount() render as "99+", and
    // zero clears the badge.
    void setCount(int count);
    [[nodiscard]] int count() const noexcept { return count_; }

    void setMaxCount(int max);
    [[nodiscard]] int maxCount() const noexcept { return maxCount_; }

    // A dot: one circle, no content. Setting a number or a text leaves dot mode.
    void setDot(bool dot);
    [[nodiscard]] bool isDot() const noexcept { return dot_; }

    // The fill. The default is the theme's error role, i.e. red.
    void setColour(QColor colour);
    [[nodiscard]] QColor colour() const { return colour_; }

    // Attaches the badge to the top-trailing corner of `target` and keeps it
    // there while the target moves or resizes. The badge becomes a sibling of
    // the target so it can overhang the corner; a target without a parent hosts
    // the badge as a child instead. A null target detaches it and leaves the
    // badge an ordinary widget at its current position.
    void setTarget(QWidget* target);
    [[nodiscard]] QWidget* target() const;

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool focusable() const override { return false; }

private:
    // Re-measures, resizes and repositions an attached badge. A detached badge
    // only invalidates its geometry.
    void refreshGeometry();
    void positionOverTarget();

    QString text_;
    int count_ = 0;
    int maxCount_ = 99;
    bool dot_ = false;
    // True when the zero-count rule hid the badge, so a later count can bring
    // it back without the owner having to show it again.
    bool hiddenByCount_ = false;
    QColor colour_;
    QPointer<QWidget> target_;
};

} // namespace md
