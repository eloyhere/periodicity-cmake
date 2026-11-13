# Animation audit — continuity and smoothness

Scope: every animated component under `components/`, the motion infrastructure it
depends on (`tokens/`, `animation/`, `view/`, `layer/`, `ripple/`), and the frame
scheduler in `schedule/`. HEAD `efa128e` plus the working tree; no build was run
for this audit (build authority sits with the RHI workstream). Every finding is
from reading, cross-checked against the manual-clock tests in
`tests/components/tst_motion_continuity.cpp`.

The rules are the ten in the brief: single owner; retarget rather than restart;
token motion only; the project's clock; no work when hidden; a reduced-motion
snap path; continuity across state and theme change; no per-frame allocation;
correct repaint scope; termination.

Severity: **P0** wrong on every use and visibly broken; **P1** visibly wrong in a
common interaction; **P2** cost or rough edge, no visible break; **P3** rule
deviation with no user-visible effect.

## Counts by severity

| Severity | Found | Fixed in this pass | Open |
|---|---:|---:|---:|
| P0 | 1 | 1 | 0 |
| P1 | 11 | 9 | 2 |
| P2 | 12 | 0 | 12 |
| P3 | 3 | 0 | 3 |
| Blocked (other workstream) | 2 | 0 | 2 |

Line numbers are anchors: where a fix landed, the anchor is the function that was
fixed; where a finding is open, it is the line that still shows the defect.

## Findings

