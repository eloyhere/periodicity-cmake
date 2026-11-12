// Unit tests for the tokens module (L1): type scale, shape, elevation, motion,
// state layers and the baseline colour schemes. The suite is headless: tokens
// are values, so no window system, widget or painter is involved.
//
// The WCAG contrast helper below is deliberately local to this file: contrast
// is an assertion about the token values, not part of the library API.

#include <QColor>
#include <QEasingCurve>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QTest>

#include <md/core/geometry.h>
#include <md/core/view_state.h>
#include <md/tokens/color_scheme.h>
#include <md/tokens/elevation.h>
#include <md/tokens/motion.h>
#include <md/tokens/shape.h>
#include <md/tokens/state_layer.h>
#include <md/tokens/type_scale.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <iterator>

using md::ColorScheme;
using md::CornerRadii;
using md::CubicBezier;
using md::Duration;
using md::Easing;
using md::Elevation;
using md::MotionScheme;
using md::MotionSpec;
using md::Shape;
using md::ShapeScale;
using md::TextStyle;
using md::TypeScale;
using md::ViewState;

namespace {

// QPainterPath arcs are cubic approximations, so path bounds are compared with
// a small tolerance rather than bit for bit.
constexpr qreal kRectTolerance = 0.01;

bool rectsAlmostEqual(const QRectF& lhs, const QRectF& rhs, qreal tolerance = kRectTolerance)
{
    return qAbs(lhs.left() - rhs.left()) <= tolerance
        && qAbs(lhs.top() - rhs.top()) <= tolerance
        && qAbs(lhs.width() - rhs.width()) <= tolerance
        && qAbs(lhs.height() - rhs.height()) <= tolerance;
}

// WCAG 2.x relative luminance of an sRGB colour.
qreal relativeLuminance(const QColor& colour)
{
    const auto linear = [](int channel) {
        const qreal value = channel / 255.0;
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };

    return 0.2126 * linear(colour.red())
        + 0.7152 * linear(colour.green())
        + 0.0722 * linear(colour.blue());
}

// WCAG contrast ratio. Symmetric in its arguments and always at least 1.
qreal contrastRatio(const QColor& first, const QColor& second)
{
    const qreal firstLuminance = relativeLuminance(first);
    const qreal secondLuminance = relativeLuminance(second);
    const qreal lighter = qMax(firstLuminance, secondLuminance);
    const qreal darker = qMin(firstLuminance, secondLuminance);

    return (lighter + 0.05) / (darker + 0.05);
}

// --- Type scale -------------------------------------------------------------

struct StyleRow
{
    const char* name;
    TextStyle TypeScale::*style;
    qreal sizeSp;
    qreal lineHeightSp;
    qreal trackingSp;
    int weight;
};

const StyleRow kStyles[] = {
    {"displayLarge", &TypeScale::displayLarge, 57.0, 64.0, -0.2, 400},
    {"displayMedium", &TypeScale::displayMedium, 45.0, 52.0, 0.0, 400},
    {"displaySmall", &TypeScale::displaySmall, 36.0, 44.0, 0.0, 400},
    {"headlineLarge", &TypeScale::headlineLarge, 32.0, 40.0, 0.0, 400},
    {"headlineMedium", &TypeScale::headlineMedium, 28.0, 36.0, 0.0, 400},
    {"headlineSmall", &TypeScale::headlineSmall, 24.0, 32.0, 0.0, 400},
    {"titleLarge", &TypeScale::titleLarge, 22.0, 28.0, 0.0, 400},
    {"titleMedium", &TypeScale::titleMedium, 16.0, 24.0, 0.2, 500},
    {"titleSmall", &TypeScale::titleSmall, 14.0, 20.0, 0.1, 500},
    {"bodyLarge", &TypeScale::bodyLarge, 16.0, 24.0, 0.5, 400},
    {"bodyMedium", &TypeScale::bodyMedium, 14.0, 20.0, 0.2, 400},
    {"bodySmall", &TypeScale::bodySmall, 12.0, 16.0, 0.4, 400},
    {"labelLarge", &TypeScale::labelLarge, 14.0, 20.0, 0.1, 500},
    {"labelMedium", &TypeScale::labelMedium, 12.0, 16.0, 0.5, 500},
    {"labelSmall", &TypeScale::labelSmall, 11.0, 16.0, 0.5, 500},
};

// --- Shape ------------------------------------------------------------------

struct ShapeRow
{
    const char* name;
    Shape shape;
    qreal radius;
};

const ShapeRow kShapes[] = {
    {"none", Shape::none(), 0.0},
    {"extraSmall", Shape::extraSmall(), 4.0},
    {"small", Shape::small(), 8.0},
    {"medium", Shape::medium(), 12.0},
    {"large", Shape::large(), 16.0},
    {"extraLarge", Shape::extraLarge(), 28.0},
    {"extraExtraLarge", Shape::extraExtraLarge(), 48.0},
};

// --- Elevation --------------------------------------------------------------

struct ElevationRow
{
    Elevation level;
    qreal dp;
};

const ElevationRow kElevations[] = {
    {Elevation::Level0, 0.0},
    {Elevation::Level1, 1.0},
    {Elevation::Level2, 3.0},
    {Elevation::Level3, 6.0},
    {Elevation::Level4, 8.0},
    {Elevation::Level5, 12.0},
};

// --- Motion -----------------------------------------------------------------

struct DurationRow
{
    Duration duration;
    long long ms;
};

const DurationRow kDurations[] = {
    {Duration::Short1, 50},
    {Duration::Short2, 100},
    {Duration::Short3, 150},
    {Duration::Short4, 200},
    {Duration::Medium1, 250},
    {Duration::Medium2, 300},
    {Duration::Medium3, 350},
    {Duration::Medium4, 400},
    {Duration::Long1, 450},
    {Duration::Long2, 500},
    {Duration::Long3, 550},
    {Duration::Long4, 600},
    {Duration::ExtraLong1, 700},
    {Duration::ExtraLong2, 800},
    {Duration::ExtraLong3, 900},
    {Duration::ExtraLong4, 1000},
};

struct EasingRow
{
    const char* name;
    Easing easing;
    CubicBezier curve;
};

const EasingRow kEasings[] = {
    {"linear", Easing::Linear, CubicBezier{0.0, 0.0, 1.0, 1.0}},
    {"standard", Easing::Standard, CubicBezier{0.2, 0.0, 0.0, 1.0}},
    {"standardDecelerate", Easing::StandardDecelerate, CubicBezier{0.0, 0.0, 0.0, 1.0}},
    {"standardAccelerate", Easing::StandardAccelerate, CubicBezier{0.3, 0.0, 1.0, 1.0}},
    {"emphasized", Easing::Emphasized, CubicBezier{0.2, 0.0, 0.0, 1.0}},
    {"emphasizedDecelerate", Easing::EmphasizedDecelerate, CubicBezier{0.05, 0.7, 0.1, 1.0}},
    {"emphasizedAccelerate", Easing::EmphasizedAccelerate, CubicBezier{0.3, 0.0, 0.8, 0.15}},
    {"legacy", Easing::Legacy, CubicBezier{0.4, 0.0, 0.2, 1.0}},
    {"legacyDecelerate", Easing::LegacyDecelerate, CubicBezier{0.0, 0.0, 0.2, 1.0}},
    {"legacyAccelerate", Easing::LegacyAccelerate, CubicBezier{0.4, 0.0, 1.0, 1.0}},
};

// --- Colour -----------------------------------------------------------------

struct RoleRow
{
    const char* name;
    QColor ColorScheme::*member;
};

// Every role in the scheme: the table exists so that "all colours opaque" is
// checked exhaustively rather than on a sample.
const RoleRow kRoles[] = {
    {"primary", &ColorScheme::primary},
    {"onPrimary", &ColorScheme::onPrimary},
    {"primaryContainer", &ColorScheme::primaryContainer},
    {"onPrimaryContainer", &ColorScheme::onPrimaryContainer},
    {"secondary", &ColorScheme::secondary},
    {"onSecondary", &ColorScheme::onSecondary},
    {"secondaryContainer", &ColorScheme::secondaryContainer},
    {"onSecondaryContainer", &ColorScheme::onSecondaryContainer},
    {"tertiary", &ColorScheme::tertiary},
    {"onTertiary", &ColorScheme::onTertiary},
    {"tertiaryContainer", &ColorScheme::tertiaryContainer},
    {"onTertiaryContainer", &ColorScheme::onTertiaryContainer},
    {"error", &ColorScheme::error},
    {"onError", &ColorScheme::onError},
    {"errorContainer", &ColorScheme::errorContainer},
    {"onErrorContainer", &ColorScheme::onErrorContainer},
    {"surface", &ColorScheme::surface},
    {"onSurface", &ColorScheme::onSurface},
    {"surfaceVariant", &ColorScheme::surfaceVariant},
    {"onSurfaceVariant", &ColorScheme::onSurfaceVariant},
    {"surfaceDim", &ColorScheme::surfaceDim},
    {"surfaceBright", &ColorScheme::surfaceBright},
    {"surfaceContainerLowest", &ColorScheme::surfaceContainerLowest},
    {"surfaceContainerLow", &ColorScheme::surfaceContainerLow},
    {"surfaceContainer", &ColorScheme::surfaceContainer},
    {"surfaceContainerHigh", &ColorScheme::surfaceContainerHigh},
    {"surfaceContainerHighest", &ColorScheme::surfaceContainerHighest},
    {"outline", &ColorScheme::outline},
    {"outlineVariant", &ColorScheme::outlineVariant},
    {"shadow", &ColorScheme::shadow},
    {"scrim", &ColorScheme::scrim},
    {"surfaceTint", &ColorScheme::surfaceTint},
    {"inverseSurface", &ColorScheme::inverseSurface},
    {"inverseOnSurface", &ColorScheme::inverseOnSurface},
    {"inversePrimary", &ColorScheme::inversePrimary},
    {"primaryFixed", &ColorScheme::primaryFixed},
    {"primaryFixedDim", &ColorScheme::primaryFixedDim},
    {"onPrimaryFixed", &ColorScheme::onPrimaryFixed},
    {"onPrimaryFixedVariant", &ColorScheme::onPrimaryFixedVariant},
    {"secondaryFixed", &ColorScheme::secondaryFixed},
    {"secondaryFixedDim", &ColorScheme::secondaryFixedDim},
    {"onSecondaryFixed", &ColorScheme::onSecondaryFixed},
    {"onSecondaryFixedVariant", &ColorScheme::onSecondaryFixedVariant},
    {"tertiaryFixed", &ColorScheme::tertiaryFixed},
    {"tertiaryFixedDim", &ColorScheme::tertiaryFixedDim},
    {"onTertiaryFixed", &ColorScheme::onTertiaryFixed},
    {"onTertiaryFixedVariant", &ColorScheme::onTertiaryFixedVariant},
};

// A text role pair: `onX` content on an `X` background must stay legible.
struct ContrastPair
{
    const char* name;
    QColor ColorScheme::*background;
    QColor ColorScheme::*foreground;
};

const ContrastPair kTextPairs[] = {
    {"primary/onPrimary", &ColorScheme::primary, &ColorScheme::onPrimary},
    {"secondary/onSecondary", &ColorScheme::secondary, &ColorScheme::onSecondary},
    {"tertiary/onTertiary", &ColorScheme::tertiary, &ColorScheme::onTertiary},
    {"error/onError", &ColorScheme::error, &ColorScheme::onError},
    {"surface/onSurface", &ColorScheme::surface, &ColorScheme::onSurface},
    {"surfaceContainerLowest/onSurface", &ColorScheme::surfaceContainerLowest,
     &ColorScheme::onSurface},
    {"surfaceContainerLow/onSurface", &ColorScheme::surfaceContainerLow, &ColorScheme::onSurface},
    {"surfaceContainer/onSurface", &ColorScheme::surfaceContainer, &ColorScheme::onSurface},
    {"surfaceContainerHigh/onSurface", &ColorScheme::surfaceContainerHigh,
     &ColorScheme::onSurface},
    {"surfaceContainerHighest/onSurface", &ColorScheme::surfaceContainerHighest,
     &ColorScheme::onSurface},
};

// Both baseline schemes, labelled so that a failure message says which one
// failed.
struct SchemeRow
{
    const char* name;
    ColorScheme scheme;
};

const SchemeRow kSchemes[] = {
    {"light", ColorScheme::baselineLight()},
    {"dark", ColorScheme::baselineDark()},
};

} // namespace

