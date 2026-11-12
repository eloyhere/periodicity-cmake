// Periodicity — motion tokens (framework.md §3.5).
//
// Durations and easings are values; the animator that consumes them lives in
// the animation layer. Keeping the curve here as four numbers rather than as a
// QEasingCurve means a motion spec stays cheap to copy, compare and snapshot,
// and a headless tool can read it without linking the animation layer.

#pragma once

#include <QEasingCurve>
#include <QtGlobal>

#include <chrono>

namespace md {

// A cubic Bézier easing curve, with the two control points expressed as in
// CSS: (0,0) and (1,1) are implicit endpoints.
struct CubicBezier
{
    qreal x1 = 0.0;
    qreal y1 = 0.0;
    qreal x2 = 0.0;
    qreal y2 = 0.0;

    // The equivalent Qt easing curve. The conversion is one-way: a
    // QEasingCurve cannot be turned back into a value type.
    [[nodiscard]] QEasingCurve toEasingCurve() const;

    [[nodiscard]] friend constexpr bool operator==(const CubicBezier& lhs,
                                                   const CubicBezier& rhs) noexcept = default;
};

enum class Duration {
    Short1,
    Short2,
    Short3,
    Short4,
    Medium1,
    Medium2,
    Medium3,
    Medium4,
    Long1,
    Long2,
    Long3,
    Long4,
    ExtraLong1,
    ExtraLong2,
    ExtraLong3,
    ExtraLong4,
    // Periodicity extensions: the reference export stops at 1000 ms, but the
    // duration ladder below needs 750 ms and 1250 ms rungs so that every level
    // is exactly 250 ms apart.
    Extended1,
    Extended2,
    Extended3,
};

enum class Easing {
    Linear,
    Standard,
    StandardDecelerate,
    StandardAccelerate,
    Emphasized,
    EmphasizedDecelerate,
    EmphasizedAccelerate,
    Legacy,
    LegacyDecelerate,
    LegacyAccelerate,
};

// Semantic motion levels: a fixed ladder of durations, 250 ms apart.
//
// Every level is a pure duration; the easing stays standard so that comparing
// two levels compares speed and nothing else. The ladder is deliberately
// arithmetic rather than "small/medium/large": a designer can point at a rung
// and say "that one" without translating a name into a time.
enum class MotionLevel : quint8 {
    Level0, // 0 ms — no transition
    Level1, // 250 ms
    Level2, // 500 ms
    Level3, // 750 ms
    Level4, // 1000 ms
    Level5, // 1250 ms
    Level6, // 1500 ms
};

// Duration of `duration` in milliseconds.
[[nodiscard]] constexpr std::chrono::milliseconds durationMs(Duration duration) noexcept
{
    using Ms = std::chrono::milliseconds;

    switch (duration) {
    case Duration::Short1:
        return Ms(50);
    case Duration::Short2:
        return Ms(100);
    case Duration::Short3:
        return Ms(150);
    case Duration::Short4:
        return Ms(200);
    case Duration::Medium1:
        return Ms(250);
    case Duration::Medium2:
        return Ms(300);
    case Duration::Medium3:
        return Ms(350);
    case Duration::Medium4:
        return Ms(400);
    case Duration::Long1:
        return Ms(450);
    case Duration::Long2:
        return Ms(500);
    case Duration::Long3:
        return Ms(550);
    case Duration::Long4:
        return Ms(600);
    case Duration::ExtraLong1:
        return Ms(700);
    case Duration::ExtraLong2:
        return Ms(800);
    case Duration::ExtraLong3:
        return Ms(900);
    case Duration::ExtraLong4:
        return Ms(1000);
    case Duration::Extended1:
        return Ms(750);
    case Duration::Extended2:
        return Ms(1250);
    case Duration::Extended3:
        return Ms(1500);
    }
    return Ms(0);
}

// Control points of `easing`.
//
// `emphasized` is identical to `standard` in the current reference export; the
// distinguished behaviour of the emphasised family comes from its accelerate
// and decelerate variants and from the longer durations they are paired with.
// The names are kept distinct so a future revision can diverge without
// touching call sites (framework.md §3.5).
[[nodiscard]] constexpr CubicBezier easingCurve(Easing easing) noexcept
{
    switch (easing) {
    case Easing::Linear:
        return CubicBezier{0.0, 0.0, 1.0, 1.0};
    case Easing::Standard:
        return CubicBezier{0.2, 0.0, 0.0, 1.0};
    case Easing::StandardDecelerate:
        return CubicBezier{0.0, 0.0, 0.0, 1.0};
    case Easing::StandardAccelerate:
        return CubicBezier{0.3, 0.0, 1.0, 1.0};
    case Easing::Emphasized:
        return CubicBezier{0.2, 0.0, 0.0, 1.0};
    case Easing::EmphasizedDecelerate:
        return CubicBezier{0.05, 0.7, 0.1, 1.0};
    case Easing::EmphasizedAccelerate:
        return CubicBezier{0.3, 0.0, 0.8, 0.15};
    case Easing::Legacy:
        return CubicBezier{0.4, 0.0, 0.2, 1.0};
    case Easing::LegacyDecelerate:
        return CubicBezier{0.0, 0.0, 0.2, 1.0};
    case Easing::LegacyAccelerate:
        return CubicBezier{0.4, 0.0, 1.0, 1.0};
    }
    return CubicBezier{};
}

// A resolved motion: how long a transition runs, along which curve, and how
// long it waits before starting.
struct MotionSpec
{
    std::chrono::milliseconds duration{};
    CubicBezier easing{};
    std::chrono::milliseconds delay{};

