# Periodicity

English | [简体中文](README.zh-CN.md)

Periodicity is a cross-platform C++ library of user-interface components for desktop
applications, modelled on Material Design 3 and built on Qt Widgets. It paints every visual
itself through `QPainter`; it does not restyle widgets with Qt Style Sheets, and it does not
fall back on the platform theme engine. The only things it takes from the host system are
the fonts and the native window frame, so a layout that looks right on Windows looks right
on macOS and Linux as well. Development happens at
[github.com/eloyhere/periodicity-cmake](https://github.com/eloyhere/periodicity-cmake).

The library is arranged as a stack of layers, from units and geometry at the bottom to the
public widgets at the top. [framework.md](framework.md) sets that architecture out in full —
the layering, the responsibility of each module, the core abstractions and the matrix of
permitted dependencies. Read it before changing a module boundary.

## Layout

| Path | Layer | Target |
| --- | --- | --- |
| `core/` | L0 — units, geometry, state flags | `periodicity::core` |
| `structure/` | L1a — headless data structures (nodes, trees, forests, pools) and the table/virtual-source contracts | `periodicity::structure` |
| `pool/` | L1p — object pools, thread pool, ring buffers, IO thread, mailbox and process IPC | `periodicity::pool` |
| `schedule/` | L1 — task priority queue (input before UI) | `periodicity::schedule` |
| `tokens/` | L1 — colour, type, shape, elevation, motion, state | `periodicity::tokens` |
| `theme/` | L2 — theme bundle and scoped resolution | `periodicity::theme` |
| `event/` | L3 — event values, dispatch, propagation, gestures | `periodicity::event` |
| `drawable/` | L3a — painting primitives | `periodicity::drawable` |
| `layer/` | L3b — background, elevation and foreground stages | `periodicity::layer` |
| `animation/` | L3c — tweening driven by motion tokens | `periodicity::animation` |
| `ripple/` | L3 — pointer-origin hover and press feedback | `periodicity::ripple` |
| `view/` | L4 — measure/arrange/paint kernel and state machine | `periodicity::view` |
| `layout/` | L5 — containers, spacing, window size classes | `periodicity::layout` |
| `components/` | L6 — public Material widgets | `periodicity::components` |
| `demo/` | documentation application (Element-Plus-style showcase) | `periodicity_demo` |
| `apps/ipc_echo/` | child process used by the Pool page and the IPC tests | `periodicity_ipc_echo` |
| `tests/` | Qt Test suites, one directory per module | — |
| `tools/` | layering check and other tooling | — |

Each module has the same internal shape:

```
<module>/
├── CMakeLists.txt
├── include/md/<module>/*.h
└── src/*.cpp
```

The headers under `include/` are the installed, public face of a module and the only part
of it that another module may include; the sources under `src/` stay private. The gallery
application in `apps/gallery/` is assembled from the public targets alone, which keeps that
promise honest.

## Requirements

Periodicity needs Qt **6.8.3**, built for MSVC 2022 x64 and installed at
`D:/Qt/6.8.3/msvc2022_64`, together with a Visual Studio 2022-or-newer toolset (the
`msvc2022_64` Qt binaries are ABI-compatible with later MSVC 14.x releases) and the CMake
and Ninja that ship with it. The project itself asks for CMake 3.21 or newer and compiles
as C++20. `CMakePresets.json` provides a solution preset for both Visual Studio 2022 and
Visual Studio 2026; see [Visual Studio](#visual-studio).

The Qt installation path is pinned in `CMakePresets.json`. If yours sits somewhere else,
the tidiest fix is a `CMakeUserPresets.json` that inherits from `windows-base` and sets its
own `CMAKE_PREFIX_PATH`; that file is ignored by Git, so the committed preset stays
portable for everyone else.

## Build

Configure and build from a Visual Studio developer environment, so that `cl.exe` is on
`PATH`:

```bat
call "D:\visual studio\VC\Auxiliary\Build\vcvars64.bat"
cmake --preset x64-debug
cmake --build --preset x64-debug
```

From a plain shell, wrap the same commands:

```powershell
cmd /c "call ""D:\visual studio\VC\Auxiliary\Build\vcvars64.bat"" >nul && ""D:\visual studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"" --preset x64-debug && ""D:\visual studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"" --build --preset x64-debug"
```

Both presets build into `out/build/<preset>`. `x64-release` compiles the same targets with
optimisation on and without debug information.

## Visual Studio

The repository carries two solution presets, because the Visual Studio generator is tied to
the toolset the IDE ships:

| Preset | IDE / toolset | Solution |
| --- | --- | --- |
| `vs2026` | Visual Studio 2026, MSVC v180 | `out/build/vs2026/Periodicity.slnx` |
| `vs2022` | Visual Studio 2022, MSVC v143 | `out/build/vs2022/Periodicity.sln` |

Use the one that matches the installed IDE — a v143 preset on a v180-only installation fails
at configure time with `MSB8020`, which is the toolset telling you to pick the other preset.

```bat
cmake --preset vs2026
cmake --build --preset vs2026-debug     :: builds periodicity_demo and what it needs
```

Then open `out/build/vs2026/Periodicity.slnx` and press **F5**. `periodicity_demo` is already
the solution's startup project (set through `VS_STARTUP_PROJECT` in the root `CMakeLists.txt`),
so no project has to be selected by hand; `vs2026-all-debug` builds every target instead.

Two details make the demo runnable straight from the IDE:

* `periodicity_deploy_qt_runtime()` (in `cmake/PeriodicityModule.cmake`) runs `windeployqt`
  after each link, so Qt's DLLs and the platform plugin sit beside `periodicity_demo.exe` and
  the executable also starts from Explorer or a plain shell;
* the same helper sets `VS_DEBUGGER_ENVIRONMENT` and `VS_DEBUGGER_WORKING_DIRECTORY`, so the
  debugger finds Qt even on a first run, before that deployment step has produced anything.

The gallery gets the same treatment. On a Qt installation without a `translations` directory
(a developer build), `windeployqt` prints a warning that translations are unavailable; it is
informational and does not affect the build.

Visual Studio's folder mode works too: **File → Open → Folder**, choose the repository root,
and select the `vs2026` (or `vs2022`) configure preset.

## Test

The tests are Qt Test suites, one directory per module, and CTest runs them:

```powershell
cmd /c "call ""D:\visual studio\VC\Auxiliary\Build\vcvars64.bat"" >nul && ""D:\visual studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"" --test-dir out/build/x64-debug --output-on-failure"
```

The suites run with `QT_QPA_PLATFORM=offscreen`, so they need no display and behave the
same on a desktop and on a build machine.

## Layering check

The dependency rules in `framework.md` are enforced by a small CMake script:

```powershell
D:\visual studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -DSOURCE_DIR=D:/project/periodicity-cmake -P tools/check_layering.cmake
```

It fails if a public header includes a module that its own layer is not allowed to depend
on, or if `core` or `tokens` reach for Qt Widgets.

## Gallery

`apps/gallery/` is an interactive test bench assembled from the public targets only: a
sidebar switches between component families, a header button toggles light and dark, and
every page instantiates real widgets rather than screenshots. It can also render itself
for a headless smoke check:

```powershell
out\build\x64-debug\periodicity_gallery.exe
out\build\x64-debug\periodicity_gallery.exe --screenshot .tmp\shots          # top of every page
out\build\x64-debug\periodicity_gallery.exe --screenshot .tmp\shots --dark   # dark theme
```

`--screenshot` also writes a `*-bottom.png` per page (scrolled to the end) so the cards
below the fold are covered.

## Status

Periodicity is still under construction. The chain from `core` to `components` now builds,
tests and renders: basic widgets, inputs and pickers, dates/cascade/mention, layout
containers, tool bars/badges/states, and the display widgets all have real implementations
and tests. The token values recorded in `framework.md` §3 were verified against Google's
reference token sources. Dynamic colour and Material 3 Expressive remain deliberately
postponed; installation and export (`find_package(Periodicity)`) are not implemented yet,
so the library is currently consumed inside the source tree.