class TestTokens : public QObject
{
    Q_OBJECT

private slots:
    void typeScaleBaseline();
    void typeScaleEquality();
    void shapeRadii();
    void shapePill();
    void shapePath();
    void shapeScale();
    void elevationLevels();
    void motionDurations();
    void motionEasings();
    void motionEasingCurveConversion();
    void motionReduced();
    void motionConvenience();
    void stateLayerOpacities();
    void stateLayerApplication();
    void colourSchemesDiffer();
    void colourSurfaceAtElevation();
    void colourContrast();
    void colourOpaque();
    void shapeClampsOversizedRadii();
};

void TestTokens::typeScaleBaseline()
{
    const TypeScale scale = TypeScale::baseline();

    // Fifteen rows: a style missing from the table would leave one member
    // unverified, so the count is asserted as well.
    QCOMPARE(int(std::size(kStyles)), 15);

    for (const StyleRow& row : kStyles) {
        const TextStyle& style = scale.*(row.style);
        QVERIFY2(style.family == QStringLiteral("Roboto"), row.name);
        QCOMPARE(style.sizeSp, row.sizeSp);
        QCOMPARE(style.lineHeightSp, row.lineHeightSp);
        QCOMPARE(style.trackingSp, row.trackingSp);
        QCOMPARE(style.weight, row.weight);
    }

    // Line heights are explicit M3 values, never font metrics; spot-check the
    // two extremes so a table transposition cannot slip through.
    QCOMPARE(scale.displayLarge.lineHeightSp, 64.0);
    QCOMPARE(scale.labelSmall.sizeSp, 11.0);
    QCOMPARE(scale.labelSmall.lineHeightSp, 16.0);
}

