# Robustness audit — `s*` components, `event/` and `drawable/`

Read-only audit. No code was modified and nothing was built. Scope was exactly:

- the 12 `components/src/s*.cpp`: `scatter_chart`, `schedule`, `scheduler`,
  `scrollbar`, `search_bar`, `seekbar`, `skeleton`, `spectrogram_chart`,
  `spectrum_chart`, `state`, `sub_menu`, `switch`;
- the matching 12 `components/include/md/components/*.h`;
- `event/` — 8 headers, 7 sources and `CMakeLists.txt`;
- `drawable/` — 6 headers, 6 sources, the private `drawable_hash_p.h` and
  `CMakeLists.txt`.

Everything below comes from reading the source. There is no runtime evidence:
nothing was instrumented, sanitised or executed. One Qt claim is backed by the
upstream 6.8 source (linked in S-01) because the defect depends on Qt's event
delivery; every other claim is derived from the code in this repository.

`docs/audit/s.md` is the only file written. No finding here is style-only; the
"checked and found sound" section at the end records what was deliberately
traced and cleared, so silence about a file is not an omission.

## Headline

| Severity | Count |
|---|---|
| P0 | **1** |
| P1 | **1** |
| P2 | **7** |

The P0 is a normal keyboard interaction on `SearchBar` that recurses without
bound; it is the one finding that should block a release. The P1 is the
dispatcher's silent `catch (...)`: containment is correct, the absence of any
diagnostic is not. The seven P2s are hardening: one exception-boundary gap in
the dispatcher, one painter-state leak, one stale-state bug, three
undefined-behaviour paths on unvalidated numeric input, and one missing
diagnostic in the drawable layer.

## Findings

| id | file:line | severity | issue | fix |
|---|---|---|---|---|
| S-01 | `components/src/search_bar.cpp:447-458` (send at `453`) | **P0** | `keyPressEvent()` re-sends the key event it is handling to `editor_`; `QApplication::notify()` walks an unaccepted key up to the parent widget (`SearchBar`), so the handler re-enters itself without bound → stack overflow | Do not forward the in-flight event; the editor is the focus proxy and already receives every key. If a host routes keys to the bar, forward a copy once behind a re-entrancy flag |
| S-02 | `event/src/event_dispatcher.cpp:83-99` (same pattern `event/src/gesture_detector.cpp:63-67`) | **P1** | Every handler exception is swallowed with no diagnostic; dispatch continues as if the handler returned `false`, so a throwing public handler is invisible | Keep the containment, add a rate-limited `qWarning` naming the target and the exception |
| S-03 | `event/src/event_dispatcher.cpp:257, 274, 294, 308` (helpers at `106-152`) | P2 | `EventTarget::mapPointFrom()` is called outside the exception boundary; a throw escapes `dispatch()` and breaks the header's "handlers may throw, the chain cannot break" contract (F.6) | Wrap `prepare()`/`resetForDispatch()` in `try`/`catch`; abort the pass and return `finished(event)` |
| S-04 | `components/src/schedule.cpp:708, 816, 851, 921-926` | P2 | The catch-and-continue paint path restores only one of the nested `save()`s; on a throw the View's later stages run with the schedule's clip/opacity still pushed (and `QPainter` warns at destruction) | Use an RAII painter guard per nested region, as `scheduler.cpp:167-186` already does |
| S-05 | `components/src/spectrogram_chart.cpp:158-181`, `714-735` | P2 | `setFrames()`/`setMagnitude()` replace the matrix but never clear the hover indices/values; the crosshair and readout keep reporting the previous matrix's time, frequency and level | Call `setHoveredCell(-1, -1)` in both setters |
| S-06 | `components/src/scrollbar.cpp:181, 198, 252`; `121`; `265`; `433` | P2 | 32-bit range/step arithmetic overflows for legal extreme values (`setRange(INT_MIN, INT_MAX)`, `setPageStep(INT_MAX)` + PageDown at the end): signed-overflow UB (INT32-C) | Compute the span and page target in `qint64`/`qreal`; clamp the page step to the span |
| S-07 | `components/src/seekbar.cpp:155, 168-169, 311` | P2 | `setValue`/`setRange`/`setStep` accept non-finite or denormal values; `qRound()` on the resulting unbounded ratio is UB, and `NaN` propagates into the emitted value and painter geometry | Reject non-finite input, and bound the step ratio before `qRound` |
| S-08 | `drawable/src/drawable_hash_p.h:42-54` (`shape_drawable.cpp:47-53`, `shadow_drawable.cpp:231-236`) | P2 | `qRound64()` is applied to unvalidated `qreal`s: `ShapeDrawable::setStroke(colour, NaN)` or a `Shape` with non-finite corner radii makes `cacheKey()` undefined (float→int out of range) | Reject non-finite values in the setters; make the hash helpers finite-safe |
| S-09 | `drawable/src/layer_drawable.cpp:46-49` (also `icon_drawable.cpp:235-239`, `shadow_drawable.cpp:342-346`, `state_layer_drawable.cpp:68-70`) | P2 | A child drawable's exception is discarded with no diagnostic; the composite keeps painting and a broken primitive silently disappears | Keep the containment, add a rate-limited `qWarning` with the child index and the reason |

