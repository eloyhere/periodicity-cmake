// Periodicity — SurfaceContext resolution (framework.md §5.5, §7.4).
//
// The two derivations live out of line so that surface_context.h stays a
// cheap value type: a layer includes it only for the fields it reads, while the
// fallback rules are implemented once, here, and can never drift between the
// background and the foreground stages.

#include <md/layer/surface_context.h>

namespace md::layer {

QColor SurfaceContext::resolvedContainerColour() const
{
    // An explicit colour — including a fully transparent one — is a caller
    // decision and is never overridden. Tonal elevation is a neutral-ramp
    // lookup, not a white overlay (framework.md §7.4), which is why the
    // fallback goes through the colour scheme.
    if (containerColour.isValid())
        return containerColour;

    return theme.colours().surfaceAtElevation(elevation);
}

QColor SurfaceContext::resolvedContentColour() const
{
    if (contentColour.isValid())
        return contentColour;

    return theme.colours().onSurface;
}

} // namespace md::layer