void TestTokens::typeScaleEquality()
{
    const TypeScale scale = TypeScale::baseline();
    QVERIFY(scale == TypeScale::baseline());

    TypeScale changed = scale;
    changed.bodySmall.weight = 500;
    QVERIFY(scale != changed);
    QVERIFY(changed != TypeScale::baseline());

    const TextStyle style = scale.labelLarge;
    QVERIFY(style == scale.labelLarge);

    TextStyle retracked = style;
    retracked.trackingSp = 0.2;
    QVERIFY(retracked != style);
}

void TestTokens::shapeRadii()
{
    QCOMPARE(int(std::size(kShapes)), 7);

    // A large square bounds so that no radius is clamped.
    const QRectF bounds(0.0, 0.0, 200.0, 200.0);

    for (const ShapeRow& row : kShapes) {
        QVERIFY2(!row.shape.isPill(), row.name);

        const CornerRadii radii = row.shape.resolved(bounds);
        QCOMPARE(radii.topStart, row.radius);
        QCOMPARE(radii.topEnd, row.radius);
        QCOMPARE(radii.bottomEnd, row.radius);
        QCOMPARE(radii.bottomStart, row.radius);
    }

    // The factories are values: equal radii mean equal shapes.
    QVERIFY(Shape::none() == Shape::rounded(CornerRadii{}));
    QVERIFY(Shape::medium() == Shape::medium());
    QVERIFY(Shape::medium() != Shape::large());
    QVERIFY(Shape::none() != Shape::extraSmall());

    // Explicit asymmetric radii survive untouched.
    const CornerRadii asymmetric{4.0, 8.0, 12.0, 16.0};
    const Shape custom = Shape::rounded(asymmetric);
    QVERIFY(custom == Shape::rounded(asymmetric));
    QVERIFY(!custom.isPill());
    QCOMPARE(custom.radii().topStart, 4.0);
    QCOMPARE(custom.radii().topEnd, 8.0);
    QCOMPARE(custom.radii().bottomEnd, 12.0);
    QCOMPARE(custom.radii().bottomStart, 16.0);
    QVERIFY(custom.resolved(bounds) == asymmetric);
}

