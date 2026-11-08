// Periodicity — state-layer tokens (framework.md §3.6).
//
// The opacity table is constexpr in the header; this translation unit owns the
// compositing step, which has to touch QColor.

#include <md/tokens/state_layer.h>

namespace md {

QColor applyStateLayer(const QColor& content, ViewState state)
{
    QColor result = content;

    // QColor stores components as float in Qt 6, so the multiplication is made
    // explicitly in double and rounded back once, at the setAlphaF boundary.
    const qreal opacity = stateLayerOpacity(state);
    result.setAlphaF(static_cast<float>(content.alphaF() * opacity));
    return result;
}

} // namespace md
