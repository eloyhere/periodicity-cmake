// Periodicity — measurement constraints (framework.md §6.2).
//
// A MeasureSpec is the Android-style constraint handed down the measurement
// contract: a size together with a mode that says how the size may be used.
// It is a plain value type with no Qt object overhead, and everything that
// can be computed at compile time is constexpr so that default measurements
// cost nothing at run time.

#pragma once

#include <QtGlobal>

namespace md {

class MeasureSpec
{
public:
    // How the size member constrains the desired size.
    enum class Mode {
        Unspecified, // The parent imposes no bound; the child chooses freely.
        Exactly,     // The child must adopt the size unconditionally.
        AtMost,      // The child may be smaller, but never larger than the size.
    };

    constexpr MeasureSpec() noexcept = default;
    constexpr MeasureSpec(qreal size, Mode mode) noexcept : size_(size), mode_(mode) {}

    // A spec that forces the measured size to `size`.
    [[nodiscard]] static constexpr MeasureSpec exactly(qreal size) noexcept
    {
        return MeasureSpec(size, Mode::Exactly);
    }

    // A spec that caps the measured size at `size`.
    [[nodiscard]] static constexpr MeasureSpec atMost(qreal size) noexcept
    {
        return MeasureSpec(size, Mode::AtMost);
    }

    // A spec that imposes no constraint; the carried size is zero.
    [[nodiscard]] static constexpr MeasureSpec unspecified() noexcept
    {
        return MeasureSpec(0.0, Mode::Unspecified);
    }

    [[nodiscard]] constexpr qreal size() const noexcept { return size_; }
    [[nodiscard]] constexpr Mode mode() const noexcept { return mode_; }

    // Applies the spec to a size the child would like to have.
    //
    //   Exactly     -> the spec size (desired is ignored, even when smaller)
    //   AtMost      -> min(desired, spec size)
    //   Unspecified -> desired, unchanged
    [[nodiscard]] constexpr qreal resolve(qreal desired) const noexcept
    {
        switch (mode_) {
        case Mode::Exactly:
            return size_;
        case Mode::AtMost:
            return desired < size_ ? desired : size_;
        case Mode::Unspecified:
            break;
        }
        return desired;
    }

private:
    qreal size_ = 0.0;
    Mode mode_ = Mode::Unspecified;
};

} // namespace md