| Component | file:line | Rule violated | Severity | Intended fix |
|---|---|---|---|---|
| PieChart | `components/src/pie_chart.cpp:402` (`startReveal`), `components/include/md/components/pie_chart.h:204` | 1 (single owner), 10 (termination) | P0 | Removed the pie's own `Animator` and `reveal_` state; the sweep paints `animation().progress()`, so ChartBase owns the only run. A data update now re-runs that one animation instead of an invisible duplicate. |
| Ring | `components/src/ring.cpp:129` (`showEvent`/`hideEvent`) | 5 (no work when hidden), 7 (continuity) | P1 | Replaced the `QWidget::showEvent`/`hideEvent` overrides with `onShown()`/`onHidden()`. The override skipped `View::showEvent/hideEvent`, so the lifecycle never left `Created`/`Shown`, the ripple was never suspended and hover/press state survived a hide. |
| Progress | `components/src/progress.cpp:157` (`showEvent`/`hideEvent`) | 5, 7 | P1 | Same fix: lifecycle hooks instead of the Qt overrides. |
| BottomSheet | `components/src/bottom_sheet.cpp:109` (tick) | 4 (correct clock) | P1 | The tick assumed every timer event was 16 ms (`elapsedMs_ += kTickMs`), so a delayed frame stretched the token duration and desynchronised the slide from every other animation. Now measures `QElapsedTimer`. |
| Drawer | `components/src/drawer.cpp:117` (tick) | 4 | P1 | Same fake clock replaced with `QElapsedTimer`. |
| Pager | `components/src/pager.cpp:73` (tick) | 4 | P1 | Same fake clock replaced with `QElapsedTimer`. |
| BottomSheet | `components/src/bottom_sheet.cpp:449` (`easedProgress`) | 8 (per-frame allocation) | P1 | Resolved the `QEasingCurve` once per run in `animateTo()` and cached it; the tick no longer constructs a curve and re-reads the theme 60 times a second. |
| Drawer | `components/src/drawer.cpp:463` (`easedProgress`) | 8 | P1 | Same cached curve. |
| Pager | `components/src/pager.cpp:380` (`easedProgress`) | 8 | P1 | Same cached curve. |
| ChartBase (all charts) | `components/src/chart_common.cpp:1639` (`startRevealIfNeeded`) | 5 | P1 | Added `onHidden()`: a chart hidden mid-reveal stops the animator and settles the run on the finished frame, instead of repainting an invisible widget for up to a second. |
| TimePicker | `components/src/time_picker.cpp:484` (`tickAnimation`, `kAnimationFraction` at `:495`) | 3 (token motion only) | P1 | **Open.** The wheel chases its target with a hand-rolled exponential ease and a literal per-tick fraction; no duration and no curve come from `MotionLevel`. Intended fix: per-column `startOffset`/`progress`, advanced by a real clock over `motion().level(Level2)`, eased with the token curve; retarget resets that column's progress with the current offset as its start. |
| Carousel | `components/src/carousel.cpp:454` (ordinary transition) | 3, 2 | P1 | **Open.** A page change advances `progress_ += interval / duration` — a linear ramp with no easing token, while the drag settle right above it uses `medium1`'s curve, so a tap-slide and a swipe-settle do not match. Intended fix: drive `progress_` from a clock over `medium1()` and ease it with the token curve. |
| Banner | `components/src/banner.cpp:336` (`tickReveal`) | 8 | P2 | **Open.** Builds a `QEasingCurve` and reads the theme on every 16 ms tick. Cache the spec in `startReveal`, as the three overlays now do. |
| TabBar | `components/src/tabbar.cpp:316` (`tickIndicator`) | 8 | P2 | **Open.** Same per-frame curve construction; cache it when the indicator run starts. |
| Carousel | `components/src/carousel.cpp:342` (`setDuration`), `:401` (`transitionDuration`) | 3 | P2 | **Open.** The transition duration is a literal millisecond value with a public setter; the default should resolve through a `MotionLevel` and the setter stay as the documented override. |
| Carousel | `components/src/carousel.cpp:444` (tick step) | 4 | P2 | **Open.** The step is `interval()/duration`, so a dropped frame stretches the slide; measure the elapsed time instead (`QElapsedTimer`). |
| TabBar | `components/src/tabbar.cpp:283` (`snapIndicator`) | 5 | P2 | **Open.** No `onHidden()`: a hide during the 250 ms indicator run leaves the timer ticking (it stops itself at the end, so the cost is bounded). Settle on hide. |
| Banner | `components/src/banner.cpp:312` (`startReveal`) | 5 | P2 | **Open.** No `onHidden()`: a hide during a reveal leaves the 16 ms timer running for up to 300 ms. Settle on hide. |
| Carousel | `components/src/carousel.cpp:432` (`tickTransition`) | 4 | P2 | **Open.** Per-widget `QTimer` transport for a token-driven transition; `View`'s frame coalescing is bypassed. Move the transport onto the animator, as `Switch`/`Animation` do. |
| SubMenu | `components/src/sub_menu.cpp:44` | 4 | P2 | **Open.** Same QTimer transport, token duration and curve are otherwise correct. |
| Video | `components/src/video.cpp:1550` (`stepControlsFade`) | 3, 4 | P2 | **Open.** Controls fade by a constant step per tick (linear, token-derived duration but no curve) — use the token curve and an elapsed clock. |
| Video | `components/src/video.cpp:646` (fade timer) | 4 | P2 | **Open.** Same QTimer transport for the fade. |
| BottomSheet, Drawer | `components/src/bottom_sheet.cpp:119`, `components/src/drawer.cpp:127` (`syncLayout` per tick) | 9 (repaint scope) | P2 | **Open, deferred.** A window-sized overlay repaints the whole host (scrim included) at 60 Hz while only the surface moves. Intended fix: `requestUpdate(surfaceRect)` — depends on the damage-tracker API that is uncommitted in `view/`, which this workstream does not own. |
| PieChart (all charts) | `components/src/chart_common.cpp:1664` (`startChartAnimation`) | 2 (retarget) | P2 | **Open, documented.** A data update deliberately restarts progress at 0 ("the old data is re-revealed, not interpolated"). It reads as a wipe rather than a morph; if that is not wanted, retarget from the current progress. Recorded as a product decision, not a bug. |
| Skeleton | `components/src/skeleton.cpp:325` (`advanceFrame`, `kPhaseStep`) | 3 | P3 | **Open.** A continuous shimmer advances by a literal phase step per tick. `Ring`/`Progress` derive their cycle from `Level3`; the shimmer should use the same ladder rung. |
| Marquee | `components/src/marquee.cpp:230` (`tick`) | 3 | P3 | **Open.** Scroll speed is a dp/s parameter, not a motion token. A continuous scroll has no natural duration; the literal should be documented as the one allowed case (it is exposed as `setSpeed`, and 0 means no timer). |
| TimePicker | `components/src/time_picker.cpp:60` (`animator_` is a `QTimer`) | 4 | P3 | **Open.** The member is named `animator_` but is a `QTimer`; rename with the transport fix above, so the name cannot be mistaken for `md::animation::Animator`. |

## Checked and found sound

- **`md::animation::Animator`** (`animation/src/animator.cpp`): one
  `QVariantAnimation` reused per instance; retarget scales the remaining
  duration by `1 - progress`; zero-duration specs snap on the next turn, never
  synchronously; `stop()` preserves the last emitted value; the callback is
  invoked from a copy and guarded by a `QPointer`, so a `start()`/`stop()`/delete
  from inside it is safe. No defect. (Owned by another workstream; read only.)
- **`View` frame path** (`view/src/view.cpp:755-806`): `requestUpdate()`
  coalesces through `UiThread::requestFrame` + a `UiPriority` task, or falls
  back to `update()` headless. Ripple suspension on hide is correct.
- **`md::Animation`** (`components/src/animation.cpp`): retargets from the
  current progress in both directions, snaps for reduced motion, settles the
  child to its natural geometry, `onHidden()` collapses to 0.
- **Switch** (`components/src/switch.cpp`): token duration, reduced-motion snap,
  hidden snap, `onHidden()` stops, painted value continuous across a retarget.
  (Its `stop()`-before-`start()` gives up the animator's remaining-duration
  scaling and replays the full token on a retarget; the value stays continuous,
  so this is a P3 pacing note, not a defect.)
- **Skeleton** (`components/src/skeleton.cpp`): hidden, disabled and
  reduced-motion all stop the timer; `onShown`/`onHidden`/`onEnabledChanged` are
  the only entry points.
