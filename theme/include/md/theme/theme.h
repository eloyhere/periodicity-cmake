// Periodicity — the theme bundle (framework.md §5.3, §10.1–10.3).
//
// A Theme is the five token subsystems a component paints from, plus the
// density that converts their dp/sp values into device-independent pixels.
// It is an immutable value: the `withX` methods return a modified copy rather
// than mutating a shared object, so a scope can hand a snapshot to a subtree
// without any locking or invalidation protocol.
//
// Resolution is a separate concern. `forWidget` walks the widget parent chain
// looking for a ThemeScope (see theme_scope.h), then falls back to the
// application theme, then to the built-in baseline light theme. Resolution
// never fails and never allocates in the common case.
//
// The palette bridge is deliberately one-way (framework.md §10.3): a Theme
// derives a QPalette so that stock Qt widgets are visually consistent, but a
// palette assigned by an application never flows back into the Material
// theme.

#pragma once

#include <md/core/density.h>
#include <md/tokens/color_scheme.h>
#include <md/tokens/motion.h>
#include <md/tokens/shape.h>
#include <md/tokens/type_scale.h>

#include <QPalette>
#include <QtGlobal>

class QWidget;

namespace md {

class Theme
{
public:
    // Every subsystem is taken by value and stored by value: a Theme owns its
    // tokens, so the caller may keep mutating the arguments afterwards.
    Theme(ColorScheme colours, TypeScale type, ShapeScale shapes, MotionScheme motion,
          Density density);

    [[nodiscard]] const ColorScheme& colours() const noexcept { return colours_; }
    [[nodiscard]] const TypeScale& type() const noexcept { return type_; }
    [[nodiscard]] const ShapeScale& shapes() const noexcept { return shapes_; }
    [[nodiscard]] const MotionScheme& motion() const noexcept { return motion_; }
    [[nodiscard]] const Density& density() const noexcept { return density_; }

    // A copy of this theme with exactly one subsystem replaced. The other four
    // members are copied unchanged, which is asserted by the unit tests.
    [[nodiscard]] Theme withColours(ColorScheme colours) const;
    [[nodiscard]] Theme withType(TypeScale type) const;
    [[nodiscard]] Theme withShapes(ShapeScale shapes) const;
    [[nodiscard]] Theme withMotion(MotionScheme motion) const;
    [[nodiscard]] Theme withDensity(Density density) const;

    // The one-way palette bridge for stock Qt widgets (framework.md §10.3).
    //
    // Mapping: Window→surface, WindowText→onSurface,
    // Base→surfaceContainerLowest, Text→onSurface, Button→surfaceContainer,
    // ButtonText→onPrimary, Highlight→primary, HighlightedText→onPrimary,
    // ToolTipBase→inverseSurface, ToolTipText→inverseOnSurface,
    // PlaceholderText→onSurfaceVariant. Disabled Text and ButtonText are the
    // same colours at 38% alpha. Roles not named here keep Qt's defaults;
    // Periodicity widgets themselves never paint from a QPalette.
    [[nodiscard]] QPalette toPalette() const;

    // The two built-in themes. Both bundle the baseline type, shape and motion
    // scales; they differ only in their colour scheme.
    [[nodiscard]] static Theme baselineLight();
    [[nodiscard]] static Theme baselineDark();

    [[nodiscard]] friend bool operator==(const Theme&, const Theme&) = default;

    // Resolves the theme that applies to `widget`:
    //   nearest ThemeScope ancestor → application theme → baseline light.
    // A null widget resolves to the application theme. The reference stays
    // valid until the scope or the application theme is replaced.
    [[nodiscard]] static const Theme& forWidget(const QWidget* widget);

    // Replaces the application theme and posts QEvent::ThemeChange to every
    // top-level widget and its subtree, so cached paints are invalidated.
    //
    // This function touches the widget tree and must therefore be called on
    // the GUI thread. Calling it before QApplication exists is allowed (the
    // value is stored and no events are posted), which lets an application
    // pin its theme before constructing widgets.
    static void setApplicationTheme(Theme theme);

    // The current application theme. Until setApplicationTheme is called this
    // is Theme::baselineLight(). Like the tokens it bundles, the returned
    // reference is read-only.
    [[nodiscard]] static const Theme& applicationTheme();

private:
    ColorScheme colours_;
    TypeScale type_;
    ShapeScale shapes_;
    MotionScheme motion_;
    Density density_;
};

} // namespace md
