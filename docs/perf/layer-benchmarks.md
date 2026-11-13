# Layer benchmarks

These are the three benchmarks the rendering architecture requires before a
retained-layer change can land. They live in
`tests/benchmarks/tst_layer_benchmarks.cpp` and are registered by the existing
`tests/CMakeLists.txt` glob as the executable `tst_layer_benchmarks`; no CMake
change is needed.

The acceptance rule is the one in `docs/rendering-architecture.md`
("Performance workstream" and "Target architecture"): a step lands only with a
lower number. The benchmark draws the line that matters most — the cached clean
frame must be at least **2x cheaper** than the uncached full paint — and reports
every other comparison without enforcing it.

## Running

`tst_layer_benchmarks` is an ordinary ctest test, so the offscreen platform and
the Qt plugin paths are already set by `tests/CMakeLists.txt`:

```powershell
ctest --test-dir <build-dir> -R tst_layer_benchmarks --output-on-failure
```

To run it directly, set the platform yourself. The executable forces
`QT_QPA_PLATFORM=offscreen` only when the variable is empty, so a caller can
still choose a different platform:

```powershell
$env:QT_QPA_PLATFORM = "offscreen"
$env:PERIODICITY_BENCH_OUT = "out/perf/layer-benchmarks.txt"
<build-dir>\tests\tst_layer_benchmarks.exe
```

Every row goes to stdout as one `PERF_ROW` line (`scenario=`, `config=`,
`iterations=`, `frames_per_iteration=`, `mean_ms=`, `median_ms=`, `p95_ms=`,
`min_ms=`, `max_ms=`, `ratio_median=`, `verdict=`). The same table, including
the `PERF_META` and `PERF_SHAPE` preamble, is written to
`PERIODICITY_BENCH_OUT` when that variable names a file (parent directories are
created). A write failure fails the test; a silent missing results file is not
an acceptable outcome.

The numbers below are left empty on purpose. Fill them from a real run on the
machine that will be compared, one row per `PERF_ROW` line, and state the
machine and build configuration with them. Do not copy numbers from another
machine, a different Qt build, or a different display scale.

## Scenario shapes

| Scenario | Shape |
| --- | --- |
| `windowOfComponents` | One 1170 x 634 host window with 74 `View` components in a 10 x 8 grid of 110 x 72 cells: 37 `Button` and 37 `Text`, alternating. Three configurations: uncached full paint, cached clean frame, cached frame with one component's 10 x 10 rectangle invalidated by `requestUpdate(QRectF)`. |
| `pageOfCharts` | One 1548 x 1148 page with 25 `LineChart` widgets in a 5 x 5 grid of 300 x 220 cells. Each chart has four series of 120 points. Three configurations: uncached full paint, cached clean frame, cached frame after `setSeries()` changes one chart's data (a full-layer invalidation on that one chart, which is the damage a data change actually produces). |
| `revealAnimation` | One 640 x 420 `LineChart` subclass (four series, 120 points) playing the chart's `Level3` reveal (750 ms). The chart's own wall-clock reveal is suppressed with `setAnimated(false)` and the benchmark chart plays the same fade through the public `paintPlot(..., progress)` hook, driven by an `md::animation::Animator` on its manual clock: each frame consumes 16 ms of animation time, so a run is 47 advances and 48 painted frames on every machine and in every run. Two configurations: uncached and cached (layer-backed). The reported value is the total time from the first frame to the settled frame, not a per-frame slice. |

`ChartBase` has no public manual clock (unlike `ToggleButton`, `RowFeedback`,
`Modal`, `ContainerTransform`, `DropdownTrigger`, `Language` and `PickerSheet`),
and the benchmark does not add one. The reveal subclass owns the manual-clock
`Animator` and forwards the per-frame value to the documented progress hook; the
chart's paint path, reveal spec and whole-chart invalidation are unchanged. This
is the only library-API compromise in the benchmark, and it is local to
`tests/benchmarks/`.

All scenarios are offscreen and deterministic: no sleeps, no wall-clock waits,
no timers. The chart range is settled synchronously (`setAutoFit(false)`) before
the first measurement so a fit animation cannot move the plot between samples.
The page scenarios use `setAnimated(false)`. The reveal scenario's subclass owns
the only animator and advances it from the manual clock; its value callback
records the whole-chart damage that a real reveal frame records.