- **Marquee**: hidden, zero-speed and reduced-motion never run a timer; a resize
  that removes the overflow stops it.
- **Carousel**: auto-play stops when hidden, at the end of a non-looping set and
  during a drag; a hide settles the transition; reduced motion snaps; a retarget
  keeps the visible pair one step apart.
- **Banner**: retargets from the current reveal value; reduced motion snaps; a
  completed close hides itself exactly once.
- **TabBar**: retargets the indicator from its current interpolated rectangle (no
  jump to the new tab), reduced motion snaps, a disabled animation snaps.
- **ChartBase**: one animator per chart, reduced motion settles synchronously,
  repaints are throttled to ~16 ms with the final frame always requested, the
  callback is contained, and a theme change that turns reduced motion on settles
  the run.
- **Input / TextInput label**: token duration, reduced-motion and hidden snaps,
  retargets from `labelProgress_`, `settleLabel()` cancels the run.
- **Progress, Ring (determinate)**: single value property, no animation.
- **Modality/overlay lifecycle**: `Modal`, `Drawer`, `BottomSheet` all hide or
  detach their filters/layer when settled; no dangling timer after a close.
- **No blocking or sleeping loops** anywhere in `components/`: the survey found
  no `QThread::sleep`, `processEvents`, nested `QEventLoop` or spin loop in an
  animation path.
- **Not motion, deliberately**: `Clock`'s 1 s tick, `Scheduler`'s refresh timer,
  `Notification`'s auto-dismiss and `Tooltip`'s show delay are time-based state,
  not animation; they correctly are not on the motion ladder.

## Blocked by concurrent workstream

These files are uncommitted in another agent's workstream and were read but not
edited. Defects here need routing.

| Component | file:line | Rule | Severity | Finding |
|---|---|---|---|---|
| SwitchingOverlay | `components/src/switching_overlay.cpp:333` | 10 | P3 | A second `Animator` (`hold_`) is started with an empty callback `[](qreal) {}` purely to measure a delay. It works, but it runs a frame loop that paints nothing; a `QTimer::singleShot`-style hold or the animator's delay field would express the same wait without a second running animation. |
| ThemeSwitcher | `components/src/theme_switcher.cpp` (whole file) | 7 | P2 | Inspection found no animation clock at all (no `QTimer`/`QElapsedTimer`, no `Animator::start`, no progress state) in the changed file, while rule 7 asks a theme change to interpolate colours rather than replace the animated value. If the cross-fade lives in a path this audit could not see, the owning agent should confirm it; otherwise the switch is instantaneous. |

Also read and found sound in that workstream: `Modal` (scrim retargets
from/target, manual-clock seam, `hideEvent` settles), `Language` and
`DropdownTrigger` (chevron retargets from the current value, one animator each),
`ContainerTransform` (single animator, from/target retarget, manual clock),
`Font` (no animation path).

## Public API

No public signature was changed. Two protected overrides were replaced by the
documented hooks they were bypassing (`Ring`/`Progress`: `showEvent`/`hideEvent`
→ `onShown`/`onHidden`), one protected hook was added (`ChartBase::onHidden`),
and three components gained private members only (`QElapsedTimer`, cached
`QEasingCurve`). `PieChart` lost no public API: `revealProgress()` and
`isRevealing()` remain and now report the base chart's run.

## Tests

`tests/components/tst_motion_continuity.cpp` — manual clock only, no sleeping:

- an interruption carries the current value into the new run (`progress()` is
  unchanged across `start()`, and the first frame after it is within 5 % of the
  value before it);
- an interruption consumes only the remaining duration (advancing
  `(1 - progress) * duration - 15 ms` is still running; `+20 ms` finishes);
- a settled run requests no further frames (the callback count does not grow);
- a reduced-motion spec snaps on the first turn and emits exactly one frame;
- `Progress` and `Ring` track `LifecycleState` through `onShown`/`onHidden`, and a
  hide mid-animation stops the animator in the same turn;
- a pie's sweep is the base chart's run (`revealProgress()` equals
  `animation().progress()`), a hidden pie settles at 1.0 and stops, and a
  reduced-motion pie snaps.

## Three most user-visible defects

1. **Every pie chart animated twice** (P0): its own sweep ran on top of
   ChartBase's reveal; the base run painted nothing, so a data update played an
   invisible animation while the visible pie jumped. Now one owner.
2. **A delayed frame stretched every overlay animation** (P1, BottomSheet,
   Drawer, Pager): counting timer ticks instead of measuring time meant the sheet,
   drawer and pager took longer than their token whenever the UI hiccuped, and
   drifted out of step with the animations beside them.
3. **Hiding a spinner left the View lifecycle behind** (P1, Ring, Progress): the
   components replaced `showEvent`/`hideEvent` outright, so `View` never learned
   the widget was shown or hidden — the ripple was never suspended and hover or
   press state could survive a hide. Now they use the lifecycle hooks.