    [[nodiscard]] friend constexpr bool operator==(const MotionSpec& lhs,
                                                   const MotionSpec& rhs) noexcept = default;
};

// The motion scheme a theme exposes. The scheme is a value: reduced motion is
// a property of the snapshot, not a global setting, so a caller can preview
// the reduced variant without mutating anything.
struct MotionScheme
{
    // When true every resolved spec has a zero duration and the caller snaps
    // to the end state, which is the accessible behaviour for users who have
    // asked the operating system to reduce motion.
    bool reducedMotion = false;

    [[nodiscard]] constexpr MotionSpec spec(Duration duration,
                                            Easing easing = Easing::Standard) const noexcept
    {
        const std::chrono::milliseconds length =
            reducedMotion ? std::chrono::milliseconds{0} : durationMs(duration);
        return MotionSpec{length, easingCurve(easing), std::chrono::milliseconds{0}};
    }

    // The named combinations components use most often. They resolve through
    // spec(), so the reduced-motion rule applies to all of them.
    [[nodiscard]] constexpr MotionSpec short4() const noexcept
    {
        return spec(Duration::Short4);
    }

    [[nodiscard]] constexpr MotionSpec medium1() const noexcept
    {
        return spec(Duration::Medium1);
    }

    [[nodiscard]] constexpr MotionSpec medium2() const noexcept
    {
        return spec(Duration::Medium2);
    }

    [[nodiscard]] constexpr MotionSpec long2() const noexcept
    {
        return spec(Duration::Long2);
    }

    [[nodiscard]] constexpr MotionSpec emphasizedMedium2() const noexcept
    {
        return spec(Duration::Medium2, Easing::Emphasized);
    }

    // The duration ladder. Level0 stays instantaneous even when the scheme is
    // not reduced: a caller that asked for no transition must get none.
    [[nodiscard]] constexpr MotionSpec level(MotionLevel value) const noexcept
    {
        switch (value) {
        case MotionLevel::Level0:
            return MotionSpec{std::chrono::milliseconds{0}, easingCurve(Easing::Linear),
                              std::chrono::milliseconds{0}};
        case MotionLevel::Level1:
            return spec(Duration::Medium1);
        case MotionLevel::Level2:
            return spec(Duration::Long2);
        case MotionLevel::Level3:
            return spec(Duration::Extended1);
        case MotionLevel::Level4:
            return spec(Duration::ExtraLong4);
        case MotionLevel::Level5:
            return spec(Duration::Extended2);
        case MotionLevel::Level6:
            return spec(Duration::Extended3);
        }
        return spec(Duration::Medium2);
    }

    [[nodiscard]] static constexpr MotionScheme baseline() noexcept { return MotionScheme{}; }

    [[nodiscard]] friend constexpr bool operator==(const MotionScheme& lhs,
                                                   const MotionScheme& rhs) noexcept = default;
};

} // namespace md