void TestTokens::shapePill()
{
    const Shape pill = Shape::pill();
    QVERIFY(pill.isPill());
    QVERIFY(pill != Shape::large());

    // The cap is half the shorter side, whichever way round the rectangle is.
    QVERIFY(pill.resolved(QRectF(0.0, 0.0, 120.0, 40.0)) == CornerRadii::uniform(20.0));
    QVERIFY(pill.resolved(QRectF(0.0, 0.0, 40.0, 120.0)) == CornerRadii::uniform(20.0));
    QVERIFY(pill.resolved(QRectF(0.0, 0.0, 32.0, 32.0)) == CornerRadii::uniform(16.0));
    QVERIFY(pill.resolved(QRectF(10.0, 20.0, 64.0, 24.0)) == CornerRadii::uniform(12.0));

    // A degenerate rectangle resolves to no rounding rather than a negative
    // radius.
    QVERIFY(pill.resolved(QRectF(0.0, 0.0, 0.0, 0.0)) == CornerRadii::uniform(0.0));

    // The pill path is exactly the path of the resolved radii.
    const QRectF wide(0.0, 0.0, 120.0, 40.0);
    QVERIFY(pill.path(wide) == CornerRadii::uniform(20.0).toPath(wide));
}

void TestTokens::shapePath()
{
    const QRectF bounds(20.0, 10.0, 160.0, 72.0);

    for (const ShapeRow& row : kShapes) {
        const QPainterPath path = row.shape.path(bounds);
        QVERIFY2(!path.isEmpty(), row.name);
        QVERIFY2(rectsAlmostEqual(path.boundingRect(), bounds), row.name);
        QVERIFY2(path.contains(bounds.center()), row.name);
        QVERIFY2(path == CornerRadii::uniform(row.radius).toPath(bounds), row.name);
    }

    // A rounded corner is actually cut: the top-start pixel is outside a
    // 12 dp shape but inside the plain rectangle.
    const QPainterPath mediumPath = Shape::medium().path(bounds);
    QVERIFY(!mediumPath.contains(QPointF(bounds.left() + 1.0, bounds.top() + 1.0)));
    QVERIFY(Shape::none().path(bounds).contains(QPointF(bounds.left() + 1.0,
                                                        bounds.top() + 1.0)));

    // An empty rectangle has no outline at all.
    QVERIFY(Shape::medium().path(QRectF()).isEmpty());
    QVERIFY(Shape::pill().path(QRectF()).isEmpty());
}

