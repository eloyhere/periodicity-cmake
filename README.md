# Periodicity

A cross-platform C++ UI component library implementing Material Design 3 on top of
**Qt Widgets**, with every visual produced by `QPainter` rather than Qt Style Sheets or
platform theme engines.

The architecture — layering, module responsibilities, core abstractions and the
dependency matrix — is specified in [framework.md](framework.md). Read it before
changing a module boundary.

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

## Requirements

- Qt **6.8.3** (MSVC 2022 x64), installed at `R:/Qt/6.8.3/msvc2022_64`
- Visual Studio 2022 (toolset 14.51) and its bundled CMake/Ninja
- CMake ≥ 3.21, C++20

The Qt path is pinned in `CMakePresets.json`; change `CMAKE_PREFIX_PATH` there if your
installation differs.

## Build

Run from a Visual Studio developer environment so that `cl.exe` is on `PATH`:

```bat
call "R:\visual\ide\VC\Auxiliary\Build\vcvars64.bat"
cmake --preset x64-debug
cmake --build --preset x64-debug
```

From a plain shell, wrap the same commands:

```powershell
cmd /c "call R:\visual\ide\VC\Auxiliary\Build\vcvars64.bat >nul && R:\visual\ide\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --preset x64-debug && R:\visual\ide\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build --preset x64-debug"
```

## Test

```powershell
cmd /c "call R:\visual\ide\VC\Auxiliary\Build\vcvars64.bat >nul && R:\visual\ide\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir out/build/x64-debug --output-on-failure"
```

Tests run with `QT_QPA_PLATFORM=offscreen`, so no display is required.

## Layering check

```powershell
R:\visual\ide\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -DSOURCE_DIR=R:/project/periodicity-cmake -P tools/check_layering.cmake
```

The check fails if a public header includes a module that its layer is not allowed to
depend on, or if `core`/`tokens` pull in `QtWidgets`.

## Status

Under construction, layer by layer. The token values in `framework.md` §3 are verified
against Google's reference token sources; dynamic colour and M3 Expressive are deferred.
