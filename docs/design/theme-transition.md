# Theme-switch reveal — research and design

**Status:** design proposal, not implemented.
**Scope:** the Font / Theme / Language switch surfaces in this repository. The
same mechanism serves all three; "theme" below is shorthand for any of them.
**Non-goals:** build instructions, source changes, and the CMake wiring. This
document specifies behaviour, geometry and an API, not an implementation.

The request was for a shape that expands from the bottom-left corner in a fan,
covers the window while the new configuration is applied underneath, and then
clears to reveal the new UI — one continuous transition, not a fade.

The recommendation is:

- **the fan (sector) sweep as the signature effect**, from whichever window
  corner is nearest the tapped control, with the circle as the generic fallback
  when the trigger is a non-corner control;
- **one CPU snapshot plus an inverse-mask overlay painted with `QPainter`**, not
  an RHI pass, because the new UI is a `QWidget` tree rather than a texture and
  because a 4K snapshot at device pixel ratio 2 is four times larger than the
  largest render target this codebase accepts;
- **about 0.6–0.7 s of visible motion** at a laptop window size, clamped by
  distance, with the whole interaction finishing in under 0.8 s.

---

## 0. Summary of the decision

| Question | Answer | Why |
| --- | --- | --- |
| Default shape | Sector (fan), 90° sweep from the nearest window corner | Requested; the corner origin makes the sweep a single 90° arc rather than a full turn |
| Fallback shape | Circle from the trigger centre | Works for any trigger position, and is the form every published implementation uses |
| Visible duration | 340–680 ms depending on window size; ~630 ms at 1440 × 900 | Large surfaces need the long end of the duration ladder; small windows must not feel sluggish |
| Curves | `emphasized-decelerate` for the sweep, `standard` for the settle | The new UI is entering; the mask must not overshoot |
| Render path | CPU snapshot + `QPainter` mask in a child overlay | No RHI dependency, works under `ctest`, and the snapshot never enters `render::RenderTarget` |
| Snapshot cost | 126.6 MiB at 3840 × 2160 with dpr 2 | Measured arithmetic, not a benchmark; the budget and fallback ladder are in §4.4 |
| Reduced motion | Snap, no snapshot, no overlay | The repository already collapses every spec to zero duration when the motion scheme asks for it |
| Second switch during a reveal | Retarget before the swap; coalesce afterwards | A queued obsolete theme is worse than a short second transition; details in §5 |

The two alternatives considered and rejected as the default are in §6: an
ink-bloom circle with a soft edge, and a diagonal wipe with a spring overshoot.

---

## 1. Anatomy

The transition has five phases. Only **Uncover** is the visible animation;
**Capture**, **Cover** and **Swap** are bounded by one or two frames each, and
**Settle** overlaps the end of Uncover. The phases are logical states of the
implementation, and each one has a failure mode if it is skipped.

| Phase | Wall time | On screen | What the user must not see | If the phase is skipped |
| --- | --- | --- | --- | --- |
| **Capture** | one frame, budget ≤ 16 ms | the old UI, unchanged | a frame in which the control has begun its pressed/ripple state but the snapshot was taken from an earlier frame | the snapshot is taken after the configuration changes, so the frozen frame is already the new UI and the reveal shows nothing |
| **Cover** | ≤ 1 frame | the same old UI, now a still image filling the window | any offset, scale or palette difference between the live frame and the plate | the live tree repaints to the new configuration on the first frame and the swap flashes |
| **Swap** | 1–2 frames | still the old image; underneath it, the tree repaints in the new configuration | a flash of new UI through the plate, a half-repainted frame, or the old configuration reappearing | there is nothing new to reveal; the mask would uncover the old UI |
| **Uncover** | 340–680 ms | old image outside the shape, live new UI inside; only the leading edge moves | a stuttering edge, a lag between mask and plate, or a seam where the new UI is only partly painted | the configuration changes instantly — the exact "flash" the request rejects |
| **Settle** | 120 ms, overlapping the last 60 ms of Uncover | the complete new UI; the 1 px edge lip fades; input and focus return | a second content animation competing with the wipe, a lingering lip, or the first click being swallowed | an abrupt stop at the last corner, and a click in the first 100 ms may be lost |

### 1.1 The inversion that makes the effect cheap

The brief describes the shape as an object that covers the screen and then
disappears. The implementation inverts the ownership: **the frozen old frame is
the cover**, and the shape is a hole in it through which the live new UI is
visible. The perceived motion is the same — the new configuration grows out of
the corner and consumes the old one — but the inverse mask is what a single
`QWidget` overlay can paint without a second full-window render every frame.

