// Periodicity — core module (L0).
//
// Anchor translation unit: the target needs at least one source file.
// Implementations live in the other src/*.cpp files.

#include <md/core/density.h>
#include <md/core/geometry.h>
#include <md/core/measure_spec.h>
#include <md/core/view_state.h>

// The core value types are header-only and constexpr-friendly. The
// translation unit exists so that the static library is never empty; the
// static_asserts below also guarantee that a header change cannot silently
// drop its constexpr support.
namespace {

static_assert(md::MeasureSpec::exactly(12.0).resolve(4.0) == 12.0);
static_assert(md::MeasureSpec::atMost(12.0).resolve(4.0) == 4.0);
static_assert(md::MeasureSpec::unspecified().resolve(4.0) == 4.0);
static_assert(md::Density{}.dp(1.0) == 1.0);
static_assert(md::CornerRadii::uniform(4.0).topStart == 4.0);

} // namespace
