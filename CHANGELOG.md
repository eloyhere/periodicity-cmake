# Changelog

All notable changes to Periodicity are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). The project has no tagged release
yet, so each section below is one commit, newest first, headed by its short hash and date.

## [Unreleased]

### Added

- **Charts:** the antialiasing set grows to eleven strategies. Five new modes — MLAA, SMAA,
  CMAA, 4× jittered sampling and 8× jittered sampling — join the six already committed
  (none, grayscale, 2× and 4× supersampling, FXAA and FMAA); `ChartAntialiasing` and
  `ChartBase::setAntialiasing()` select them.
- **GPU compositor:** `md::GpuCompositor` and `md::GpuCanvas` add an opt-in OpenGL path that
  composites already-painted frames as textures. Software remains the default, and a missing
  `Qt6::OpenGLWidgets` module or GL context keeps the view on the software path and records
  the reason in `View::lastError()`. `docs/gpu.md` documents the API, cost model and fallback.
- **View:** `setGpuAccelerated()`, `isGpuAccelerated()`, `setRenderBackend()`/`renderBackend()`
  and the `gpuAccelerationChanged` signal expose the opt-in flag on the kernel, so every
  component supports it without a per-component API.
- **Tests:** `tests/view/tst_render_backend.cpp` covers backend selection and fallback; a
  working-tree `ctest` run lists 100 tests.
- **Licence:** the project is now licensed under the Apache License, Version 2.0; `LICENSE`
  (full text) and `NOTICE` (copyright and attribution) are added.

## [211998d] — 12 November 2026

### Added

- `ChartAntialiasing` with four strategies: `None`, Qt's greyscale raster coverage
  (`Grayscale`), 2× and 4× supersampling.
- `ChartBase::setAntialiasing()`/`antialiasing()`, which invalidates the plot but not the
  cached static layer, and the `tst_chart_antialiasing` suite.

## [0e2dd4b] — 12 November 2026

### Fixed

- Expired tasks now compare against a `Clock::time_point::max()` deadline sentinel, so a task
  with no explicit deadline is not dropped; the overrun path has explicit test coverage.
- An overrunning frame queues a follow-up turn for the work the budget left pending instead
  of waiting for another caller to schedule one.
- Paint callbacks persist across frames instead of being consumed by the first frame.

## [a16c719] — 12 November 2026

### Added

- The `mathematic` module: matrices, decompositions, spectra, filters, statistics and the
  Hilbert, Laplace, Mellin and Z integral transforms.
- Embedded i18n through `md::i18n` and the `zh_CN` catalogue, with the `Font` and `Language`
  configuration containers.
- Sixteen signal charts, `ChartAnimation`, `ContainerTransform`, and the `UiThread` and
  `EventThread` frame/input runtimes.

## [5a4f203] — 12 November 2026

### Changed

- Refreshed the gallery visual-regression baseline for the new pages, including the new
  Charts reference pair (top and bottom).

## [cde6b3c] — 12 November 2026

### Added

- `Canvas` with the `Context1D`, `Context2D` and `Context3D` drawing contexts.
- Nine charts on the new `ChartBase`/`MathematicsCoordination` stack, plus `Clock`, `Schedule`
  and `Scheduler`.
- The LRU container family in `structure` and new gallery pages for the visualisation work.

## [4fe2fb2] — 12 November 2026

### Fixed

- Closed the 26 P1 findings from the boundary audit across every module, covering event
  dispatch, animation interruption, pool lifetime, schedule budget and theme resolution,
  with regression tests for the fixed boundaries.

## [557fa4d] — 12 November 2026

### Removed

- Removed the stray one-byte `issue.md.fence` scratch file.

## [0d14d5d] — 12 November 2026

### Added

- The per-module boundary audit in `issue.md`: 26 P1 and 59 P2 findings with locations and
  test coverage.

### Fixed

- Corrected the badge test that the audit exposed.

## [f2ad36c] — 12 November 2026

### Added

- The P0 gap components: `BottomSheet`, `Chip`, `Divider`, `Fab`, `IconButton`,
  `NavigationBar`, `SearchBar` and `Switch`.
- Installable packaging: the `PeriodicityTargets` export set, `PeriodicityConfig.cmake`, the
  version file and the `periodicity::` namespace.
- Accessibility plumbing, the gallery visual-regression harness and its CI workflow.

## [8f3ccff] — 12 November 2026

### Added

- The `pool` module: object pools, thread pool, ring buffers, IO thread, cross-thread mailbox
  and process IPC.

### Changed

- Moved the object pools out of `structure` into `pool`, and dropped the vendored PyYAML tree
  from `.tmp`.

## [bb5f808] — 9 November 2026

### Changed

- Merge commit joining the two 9 November import lines; the merge itself adds no file changes.

## [ba680b3] — 9 November 2026

### Added

- Bulk import of the basic widget implementations and the demo shell.

## [a12a593] — 9 November 2026

### Added

- Bulk import of the CI build workflow, the gallery sources and supporting files.

## [38846c1] — 9 November 2026

### Removed

- Deleted the `.github/workflows` directory and its single-platform CMake workflow.

## [aaea4b3] — 9 November 2026

### Added

- A single-platform CMake build workflow for GitHub Actions.

## [3f1abd0] — 9 November 2026

### Changed

- Rewrote `README.md` in British English and added the Simplified Chinese translation.

## [14955b1] — 9 November 2026

### Added

- Initial import of the component library: CMake presets, the module skeleton for the layer
  stack, the core units/geometry/state headers, the gallery stub, the design backlog and the
  architecture notes.