---

### S-01 — `SearchBar` re-sends the key event it is dispatching and recurses to stack overflow

**file:line** `components/src/search_bar.cpp:447-458`, `QCoreApplication::sendEvent` at `453`.

`SearchBar` embeds a transparent `QLineEdit` as its focus proxy
(`search_bar.cpp:230-234`) and installs itself as the editor's event filter
(`:233`). `keyPressEvent()` then forwards *the same* `QKeyEvent` object to the
editor:

```cpp
void SearchBar::keyPressEvent(QKeyEvent* event)          // :447
{
    if (editor_ != nullptr) {
        QCoreApplication::sendEvent(editor_, event);     // :453
        return;
    }
    View::keyPressEvent(event);
}
```

Reachable call path (no special configuration):

```
editor_ has focus (it is the bar's focus proxy)
  key press that QLineEdit does not consume
  (F5, or Down/Up when filtered_ is empty, etc.)
    → SearchBar::eventFilter            search_bar.cpp:471-510   not consumed
    → QLineEdit::keyPressEvent          ignores the key
    → QApplication::notify()            walks an unaccepted key to parentWidget()
    → SearchBar::keyPressEvent          search_bar.cpp:447
        → QCoreApplication::sendEvent(editor_, event)            :453
            → filter, QLineEdit ignores, parent walk again
            → SearchBar::keyPressEvent again  ← unbounded recursion
```

