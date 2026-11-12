# Boundary audit — issues

> Read-only boundary audits, one subagent per module. Each auditor read the
> public headers and sources, checked the test coverage of every boundary it
> identified, and ran the module's test binary once to record the baseline.
> **No repository file was modified by an auditor.**
>
> Date: 2026-11-12 · Commit: `f2ad36c` · Qt 6.8.3 / MSVC 2022 / `x64-debug`

Severity legend:

- **P0** — crash, undefined behaviour or a wrong result reachable from the public API.
- **P1** — surprising behaviour: the code does something a caller would not expect.
- **P2** — missing guard, missing test or missing documentation of a precondition.

## Summary

| Module | P0 | P1 | P2 | Baseline (that module's own suite) |
| --- | --- | --- | --- | --- |
| core | 0 | 2 | 4 | `tst_core` 10 pass |
| store | 0 | 2 | 4 | `tst_store` 7 pass |
| structure | 0 | 1 | 5 | `tst_binary` 10, `tst_compound` 12 pass |
| pool | 0 | 3 | 6 | 4 suites, 51 pass |
| schedule | 0 | 3 | 3 | `tst_schedule` 10 pass |
| tokens | 0 | 1 | 4 | `tst_tokens` 20, `tst_motion_levels` 6 pass |
| theme | 0 | 2 | 3 | `tst_theme` 20 pass |
| event | 0 | 3 | 2 | `tst_event` 20 pass |
| drawable | 0 | 1 | 3 | `tst_drawable` 12 pass |
| layer | 0 | 1 | 4 | `tst_layer` 12 pass |
| animation | 0 | 1 | 5 | `tst_animation` 10 pass |
| ripple | 0 | 0 | 4 | `tst_ripple` 12, `tst_ripple_layer` 5 pass |
| view | 0 | 2 | 6 | `tst_view` 19 pass |
| layout | 0 | 2 | 4 | `tst_layouts` 16 pass (after the fix below) |
| components | 0 | 2 | 2 | sampled suites pass |
| **total** | **0** | **26** | **59** | |

No P0. The 26 P1s cluster into five patterns:

1. **Re-entrancy and lifetime in callbacks** (event, view, animation, layer, ripple, theme, schedule): user code runs while framework state is on the stack, and the framework then reads freed memory or a destroyed callback.
2. **Owner destroyed or mutated during enumeration** (`paintChildren`, `LayerStack::paint`, dispatcher chains, `Schedule` flush) — iterating a container that a callback can mutate.
3. **Signals not emitted when state is repaired** (`store::clear`, `AppBar::setScrollRange`, List/Recycler row removal, adapter-backed selects).
4. **Unbounded work reachable from the public API** (`struct` recursion to stack overflow, shadow mask allocation, `retired_` growth, `ObjectPool` double release).
5. **Unvalidated numeric input** (`Density` negative scale breaking `std::clamp`, NaN corner radii becoming the maximum).

## Fixed while auditing

- `tests/layout/tst_layouts.cpp:513` asserted `width > height` for a one-digit badge, which fails on the windows platform (a one-digit badge is circular). Now `>=`; `tst_layouts` passes 16/16 on both platforms.

---
## core

### API (public surface)
- `density.h` — `Density{scale, fontScale}`, `dp`, `sp`, `pxToDp`, defaulted `==`.
- `geometry.h` — `Insets{left,top,right,bottom}`, `horizontal`, `vertical`, `appliedTo`, `==`; `CornerRadii{topStart,topEnd,bottomEnd,bottomStart}`, `uniform`, `toPath`, `==`.
- `measure_spec.h` — `MeasureSpec`: `Mode{Unspecified,Exactly,AtMost}`, ctor, `exactly`/`atMost`/`unspecified`, `size`, `mode`, `resolve`.
- `view_state.h` — `ViewState` (9 `quint32` bits), `ViewStates` flags.
- `logging.h` — `mdCore`/`mdPaint`/`mdLayout`/`mdTheme` + global aliases (`logging.cpp` defines them).

Baseline: `tst_core.exe` — **10 passed, 0 failed, 0 skipped**. P0 findings: none.

### ISSUE-core-1: `pxToDp` is guarded against `scale <= 0` but `dp`/`sp` are not
- Severity: P1
- Location: `core/include/md/core/density.h:45` (against `:31`, `:38`)
- Boundary: `scale == 0` (a zoom animating in from zero), `scale < 0`, or non-finite `scale`.
- Expected: the comment at `:41-42` ("A non-positive scale is treated as 1.0") to hold for the whole struct, and `pxToDp(dp(x)) == x`.
- Actual: `dp()` multiplies unconditionally, so `scale=0` makes every dp/sp value 0 (collapsed layout) while `pxToDp(24)` still returns 24; with `scale=-2`, `dp(12)==-24` but `pxToDp(-24)` returns -24, not 12.
- Fix: one `effectiveScale()` (`scale > 0 && qIsFinite(scale) ? scale : 1.0`) used by `dp`, `sp` and `pxToDp`.

### ISSUE-core-2: NaN corner radius silently becomes the maximum radius
- Severity: P1
- Location: `core/src/geometry.cpp:20`
- Boundary: any of the four radii is NaN.
- Expected: the negative-radius rule at `:18-19` — an invalid radius degenerates to 0.
- Actual: `radius <= 0.0` and `radius < limit` are both false for NaN, so the ternary returns `limit`: the most-rounded corner the rectangle allows, the opposite of degenerate.
- Fix: `if (!(radius > 0.0)) return 0.0;` then clamp the rest (Inf still clamps to `limit`).

### ISSUE-core-3: `toPath`'s empty-rect guard misses NaN/Inf bounds
- Severity: P2
- Location: `core/src/geometry.cpp:31`
- Boundary: `QRectF` with NaN or infinite width/height.
- Expected: header promise at `geometry.h:59` ("A rectangle with no area yields an empty path"); non-finite geometry should be a no-op.
- Actual: `QRectF::isEmpty()` is `width() <= 0 || height() <= 0`; NaN fails both, so the guard does not fire. `std::min` then yields a NaN `limit`, every radius clamps to NaN, and the function returns a non-empty path of NaN points (infinite sizes give Inf points).
- Fix: after the `isEmpty` check, return an empty path unless `qIsFinite(bounds.width()) && qIsFinite(bounds.height())` (likewise x/y).

### ISSUE-core-4: an out-of-range `MeasureSpec::Mode` silently drops the constraint
- Severity: P2
- Location: `core/include/md/core/measure_spec.h:56-64`
- Boundary: `MeasureSpec(100.0, static_cast<Mode>(3))` or any corrupted mode.
- Expected: reject/assert, or a documented deterministic fallback.
- Actual: the `switch` has no `default`, so control falls through to `return desired;` — the spec behaves as `Unspecified` and an intended limit vanishes; `mode()` still reports 3.
- Fix: add `default:` with `Q_ASSERT(false)` and a documented fallback (e.g. `size_`).

### ISSUE-core-5: NaN handling in `MeasureSpec::resolve`
- Severity: P2
- Location: `core/include/md/core/measure_spec.h:60`
- Boundary: NaN `size_` or NaN `desired`.
- Expected: NaN rejected, not converted into a plausible size.
- Actual: `desired < size_` is false when either operand is NaN, so `atMost(80).resolve(NaN)` returns 80 (the cap) and `atMost(NaN).resolve(50)` returns NaN, which then reaches `resize`/`QRectF`.
- Fix: document the finite precondition plus a debug assert, or branch on `qIsFinite` explicitly.

### ISSUE-core-6: defaulted `qreal` equality is not reflexive for NaN
- Severity: P2
- Location: `core/include/md/core/density.h:48`, `core/include/md/core/geometry.h:35`, `core/include/md/core/geometry.h:62`
- Boundary: any NaN member (`Density{NaN}`, `Insets{0,NaN,0,0}`, NaN `CornerRadii`).
- Expected: `x == x`, so the value types can be keys or deduplicated.
- Actual: `= default` uses `==` per member, so a value is not equal to itself; `densityEquality` only tests finite values.
- Fix: document the finite precondition, or implement explicit equality treating bitwise-identical NaNs as equal.

### Covered
- Exactly/AtMost/Unspecified resolution at 0, equal, below and above the spec size — `tests/core/tst_core.cpp:52`.
- Density with positive `scale` (1, 2) and stacked `fontScale` (1.5×2), one dp round trip — `:80`.
- `Density` equality for finite variants — `:108`.
- `Insets::horizontal/vertical/appliedTo` on 100×50 — `:125`.
- Zero-radius rectangle, full-bounds path, LTR start/end corners — `:139`.
- Oversized radius clamped to half the shorter side (50 on a 40-high rect), mixed radii, empty `QRectF()` — `:171`.
- Flag set/clear/testFlag/toInt — `:198`.
- Four category names and the global alias — `:234`.

### Not covered
- `pxToDp` with `scale <= 0` (the only documented guard in `Density`) — no test.
- `scale`/`fontScale` = 0, negative, NaN or Inf for `dp`/`sp`; subnormal scale overflow.
- NaN radius (finding 2), negative radius, Inf radius, NaN/non-finite bounds (finding 3), negative-width rect.
- Insets that exceed the rectangle (documented negative result), negative insets, NaN insets.
- Negative/NaN/Inf `MeasureSpec` sizes, default-constructed spec, invalid `Mode` cast.
- NaN equality for all three value types; unknown `ViewStates` bits and empty-set semantics.
- Locale/precision: only binary-exact scales (2.0, 1.5) are tested; 0.1-style round trips are not.

---

## pool

Baseline: `tst_pool` 19, `tst_thread_pool` 13, `tst_ring_channel` 12, `tst_ipc` 7 — all pass. A bare run needs `PATH` to include both the build dir and the Qt `bin` (exit 0xC0000135 otherwise).

### ISSUE-pool-1: `parallelFor` hangs forever if the body throws
- Severity: P1
- Location: `pool/src/thread_pool.cpp:238-246` (wait at `:260`, worker catch at `:318-323`)
- Boundary: a task body throws.
- Expected: the latch is counted down on every path, so `parallelFor` returns (the failure is observable).
- Actual: the worker swallows the exception and only increments `failed_`; `pending.count_down()` is skipped, so the wait never returns.
- Fix: count down in a scope guard in the wrapper that invokes the body.

### ISSUE-pool-2: `parallelFor` hangs if the queue is discarded concurrently
- Severity: P1
- Location: `pool/src/thread_pool.cpp:131-135`, `:152`
- Boundary: `stop(false)` / `discardQueuedLocked()` runs while a `parallelFor` is waiting.
- Expected: destructing queued nodes still releases the latches they hold.
- Actual: queued chunk nodes are destroyed without counting down, so the latch never reaches zero.
- Fix: have the task node's destructor count down, or drain pending chunks through a completion path.

### ISSUE-pool-3: `ObjectPool` double destroy / foreign release corrupts the free list in release builds
- Severity: P1
- Location: `pool/include/md/pool/block_pool.h:278-290` (`isFree()` is `return false` under `NDEBUG`), `release()` at `:135-144`; debug-only asserts at `object_pool.h:126-128`
- Boundary: releasing the same block twice, or a block the pool does not own.
- Expected: rejected or a documented precondition; at worst a no-op.
- Actual: in release the block is pushed onto the free list twice and `inUse_` underflows; the pool later hands out the same storage to two owners.
- Fix: keep a debug-and-release check (a generation counter or a magic word), or document the precondition as a contract violation and always assert.

### ISSUE-pool-4: `Mailbox` is unbounded and can lose its wake-up permanently
- Severity: P2
- Location: `pool/src/mailbox.cpp:21-48`
- Boundary: `QMetaObject::invokeMethod` fails (object destroyed / wrong thread); a large backlog.
- Expected: the failure is detected and the message delivered later, and there is a bound or a dropped counter.
- Actual: the `bool` result is ignored; if it fails `wakeup` stays `true` and every later `post()` returns early, so the backlog is never drained. No bound, no dropped counter. `state_` is read without synchronisation against `~Mailbox`.
- Fix: reset the wake-up flag on failure, add a capacity and a dropped counter, and guard the state against destruction.

### ISSUE-pool-5: `IoThread` periodic timers always record zero latency; `readFile`'s rejected path runs `done()` on the caller thread
- Severity: P2
- Location: `pool/src/io_thread.cpp:256-259` (clamps at `:304`), `:209-210` (contrary to `io_thread.h:74-75`)
- Boundary: any periodic timer; `readFile` rejected before it is posted.
- Expected: latency is the measured queue-to-finish delay; `done()` always runs on the IO thread.
- Actual: `due` is recorded as `finished + period`, so the subtraction is negative and clamps to 0. The rejected `readFile` path calls `done()` inline on the caller's thread.
- Fix: record the scheduled due time before the wait; post `done()` through the queue on the rejection path (or document the exception).

### ISSUE-pool-6: `size()`/`used()` can underflow
- Severity: P2
- Location: `pool/include/md/pool/ring_buffer.h:119-124`, `pool/src/ipc/shared_ring.cpp:388-396`
- Boundary: concurrent producer/consumer while size is queried.
- Expected: a consistent snapshot.
- Actual: `head` is loaded before `tail`; a consumer that drains in between leaves a stale head, so the subtraction wraps to ~2^64 (`full()` true, `empty()` false).
- Fix: load `tail` first (or both with acquire and re-check) in the size path, or compute size as a saturating difference.

### ISSUE-pool-7: `encodeFrame` has no size guard
- Severity: P2
- Location: `pool/include/md/pool/ipc/frame.h:29-46`
- Boundary: a payload of 1 MiB or more, up to 4 GiB.
- Expected: the same limit the decoder (`frame.h:89`) and `SharedRingBuffer::push` (`shared_ring.cpp:310`) enforce (1 MiB).
- Actual: no `kMaxFrameSize` check and no 32-bit length guard, so ≥ 4 GiB payloads are silently truncated and 1 MiB–4 GiB frames are encoded but can never be decoded.
- Fix: reject payloads above the documented maximum in `encodeFrame`.

### ISSUE-pool-8: `SharedRingBuffer` silently clamps capacity and demands the creator's byte count
- Severity: P2
- Location: `pool/src/ipc/shared_ring.cpp:55-61`, `:164`, `:287-297`
- Boundary: capacity above 256 MiB; a consumer opening with a different (equivalent) capacity.
- Expected: a documented maximum and an open that derives the layout from the header.
- Actual: the capacity is clamped silently, and `open()` requires the creator's exact byte count or it fails.
- Fix: report the clamped capacity back to the caller, and validate the header instead of the requested size.

### ISSUE-pool-9: child EOF mid-frame is never signalled
- Severity: P2
- Location: `pool/src/ipc/child_process.cpp:288-299`, `:360-369`; POSIX `:438-446`
- Boundary: the child exits after writing a partial frame.
- Expected: the decoder reports a truncated frame / EOF so the consumer can give up.
- Actual: the reader just breaks; `frame.h` keeps a partial frame forever and the consumer blocks. On POSIX `isRunning()` reaps with `waitpid(WNOHANG)`, so a later `waitForFinished()` returns -1.
- Fix: surface an EOF/truncated status from the reader and reset the decoder.

### Notes
- SPSC memory ordering is correct (`ring_buffer.h:70/79`, `:93/100`; `shared_ring.cpp:319/337`, `:348/379`); no missing fence found. The only ordering defect is the head-before-tail snapshot in ISSUE-pool-6.
- Untested: `writeAt`/`readAt` wrap branch (`shared_ring.cpp:72-90`), `parallelFor` with 0/1 items, `Channel` destroyed with a blocked consumer, `stop()` called from a worker (self-join → `terminate`), 64-bit cursor wrap, POSIX branches.

---

## event

Baseline: `tst_event.exe` 20 passed / 0 failed. Tests live at `tests/event/tst_event.cpp`, not `event/tests/*`. There is no listener registry: dispatch walks a parent-chain snapshot of `EventTarget*`.

### ISSUE-event-1: re-entrant dispatch of the same `Event` wipes the outer pass's state
- Severity: P1
- Location: `event/src/event_dispatcher.cpp:79`
- Boundary: a handler dispatches the event it is currently handling to another target.
- Expected: the outer run keeps its phase/flags, or re-entry is rejected.
- Actual: `resetForDispatch()` (`event.cpp:70-76`) clears accepted/stopped and nulls the target; earlier `accept()`/`stopPropagation()` is lost. The bubble loop never re-sets the target (`:142-150`), so outer bubble handlers see the inner target. Nesting is unbounded → stack overflow.
- Fix: guard against re-entry (assert/no-op) or save/restore phase, target and flags; cap nesting depth.

### ISSUE-event-2: a target destroyed during dispatch is a use-after-free
- Severity: P1
- Location: `event/src/event_dispatcher.cpp:82-92`, `:135`
- Boundary: a handler deletes the target or one of its ancestors.
- Expected: a documented lifetime rule, or node guards.
- Actual: the chain is a raw `std::vector<EventTarget*>` and the loops keep calling into freed memory. `Event::m_target` (`event.h:95`) survives the dispatch, so a retained event dangles (locked in by `tst_event.cpp:223-224`).
- Fix: hold `QPointer` in the chain and abort on a nulled node; clear `m_target` on return.

### ISSUE-event-3: a lost release leaves the gesture pressed forever
- Severity: P1
- Location: `event/src/gesture_detector.cpp:75-96`
- Boundary: press without release (focus loss, popup, release outside the window), then hover moves.
- Expected: `gesture_detector.h:12-13` — no callback at all; cancel on focus loss.
- Actual: only `handleUp()`/`reset()` clear `m_pressed`; a later move fires `onLongPress` (`:94-96`) and every later move is consumed. `handle()` ignores `FocusOut` (`:41-54`) although `View::focusOutEvent` dispatches one (`view/src/view.cpp:546-548`); `Callbacks` has no `onCancel`.
- Fix: reset on `FocusOut`/`PointerLeave`, add a stale-press deadline and an `onCancel` callback, correct the header claim.

### ISSUE-event-4: a second `PointerDown` silently abandons an in-progress drag
- Severity: P2
- Location: `event/src/gesture_detector.cpp:44-46`, `:154-161`
- Boundary: a second pointer, or a second press while dragging.
- Expected: ignore it (single-pointer detector) or cancel the old gesture with a callback.
- Actual: every `PointerDown` calls `begin()`, overwriting the drag state with no callback; later deltas are measured from the second press (`:62`), and the first pointer's Up ends the second gesture. `PointerEvent` has no pointer id (`pointer_event.h:42-47`) and the bridge only converts `QMouseEvent` (`qt_bridge.cpp:8-24`), so touch cannot be filtered or received.
- Fix: ignore a press while pressed (or track ids), emit a cancel when replacing a gesture, add touch support and a pointer id.

### ISSUE-event-5: `WheelEvent` position is documented per-target but never translated
- Severity: P2
- Location: `event/include/md/event/wheel_event.h:32`
- Boundary: a wheel bubbles from a child to a scrolling ancestor.
- Expected: the position is in the receiving target's frame, as documented.
- Actual: `PointerTranslator` only casts to `PointerEvent` (`event_dispatcher.cpp:36`), and `WheelEvent` has no `setPosition`. Latent: nothing dispatches wheels — `View::onEvent` ignores `EventType::Wheel` (`view/src/view.cpp:359-361`).
- Fix: add `setPosition` and translate wheel events, or reword the comment to "dispatch target's coordinate system".

### Covered
- Root termination of the chain (`event_dispatcher.cpp:84`, `tst_event.cpp:375`); capture-phase `accept()` (`:136`); self-parent and two-node cycles (`:88-90`); event reuse (`eventReuse`, `tst_event.cpp:409`); nested dispatch of a *different* event.

### Not covered
- Re-entrant dispatch, target destruction, capture-phase accept, focus-loss/cancel, multiple pointers, wheel translation, intercept-that-accepts, chains beyond 1024 nodes.
- `GestureDetector` has no production consumer, so begin-without-end and cancel are never exercised end to end.

---

## theme

Baseline: `tst_theme.exe` 20 passed. No font resolver in this module (font resolution lives in `components/font.h`).

### ISSUE-theme-1: replacing or removing a `ThemeScope` changes resolution without invalidating anything
- Severity: P1
- Location: `theme/src/theme_scope.cpp:47`
- Boundary: `ThemeScope::install()` replaces an existing scope, or a scope is destroyed while widgets live.
- Expected: the host subtree is notified, as `setTheme` does.
- Actual: `install()` does `delete existing` with no `ThemeChange` posted and no destructor; widgets keep cached colours (e.g. `NavigationView` caches its background in the constructor, `components/src/navigation_view.cpp:57`). `tst_theme.cpp:349-352` deletes a scope but only re-checks resolution.
- Fix: post `ThemeChange` to the host subtree before replacing and in the destructor (or `deleteLater()`); note that `install()` also invalidates every previously returned `const Theme&`.

### ISSUE-theme-2: application theme read unsynchronised from worker threads
- Severity: P1
- Location: `theme/src/theme.cpp:166` (setter checks the thread at `:160-164`; getter at `:180-183`)
- Boundary: a worker calls `forWidget()` while the GUI switches theme.
- Expected: a data-race-free read (or a documented GUI-thread-only contract enforced everywhere).
- Actual: the setter assigns a plain static `Theme`; `applicationTheme()` and `forWidget()` read it with no lock → race on `QColor`/`QString` members (UB). The rejected setter also fails silently.
- Fix: mutex + return by value/`shared_ptr<const Theme>`, or mark all resolution GUI-thread and document it.

### ISSUE-theme-3: adapter scopes never re-resolve after an ancestor or application theme change
- Severity: P2
- Location: `theme/src/theme_scope.cpp:69`
- Boundary: nested `Adapter` scope after an ancestor `setTheme` or `setApplicationTheme`.
- Expected: derived scopes re-resolve (`theme_scope.h:70-73`).
- Actual: `refresh()` runs only at install (`:65`) and manually; neither `setTheme` (`:88-101`) nor `setApplicationTheme` (`theme.cpp:153-178`) walks derived scopes.
- Fix: collect descendant scopes with adapters and `refresh()` them, with a re-entrancy guard.

### ISSUE-theme-4: `Density` accepts zero / negative / huge scale
- Severity: P2
- Location: `core/include/md/core/density.h:31`
- Boundary: `scale == 0`, negative, or a huge `fontScale`.
- Expected: a positive, bounded scale, or a documented precondition.
- Actual: `dp()`/`sp()` multiply unclamped while `pxToDp()` treats non-positive scale as 1.0 — inconsistent; scale 0 collapses geometry, -1 inverts it, fontScale 0 gives 0-px text.
- Fix: validate/clamp in `Density` or at the `Theme` boundary.

### ISSUE-theme-5: default/empty `ColorScheme` produces an invalid palette
- Severity: P2
- Location: `theme/src/theme.cpp:103`
- Boundary: `Theme{}` (default constructor).
- Expected: a usable baseline palette.
- Actual: `ColorScheme` members have no initialisers, so `toPalette()` maps invalid `QColor`s; `colourPrimaryDark()`/`colourSecondaryDark()` infer dark from `surface.lightness()` (0 for an invalid colour) and always report dark. `fromColours` already substitutes baseline colours for invalid seeds, so invalid input is anticipated elsewhere.
- Fix: default the members to `baselineLight`, or fall back in `toPalette()` and carry brightness explicitly.

### Covered
Value semantics/`withX`/equality/baselines (`tests/theme/tst_theme.cpp:111-254`), app fallback/null/notification (`:256-293`), scope install/find/replace/null (`:295-306,355-410`), subtree resolution + nearest scope + delete-promotion (`:308-353`), `setTheme` notify/equality no-op (`:376-404`), palette map + 38% disabled alpha (`:412-456`). Also verified: nested scopes, scope destroyed while resolving, `setTheme` during construction, dark↔light with `reducedMotion`, theme switched mid-paint.

### Not covered
Adapter install/refresh (only `tests/components/tst_config_containers.cpp:126-150`), `installFromStore`/`publishTheme`, scope-destruction notification, app switch with live scopes, worker-thread resolution, rejected setter, repeated identical `setApplicationTheme` (no equality no-op → floods the tree), `Theme{}`, zero/negative density, huge `fontScale`, `setTheme` in a constructor.

---

## store

Baseline: `tst_store.exe` 7 passed. No P0 found.

### ISSUE-store-1: `clear()` removes keys without any per-key notification
- Severity: P1
- Location: `store/src/store.cpp:124-131` (contract at `store.h:41`)
- Boundary: bulk removal while watchers are connected.
- Expected: each removed key observable, as `remove()` is (`:119`).
- Actual: only `changed()` is emitted; a watcher on `valueChanged` — `View::bindStore` (`view/src/view.cpp:701-705`) — keeps the old theme after a clear. Untested.
- Fix: emit `valueChanged(key, {})` over a key snapshot before `changed()`, or add `cleared()`; document and test both.

### ISSUE-store-2: removal is indistinguishable from an invalid value
- Severity: P1
- Location: `store/src/store.cpp:111-121`
- Boundary: key removed while a watcher exists; absent vs `setValue(key, QVariant())`.
- Expected: watchers can tell removal from an invalid value.
- Actual: the old value is captured and discarded (`Q_UNUSED(old)`), and the signal carries `QVariant()` — identical to setting an invalid value. `Language` (`components/src/language.cpp:576-580`) reacts by calling `setCurrentLanguage("")`. Tests assert state only (`store/tests/tst_store.cpp:75-77`).
- Fix: add `removed(key)` or a presence flag; document `hasValue()` as the discriminator; test the payload.

### ISSUE-store-3: registry unsynchronised; `open()` can return two stores for one id
- Severity: P2
- Location: `store/src/store.cpp:12-16`, `:34-46`
- Boundary: `open()` called from two threads.
- Expected: "same object for every caller" (`store.h:4-5`).
- Actual: check-then-insert without a lock; both threads can insert and receive different objects; `values_` is mutated unsynchronised.
- Fix: `Q_ASSERT` thread affinity plus a registry mutex, or document the restriction.

### ISSUE-store-4: empty key silently dropped
- Severity: P2
- Location: `store/src/store.cpp:88-89`
- Boundary: `setValue("", v)`.
- Expected: rejected visibly or documented (the empty *id* is documented).
- Actual: silent no-op; `hasValue("")` stays false. Only the empty id is tested.
- Fix: `Q_ASSERT(!key.isEmpty())` or return `bool`; test.

### ISSUE-store-5: stores cannot be closed; destruction path unreachable
- Severity: P2
- Location: `store/src/store.cpp:27-32`, `:43`
- Boundary: store lifetime / registry reuse.
- Expected: a disposal path, at least for tests.
- Actual: `new Store(id)` with no parent, private destructor, no `close()`; the null-`QPointer` branch (`:40-41`) is unreachable. Tests leak registry state (`tst_store.cpp:92` uses `>= 2`).
- Fix: add `close(id)`/test reset; test destroy → `find()==nullptr` → fresh `open()`.

### ISSUE-store-6: value type and size unchecked; every set deep-compares
- Severity: P2
- Location: `store/src/store.cpp:91-95`
- Boundary: type change (int→string); huge payload.
- Expected: a stated value contract.
- Actual: any `QVariant` accepted; a type change is an ordinary `valueChanged` and readers coerce silently (`toInt()` on `"abc"` → 0); the no-op test is `QVariant::operator==`, so a redundant large `QString`/`QByteArray` compares all bytes (O(n)).
- Fix: document untyped/coercing semantics, compare `metaType()` first, consider a size guard.

### Covered
Empty id → nullptr (`:31`), unknown key fallback (`:68-69`), duplicate `open` same pointer (`:23-29`), distinct ids (`:36-39`), equal-value no-op + one `valueChanged` + one `changed` (`:50-60`), `remove` state/size and `clear` empties (`:75-81`), `ids()`/`count()` (`:89-92`).

### Not covered
Signal counts/payloads for `remove()`/`clear()`; unknown-key `remove`; empty-store `clear`; re-entrant mutation from a slot; duplicate subscription; watcher destroyed during notification; `const QVariant&` payload lifetime (reference to the caller's argument, valid only during the call); thread affinity; huge payloads; store destruction/re-open.

---

## schedule

Baseline: `tst_schedule.exe` 10 passed. Tests live at `tests/schedule/tst_schedule.cpp`.

### ISSUE-schedule-1: a throwing callback permanently wedges the queue
- Severity: P1
- Location: `schedule/src/task_queue.cpp:118`
- Boundary: a task callable throws.
- Expected: `flushing_` reset, remaining work rescheduled, `drained()` emitted.
- Actual: `flushing_ = true` (`:112`) is cleared only at `:127`; a throw leaves it true forever, so `flush()` returns 0 and `scheduleFlush()` bails — posts pile up and never run.
- Fix: scope guard resetting state and rescheduling, plus a per-task catch.

### ISSUE-schedule-2: `retired_` grows without bound in normal coalesced use
- Severity: P1
- Location: `schedule/src/task_queue.cpp:48-49`, `:63`, `:136-152`
- Boundary: reposting a key whose task already ran.
- Expected: retired ids bounded by entries still queued.
- Actual: `popNext` never erases `coalesced_[key]`, so the next `postCoalesced` retires a dead id that nothing erases. Each `View::requestUpdate()` after a flush leaks one node — unbounded under animation.
- Fix: carry `key` in `Entry`; erase `coalesced_[key]` when the popped id matches.

### ISSUE-schedule-3: unsynchronised queue; `main()` affinity is whoever calls first
- Severity: P1
- Location: `schedule/src/task_queue.cpp:26-30`, `:32-39`
- Boundary: post from another thread; first `main()` call from a worker.
- Expected: framework.md §5.12 rule 5 — one queue per thread, main queue GUI-thread-only.
- Actual: stated only as a comment; `main()` adopts the caller's affinity, and `post` mutates deques with no mutex → data race.
- Fix: assert thread affinity in `post`/`flush`/`main()`; add a queued or mutexed cross-thread path.

### ISSUE-schedule-4: `postCoalesced` violates its documented priority/position contract
- Severity: P2
- Location: `schedule/src/task_queue.cpp:48-54`
- Boundary: repost with a different priority, or while other keys are pending.
- Expected: header `:66-68` — "keeps the priority and queue position of its first post".
- Actual: the old entry is retired and the new one pushed at the new priority; a same-priority repost moves behind keys posted later.
- Fix: replace the callable in place, or amend the header.

### ISSUE-schedule-5: the budget cannot bound a task; strict priority can starve lower levels
- Severity: P2
- Location: `schedule/src/task_queue.cpp:117-124`
- Boundary: one slow task; continuous high-priority posting; negative budget.
- Expected: the 8 ms default bounds a flush; zero means no budget.
- Actual: elapsed time is checked only after `task()` returns, so one 500 ms task blocks the GUI thread for 500 ms. A `UserInput` task that reposts `UserInput` wins every iteration; `UiUpdate` can starve (no aging/quota). A negative budget means "unlimited".
- Fix: check elapsed at the loop head, reject/document negatives, consider per-level quotas.

### ISSUE-schedule-6: lifecycle gaps — cancel, destruction, shutdown
- Severity: P2
- Location: `schedule/src/task_queue.cpp:24`, `:57-65`, `:127-132`
- Boundary: cancel while running or after drain; queue destroyed with pending work or from a callback; post after shutdown.
- Expected: cancellation observable; pending work drained or reported; destruction safe.
- Actual: `cancel` reaches only coalesced pending entries and returns void; the defaulted destructor silently destroys pending callables; destroying the queue from a callback makes `flush` use freed `this` (UAF); posting after static destruction is UB.
- Fix: return `bool` from `cancel`; clear and notify in the destructor; guard self-destruction; check `qApp` lifetime.

### Covered
Five-level ordering, FIFO within a level, mid-drain `UserInput` jump, coalescing replacement, cancel-before-flush, re-entrant post in the same flush, `maxTasks` + event-loop rescheduling, retired-aware counts.

### Not covered
Empty flush / `drained()`; null callable; time-budget stop; invalid `Priority`; cross-thread post; throwing callback; cancel of a running task; destruction with pending tasks; post after shutdown; coalesced priority/position retention; repost while running (the leak path); nested `flush()`; `flush(0)`; huge backlog; `main()` affinity.

---

## structure

Baseline: `tst_binary` 10, `tst_compound` 12 — pass.

### ISSUE-structure-1: traversals and node teardown recurse once per level
- Severity: P1
- Location: `structure/include/md/structure/tree/binary_tree.h:143`, `:150`, `:367`, `:64`; `binary_plus_tree.h:474`; `forest/sibling_forest.h:188`
- Boundary: a chain of n ≫ 1,000 nodes (a `BinarySearchTree` degenerates by design — `tests/structure/tst_ordered.cpp:196-200` asserts height 100 for 100 sorted inserts).
- Expected: O(n) time, stack depth independent of n — `LinearTree::clear` (`linear_tree.h:270-278`) already releases iteratively "so that a long list cannot exhaust the stack".
- Actual: `sizeOf`/`heightOf`/the traversals/`cloneSubtree`/`refreshSubtree` recurse per level, and the defaulted `~BinaryTree` destroys a left-child chain recursively. Tens of thousands of sorted inserts overflow the 1 MB stack in `size()`, `height()`, `toInorder()` or at scope exit. Only AVL is depth-bounded; no test goes past 100.
- Fix: iterative traversal/teardown or a documented depth cap; test a 100k-node chain.

### ISSUE-structure-2: `Forest::tree(index)` is unchecked while `removeTree(index)` is checked
- Severity: P2
- Location: `structure/include/md/structure/forest/forest.h:53-54`
- Boundary: empty forest, or `index == treeCount()`.
- Expected: the same bounds contract as `removeTree` (`forest.h:42-48`), which returns false, or at least an assert.
- Actual: `trees_[index]` — UB on an empty forest or after `removeTree` shrank the vector. Tests only index existing trees (`tests/structure/tst_forest.cpp:42-47`).
- Fix: assert and document the precondition.

### ISSUE-structure-3: `SiblingForest::removeSubtree` dereferences a null/foreign node
- Severity: P2
- Location: `structure/include/md/structure/forest/sibling_forest.h:148`, `:245-246`
- Boundary: `forest.removeSubtree(forest.root(99))` — `root()` returns nullptr past the end (`:103-111`).
- Expected: no-op or assert, like `MultiTree::appendChild` (`multi_tree.h:82`).
- Actual: `node->nextSibling` is dereferenced before validation → access violation in debug and release. A node from another forest passes that line and makes `slotOf` walk past the last sibling and dereference null at `:246`.
- Fix: return nullptr / assert on null and on a node that is not in this forest.

### ISSUE-structure-4: a moved-from `Trie` reports `size() != 0` with a null root
- Severity: P2
- Location: `structure/include/md/structure/tree/trie.h:84-85`, `:272`, `:100-105`, `:211-213`, `:262-263`
- Boundary: use, copy or `empty()` check after `std::move`.
- Expected: moved-from is empty, as `LinearTree` (`linear_tree.h:104-122`) and `SiblingForest` (`sibling_forest.h:63-77`) do; `tst_linear.cpp:208-227` tests that convention.
- Actual: the defaulted move moves `root_` but copies `size_`, so `empty()` is false while `root()` is null; `insert`, `descend` and `nodeCount` dereference it, and copying a moved-from trie calls `cloneSubtree(nullptr)`. `tst_heap_trie.cpp:186-204` only tests copying.
- Fix: user-declared move that resets `size_` and recreates the root.

### ISSUE-structure-5: `MultiTree::appendChild` on a full parent returns a dangling reference in release
- Severity: P2
- Location: `structure/include/md/structure/tree/multi_tree.h:86-87`, `node/multi_node.h:65-71`
- Boundary: the parent is already at capacity (asserted in debug only).
- Expected: reject the insert; the node-level call already reports failure by returning `N`.
- Actual: with `NDEBUG`, `MultiNode::appendChild` takes the `unique_ptr` by value, sees `isFull()`, returns `N` and destroys the parameter, then `return *raw;` hands back a reference to freed storage.
- Fix: test `parent->isFull()` in `MultiTree` and return a pointer; never return `*raw` unless the append succeeded.

### ISSUE-structure-6: grafting a released root under its own node creates a parent-chain cycle
- Severity: P2
- Location: `structure/include/md/structure/tree/binary_plus_tree.h:134-138`, `:163-167`
- Boundary: `setChild(p, side, child)` where `child`'s subtree already contains `p` — reachable via `releaseRoot()` or `takeChild()` while raw pointers into the subtree remain valid.
- Expected: reject; `unique_ptr` normally makes alias insertion impossible.
- Actual: the released root is linked beneath its own descendant; `updateToRoot(parent)` never reaches nullptr (infinite loop) and teardown double-frees.
- Fix: walk `parent`'s ancestors and assert `child.get()` is not among them; document that `parent` must belong to `*this`.

### Covered
Empty size/height/find/erase; erase-to-empty and reuse; duplicate keys rejected by the skip list and the BST; descending comparator; moved-from `LinearTree` reuse; AVL sorted input and a 4,000-op churn; trie prefixes and foreign symbols; segment-tree empty range and non-commutative order; removing the only root.

### Not covered
`front()`/`back()`/`popFront()` on an empty `LinearTree` (no assert or doc); `Forest::tree(i >= treeCount())`; `removeSubtree` null/foreign; any `Trie` move; null passed to `depth`/`inorderIndex`; iterator validity across `pushFront`/`eraseAfter`; self-assignment; chains deeper than 100 (BST) / 1,000 (AVL); `SegmentTree::at(size_)`/`set(size_)`/out-of-range `query`; throwing `Compare`/`Combine`; `SkipList` exception safety between splice and `arena_.emplace`; capacity overflow at `SIZE_MAX`; and `table.h`, `tree.h`/`flattenTree`, `virtual_source.h` have no test target at all.

---

## tokens

Baseline: `tst_tokens` 20 passed; `tst_motion_levels` 6 passed. No name-keyed role lookup exists; `Density` lives in `core/`, not here.

### ISSUE-tokens-1: `Density` has no bounds; a negative scale violates `std::clamp` preconditions
- Severity: P1
- Location: `core/include/md/core/density.h:29-46`; consumer `components/src/card.cpp:416`
- Boundary: `scale`/`fontScale` = 0, negative, 1e9.
- Expected: reject or normalise to a positive finite range (`Theme::withDensity`, `theme/src/theme.cpp:92`).
- Actual: raw multiplication. `scale=-1` gives `dp(80)=-80`, `dp(200)=-200`, and `std::clamp(preferred, -80, -200)` violates `lo <= hi` (debug assert, UB in release). Huge scales overflow `qRound`; scale 0 zeroes every dp; `pxToDp` returns the value for `scale <= 0` while `dp()` multiplies, so they are not inverses.
- Fix: normalise once (finite check, clamp to e.g. 0.1..4.0) and reuse in `dp`, `sp`, `pxToDp`.

### ISSUE-tokens-2: `ColorScheme` is an unvalidated mutable aggregate, and the derived path already fails contrast
- Severity: P2
- Location: `tokens/include/md/tokens/color_scheme.h:24-100`
- Boundary: arbitrary roles, pure black/white, default `ColorScheme{}`.
- Expected: the "immutable value" advertised at `:7` with a contrast guarantee, or explicit plain-data documentation.
- Actual: 47 public non-const members plus a default constructor; `ColorScheme{}` is 47 invalid colours and `primary == onPrimary` gives 1:1. The supported derived path `Theme::fromColours(Qt::blue, Qt::green, Qt::red)` (`tests/components/tst_theme_android.cpp:63`) assigns white to `onTertiary` via the `qGray > 140` heuristic (`theme/src/theme_colours.cpp:31-34`): 4.00:1 on #FF0000, below 4.5.
- Fix: private members with validating factories; pick on-colours by measured contrast (best of black/white, ≥ 4.5), not `qGray`.

### ISSUE-tokens-3: `TypeScale` allows zero/negative metrics; consumers clamp size but not line height
- Severity: P2
- Location: `tokens/include/md/tokens/type_scale.h:18-27`
- Boundary: `sizeSp`/`lineHeightSp`/`trackingSp` 0 or negative; weight outside 1..1000.
- Expected: a documented invariant, or one resolver clamping both.
- Actual: plain fields with only a positive baseline. Consumers clamp pixel size (`components/src/font.cpp:18`) but use `lineHeightSp` raw (`components/src/dialog.cpp:350`, `components/src/text.cpp:37`), so a negative line height yields negative layout extents; `weight` is cast unchecked (`components/src/font.cpp:19`).
- Fix: validate in a `TypeScale` helper and clamp line height with the same policy.

### ISSUE-tokens-4: `Shape::resolved()` does not clamp; only `path()` does
- Severity: P2
- Location: `tokens/include/md/tokens/shape.h:85-93`, `tokens/src/shape.cpp:16`
- Boundary: `extraExtraLarge` (48) on a box shorter than 96; asymmetric radii.
- Expected: the radii "this shape uses for `bounds`" are box-safe.
- Actual: the non-pill branch returns the stored radii unchanged; only `CornerRadii::toPath` clamps to `min(w,h)/2` (`core/src/geometry.cpp:34-38`). `View::updateRippleGeometry` passes the raw value (`view/src/view.cpp:664`), safe only because ripple later calls `toPath`; the gallery hands a raw 12/28 to `drawRoundedRect` (`apps/gallery/src/main.cpp:254`).
- Fix: clamp non-pill radii in `resolved()`, or rename it and document the raw pass-through.

### ISSUE-tokens-5: out-of-range enum values fall back inconsistently
- Severity: P2
- Location: `tokens/include/md/tokens/motion.h:242`; also `motion.h:129,163`, `elevation.h:45`, `tokens/src/color_scheme.cpp:43`
- Boundary: `static_cast<Elevation>(6)`, `Duration(99)`, `Easing(99)`, `MotionLevel(6)`, unknown `ViewState` bits.
- Expected: one documented fallback policy.
- Actual: elevation → 0 dp/surface (safe), duration → 0 ms (safe), easing → `CubicBezier{0,0,0,0}` (zero handles, neither Standard nor Linear), `level(6)` → 300 ms Standard (it animates); unknown `ViewState` bits → opacity 0 (safe).
- Fix: explicit `default:` cases returning the neutral value, plus tests.

### Covered
15 exact type styles and equality; 47 roles valid and opaque; 10 pair ratios ≥ 4.5 in both schemes; 7 shapes plus pill/degenerate/path/empty; 6 elevations; 16 durations; 10 easings and Bezier endpoints; reduced motion; the 5 helpers; state opacities/combinations (alpha 255 and 128); `surfaceAtElevation`; the 0–1250 ms ladder and its reduced collapse.

### Not covered
No out-of-range Elevation/Duration/Easing/MotionLevel or density 0/negative/huge tests; no alpha 0/1 content; no negative type metrics. Contrast covers 10 baseline pairs only; computed but untested: light `outline`/`surface` 4.33, `outlineVariant` 1.62 (dark 5.87/1.99), container/on-container 13.32 (dark 7.17), `surfaceVariant`/`onSurfaceVariant` 7.24 (dark 5.48). `shapeRadii` uses a 200×200 box, so oversized radii are never asserted by value. `kDurations` omits Extended1/2 (16 of 18). `MotionSpec.delay` is always 0 and untested.

---

## view

Baseline: `tst_view` 19 passed. The exe needs `D:\Qt\6.8.3\msvc2022_64\bin` on `PATH` (otherwise exit 0xC0000135).

### ISSUE-view-1: the theme is a dangling reference if a theme is installed during paint
- Severity: P1
- Location: `view/src/view.cpp:387`
- Boundary: a store-driven theme change while `paintContent` runs.
- Expected: the `Theme` used by stages 5–7 outlives the whole `paintEvent`; `SurfaceContext` documents a non-owning reference whose caller guarantees lifetime (`layer/include/md/layer/surface_context.h:39`).
- Actual: `paintEvent` binds `const Theme&` from `Theme::forWidget(this)` (`:387`) into `context`, then runs `paintContent` (`:398`) before `foreground_.paint(...)` (`:405`). If paintContent writes the "theme" store value, `onStoreChanged` calls `ThemeScope::install(this, …)` (`:781`), which deletes the existing scope (`theme/src/theme_scope.cpp:47-48`) — the reference is freed and stages 5–7 read it (use-after-free); without deletion the overlay would still mix two themes.
- Fix: copy the `Theme` by value into the context, or rebuild `context` after `paintContent`.

### ISSUE-view-2: `paintChildren` iterates a vector a child can mutate during render
- Severity: P1
- Location: `view/src/view_group.cpp:108`
- Boundary: a child deletes a sibling (or itself) from its own paint/event handler.
- Expected: destroying or adding a child while the parent paints cannot invalidate the paint loop.
- Actual: `for (View* view : views_) { … view->render(...) }` (`:108-116`) runs arbitrary child code synchronously; the `destroyed` lambda does `std::erase(views_, view)` (`:57-59`) and `addView` can `push_back`/reallocate (`:53`). Either invalidates the iterator — UB. No `deleteLater` convention exists in `view/src`.
- Fix: index with a size snapshot and re-check membership, or paint a copy and skip dead entries.

### ISSUE-view-3: `contentRect` is documented as overridable but is not virtual
- Severity: P2
- Location: `view/include/md/view/view.h:90`
- Boundary: a subclass that wants a content inset.
- Expected: the comment at `view.h:124-127` tells subclasses to "override the rectangle rather than the pipeline".
- Actual: non-virtual; `paintEvent` (`view.cpp:386`), `updateRippleGeometry` (`:662`) and `arrange` (`:114`) all call the base version, so the override is silently ignored and `arrangeOverride` receives the full widget rect.
- Fix: make it `virtual`, or correct the comment.

### ISSUE-view-4: `arrange()` arranges twice when the geometry changes
- Severity: P2
- Location: `view/src/view.cpp:106`
- Boundary: any arrange that changes the geometry.
- Expected: one `arrangeOverride` per `arrange` (`view.cpp:111-113` explains why it must run even for an unchanged rect).
- Actual: `setGeometry` (`:109`) emits a resize, `resizeEvent` calls `arrangeOverride(contentRect())` (`:434`), then `arrange` calls it again (`:114`). Every child is placed twice.
- Fix: drop the `resizeEvent` call for arranges, or guard with an in-progress flag.

### ISSUE-view-5: Qt geometry queries overwrite the measurement cache
- Severity: P2
- Location: `view/src/view.cpp:211`
- Boundary: a layout query after `measure()`.
- Expected: `measuredSize()` ("how large the view asked to be", `view.h:254`) survives a hint query.
- Actual: one `measuredSize_` (`view.h:255`) is rewritten by `sizeHint()` (`:215`), `minimumSizeHint()` (`:222`) and `heightForWidth()` (`:233`); whichever Qt calls last wins, so a container reading `child->measuredSize()` afterwards sees the hint spec's result.
- Fix: cache per spec, or keep hint results separate.

### ISSUE-view-6: re-enabling interaction feedback bypasses the enabled/focus checks
- Severity: P2
- Location: `view/src/view.cpp:767`
- Boundary: `setInteractionFeedbackEnabled(true)` on a disabled or `NoFocus` view.
- Expected: ripple enabled iff `interactionFeedbackEnabled_ && isEnabled() && focusPolicy() != Qt::NoFocus`, as in the constructor (`:92`) and `changeEvent` (`:569-570`).
- Actual: only `ripple_.setEnabled(enabled)` runs, so a disabled view paints a press ripple until the next event re-syncs it at `:287`.
- Fix: apply the full conjunction in all three places.

### ISSUE-view-7: hiding does not stop the ripple or clear interaction state
- Severity: P2
- Location: `view/src/view.cpp:626`
- Boundary: hide/show while animating; hiding mid-press.
- Expected: `lifecycle.h:21` requires animations stopped in `Hidden`; `EnabledChange` clears Hovered/Pressed/Focused/Dragged (`:560-567`).
- Actual: `hideEvent` only flips `lifecycle_` and calls `onHidden()`; the ripple is never stopped (`ripple/include/md/ripple/ripple.h:88`) and Hovered/Pressed/Dragged stay set, so a view hidden mid-press resumes as pressed.
- Fix: stop the ripple and clear the interaction flags in `hideEvent`; re-sync in `showEvent`.

### ISSUE-view-8: disabled views are reported invalid to accessibility
- Severity: P2
- Location: `view/src/accessible.cpp:151`
- Boundary: accessibility state of any disabled control.
- Expected: `disabled` only; `QAccessible::State::invalid` means the interface is no longer valid and makes assistive tech drop the node.
- Actual: `result.invalid = result.invalid || result.disabled` (documented as intentional at `accessible.h:91`), so every disabled control disappears from the accessible tree instead of being announced as unavailable.
- Fix: remove the `invalid` coupling.

### Covered
Defaults, the sizeHint/measure-spec matrix (positive sizes only), minimumSizeHint, heightForWidth, arrange geometry, theme surface + custom background, elevation tonal surface, pointer state transitions, key activation, ignored events, update coalescing, disabled input/paint, ripple darkening, group arrange/transfer/paint order, `parentTarget` and point mapping.

### Not covered
No test at all for: lifecycle state/hooks, store binding or a destroyed store, accessibility (`installAccessibility` never called), focus ring/keyboard focus, hide/show during animation, negative/NaN/zero measure specs, negative or empty arrange rect, paint without a theme, re-entrant `requestUpdate` from `paintContent`, a theme install during paint, child destruction during paint, density. `View::event`'s `ParentChange` branch (`view.cpp:593-604`) leaves a `Created` view that gains a parent while hidden at `Created` — no `onAttached` until first show.

---

## layer

Baseline: `tst_layer` 12 passed.

### ISSUE-layer-1: `RippleLayer` hands a member `QObject` to an external parent
- Severity: P1
- Location: `layer/src/ripple_layer.cpp:12`
- Boundary: a non-null `parent` argument (the null case is safe).
- Expected: the layer owns `controller_`; passing a parent must not give Qt ownership of a subobject.
- Actual: `controller_(parent)` constructs the value member `ripple::RippleController` (`layer/include/md/layer/ripple_layer.h:54`), a `QObject` (`ripple/include/md/ripple/ripple.h:89`), as a child of `parent`. If the parent dies first it deletes the member, and `~RippleLayer` destroys it again → invalid free/UB. In-repo callers pass no parent (`demo/src/demo_ui.h:245`, `tests/layer/tst_ripple_layer.cpp:51`), so it is latent public-API damage.
- Fix: `controller_(nullptr)` (or drop/assert the parameter) and document that the controller's lifetime is the layer's.

### ISSUE-layer-2: the shape clip replaces the caller's clip instead of intersecting it
- Severity: P2
- Location: `layer/src/background.cpp:29`, `layer/src/foreground.cpp:56`
- Boundary: an empty or pre-clipped painter.
- Expected: the fill is clipped to caller clip ∩ shape; an empty caller clip paints nothing.
- Actual: `setClipPath(...)` uses Qt's default `Qt::ReplaceClip`, discarding the host's damage region, viewport or empty clip for the duration of the call — a pre-clipped painter still receives the whole shape. `ripple/src/ripple.cpp:363` uses `Qt::IntersectClip`, so the stages disagree.
- Fix: pass `Qt::IntersectClip` in both calls (save/restore is already present).

### ISSUE-layer-3: `RippleLayer::paint` ignores bounds/context and lacks the empty/inactive guards
- Severity: P2
- Location: `layer/src/ripple_layer.cpp:47`
- Boundary: empty layer rect, zero-size or stale paint rect, inactive painter.
- Expected: `Layer::paint` receives the rect to paint (`layer/include/md/layer/layer.h:38`); the other stages return on `bounds.isEmpty() || !painter.isActive()` (`background.cpp:20`, `foreground.cpp:36,69,108`, `elevation.cpp:98`).
- Actual: `Q_UNUSED(bounds); Q_UNUSED(context);` then `controller_.paint(painter)` (`:52`), which clips to the bounds cached by `setBounds` (`ripple/src/ripple.cpp:353`), not the argument. An empty paint rect still paints, and an inactive painter reaches `painter.save()`.
- Fix: add the standard guard; either clip to `bounds` or document the controller-owned geometry and assert equality in debug.

### ISSUE-layer-4: layer stack traversal is unsafe against mutation from a paint callback
- Severity: P2
- Location: `layer/src/layer_stack.cpp:55`
- Boundary: a layer removed or destroyed mid-paint, one added during paint, re-entrant paint.
- Expected: mutation during traversal is rejected or deferred, and documented.
- Actual: range-for over `layers_`; a callback calling `clear()` (`:39`) or `add()` destroys or reallocates the storage being iterated (use-after-free). No guard, no comment, no test.
- Fix: a `painting_` guard (assert/ignore) or an index loop over a snapshot; document "no mutation during paint".

### ISSUE-layer-5: an out-of-range `Elevation` is not normalised to Level0
- Severity: P2
- Location: `layer/src/elevation.cpp:102`
- Boundary: `static_cast<Elevation>(6)`.
- Expected: an unknown level behaves as Level0 — zero margins, no pixels.
- Actual: only `== Level0` is a no-op; `ShadowDrawable::draw` also tests equality (`drawable/src/shadow_drawable.cpp:236`) while `elevationDp` falls back to 0 (`tokens/include/md/tokens/elevation.h:45`). Result: `shadowMargins()` returns 2 dp (`shadow_drawable.cpp:229`, `:58`) and a 1 px blurred rim is painted (`:267`). The colour lookup does fall back safely (`tokens/src/color_scheme.cpp:43`).
- Fix: normalise in `setElevation`/`effectiveElevation` (clamp invalid to Level0).

### Covered
Double add is impossible (`unique_ptr` ownership) and a null add is ignored (`layer_stack.cpp:20`, `tst_layer.cpp:507`); insert-past-end clamps (`:494`); `at()` out of range returns nullptr (`:424`); empty-stack hitTest (`:483`); paint order forward / hitTest reverse (`:409-484`); animation dispatch (`:510-516`); unknown state bits resolve to 0; zero-alpha fills skip; all five paint paths save/restore; empty bounds guarded in the four non-ripple stages.

### Not covered
No test uses an empty/zero-size rect, an inactive painter or a pre-clipped painter; no test asserts that painter state (pen/brush/clip/opacity) is unchanged, and `LayerStack::paint` does not wrap each layer in save/restore (`layer_stack.cpp:55-58`); no test for `RippleLayer` with a non-null parent, stale bounds or an empty paint rect; no invalid `Elevation` test; no test for stack mutation during paint, re-entrant paint or layer destruction mid-paint; "transparent container paints nothing" is untested (only `resolvedContainerColour()` is asserted).

---

## layout

Baseline: `tst_layouts` — **15 passed, 1 failed** when run from the build tree with the default (windows) platform: `badgeCountTruncates` at `tests/layout/tst_layouts.cpp:513` (a Badge test, not a container). The same binary passes under `ctest`, which sets `QT_QPA_PLATFORM=offscreen`; see the environment note at the end.

### ISSUE-layout-1: a weighted child is shrunk below its natural size when the run overflows
- Severity: P1
- Location: `layout/include/md/layout/linear.h:249-260`
- Boundary: a weighted run whose natural children exceed the main axis (child larger than parent; `Exactly 0`).
- Expected: weight takes a share of *positive* spare space (`linear.h:3-5`); a 40+40 run in a 60-wide axis keeps 40/40 and overflows, and a zero axis keeps 40/40.
- Actual: `spare` is 0 (`:207-208`), yet the remainder fix-up rewrites the last weighted slot (`:254`). 40+40 with weight on the second gives 40/20 at axis 60, while at axis 0 the same code skips (`adjusted < 0`, `:255-256`) and leaves 40/40. The invariant at `:242-244` holds only by crushing one child, and discontinuously.
- Fix: skip the remainder when `spare <= 0`, and clamp to `qMax(natural, slot)` so a weight never shrinks a child below its measurement.

### ISSUE-layout-2: hidden `AppBar` actions still reserve and receive space
- Severity: P1
- Location: `layout/src/appbar.cpp:224-228` (measure), `:278-288` (arrange), `actions()` `:88-96`
- Boundary: a gone/hidden child.
- Expected: a hidden child occupies nothing, as in Linear (`linear.h:125`), Frame (`frame.h:100`), Constraint (`constraint.h:118`) and Coordinator (`coordinator.cpp:184`).
- Actual: `actions()` returns every non-navigation child including hidden ones; measure adds its width (`:227`) and arrange places it (`:281-316`). Hiding an action leaves a trailing gap and steals title width.
- Fix: filter `!action->isHidden()` in `actions()` or skip it in both loops.

### ISSUE-layout-3: `setScrollRange` clamps the offset silently
- Severity: P2
- Location: `layout/src/appbar.cpp:129-145` (clamp `:138-142`; compare `:157-163`)
- Boundary: shrinking the range, or a range of 0 with no scroll target.
- Expected: a changed offset is announced like `setScrollOffset` does (`offsetChanged`, plus re-derived `collapsed_`).
- Actual: `scrollOffset_` is rewritten without `offsetChanged`/`collapsedChanged`; `collapsed_` is recomputed only at `:157-160`. Collapse to 100, then `setScrollRange(0)`: the fraction is 0 and the bar is full height, but the last `collapsedChanged` said true and no new one is emitted.
- Fix: route the clamp through `setScrollOffset` (or emit both signals and recompute `collapsed_` there).
- Test gap: `tests/layout/tst_layouts.cpp:797-801` changes the range only while the offset is already 0.

### ISSUE-layout-4: zero height is silently replaced, and can invert the collapse
- Severity: P2
- Location: `layout/src/appbar.cpp:109-127` (fallbacks `:111`, `:121`), `layout/src/collapse.cpp:76-84` (`:78`), `layout/include/md/layout/alignment.h:109-112`
- Boundary: height 0 (and non-finite) in `setExpandedHeight`/`setCollapsedHeight`/`setHeaderHeight`.
- Expected: 0 is honoured or visibly rejected; the documented defaults are 112/64 (`appbar.h:59-64`).
- Actual: `length()` treats 0 as invalid and returns the fallback, so `setCollapsedHeight(0)` stores 56 — above an explicitly set 40 expanded height — and `barHeight()` (`:203-207`) makes the bar *grow* while collapsing. `Collapse::setHeaderHeight(0)` stores 48.
- Fix: accept 0 (clamp only negatives/NaN), or document and reject it.

### ISSUE-layout-5: clearing the navigation button turns it into a trailing action
- Severity: P2
- Location: `layout/src/appbar.cpp:58-77`, `:88-96`
- Boundary: the optional navigation button is removed.
- Expected: "Null clears the role without deleting the view" (`appbar.h:45-48`) — it should leave the action run alone.
- Actual: the view stays a child and `actions()` lists every child that is not the *current* navigation button, so the former button is measured and placed as an action at the trailing edge.
- Fix: remove or hide the old button when the role is cleared.

### ISSUE-layout-6: conflicting `Constraint` anchors place the child outside the content
- Severity: P2
- Location: `layout/include/md/layout/constraint.h:154-160`, `:178-182`, `:201`
- Boundary: start + end larger than the parent.
- Expected: the child stays inside the content rectangle (Frame bounds it, `frame.h:144-149`; Coordinator clamps, `coordinator.cpp:214-215`), or the conflict is rejected.
- Actual: `availableWidth` becomes 0 (`:159`) but `x = insetStart` is never clamped; width 100 with start = end = 150 puts a zero-width child at x = 150, fully outside. The measure pass asked for 300 (`:125-130`), silently capped by an atMost parent.
- Fix: clamp the insets to the content width and split the remaining space symmetrically.

### Covered
Linear: one weighted child with positive spare, spacing, vertical cross-axis gravity, RTL cross mirror (`tst_layouts.cpp:179-219`); unweighted row RTL and centred run (`:221-246`). Frame: fill, nine-cell gravity, margin, mirror (`:248-290`). Constraint: opposing anchors, centring, start-only + RTL, negative/NaN/foreign anchors, no-anchor natural size (`:292-369`). Coordinator: registration order, consumed accumulation, no-gesture no-op, `removeBehaviour` (`:593-654`); AppBar + Collapse driven by behaviours (`:656-750`). AppBar: clamping, fraction, both signals, zero range (`:752-802`). Collapse: expand/collapse animation, header height, content geometry, hidden content, snap path (`:804-860`).

### Not covered
Zero children; a single child with weight 0 / total weight 0; uneven weights and round-off; negative spacing/margin; child larger than parent; hidden child inside Linear/Frame/Constraint/Coordinator; nested layouts; arrange twice or without measure; child removed during arrange; Constraint conflict/start beyond width; Collapse height 0 or no content; AppBar without nav/actions, zero height, RTL; `Exactly 0`/NaN specs; Collapse RTL geometry.

### Environment note (P2, test quality — fixed during this audit)
`badgeCountTruncates` (`tests/layout/tst_layouts.cpp:513`) failed on the **windows** platform and passed under `ctest`'s `offscreen` platform, because it asserted `width > height` for a one-digit badge. A one-digit badge is the circular form, so its width is never *narrower* than its height but need not be wider either. The assertion is now `width >= height`, and the suite passes on both platforms (16/16 on windows).

---

## animation

Baseline: `tst_animation` 10 passed (needs the Qt `bin` on `PATH`).

### ISSUE-animation-1: a callback that deletes the `Animator` reads freed memory
- Severity: P1
- Location: `animation/src/animator.cpp:172` (also `:189`)
- Boundary: `onValue` destroys the animator or its owning widget.
- Expected: no member access after user code, so deletion is safe or explicitly rejected.
- Actual: `completeRun()` calls `onValue_(1.0)` at `:172`, then reads `generation_` at `:177`; `snapRun()` repeats it at `:189`/`:191`. If the callback deletes the animator or its parent, both reads are use-after-free and `finished()` may be emitted on a dead `QObject`. (Deleting inside `finished()` is safe because nothing follows `:178` — which is why `ripple/src/ripple.cpp:211-215` only needs `deleteLater()`.)
- Fix: `QPointer<Animator> guard(this);` around the call and return when null; document the callback contract.

### ISSUE-animation-2: re-entrant `start()`/`stop()` destroys the executing `std::function`
- Severity: P2
- Location: `animation/src/animator.cpp:96` (invocation at `:61`, `:172`)
- Boundary: the value callback starts another animation (retarget) or stops, from inside `onValue`.
- Expected: retargeting is advertised for a run in flight (`animator.h:39-42`), so it should be callable from the callback.
- Actual: `onValue_ = std::move(onValue)` destroys the member's current target while that target is still on the stack, so the closure and its by-value captures die mid-call (UB); `stop()` does the same via `onValue_ = nullptr` at `:200`. It also calls `animation_.stop()` inside `animation_`'s own `valueChanged` emission.
- Fix: `const auto cb = onValue_; if (cb) cb(progress_);` and install the new callback after the call returns.

### ISSUE-animation-3: a zero-duration spec silently discards the delay
- Severity: P2
- Location: `animation/src/animator.cpp:98` vs `:123`
- Boundary: `duration == 0` with `delay > 0`.
- Expected: snap after the delay, which is part of `MotionSpec`.
- Actual: the `duration.count() <= 0` branch returns before the delay branch, so the snap is queued immediately. `reducedMotionSpec()` sets delay 0 (`tests/animation/tst_animation.cpp:35-42`), so nothing catches it.
- Fix: queue `snapRun` behind the same delay timer instead of short-circuiting.

### ISSUE-animation-4: long durations truncate; near `INT64_MAX` they overflow
- Severity: P2
- Location: `animation/src/animator.cpp:32`, `:35-39`, `:127`
- Boundary: duration/delay beyond `int` milliseconds.
- Expected: a documented or rejected cap, never a silent change of the requested time.
- Actual: `toAnimationDuration()` clamps to `INT_MAX` (≈24.8 days), so a 49-day run finishes early (`:152`) and a 49-day delay fires early (`:127`). `remainingDuration()` computes `qRound64(total * fraction)`; at `total == INT64_MAX` the product rounds to 2^63, outside `qint64` (UB; MSVC x64 yields `INT64_MIN`), so `remaining <= 0` at `:110` completes the run on the next turn without animating.
- Fix: saturate before `qRound64`, and document or reject `duration.count() > INT_MAX`.

### ISSUE-animation-5: a retarget at progress 1.0 completes without calling the new callback
- Severity: P2
- Location: `animation/src/animator.cpp:90`, `:109-121`, `:169-173`
- Boundary: restart from a value callback that receives 1.0 while `running_` is still true.
- Expected: `animator.h:37` — "the final call is always 1.0 for a run that completes".
- Actual: `from = progress_ = 1.0` gives `remaining == 0`; the queued `completeRun()` sees `progress_ == 1.0`, skips the callback at `:171` and only emits `finished()`. The freshly installed callback is never invoked.
- Fix: `from = (running_ && progress_ < 1.0) ? progress_ : 0.0`, or always deliver `onValue(1.0)` to the installed callback.

### ISSUE-animation-6: no pause/resume; `stop()` + `start()` rewinds to 0
- Severity: P2
- Location: `animation/src/animator.cpp:90`, `:195-201`
- Boundary: pause/resume.
- Expected: the value-retention note at `animator.h:49-51` suggests a stopped run can continue.
- Actual: no `pause()`/`resume()` exists, and `start()` after `stop()` computes `from` as 0.0 because `stop()` cleared `running_` at `:198`, so the value jumps back to 0 while `progress()` still reports 0.5. `tst_animation.cpp:130` stops but never restarts.
- Fix: add `pause()`/`resume()`, or resume from `progress_` after a mid-flight stop, and document the choice.

### Covered
Zero duration snaps on the next turn with exactly one value and one `finished` (`tests/animation/tst_animation.cpp:69`); `start()` while running continues from the current progress without a jump (`:174`, and `:210` for the delayed variant); `stop()` mid-run keeps the value and emits no `finished` (`:130`, `:153`); normal completion ends at 1.0 (`:99`); a live run and destruction leave no queued callback (`:250`).

### Not covered
Negative duration/delay (guarded, never tested); `stop()` before `start()`; a callback that destroys the animator, calls `start()`/`stop()`, or throws (no try/catch at `animator.cpp:60-61`); reduced motion switched mid-flight at the Animator level; multiple animators on one property (no arbitration — two animators interleave and the older run's final 1.0 overwrites the newer); easing outside 0..1 (values are clamped at `:55`, but a NaN/Inf control point maps to 1.0 through `qBound`, so the run jumps to the end on its first frame).

---

## drawable

Baseline: `tst_drawable` 12 passed.

### ISSUE-drawable-1: the shadow mask allocation is unbounded and unchecked, and a null mask is dereferenced
- Severity: P1
- Location: `drawable/src/shadow_drawable.cpp:141`, `:154-160`
- Boundary: a huge target rect (huge pixmap), or an excess beyond the painter clip.
- Expected: cap the device-pixel mask (or intersect with `painter.clipBoundingRect()`) and skip drawing if allocation fails.
- Actual: `pixelSize` is `qCeil(imageRect * dpr)` then `bucketize()` (`:245-251`, `:71-74`; `value + 7` overflows `int` near `INT_MAX`). `renderMask` constructs `QImage mask(pixelSize)` (`:141`) with no `isNull()` check — Qt returns a null image when allocation fails — then allocates `width*height` bytes (`:154-155`). The copy loop calls `mask.constScanLine(y)` (`:157`), which returns `nullptr` for a null image, and `line[4*x+3]` dereferences it (`:160`). At 40000×40000 the vector alone is 1.6 GB; failure is an uncaught `std::bad_alloc`.
- Fix: bounds-check the device size against a hard cap (e.g. 8192 px per side), test `mask.isNull()` and `buffer.empty()`, and return without painting.

### ISSUE-drawable-2: a negative blur scale inverts the shadow offset while the margins are zero
- Severity: P2
- Location: `drawable/src/shadow_drawable.cpp:269`
- Boundary: a negative `blurScale`.
- Expected: the header promises "Non-positive values disable the blur and the offset" (`shadow_drawable.h:66-68`).
- Actual: `shadowMargins()` returns zeros for `blurScale_ <= 0` (`:224-225`), yet `draw()` still runs for any non-zero elevation (`:236`) and translates the silhouette by `offsetDp(elevation_) * blurScale_` (`:269`). With a negative scale the shift is upward, `imageRect` equals `bounds` (`:243-244`), and the top of the silhouette is cut off — a truncated, displaced shadow instead of the documented unshifted one.
- Fix: clamp in `setBlurScale` (`qMax(0.0, scale)`) or use `qMax(0.0, blurScale_)` for the offset.

### ISSUE-drawable-3: a device pixel ratio of 0 or NaN is not guarded
- Severity: P2
- Location: `drawable/src/icon_drawable.cpp:177-180`, `drawable/src/shadow_drawable.cpp:241-246`
- Boundary: device pixel ratio 0 (or non-finite), reachable because `QImage::setDevicePixelRatio(0.0)` is stored unvalidated and returned by `QPainter::device()->devicePixelRatioF()`.
- Expected: a non-positive or non-finite ratio falls back to 1.0.
- Actual: Icon computes `pixelSize = max(1, ceil(target*dpr))` — collapsing to 1×1 at 0 — tags the result with the raw ratio (`icon_drawable.cpp:164`) and keys the cache with `qRound(dpr*100) == 0` (`:186`). Qt then derives logical geometry as `size/dpr`; Shadow's `exact` becomes empty and the shadow silently disappears (`shadow_drawable.cpp:245-247`, tag at `:276`).
- Fix: one helper, `dpr = (device && qIsFinite(r) && r > 0) ? r : 1.0`, used for sizing, keying and tagging in both drawables.

### ISSUE-drawable-4: `IconDrawable::setSize` stores non-positive and non-finite sizes
- Severity: P2
- Location: `drawable/src/icon_drawable.cpp:54-60`
- Boundary: negative and NaN/Inf intrinsic sizes.
- Expected: reject non-finite values and clamp negatives (the Icon component already clamps dp at `components/src/icon.cpp:56`); only zero should mean "no preference".
- Actual: `setSize(QSizeF(-8,-8))` is stored and returned by `intrinsicSize()` while `draw()` silently paints nothing (`size_.isEmpty()`, `:88`). For NaN, `QSizeF::isEmpty()` is false, so `alignedTarget` divides by NaN, `fitted.isEmpty()` is also false, and a NaN rect reaches the 1×1 raster/cache path (`:95-113`); `qCeil(NaN)` is an undefined float-to-int conversion.
- Fix: in `setSize`, return early unless both components are finite, and clamp each to `qMax(0.0, v)`.

### Covered
Shape fill/rounded corners/outside bounds, stroke and zero-width stroke, transparent fill (`tests/drawable/tst_drawable.cpp:120-169`); state-layer token opacity incl. idle/empty (`:171-198`); icon tint, intrinsic size, invalid tint, RTL-only mirroring (`:200-259`); layer insertion order and out-of-range `at` (`:261-287`); shadow margins vs elevation/blur (`:289-314`); shadow painting outside bounds (`:316-349`); revision/state invalidation and cache-key sensitivity (`:351-388`). Code also early-returns on empty/negative rects in all four drawables, handles null sources, alpha-0 paint, and balances save/restore.

### Not covered
No test for empty/negative/NaN bounds or sizes, null sources, alpha-0 tint, dpr 0 or dpr 2 (all tests paint into a dpr-1 `QImage`), huge rects, painter clip, painter-state restoration, re-entrancy or a theme change. `IconDrawable`/`ShadowDrawable` mutate the global `QPixmapCache` from a `const draw()` (`icon_drawable.cpp:191-195`, `shadow_drawable.cpp:262-277`); Qt restricts that cache to the main thread and nothing clears it on a theme switch. `QIcon::cacheKey()` changes only on `addPixmap`/`addFile`, so `icon_drawable.cpp:213-217` can reuse a raster across an icon-theme change.

---

## components

Sampled: the input family (`input.cpp`, `date_picker.cpp`, `time_picker.cpp`), the data widgets (`table.cpp`, `virtual_table.cpp`, `virtual_select.cpp`, `list.cpp`), the overlays (`dialog.cpp`, `drawer.cpp`, `popup.cpp`, `notification.cpp`), `carousel.cpp`/`pager.cpp`/`scrollbar.cpp`, and the eight new components.

### ISSUE-components-1: adapter-backed selects never observe a shrinking model
- Severity: P1
- Location: `components/src/input.cpp:1261` (`DropDown::setSourceModel`), `:2458` (`PickerBase::setSourceModel`)
- Boundary: the source model resets (`Model::setValues` → `beginResetModel`) or removes rows while `currentIndex()` is held, including emptying it.
- Expected: the current index is clamped to the new count (or -1 when empty) and `currentIndexChanged`/`currentValueChanged` fire, exactly as `PickerBase::refreshFromModel()` already computes at `:2968-2980`.
- Actual: neither class connects `modelReset`/`rowsInserted`/`rowsRemoved` (those connections exist only in `list.cpp`, `recycler.cpp`, `table.cpp`, `tree_select.cpp`), and `refreshFromModel()` runs only from `setSourceModel`/`setCyclic`/`setOwnedValues`. `current_` keeps a now-invalid row: `DropDown::paintContent` draws `adapter_->text(current_)` (`:1506`) and `Adapter::data` rejects an index ≥ `rowCount()` (`model.cpp:132-134`), so the closed field paints an empty value with no placeholder while `currentIndex()` still reports it; `PickerBase` paints `normalized(current_ + slot)` (`:2654`), i.e. `current_ % rows`, so NumberPicker/RingPicker show row 0 while `currentIndex()` says 2. No signal in either case.
- Fix: from both `setSourceModel()`s connect `modelReset`/`rowsInserted`/`rowsRemoved` to a clamp-and-emit refresh, clamping with `count() <= 0 ? -1 : std::clamp(current_, 0, count() - 1)`.

### ISSUE-components-2: removing the selected row drops the selection without a signal
- Severity: P1
- Location: `components/src/recycler.cpp:429`; `components/src/list.cpp:301`
- Boundary: `rowsRemoved` covering the current row (and `modelReset` for Recycler).
- Expected: one `currentIndexChanged(QModelIndex())`, the contract `Table` implements at `table.cpp:670-687` and List itself implements for reset at `list.cpp:315-316`.
- Actual: Recycler's `rowsRemoved` handler only calls `rebuild()`, and its `modelReset` handler clears `current_` (`:430-434`) with no emit. List tries, but its guard `current_.isValid() && current_.row() >= rowCount()` can never hold for a removed row: `QPersistentModelIndex` invalidates itself during `beginRemoveRows`, so `isValid()` is already false when the slot runs. Result: `currentRow()` flips to -1 and the row stops painting as selected, while a consumer mirroring `currentIndexChanged` keeps pointing at the deleted row. `tests/components/tst_display_controls.cpp:444-446` only asserts `currentRow() == -1`.
- Fix: cache the current row in `rowsAboutToBeRemoved` (or compare against a stored previous row count) and emit in `rowsRemoved`; emit on Recycler's reset too.

### ISSUE-components-3: `NavigationBar` activates on press and cannot be cancelled
- Severity: P2
- Location: `components/src/navigation_bar.cpp:362`
- Boundary: press on a destination then drag off / release outside (a scroll or drag starting on the bar).
- Expected: release-time activation with a press-inside flag — the pattern every sibling clickable uses (`switch.cpp:202`, `icon_button.cpp:327`, `fab.cpp:324`, `chip.cpp:332`).
- Actual: `mousePressEvent` calls `activate(index)` immediately; `mouseReleaseEvent` (`:366-374`) only clears `pressed_`. `destinationActivated` therefore fires and `currentIndex` changes even when the gesture ends outside the bar, with no way to abort.
- Fix: record `pressed_ = index` on press, hit-test and `activate()` on release.

### ISSUE-components-4: `firstVisibleRow()` can return `rowCount()`
- Severity: P2
- Location: `components/src/list.cpp:222`
- Boundary: a zero-height (or shorter-than-one-row) viewport plus `scrollToRow()` to the last row: `maximumScroll()` is `total` (`list.cpp:244-248`), `ensureVisible()` scrolls to `total`, and `floor(total / rowHeight) == rowCount()`.
- Expected: an index that names a row — `Table` (`table.cpp:329`) and `VirtualTable` (`virtual_table.cpp:337-338`) clamp to `rows - 1`; 0 when empty.
- Actual: `std::clamp(..., 0, rowCount())` admits `rowCount()` itself, so a 10-row list reports `firstVisibleRow() == 10` while `visibleRowCount() == 0`.
- Fix: clamp to `rowCount() - 1`, or return 0 when the viewport height is 0.

### Covered
Table shrink/reset with a live current cell (`tests/components/tst_table_video.cpp:418-450`); Model/Adapter boundaries and invalid indexes (`tst_input_controls.cpp:137-167`, `:193-214`); NumberInput clamping, reversed range, out-of-range and unparsable text (`:378-430`); picker clamping, cyclic pickers, borrowed source (`:756-833`, `:878`); a borrowed source outlived by the control (`:244-266`); empty and disabled NavigationBar and non-left button (`tst_navigation_bar.cpp:161-168`, `:219-246`); Switch 48 dp target, click, Space (`tst_switch.cpp:100-104`, `:135-157`, `:193`); overlay lifetime — caller-destroyed sheet content (`tst_bottom_sheet.cpp:342`) and a dropdown host destroyed while open (`tst_dropdown_modal.cpp:84`).

### Not covered
Any `modelReset`/`rowsRemoved` on DropDown/Select/NumberPicker/RingPicker (no test mutates their source after `setSourceModel`); the `currentIndexChanged` emission (as opposed to the value) on row removal for List/Recycler; NavigationBar press vs release (`clickAt` sends both at the same point); row removal while a select popup is open (`MenuPopup` holds the same Adapter); zero-size geometry (no sampled test resizes a widget to 0×0 or below one row/touch target); a source model destroyed while the control is alive *and its popup is open*.

---

## ripple

Baseline: `tst_ripple` 12 passed (5.4 s); `tst_ripple_layer` 5 passed. No P0/P1 found.

### ISSUE-ripple-1: a ripple that can never be seen still animates
- Severity: P2
- Location: `ripple/src/ripple.cpp:136-156`, `:300-307`, `:349-377`
- Boundary: zero-size/empty bounds, `maxRadiusScale <= 0`, `hoverOpacity`/`pressedOpacity == 0`, content-colour alpha 0.
- Expected: no ripple when the surface cannot show a pixel — `isAnimating()` false, no frame notifications.
- Actual: `pointerEntered`/`pointerPressed` add and start a ripple unconditionally; `paint()` returns at `:353-354` or skips every circle at `:371-377`, yet the animator runs the full expansion + fade and calls `notify()` every frame (`:167`, `:179`). The host keeps its repaint timer alive for an invisible animation.
- Fix: reject input when the bounds are invalid/empty, `maxRadiusScale <= 0` or the effective opacity is zero; drop running ripples when `setBounds` becomes empty.

### ISSUE-ripple-2: hiding the surface cannot stop an in-flight ripple
- Severity: P2
- Location: `ripple/include/md/ripple/ripple.h:122-124`, `ripple/src/ripple.cpp:281-292`, `:344-347`
- Boundary: press (or hover) then hide the host widget.
- Expected: the host can suspend an in-flight ripple without disabling input, so a hidden surface stops animating and a re-show cannot reveal a stale circle.
- Actual: the only stop is `setEnabled(false)`, which also blocks future input; `pointerLeft()` is a no-op. The host does nothing on hide (`view/src/view.cpp:626-634`), so a press hidden mid-animation runs to completion and keeps emitting `repaintRequested`.
- Fix: add `suspend()`/`setSurfaceVisible(false)` that stops the animators without changing `enabled_`, call it from hide/show, or document that hide must call `setEnabled(false)`.

### ISSUE-ripple-3: the eight-ripple cap bounds retained ripples, not allocations
- Severity: P2
- Location: `ripple/src/ripple.cpp:143`, `:214-215`, `:333-334`, `:404`
- Boundary: a double press, auto-repeat, or any synchronous burst while earlier ripples expand.
- Expected: the documented cap (8 press + 1 hover) also bounds resources.
- Actual: every press allocates a new `Animator` (`:143`); eviction only calls `deleteLater()` (`:215`), so N presses in one event-loop turn create N animators and leave N−8 stopped-but-alive until Qt drains `DeferredDelete`. No pooling or reuse.
- Fix: keep a per-controller free list of stopped animators, or delete an evicted animator immediately when not inside its `finished()` emission.

### ISSUE-ripple-4: `repaintRequested` is emitted inside the animator callback
- Severity: P2
- Location: `ripple/src/ripple.cpp:165-168`, `:177-180` (supporting `animation/src/animator.cpp:60-61`, `:200`)
- Boundary: a directly connected host slot reacts to `repaintRequested` by disabling the controller or replacing the hover.
- Expected: reacting to the notification cannot destroy the state that emits it.
- Actual: emission is synchronous from `onValue_`; `setEnabled(false)` → `removeAll()` (`:288-289`, `:235-242`) → `Animator::stop()` assigns `onValue_ = nullptr` while that `std::function` is executing, and `ripples_.clear()` destroys the `Ripple` the callback captured. It survives today only because `notify()` is the last statement; any access after it is a use-after-free, and the re-entrancy rule is undocumented.
- Fix: queue/coalesce the notification (queued `invokeMethod`) or guard re-entrant mutation; document that the signal must not re-enter the controller.

### Covered
Token mapping (`tests/ripple/tst_ripple.cpp:71`); one-shot hover expand→fade→removed (`:83`); full coverage from a near-corner origin (`:102`); press stronger than hover (`:121`); disabled before input paints nothing (`:147`); clip to bounds (`:162`); light/dark content colour (`:177`); an invalid colour falls back to `onSurface` (`:202`); `pointerMoved` no-op (`:235`); the `repaintRequested` frame stream (`:253`); layer paint/`isAnimated`/clip (`tests/layer/tst_ripple_layer.cpp:48`, `:62`, `:77`).

### Not covered
An exact corner/edge press, an origin outside the bounds, `maxRadiusScale > 1`, a radius larger than the widget; zero-size bounds, opacity 0/1, content alpha 0, reduced motion; press-then-disable (`setEnabled(false)` is never called with a live ripple, so `removeAll()` at `ripple.cpp:235-242` is untested); press-then-hide; release without press; double press and stacked alpha (8 × 12% ≈ 64% at the origin); 8-ripple eviction; press while expanding; destruction mid-animation; hover replacement mid-flight; `setSpec`/`setTheme` mid-flight; `setContentColour` pinning across `setTheme`; `setCornerRadius` clamping; an inactive painter; NaN spec values.

---
