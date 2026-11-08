// Periodicity — motion tokens (framework.md §3.5).
//
// Only the conversion to Qt's easing type lives here: the token tables are
// constexpr in the header so that a caller pays nothing for reading them.

#include <md/tokens/motion.h>

#include <QPointF>

namespace md {

QEasingCurve CubicBezier::toEasingCurve() const
{
    // An empty QEasingCurve spline starts at (0, 0); one cubic segment ending
    // at (1, 1) therefore describes exactly this token. Qt 6 has no one-shot
    // setCubicBezier(), so the segment is added explicitly.
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment(QPointF(x1, y1), QPointF(x2, y2), QPointF(1.0, 1.0));
    return curve;
}

} // namespace md
