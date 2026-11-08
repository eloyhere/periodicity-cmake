// Periodicity — logging categories (framework.md §5.1).
//
// Every layer logs through one of these categories, so an application can
// switch diagnostics on per subsystem with QLoggingCategory rules such as
//   QLoggingCategory::setFilterRules(QStringLiteral("md.paint.debug=true"));
// The names are stable API: they appear in user-facing filter rules.

#pragma once

#include <QLoggingCategory>

namespace md {

// Units, geometry, state and the core value types.
Q_DECLARE_LOGGING_CATEGORY(mdCore)

// Painting: drawables, layers and the paint pipeline.
Q_DECLARE_LOGGING_CATEGORY(mdPaint)

// Measurement, arrangement and layout adapters.
Q_DECLARE_LOGGING_CATEGORY(mdLayout)

// Theme resolution and token lookup.
Q_DECLARE_LOGGING_CATEGORY(mdTheme)

} // namespace md

// The categories live in namespace md, but Qt code conventionally names them
// unqualified. These aliases let `qCDebug(mdTheme)` work at any scope without
// giving up the namespaced spelling `md::mdTheme`.
using md::mdCore;
using md::mdLayout;
using md::mdPaint;
using md::mdTheme;