void TestTokens::shapeScale()
{
    const ShapeScale scale = ShapeScale::baseline();

    QVERIFY(scale.none == Shape::none());
    QVERIFY(scale.extraSmall == Shape::extraSmall());
    QVERIFY(scale.small == Shape::small());
    QVERIFY(scale.medium == Shape::medium());
    QVERIFY(scale.large == Shape::large());
    QVERIFY(scale.extraLarge == Shape::extraLarge());
    QVERIFY(scale.full == Shape::pill());

    QVERIFY(scale.full.isPill());
    QVERIFY(!scale.medium.isPill());

    QCOMPARE(scale.medium.radii().topStart, 12.0);
    QCOMPARE(scale.extraLarge.radii().topStart, 28.0);

    QVERIFY(scale == ShapeScale::baseline());

    ShapeScale changed = scale;
    changed.small = Shape::medium();
    QVERIFY(changed != scale);
}

void TestTokens::elevationLevels()
{
    QCOMPARE(int(std::size(kElevations)), 6);

    for (const ElevationRow& row : kElevations)
        QCOMPARE(md::elevationDp(row.level), row.dp);

    // The ramp is strictly increasing, which is what the shadow offset and the
    // tonal surface mapping both rely on.
    for (std::size_t i = 1; i < std::size(kElevations); ++i)
        QVERIFY(kElevations[i].dp > kElevations[i - 1].dp);
}

void TestTokens::motionDurations()
{
    QCOMPARE(int(std::size(kDurations)), 16);

    for (const DurationRow& row : kDurations)
        QCOMPARE(md::durationMs(row.duration).count(), row.ms);

    // Durations grow monotonically across the four families.
    for (std::size_t i = 1; i < std::size(kDurations); ++i)
        QVERIFY(kDurations[i].ms > kDurations[i - 1].ms);
}

void TestTokens::motionEasings()
{
    QCOMPARE(int(std::size(kEasings)), 10);

    for (const EasingRow& row : kEasings)
        QVERIFY2(md::easingCurve(row.easing) == row.curve, row.name);

    // The reference export defines `emphasized` identically to `standard`; the
    // token names are kept distinct so they may diverge later.
    QVERIFY(md::easingCurve(Easing::Emphasized) == md::easingCurve(Easing::Standard));
    QVERIFY((md::easingCurve(Easing::Linear) == CubicBezier{0.0, 0.0, 1.0, 1.0}));
}

void TestTokens::motionEasingCurveConversion()
{
    for (const EasingRow& row : kEasings) {
        const QEasingCurve curve = md::easingCurve(row.easing).toEasingCurve();
        QVERIFY2(curve.type() == QEasingCurve::BezierSpline, row.name);
        QVERIFY2(qAbs(curve.valueForProgress(0.0)) < 0.001, row.name);
        QVERIFY2(qAbs(curve.valueForProgress(1.0) - 1.0) < 0.001, row.name);

        const qreal middle = curve.valueForProgress(0.5);
        QVERIFY2(middle >= 0.0 && middle <= 1.0, row.name);
    }

    // A linear curve is the identity: a half-way input is a half-way output.
    QVERIFY(qAbs(md::easingCurve(Easing::Linear).toEasingCurve().valueForProgress(0.5)
                 - 0.5)
            < 0.01);
}

void TestTokens::motionReduced()
{
    const MotionScheme normal = MotionScheme::baseline();
    QVERIFY(!normal.reducedMotion);

    const MotionSpec standard = normal.spec(Duration::Medium2, Easing::Emphasized);
    QCOMPARE(standard.duration.count(), 300LL);
    QVERIFY(standard.easing == md::easingCurve(Easing::Emphasized));
    QCOMPARE(standard.delay.count(), 0LL);

    // The default easing is Standard.
    QVERIFY(normal.spec(Duration::Short1).easing == md::easingCurve(Easing::Standard));

    MotionScheme reduced = normal;
    reduced.reducedMotion = true;

    // Reduced motion keeps the easing and drops the duration to zero, so the
    // caller snaps to the end state on the next frame.
    const MotionSpec snapped = reduced.spec(Duration::Medium2, Easing::Emphasized);
    QCOMPARE(snapped.duration.count(), 0LL);
    QVERIFY(snapped.easing == md::easingCurve(Easing::Emphasized));
    QVERIFY(snapped.easing == standard.easing);
    QCOMPARE(snapped.delay.count(), 0LL);

    QCOMPARE(reduced.spec(Duration::ExtraLong4).duration.count(), 0LL);

    // The flag is part of the value, so comparison notices it.
    QVERIFY(reduced != normal);
}

