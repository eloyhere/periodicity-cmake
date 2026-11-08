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
| `tokens/` | L1 — colour, type, shape, elevation, motion, state | `periodicity::tokens` |
| `theme/` | L2 — theme bundle and scoped resolution | `periodicity::theme` |
| `event/` | L3 — event values, dispatch, propagation, gestures | `periodicity::event` |
| `drawable/` | L3a — painting primitives | `periodicity::drawable` |
| `layer/` | L3b — background, elevation and foreground stages | `periodicity::layer` |
| `animation/` | L3c — tweening driven by motion tokens | `periodicity::animation` |
| `view/` | L4 — measure/arrange/paint kernel and state machine | `periodicity::view` |
| `layout/` | L5 — containers, spacing, window size classes | `periodicity::layout` |
| `components/` | L6 — public Material widgets | `periodicity::components` |
| `apps/gallery/` | demo application | `periodicity::gallery` |
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
`R:/Qt/6.8.3/msvc2022_64`, together with Visual Studio 2022 (toolset 14.51) and the CMake
and Ninja that ship with it. The project itself asks for CMake 3.21 or newer and compiles
as C++20.

The Qt installation path is pinned in `CMakePresets.json`. If yours sits somewhere else,
the tidiest fix is a `CMakeUserPresets.json` that inherits from `windows-base` and sets its
own `CMAKE_PREFIX_PATH`; that file is ignored by Git, so the committed preset stays
portable for everyone else.

## Build

Configure and build from a Visual Studio developer environment, so that `cl.exe` is on
`PATH`:

```bat
call "R:\visual\ide\VC\Auxiliary\Build\vcvars64.bat"
cmake --preset x64-debug
cmake --build --preset x64-debug
```

From a plain shell, wrap the same commands:

```powershell
cmd /c "call R:\visual\ide\VC\Auxiliary\Build\vcvars64.bat >nul && R:\visual\ide\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --preset x64-debug && R:\visual\ide\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build --preset x64-debug"
```

Both presets build into `out/build/<preset>`. `x64-release` compiles the same targets with
optimisation on and without debug information.

## Test

The tests are Qt Test suites, one directory per module, and CTest runs them:

```powershell
cmd /c "call R:\visual\ide\VC\Auxiliary\Build\vcvars64.bat >nul && R:\visual\ide\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir out/build/x64-debug --output-on-failure"
```

The suites run with `QT_QPA_PLATFORM=offscreen`, so they need no display and behave the
same on a desktop and on a build machine.

## Layering check

The dependency rules in `framework.md` are enforced by a small CMake script:

```powershell
R:\visual\ide\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -DSOURCE_DIR=R:/project/periodicity-cmake -P tools/check_layering.cmake
```

It fails if a public header includes a module that its own layer is not allowed to depend
on, or if `core` or `tokens` reach for Qt Widgets.

## Status

Periodicity is under construction, built up layer by layer. The token values recorded in
`framework.md` §3 have been verified against Google's reference token sources. Dynamic
colour and Material 3 Expressive are deliberately postponed until the layers beneath them
have settled.
