// Periodicity — logging category definitions (framework.md §5.1).
//
// The category names are part of the public contract: users filter on them
// through QLoggingCategory rules, so they must not change between releases.

#include <md/core/logging.h>

namespace md {

Q_LOGGING_CATEGORY(mdCore, "md.core")
Q_LOGGING_CATEGORY(mdPaint, "md.paint")
Q_LOGGING_CATEGORY(mdLayout, "md.layout")
Q_LOGGING_CATEGORY(mdTheme, "md.theme")

} // namespace md