The decisive detail is Qt's own delivery loop. `QApplication::notify()`
contains, for `KeyPress`/`KeyRelease`/`ShortcutOverride`, a `while (w)` that
retries the event on `w->parentWidget()` until the event is accepted or the
widget is a window
([qtbase 6.8 `qapplication.cpp`](https://github.com/qt/qtbase/blob/6.8/src/widgets/kernel/qapplication.cpp)).
`SearchBar` is the editor's parent and is not a window, so an ignored key
reaches `SearchBar::keyPressEvent`; that handler then sends the event back to
the editor, and the editor's ignore restarts the same walk. Nothing breaks the
cycle: the event is never accepted between the two widgets, and the resend is
synchronous. The result is stack exhaustion (SIGSEGV), not a caught exception.

Why the tests miss it: `tst_search_bar.cpp:177` types printable characters into
`bar.editor()`, and printable keys are accepted by `QLineEdit`, so the parent
walk never happens. Pressing a key the editor ignores (`F5`, or `Down` after a
query that matches no suggestion, where `moveHighlight()` returns `false` at
`search_bar.cpp:697-699`) is enough.

The `sendEvent` is also redundant: because the editor is the focus proxy,
Qt already routes every key to it while the bar has focus. The forwarding only
runs when the bar itself is the receiver — which, in practice, is exactly the
parent-walk case above.

Patch sketch — remove the forward (preferred):

```cpp
void SearchBar::keyPressEvent(QKeyEvent* event)
{
    // editor_ is the focus proxy and already receives every key. Re-sending
    // the event here re-enters this handler: QApplication::notify() retries an
    // unaccepted key on the parent widget, which is this bar.
    View::keyPressEvent(event);
}
```

If a host genuinely delivers keys to the bar (e.g. `QTest::keyClick(&bar, …)`),
forward a *copy* exactly once behind a guard:

```cpp
// search_bar.h
bool forwardingKey_ = false;

// search_bar.cpp
if (editor_ != nullptr && !forwardingKey_) {
    QScopedValueRollback<bool> guard(forwardingKey_, true);
    QKeyEvent copy(event->type(), event->key(), event->modifiers(), event->text(),
                   event->isAutoRepeat(), event->count());
    QCoreApplication::sendEvent(editor_, &copy);
    if (copy.isAccepted()) {
        event->accept();
        return;
    }
}
View::keyPressEvent(event);
```

Rules: the handler violates the basic control-flow rule of "never re-post the
event you are handling"; the crash is UB (stack exhaustion) reached from an
ordinary public interaction, which is why it is P0. `ERR50-CPP` does not apply
(no `std::terminate` path); the relevant C++ Core Guidelines concern is
**E.1** (the interface must not have a hidden fatal mode).

---

### S-02 — the dispatcher swallows every handler exception without a diagnostic

**file:line** `event/src/event_dispatcher.cpp:83-99`; the same shape in
`event/src/gesture_detector.cpp:63-67`.

```cpp
void deliver(EventTarget& node, Event& event) noexcept
{
    try {
        node.onEvent(event);
    } catch (...) {
        // Intentionally swallowed; see event_dispatcher.h.
    }
}
```

Containment is the right boundary policy (**F.6**): an event handler must not
unwind into Qt's event loop. The defect is that the failure leaves *no trace*.
A throwing `onEvent()` is treated exactly like one that returned `false`: the
remaining capture/target/bubble passes still run, the event is delivered to
other targets, and the application has no way to learn that a handler was
skipped. The same applies to `onInterceptEvent()` at `:92-99` and to
`GestureDetector::handle()` at `:63-67`, where a throwing tap/drag callback
silently cancels the gesture (`reset()` at `:65`).

Reachable call path: any user subclass of `EventTarget` that throws from
`onEvent()` (an allocation, a `std::function` target, a bug), or any
`GestureDetector` callback that throws, dispatched through
`EventDispatcher::dispatch()`.

Patch sketch — keep the catch, add the diagnostic:

```cpp
void deliver(EventTarget& node, Event& event) noexcept
{
    try {
        node.onEvent(event);
    } catch (const std::exception& error) {
        qWarning("EventDispatcher: %s::onEvent threw: %s",
                 typeid(node).name(), error.what());
    } catch (...) {
        qWarning("EventDispatcher: %s::onEvent threw an unknown exception",
                 typeid(node).name());
    }
}
```

A rate limit (the `guardReport_` idiom used by `View::paintEvent`) keeps a
handler that throws every frame from flooding the log. `EventTarget` has no
name; `typeid(node).name()` or a small virtual `debugName()` both work.

Rules: **F.6** (a boundary that catches must make the failure observable),
**E.1**/**E.3** (a component that invokes user code owns the failure policy).
Not `ERR50-CPP`: nothing terminates.

---

### S-03 — `mapPointFrom()` can throw across the dispatcher boundary

**file:line** `event/src/event_dispatcher.cpp:257, 274, 294, 308`; helper at
`:132-146`.

Only `onEvent()` and `onInterceptEvent()` are wrapped in `try`/`catch`
(`:83-99`). The per-node position translation runs the other virtual hook of
the same interface outside any guard:

```cpp
translator.prepare(node, origin);   // -> node.mapPointFrom(origin, m_origin)
deliver(node, event);
```

`EventTarget::mapPointFrom()` (`event_target.h:77`) is an ordinary virtual with
no `noexcept`; an implementation that builds a `QTransform`/`QPointF`
translation or queries a parent can throw `std::bad_alloc`. That exception
leaves `EventDispatcher::dispatch()`, contradicting the class comment at
`event_dispatcher.h:53-57` ("Every onEvent()/onInterceptEvent() invocation is
wrapped in a catch-all … A throwing handler therefore cannot break the chain").
The event's position is restored by `PositionTranslator`'s destructor, and the
`DispatchScope` clears/restores the target, so no dangling state remains — but
the caller of `dispatch()` gets an exception from a `noexcept`-looking API, and
the remaining chain is abandoned at an arbitrary point.

The chain allocation at `:229-244` (`std::vector::push_back`) and the virtual
`resetForDispatch()` at `:224` are likewise unguarded, though a `bad_alloc`
there is less likely.

Patch sketch:

```cpp
[[nodiscard]] bool prepare(EventTarget& node, const EventTarget& origin) noexcept
{
    try {
        translator.prepare(node, origin);
        return true;
    } catch (...) {
        qWarning("EventDispatcher: mapPointFrom threw; dispatch aborted");
        return false;
    }
}
// capture/intercept/target/bubble:
if (!prepare(node, origin))
    return finished(event);
```

and wrap the top of `dispatch()` (chain construction + `resetForDispatch`) in
the same catch-all that returns `false`.

Rules: **F.6** (catch at the boundary when the caller cannot handle), plus the
header's own contract. P2 because it needs an allocation failure, but it is the
only virtual on the interface that is not contained.

---

### S-04 — `Schedule::paintContent()` leaves the painter stack unbalanced on its exception path

**file:line** `components/src/schedule.cpp:703-927`; outer `save()` at `708`,
nested saves at `806` and `816`/`851`, `catch` at `921-925`, single `restore()`
at `926`.

`Schedule` guards its paint body locally instead of using an RAII guard:

```cpp
painter.save();                       // :708
try {
    ...
    painter.save();                   // :806 / :816 / :851
    ...
    painter.restore();                // :811 / :868 / :870
} catch (const std::exception& error) {
    qWarning(...);                    // :921-925
}
painter.restore();                    // :926
```

If the throw happens after the `save()` at `:816` (event rendering: `drawText`,
`drawRoundedRect`, `elidedText`), the stack holds two or three saved states and
the single `restore()` at `:926` pops one. `paintContent()` then returns
normally, so `View::paintEvent()` continues with stages 4–7 (`paintChildren`,
state overlay, ripple, focus ring) on the *same* painter, with the schedule's
clip (`:718`, `:817`) and opacity (`:717`) still active. `QPainter`'s
destructor then reports "Painter ended with N saved states". The result is a
mis-clipped frame after a paint failure, not a crash.

`Scheduler` already demonstrates the correct pattern
(`scheduler.cpp:167-186`, constructed inside the `try` at `:460`), as do the
three chart sources and the drawables.

Patch sketch:

```cpp
void Schedule::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;
    // Same local RAII class as scheduler.cpp:167-186 (copy it into this file).
    const PainterStateGuard painterGuard(painter);   // replaces :708 save
    try {
        ...
        {
            const PainterStateGuard state(painter);  // replaces :806 / :811
            ...
        }
        {
            const PainterStateGuard events(painter); // replaces :816 / :870
            ...
            {
                const PainterStateGuard label(painter);
                ...
            }
        }
    } catch (...) { qWarning(...); }
    // no manual restore
}
```

Rules: **E.3** (RAII for every resource, including painter state), **R.1**
(no leaked state), and the local contract stated in `scheduler.cpp:165-166`
("A paint helper that throws after save() must not leave the shared painter
unbalanced").

---

### S-05 — `SpectrogramChart` keeps hover values from the replaced matrix

**file:line** `components/src/spectrogram_chart.cpp:158-181` (setters),
`714-735` (`setHoveredCell`), `974` (paint gate).

```cpp
void SpectrogramChart::setFrames(std::vector<std::vector<double>> frames)
{
    frames_ = std::move(frames);
    magnitudeScale_ = false;
    syncPresenceSeries();
}                                        // hoveredBin_/Frame_/Time_/… unchanged

void SpectrogramChart::setMagnitude(const std::vector<std::vector<double>>& matrix,
                                    double sampleRate, double hopSize)
{
    ...
    frames_ = matrix;
    magnitudeScale_ = true;
    syncPresenceSeries();
}
```

`hoveredLevelDb_` is computed once in `setHoveredCell()` (`:727`) and is never
recomputed from the stored indices. After a data replacement, `paintScene()`
still passes `hasHover() && hoveredFrame_ < columns && hoveredBin_ < rows`
(`:974`) and paints a crosshair plus the *old* readout over the *new* matrix.
`ScatterChart::setSeries()` explicitly clears its hover for exactly this reason
(`scatter_chart.cpp:97-102`); `SpectrogramChart` does not.

Reachable call path: hover a cell, then call `setFrames()`/`setMagnitude()`
from a data-refresh slot — the widget keeps showing the previous cell's time,
frequency and level (and possibly a crosshair on a cell that now holds
different data) until the next mouse move or leave.

Patch sketch:

```cpp
void SpectrogramChart::setFrames(std::vector<std::vector<double>> frames)
{
    frames_ = std::move(frames);
    magnitudeScale_ = false;
    setHoveredCell(-1, -1);   // clears values and calls requestUpdate()
    syncPresenceSeries();
}
```

and the same in `setMagnitude()`. P2: stale presentation, self-corrects on the
next pointer event; no out-of-bounds access (the paint gate bounds-checks the
indices, and `levelDb()` bounds-checks again).

---

### S-06 — `Scrollbar` overflows 32-bit range arithmetic for legal extreme values

**file:line** `components/src/scrollbar.cpp:181` (`thumbLength`), `198`
(`thumbOffset`), `252` (`valueFromTrackPosition`), `121`
(`effectiveSingleStep`), `265` (`pageBy`), `433` (wheel step).

```cpp
const int span = maximum_ - minimum_;                    // :181, :198, :252
return std::max(1, (maximum_ - minimum_) / 10);          // :121
setValue(value_ + direction * step);                     // :265
setValue(value_ - step);                                 // :433
```

`setRange()` accepts any two `int`s and only orders them (`:59-75`), so
`setRange(INT_MIN, INT_MAX)` is legal and makes `maximum_ - minimum_`
`4294967295`, which does not fit `int`: signed overflow is UB (CERT
**INT32-C**). It is reached by the first `thumbRect()`/paint/key press, i.e.
immediately. Independently, `setPageStep(INT_MAX)` is accepted (`:88-96`), and
`pageBy()` computes `value_ + direction * step`; with `value_` at the end of the
range the addition overflows before `setValue()` can clamp it. `wheelEvent()`
(`:433`) multiplies the step by the notch count and has the same shape.

These are hardening findings, not realistic failures: the values are legal but
extreme, and the result is UBSan-detectable UB rather than a crash on a normal
range.

Patch sketch:

```cpp
const qint64 span = qint64(maximum_) - qint64(minimum_);            // 64-bit
const qreal fraction = travel > 0.0 && span > 0
    ? qreal(qint64(value_) - qint64(minimum_)) / qreal(span) : 0.0;

void Scrollbar::pageBy(int direction)
{
    const qint64 step = std::max<qint64>(1, pageStep_ > 0 ? pageStep_ : effectiveSingleStep());
    const qint64 next = std::clamp<qint64>(qint64(value_) + direction * step,
                                           minimum_, maximum_);
    setValue(static_cast<int>(next));
}
```

and clamp `pageStep_`/`singleStep_` to the span (as `qint64`) in their setters.

---

### S-07 — `SeekBar` accepts non-finite values and feeds them to `qRound()`

**file:line** `components/src/seekbar.cpp:78-98` (`setRange`), `110-113`
(`setValue`), `120-128` (`setStep`), `149-157` (`snap`, `qRound` at `155`),
`168-169` (`valueFromX`), `311` (`keyPressEvent`).

```cpp
void SeekBar::setValue(qreal value) { applyValue(value); }          // no validation
qreal SeekBar::snap(qreal value) const
{
    const qreal clamped = std::clamp(value, minimum_, maximum_);
    if (step_ <= 0.0) return clamped;
    const qreal stepped = minimum_
        + static_cast<qreal>(qRound((clamped - minimum_) / step_)) * step_;  // :155
    return std::clamp(stepped, minimum_, maximum_);
}
```

Three concrete paths:

1. `setValue(std::numeric_limits<qreal>::quiet_NaN())`: `std::clamp` returns
   the NaN (both comparisons are false), `step_ == 0` returns it unchanged,
   `applyValue()` stores it and emits `valueChanged(NaN)`; the next paint
   derives `thumbX` from it (`:208-210`) and passes NaN to `drawEllipse`.
2. `setRange(NaN, 100)` stores a NaN `minimum_`; with a positive step,
   `snap()` computes `NaN` and calls `qRound(NaN)` — undefined behaviour.
3. `setStep(1e-300)` (legal: only `step > 0.0` is required) makes
   `(clamped - minimum_) / step_` ~`1e302`; `qRound()` of an out-of-range
   double is undefined (CERT **INT32-C** / **FLP34-C** territory).

Compare `Schedule::setScrollOffset()` (`schedule.cpp:669-672`) and
`IconDrawable::setSize()` (`icon_drawable.cpp:69-70`), which reject non-finite
input outright — this class does not.

Patch sketch:

```cpp
void SeekBar::setValue(qreal value)
{
    if (!std::isfinite(value)) {
        qWarning("md::SeekBar::setValue rejected a non-finite value");
        return;
    }
    applyValue(value);
}

void SeekBar::setRange(qreal minimum, qreal maximum)
{
    if (!std::isfinite(minimum) || !std::isfinite(maximum))
        return;
    ...
}

void SeekBar::setStep(qreal step)
{
    const qreal next = (std::isfinite(step) && step > 0.0) ? step : 0.0;
    ...
}

// snap(): bound before the conversion
const qreal steps = (clamped - minimum_) / step_;
const qreal bounded = std::clamp(steps, qreal(-1.0e9), qreal(1.0e9));
const qreal stepped = minimum_ + qRound(bounded) * step_;
```

P2: needs an invalid setter value; no practical crash, but the emitted NaN and
the float-to-int UB are real.

---

### S-08 — drawable cache keys hash unvalidated `qreal` with `qRound64()`

**file:line** `drawable/src/drawable_hash_p.h:42-45` (`hashShape`),
`:52-55` (`hashScalar`); unvalidated inputs in
`drawable/src/shape_drawable.cpp:47-53` (`setStroke`) and
`drawable/src/shadow_drawable.cpp:231-236` (`setShape`).

```cpp
[[nodiscard]] inline quint64 hashScalar(qreal value) noexcept
{
    return static_cast<quint64>(qRound64(value * 1000.0));   // :54
}
```

`ShapeDrawable::setStroke(QColor colour, qreal widthDp)` stores `widthDp`
without any validation, and `ShapeDrawable::cacheKey()` hashes it at
`shape_drawable.cpp:121`. `Shape`/`CornerRadii` are plain value types
(`tokens/include/md/tokens/shape.h:25-78`) with no sanitising constructor, so
`Shape::rounded(CornerRadii::uniform(NaN))` is constructible and reaches
`hashShape()` through `ShadowDrawable::setShape()`/`cacheKey()` and
`ShapeDrawable::cacheKey()`.

`qRound64()` is `d >= 0.0 ? qint64(d + 0.5) : qint64(d - 0.5)`. For `NaN` the
comparison is false and the cast is undefined; for `±inf` it is out of range
and also undefined. `cacheKey()` is called by every cache lookup and by
`LayerDrawable::cacheKey()`, so the UB is on the ordinary paint path after one
bad setter call. Note that the rest of the drawable module is unusually careful
here (`IconDrawable::setSize`, `ShadowDrawable::setBlurScale`,
`Drawable::effectiveDevicePixelRatio` all reject non-finite geometry); the hash
helpers and these two setters are the gap.

Patch sketch:

```cpp
void ShapeDrawable::setStroke(QColor colour, qreal widthDp)
{
    if (!qIsFinite(widthDp)) {
        qWarning("md::ShapeDrawable::setStroke rejected a non-finite width");
        widthDp = 0.0;                     // or return, keeping the old stroke
    }
    ...
}

[[nodiscard]] inline quint64 hashScalar(qreal value) noexcept
{
    if (!qIsFinite(value))
        return 0;
    return static_cast<quint64>(qRound64(value * 1000.0));
}
```

and in `hashShape()`, substitute `0.0` for each non-finite radius before
`qRound64()`. P2: needs a non-finite setter argument; UBSan-only in practice.

---

### S-09 — the drawable layer discards a child's exception without a diagnostic

**file:line** `drawable/src/layer_drawable.cpp:46-49`; identical policy in
`icon_drawable.cpp:235-239`, `shadow_drawable.cpp:342-346`,
`state_layer_drawable.cpp:68-70`.

```cpp
try {
    child->draw(painter, bounds);
} catch (...) {
}
```

The isolation policy is right — `Drawable::draw()` is documented never to let an
exception escape (`drawable.h:43-44`), and one broken primitive must not take
down the composite. But an empty `catch` means a component's background, icon
or shadow can vanish with no clue why. `LayerDrawable` is the worst case: it
silently skips the child and keeps painting the siblings, so the failure is
only visible as missing pixels.

Patch sketch:

```cpp
std::size_t index = 0;
for (const auto& child : children_) {
    try {
        child->draw(painter, bounds);
    } catch (const std::exception& error) {
        qWarning("md::drawable::LayerDrawable: child %zu failed: %s",
                 index, error.what());
    } catch (...) {
        qWarning("md::drawable::LayerDrawable: child %zu failed: unknown exception",
                 index);
    }
    ++index;
}
```

with a per-instance "warn once" flag (the `View::paintEvent` guard-report
pattern) so a drawable that fails every frame does not flood the log.

Rules: **F.6**/**E.1** again — containment without observability hides a defect
that the audit criteria explicitly ask about ("swallowed catches with no
diagnostic").

---

## Checked and found sound

These were traced explicitly and are *not* findings.

1. **Dispatcher lifetime protocol.** `EventTarget` carries a shared
   `TargetLifetime` token (`event_target.h:35-99`); the chain holds a copy
   (`event_dispatcher.cpp:51-60`) and checks `alive()` before every delivery.
   A handler that destroys the delivery target, an ancestor, or the original
   target is detected by `chainIntact()` (`:65-78`), which also detaches
   `Event::target()` from the dead node; the remaining passes abort. There is
   no path that calls `onEvent()`/`onInterceptEvent()`/`mapPointFrom()` on a
   node whose token says dead. A cycle is caught by the revisit test
   (`:235-242`) and the chain is capped at 1024 nodes (`:30`, `:232-234`).
2. **Dispatcher re-entrancy.** `DispatchScope` saves phase/target/flags when
   the same `Event` is re-dispatched and restores them on scope exit
   (`:154-195`), `PositionTranslator` restores the caller's position on every
   exit path (`:106-152`), and nesting is capped at 16 (`:35`, `:217-219`).
   Re-dispatching the event from a handler does not corrupt the outer pass.
3. **No target/thread escape.** `dispatch()` refuses a cross-thread call while
   the `EventThread` marker is installed (`:199-213`). It is GUI-thread-only
   by contract, and the guard is the documented one.
4. **`GestureDetector` state machine.** No stuck press: `PointerLeave` and
   `FocusOut` cancel (`gesture_detector.cpp:52-59`), a stalled stream is
   detected by `isStale()` (`:242-251`), a second press is either consumed or
   replaces a stale gesture (`:77-88`), and every callback-throw path resets
   the detector (`:63-67`). The detector owns no timer, so it cannot fire
   after its receiver is gone.
5. **`Scheduler` timer and queue lifetime.** A zero or negative
   `setRefreshInterval()` is clamped to zero and *stops* the timer
   (`scheduler.cpp:240-254`); `onShown()` starts it only for a positive interval
   (`:418-429`); `onHidden()` stops it (`:431-442`). The `QTimer` is a member
   connected with `this` as context, so it cannot fire after the widget is
   destroyed. `TaskQueue` lifetime is handled twice: `QPointer` for every read
   (`:139`) and a `destroyed()` connection that unbinds and emits
   `queueChanged(nullptr)` (`:206-233`, `:358-386`). `drainQueue()` is bounded
   to 64 rounds and stops on zero progress (`:312-334`), so a self-reposting
   task cannot pin the GUI thread.
6. **`Schedule` date/time and index math.** `setDate()` ignores invalid dates
   (`schedule.cpp:125-134`), `setDayRange()` handles invalid `QTime` and the
   24:00 convention (`:150-178`), `layoutEvents()` skips invalid or inverted
   `QDateTime` intervals (`:441-471`), and every `events_.at()` is preceded by
   an explicit size/visibility test (`:579`, `:610`, `:624`, `:826`,
   `:1014`, `:1079`). The lane/cluster algorithm was walked with overlapping,
   nested and adjacent intervals: lane ends are reused only when
   `end <= start`, a new cluster starts only when no lane is active, and the
   final `laneCount` pass maps `list` onto `placed` through the captured
   `base` index correctly (`:486-540`).
7. **`Schedule` scrolling.** `contentHeight()` derives from the day range and
   `maximumScrollOffset()` clamps to `>= 0` (`:654-667`), so a viewport taller
   than the content yields no scroll rather than a negative range;
   `setScrollOffset()` rejects non-finite input (`:669-680`) and
   `resizeEvent()` re-clamps (`:1127-1137`).
8. **`Scrollbar` geometry with content smaller than the viewport.** With
   `maximum_ == minimum_` the thumb is the whole track (`:181-183`), travel is
   zero, and both `thumbOffset()` (`:199-200`) and `valueFromTrackPosition()`
   (`:253-254`) return before dividing. `setValue()` clamps at every entry
   point; the only defect is the 32-bit extreme-value overflow in S-06.
9. **`SearchBar` event-filter lifetime.** The filter is installed on the child
   `editor_` in the constructor (`search_bar.cpp:233`) and never explicitly
   removed. This is safe in this class: `editor_` is a child of the filter
   object and `QObject::~QObject` destroys children *after* disconnecting the
   dying object as a receiver, and destruction delivers no events through the
   filter. The window-parented popup, by contrast, is explicitly closed and
   deleted in `~SearchBar()` (`:245-251`), so the `QPointer`s cannot dangle.
   The `QEvent::Type` check precedes the `static_cast<QKeyEvent*>` in the
   filter (`:463`, `:472`), so the cast is type-correct.
10. **"Signals emitted while an object is being destroyed".**
    `SubMenu::~SubMenu()` stops the timer and disconnects every child before
    `QObject` destroys the children (`sub_menu.cpp:68-79`), exactly the hazard
    that `Qt` would otherwise create with the `destroyed()` lambdas installed
    in `insertItem()` (`:122-125`). `Scheduler` may emit `queueChanged(nullptr)`
    from the queue's `destroyed()` handler, but the *scheduler* is alive and
    the queue pointer has already been cleared — legal. `StateLayout`'s
    `destroyed` lambdas (`state.cpp:184-189`) are disconnected in
    `QObject::~QObject` before children are deleted, so they do not run against
    the half-destroyed layout. No component emits one of its own signals from
    its own destructor.
11. **Timers cannot fire after their receiver dies.** `Skeleton`'s and
    `SubMenu`'s `QTimer`s are value members connected with `this` as context,
    and the `Switch`/`SubMenu` animators stop on hide and on destruction.
    `Skeleton` starts its timer only while `shimmerAllowed()` holds
    (`skeleton.cpp:292-314`), i.e. visible, enabled and motion-permitted.
12. **Chart boundary math.** `ScatterChart::paintPlot()` validates the hover
    indices against the *current* vectors before indexing (`scatter_chart.cpp:183-187`);
    `SpectrumChart` guards every bin index (`frequencyAt`, `magnitudeAt`,
    `hasCursor`) and rejects non-finite magnitudes from all queries
    (`spectrum_chart.cpp:245-278`); `SpectrogramChart::cellAt()`/`cellRect()`/
    `levelDb()` bounds-check rows and frames, including ragged rows
    (`spectrogram_chart.cpp:533-600`, `665-685`). Empty-series paths return an
    empty tick list or rectangle rather than indexing.
13. **Chart divisions.** `frequencyRange()`/`valueRange()` always return a
    non-empty interval (`spectrum_chart.cpp:671-726`), and every mapping helper
    checks `range.maximum > range.minimum` before dividing
    (`:786-835`); `SpectrogramChart` checks `span > 0`, `rows > 1`,
    `columns > 0` and the log-axis denominator before dividing
    (`spectrogram_chart.cpp:544-554`, `787-833`).
14. **Drawable painter discipline (normal path).** `ShapeDrawable`,
    `IconDrawable`, `ShadowDrawable`, `StateLayerDrawable` all use
    `save()`/`try`/`restore()` with the restore after the catch, and
    `LayerDrawable` relies on each child doing the same. The exception path is
    the subject of S-09 only; the normal path is balanced.
15. **Drawable geometry validation.** `IconDrawable::setSize()` rejects
    non-finite extents (`icon_drawable.cpp:69-70`); `draw()` validates the
    device-pixel product, caps it at 8192 and checks `qCeil()` inputs
    (`:207-215`); `ShadowDrawable::setBlurScale()` clamps non-finite input
    (`:247-259`), `bucketize()` guards the cap addition (`:78-88`), and
    `draw()` refuses a mask larger than 8192 device pixels before allocating
    (`:287-307`). `QPixmapCache`/`QImage` allocation failures are propagated as
    "paint nothing", not dereferenced (`:163-166`, `:180-183`, `:198-201`).
16. **Ownership and virtual destructors.** `EventTarget` has a virtual
    destructor and deleted copy operations (`event_target.h:61-67`) —
    **C.35**/**C.63**. `Drawable` has a virtual destructor (`drawable.h:36`).
    `LayerDrawable` owns its children through `std::unique_ptr`
    (`layer_drawable.h:56`) — **I.11**/**R.1**. `SearchBar`'s two raw `new`s
    are Qt-parent-ownership (`search_bar.cpp:230`, `:634`) — **ES.20/ES.21**
    acceptable for QObjects. No owning raw pointer was found in the scope.
17. **Zero-size content.** Every component returns before painting on an empty
    content rectangle (`schedule.cpp:705`, `scheduler.cpp:82-83`,
    `scrollbar.cpp:284-288`, `search_bar.cpp:371-372`, `seekbar.cpp:192-193`,
    `skeleton.cpp:213-214`, `switch.cpp:135-136`, `spectrum_chart.cpp:731-732`,
    `spectrogram_chart.cpp:462-463`, `state.cpp:234-235`), and layout
    divisions by a column/row count are guarded by `columns > 0` or
    `columnCount() >= 1`.
18. **No re-posting handlers or deleting filters elsewhere.** The only
    `installEventFilter` in the scope is `SearchBar`'s (`search_bar.cpp:233`);
    it never deletes the filtered object. The only `QCoreApplication::sendEvent`
    is the S-01 site. `Qt::Key_Down`/`Up` handling consumes the event when it
    acts (`:474-485`), so there is no "coalescing drops a key-up" path in this
    scope; key state is not tracked anywhere in these files.

## Notes on method

- All 12 sources and 12 headers were read end-to-end, as were all 15 files
  under `event/` and all 13 files under `drawable/` (including the private
  `drawable_hash_p.h`).
- `components/src/select_tree.cpp` does not exist in this checkout; the
  `s*.cpp` set is the 12 files listed at the top. `tree_select`/`virtual_tree_select`
  are outside this audit's scope.
- The Qt 6.8 `qapplication.cpp` claim in S-01 was verified against the upstream
  source; all other Qt-level reasoning (destruction order, event-filter
  lifetime) is from the Qt 6.8 `qobject.cpp` source.

**Counts: P0 = 1 · P1 = 1 · P2 = 7.**
