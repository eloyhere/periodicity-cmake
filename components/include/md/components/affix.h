// Periodicity — the Affix component (framework.md §5.9).
//
// Affix is the sticky-bar container: it holds exactly one child View and
// translates that child by a scroll-derived offset. While the page has not
// scrolled past the threshold the child moves with the content — offset() is
// the (negative) translation actually applied. Once
// `scrollOffset() - targetOffset()` exceeds threshold() the child stops moving
// at the position it had at the boundary and the bar reads as stuck: the tonal
// background and the bottom shadow appear and stuckChanged(true) is emitted.
//
// The model is deliberately continuous: offset() is -(local scroll) before the
// boundary and -threshold() after it, so the two branches agree exactly at the
// boundary and a caller never sees a jump. `targetOffset` lets a caller measure
// the scroll in document coordinates while the component reasons in its own
// local ones, which is what the tests use to move the boundary around.

#pragma once

#include <md/view/view_group.h>

#include <QRectF>

class QPainter;

namespace md {

class Affix : public ViewGroup
{
    Q_OBJECT

public:
    explicit Affix(QWidget* parent = nullptr);

    // Replaces the single child. Passing the current child is a no-op; passing
    // null detaches it and leaves the bar empty. The previous child is
    // detached, not deleted.
    void setContent(View* content);
    [[nodiscard]] View* content() const noexcept { return content_; }

    [[nodiscard]] qreal scrollOffset() const noexcept { return scrollOffset_; }

    // The page's scroll position. The setter re-evaluates the stuck state,
    // re-arranges the child and repaints; it never modifies the offset itself.
    void setScrollOffset(qreal offset);

    [[nodiscard]] qreal threshold() const noexcept { return threshold_; }

    // How far past `targetOffset` the page must scroll before the bar sticks.
    // Negative values are clamped to zero.
    void setThreshold(qreal threshold);

    [[nodiscard]] qreal targetOffset() const noexcept { return targetOffset_; }

    // The document position at which the bar's own content starts. It shifts
    // the point where sticking begins without changing the layout.
    void setTargetOffset(qreal offset);

    [[nodiscard]] bool isStuck() const noexcept { return stuck_; }

    // The translation currently applied to the child, in pixels. Zero when the
    // bar has not scrolled at all, -(local scroll) while it is loose and
    // -threshold() while it is stuck.
    [[nodiscard]] qreal offset() const;

    // targetOffset() + threshold(): the local scroll position at which the bar
    // sticks. Exposed because it is the number the offset() branches meet at.
    [[nodiscard]] qreal stickPoint() const;

    // Where the child currently sits, in widget coordinates.
    [[nodiscard]] QRectF contentViewRect() const;

signals:
    void stuckChanged(bool stuck);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeChildren(const QRectF& content) override;
    void paintChildren(QPainter& painter) override;

private:
    void syncStuck();
    void applyStuckAppearance();
    [[nodiscard]] bool computedStuck() const;

    View* content_ = nullptr;
    qreal scrollOffset_ = 0.0;
    qreal threshold_ = 0.0;
    qreal targetOffset_ = 0.0;
    bool stuck_ = false;
};

} // namespace md
