// Periodicity — the process-wide layer debug switch.
//
// The retained-layer path is only trustworthy if it can be compared with the
// path it replaced. This header is that comparison: with the switch on, every
// View paints directly into its widget exactly as it did before layers existed,
// and no layer is acquired anywhere in the process. A test (or a developer
// chasing a ghost on screen) renders the same view in both modes and compares
// the frames pixel for pixel; a difference is a missed invalidation, not a
// rendering preference.
//
// The switch is a test flag, not a hidden environment variable: it is process
// wide, it applies to every View, and it is off by default. It takes effect on
// the next paint of each view; a layer-backed view releases its surface while
// it is in this mode, so `liveLayerCount()` reaches zero once every view has
// painted (or has been released) once.
//
// Threading. UI thread only, like the widgets it affects.

#pragma once

#include <QtGlobal>

namespace md::view {

// Turns the uncached debug mode on or off for the whole process. While it is
// on, a layer-backed View paints directly and holds no layer.
void setLayerDebugUncached(bool uncached);

// True while the uncached debug mode is on.
[[nodiscard]] bool isLayerDebugUncached();

// The number of layer surfaces the process-wide cache currently holds. It is a
// diagnostic for tests and benchmarks: zero means the uncached mode (or a
// process that never opted in) really has no layer allocated.
[[nodiscard]] int liveLayerCount();

} // namespace md::view
