# Periodicity — Framework Architecture

> **Status** Draft for review · **Baseline** Material Design 3 (M3) · **Stack** C++20, Qt 6.8 LTS (Widgets + QPainter) · **Platforms** Windows first, then Linux and macOS · **Version** 0.1 · **Date** 9 November 2026

This document defines the internal architecture of Periodicity: how the code is layered, what each module is responsible for, which abstractions the components are built on, and which dependencies are permitted. It is a design document, not an API reference and not a delivery schedule; the roadmap in §16 exists only to show the order in which the architecture is expected to be realised.

---

## 1. Purpose and scope

Periodicity is a cross-platform C++ UI component library that implements Material Design on top of Qt Widgets, with all visual output produced by `QPainter` rather than by Qt Style Sheets or platform-native controls. The library targets applications that want Material-styled widgets without adopting Qt Quick, and without depending on a platform theme engine.

### 1.1 In scope

- A design-token layer (colour, typography, shape, elevation, motion, state) that is independent of any widget.
- A drawing substrate (drawables and composited layers) shared by every component.
- A view core providing a predictable measure → arrange → paint lifecycle, state handling and input semantics.
- Layout containers and adaptive behaviour keyed to window size classes.
- A component set built exclusively on the abstractions above.
- Theming and integration with the host Qt application.

### 1.2 Out of scope

| Excluded | Rationale |
| --- | --- |
| A QML/Qt Quick binding | A separate front end; would double the surface area before the Widgets core is stable. Revisit after Phase 4. |
| A runtime theme scripting language | Themes are C++ value objects; scripting adds a parser, an evaluation model and a security surface for little gain. |
| Reimplementation of text shaping, input methods or accessibility plumbing | Qt already provides these; Periodicity adapts them rather than replacing them. |
| Platform-native look and feel | Explicitly a Material implementation; native dialogs and menus may still be used where Material has no equivalent (see §10.6). |
| A full icon set | Material Symbols are consumed as assets; the library provides tinting and mirroring, not glyph design. |

### 1.3 Intended readers

Contributors implementing components, reviewers assessing whether a change respects the layering, and integrators who need to know which parts of the library are stable and which are internal.

---

## 2. Goals, constraints and decisions

### 2.1 Goals

| Goal | What it means concretely |
| --- | --- |
| Fidelity | Token values and component metrics match the M3 specification; deviations are documented and localised to a single token or metric constant. |
| Composability | Any component can be placed in a stock Qt layout, and any stock Qt widget can sit inside a Periodicity container. |
| Themeability | Switching light/dark or replacing the colour scheme at runtime requires no widget reconstruction. |
| Predictability | Painting follows one documented order; geometry follows one documented measure contract. |
| Testability | Every layer can be exercised headlessly; visual output can be compared deterministically. |
| Performance | A component repaints within a fraction of a 60 Hz frame; no allocation in the steady-state paint path. |
| Accessibility | Every interactive component exposes role, name, state and action through `QAccessible`. |
| Portability | No platform-specific code outside a small, isolated adapter; Windows is the first target, not the only design point. |

### 2.2 Constraints

- **Qt 6.8 LTS**, modules `Core`, `Gui`, `Widgets`, `Test`. No private Qt headers.
- **QPainter** is the only rendering path for Periodicity widgets. `QGraphicsEffect`, `QGraphicsView` and QSS-based styling are not used for component visuals.
- **C++20**, no compiler extensions, no exceptions escaping Qt signal/slot boundaries.
- **CMake ≥ 3.21** (the current `cmake_minimum_required(VERSION 3.10)` will be raised; presets v3 already require 3.21).
- **MSVC 2022** first; GCC and Clang must remain viable so that Linux and macOS follow without redesign.
- **No global mutable state** in the token or painting layers; a theme is a value, not a singleton.

### 2.3 Architecture decisions

| ID | Decision | Consequence |
| --- | --- | --- |
| AD-1 | Qt Widgets with `QPainter`-based rendering | Full control over Material visuals; loses native theming and requires reimplementing text-field editing chrome. |
| AD-2 | Tokens are immutable value types | Cheap copying, trivial thread-safety, easy snapshotting for golden tests; theme switching allocates a new bundle. |
| AD-3 | `1 dp` equals `1` device-independent pixel at scale 1.0 | Desktop-sensible sizing; deviates from Android's 160 dpi definition and is recorded as an open question (§17). |
| AD-4 | Hybrid layout: Android-style measure hooks bridged through `QLayout` | Components compose with stock Qt layouts while retaining Material measurement semantics. |
| AD-5 | Painting is a fixed template method over composited layers | One documented order; subclasses cannot accidentally paint outside it. |
| AD-6 | No `QGraphicsEffect`; shadows are cached, pre-blurred pixmaps | Predictable cost, correct high-DPI output, no offscreen render passes per frame. |
| AD-7 | Theme resolution walks the widget parent chain | Scoped theming (a dark card inside a light page) without global state. |
| AD-8 | One CMake target per module, acyclic | Layering is enforced by the build system, not by convention alone. |

---

## 3. Material baseline

The library targets the **M3 baseline**: the stable colour, type, shape, elevation, motion and state systems. Dynamic colour (wallpaper-derived palettes) and M3 Expressive are explicitly deferred; the token model is designed so that both can be added without breaking the component contract (§17, Q-1).

All values below were cross-checked against Google's reference token sources rather than transcribed from prose documentation: the Compose Material 3 token files (`MotionTokens.kt`, `ShapeTokens.kt`, `ElevationTokens.kt`, `TypeScaleTokens.kt`), the Material Components for Android colour document, and the Material Web state-token export. The specification site `m3.material.io` serves content only to JavaScript clients and could not be read directly through the proxy; this is noted so that future token audits know which source is authoritative.

### 3.1 Colour

A scheme is generated from five key colours — primary, secondary, tertiary, neutral and neutral-variant — each expanded into a **tonal palette of 13 tones** (0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 95, 99, 100). Roles are then assigned by tone, differently for light and dark schemes.

| Group | Roles |
| --- | --- |
| Primary | `primary`, `onPrimary`, `primaryContainer`, `onPrimaryContainer` |
| Secondary | `secondary`, `onSecondary`, `secondaryContainer`, `onSecondaryContainer` |
| Tertiary | `tertiary`, `onTertiary`, `tertiaryContainer`, `onTertiaryContainer` |
| Error | `error`, `onError`, `errorContainer`, `onErrorContainer` |
| Surface | `surface`, `onSurface`, `surfaceVariant`, `onSurfaceVariant`, `surfaceDim`, `surfaceBright`, `surfaceContainerLowest`, `surfaceContainerLow`, `surfaceContainer`, `surfaceContainerHigh`, `surfaceContainerHighest` |
| Outline and utility | `outline`, `outlineVariant`, `shadow`, `scrim`, `surfaceTint` |
| Inverse | `inverseSurface`, `inverseOnSurface`, `inversePrimary` |
| Fixed | `primaryFixed`, `primaryFixedDim`, `onPrimaryFixed`, `onPrimaryFixedVariant` (and the secondary/tertiary equivalents) |

Rules that the implementation must respect:

- **On-roles are contrast partners, not decoration.** A component may only place `onX` content on `X`; the contrast pair is part of the token contract and is asserted in debug builds.
- **Surface containers encode hierarchy.** Elevated or nested surfaces use the `surfaceContainer*` ramp rather than ad-hoc alpha overlays.
- **Tonal elevation is a colour operation.** `surfaceAtElevation(level)` resolves a surface colour from the neutral palette; it is not an alpha-blended white/black overlay (see §7.4).
- **Dynamic colour is absent.** No seed-colour derivation, no wallpaper access, no HCT conversion in the baseline. The `ColorScheme` type has a factory for baseline light and dark schemes only.

### 3.2 Typography

Fifteen styles in five families and three sizes. Values verified from `TypeScaleTokens.kt`:

| Style | Size (sp) | Line height (sp) | Tracking (sp) | Weight |
| --- | ---: | ---: | ---: | --- |
| `displayLarge` | 57 | 64 | −0.2 | 400 |
| `displayMedium` | 45 | 52 | 0.0 | 400 |
| `displaySmall` | 36 | 44 | 0.0 | 400 |
| `headlineLarge` | 32 | 40 | 0.0 | 400 |
| `headlineMedium` | 28 | 36 | 0.0 | 400 |
| `headlineSmall` | 24 | 32 | 0.0 | 400 |
| `titleLarge` | 22 | 28 | 0.0 | 400 |
| `titleMedium` | 16 | 24 | 0.2 | 500 |
| `titleSmall` | 14 | 20 | 0.1 | 500 |
| `bodyLarge` | 16 | 24 | 0.5 | 400 |
| `bodyMedium` | 14 | 20 | 0.2 | 400 |
| `bodySmall` | 12 | 16 | 0.4 | 400 |
| `labelLarge` | 14 | 20 | 0.1 | 500 |
| `labelMedium` | 12 | 16 | 0.5 | 500 |
| `labelSmall` | 11 | 16 | 0.5 | 500 |

Type is specified in **sp**, so it scales with the user's font scale independently of density. Line height is explicit: Qt's `QFontMetricsF::height()` is not used as a line height, because font designers' metrics do not match the M3 ratios.

### 3.3 Shape

| Token | Corner radius (dp) | Typical use |
| --- | ---: | --- |
| `none` | 0 | Full-bleed surfaces, dividers |
| `extraSmall` | 4 | Menus, the top corners of filled text fields |
| `small` | 8 | Chips, dense cards |
| `medium` | 12 | Cards, floating surfaces |
| `large` | 16 | Floating action buttons, extended FABs |
| `largeIncreased` | 20 | Expressive variants (reserved) |
| `extraLarge` | 28 | Dialogs, bottom sheets |
| `extraLargeIncreased` | 32 | Reserved |
| `extraExtraLarge` | 48 | Reserved |
| `full` | 50 % of the shortest side | Pills, avatars, icon buttons |

Corner radii are stored per corner (start-start, start-end, end-end, end-start) so that directional shapes such as menus and bottom sheets are expressible without mirroring hacks. A shape is a **path factory**, not a radius list: cut corners and notched shapes are supported by substituting the factory.

### 3.4 Elevation

Six levels, expressed both as a shadow offset/blur and as a tonal surface colour:

| Level | dp | Shadow | Tonal surface |
| --- | ---: | --- | --- |
| 0 | 0 | None | `surface` |
| 1 | 1 | Subtle | `surfaceAtElevation(1)` |
| 2 | 3 | Low | `surfaceAtElevation(2)` |
| 3 | 6 | Medium | `surfaceAtElevation(3)` |
| 4 | 8 | High | `surfaceAtElevation(4)` |
| 5 | 12 | Highest | `surfaceAtElevation(5)` |

The shadow is a property of the surface, not of the content: it is drawn outside the shape's bounds and must never be clipped by the content rectangle.

### 3.5 Motion

Durations and easings verified from `MotionTokens.kt`:

| Token | ms | Token | ms |
| --- | ---: | --- | ---: |
| `short1` | 50 | `medium3` | 350 |
| `short2` | 100 | `medium4` | 400 |
| `short3` | 150 | `long1` | 450 |
| `short4` | 200 | `long2` | 500 |
| `medium1` | 250 | `long3` | 550 |
| `medium2` | 300 | `long4` | 600 |
| `extraLong1` | 700 | `extraLong2` | 800 |
| `extraLong3` | 900 | `extraLong4` | 1000 |

| Easing token | Cubic Bézier |
| --- | --- |
| `linear` | (0.0, 0.0, 1.0, 1.0) |
| `standard` | (0.2, 0.0, 0.0, 1.0) |
| `standardDecelerate` | (0.0, 0.0, 0.0, 1.0) |
| `standardAccelerate` | (0.3, 0.0, 1.0, 1.0) |
| `emphasized` | (0.2, 0.0, 0.0, 1.0) |
| `emphasizedDecelerate` | (0.05, 0.7, 0.1, 1.0) |
| `emphasizedAccelerate` | (0.3, 0.0, 0.8, 0.15) |
| `legacy` | (0.4, 0.0, 0.2, 1.0) |
| `legacyDecelerate` | (0.0, 0.0, 0.2, 1.0) |
| `legacyAccelerate` | (0.4, 0.0, 1.0, 1.0) |

Note that the reference export defines `emphasized` identically to `standard`; the distinguishing behaviour of the emphasised family comes from the accelerate/decelerate variants and from longer durations. The implementation keeps them as distinct names so that a future specification revision can diverge without touching call sites.

**Semantic levels.** Components usually choose by the size of the change rather than by an
exact token, so the scheme also exposes a fixed duration ladder. Every rung is 250 ms apart and
uses the standard curve, so comparing two levels compares speed and nothing else:

| Level | Duration | Typical use |
| --- | ---: | --- |
| `Level0` | 0 ms | A state swap with no transition |
| `Level1` | 250 ms | Hover, focus, small state changes |
| `Level2` | 500 ms | The default transition |
| `Level3` | 750 ms | Entrance and exit of prominent content |
| `Level4` | 1000 ms | Sheets and dialogs |
| `Level5` | 1250 ms | Full-screen or hero transitions |

The ladder needs 750 ms and 1250 ms, which the reference export does not contain; they are
declared as `Extended1` and `Extended2` in the duration enum and marked as Periodicity
extensions. Under reduced motion every level except `Level0` collapses to zero duration;
`Level0` is zero in both cases because a caller that asked for no transition must get none.

### 3.6 State layers

Interaction feedback is a translucent overlay of the content colour, at fixed opacities:

| State | Opacity |
| --- | ---: |
| Hover | 0.08 |
| Focus | 0.12 |
| Pressed | 0.12 |
| Dragged | 0.16 |
| Disabled content | 0.38 |
| Disabled container | 0.12 |

These are **state-layer** opacities, not component colours: a widget never hard-codes a grey for hover; it composites the state layer over its own container colour.

---

## 4. Layered architecture

### 4.1 Layer diagram

```
                    ┌──────────────────────────────┐
  L6  components    │ Button, Card, TextField, …   │
                    ├──────────────────────────────┤
  L5  layout        │ Row/Column/Frame, size classes│
                    ├──────────────────────────────┤
  L4  view          │ View, ViewGroup, state machine│
                    ├───────────┬──────────┬────────┤
  L3  drawable/layer│ Drawable  │ Layer    │ Motion │
                    ├───────────┴──────────┴────────┤
  L2  theme         │ MaterialTheme, ThemeScope    │
                    ├──────────────────────────────┤
  L1  tokens        │ Colour, Type, Shape, Motion  │
                    ├──────────────────────────────┤
  L1p pool          │ Pools, threads, IO, IPC      │
                    ├──────────────────────────────┤
  L0  core          │ Units, geometry, state flags │
                    └──────────────────────────────┘
                         Qt 6 Core / Gui / Widgets
```

### 4.2 Layer responsibilities