void TestTokens::motionConvenience()
{
    const MotionScheme normal = MotionScheme::baseline();

    QCOMPARE(normal.short4().duration.count(), 200LL);
    QCOMPARE(normal.medium1().duration.count(), 250LL);
    QCOMPARE(normal.medium2().duration.count(), 300LL);
    QCOMPARE(normal.long2().duration.count(), 500LL);

    const MotionSpec emphasized = normal.emphasizedMedium2();
    QCOMPARE(emphasized.duration.count(), 300LL);
    QVERIFY(emphasized.easing == md::easingCurve(Easing::Emphasized));

    // Every convenience helper starts immediately.
    QCOMPARE(normal.short4().delay.count(), 0LL);
    QCOMPARE(normal.medium1().delay.count(), 0LL);
    QCOMPARE(normal.medium2().delay.count(), 0LL);
    QCOMPARE(normal.long2().delay.count(), 0LL);
    QCOMPARE(emphasized.delay.count(), 0LL);

    // Reduced motion applies to the helpers too, because they resolve through
    // spec().
    MotionScheme reduced = normal;
    reduced.reducedMotion = true;

    QCOMPARE(reduced.short4().duration.count(), 0LL);
    QCOMPARE(reduced.medium1().duration.count(), 0LL);
    QCOMPARE(reduced.medium2().duration.count(), 0LL);
    QCOMPARE(reduced.long2().duration.count(), 0LL);
    QCOMPARE(reduced.emphasizedMedium2().duration.count(), 0LL);
    QVERIFY(reduced.emphasizedMedium2().easing == md::easingCurve(Easing::Emphasized));
}

void TestTokens::stateLayerOpacities()
{
    QCOMPARE(md::stateLayerOpacity(ViewState::Hovered), 0.08);
    QCOMPARE(md::stateLayerOpacity(ViewState::Focused), 0.12);
    QCOMPARE(md::stateLayerOpacity(ViewState::Pressed), 0.12);
    QCOMPARE(md::stateLayerOpacity(ViewState::Dragged), 0.16);

    // Every state without an interaction overlay resolves to zero.
    QCOMPARE(md::stateLayerOpacity(ViewState::Enabled), 0.0);
    QCOMPARE(md::stateLayerOpacity(ViewState::Selected), 0.0);
    QCOMPARE(md::stateLayerOpacity(ViewState::Checked), 0.0);
    QCOMPARE(md::stateLayerOpacity(ViewState::Invalid), 0.0);
    QCOMPARE(md::stateLayerOpacity(ViewState::ReadOnly), 0.0);
    QCOMPARE(md::stateLayerOpacity(static_cast<ViewState>(0u)), 0.0);

    // Several interaction flags in one value: the strongest overlay wins, and
    // the opacities are never summed.
    const auto combine = [](ViewState first, ViewState second) {
        return static_cast<ViewState>(static_cast<quint32>(first)
                                      | static_cast<quint32>(second));
    };

    QCOMPARE(md::stateLayerOpacity(combine(ViewState::Hovered, ViewState::Dragged)), 0.16);
    QCOMPARE(md::stateLayerOpacity(combine(ViewState::Dragged, ViewState::Pressed)), 0.16);
    QCOMPARE(md::stateLayerOpacity(combine(ViewState::Hovered, ViewState::Focused)), 0.12);
    QCOMPARE(md::stateLayerOpacity(combine(ViewState::Focused, ViewState::Pressed)), 0.12);
    QCOMPARE(md::stateLayerOpacity(combine(ViewState::Hovered, ViewState::Enabled)), 0.08);

    QCOMPARE(md::kDisabledContentOpacity, 0.38);
    QCOMPARE(md::kDisabledContainerOpacity, 0.12);
}