## What is asserted

- **The cached clean frame is at least 2x cheaper than the uncached full paint**
  (median, `windowOfComponents` and `pageOfCharts`). This is the acceptance rule
  and the one hard gate. A failure prints both medians and the speedup.
- **The frame was produced.** Every timed frame renders through
  `QWidget::render()` into a pre-cleared image. A validation frame must be fully
  opaque (the host window was painted) and contain at least eight sampled colours
  (the components painted on top). An empty paint fails rather than producing a
  suspiciously fast sample.
- **The cached configuration really is layer-backed.** Every view holds a
  surface, `md::view::liveLayerCount()` is exactly the expected number and does
  not change between frames, and the clean frame runs **zero** `paintContent`
  hooks. A cache miss, an eviction, a reallocation or a fallback to the direct
  path all repaint the content and fail the assertion. `LayerCacheStats` is
  private to `render/` (and to `view/`'s private edge), so the hit is asserted
  through these public consequences rather than through the cache's own
  counters.
- **The damage path rasterises only the damage.** In `windowOfComponents` the
  invalidated component runs its content hook exactly once, with a painter clip
  that contains the 10 x 10 rectangle and is no larger than it (one pixel of
  alignment tolerance), and every other component runs zero hooks. In
  `pageOfCharts` exactly one chart repaints, with the whole chart layer, and the
  other 24 run zero hooks.
- **The reveal is deterministic and settles.** Every run paints the same number
  of frames, ends at progress 1.0, and runs exactly one content hook per painted
  frame. After settling, a cached chart runs zero hooks on a clean frame and an
  uncached chart runs one.
- **Layers are released.** Each scenario destroys its widgets and asserts that
  the process-wide layer count returns to its starting value.

## What is only reported

- The cached frame with one component's damage (`cached-one-damage`) and the
  cached frame with one chart's data change (`cached-one-data-change`), relative
  to the uncached full paint. Either may be slower on a given machine; that is a
  legitimate result to read, not a failure.
- The cached versus uncached reveal totals. A reveal repaints every frame, so
  the layer cannot reuse pixels during the run; the comparison is reported, not
  enforced.
- Mean, median, p95, min and max for every configuration. Only the clean-frame
  median is used for the 2x gate; p95 is reported so a bad tail is visible
  without turning a single slow iteration into a test failure.

## Recorded results

Machine: local development workstation, Windows 11, single process, `QT_QPA_PLATFORM=offscreen`  
Build: `x64-debug` (Debug, no optimisations), MSVC 2022, Qt 6.8.3 MSVC 2022 64-bit  
Display scale / device pixel ratio: 1 (offscreen surface, no platform scaling)  
Date: 13 November 2026

| Scenario | Configuration | Iterations | Frames/iter | Mean (ms) | Median (ms) | p95 (ms) | Ratio vs uncached | Verdict |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `windowOfComponents` | `uncached-full` | 30 | 1 | 13.849 | 13.491 | 17.011 | 1.000 | reference |
| `windowOfComponents` | `cached-clean` | 30 | 1 | 3.446 | 3.293 | 4.183 | **4.10x** | asserted-2x |
| `windowOfComponents` | `cached-one-damage` | 30 | 1 | 4.166 | 3.797 | 5.628 | 3.55x | reported |
| `pageOfCharts` | `uncached-full` | 30 | 1 | 116.015 | 114.174 | 123.587 | 1.000 | reference |
| `pageOfCharts` | `cached-clean` | 30 | 1 | 2.883 | 2.622 | 4.465 | **43.5x** | asserted-2x |
| `pageOfCharts` | `cached-one-data-change` | 30 | 1 | 8.219 | 8.252 | 9.152 | 13.8x | reported |
| `revealAnimation` | `uncached-reveal` | 30 | 48 | 513.604 | 521.469 | 565.443 | 1.000 | reference |
| `revealAnimation` | `cached-reveal` | 30 | 48 | 534.903 | 526.838 | 580.670 | 0.99x | reported |

This is a **Debug** build, so the absolute milliseconds are not the numbers a
release build would show; the ratios are the result. Both hard gates pass with
margin: 4.10x on the component window and 43.5x on the chart page, against a 2x
requirement.

The chart page gains far more than the component window because its uncached
paint is dominated by 25 charts each re-fitting and re-rasterising four series of
120 points, while its cached clean frame is a compositor blit of 25 small
surfaces. The component window starts cheaper (13.5 ms uncached), so there is
less to remove, and it still more than quadruples.

### The reveal animation does not benefit, and that is expected

`revealAnimation` is the one scenario where layer backing is **neutral**:
0.99x, i.e. no gain and a possible small loss. A reveal repaints the whole chart
every frame, which invalidates the whole layer every frame, so no pixels are ever
reused during the run — the layer adds a surface copy without removing the
repaint that dominates. This is recorded as a reported result rather than a
failure because it is the correct behaviour for a full-surface animation, not a
defect in the cache. It does mean the honest claim for the reveal workstream is
"no regression", not "a speed-up", and any future claim of a reveal win has to
come from painting less per frame (a smaller damage region, or an effect that
composites the settled layer) rather than from retention alone.

`cached-one-damage` at 3.55x and `cached-one-data-change` at 13.8x are the
interesting middle cases: a small invalidation still costs a full-frame composite
plus one component repaint, so it lands between the clean frame and the full
paint, which is what the design predicts.

## What the first run settled, and what is still open

The benchmark source and this file were written without a compiler, then built
and run by the repository's single builder. The run settled the questions that
mattered most, and the bullets below now record the outcome rather than an
expectation. Anything still open is marked as open.

- **Settled:** `QWidget::render()` on a parent does deliver every child's paint
  event into the shared target painter on this Qt 6.8.3 / Windows / offscreen
  combination. The frame-produced assertions pass with 74 and with 25 children,
  and the per-component counters show every content hook running once per
  uncached frame.
- The width of the layer painter's clip for a 10 x 10 damage rectangle, and the
  exact clip reported for a fully damaged chart layer. The assertions allow one
  pixel of alignment tolerance.
- **Settled, and it holds:** the 2x gate passes on the 74-component window at
  4.10x and on the 25-chart page at 43.5x, in a Debug build. The components are
  real (`Button`, `Text`) and 37 of them paint text, so this is not a synthetic
  win. Re-measure in a release build before quoting absolute times.
- Whether `setSeries()` on a cached chart produces exactly one content hook at
  the expected clip on every Qt build. The assertion follows the damage the
  component itself records; a mismatch would mean a missed or extra
  invalidation and should be investigated, not loosened.
- The reveal frame count for the pinned `Level3` duration. The benchmark does
  not hard-code 48 frames; it asserts that every run in both configurations has
  the same count and records it in the table.
- Whether the benchmark-side reveal paints identical pixels to ChartBase's own
  reveal. The override mirrors the base's default progress overload (opacity
  fade, plot clip while in flight, unclipped at progress 1), but the two paths
  are not pixel-compared here; that comparison belongs to the chart's own test
  suite.
- The RHI/GPU host. These benchmarks exercise the software device and the
  in-widget CPU compositor (`render::CpuCompositor`), which is the path `View`
  drives today. A `QRhiWidget` host or an RHI compositor is not measured here;
  when one lands it needs a matching row rather than a reinterpretation of these
  numbers.

## Note on the reveal driver

The reveal scenario needs to advance an animation without a wall clock.
`ChartBase` does not expose a manual clock, and adding one to a public header is
outside this deliverable. The benchmark therefore defines `RevealLineChart`, a
`LineChart` subclass local to the test file:

- it calls `setAnimated(false)` so ChartBase's own wall-clock reveal never
  starts;
- it owns an `md::animation::Animator` with `setManualClock(true)`, takes the
  reveal spec from `chart.animation().specFor(PlayMode::Reveal)`, and on every
  value calls `requestUpdate(QRectF(rect()))`;
- it overrides the public `paintPlot(painter, plot, progress)` hook to apply
  the same fade and clip the base's default progress overload applies, using the
  manual clock's value.

No library file is modified. The difference from the chart's built-in reveal is
only who owns the animator; the paint path, the reveal timing and the
invalidation are the chart's. The doc records the one caveat above: the two
reveal paths are not pixel-compared in this benchmark.