| Layer | Owns | Must not |
| --- | --- | --- |
| L0 `core` | Density, `MeasureSpec`, `ViewStates`, geometry helpers, logging categories | Include `QWidget`, `QPainter` or any token type |
| L1 `schedule` | Task priority queue: ordering, coalescing, budgeted flushes | Know about widgets, painting or tokens |
| L1p `pool` | Object pools, thread pool, ring buffers, IO thread, cross-thread mailbox, same-machine process IPC | Include `QtWidgets`, a token type or any widget |
| L1 `tokens` | Immutable colour scheme, type scale, shape scale, elevation scale, motion specs, state opacities | Know about widgets, painting or Qt Widgets |
| L2 `theme` | The token bundle, scoped resolution, light/dark switching, `QPalette` bridge | Paint anything |
| L3 `event` | Event values, dispatch chain, capture/intercept/bubble, gesture recognition | Know about widgets, painting or tokens |
| L3 `drawable` / `layer` / `animation` / `ripple` | Drawing primitives, composited layers, tweening, pointer-origin feedback | Contain component-specific policy (a button's padding is not a drawable) |
| L4 `view` | Measure/arrange/paint template, event-to-state mapping, focus, hit testing | Know any concrete component |
| L5 `layout` | Containers, spacing, window size classes, RTL arrangement | Paint beyond backgrounds |
| L6 `components` | Public widgets and their anatomy | Introduce new token values or bypass the view template |

### 4.3 Rules

1. **Dependencies point downwards only.** No layer may include a header from a layer above it.
2. **Tokens are data.** A token type has no virtual functions, no `QObject` base and no dependency on `QtWidgets`.
3. **One paint path.** Every component paints through `View::paintEvent`, which is `final`.
4. **No cross-layer friendship.** Where a lower layer must notify an upper one, it uses a signal or a callback supplied by the upper layer, never an upward `#include`.
5. **`detail/` is private.** Headers under `detail/` are not installed and may change without notice.

---

## 5. Module responsibilities

### 5.1 `core` — units, geometry, state

The only layer that every other layer may include. It contains no visual policy.

- `Density` — converts dp and sp to device-independent pixels. At scale 1.0, `dp(1) == 1.0`. Qt 6 already applies the operating system's display scaling to widget geometry, so `Density` must **not** multiply by `logicalDotsPerInch() / 96`; doing so double-scales. The density scale exists for an explicit application-level zoom, not for DPI.
- `MeasureSpec` — the Android-style constraint triple used by the measure contract (§6.2).
- `ViewStates` — a bitset of interaction states (§6.4).
- Small geometry helpers (insets, corner radii, alignment) and the `md.core` logging category.

### 5.2 `tokens` — the design system as data

Each subsystem is a value type with `constexpr`-friendly defaults and no heap allocation beyond `QColor`/`QString` members.

```cpp
namespace md {

class ColorScheme {
public:
    QColor primary, onPrimary, primaryContainer, onPrimaryContainer;
    QColor secondary, onSecondary, secondaryContainer, onSecondaryContainer;
    QColor tertiary, onTertiary, tertiaryContainer, onTertiaryContainer;
    QColor error, onError, errorContainer, onErrorContainer;
    QColor surface, onSurface, surfaceVariant, onSurfaceVariant;
    QColor outline, outlineVariant, shadow, scrim, surfaceTint;
    QColor inverseSurface, inverseOnSurface, inversePrimary;

    [[nodiscard]] QColor surfaceAtElevation(Elevation level) const;
    [[nodiscard]] QColor stateLayer(QColor content, State state) const;

    static ColorScheme baselineLight();
    static ColorScheme baselineDark();
};

}
```

Token types are copyable and comparable, which makes golden tests and theme snapshots straightforward. They are intentionally **not** `QObject`s: no signals, no parent, no introspection.

### 5.3 `theme` — bundle and scope

`Theme` is a bundle of the five token subsystems plus density. `ThemeScope` attaches a theme to a widget subtree.

```cpp
namespace md {

class Theme {
public:
    Theme(ColorScheme colours, TypeScale type, ShapeScale shapes,
          MotionScheme motion, Density density);

    const ColorScheme& colours() const noexcept;
    const TypeScale&   type()   const noexcept;
    const ShapeScale&  shapes() const noexcept;
    const MotionScheme& motion() const noexcept;
    const Density&     density() const noexcept;

    [[nodiscard]] Theme withColours(ColorScheme) const;

    static const Theme& forWidget(const QWidget* widget);
    static void setApplicationTheme(Theme theme);
};

}
```

Resolution order for `forWidget`: nearest `ThemeScope` ancestor → application theme → built-in baseline light theme. Resolution never fails and never allocates in the common case (the application theme is stored once; scoped themes are values held by the scope object).

### 5.4 `drawable` — reusable painting primitives

A `Drawable` knows how to paint itself into a rectangle and how to describe its intrinsic size. It does not know its parent component.

```cpp
namespace md::drawable {

class Drawable {
public:
    virtual ~Drawable() = default;

    virtual void draw(QPainter& painter, const QRectF& bounds) const = 0;
    [[nodiscard]] virtual QSizeF intrinsicSize() const { return {}; }

    void setState(ViewStates state);
    [[nodiscard]] ViewStates state() const noexcept { return state_; }

    void invalidate();
    [[nodiscard]] virtual quint64 cacheKey() const { return 0; }

protected:
    virtual void onStateChanged(ViewStates previous) { Q_UNUSED(previous); }

private:
    ViewStates state_{ViewState::Enabled};
};

}
```

Concrete drawables planned for Phase 1: `ShapeDrawable` (fill, stroke, corner radii), `ShadowDrawable` (cached blurred mask), `RippleDrawable`, `StateLayerDrawable`, `IconDrawable` (tinted, auto-mirrored), `StateListDrawable` (state → drawable), `InsetDrawable`, `LayerDrawable` (ordered children).

**Caching contract.** A drawable that renders expensively (shadow, ripple mask, complex path) must implement `cacheKey()` and store its raster in `QPixmapCache` under a key composed of the drawable identity, the cache key, the bounds and the device pixel ratio. Invalidation on theme change, state change or resize is the component's responsibility, expressed by calling `invalidate()`.

### 5.5 `layer` — composited surfaces

The `layer/` directory corresponds to the existing empty headers `layer.h`, `background.h`, `foreground.h` and `elevation.h`. A layer is a stage in the paint pipeline rather than a separate render target:

```cpp
namespace md::layer {

class Layer {
public:
    virtual ~Layer() = default;
    virtual void paint(QPainter& painter, const QRectF& bounds,
                       const SurfaceContext& context) const = 0;
    [[nodiscard]] virtual bool hitTest(const QPointF& point) const { return false; }
    [[nodiscard]] virtual bool isAnimated() const noexcept { return false; }
    virtual void advance(std::chrono::milliseconds elapsed) { Q_UNUSED(elapsed); }
};

class BackgroundLayer final : public Layer { /* fill + tonal elevation */ };
class ElevationLayer final : public Layer { /* shadow, drawn outside bounds */ };
class ForegroundLayer : public Layer { /* state layer, ripple, outline */ };

}
```

`SurfaceContext` carries the resolved theme, the current `ViewStates`, the shape and the elevation, so that layers never resolve tokens themselves.

**Why layers rather than one `paint()` per component:** ordering, hit testing and animation become uniform. A focus ring is a foreground layer; a shadow is an elevation layer that paints before the background; the ripple is its own stage (`RippleLayer`) between the state overlay and the focus ring, so a surface composes it like any other stage instead of calling a controller from its `paintEvent`.

### 5.6 `animation` — motion as a specification

```cpp
// md/tokens/motion.h — immutable motion values (no QtWidgets).
namespace md {

struct CubicBezier {
    qreal x1, y1, x2, y2;
    [[nodiscard]] QEasingCurve toEasingCurve() const;
};

struct MotionSpec {
    std::chrono::milliseconds duration{200};
    CubicBezier easing{0.2, 0.0, 0.0, 1.0};
    std::chrono::milliseconds delay{0};
};

}

// md/animation/animator.h — the only stateful part of the motion system.
namespace md::animation {

class Animator : public QObject {
    Q_OBJECT
public:
    explicit Animator(QObject* parent = nullptr);
    void start(MotionSpec spec, std::function<void(qreal)> onValue);
    void cancel();
    [[nodiscard]] bool isRunning() const noexcept;
signals:
    void finished();
};

}
```

The value types live in `tokens` so that `theme`, which bundles a `MotionScheme`, never
depends on `animation`; only `Animator` is stateful and belongs to `animation`.

Behavioural rules:

- **Interruptible by default.** Starting an animation on a property that is already animating retargets from the current value; it never jumps to the start value.
- **Frame-driven, not timer-driven by components.** One `QVariantAnimation` per property; components do not own `QTimer`s.
- **Reduced motion is honoured centrally.** When the platform reports that animations are disabled, `Animator` jumps to the end state and emits `finished()` on the next event-loop turn.
- **Duration tokens are semantic.** Call sites use `MotionScheme::short4` or `MotionScheme::medium2`, never literal milliseconds, so that a future token revision is a one-file change.

### 5.7 `view` — the component kernel

`View` is the base class of every Periodicity widget and the single place where painting, state and input are orchestrated.

```cpp
namespace md {

class View : public QWidget {
    Q_OBJECT
public:
    explicit View(QWidget* parent = nullptr);
    ~View() override;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

    // Measure contract (§6.2); the QLayout bridge calls these (§6.3).
    void measure(MeasureSpec width, MeasureSpec height);
    void arrange(const QRect& geometry);
    [[nodiscard]] QSizeF measuredSize() const noexcept { return measuredSize_; }

    [[nodiscard]] ViewStates state() const noexcept;
    void setState(ViewState flag, bool enabled);

    void setBackground(std::unique_ptr<drawable::Drawable> background);
    void addForegroundLayer(std::unique_ptr<layer::Layer> foreground);
    void setElevation(Elevation level);
    void setShape(Shape shape);

protected:
    // Measure contract (§6.2). Implementations set measuredSize_.
    virtual void measureOverride(MeasureSpec width, MeasureSpec height) = 0;
    virtual void arrangeOverride(const QRectF& content) { Q_UNUSED(content); }
    virtual void paintContent(QPainter& painter, const QRectF& content) = 0;

    void paintEvent(QPaintEvent* event) final;
    void resizeEvent(QResizeEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void changeEvent(QEvent* event) override;

    [[nodiscard]] QRectF contentRect() const noexcept;
    [[nodiscard]] virtual bool focusable() const { return isEnabled(); }
    void drawFocusIndicator(QPainter& painter, const QRectF& content) const;

private:
    mutable QSizeF measuredSize_;   // cache written by the const sizeHint() path
    Shape shape_{Shape::small()};
    Elevation elevation_{Elevation::Level0};
    ViewStates state_{ViewState::Enabled};
    std::unique_ptr<drawable::Drawable> background_;
    std::unique_ptr<layer::ElevationLayer> elevationLayer_;
    std::vector<std::unique_ptr<layer::Layer>> foreground_;
};

}
```

`paintEvent` is `final` deliberately. It performs the whole pipeline and calls the virtual hooks; a component cannot skip the state layer, the focus ring or the shadow. The template is:

```cpp
void View::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const QRectF content = contentRect();
    const SurfaceContext context{Theme::forWidget(this), state_, shape_, elevation_};

    if (elevation_ != Elevation::Level0)
        elevationLayer_->paint(painter, content, context);   // shadow, outside bounds

    if (background_)
        background_->draw(painter, content);

    paintContent(painter, content);

    for (const auto& foreground : foreground_)
        foreground->paint(painter, content, context);

    if (hasFocus() && focusable())
        drawFocusIndicator(painter, content);
}
```

`ViewGroup` extends `View` with children and arrangement, but does **not** replace `QLayout`; it delegates to an `md::layout::Layout` instance (§6.3).

### 5.8 `layout` — containers and adaptivity

`layout::Layout` derives from `QLayout` and bridges Qt's geometry system to the measure contract. `LinearLayout` (row/column), `FrameLayout` (stacking with gravity), `GridLayout` (column count from the size class) and `ScrollView` (a `QAbstractScrollArea` with Material scrollbars) are the planned containers.

Adaptivity is expressed through `WindowSizeClass`, which is a **container** query, not a screen query: a component asks for the width class of its own available space, so that a narrow pane inside a wide window still behaves compactly.

### 5.9 `components` — the public widgets

A component is a `View` subclass plus:

1. an anatomy declaration (slots, paddings, minimum touch target),
2. the token roles it consumes,
3. the states it supports,
4. its accessibility role and actions,
5. its RTL behaviour.

Naming: `md::Button`, `md::FilledButton`? No — one class with an enum variant is preferred over a class explosion:

```cpp
enum class ButtonVariant { Filled, FilledTonal, Elevated, Outlined, Text };

class Button : public View {
    Q_OBJECT
    Q_PROPERTY(ButtonVariant variant READ variant WRITE setVariant)
public:
    explicit Button(const QString& text, QWidget* parent = nullptr);
    void setIcon(const QIcon& icon);
    void setVariant(ButtonVariant variant);
signals:
    void clicked();
};
```

Component catalogue, ordered by dependency (each row depends only on rows above it):

| Tier | Components | Depends on |
| --- | --- | --- |
| 0 | `Icon`, `Divider`, `Badge`, `TouchTarget` helpers | tokens, drawable |
| 1 | `Button`, `IconButton`, `FloatingActionButton`, `Chip`, `Checkbox`, `RadioButton`, `Switch`, `Slider`, `ProgressIndicator` | tier 0, view, animation |
| 2 | `Card`, `ListItem`, `TextField` (filled/outlined), `SegmentedButton`, `Tabs` | tier 1 |
| 3 | `TopAppBar`, `NavigationBar`, `NavigationRail`, `NavigationDrawer` | tier 2, layout |
| 4 | `Dialog`, `Menu`, `Snackbar`, `Tooltip`, `BottomSheet` | tier 3, overlay windows |
| 5 | `SearchBar`, `DatePicker`, `TimePicker`, `Carousel` | tier 4 |

This ordering is the recommended implementation sequence; it is not a hard dependency rule (a component may be built earlier if its prerequisites exist).

---

### 5.10 `event` — dispatch and propagation

Events are first-class values rather than raw Qt events: `md::Event` carries a type, a phase,
an accepted flag and a stop-propagation flag, and `md::EventDispatcher` routes it through a
chain of `md::EventTarget`s.

Although this layer sits beneath `view` in the dependency order (it depends only on `core`),
it is documented last so that the existing section numbers stay stable.

```cpp
namespace md {

enum class EventType {
    None, PointerDown, PointerUp, PointerMove, PointerEnter, PointerLeave,
    Wheel, KeyDown, KeyUp, FocusIn, FocusOut
};

class Event
{
public:
    explicit Event(EventType type, std::chrono::milliseconds timestamp = {});

    [[nodiscard]] EventType type() const noexcept;
    [[nodiscard]] EventPhase phase() const noexcept;
    [[nodiscard]] EventTarget* target() const noexcept;

    void accept() noexcept;            // handled: stops the bubble
    void ignore() noexcept;
    [[nodiscard]] bool isAccepted() const noexcept;

    void stopPropagation() noexcept;   // stops capture and bubble immediately
    [[nodiscard]] bool isPropagationStopped() const noexcept;
};

class EventTarget
{
public:
    virtual ~EventTarget() = default;
    [[nodiscard]] virtual EventTarget* parentTarget() const noexcept = 0;
    virtual bool onInterceptEvent(Event& event);   // ancestor claims the gesture
    virtual bool onEvent(Event& event) = 0;
};

class EventDispatcher
{
public:
    // Capture (root -> target), target, bubble (target -> root).
    bool dispatch(Event& event, EventTarget& target);
};

}
```

Propagation rules:

1. **Capture** walks from the root down to the target's parent; a handler may stop propagation
   before the target ever sees the event.
2. **Interception** lets an ancestor claim a gesture before its descendant does — the Android
   `onInterceptTouchEvent` contract. The first ancestor returning `true` becomes the target.
3. **Target** delivers to the target itself.
4. **Bubble** walks from the target's parent up to the root and stops at the first accepted event.
5. `stopPropagation()` is absolute; `accept()` only stops the bubble.

Gestures are recognised from pointer streams by `md::GestureDetector` (tap, long press, drag)
with configurable slop and timing, so components never re-implement threshold logic. The Qt
bridge (`eventFromQMouseEvent`, `eventFromQWheelEvent`, `eventFromQKeyEvent`) is the only place
that knows Qt's event classes and keeps `view` free of that translation.

---

### 5.11 `ripple` — pointer-origin feedback

A ripple is a circle that grows from the exact pointer position: a low-opacity one acknowledges
hover, a stronger one acknowledges a press. Each is a **one-shot animation** — it expands until
it covers the whole component, then fades out and is removed. Nothing follows the pointer and no
static circle is left behind. Both are clipped to the widget's shape, so the effect never bleeds
outside a rounded card or button.

```cpp
namespace md::ripple {

struct RippleSpec {
    MotionSpec expansion;                 // radius growth
    MotionSpec fade;                      // fade-out
    qreal hoverOpacity = 0.08;
    qreal pressedOpacity = 0.12;
    qreal hoverFollowThresholdDp = 4.0;
    qreal maxRadiusScale = 1.0;
    static RippleSpec fromTheme(const Theme&);
};

class RippleController : public QObject
{
    Q_OBJECT
public:
    void setTheme(Theme theme);
    void setBounds(const QRectF& bounds);
    void setCornerRadius(qreal radius);

    void pointerEntered(const QPointF& position);
    void pointerMoved(const QPointF& position);
    void pointerPressed(const QPointF& position);
    void pointerReleased(const QPointF& position);
    void pointerLeft();

    void paint(QPainter& painter) const;

signals:
    void repaintRequested();
};

}
```

Rules:

1. **Centred on the pointer, never on the widget.** The maximum radius is the distance to the
   farthest corner of the bounds, so the circle finishes by filling the shape whichever edge the
   pointer entered from.
2. **One shot, not a marker.** A hover entry plays a single expansion and fade; `pointerMoved`
   does nothing. The feedback acknowledges the interaction rather than painting something that
   trails the pointer.
3. **Opacity comes from tokens.** Hover uses the hover state-layer opacity and press uses the
   pressed opacity; no component hard-codes an alpha value.
3. **The controller is widget-agnostic.** The host forwards pointer positions and calls `paint`
   from its own paint pipeline; `repaintRequested` tells the host when to `update()`.
4. **Clipping follows the shape.** Rounded geometry clips the ripple, so a card's corner stays
   clean rather than showing a square flash.

### 5.12 `schedule` — task priority queue

Every deferred action — a repaint requested by an animation, a hover notification, a background
icon decode — goes through one queue per thread instead of being posted to the event loop
directly. That makes "respond to the user first" a property of the framework rather than a habit
each component has to remember.

| Priority | Value | Typical work |
| --- | ---: | --- |
| `UserInput` | 0 | press, click, key activation, scroll, ripple start |
| `UiUpdate` | 1 | layout and paint invalidation |
| `UiEvent` | 2 | hover, synthetic notifications, animation ticks |
| `Background` | 3 | decoding, prefetching, cache warming |
| `Idle` | 4 | cleanup, metrics |

```cpp
namespace md::schedule {

enum class Priority : quint8 { UserInput, UiUpdate, UiEvent, Background, Idle };

class TaskQueue : public QObject
{
    Q_OBJECT
public:
    explicit TaskQueue(QObject* parent = nullptr);
    [[nodiscard]] static TaskQueue& main();      // the GUI-thread instance

    void post(Priority priority, std::function<void()> task);
    void postCoalesced(Priority priority, quint64 key, std::function<void()> task);
    void cancel(quint64 key);

    [[nodiscard]] bool isEmpty() const noexcept;
    [[nodiscard]] int pending(Priority priority) const noexcept;

    void setBatchTimeBudget(std::chrono::microseconds budget);
    int flush(int maxTasks = 256);
signals:
    void drained();
};

}
```

Rules:

1. **Priority order, re-checked after every task.** A user task posted while a UI batch is
   draining runs before the rest of that batch; the queue never finishes a low-priority sweep
   first out of politeness.
2. **Coalescing by key.** Repeated invalidation or repaint requests for the same target collapse
   into one pending task, so a burst of property changes paints once.
3. **Budgeted flush.** A flush stops after its time budget and reschedules itself, which bounds
   the latency a background burst can add to input.
4. **Reentrancy-safe.** Posting from inside a task appends to the appropriate queue; it never
   recurses into the drain loop.
5. **One queue per thread.** Widgets use `TaskQueue::main()`; workers may own their own.

### 5.13 `pool` — object storage, threads, buffers and process channels

The widget path is single-threaded; the work feeding it is not. This layer is the headless set
of primitives that makes off-thread work possible without inventing a threading model per
feature, and it recycles storage instead of allocating per use. It depends on `core` only: the
`ObjectPool` that the thread pool recycles its task nodes from lives in the same layer.

```cpp
md::pool::ObjectPool<Node> nodes;                        // storage recycled, T fresh
md::pool::ThreadPool pool({.workers = 4});

pool.post([]{ /* off the paint thread */ });
auto future = pool.submit([] { return 42; });

md::pool::SpscRingBuffer<std::uint64_t> ring(8192);      // lock-free, two threads
md::pool::Channel<Job> channel(64);                      // closeable, N producers
md::pool::IoThread io;                                   // deadlines, not frames
io.postAfter(250ms, task);

mailbox.post([this] { label->setText(...); });           // worker -> UI thread
```

Rules:

1. **The UI thread never blocks on work.** `post()` takes one mutex and pushes one pointer; a
   full queue rejects and is counted rather than growing without bound.
2. **A pool recycles storage, not lifetime.** `ObjectPool::create()` constructs a fresh `T`;
   `destroy()` runs its destructor and returns the block. Nothing may be live when the pool dies.
3. **Every ring has one producer and one consumer.** `SpscRingBuffer` publishes each cursor with
   a release store; a full ring drops and counts, it never overwrites.
4. **Close is the termination signal.** `Channel::close()` wakes every waiter, fails further
   sends and lets receivers drain what is already queued.
5. **A timer is measured from when it became due.** `IoThread` sleeps until the earliest
   deadline, so an idle IO thread costs nothing and a late task is visible in the statistics.
6. **A worker returns through the mailbox.** It posts a closure, not a widget pointer; the
   wake-up is coalesced, so a burst costs one event-loop turn.
7. **Both IPC transports frame their own messages.** Pipes carry a 4-byte length and a 1 MiB
   cap; the shared ring stores whole frames behind two atomic cursors, so a partial message is
   never observable.

The gallery's **Pool** page drives all of it live: submit jobs, churn an object pool, start
the ring and channel pairs, read a file on the IO thread, and spawn `periodicity_ipc_echo` to
trade PING/PONG over a pipe while it streams TICK frames through shared memory.

---

## 6. Core abstractions in detail

### 6.1 The three contracts

Every component participates in exactly three contracts:

1. **Measurement** — how large do I want to be, given a constraint?
2. **Arrangement** — where do my children go, given a rectangle?
3. **Painting** — what do I draw, in which order?

Everything else (state, animation, theming) is infrastructure that feeds these three.

### 6.2 Measurement

```cpp
class MeasureSpec {
public:
    enum class Mode { Unspecified, Exactly, AtMost };

    constexpr MeasureSpec(qreal size, Mode mode) : size_(size), mode_(mode) {}

    static constexpr MeasureSpec exactly(qreal size) { return {size, Mode::Exactly}; }
    static constexpr MeasureSpec atMost(qreal size) { return {size, Mode::AtMost}; }
    static constexpr MeasureSpec unspecified() { return {0.0, Mode::Unspecified}; }

    [[nodiscard]] constexpr qreal size() const noexcept { return size_; }
    [[nodiscard]] constexpr Mode mode() const noexcept { return mode_; }

    [[nodiscard]] qreal resolve(qreal desired) const noexcept;

private:
    qreal size_ = 0.0;
    Mode mode_ = Mode::Unspecified;
};
```

`resolve` applies the standard clamp: `Exactly` returns `size_`, `AtMost` returns `min(desired, size_)`, `Unspecified` returns `desired`.

Widgets implement `measureOverride` by computing `measuredSize_` from child measurements and their own padding. The base class then answers Qt's geometry questions:

- `sizeHint()` → a measurement pass with unspecified width and height, reusing the cached result
- `minimumSizeHint()` → a measurement pass with `atMost(0)` on both axes, using minimum metrics
- `heightForWidth(w)` → a measurement pass with `exactly(w)` on the width and an unspecified height

### 6.3 The Qt layout bridge

Qt's `QLayout` owns geometry; Periodicity's `Layout` adapter translates:

```cpp
void Layout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    applyGeometry(rect);          // the subclass computes each item rectangle
}

// Called by every concrete Layout once it knows where an item belongs.
void Layout::placeItem(QLayoutItem* item, const QRect& rect)
{
    if (auto* view = qobject_cast<View*>(item->widget())) {
        view->measure(MeasureSpec::exactly(rect.width()),
                      MeasureSpec::exactly(rect.height()));
        view->arrange(rect);
    } else {
        item->setGeometry(rect);  // stock Qt widget: Qt's own geometry path
    }
}
```

Consequences:

- Periodicity widgets work inside stock `QVBoxLayout`/`QGridLayout`, because they still answer `sizeHint()` and `heightForWidth()`.
- Stock Qt widgets work inside Periodicity layouts, because the adapter falls back to `QLayoutItem::setGeometry`.
- Components never call `setGeometry` on their children directly; they go through the adapter.

### 6.4 State

```cpp
enum class ViewState : quint32 {
    Enabled   = 1u << 0,
    Hovered   = 1u << 1,
    Focused   = 1u << 2,
    Pressed   = 1u << 3,
    Dragged   = 1u << 4,
    Selected  = 1u << 5,
    Checked   = 1u << 6,
    Invalid   = 1u << 7,
    ReadOnly  = 1u << 8,
};
Q_DECLARE_FLAGS(ViewStates, ViewState)
Q_DECLARE_OPERATORS_FOR_FLAGS(ViewStates)
```

State transitions are driven by Qt events in `View` and are the only source of state-layer changes:

| Event | Transition |
| --- | --- |
| `QEvent::Enter` | `Hovered` on |
| `QEvent::Leave` | `Hovered` off, `Pressed` off |
| `QEvent::MouseButtonPress` (left) | `Pressed` on; ripple starts |
| `QEvent::MouseButtonRelease` | `Pressed` off; `clicked()` if inside |
| `QEvent::FocusIn` / `FocusOut` | `Focused` on/off |
| `QEvent::KeyPress` (Space/Return) | `Pressed` on |
| `QEvent::KeyRelease` | `Pressed` off; `clicked()` |
| `QEvent::EnabledChange` | `Enabled` recomputed |
| Drag threshold exceeded | `Pressed` off, `Dragged` on |

The mapping lives in one place (`View`), not in each component. Components react by overriding `stateChanged(ViewStates, ViewStates)` and invalidating the layers they own.

### 6.5 Painting

The order is fixed (§5.7). Two rules make it predictable:

1. **A child paints after its parent's background but before its parent's foreground.** `ViewGroup::paintEvent` paints background layers, then children, then foreground layers.
2. **Hit testing follows the reverse order** — the topmost layer that claims a hit wins, then children in reverse z-order.

---

## 7. Rendering pipeline

### 7.1 Repaint discipline

- State changes call `update()` on the affected rectangle, not on the whole widget. A ripple repaints its own bounding box; a hover repaints the background rect.
- `View` sets `Qt::WA_OpaquePaintEvent` only when the background is fully opaque; otherwise it sets `Qt::WA_TranslucentBackground` and expects the parent to have painted underneath.
- `Qt::WA_StaticContents` is not used: component content is theme- and state-dependent, and partial repaint correctness is not worth the risk.
- No allocation in `paintEvent` in the steady state: paths, fonts and pixmaps are cached; `QPainterPath` construction is hoisted into `shapeChanged()`.

### 7.2 Device pixel ratio

All painter work is in logical coordinates; Qt applies the device pixel ratio. Pixmap caches must, however, be created at `devicePixelRatioF()` and tagged with `QPixmap::setDevicePixelRatio`, or they will be resampled and look soft on 150 % displays.

### 7.3 Shadows

`ShadowDrawable` renders a blurred alpha mask of the shape and caches it:

- The mask is generated at device resolution: render the shape path in black, then apply a three-pass separable box blur approximating a Gaussian.
- The cache key is `(shape hash, corner radii, elevation level, size bucket, dpr)`. Sizes are bucketed to 8 dp steps to bound the cache; the mask is stretched with a nine-slice when the surface is larger than the bucket.
- Shadows are never clipped by the widget's own rect: `View` reserves a shadow inset in its geometry when `elevation > 0`.
- `QGraphicsDropShadowEffect` is prohibited: it forces an offscreen render of the whole widget, does not compose with `QPainter` clipping, and is a performance trap in lists.

### 7.4 Tonal elevation

Elevation changes the surface colour as well as the shadow. `ColorScheme::surfaceAtElevation(level)` returns a colour from the neutral tonal palette; the background layer fills with it. The implementation must **not** composite a white overlay at a fixed alpha, because that breaks in dark themes and in high-contrast modes.

### 7.5 Text

Text is laid out with `QTextLayout`, not `QPainter::drawText` with a rectangle, whenever wrapping or ellipsis is required. Line height comes from the type token; baseline offsets are applied explicitly. Letter spacing is applied through `QFont::setLetterSpacing(QFont::AbsoluteSpacing, trackingPx)`.

### 7.6 Icons

Icons are `QPixmap` masks tinted at paint time with `CompositionMode_SourceIn` into a cached pixmap keyed by `(icon id, colour, dpr, direction)`. RTL mirroring is opt-in per icon via a `Mirrored` flag, because most Material Symbols are direction-agnostic while arrows and chevrons are not.

---

## 8. Input, focus and accessibility

- **Hover** requires `setAttribute(Qt::WA_Hover)`; without it, `QEvent::Enter`/`Leave` are not delivered on all platforms.
- **Touch targets** are at least 48 × 48 dp even when the visual is smaller; the extra area is transparent padding, not layout size.
- **Focus** follows `Qt::StrongFocus`; the focus indicator is a 2 dp outline drawn outside the shape, with a 2 dp offset, in `secondary`. It is drawn by `View`, so a component cannot omit it.
- **Keyboard activation** is Space and Return for buttons, arrow keys for radio groups and sliders, Tab/Shift+Tab for traversal. Focus order is the child order unless a component overrides `focusNextPrevChild`.
- **Accessibility** is implemented per component by subclassing `QAccessibleWidget` and registering a factory in the component's translation unit. Roles map to `QAccessible::Role::Button`, `CheckBox`, `RadioButton`, `Slider`, `ComboBox`, `EditableText`, `ListItem`, `PageTab`, `Dialog`. Names come from the visible label or `accessibleName`; states mirror `ViewStates`; actions map `clicked` and `toggle`.
- **Reduced motion** is a single flag on `MotionScheme`; when set, `Animator` snaps to the final value.

---

## 9. Layout and adaptivity

### 9.1 Size classes

| Class | Width | Columns | Margin | Gutter |
| --- | --- | ---: | ---: | ---: |
| Compact | < 600 dp | 4 | 16 dp | 16 dp |
| Medium | 600–839 dp | 8 | 24 dp | 16 dp |
| Expanded | 840–1199 dp | 12 | 24 dp | 24 dp |
| Large | 1200–1599 dp | 12 | 24 dp | 24 dp |
| Extra-large | ≥ 1600 dp | 12 | 24 dp | 24 dp |

Breakpoints are verified against the Android window size class documentation. The column, margin and gutter values are the M3 canonical layout guidance and are marked **provisional**: they will be locked in Phase 3 together with the layout tokens.

### 9.2 Adaptive behaviour

Components respond to the size class of their own available width, not to the window. A `NavigationRail` inside a narrow pane collapses to a `NavigationBar`; a two-pane layout switches to a single pane below 840 dp. The query is `WindowSizeClass::forWidth(availableWidth())`, evaluated during measure.

### 9.3 RTL

All horizontal metrics are expressed with start/end semantics in the layout layer and resolved through `QWidget::isRightToLeft()`. Shapes carry per-corner radii, so a menu with a rounded start edge mirrors correctly without a transform. Icons opt into mirroring individually.

---

## 10. Theming and Qt integration

### 10.1 Theme propagation

`Theme::forWidget` walks the parent chain. A `ThemeScope` is a `QObject` child of a widget that holds a `Theme` value; `ThemeScope::install(widget, theme)` creates it. Switching a theme is a value assignment followed by a `QEvent::ThemeChange` delivered to the subtree, which invalidates caches and repaints.

### 10.2 Light and dark

The application theme follows `QStyleHints::colorScheme()` (Qt 6.5+) unless the integrator pins it. Because the scheme is a value, a pinned theme and a system-following theme differ only in which `ColorScheme` factory is called.

### 10.3 Qt palette bridge

Periodicity widgets do not use `QPalette` for their own painting. They do, however, publish a derived `QPalette` on the application so that stock Qt widgets (menus, tooltips, message boxes, native dialogs) are visually consistent. The mapping is one-way: theme → palette. A user-supplied palette does not flow back into the Material theme.

### 10.4 Style sheets

QSS applied to a Periodicity widget is unsupported: the widget paints itself and ignores the style's background, border and font. QSS continues to work on stock Qt widgets, and a stylesheet on a parent does not leak into Periodicity children beyond inherited fonts. This restriction is documented in the README rather than enforced at runtime.

### 10.5 Fonts

Roboto is the reference family and is Apache-2.0 licensed, so it may be bundled with a NOTICE file. Bundling is recommended for reproducible tests and identical metrics across platforms. For scripts not covered by Roboto (CJK, Arabic, Hebrew, Devanagari), the font stack falls back to platform UI families through `QFont::setFamilies`. Icon glyphs come from Material Symbols (also Apache-2.0), shipped as a bundled font or as SVG assets; the choice is deferred to Phase 1.

### 10.6 Native surfaces

Menus, tooltips and native file dialogs may remain native where Material defines no equivalent, provided they inherit the palette bridge. Modal Material dialogs are `QDialog` subclasses with a Periodicity scrim, not `QMessageBox`.

---

## 11. Cross-cutting concerns

| Concern | Policy |
| --- | --- |
| Threading | All widgets and painting on the GUI thread. Image decoding may be offloaded; results are delivered via queued signals. Tokens are immutable, so they may be read from any thread. |
| Logging | `QLoggingCategory`: `md.core`, `md.paint`, `md.layout`, `md.theme`. Categories are declared once and referenced, never string-built. |
| Assertions | `Q_ASSERT` for internal invariants; `Q_ASSERT_X` with a message for token validity (for example, a scheme whose `onPrimary` equals `primary`). |
| Errors | No exceptions across Qt boundaries. Parsing (future theme files) returns `std::optional`/`std::expected`-style results. |
| Memory | Widgets use Qt parent ownership; drawables and layers are `std::unique_ptr` owned by their `View`; tokens are values. |
| Internationalisation | All user-visible strings via `tr()`; no concatenated sentences; plural forms via `QCoreApplication::translate` with `%n`. |
| High contrast | A high-contrast scheme variant is planned for Phase 6; components must not hard-code colours, so the change is confined to tokens. |
| Determinism | Golden tests force `QT_SCALE_FACTOR=1`, `QT_QPA_PLATFORM=offscreen` and bundled fonts. |

---

## 12. Repository and build structure

### 12.1 Target layout

```
periodicity/
├── CMakeLists.txt              # top level: find_package(Qt6), add_subdirectory per module
├── CMakePresets.json           # extended with library and test presets
├── core/                       # md::core
├── schedule/                   # md::schedule    (task priority queue)
├── tokens/                     # md::tokens
├── theme/                      # md::theme
├── event/                      # md::event       (dispatch, propagation, gestures)
├── drawable/                   # md::drawable
├── layer/                      # md::layer       (layer.h, background.h, foreground.h, elevation.h)
├── animation/                  # md::animation
├── ripple/                     # md::ripple      (pointer-origin hover and press feedback)
├── view/                       # md::view
├── layout/                     # md::layout
├── components/                 # md::components
├── demo/                       # documentation application (periodicity_demo)
├── tests/                      # unit, golden, interaction, a11y
├── tools/                      # layering check, token export, image diff
└── docs/                       # architecture notes; this file lives at the root
```

Each module follows the same internal shape:

```
<module>/
├── CMakeLists.txt
├── include/md/<module>/*.h     # public headers, installed
└── src/*.cpp                   # implementations, private
```

### 12.2 CMake targets

| Target | Type | Links |
| --- | --- | --- |
| `periodicity::core` | STATIC | `Qt6::Core`, `Qt6::Gui` |
| `periodicity::pool` | STATIC | `periodicity::core`, `Qt6::Core`, `Threads::Threads` |
| `periodicity::schedule` | STATIC | `periodicity::core`, `Qt6::Core` |
| `periodicity::tokens` | STATIC | `periodicity::core` |
| `periodicity::theme` | STATIC | `periodicity::core`, `periodicity::tokens` |
| `periodicity::event` | STATIC | `periodicity::core`, `Qt6::Core`, `Qt6::Gui` |
| `periodicity::drawable` | STATIC | core, tokens, theme, `Qt6::Gui` |
| `periodicity::layer` | STATIC | core, tokens, theme, drawable, ripple |
| `periodicity::animation` | STATIC | core, tokens, `Qt6::Core` |
| `periodicity::ripple` | STATIC | core, tokens, theme, animation, `Qt6::Gui` |
| `periodicity::view` | STATIC | core, tokens, theme, event, drawable, layer, animation, `Qt6::Widgets` |
| `periodicity::layout` | STATIC | view |
| `periodicity::components` | STATIC | all of the above |
| `periodicity::periodicity` | INTERFACE | `periodicity::components` (umbrella) |
| `periodicity::gallery` | EXECUTABLE | umbrella |
| `periodicity_ipc_echo` | EXECUTABLE | `periodicity::pool`, `Qt6::Core` |

Every target sets `target_compile_features(cxx_std_20)` and `target_include_directories` with `$<BUILD_INTERFACE:...>` / `$<INSTALL_INTERFACE:...>`. Implementation-only dependencies are `PRIVATE`; this is what makes the dependency matrix enforceable.

### 12.3 Compiler settings

- MSVC: `/W4 /permissive- /Zc:__cplusplus /utf-8`; warnings as errors in CI for the library targets.
- GCC/Clang: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` (conversion warnings may be relaxed where Qt forces narrowing).
- `QT_NO_CAST_FROM_ASCII` and `QT_NO_CAST_TO_ASCII` are on, so string literals must be explicit `QStringLiteral`/`u""_s`.
- C++20 designated initialisers are used for token aggregates; no `-fno-exceptions`.

### 12.4 Migration from the current scaffold

The repository currently contains a Visual Studio CMake template: a `main()` that prints a greeting, four empty headers under `layer/`, and an `add_executable` that lists them. Phase 0 replaces the executable with the gallery app and moves `layer/*.h` into the `layer` module's public include directory. The existing `CMakePresets.json` windows presets are retained and extended rather than rewritten.

---

## 13. Dependency rules

### 13.1 Matrix

Rows are consumers; columns are dependencies. A tick means the include and the link are permitted.

| ↓ depends on → | core | schedule | tokens | theme | event | ripple | drawable | layer | animation | view | layout | components |
| --- | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: |
| **core** | — | | | | | | | | | | | |
| **schedule** | ✓ | — | | | | | | | | | | |
| **tokens** | ✓ | | — | | | | | | | | | |
| **theme** | ✓ | | ✓ | — | | | | | | | | |
| **event** | ✓ | | | | — | | | | | | | |
| **ripple** | ✓ | | ✓ | ✓ | | — | | | | | | |
| **drawable** | ✓ | | ✓ | ✓ | | | — | | | | | |
| **layer** | ✓ | | ✓ | ✓ | | ✓ | ✓ | — | | | | |
| **animation** | ✓ | | ✓ | | | | | | — | | | |
| **view** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | | |
| **layout** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | |
| **components** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |

`structure`, `store` and `pool` are headless side layers, left out of the matrix for width:
`structure` and `pool` depend on `core` only, and none of them may include a token, a widget or
a painted surface.

### 13.2 Forbidden dependencies

| Forbidden | Why |
| --- | --- |
| `tokens` → `QWidget`, `QPainter` | Tokens must remain usable headlessly and in tools. |
| `view` → `components` | A base class that knows its subclasses cannot be extended without modification. |
| `event` → `QWidget`, `QPainter` | Dispatch must stay usable by tests and tools without a widget tree. |
| `pool` → `QWidget`, `QPainter`, `QtWidgets` | A worker must never touch a widget, and the layer must stay usable by headless tools and tests. |
| `drawable` → `view` | Drawables are geometry-agnostic painting primitives. |
| Any module → `apps/`, `tests/` | Applications and tests are leaves. |
| Public headers → `detail/` headers | `detail/` is an implementation detail with no stability guarantee. |
| Any module → a Qt private header (`QtWidgets/private/...`) | Portability and source compatibility across Qt patch releases. |

### 13.3 Enforcement

1. **Target boundaries.** Each module links only what the matrix permits, and only target-level includes are exported. An illegal `#include` fails to compile because the header directory is not on the include path.
2. **Layering check.** `cmake -P tools/check_layering.cmake` walks every public header, extracts `#include <md/...>` lines and compares the includer's module with the matrix. CI fails on a violation, including in pull requests.
3. **Header hygiene.** Public headers must compile standalone: a generated target compiles each header in isolation with only its permitted dependencies.
4. **Review rule.** A new module requires an update to this document and to the matrix before it is added to the build.

---

## 14. Testing strategy

| Level | Scope | Tooling |
| --- | --- | --- |
| Unit | Token maths (`surfaceAtElevation`, state-layer compositing, `MeasureSpec::resolve`), geometry helpers | Qt Test, data-driven rows |
| Layout | Measure/arrange for each container at the five size classes, RTL, `heightForWidth` | Qt Test, assertions on rectangles |
| Interaction | Event → state → visual outcome: hover, press, focus, keyboard activation, drag | `QTest::mouseClick`, `QTest::keyClick`, `QSignalSpy` |
| Golden image | Component rendering at DPR 1 and 2, light and dark | `QWidget::grab()` into `QImage`, tolerant pixel comparison |
| Accessibility | Role, name, state, action for every interactive component | `QAccessibleInterface` queries |
| Performance | Paint time per component, cache hit ratio, construction cost | Benchmarks with a fixed scene, asserted against budgets |

Determinism rules for golden tests: `QT_QPA_PLATFORM=offscreen`, `QT_SCALE_FACTOR=1`, bundled Roboto, fixed locale (`en_GB`), and a fixed palette. Image comparison allows a per-channel delta of 2 and a mismatch ratio of 0.1 %, to absorb anti-aliasing differences between platforms; the tolerant comparator lives in `tools/`.

---

## 15. Performance budgets

| Metric | Budget | Notes |
| --- | --- | --- |
| Paint, single component | ≤ 4 ms at 1080p, DPR 1 | Measured with `QElapsedTimer` around `grab()` in a benchmark scene |
| Frame budget | 16.6 ms | 60 Hz target; 120 Hz is a stretch goal |
| Shadow cache hit rate | ≥ 95 % in a scrolling list | Misses trigger a rebuild, not a per-frame blur |
| Widget construction | ≤ 1 ms for a simple component | Tokens are values, no file I/O |
| Theme switch | ≤ 16 ms for a 200-widget scene | Invalidate caches, single repaint pass |
| Allocations in paint | 0 steady state | Verified by a counting allocator in debug |

---

## 16. Roadmap

| Phase | Deliverable | Exit criterion |
| --- | --- | --- |
| 0 | Build skeleton: targets, presets, layering check, CI | `cmake --preset x64-debug` configures and builds an empty gallery; layering check passes |
| 1 | `core`, `tokens`, `theme`, `drawable`, `layer` | Baseline light/dark schemes render; shadow and ripple drawables verified by golden images |
| 2 | `view`, `animation`, `layout` | A hand-built button responds to hover/press/focus with correct state layers and motion |
| 3 | Tier 0–1 components | Button, icon button, FAB, chip, checkbox, radio, switch, slider, progress |
| 4 | Tier 2 components | Card, list item, text field, tabs, segmented button |
| 5 | Tier 3–4 components and adaptivity | App bar, navigation bar/rail/drawer, dialogs, menus, snackbar, tooltip, bottom sheet; size-class behaviour verified |
| 6 | Accessibility audit, high contrast, RTL audit, dynamic colour exploration | Axe-style manual audit passes; RTL golden images; dynamic colour prototype |

Phases overlap in practice; the exit criteria are the gates, not the calendar.

---

## 17. Assumptions, risks and open questions

### 17.1 Assumptions

- **A-1** Material 3 baseline is the target; the Material-version question was not answered and dynamic colour / Expressive are deferred.
- **A-2** Component priorities follow the dependency order in §5.9; the component-selection question was not answered.
- **A-3** Qt 6.8 LTS is the minimum; no compatibility layer for Qt 5 is required.
- **A-4** Windows is the first platform, but no Windows-only API may leak outside the platform adapter.

### 17.2 Risks

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Specification drift (M3 Expressive) | Token tables age | Tokens are isolated; expressive variants already exist in the reference export and can be added as new tokens |
| Cross-platform font metrics | Golden images differ | Bundle Roboto; tolerant comparison; per-platform baselines only where justified |
| Shadow blur cost | Jank in lists | Cache and nine-slice; budget asserted in benchmarks |
| Text-field editing complexity | Largest single component | Build on `QLineEdit`/`QTextEdit` internals only through composition, not subclassing Qt's private editing code |
| `QStyle` interplay | Stock widgets look inconsistent | Palette bridge and documented QSS restriction |
| Fractional scaling (125 %, 150 %) | Blurry edges | Cache at DPR; snap strokes to half-pixels where necessary |

### 17.3 Open questions

| ID | Question | Default if unresolved |
| --- | --- | --- |
| Q-1 | Confirm the M3 baseline, or commit to dynamic colour / Expressive | Baseline |
| Q-2 | Confirm `1 dp = 1 DIP` at scale 1.0 rather than Android's 160 dpi definition | As documented in AD-3 |
| Q-3 | Bundle Roboto and Material Symbols, or rely on system fonts | Bundle, for determinism |
| Q-4 | Should Periodicity support QSS on its own widgets at all | No |
| Q-5 | Is `md::` the final namespace, or `periodicity::` | `md::` |
| Q-6 | Minimum Windows version and MSVC toolset | Windows 10 1809, MSVC 2022 |
| Q-7 | Are stock Qt menus acceptable for `Menu`, or must it be fully custom | Custom for Material menus, native for context menus on request |

---

## 18. References

Sources consulted through the local proxy (`curl -x http://127.0.0.1:15236`) while preparing this document:

1. Material Design 3 in Compose — <https://developer.android.com/develop/ui/compose/designsystems/material3>
2. Material theming for Views — <https://developer.android.com/develop/ui/views/theming/look-and-feel>
3. Window size classes — <https://developer.android.com/develop/ui/views/layout/use-window-size-classes>
4. Material Components for Android, colour roles — <https://github.com/material-components/material-components-android/blob/master/docs/theming/Color.md>
5. Compose Material 3 tokens, motion — <https://github.com/androidx/androidx/blob/androidx-main/compose/material3/material3/src/commonMain/kotlin/androidx/compose/material3/tokens/MotionTokens.kt>
6. Compose Material 3 tokens, shape — <https://github.com/androidx/androidx/blob/androidx-main/compose/material3/material3/src/commonMain/kotlin/androidx/compose/material3/tokens/ShapeTokens.kt>
7. Compose Material 3 tokens, elevation — <https://github.com/androidx/androidx/blob/androidx-main/compose/material3/material3/src/commonMain/kotlin/androidx/compose/material3/tokens/ElevationTokens.kt>
8. Compose Material 3 tokens, type scale — <https://github.com/androidx/androidx/blob/androidx-main/compose/material3/material3/src/commonMain/kotlin/androidx/compose/material3/tokens/TypeScaleTokens.kt>
9. Material Web state tokens — <https://github.com/material-components/material-web/blob/main/tokens/versions/v0_192/_md-sys-state.scss>
10. Material Design 3 specification — <https://m3.material.io> (JavaScript-only client; not machine-readable through the proxy)

---

## Appendix A — Naming conventions

| Kind | Convention | Example |
| --- | --- | --- |
| Namespace | `md`, nested by module | `md::layer::Layer` |
| Class | `PascalCase` | `ColorScheme`, `NavigationRail` |
| Enum class | `PascalCase`, scoped | `ButtonVariant::Filled` |
| Function | `camelCase` | `surfaceAtElevation` |
| Member | trailing underscore | `measuredSize_` |
| Constant | `kPascalCase` | `kMinTouchTargetDp` |
| Macro | `MD_` prefix | `MD_ENABLE_LAYERING_CHECK` |
| Header | `snake_case.h`, `<md/module/name.h>` | `<md/view/view.h>` |
| CMake target | `periodicity::module` | `periodicity::drawable` |

## Appendix B — Glossary

| Term | Meaning in this document |
| --- | --- |
| Token | An immutable design value (colour role, type style, radius, duration, opacity) |
| Role | A semantic colour name whose value depends on the scheme |
| State layer | A translucent overlay applied on interaction, at the §3.6 opacities |
| Drawable | A reusable painting primitive with bounds and state |
| Layer | A stage in the paint pipeline (background, elevation, foreground) |
| MeasureSpec | The constraint passed to a component during measurement |
| Size class | A width bucket (compact … extra-large) used for adaptive layout |
| Gallery | The demo application that hosts every component |