void TestTokens::stateLayerApplication()
{
    const QColor primary(0x67, 0x50, 0xA4);
    QCOMPARE(primary.alpha(), 255);

    // Alpha is multiplied by the overlay opacity and rounded once: 0.08 * 255
    // is 20.4, 0.12 * 255 is 30.6 and 0.16 * 255 is 40.8.
    const QColor hovered = md::applyStateLayer(primary, ViewState::Hovered);
    QCOMPARE(hovered.alpha(), 20);
    QCOMPARE(hovered.red(), 0x67);
    QCOMPARE(hovered.green(), 0x50);
    QCOMPARE(hovered.blue(), 0xA4);

    QCOMPARE(md::applyStateLayer(primary, ViewState::Focused).alpha(), 31);
    QCOMPARE(md::applyStateLayer(primary, ViewState::Pressed).alpha(), 31);
    QCOMPARE(md::applyStateLayer(primary, ViewState::Dragged).alpha(), 41);

    // A state with no overlay yields a fully transparent copy.
    QCOMPARE(md::applyStateLayer(primary, ViewState::Enabled).alpha(), 0);

    // A translucent content colour keeps its own alpha in the product:
    // 128/255 * 0.12 * 255 is 15.36, which rounds to 15.
    const QColor translucent(0x67, 0x50, 0xA4, 128);
    QCOMPARE(md::applyStateLayer(translucent, ViewState::Pressed).alpha(), 15);

    // Combining two interaction flags uses the stronger overlay only.
    const auto combined = static_cast<ViewState>(static_cast<quint32>(ViewState::Hovered)
                                                 | static_cast<quint32>(ViewState::Dragged));
    QCOMPARE(md::applyStateLayer(primary, combined).alpha(), 41);
}

void TestTokens::colourSchemesDiffer()
{
    const ColorScheme light = ColorScheme::baselineLight();
    const ColorScheme dark = ColorScheme::baselineDark();

    QVERIFY(!(light == dark));
    QVERIFY(light == ColorScheme::baselineLight());
    QVERIFY(dark == ColorScheme::baselineDark());

    // The factories are deterministic, and a default-constructed scheme is not
    // a valid baseline.
    QVERIFY(light != ColorScheme{});

    // The same role holds a different colour in the two schemes.
    QVERIFY(light.primary != dark.primary);
    QVERIFY(light.onPrimary != dark.onPrimary);
    QVERIFY(light.surface != dark.surface);
    QVERIFY(light.onSurface != dark.onSurface);
    QVERIFY(light.error != dark.error);

    // Canonical baseline values, spot-checked in both schemes.
    QCOMPARE(light.primary.name(), QStringLiteral("#6750a4"));
    QCOMPARE(light.onPrimary.name(), QStringLiteral("#ffffff"));
    QCOMPARE(light.primaryContainer.name(), QStringLiteral("#eaddff"));
    QCOMPARE(light.surface.name(), QStringLiteral("#fef7ff"));
    QCOMPARE(light.surfaceContainer.name(), QStringLiteral("#f3edf7"));
    QCOMPARE(light.surfaceContainerHighest.name(), QStringLiteral("#e6e0e9"));
    QCOMPARE(light.error.name(), QStringLiteral("#b3261e"));
    QCOMPARE(light.inversePrimary.name(), QStringLiteral("#d0bcff"));

    QCOMPARE(dark.primary.name(), QStringLiteral("#d0bcff"));
    QCOMPARE(dark.onPrimary.name(), QStringLiteral("#381e72"));
    QCOMPARE(dark.primaryContainer.name(), QStringLiteral("#4f378b"));
    QCOMPARE(dark.surface.name(), QStringLiteral("#141218"));
    QCOMPARE(dark.surfaceContainer.name(), QStringLiteral("#211f26"));
    QCOMPARE(dark.surfaceContainerHighest.name(), QStringLiteral("#36343b"));
    QCOMPARE(dark.error.name(), QStringLiteral("#f2b8b5"));
    QCOMPARE(dark.inversePrimary.name(), QStringLiteral("#6750a4"));

    // The fixed roles are identical across schemes by definition.
    QVERIFY(light.primaryFixed == dark.primaryFixed);
    QVERIFY(light.onTertiaryFixedVariant == dark.onTertiaryFixedVariant);
}

