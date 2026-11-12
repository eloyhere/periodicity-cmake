# Periodicity

English | [简体中文](README.zh-CN.md)

Periodicity is a cross-platform C++20 component library for desktop applications, built on
Qt 6.8 Widgets and a declarative design baseline. Every component paints itself with
`QPainter`: the library uses no Qt Style Sheets and no `.ui` files, and it does not fall back
on the platform style engine. The only things it takes from the host system are the fonts and
the native window frame. Development happens at
[github.com/eloyhere/periodicity-cmake](https://github.com/eloyhere/periodicity-cmake).

The library is arranged as a stack of modules, from units and geometry at the bottom to the
public widgets at the top. [framework.md](framework.md) is the architecture contract: the
layering, each module's responsibilities, the core abstractions and the matrix of permitted
dependencies. Read it before changing a module boundary. The component catalogue is in
[docs/components.md](docs/components.md).

## What the library is

- A C++20 / Qt 6.8 Widgets library. It requires Qt `Core`, `Gui`, `Widgets` and (for
  `Avatar`'s URL loader) `Network`; `OpenGLWidgets` and `LinguistTools` are optional.
- A `QPainter`-only renderer. Every visual is drawn by the component, not styled by the host
  platform, so a layout that looks right on Windows looks right on Linux and macOS too.
- A layered architecture. Each module is one static CMake target, `periodicity_<module>`,
  exported as `periodicity::<module>`, and the build enforces the dependency direction. The
  module order is a build order, not a topological sort: [framework.md](framework.md) §13.1 is
  the authoritative edge list.
- Public headers only. A module's `include/md/<module>/` tree is its installed interface;
  `src/` stays private, and `detail/` headers are not installed.
- One paint path. `View::paintEvent()` is `final` and paints in a fixed order: elevation
  shadow, background, `paintContent()`, foreground layers, focus ring. A component cannot
  skip the state layer or the focus indicator.
- Immutable design tokens. Colour, type, shape, elevation, motion and state are value types;
  a `Theme` is a value bundle and `ThemeScope` resolves it by walking the widget parent chain.
  Dynamic colour and the expressive extensions of the design baseline are deliberately
  deferred.

## Quick start

### Requirements

- Windows x64. The committed presets are guarded by a `Windows` host condition.
- Qt **6.8.3**, MSVC 2022 x64 build, installed at `D:/Qt/6.8.3/msvc2022_64`. The project asks
  for Qt 6.8 or newer; the pinned path is the one the presets use.
- **MSVC 2022** (v143) with the Desktop development with C++ workload, plus the CMake (3.21 or
  newer) and Ninja that ship with Visual Studio. The `vs2022` and `vs2026` presets cover the
  two Visual Studio toolsets.
- A developer prompt, so that `cl.exe` is on `PATH`.

If Qt lives somewhere else, add a `CMakeUserPresets.json` that inherits from `windows-base`
and sets its own `CMAKE_PREFIX_PATH`. The file is ignored by Git, so the committed presets stay
portable.

### Build and test

```bat
cmake --preset x64-debug
cmake --build --preset x64-debug
ctest --preset x64-debug
```

The build tree is `out/build/x64-debug`. `x64-release` compiles the same targets with
optimisation and without debug information. The Visual Studio solution presets are `vs2022`
and `vs2026`; `cmake --build --preset vs2022-debug` builds `periodicity_demo` and its
dependencies, and the demo is the solution's startup project.

### Run the examples

The two example applications build into the same tree:

```bat
out\build\x64-debug\periodicity_gallery.exe
out\build\x64-debug\periodicity_demo.exe
```

`periodicity_gallery` is the interactive test bench: a sidebar switches component families,
a header button toggles light and dark, and every page instantiates real widgets rather than
screenshots. It also supports `--screenshot <dir>` for unattended rendering.

`periodicity_demo` is the documentation-style showcase window. Both executables are deployed
with their Qt runtime by `periodicity_deploy_qt_runtime()`, so they also start from Explorer
or a plain shell.

A third executable, `out\build\x64-debug\periodicity_ipc_echo.exe`, is the child process used
by the gallery's Pool page and by the IPC tests.

## Repository layout

The modules are listed in dependency order, as they appear in the root `CMakeLists.txt`.

| Path | Target | Purpose |
| --- | --- | --- |
| `core/` | `periodicity::core` | L0: density and unit conversion, `MeasureSpec`, `ViewStates`, geometry helpers, logging categories and the i18n facility. No widgets, no painting policy. |
| `mathematic/` | `periodicity::mathematic` | L0: headless signal mathematics — matrices, decompositions, spectra, filters, windows, statistics, and the Hilbert, Laplace, Mellin and Z integral transforms. Standard library only. |
| `structure/` | `periodicity::structure` | L1a: headless nodes, trees, forests, a skip list, allocators, the LRU container family and the table/virtual-source contracts. No widgets, tokens or painters. |
| `store/` | `periodicity::store` | L1b: process-wide registry of named stores; each store keeps its values in a bounded LRU map. Widget-free, Qt Core only. |
| `pool/` | `periodicity::pool` | L1p: object pools, thread pool, SPSC ring buffers, IO thread, cross-thread mailbox and same-machine process IPC. |
| `schedule/` | `periodicity::schedule` | L1: the task priority queue, plus the `UiThread` frame scheduler and the `EventThread` input pipeline. |
| `tokens/` | `periodicity::tokens` | L1: immutable colour schemes, type scale, shape scale, elevation, motion and state-layer values. Never includes Qt Widgets. |
| `theme/` | `periodicity::theme` | L2: the token bundle, scoped resolution, light/dark switching and the `QPalette` bridge. |
| `event/` | `periodicity::event` | L3: event values, capture/intercept/bubble dispatch, gesture recognition and the Qt event bridge. |
| `drawable/` | `periodicity::drawable` | L3a: reusable painting primitives — shape, shadow, icon, state layer and layer drawables. |
| `layer/` | `periodicity::layer` | L3b: composited paint stages — background, elevation, foreground and ripple layers, plus the layer stack. |
| `animation/` | `periodicity::animation` | L3c: the interruptible `Animator`, driven by the motion tokens. |
| `ripple/` | `periodicity::ripple` | L3: pointer-origin one-shot hover and press feedback. |
| `view/` | `periodicity::view` | L4: the component kernel — measure/arrange/paint template, state machine, accessibility, error guard and the optional GPU compositor. |
| `layout/` | `periodicity::layout` | L5: `Linear`, `Frame`, `Constraint`, `AppBar`, `Collapse` and `Coordinator` containers, spacing and window size classes. |
| `components/` | `periodicity::components` | L6: the public widgets, the 25 chart components, `Canvas` and its contexts, and the Clock/Schedule/Scheduler set. |
| `tests/` | — | Qt Test suites, one directory per module, plus the visual-regression baseline under `tests/visual/`. |
| `apps/` | — | `gallery` (interactive test bench) and `ipc_echo` (child process for the Pool page and IPC tests). |
| `demo/` | — | The documentation-style demo window, target `periodicity_demo`. |
| `tools/` | — | The branding and layering gates (`check_branding.cmake`, `check_layering.cmake`) and `visual_regression.py`. |
| `translations/` | — | `periodicity_zh_CN.ts`, compiled into the catalogue embedded under `:/i18n`. |

Each module has the same internal shape:

```
<module>/
├── CMakeLists.txt
├── include/md/<module>/*.h
└── src/*.cpp
```

## Feature highlights

### Components and charts

- **112 public headers** under `components/include/md/components/`. They cover basic widgets,
  inputs and pickers, dates and cascades, forms, display and navigation, media, overlays,
  data tables and virtualised sources, containers, time and scheduling, and the chart stack.
- **25 chart components**, all deriving from `ChartBase`: the nine general charts (`LineChart`,
  `ScatterChart`, `PieChart`, `BarChart`, `HistogramChart`, `BoxPlotChart`, `HeatmapChart`,
  `RadarChart`, `FunnelChart`) and the sixteen signal charts (`AutocorrelationChart`,
  `BodeChart`, `ChordDiagram`, `CoherenceChart`, `ConnectivityChart`, `EpicycleChart`,
  `HilbertChart`, `LaplaceChart`, `MellinChart`, `PolarPlotChart`, `SpectrogramChart`,
  `SpectrumChart`, `TopographyChart`, `WaterfallChart`, `WaveformChart`,
  `ZTransformChart`).
- **Chart animation.** Every chart reveals its data marks through `ChartAnimation`, driven by
  `Animator` over the motion ladder. A first paint fades the marks in; changing the data
  re-reveals at the shorter update duration; reduced motion settles immediately. The static
  furniture — title, legend, grid, axes and ticks — is cached in a device-pixel-ratio-aware
  pixmap and re-blitted, so only the marks are painted per frame.
- **Antialiasing strategies.** `ChartBase::setAntialiasing()` selects one of eleven strategies
  in four families: aliased and greyscale coverage, 2×/4× supersampling, 4×/8× jittered sample
  accumulation, and the FXAA, FMAA, MLAA, SMAA and CMAA post-process filters. The default is
  Qt's greyscale raster coverage; the expensive modes are opt-in.

### Canvas and drawing contexts

`Canvas` hosts three contexts. `Context1D` draws a scalar sparkline strip. `Context2D` is a
retained `QPainter` command façade. `Context3D` is a retained primitive scene with a camera:
it projects with `QPainter` by default, and the software path is always compiled and is what
the headless tests exercise. When the components target is built with Qt's `OpenGLWidgets`
module (`PERIODICITY_HAS_OPENGL`), `Context3D` can embed a `QOpenGLWidget` backend and falls
back to software with a single log entry if no GL context is available at paint time.
`Canvas::requestDraw()` schedules the repaint; the contexts do not schedule their own.

### Internationalisation

`md::i18n` manages one compiled Qt translation per language at
`:/i18n/periodicity_<locale>.qm`. The repository ships `translations/periodicity_zh_CN.ts`,
which CMake compiles with `qt_add_lrelease` and embeds under `:/i18n`; the bundled `zh_CN`
catalogue currently covers the `Language` and `Schedule` contexts. `install("zh_CN")` loads
it, `install("en")` returns to the source language, and an unknown or unbuilt locale leaves
the active language untouched. `LinguistTools` is optional: without it the library builds
with no catalogue and the source language stays in place. `md::i18n` needs only Qt Core, so
it also works in a non-GUI tool.

### Concurrency runtime

`md::schedule::UiThread` turns deferred work into bounded frames with explicit phases.
`md::schedule::EventThread` owns input decoding and coalescing: it keeps every down, up and
key event, keeps only the last pointer position and wheel delta per target, and hands one
ordered batch to the UI thread at the next frame boundary. `md::schedule::TaskQueue` keeps the
five-level priority ladder for code that posts work directly, and `md::pool` supplies the
worker pool, IO thread, ring buffers, mailbox and IPC for everything that must stay off the
UI thread. The full model is in [docs/threading.md](docs/threading.md).

### Configuration stores and containers

`md::Store` is a process-wide registry of named value maps: `Store::open("md.theme")` always
returns the same object, and stores emit change signals. The `Font`, `Language` and theme
scope containers publish into `md.font`, `md.language` and `md.theme`, and any `View` can bind
a store with `bindStore()`; the framework applies the `theme` key and lets a subclass extend
`onStoreChanged()`. The stores keep their values in the `structure` LRU map, so a store can be
bounded like any other cache.

### LRU containers

`structure` provides `UnorderedLRUMap`/`Set` (index in a hash map) and `OrderedLRUMap`/`Set`
(index in an ordered map), all sharing the same promotion and eviction core. Promotion is
O(1), a full cache evicts the oldest entry before inserting, `setCapacity()` shrinks
oldest-first, and `setEvictionHandler()` runs after the victim has left both containers.
`ArrayLRUList` keeps order slots in one growable array; `LinkedLRUList` keeps them in
intrusive nodes and is the default order container.

### Optional GPU compositor

The GPU path is **opt-in**; the default is always the software `QPainter` path. `GpuCompositor`
hosts child views, paints each enabled child once with the normal `QPainter` pipeline into an
offscreen image, uploads that image as a texture and composites the textures on the GPU.
`GpuCanvas` is the single-view variant that opens a `QPainter` on a `QOpenGLWidget` surface.
`View::setGpuAccelerated(true)` is the opt-in switch; it applies to the view and its current
`View` descendants, and every component supports it because the switch lives on the kernel
rather than on a component's API. `setRenderBackend(RenderBackend::OpenGL)` is the same
request expressed as a backend.

The acceleration is honest about its scope: it accelerates **compositing and blitting of
already-painted frames**, not the components' painting. A changed widget's first paint still
rasterises on the CPU; layout, text shaping and path construction stay in the CPU paint
pipeline. If the build has no `Qt6::OpenGLWidgets`, or the platform cannot create a GL
context, the request stays on Software and `View::lastError()` records why. See
[docs/gpu.md](docs/gpu.md) for the API, the cost model and the fallback rules.

## Threading and rendering model

The UI thread owns widgets, layout, painting and every event target. The event thread owns
input decoding and never touches a widget: its payload carries an opaque target token, not a
`QWidget` pointer. Workers return results through a queued signal, `TaskQueue` or
`UiThread::postTask()`. A thread owns its state, a queue is the only way across a thread
boundary, and the UI thread is never blocked.

`UiThread` runs one frame through these phases:

| Phase | Work |
| --- | --- |
| `Idle` | Resting state between frames; `postIdleTask()` may run here. |
| `PreFrame` | One-shot `addFrameCallback()` callbacks, including the event-batch hand-off. This is the frame boundary. |
| `Build` | `Priority::UserInput` and `Priority::UiUpdate` work. |
| `Layout` | `Priority::UiEvent` work, including delivery of an input batch. |
| `Paint` | `addPaintCallback()` hooks; `recordPaint()` feeds the paint statistics. |
| `PostFrame` | `Priority::Background` and `Priority::Idle` work. |

The priority ladder maps onto those phases rather than draining one flat queue:

| Priority | Value | Phase | Typical work |
| --- | ---: | --- | --- |
| `UserInput` | 0 | `Build` | press, click, key activation, scroll, ripple start |
| `UiUpdate` | 1 | `Build` | layout and paint invalidation |
| `UiEvent` | 2 | `Layout` | hover, synthetic notifications, animation ticks, input batches |
| `Background` | 3 | `PostFrame` | decoding, prefetching, cache warming |
| `Idle` | 4 | `PostFrame` and `postIdleTask()` | cleanup, metrics |

Within a priority the order is FIFO; across priorities strict order is preserved, so a press
posted while a `UiUpdate` batch is draining runs before the rest of that batch. `requestFrame()`
coalesces a burst into one frame, resize requests keep only the last size per target, and every
queued task has a deadline (one second by default) after which it is dropped and counted
rather than replayed. `setFrameBudget()` defaults to 8000 µs; an overrunning frame still
reaches `PostFrame`, records the overrun once and schedules its remaining work for a later
event-loop turn.

`View` participates by request: `requestUpdate()` coalesces a repaint through the frame
runtime when one is installed, and falls back to `QWidget::update()` plus a coalesced
`TaskQueue` entry when none is. [docs/threading.md](docs/threading.md) documents the phases,
the coalescing rules, the input pipeline and the diagnostics.

## Testing

```bat
ctest --preset x64-debug
```

The tests are Qt Test suites, one directory per module and a `tests/components` directory for
the widgets; the suite registers **99 test executables** in the committed tree. CTest runs
them with `QT_QPA_PLATFORM=offscreen`, so they need no display and behave the same on a
desktop and on a build machine.

Two further checks live in `tools/`:

- `tools/check_branding.cmake` fails when a forbidden product name appears in a source,
  comment or document. Run it directly:

  ```bat
  cmake -DSOURCE_DIR=D:/project/periodicity-cmake -P tools/check_branding.cmake
  ```

- `tools/check_layering.cmake` enforces the dependency matrix in `framework.md` §13: a public
  header may include only the modules its layer permits, `core`, `structure`, `pool`, `tokens`
  and `event` must stay free of Qt Widgets, and an unknown module name is a failure.

Visual regression is separate from CTest. `tests/visual/baseline/` holds 36 reference PNGs —
a top and a bottom capture for each of the 18 gallery pages — and `tools/visual_regression.py`
compares a fresh capture with that baseline, writing red diff images for the failures. See
[docs/visual-regression.md](docs/visual-regression.md).

## Installation

Every module contributes install rules through `cmake/PeriodicityModule.cmake`: the static
library goes to `${CMAKE_INSTALL_LIBDIR}`, its public headers keep the `md/<module>/` layout
under `${CMAKE_INSTALL_INCLUDEDIR}`, and the target joins the `PeriodicityTargets` export set.
The root `CMakeLists.txt` installs that set with `install(EXPORT PeriodicityTargets)` and the
`periodicity::` namespace:

```cmake
install(EXPORT PeriodicityTargets
  DESTINATION "${PERIODICITY_INSTALL_CMAKEDIR}"
  NAMESPACE periodicity::
  FILE PeriodicityTargets.cmake)
```

An installed tree therefore exposes `periodicity::core`, `periodicity::view`,
`periodicity::components` and the rest, together with `PeriodicityConfig.cmake` and
`PeriodicityConfigVersion.cmake` under `${prefix}/lib/cmake/Periodicity`. A consumer writes:

```cmake
find_package(Periodicity 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE periodicity::components)
```

`cmake --install out/build/x64-debug --prefix <prefix>` performs the install. The demo, the
gallery and the tests are development-only and are not installed.

## Status

Periodicity is at version 0.1.0 and is still under construction: all sixteen modules build
and are covered by the test suite. Dynamic colour and the expressive extensions of the design
baseline remain deferred. Two features described above are in the working tree but not yet
committed, and are recorded under `Unreleased` in [CHANGELOG.md](CHANGELOG.md): the optional
GPU compositor with `View`'s opt-in flag, and the seven extra antialiasing modes that take the
chart set from four to eleven. The `LICENSE` and `NOTICE` files are also new in the working
tree. Because the working tree adds `tests/view/tst_render_backend.cpp`, a `ctest` run there
lists 100 tests rather than the committed 99.

## Licence and attribution

Periodicity is licensed under the **Apache License, Version 2.0**. The full text is in
[LICENSE](LICENSE), and the copyright and attribution statement is in [NOTICE](NOTICE).

> Licensed under the Apache License, Version 2.0.

The licence applies repository-wide: every file in the repository is covered unless it states
otherwise. Per-file licence headers are therefore omitted to keep the sources readable; the
root `LICENSE` and `NOTICE` are the authoritative statements.

Qt 6.8 is an external dependency and remains under its own licence; the repository does not
vendor Qt. The translation catalogue in `translations/` is part of this repository.

The visual design follows a public design baseline. The token values in `framework.md` §3 were
cross-checked against the upstream reference token exports, and the sources describe the
baseline by what it is rather than by whose product it is; `tools/check_branding.cmake` is the
enforcement half of that convention.

## Documentation

- [framework.md](framework.md) — architecture, layering and the dependency matrix.
- [docs/components.md](docs/components.md) — the component catalogue with headers and tests.
- [docs/threading.md](docs/threading.md) — frame phases, priorities and the input pipeline.
- [docs/gpu.md](docs/gpu.md) — the optional GPU compositor and its fallback rules.
- [docs/visual-regression.md](docs/visual-regression.md) — the gallery baseline workflow.
- [issue.md](issue.md) — the per-module boundary audit.
- [CHANGELOG.md](CHANGELOG.md) — the commit-by-commit history, newest first.
