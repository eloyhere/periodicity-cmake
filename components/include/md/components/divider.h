// Periodicity — the Divider component (framework.md §5.9, §7).
//
// A Divider is a single hairline in the theme's outlineVariant: the thin rule
// that separates two blocks of content in a list, a menu or a form. It is pure
// decoration. It owns no interaction, never takes focus and paints nothing
// except the rule, so placing one between two rows cannot change the hit
// testing or the keyboard order of its neighbours.
//
// Measurement follows the design baseline axis contract:
//
//   * horizontal — the natural height is the line thickness, and the width has
//     no intrinsic value: the divider adopts the bound its parent offers (an
//     Exactly spec forces it and an AtMost bound is taken as the length to
//     fill, which is what "full-bleed" means). Only a completely unspecified
//     spec leaves the length at zero;
//   * vertical   — the axes swap. The natural width is the thickness, and the
//     height adopts the offered bound.
//
// The thin axis is the only part that appears in sizeHint(): a standalone
// horizontal divider reports a height of one dp and a zero natural length,
// because a rule has no preferred width of its own. QSizePolicy is Expanding
// on the fill axis so a plain Qt layout stretches it the same way.
//
// setInset() is the design baseline "inset divider": it shortens the painted line at the
// leading and trailing ends without changing the measured rectangle, so the
// space the divider reserves stays stable while the rule itself is indented.
// The insets are logical: on a horizontal divider the leading end is the right
// edge in a right-to-left layout, while a vertical divider always insets from
// the top and the bottom.
//
// The rule is snapped to whole device pixels and painted with antialiasing
// off. A 1 dp divider is therefore exactly one opaque row at a 1x ratio and the
// nearest whole number of device pixels at a fractional one, never a soft grey
// smear; thickness overrides follow the same rule. A thickness of zero or less
// paints nothing.

#pragma once

#include <md/view/view.h>

class QPainter;
class QRectF;

namespace md {

class Divider : public View
{
    Q_OBJECT

public:
    // The axis the rule runs along. Horizontal rules fill the width; vertical
    // rules fill the height.
    enum class Orientation {
        Horizontal,
        Vertical,
    };

    explicit Divider(QWidget* parent = nullptr);
    explicit Divider(Orientation orientation, QWidget* parent = nullptr);

    void setOrientation(Orientation orientation);
    [[nodiscard]] Orientation orientation() const noexcept;

    // Leading/trailing inset along the line, in dp (design baseline "inset divider").
    void setInset(qreal startDp, qreal endDp);
    [[nodiscard]] qreal insetStart() const noexcept;
    [[nodiscard]] qreal insetEnd() const noexcept;

    void setThickness(qreal dp);          // default 1.0
    [[nodiscard]] qreal thickness() const noexcept;

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    [[nodiscard]] bool focusable() const override;

private:
    // Keeps the accessibility name and the QSizePolicy in step with the axis.
    void applyOrientationTraits();

    Orientation orientation_ = Orientation::Horizontal;
    qreal startInset_ = 0.0;
    qreal endInset_ = 0.0;
    qreal thickness_ = 1.0;
};

} // namespace md
