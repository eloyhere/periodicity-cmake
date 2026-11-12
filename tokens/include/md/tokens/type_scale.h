// Periodicity — type scale tokens (framework.md §3.2).
//
// Fifteen typographic styles, each a plain value. Sizes are in sp, so the
// user's font scale applies independently of density. The line height is
// explicit and never derived from QFontMetrics, because font designers'
// metrics do not match the ratios of the reference design system.

#pragma once

#include <QString>
#include <QtGlobal>

namespace md {

// One typographic style. Tracking is the design baseline's letter spacing in
// sp; it is a positive value for wide text (labels) and negative for display
// sizes, which is why it is signed.
struct TextStyle
{
    QString family;
    qreal sizeSp = 0.0;
    qreal lineHeightSp = 0.0;
    qreal trackingSp = 0.0;
    int weight = 400;

    [[nodiscard]] friend bool operator==(const TextStyle& lhs, const TextStyle& rhs) = default;
};

// The fifteen type styles of the design baseline. The names follow the
// specification: display / headline / title / body / label, each in large /
// medium / small.
struct TypeScale
{
    TextStyle displayLarge;
    TextStyle displayMedium;
    TextStyle displaySmall;

    TextStyle headlineLarge;
    TextStyle headlineMedium;
    TextStyle headlineSmall;

    TextStyle titleLarge;
    TextStyle titleMedium;
    TextStyle titleSmall;

    TextStyle bodyLarge;
    TextStyle bodyMedium;
    TextStyle bodySmall;

    TextStyle labelLarge;
    TextStyle labelMedium;
    TextStyle labelSmall;

    [[nodiscard]] static TypeScale baseline();

    [[nodiscard]] friend bool operator==(const TypeScale& lhs, const TypeScale& rhs) = default;
};

} // namespace md
