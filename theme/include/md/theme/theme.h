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
// only reads, so it does not allocate in the common case.
//
// Threading. The application theme is published as an immutable snapshot
// behind a mutex: `applicationTheme()` and `forWidget(nullptr)` may be called
// from any thread. Walking a widget chain is GUI-thread-only, and
// `setApplicationTheme()` asserts that affinity. The reference returned for
// the application theme points into a snapshot retained for the process
// lifetime, so it stays valid even when another thread changes the theme.
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

#include <QMetaType>
#include <QPalette>
#include <QtGlobal>

class QWidget;

namespace md {

class Theme
{
public:
    // A fully populated baseline light theme, so Q_DECLARE_METATYPE's default
    // construction (and `Theme{}`) never yields invalid colours. Constructing
    // the token bundles explicitly is the normal path.
    Theme();

    // Every subsystem is taken by value and stored by value: a Theme owns its
    // tokens, so the caller may keep mutating the arguments afterwards.
    //
    // A colour scheme whose core roles are invalid (the members of an empty
    // ColorScheme are uninitialised) is replaced as a whole by
    // ColorScheme::baselineLight(), and the density is normalised (see
    // withDensity()), so a Theme can always produce a usable palette and
    // positive dp/sp values.
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

    // The density is normalised before it is stored: a non-finite or
    // non-positive scale (or fontScale) falls back to 1.0, and a positive value
    // is clamped into 0.1..4.0, so dp()/sp() can never collapse, invert or
    // overflow the geometry. Density itself is unchanged (it is a core type);
    // this is the Theme boundary's guard.
    [[nodiscard]] Theme withDensity(Density density) const;

    // The one-way palette bridge for stock Qt widgets (framework.md §10.3).
    //
    // Mapping: Window→surface, WindowText→onSurface,
    // Base→surfaceContainerLowest, Text→onSurface, Button→surfaceContainer,
    // ButtonText→onSurface, Highlight→primary, HighlightedText→onPrimary,
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
    // A null widget resolves to the application theme.
    //
    // A null widget is safe on any thread. A non-null widget walks the widget
    // parent chain and is therefore GUI-thread-only (asserted in debug builds).
    //
    // For a scope, the reference belongs to that scope and stays valid only
    // while the scope is installed: ThemeScope::install() and deleting the
    // scope invalidate references handed out earlier, and a setTheme()/adapter
    // re-resolution changes the value seen through such a reference. For the
    // application theme, the reference points into an immutable snapshot
    // retained for the process lifetime and stays valid across
    // setApplicationTheme() calls.
    [[nodiscard]] static const Theme& forWidget(const QWidget* widget);

    // The Android-style attribute layer. Android's view system names the
    // palette `colorPrimary`, `colorPrimaryDark`, `colorSecondary` and
    // `colorAccent`; these accessors expose the same attributes from the token
    // scheme, so a host that thinks in those names does not need a second
    // palette type.
    //
    // `fromColours()` builds a whole Theme from three seed colours: the
    // on-colours, containers and neutral surfaces are derived from them, while
    // type, shape and motion come from the baseline of the same brightness.
    // `colour()` accepts `colorPrimary`/`colourPrimary`, `Second`/`Secondary`
    // and `Acent`/`Accent`; an unknown attribute yields an invalid QColor.
    [[nodiscard]] static Theme fromColours(QColor primary, QColor secondary,
                                           QColor accent, bool dark = false);
    [[nodiscard]] QColor colourPrimary() const;
    [[nodiscard]] QColor colourPrimaryDark() const;
    [[nodiscard]] QColor colourSecondary() const;
    [[nodiscard]] QColor colourSecondaryDark() const;
    [[nodiscard]] QColor colourAccent() const;
    [[nodiscard]] QColor colour(const QString& attribute) const;

    // Replaces the application theme and posts QEvent::ThemeChange to every
    // top-level widget and its subtree, so cached paints are invalidated.
    // Derived (adapter) ThemeScopes are re-resolved first, and the notification
    // then reaches their hosts as well.
    //
    // An equal theme is a no-op: nothing is re-resolved and no event is posted,
    // which also keeps a store that republishes the same theme from flooding
    // the widget tree.
    //
    // This function touches the widget tree and must therefore be called on
    // the GUI thread; a call from another thread is rejected with a warning and
    // leaves the stored theme unchanged. Calling it before QApplication exists
    // is allowed (the value is stored and no events are posted), which lets an
    // application pin its theme before constructing widgets. Readers on other
    // threads are safe: the published value is immutable.
    static void setApplicationTheme(Theme theme);

    // The current application theme. Until setApplicationTheme is called this
    // is Theme::baselineLight().
    //
    // Safe to call from any thread. The reference points into an immutable
    // snapshot that is retained for the process lifetime, so it stays valid
    // even if another thread publishes a different theme immediately after the
    // call; old snapshots are kept rather than freed, which is why the setter
    // is a user action rather than a per-frame operation.
    [[nodiscard]] static const Theme& applicationTheme();

private:
    ColorScheme colours_;
    TypeScale type_;
    ShapeScale shapes_;
    MotionScheme motion_;
    Density density_;
};

} // namespace md

Q_DECLARE_METATYPE(md::Theme)