This is the same model the CSS view-transition specification uses: the old state
is captured as a static image, the new state stays live, and the transition is
painted in a separate layer over both
([CSS View Transitions, §1.1 and §1.6](https://drafts.csswg.org/css-view-transitions-1/)).
The two Flutter implementations of the dark-mode circle arrive at the same
pipeline: capture the screen, apply the theme, wait one frame, then animate the
circle over the captured image
([theme circle animation](https://pub.dev/documentation/flutter_theme_circle_animation/1.0.2/flutter_theme_circle_animation/ThemeCircleAnimationState/toggle.html),
[circular theme reveal](https://pub.dev/documentation/circular_theme_reveal/1.0.1/circular_theme_reveal/CircularThemeRevealOverlayState/startTransition.html)).

### 1.2 Capture

Take `window->grab()` **after** the picker sheet has finished closing, **before
any scrim or busy surface is shown**, and **before the configuration changes**.
In this repository the natural point is `ThemeSwitcher::startPendingSwitch()`
(and its Font and Language equivalents), **not**
`SwitchingOverlay::performWork()`: by the time that callback runs the modal is
open at full dim and the content root is disabled, so a grab there would freeze
a dimmed, disabled window. The reveal replaces the scrim rather than sitting
inside it. The grab must be synchronous: an asynchronous grab would let the
configuration change land first.

Two details matter:

- **Capture after the sheet exits**, not on the press. The picker's own exit
  runs for 250 ms; capturing mid-exit would freeze a half-closed card and then
  wipe it away as if it were the page.
- **Capture before the content is disabled.** Disabling the content root repaints
  every enabled widget with disabled tokens; that repaint must not reach the
  plate. The order is capture → plate → disable → apply.

If this phase is skipped, the plate is new UI and the effect is a no-op — or
worse, a flash, because the configuration changed while the window was visible.

### 1.3 Cover

Paint the snapshot into an overlay that exactly covers the window's client
rectangle, raise it, and disable the content tree. The window looks pixel-for-
pixel identical; the difference is that it is now a still image.

The overlay must be **opaque for its whole area minus the hole**. If it is
painted with alpha, the new UI shows through everywhere and the swap is visible.
If it is offset by even one device pixel, the frozen frame appears to shift at
the moment of the click, which reads as a bug rather than a transition.

The existing `SwitchingOverlay` already contains the right input discipline: a
`ContentGuard` disables the content root, restores it on every exit path, and
captures the focus widget before the modal steals it. Reuse that, not a second
input-blocking mechanism.

If this phase is skipped, the live tree paints the new configuration on the
first frame after the swap and the user sees the flash.

### 1.4 Swap

Apply the new configuration and force one full repaint of the tree, all behind
the plate. The swap is not animated; it is a commit point.

What the user must not see is a frame where part of the tree is new and part is
old. On this repository a theme change posts `QEvent::ThemeChange` to every
top-level widget and its subtree
([`theme.h`](../../theme/include/md/theme/theme.h)), and each `View` repaints
from the new snapshot on its next paint. Widgets paint lazily, so "the tree is
new" is only true after one full pass. Waiting one frame after
`setApplicationTheme()` is the minimum; two frames is the safe value on a
compositor that batches.

This is also the last moment at which a retarget can be absorbed without a
second animation: if the user picks another value before the first reveal
frame, replace the pending configuration and continue the same sweep (§5).

The duration above assumes a theme swap, which is fast. A font reload or a
retranslation can take much longer, so the swap is **work-bound**: the plate
stays at full coverage until the work returns, and Uncover starts on the frame
after it does. There is no separate minimum hold — the plate is the busy state.
A work callback that throws still ends the hold and finishes the reveal, with
the reason in `lastError()`, exactly as `SwitchingOverlay` does today.

If this phase is skipped, the hole reveals the old UI and the transition
accomplishes nothing.

### 1.5 Uncover

The only phase with a duration. The leading edge moves; the plate is eaten from
the origin outward.

Three properties define "high craft" here:

1. **The edge is the only thing that moves.** The new UI behind it is static.
   Any content that animates during the wipe (a scale-in, a stagger) competes
   with the edge and makes the wipe look like a background to a second
   animation.
2. **The plate never lags the mask.** Both are painted in one `paintEvent`, from
   the same progress value, so they cannot tear.
3. **Coverage is monotone.** The mask only grows. A curve with overshoot would
   move the edge backwards for a few frames and re-reveal old pixels — visually
   a flicker, and a correctness bug rather than a taste question.

If this phase is skipped, the configuration changes instantly: the flash.

### 1.6 Settle

The sweep ends; the last visible pixels are the far corner. Settle is the
hand-back:

- the 1 px highlight lip that rides the leading edge fades over 120 ms;
- the overlay is hidden and the snapshot released;
- the content root is re-enabled and focus returns to the trigger (or to the
  window when the trigger is gone);
- interaction is live from the next frame.

No content motion belongs here. The one exception is a theme that leaves the
layout unchanged, where a 0.5 % scale settle on the content is defensible; for a
density or font change it is not, because the new layout has already been
measured (§5).

If this phase is skipped, the effect stops dead on the last corner and the first
click after the animation can be eaten by an overlay that is still up.

---

## 2. Geometry

### 2.1 Conventions

All geometry is computed in **window-local logical pixels**: the client area is
`W × H`, the origin is the top-left corner, `x` grows right and `y` grows down.
The device pixel ratio `dpr` is applied only when the path is rasterised, so the
formulas below are resolution-independent. Snap the origin to the physical grid
once at the start — `O ← round(O · dpr) / dpr` — otherwise a half-pixel origin
makes the edge shimmer for the first few frames.

For a point `p`, define

```
ρ(p) = hypot(p.x − O.x, p.y − O.y)          distance from the origin
θ(p) = atan2(p.y − O.y, p.x − O.x)          screen angle, clockwise from +x
u(θ) = (cos θ, sin θ)                        unit vector, y down
```

`θ` is measured **clockwise** in screen space because the fan from a corner
sweeps that way for three of the four corners. `QPainterPath::arcTo` measures
anti-clockwise from 3 o'clock, so its angle is `α = −θ` in degrees.

### 2.2 Circle

**Radius.** The farthest point of an axis-aligned rectangle from any point is a
corner, so

```
R = max over corners C of ρ(C)
  = hypot(max(O.x, W − O.x), max(O.y, H − O.y))
```

This is exactly the expression used by the published circular-reveal recipe,
which computes the end radius as the distance to the furthest corner from the
click point
([CSS View Transitions, §1.6](https://drafts.csswg.org/css-view-transitions-1/)).
For an origin on the bottom-left corner, `R = hypot(W, H)`.

**Animation.** With an eased progress `e(t) ∈ [0, 1]`,

```
r(t) = R · e(t)
```

and the mask is the disc `{ p : ρ(p) ≤ r(t) }`. The area of the *window* covered
is the area of the disc intersected with the rectangle, not `π r²`; the design
does not need the exact intersection, but a test that asserts "50 % of pixels"
must compute it rather than assume it.

**Origin from the control.** Map the control's centre into window coordinates:

```
O = trigger->mapTo(window, trigger->rect().center())
```

For a button that is not square, the **leading corner** of the control is a
better origin than its centre: it makes the circle appear to grow out of the
control rather than out of the middle of it. For a bottom-left toolbar, that is
`trigger->mapTo(window, QPoint(0, trigger->height()))`.

**Off-screen trigger.** Clamp rather than recentre:

```
O.x = clamp(O.x, 0, W)
O.y = clamp(O.y, 0, H)
```

A circle growing from just outside the edge still reads as coming from the
control; a circle jumping to the window centre does not. If the trigger is null,
destroyed, hidden, or on another screen entirely, use the window centre for the
circle and the nearest corner for the sector.

### 2.3 Fan (sector)

**Corner choice.** A rectangle corner subtends exactly **90°** of the window,
not 360°. That is the most important geometric fact in this design: the fan the
product asked for is a quarter-turn, not a full rotation. Any implementation
that sweeps 360° spends three quarters of its duration drawing outside the
window.

Pick the corner nearest the trigger centre:

```
corner = argmin over {BL, BR, TR, TL} of ‖triggerCentre − corner‖
```

For each corner, the window occupies the quadrant between the two edges that
meet there. With `θ` clockwise from `+x`:

| Corner | Origin `O` | Start `θ₀` | End `θ₁ = θ₀ + 90°` | First ray along |
| --- | --- | --- | --- | --- |
| Bottom-left | `(0, H)` | −90° | 0° | left edge, upwards |
| Bottom-right | `(W, H)` | 180° | 270° | bottom edge, leftwards |
| Top-right | `(W, 0)` | 90° | 180° | right edge, downwards |
| Top-left | `(0, 0)` | 0° | 90° | top edge, rightwards |

**Radius.** Every point of the window must be inside the swept sector at the
end, so

```
R = max over corners C of ρ(C) = hypot(W, H)     (corner origin)
```

The far corner is the one diagonally opposite the origin.

**Sweep.** With eased progress `e(t)`,

```
θ(t) = θ₀ + 90° · e(t)
S(t) = { O + ρ·u(θ) : θ₀ ≤ θ ≤ θ(t), 0 ≤ ρ ≤ R }
```

A point `p` is revealed when `θ(p) ∈ [θ₀, θ(t)]`; the radius test is automatic
because the whole window lies inside the `R` disc.

**Path construction.**

```
QPainterPath sector(O, θ0, θ(t), R):
    path.moveTo(O)
    path.lineTo(O + R·u(θ0))
    rect = QRectF(O.x − R, O.y − R, 2R, 2R)
    path.arcTo(rect, −θ0, −(θ(t) − θ0))     // Qt angles are −θ
    path.closeSubpath()
```

with a **minimum initial width** of about 0.75°: a zero-angle path is a
degenerate line and rasterises as a one-pixel hairline that flickers on the
first frame.

Two Qt details: `arcTo` measures degrees anti-clockwise from 3 o'clock, hence
the negated angles; and its angles are **eccentric** angles, which coincide with
real directions only when the bounding rectangle is square. The rectangle above
is `2R × 2R`, so it is square by construction — do not substitute a window-sized
rectangle
([`QPainterPath::arcTo`](https://doc.qt.io/qt-6/qpainterpath.html#arcTo)).

**Overshoot.** End at `θ₁ + 2.5°`, not `θ₁`. The extra wedge is outside the
window, so it costs nothing and it guarantees that the last visible corner is
passed *before* `t = 1`; otherwise the corner is approached asymptotically and
the last frame looks like it never quite lands.

**Coverage is not linear in angle.** For the bottom-left origin and a 16:9
window, the diagonal corner (top-right) is at

```
θ_diag = −atan(H / W)          ≈ −29.4° for 16:9
```

so it is covered at `(90° − 29.4°) / 90° ≈ 67 %` of the sweep, when exactly half
the window's area has been revealed. The covered **area** fraction is

```
F(θ) = −½·(H/W)·cot θ                    for θ₀ ≤ θ ≤ θ_diag
F(θ) = 1 + ½·(W/H)·tan θ                 for θ_diag ≤ θ ≤ θ₁
```

which gives, at 25 / 50 / 75 % of a linear 90° sweep: **11.6 % / 28.1 % /
63.2 %** of the window. The fan is strongly **back-loaded**: the first half of
the time reveals barely more than a quarter of the area, and the last quarter
reveals more than a third. That is the perceptual problem the easing has to
solve (§3), and it is the opposite of the circle, which front-loads its area.

If an even-area sweep is wanted instead, invert `F`:

```
θ(t) = −atan( H / (2·F·W) )              when F ≤ 0.5
θ(t) = −atan( 2·(1 − F)·H / W )          when F ≥ 0.5
```

This is exact but it makes the edge fast at the start and slow in the middle;
the simpler decelerating angle in §3 gives most of the benefit with one token
curve.

### 2.4 Sector versus circle at equal duration

The two shapes do not read at the same speed for the same duration.

- **Circle.** The boundary moves radially. Its speed is `R · e′(t) / T`, with an
  average of `R / T`; the revealed area grows as `r²`, so a decelerating radius
  curve front-loads the *area* heavily.
- **Fan.** The boundary is a ray rotating at
  `ω = (π/2) · e′(t) / T`. A point on that ray at distance `ρ` moves
  tangentially at `ρ·ω`; at the far corner, `ρ = R`, so the leading edge moves at
  `(π/2) · R · e′(t) / T`, with an average of **1.57 · R / T**.

At equal duration the fan's far edge is therefore **57 % faster** than the
circle's edge for the same radius, and its area is revealed back-loaded rather
than front-loaded. Two consequences:

1. **Duration.** Either accept the extra speed as the effect's energy — the
   recommendation, at about 1.18× the circle's duration — or lengthen the fan to
   `(π/2)·T_circle` to match rim speed exactly, which at a laptop size would be
   ~830 ms and too slow. A full 360° sweep would be `2π·R/T` at the rim, i.e.
   6.3× the circle, and is rejected for that reason as well as the wasted
   off-window time.
2. **Easing.** Because the area is back-loaded, the angle curve should be
   front-loaded: `emphasized-decelerate` advances the angle quickly at first and
   lands softly. The deceleration roughly cancels the area convexity, and it
   puts the slow part of the motion where the last corner is being finished.

### 2.5 The corners where the sweep is slow

The corners are not equally timed, and the two shapes fail differently.

For a bottom-left fan on a 16:9 window:

| Corner | Angle | Distance | Revealed at | Behaviour |
| --- | --- | --- | --- | --- |
| Top-left | −90° | `H` | 0 % | the first ray; covered in the first frame, at a short radius |
| Top-right | −29.4° | `hypot(W,H)` | 67.4 % | the farthest point, but not the last one |
| Bottom-right | 0° | `W` | 100 % | the last point; the ray is nearly parallel to the bottom edge |

The bottom-right is the corner where the sweep is *slow to arrive*, and it is
also where the ray is longest and moving fastest in absolute terms. The visual
result is a thin, long sliver of old UI along the bottom edge in the final 10 %
of the animation. The overshoot in §2.3 removes the asymptotic tail; the
decelerating curve makes the last degrees take longer, which reads as a landing
rather than a race.

For the circle, all four corners are on the same expanding arc, so the farthest
corner is reached last and, with a decelerating curve, its final approach has
zero velocity. That is the "crawl into the corner" people notice on slow
circular reveals. The fix is the same overshoot: compute `R` with a 2 % margin
and finish the radius at 92 % of the timeline, spending the remainder in
settle.

### 2.6 The anti-aliased edge

The mask edge is a straight ray for the fan (plus an outer arc that is normally
outside the window) and an arc for the circle. Three rules:

- **Do not use `setClipPath` for the mask.** Qt's raster engine clips to an
  integer region; a clip path gives a jagged edge at every angle. Paint the
  plate, then erase the hole with an anti-aliased fill:
  `painter.setCompositionMode(QPainter::CompositionMode_DestinationOut)` and
  `painter.fillPath(hole, Qt::black)`. Path fills are anti-aliased, so the
  subtraction leaves a soft edge. This requires the overlay to have an alpha
  backing store (`WA_TranslucentBackground`, `WA_NoSystemBackground`).
- **Feather width.** One logical pixel is enough at dpr 1; at dpr 2 it is two
  physical pixels, which is the right weight. A wider feather (8–24 px) is the
  ink-bloom alternative in §6, not the default: it softens the edge into a fade
  and weakens the "one continuous surface" read.
- **Lip.** A 1 px stroke along the leading edge in the new surface colour, at
  ~18 % alpha, gives the edge a physical thickness and hides the last
  half-pixel of aliasing. It fades during settle.

If the platform cannot give a child widget an alpha backing store, fall back to
an even-odd clip (`QPainterPath` with `Qt::OddEvenFill` containing the window
rectangle and the hole) and accept an aliased edge, or move the overlay to a
frameless top-level window with `Qt::WindowTransparentForInput`.

---

## 3. Timing and easing

### 3.1 The table

Durations assume a 1440 × 900 logical window (`R ≈ 1698` px). Phase times are
wall time; the sweep is the only animated part.

| Phase | Duration | Curve | Reason |
| --- | --- | --- | --- |
| Capture | 0 ms animated; ≤ 16 ms wall | — | One synchronous grab; it is not motion and must not be eased. Budget is one frame; above ~50 ms the effect is already late (§4.5). |
| Cover | 0 ms; ≤ 1 frame | — | A repaint of the same pixels. Nothing to animate. |
| Swap | 16–33 ms (1–2 frames) | — | One theme application plus one full repaint, hidden behind the plate. Two frames covers compositor batching. |
| Uncover (fan) | 629 ms | `emphasized-decelerate` (0.05, 0.7, 0.1, 1.0) | Large surface, so the long end of the ladder; decelerating balances the back-loaded area and lands on the last corner. |
| Uncover (circle) | 531 ms | `emphasized-decelerate` | Same reasoning; the circle is cheaper to track because its edge is radial. |
| Settle | 120 ms, starting at 90 % of Uncover | `standard` (0.2, 0.0, 0.0, 1.0) | A small property change (lip opacity, focus ring); standard is the token for a small change. |

Total wall time at 1440 × 900: **≈ 740 ms** end to end — 16 ms capture, 33 ms
swap, 629 ms sweep, and a 60 ms settle tail that overlaps the sweep.

### 3.2 Why these numbers

The duration ladder in this repository runs from 50 ms to 1500 ms in the
documented token steps, with `Long2 = 500 ms` and `Extended1 = 750 ms`
([`tokens/motion.h`](../../tokens/include/md/tokens/motion.h)). Two external
anchors bracket the choice:

- The design system's motion guidance ties duration to the size of the change: a
  transition that covers a large surface needs a longer duration than one that
  covers a small surface, and the container-transform pattern is the canonical
  case of a whole surface moving
  ([M3 motion](https://m3.material.io/styles/motion/overview)).
- Usability guidance puts most interface animation at **100–500 ms**, notes
  that duration should grow with complexity and travel distance, and observes
  that animations are far more often too long than too short
  ([NN/g, Executing UX Animations](https://www.nngroup.com/articles/animation-duration/)).

Those two pull in opposite directions for a full-window reveal. The resolution
is: sit at the top of the usability range rather than above it, and make the
duration depend on distance so that only genuinely large windows pay. Theme
switching is infrequent (a settings action), which is the case where the longer
end is acceptable; a frequently used control should use the circle at the
shorter end.

### 3.3 Distance scaling

A fixed 500 ms is wrong at both ends: it is sluggish in a 480 × 360 tool window
and too quick across a 4K display. Use constant edge speed with a floor and a
ceiling.

```
T_circle(R) = clamp(300, 600, R / 3.2)          ms, R in logical px
T_fan(R)    = clamp(340, 680, R / 2.7)          ms
```

| Window | `R` | Circle | Fan |
| --- | --- | --- | --- |
| 480 × 360 | 600 | 300 ms (floor) | 340 ms (floor) |
| 1280 × 800 | 1509 | 472 ms | 559 ms |
| 1440 × 900 | 1698 | 531 ms | 629 ms |
| 1920 × 1080 | 2202 | 600 ms (ceiling) | 680 ms (ceiling) |
| 3840 × 2160 | 4406 | 600 ms (ceiling) | 680 ms (ceiling) |

The ceiling is deliberate. At a constant 3.2 px/ms a 4K window would need
1.47 s, which is far outside the usable range; the design accepts a faster edge
on very large displays rather than a slow transition. The fan's divisor (2.7
rather than 3.2) is the 1.18× compensation from §2.4: it absorbs part of the
57 % rim-speed difference and leaves the rest as the effect's energy.

Apply the theme's `reducedMotion` flag **before** this table: a reduced-motion
scheme resolves every duration to zero and the transition snaps
([`tokens/motion.h`](../../tokens/include/md/tokens/motion.h)).

### 3.4 Curves, damping and why not a spring

The repository's motion tokens are cubic Béziers, not springs. `Standard` and
`Emphasized` currently export the same control points, `(0.2, 0, 0, 1)`, and the
emphasised family gets its character from the accelerate and decelerate
variants and from longer pairings
([`tokens/motion.h`](../../tokens/include/md/tokens/motion.h)).

The design system now also publishes **spring** tokens alongside the duration
and curve tokens. Their damping ratios are 0.9 for standard spatial motion and
1.0 for effects, and 0.8 (spatial) / 0.6 (the bounciest spatial token) / 1.0
(effects) for the expressive scheme; a spring is interruptible and velocity-
aware in a way a fixed-duration curve is not
([spring tokens](https://pub.dev/documentation/material_design/1.8.1/material_design/M3ESpring.html)).
A cubic Bézier has no damping ratio of its own; the two systems are different
parameterisations, and mapping one to the other requires a fit that the design
system does not publish.

This design stays on the curves for two reasons:

1. `animation::Animator` drives a duration and a curve, and it already has the
   manual clock, retargeting and zero-duration semantics the tests need
   ([`animator.h`](../../animation/include/md/animation/animator.h)). A spring
   would be a second animation primitive in the layer.
2. **Overshoot is a correctness problem for a mask, not just a taste one.** A
   spring at damping 0.8 overshoots; an overshooting mask moves the edge
   backwards and re-reveals old pixels. If springs arrive later, damping 1.0 is
   the only safe value for Uncover, and the settle is the phase that could take
   a bouncier token.

`emphasized-decelerate` `(0.05, 0.7, 0.1, 1.0)` is the right curve for an
entering surface: it starts quickly and lands slowly, and its end slope is zero,
which is why the overshoot in §2.3 is needed to avoid an asymptotic finish.

---

## 4. Rendering approach for this codebase

### 4.1 What the layers already provide

The pieces the effect needs exist, but not in one place:

- **`gfx`** is a software rasteriser that executes a command buffer into a
  `QImage`; it is deterministic and has no window or GL context
  ([`rasterizer.h`](../../gfx/include/md/gfx/rasterizer.h)). It has no concept of
  a mask, a snapshot or a window.
- **`render`** has software and RHI devices behind one interface. A
  `RenderTarget` is an image plus a logical size and a device pixel ratio;
  `submit()` rasterises into it and never presents
  ([`render_target.h`](../../render/include/md/render/render_target.h),
  [`render_device.h`](../../render/include/md/render/render_device.h)). The RHI
  device renders offscreen and **reads every frame back into the target image
  before `submit()` returns**; it owns no swap chain
  ([`rhi_device.h`](../../render/include/md/render/rhi_device.h)).
- **`view`** is the component kernel: the fixed paint pipeline, the theme
  resolution, the layer stack and the damage tracker
  ([`view.h`](../../view/include/md/view/view.h)). It has no window-level
  overlay concept.
- **`components`** is where window-level surfaces live: `Modal`, its layer
  registry, `ContainerTransform`, `SwitchingOverlay`, and the Font / Theme /
  Language components, all three of which already converge on
  `SwitchingOverlay` for the configuration change
  ([`font.cpp`](../../components/src/font.cpp),
  [`theme_switcher.cpp`](../../components/src/theme_switcher.cpp),
  [`language.cpp`](../../components/src/language.cpp)).
- **`Context2D`** is a retained command façade for `Canvas`: it records drawing
  calls and replays them into a `QPainter`
  ([`context2d.h`](../../components/include/md/components/context2d.h)). It is
  per-canvas and retained-mode, so it is the wrong host for a window-wide,
  per-frame mask; the overlay should paint with `QPainter` directly, as
  `SwitchingOverlay` and `Modal` already do.
- **`TextPainter`** is the single `QPainter` bridge in `render/`
  ([`text_painter.h`](../../render/include/md/render/text_painter.h)). The
  transition draws no text, so it does not need it.

Two hard facts decide the rendering choice:

1. **The largest render target this codebase accepts is 8,388,608 pixels**
   (4096 × 2048), enforced by both the software and RHI devices
   ([`rasterizer.h`](../../gfx/include/md/gfx/rasterizer.h)). A 4K window at
   dpr 2 is 33,177,600 pixels, almost exactly four times the cap. A full-window
   snapshot therefore **cannot** go through `render::RenderTarget` at that size,
   on either device.
2. **The RHI device is an offscreen renderer with a synchronous read-back.** It
   is not a compositor for the widget tree. Routing the transition through it
   would add a read-back stall per frame and still leave the live UI as a
   `QWidget` tree that has to be rendered on the CPU first.

### 4.2 Option A — CPU snapshot plus a `QPainter` mask (recommended)

**Shape of the implementation.**

1. `QPixmap plate = window->grab();` — a full widget-tree render at the window's
   current dpr, taken **before** any scrim is shown and **before** the content
   root is disabled. The grab is deterministic and independent of what is on top
   of the window, unlike a screen capture; the repository already uses it in
   tests and demos ([`tst_view.cpp`](../../tests/view/tst_view.cpp),
   [`demo/src/main.cpp`](../../demo/src/main.cpp)).
2. Create `RevealOverlay : QWidget`, child of the window, geometry = client
   rect, `WA_TranslucentBackground | WA_NoSystemBackground |
   WA_TransparentForMouseEvents`, raised above the content and below the
   tooltip layer.
3. On each `paintEvent`: draw the plate at `(0, 0)`, then set
   `CompositionMode_DestinationOut`, enable anti-aliasing, and fill the hole
   path. The hole is the circle or the sector from §2.
4. Drive progress with `animation::Animator` and call
   `update(newlyRevealedRegion)` rather than `update()`. The newly revealed
   region is the sector annulus between `θ(t−dt)` and `θ(t)`, or the disc
   annulus for the circle; that keeps the per-frame blit proportional to the
   revealed strip, not the window.
5. At the end: hide the overlay, release the pixmap, restore input and focus.

**Why it wins here.**

- No RHI, no GL context, no native child window. It works under `ctest`'s
  offscreen platform, which is where this project's visual tests run.
- One snapshot per transition, not one per frame. The plate is static; the only
  per-frame work is the strip that the edge uncovers.
- It composes with the existing overlay machinery: the `Modal` layer registry
  for stacking, the `ContentGuard` for input, and `SwitchingOverlay` for the
  work callback and error handling.
- It is testable without pixels: the geometry is a pure function of
  `(W, H, origin, shape, progress)`, and the overlay's state is observable.

**Risks.**

- A child widget with an alpha backing store is required for the
  `DestinationOut` hole. Verify on the pinned Windows platform; if it fails, use
  the even-odd clip fallback (§2.6).
- `QWidget::grab()` re-renders the tree on the CPU. On a complex page at 4K it
  can take tens of milliseconds; the budget and fallback are in §4.4–4.5.
- Native children are not guaranteed to appear in a widget grab. The
  `GpuCompositor` is a `QOpenGLWidget`, i.e. a native child
  ([`gpu_compositor.h`](../../view/include/md/view/gpu_compositor.h)); if a
  window contains one and its content is missing from the plate, use the veil
  variant in §4.5 for that window rather than showing a hole where the
  compositor should be.

### 4.3 Option B — a GPU pass with a shader mask

**Shape of the implementation.** Upload the captured old frame as a texture;
draw a full-screen triangle whose fragment shader computes `ρ` and `θ` from the
device coordinates and selects between the old texture and the live new frame;
composite on the GPU.

**Why it is not the recommendation now.**

- The new frame is not a texture. The UI is a `QWidget` tree, so "the new frame"
  would have to be rendered on the CPU into an image and uploaded every frame.
  At 4K with dpr 2 that is 126.6 MiB per frame; at 60 Hz it is 7.6 GB/s of
  uploads before any drawing, which is not a real-time proposition.
- The only GPU surface that exists is `GpuCompositor`, which is a native
  `QOpenGLWidget` and composites only its own `View` children
  ([`gpu_compositor.h`](../../view/include/md/view/gpu_compositor.h)). It cannot
  overlay an arbitrary window subtree, and native-child stacking is already
  documented as constrained.
- The RHI device is offscreen with a synchronous read-back and the same 8.39 MP
  cap; using it as a per-frame compositor means a read-back stall in every
  frame.
- A new shader is a new build dependency (`qt6_add_shaders`, `.qsb`) for one
  effect.

Option B becomes the right answer only if the application moves to a single RHI
scene graph, or if the effect has to run at 120 Hz on a 4K surface. Neither is
true today.

### 4.4 Memory cost of a full-window snapshot

ARGB32 is four bytes per pixel; the snapshot is `W · H · dpr² · 4` bytes.

| Window | dpr | Physical pixels | Bytes | MiB |
| --- | --- | --- | --- | --- |
| 1920 × 1080 | 1 | 2,073,600 | 8,294,400 | 7.9 |
| 1920 × 1080 | 2 | 8,294,400 | 33,177,600 | 31.6 |
| 2560 × 1440 | 2 | 14,745,600 | 58,982,400 | 56.2 |
| 3840 × 2160 | 1 | 8,294,400 | 33,177,600 | 31.6 |
| **3840 × 2160** | **2** | **33,177,600** | **132,710,400** | **126.6** |

For comparison, the largest `RenderTarget` the renderer accepts is
8,388,608 pixels = 33.6 MB, so the 4K@2 snapshot is **3.96×** the cap. A naive
implementation that allocates a `RenderTarget` for the plate will be refused (a
warning, zero statistics, target untouched) rather than silently truncated.

`QWidget::grab()` returns a `QPixmap`; on the raster backend that is
image-backed, so the 126.6 MiB is one allocation. Converting it to `QImage` for
convenience doubles the peak; keep the pixmap and draw it directly. Release it
on every exit path — finish, cancel, resize, window destruction — or a 4K
session leaks a third of a gigabyte per switch.

### 4.5 When the snapshot is too expensive

Budget: **64 MiB** and **one frame** for the grab. The fallback ladder, in
order:

1. **Full plate.** `W · H · dpr² · 4 ≤ 64 MiB` and the grab completes within the
   frame.
2. **Reduced-scale plate.** Render the window into a smaller `QImage` with
   `QWidget::render()` and a scaled painter, targeting `dpr = 1` or
   `s = sqrt(budget / bytes)`. The plate is slightly soft for 600 ms; nobody
   sees it. This covers 4K@2 at 33.2 MP → 8.3 MP (dpr 1), which fits.
3. **No plate: the veil variant.** An opaque sector in the new theme's surface
   colour sweeps in, the swap happens under it, and a *second* sector sweeps it
   out. No old pixels are needed because the veil is opaque, so there is no
   capture at all. It is a different effect — two strokes instead of one, with a
   flat-colour beat — and it is the honest fallback for a window whose plate
   cannot be captured (native children, excessive size, grab failure).
4. **Snap.** Apply the configuration and stop. A failed capture must never block
   the switch.

The same ladder applies to a window that contains a `GpuCompositor` and to any
platform where the alpha backing store is unavailable and the even-odd clip
fallback is not acceptable.

---

## 5. Failure and edge cases

| Situation | Behaviour | Why |
| --- | --- | --- |
| **Window resized mid-transition** | Before the first Uncover frame: re-grab at the new size, recompute `R` and the origin, keep the progress. After Uncover has started: abort the effect, drop the plate and overlay, keep the new configuration, emit `finished(true)` with a diagnostic. | The plate is in old-window coordinates. Re-grabbing mid-reveal would show the new configuration flashing outside the mask; aborting is invisible because the configuration is already correct. |
| **Trigger control deleted mid-transition** | Continue from the captured origin. Do not hold a pointer to the control; hold a `QPointF` and a `QPointer` only for focus return. If the trigger is gone, focus falls back to the window. | The origin is data, not a live object. `SwitchingOverlay` already uses this pattern for its focus return. |
| **Second switch during the reveal** | Before the swap: retarget — replace the pending configuration, keep the sweep, and apply the newest at the swap. After the swap: let the sweep finish, then run a 120 ms crossfade to the newest configuration (no second plate, no second sweep). Never queue an obsolete value and never ignore the input. | Ignoring leaves the control showing a value that is not applied. Queueing applies a value the user has already moved past. A second full sweep would double the latency and require a second capture of a half-revealed screen. |
| **Reduced motion** | Snap: no capture, no plate, no overlay, no sweep. Apply the configuration, restore focus, emit `finished(true)` after one frame. | Every spec resolves to zero duration when the motion scheme asks for reduced motion ([`tokens/motion.h`](../../tokens/include/md/tokens/motion.h)); interaction-triggered motion that is not essential has to be suppressible ([WCAG 2.3.3](https://www.w3.org/WAI/WCAG22/Understanding/animation-from-interactions.html), [technique C39](https://www.w3.org/WAI/WCAG22/Techniques/css/C39)). |
| **Theme changes metrics (density), not only colours** | The plate is the old layout, the live UI is the new layout. Keep the spatial wipe; skip the content settle. Prefer the corner origin over the trigger origin. | A density change reflows the window, so the frozen frame and the new layout do not correspond element-for-element. A wipe is honest about that; a crossfade or a shared-element morph would pretend they match. The corner origin is immune to the trigger moving, which a reflow can do. |
| **Screenshot capture fails** | No plate. Apply the configuration, emit `skipped(reason)`, and either run the veil variant or snap. Record the reason in `lastError()`. | A failed capture is not a failed switch. `SwitchingOverlay` already treats a failed work callback the same way: the overlay closes and the error is reported. |
| **DPI change** | Abort the effect and snap, exactly as for a resize. Snap the origin to the new physical grid at the start of the next transition. | The plate's physical resolution no longer matches the window's, and the origin's pixel snap is invalid. |
| **Window minimised or hidden** | Finish the transition immediately; the configuration is already applied. Do not animate into a hidden window. | A hidden window has no visible surface to reveal, and the animator would keep a plate alive for nothing. |
| **Switching to the same value** | Do not start a transition. `setApplicationTheme()` is already a no-op for an equal theme, and the picker can compare first. | An animation that reveals "no change" is a lie. |
| **Modal or tooltip open at the moment of the switch** | The overlay sits above the content and below the tooltip layer; close or re-level transient surfaces before starting. | The reveal covers the window, so anything above it would be wiped in or out inconsistently. |

---

## 6. Two alternative signature effects

The fan is the effect the request describes, and it is the one I would ship.
The two alternatives below are worth offering as options because they fail in
different, instructive ways.

### 6.1 Ink bloom — a circle with a soft, feathered edge

The circle from §2.2, but the mask is feathered over 24–48 px and overshoots
`R` by 3 %, with a 60 ms hold at full coverage before the plate is released. The
edge is produced by a radial alpha ramp (or a blurred mask image) rather than a
hard fill, and the leading rim carries a 2 px tint of the new primary colour.

**Critique.** It is the most forgiving effect: the feather hides the aliasing
and the exact corner timing, and the colour rim reads as ink soaking into
paper. It is also the least "one surface" of the three — a soft edge is
perceptually a fade at the rim, especially on a dark theme where the bloom is
bright on dark. The feather costs a blur or a gradient mask per frame, and at
4K@2 a blurred 33 MP alpha channel is the most expensive variant here. It is an
excellent second option for a light-to-dark switch, and a poor default because
it dilutes the crispness the request is asking for.

### 6.2 Diagonal wipe with a spring overshoot

A straight edge at roughly 20° from vertical travels across the window; the mask
is parameterised by a signed distance to the line, and the line's offset is
driven by a spring (damping 0.8, the expressive spatial default) so the edge
overshoots the far side by 2–3 % and settles back.

**Critique.** It is the fastest-reading of the three and the cheapest: no
radius, no angle, one distance. The spring gives it a physical snap that a
Bézier cannot. The problem is the overshoot itself: when the edge passes the far
side and comes back, it re-reveals old pixels along the trailing edge for a few
frames. On a screen that is already new, that reads as a flicker. It can be
rescued by overshooting only *past the window* (so the return is off-screen) or
by putting the bounce into a highlight rather than the mask, at which point it
is a diagonal wipe with a decoration, not a spring. The straight edge also has
no relationship to the control that triggered it, which is the one thing the
fan does well.

### 6.3 Which ships as the default, and why

**The fan.** It is the requested effect, its 90° sweep is shorter and more
purposeful than a full turn, and its corner origin ties the motion to the
window rather than to a button that may move or disappear. The circle remains
the fallback for non-corner triggers and for any window where the corner would
be off-screen. The ink bloom is the second option in a future picker; the
spring wipe does not ship in its overshooting form.

---

## 7. Storyboard

A 16:9 window, fan from the bottom-left, linear angle for legibility of the
frames (`#` = old plate, `.` = live new UI; the boundary is between the last `.`
and the first `#`; the origin is the bottom-left corner). The percentages are of
the 90° sweep; the real animation uses the decelerating angle from §3, which
front-loads the same frames.

**0 % — angle −90°, area 0 %.** The plate covers everything. Nothing has moved
yet; this is also the frame on which input is disabled.

```
####################################
####################################
####################################
####################################
####################################
####################################
####################################
####################################
####################################
####################################
```

**25 % — angle −67.5°, area ≈ 11.6 %.** A thin wedge of new UI along the left
edge, widening towards the top. The bottom-left corner is already new; the
bottom edge is untouched.

```
........############################
.......#############################
......##############################
.....###############################
.....###############################
....################################
...#################################
..##################################
.###################################
####################################
```

**50 % — angle −45°, area ≈ 28.1 %.** The wedge has reached the top edge at
about 56 % of the width. More than a third of the time has bought barely more
than a quarter of the area — the back-loading from §2.3.

```
....................################
..................##################
................####################
..............######################
............########################
..........##########################
........############################
......##############################
....################################
####################################
```

**75 % — angle −22.5°, area ≈ 63.2 %.** The diagonal corner is long covered;
what remains is a long, thin triangle along the bottom edge, tapering to the
bottom-left corner. This is the part that must not feel rushed.

```
....................................
....................................
....................................
..................................##
.............................#######
........................############
...................#################
..............######################
.........###########################
####################################
```

**100 % — angle 0° (+2.5° overshoot), area 100 %.** The plate is gone, the lip
fades over 120 ms, and input is restored. The last pixel to change is the
bottom-right corner.

```
....................................
....................................
....................................
....................................
....................................
....................................
....................................
....................................
....................................
....................................
```

| Progress | Angle | Area | What the eye is tracking |
| --- | --- | --- | --- |
| 0 % | −90° | 0 % | the plate |
| 25 % | −67.5° | 11.6 % | the left edge, opening |
| 50 % | −45° | 28.1 % | the ray crossing the top edge |
| 75 % | −22.5° | 63.2 % | the long sliver along the bottom |
| 100 % | 0° | 100 % | the bottom-right corner, then the lip |

---

## 8. Implementation plan

### 8.1 Where it belongs

`components`. The effect needs a `QWidget` window, a `QPainter`, the theme and
the overlay/layer machinery — all of which `components` already owns
(`Modal`, `ContainerTransform`, `SwitchingOverlay`). It does not belong in
`view` (the kernel deliberately has no window-level overlays), in `render` (the
devices are offscreen and have no window), or in `animation` (no widgets).

Proposed files:

- `components/include/md/components/switch_reveal.h`
- `components/src/switch_reveal.cpp`

The name is `SwitchReveal` rather than `ThemeTransition` because Font and
Language use the same path: it generalises the existing `SwitchingOverlay`,
`SwitchingCard` family.

### 8.2 API sketch

```cpp
#pragma once

#include <md/theme/theme.h>
#include <md/tokens/motion.h>

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QString>

#include <chrono>
#include <functional>
#include <memory>

class QWidget;

namespace md {

// One reveal of the new configuration over a captured frame of the old one.
//
// The work callback is always called exactly once: behind the plate when the
// effect runs, or immediately when it is skipped (reduced motion, no window,
// failed capture). A skipped effect is not a failed switch.
class SwitchReveal final : public QObject
{
    Q_OBJECT

public:
    enum class Shape { Sector, Circle };

    // Where the shape grows from.
    enum class Origin {
        Trigger,        // the control's leading corner (Sector) or centre (Circle)
        NearestCorner,  // the window corner nearest the control
        Fixed,          // use fallbackOrigin
    };

    struct Options
    {
        Shape shape = Shape::Sector;
        Origin origin = Origin::NearestCorner;
        QPointF fallbackOrigin{};      // used when the trigger is gone or off-screen
        qreal sweepDeg = 90.0;         // Sector: a window corner subtends 90°
        qreal startAngleDeg = -90.0;   // Sector: up the left edge
        qreal overshootDeg = 2.5;      // Sector: off-screen landing
        qreal durationScale = 1.0;
        qint64 maxSnapshotBytes = 64 * 1024 * 1024;
        bool settle = true;
    };

    explicit SwitchReveal(QWidget* window, QObject* parent = nullptr);
    ~SwitchReveal() override;

    // Starts one reveal. Returns false when the effect was skipped; `work` has
    // run in either case. A call while running follows the retarget rule in §5.
    bool reveal(QWidget* trigger, Options options, std::function<void()> work);

    // Convenience overload for the theme case; applies `theme` at the swap.
    bool reveal(QWidget* trigger, Theme theme, Options options = {});

    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] qreal progress() const noexcept;
    [[nodiscard]] QString lastError() const;

    // Deterministic clock, the same seam ContainerTransform and Animator use.
    void setManualClock(bool enabled);
    [[nodiscard]] bool isManualClock() const noexcept;
    void advanceTimeline(std::chrono::milliseconds delta);

    // Cancels without applying anything further; the caller owns the state.
    void cancel();

signals:
    void finished(bool applied);
    void skipped(const QString& reason);

private:
    // Snapshot, geometry, overlay and the Idle → Capturing → Covered →
    // Swapping → Revealing → Settling state machine.
    struct Private;
    std::unique_ptr<Private> d_;
};

} // namespace md
```

### 8.3 What is reused, what is new

| Reused | Use |
| --- | --- |
| `animation::Animator` | Progress stream, manual clock, retargeting and zero-duration snap semantics. No new timer. |
| `md::MotionScheme` / `tokens/motion.h` | Duration ladder, curve tokens, reduced motion. No millisecond literal in the component. |
| `md::Theme` / `md::Density` | The colours the overlay paints (lip, scrim if any) and the dp→px conversion for the 1 px edge. |
| `Modal` layer registry | Stacking the overlay above content and below tooltips. |
| `SwitchingOverlay`'s content guard and error handling | `ContentGuard` is private to `SwitchingOverlay`; extract it (or move it to a shared header) so disabling/restoring the tree and focus, and reporting a throwing work callback, are not written twice. |
| `QWidget::grab()` | The plate. Already used by the test suite and the demo. |
| `QPainter`, `QPainterPath`, `CompositionMode_DestinationOut` | The mask and the hole. |

| New | Why it cannot be reused |
| --- | --- |
| Snapshot policy (size cap, reduced-scale render, release) | Nothing in the repository captures and retains a window image today. |
| Geometry solver (`R`, origin, 90° sector, coverage, overshoot) | No existing component models a window-wide mask. |
| `RevealOverlay` widget | The existing overlays (`Modal`, `SwitchingCard`) are scrims/cards, not masked plates. |
| Cancellation and retarget state machine | `SwitchingOverlay` ignores a second run; this one must retarget. |

Not used: `gfx` (no triangles), `render` (offscreen only, and the 8.39 MP cap),
`TextPainter` (no text), `Context2D` (retained per-canvas, not a window overlay).

### 8.4 Test list

Written for the existing `QTest` + `ctest` setup; all of it can run under the
offscreen platform with a manual clock.

**Geometry (pure functions, no window).**

1. `R` for a table of origins equals `hypot(max(x, W−x), max(y, H−y))`,
   including all four corners and a centre case.
2. Sector at `s = 1` contains all four window corners and the window centre;
   sector at `s = 0` contains only the origin ray.
3. Coverage is monotone: the mask at `t₂ > t₁` is a superset of the mask at
   `t₁`, checked on a coarse grid for 32 steps of the sweep.
4. The 16:9 area fractions at 25 / 50 / 75 % match §2.3 within 0.5 percentage
   points (this pins the `F(θ)` formula).
5. Corner choice: a bottom-left trigger selects BL and `θ₀ = −90°`; a top-right
   trigger selects TR and `θ₀ = 90°`.
6. Off-screen trigger: the origin clamps to the window edge; a destroyed
   trigger falls back to the configured origin without dereferencing it.

**Timing and state.**

7. Manual clock: `advanceTimeline(0)` → progress 0; half the computed duration
   → radius/angle within 1e-6 of the analytic value; the full duration →
   `finished(true)` exactly once.
8. Distance scaling: 480 × 360, 1440 × 900 and 4K windows produce durations
   inside the clamps and in the order the table predicts.
9. Capture budget: with a plate over `maxSnapshotBytes`, the effective scale is
   ≤ 1 and the allocation stays under the cap.
10. Retarget before the swap: the final applied value is the second one, and
    only one sweep runs.
11. Retarget after the swap: one sweep completes, then one 120 ms crossfade; no
    second plate is taken.
12. Cancel: the overlay is destroyed, the plate released, the configuration is
    whatever the caller left, and `finished` is not emitted.

**Behaviour and pixels.**

13. Reduced motion: `reveal()` returns false, the work runs synchronously, no
    overlay exists, no plate is allocated.
14. Capture failure (injected null pixmap): `skipped(reason)` is emitted, the
    work still runs, no overlay is leaked.
15. Trigger destroyed mid-reveal: no crash, geometry unchanged, focus falls back
    to the window.
16. Resize / DPI change mid-reveal: the overlay aborts, the configuration is the
    new one, `lastError()` names the reason, and no `QPointer` dangles.
17. Input: a click during the reveal does not reach the content; the first click
    after `finished` does.
18. Pixel test at 320 × 200 with the manual clock: corner pixels are the old
    colour at 0 %, a mix at 50 %, and the new colour at 100 %.
19. Density change: a theme with `density = 1.5` reveals without a geometry jump
    and skips the content settle.

---

## 9. Sources

External:

- [CSS View Transitions Module Level 1](https://drafts.csswg.org/css-view-transitions-1/)
  — old-state capture, live new state, the circular-reveal recipe and the
  furthest-corner radius.
- M3 motion and M3 spring tokens — linked inline in §3.2 and §3.4. They are the
  design system's duration/easing specification and its published spring
  damping ratios.
- [Theme circle animation API](https://pub.dev/documentation/flutter_theme_circle_animation/1.0.2/flutter_theme_circle_animation/ThemeCircleAnimationState/toggle.html)
  — capture, apply, wait one frame, animate, clean up.
- [Circular theme reveal API](https://pub.dev/documentation/circular_theme_reveal/1.0.1/circular_theme_reveal/CircularThemeRevealOverlayState/startTransition.html)
  — the same pipeline with an explicit reverse (contracting) mode.
- [NN/g, Executing UX Animations: Duration and Motion Characteristics](https://www.nngroup.com/articles/animation-duration/)
  — 100–500 ms range, duration grows with travel distance, most animations are
  too long rather than too short.
- [WCAG 2.3.3, Animation from Interactions](https://www.w3.org/WAI/WCAG22/Understanding/animation-from-interactions.html)
  and [technique C39](https://www.w3.org/WAI/WCAG22/Techniques/css/C39) —
  reduced-motion requirement and the vestibular-disorder rationale.
- [MDN, `conic-gradient()`](https://developer.mozilla.org/en-US/docs/Web/CSS/Reference/Values/gradient/conic-gradient)
  — the sector shape and its angular parameterisation.
- [Qt, `QPainterPath::arcTo`](https://doc.qt.io/qt-6/qpainterpath.html#arcTo)
  — arc angle convention and the eccentric-angle caveat.
- [Qt, composition modes](https://doc.qt.io/qt-6/qpainter.html#CompositionMode-enum)
  — `DestinationOut`, the hole-punch operation.

Repository:

- [`tokens/include/md/tokens/motion.h`](../../tokens/include/md/tokens/motion.h)
- [`animation/include/md/animation/animator.h`](../../animation/include/md/animation/animator.h)
- [`view/include/md/view/view.h`](../../view/include/md/view/view.h)
- [`components/include/md/components/switching_overlay.h`](../../components/include/md/components/switching_overlay.h)
- [`gfx/include/md/gfx/rasterizer.h`](../../gfx/include/md/gfx/rasterizer.h)
- [`render/include/md/render/render_target.h`](../../render/include/md/render/render_target.h)
- [`render/include/md/render/rhi_device.h`](../../render/include/md/render/rhi_device.h)
- [`theme/include/md/theme/theme.h`](../../theme/include/md/theme/theme.h)

*Prose uses "M3" and "the design system". The two links whose host names are the
vendors' own appear verbatim as citation identifiers; they are references, not
prose.*
