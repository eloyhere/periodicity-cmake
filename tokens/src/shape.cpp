// Periodicity — shape tokens (framework.md §3.3).
//
// The outline lives here rather than in the header so that including
// <md/tokens/shape.h> stays cheap; the factory itself is a one-liner because
// CornerRadii owns the rounded-rectangle geometry.

#include <md/tokens/shape.h>

namespace md {

QPainterPath Shape::path(const QRectF& bounds) const
{
    // A pill resolves against the bounds; every other shape passes its stored
    // corners through. CornerRadii clamps oversized radii, so the token value
    // never has to know how large the surface will be.
    return resolved(bounds).toPath(bounds);
}

} // namespace md
