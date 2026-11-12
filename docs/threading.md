# Threading and frame scheduling

Periodicity makes the two UI-side threads explicit:

- **UI thread** — owns widgets, layout, painting and every `EventTarget`.
  `md::schedule::UiThread` turns its deferred work into bounded frames.
- **Event thread** — owns input decoding and coalescing. It never touches a
  widget; it carries opaque target tokens and hands one ordered batch to the UI
  thread at the next frame boundary (`md::schedule::EventThread`).
- **Worker pool** — owns CPU work that must not run on either UI thread
  (`md::pool::ThreadPool`, `md::pool::IoThread`). Results come back through a
  queue, never by touching a widget directly.

The rule behind all three: a thread owns state, a queue is the only way across a
thread boundary, and the UI thread is never blocked.

## Frame phases

`UiThread` runs the phase sequence that retained-UI schedulers converge on:

| Phase | Work |
| --- | --- |
| `Idle` | Resting state between frames; `postIdleTask()` may run here. |
| `PreFrame` | One-shot `addFrameCallback()` callbacks, including the event batch hand-off. This is the frame boundary. |
| `Build` | `Priority::UserInput` and `Priority::UiUpdate` work. |
| `Layout` | `Priority::UiEvent` work, including delivery of an input batch. |
| `Paint` | `addPaintCallback()` hooks; `recordPaint()` feeds the measured paint statistics. |
| `PostFrame` | `Priority::Background` and `Priority::Idle` work. |

`phase()`, `frameNumber()` and `stats()` expose the pipeline. A frame that
overruns its budget still reaches `PostFrame` and `Paint`; it records the
overrun once, starts no further queued work, and schedules the next frame for a
later event-loop turn. `setFrameBudget()` defaults to 8000 µs.

The ordering of the phases follows the `SchedulerPhase` model documented by
[Flutter](https://api.flutter.dev/flutter/scheduler/SchedulerPhase.html):
idle, transient callbacks, build, layout, paint, post-frame.

## Priority and coalescing

`Priority` is the existing five-level ladder from
`md/schedule/task_queue.h`; `UiThread` maps it onto phases rather than draining
one flat queue:

| Priority | Value | Phase | Typical work |
| --- | ---: | --- | --- |
| `UserInput` | 0 | `Build` | press, click, key activation, scroll, ripple start |
| `UiUpdate` | 1 | `Build` | layout and paint invalidation |
| `UiEvent` | 2 | `Layout` | hover, synthetic notifications, animation ticks, input batches |
| `Background` | 3 | `PostFrame` | decoding, prefetching, cache warming |
| `Idle` | 4 | `PostFrame` (and `postIdleTask()` between frames) | cleanup, metrics |

Within a priority, work is FIFO. Across priorities, strict order is preserved,
which is why a press queued during a `UiUpdate` batch runs before the rest of
that batch.

Coalescing is explicit at each boundary:

- `requestFrame()` coalesces a burst into one frame.
- `requestResize(target, size)` keeps only the last size per target.
- `EventThread` keeps only the last pointer position and wheel delta per target,
  while preserving every down, up and key event.
- `TaskQueue` keeps its per-key coalescing for code that still uses it.

Every queued task has a deadline (one second by default).
`postTask()` and `postIdleTask()` drop an expired task and count it in
`droppedTasks()`; stale work is never replayed after a stalled frame.

## Input pipeline

`EventThread::postInput()` is thread-safe and takes a mutex only for one
insertion or replacement. The queue is built from deques and per-target maps,
not one channel per event, so queue depth is bounded by the number of targets,
not by pointer sampling frequency.

At the frame boundary the queue is ordered and handed over as **one**
`Priority::UiEvent` task:

| Order | Input | Coalescing |
| ---: | --- | --- |
| 0 | `PointerDown`, `PointerUp`, `Key` | none — every event is kept |
| 1 | `Wheel` | last delta per target |
| 2 | `PointerMove` | last position per target |
| 3 | `Gesture` | none — runs after the moves it was derived from |

The consumer is `EventThread::setBatchHandler()`, called on the UI thread in
`Layout`. A batch handler is where an application turns input payloads into
framework events and calls `EventDispatcher::dispatch()`.

`EventDispatcher` is deliberately synchronous: it is the delivery stage that
runs on the UI thread, not an input queue. When the event runtime is installed,
an off-UI-thread dispatch is refused rather than allowed to touch a widget, and
the decoded batch reaches the dispatcher through the frame-boundary hand-off.

## Installing the runtime

An application installs both runtimes once, after `QApplication` exists, and
uninstalls them in reverse order at shutdown:

```cpp
md::schedule::UiThread::install();
md::schedule::EventThread::install();
const auto guard = qScopeGuard([] { md::schedule::EventThread::uninstall(); md::schedule::UiThread::uninstall(); });
```

`UiThread::install()` must run before `EventThread::install()`; the event
runtime attaches to the installed frame runtime. `UiThread::instance()` and
`EventThread::instance()` return null until then.

**Fallback.** With no `UiThread` installed, `View::requestUpdate()` keeps the
historical behaviour: it calls `update()` directly and posts the coalesced
`TaskQueue` entry. Headless tests, embedders and code that pumps
`TaskQueue::main()` itself are therefore unaffected. The event runtime simply
has no boundary to flush to until a UI runtime exists.

## Rules

1. **The UI thread owns widgets and painting.** `QWidget`, `View`,
   `EventTarget`, layout and every `QPainter` operation run there. No worker and
   no event-thread callback may dereference a widget.
2. **The event thread owns input decoding, never widgets.** Its payload target
   is an opaque token; it coalesces continuous input and hands the UI thread a
   batch. This mirrors the task-runner guidance used by Chromium: post to the
   right sequence/thread instead of reaching across threads directly
   ([Chromium task-runner guidance](https://github.com/chromium/chromium/blob/ef39f68efcc1029629455c13fac5ab994601add6/ios/chrome/browser/intelligence/kb/general/use-chromium-task-runner-not-grand-central-dispatch.md)).
3. **Every cross-thread hand-off is a queue.** A result from a worker enters the
   UI thread through a queued signal, `TaskQueue`, or `UiThread::postTask()`.
   Never call a widget method from another thread.
4. **Never block the UI thread.** `postTask()` and `postInput()` take one short
   lock and return; waiting, sleeping and file IO belong on the worker or IO
   thread. Qt's performance guidance makes the same point about avoiding
   synchronous work on the GUI thread
   ([Qt performance notes](https://doc.qt.io/qtforpython-6.5/overviews/qtquick-performance.html)).
5. **Batch work per frame.** One frame serves all pending requests; input is one
   batch per boundary; the budget limits how much queued work starts before the
   next turn. This keeps interaction and painting interleaved rather than
   thrashing the event loop.
6. **Idle work yields to frames.** `postIdleTask()` does not run while a frame
   is pending, so cleanup cannot delay a frame.
7. **Deadlines are accounting, not a suggestion.** Expired tasks are dropped and
   counted instead of executed late. A stalled UI recovers by skipping stale
   work, not by replaying it.

## Diagnostics

`UiThread::stats()` reports completed frames, dropped/overrun frames, mean and
maximum paint time, the current phase, and pending work. `droppedTasks()`
reports expired work separately. `EventThread::stats()` and `depth()` report
posted, coalesced, handed-off and delivered input plus the live queue depth.

The gallery's Scheduler card renders both read-outs from a `UiThread` frame
callback (throttled so the read-out does not become the frame source), and the
Scheduler component can still bind a `TaskQueue` for its per-priority lanes.