void TestTokens::colourSurfaceAtElevation()
{
    const ColorScheme light = ColorScheme::baselineLight();
    const ColorScheme dark = ColorScheme::baselineDark();

    // Level 0 is the plain surface; each step moves up the container ramp, and
    // levels 3 and 4 deliberately share the same tonal step.
    QCOMPARE(light.surfaceAtElevation(Elevation::Level0).rgba(), light.surface.rgba());
    QCOMPARE(light.surfaceAtElevation(Elevation::Level1).rgba(),
             light.surfaceContainerLow.rgba());
    QCOMPARE(light.surfaceAtElevation(Elevation::Level2).rgba(),
             light.surfaceContainer.rgba());
    QCOMPARE(light.surfaceAtElevation(Elevation::Level3).rgba(),
             light.surfaceContainerHigh.rgba());
    QCOMPARE(light.surfaceAtElevation(Elevation::Level4).rgba(),
             light.surfaceContainerHigh.rgba());
    QCOMPARE(light.surfaceAtElevation(Elevation::Level5).rgba(),
             light.surfaceContainerHighest.rgba());

    QCOMPARE(dark.surfaceAtElevation(Elevation::Level0).rgba(), dark.surface.rgba());
    QCOMPARE(dark.surfaceAtElevation(Elevation::Level1).rgba(),
             dark.surfaceContainerLow.rgba());
    QCOMPARE(dark.surfaceAtElevation(Elevation::Level2).rgba(),
             dark.surfaceContainer.rgba());
    QCOMPARE(dark.surfaceAtElevation(Elevation::Level3).rgba(),
             dark.surfaceContainerHigh.rgba());
    QCOMPARE(dark.surfaceAtElevation(Elevation::Level4).rgba(),
             dark.surfaceContainerHigh.rgba());
    QCOMPARE(dark.surfaceAtElevation(Elevation::Level5).rgba(),
             dark.surfaceContainerHighest.rgba());

    QVERIFY(light.surfaceAtElevation(Elevation::Level3)
            == light.surfaceAtElevation(Elevation::Level4));
    QVERIFY(light.surfaceAtElevation(Elevation::Level0)
            != light.surfaceAtElevation(Elevation::Level5));

    // The scheme-level state layer is the same compositing rule.
    QCOMPARE(light.stateLayer(light.primary, ViewState::Hovered).alpha(), 20);
    QVERIFY(light.stateLayer(light.primary, ViewState::Focused)
            == md::applyStateLayer(light.primary, ViewState::Focused));
}

void TestTokens::colourContrast()
{
    QCOMPARE(int(std::size(kSchemes)), 2);
    QCOMPARE(int(std::size(kTextPairs)), 10);

    for (const SchemeRow& schemeRow : kSchemes) {
        for (const ContrastPair& pair : kTextPairs) {
            const QColor background = schemeRow.scheme.*(pair.background);
            const QColor foreground = schemeRow.scheme.*(pair.foreground);
            const qreal ratio = contrastRatio(background, foreground);

            const QByteArray message =
                QStringLiteral("%1 %2 contrast ratio %3")
                    .arg(QString::fromLatin1(schemeRow.name),
                         QString::fromLatin1(pair.name),
                         QString::number(ratio, 'f', 2))
                    .toLatin1();

            QVERIFY2(ratio >= 4.5, message.constData());
        }
    }
}

void TestTokens::colourOpaque()
{
    QCOMPARE(int(std::size(kSchemes)), 2);
    QCOMPARE(int(std::size(kRoles)), 47);

    for (const SchemeRow& schemeRow : kSchemes) {
        for (const RoleRow& row : kRoles) {
            const QColor colour = schemeRow.scheme.*(row.member);
            QVERIFY2(colour.isValid(), row.name);
            QVERIFY2(colour.alpha() == 255, row.name);
        }
    }
}

void TestTokens::shapeClampsOversizedRadii()
{
    // A 40x20 box carries at most half its shorter side, so the largest named
    // token resolves to 10 rather than its stored 48. The token itself keeps
    // its nominal radius: resolution is per box.
    const QRectF small(0.0, 0.0, 40.0, 20.0);
    const CornerRadii clamped = Shape::extraExtraLarge().resolved(small);
    QVERIFY(clamped == CornerRadii::uniform(10.0));
    QCOMPARE(Shape::extraExtraLarge().radii().topStart, 48.0);

    // On a box large enough to carry it the token resolves unchanged.
    QVERIFY(Shape::extraExtraLarge().resolved(QRectF(0.0, 0.0, 200.0, 200.0))
            == CornerRadii::uniform(48.0));

    // Asymmetric corners are clamped one by one, not as a group.
    const Shape asymmetric = Shape::rounded(CornerRadii{4.0, 8.0, 12.0, 16.0});
    const CornerRadii expected{4.0, 5.0, 5.0, 5.0};
    QVERIFY(asymmetric.resolved(QRectF(0.0, 0.0, 10.0, 10.0)) == expected);

    // The resolved radii are what path() draws: the clamp does not only exist
    // in the path factory.
    const QRectF wide(0.0, 0.0, 160.0, 20.0);
    QVERIFY(Shape::extraExtraLarge().path(wide)
            == CornerRadii::uniform(10.0).toPath(wide));

    // A degenerate box resolves to no rounding rather than a negative radius.
    QVERIFY(Shape::large().resolved(QRectF(0.0, 0.0, 0.0, 12.0))
            == CornerRadii::uniform(0.0));

    // The pill is unaffected: it already resolved against the bounds.
    QVERIFY(Shape::pill().resolved(small) == CornerRadii::uniform(10.0));
}

QTEST_MAIN(TestTokens)

#include "tst_tokens.moc"
