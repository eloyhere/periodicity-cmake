// Periodicity — type scale tokens (framework.md §3.2).
//
// The baseline table is written once, here, so a revision of the specification
// touches a single translation unit. The values were verified against
// TypeScaleTokens.kt; every style uses Roboto in the baseline.

#include <md/tokens/type_scale.h>

namespace md {

namespace {

// Roboto is the only family in the baseline. A theme that bundles a different
// family replaces the whole table rather than patching individual styles.
TextStyle roboto(qreal sizeSp, qreal lineHeightSp, qreal trackingSp, int weight)
{
    return TextStyle{QStringLiteral("Roboto"), sizeSp, lineHeightSp, trackingSp, weight};
}

} // namespace

TypeScale TypeScale::baseline()
{
    TypeScale scale;

    scale.displayLarge = roboto(57.0, 64.0, -0.2, 400);
    scale.displayMedium = roboto(45.0, 52.0, 0.0, 400);
    scale.displaySmall = roboto(36.0, 44.0, 0.0, 400);

    scale.headlineLarge = roboto(32.0, 40.0, 0.0, 400);
    scale.headlineMedium = roboto(28.0, 36.0, 0.0, 400);
    scale.headlineSmall = roboto(24.0, 32.0, 0.0, 400);

    scale.titleLarge = roboto(22.0, 28.0, 0.0, 400);
    scale.titleMedium = roboto(16.0, 24.0, 0.2, 500);
    scale.titleSmall = roboto(14.0, 20.0, 0.1, 500);

    scale.bodyLarge = roboto(16.0, 24.0, 0.5, 400);
    scale.bodyMedium = roboto(14.0, 20.0, 0.2, 400);
    scale.bodySmall = roboto(12.0, 16.0, 0.4, 400);

    scale.labelLarge = roboto(14.0, 20.0, 0.1, 500);
    scale.labelMedium = roboto(12.0, 16.0, 0.5, 500);
    scale.labelSmall = roboto(11.0, 16.0, 0.5, 500);

    return scale;
}

} // namespace md
