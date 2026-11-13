# Robustness audit — `t*`, `v*`, `w*`, `z*` components

**Scope.** The 21 translation units under `components/src/` whose names start with
`t`, `v`, `w` or `z`, and the headers that declare their contracts under
`components/include/md/components/`:

| | | |
|---|---|---|
| `tab.cpp` / `tab.h` | `tabbar.cpp` / `tabbar.h` | `table.cpp` / `table.h` |
| `tag.cpp` / `tag.h` | `text.cpp` / `text.h` | `theme_switcher.cpp` / `theme_switcher.h` |
| `time_picker.cpp` / `time_picker.h` | `timeline.cpp` / `timeline.h` | `toolbar.cpp` / `toolbar.h` |
| `tooltip.cpp` / `tooltip.h` | `topography_chart.cpp` / `topography_chart.h` | `transfer.cpp` / `transfer.h` |
| `tree_select.cpp` / `tree_select.h` | `typography.cpp` / `typography.h` | `video.cpp` / `video.h` |
| `virtual_select.cpp` / `virtual_select.h` | `virtual_table.cpp` / `virtual_table.h` | `virtual_tree_select.cpp` / `virtual_tree_select.h` |
| `waterfall_chart.cpp` / `waterfall_chart.h` | `waveform_chart.cpp` / `waveform_chart.h` | `z_transform_chart.cpp` / `z_transform_chart.h` |

**Method.** Read-only static review; no code touched, nothing built or run. Every
in-scope `.cpp` was opened and its public entry points, paint pass, hit tests and
destructor/lifetime handling were traced. `ChartBase`/`View` were consulted only
to confirm call paths that cross out of the reviewed files (not audited here).
Findings are C++ Core Guidelines E.1–E.6, F.6, ES.20/ES.21, I.11, R.1/R.3, C.35,
C.63; CERT ERR50-CPP, ERR51-CPP, MEM50-CPP, ARR30-C, INT32-C; and the Qt
conventions listed in the brief.

**Result.** 0 × P0, 2 × P1, 11 × P2, plus one withdrawn (TZ-07). No memory-safety
defect is on the default path: the two P1s need caller-supplied data or a
throwing callback, and the P2s are contract, lifetime and worst-case-cost issues
(two of which are UB only when an allocation or a re-entrant slot intervenes).

## Summary

| id | file:line | severity | issue | fix |
|---|---|---|---|---|
| TZ-01 | `components/src/z_transform_chart.cpp:671` | **P1** | `paintField()` issues one `fillRect` per grid cell — `(re-1)×(im-1)` draw calls on every paint, including every reveal frame and every mouse-move repaint; a 1024×1024 spectrum is ~1.05 M draw calls per frame → event-loop stall/hang. | Rasterise the field once into a cached `QImage` (bounded, like `TopographyChart::ensureBands`), draw the image clipped to the reveal sweep; cap the accepted grid. |
| TZ-02 | `components/src/video.cpp:1724` | **P1** | The user-supplied `std::function<QImage(qint64)>` frame provider is called with no `try/catch` from the unguarded `QTimer::timeout` slot (`onTick`) — a throw escapes into Qt's event dispatch (ERR50-CPP / E.1) — and from guarded mouse handlers, where the guard catches but the scrub state latches. | Catch around `provider_()`, keep the last frame, log or emit a `frameError` signal; document "provider must not throw". |
| TZ-03 | `components/src/video.cpp:699` | P2 | `setDuration()` shrinks `duration_` and emits `positionChanged` (via `setPositionInternal`) *before* `buffered_` is clamped and *before* `durationChanged`; a slot reading `buffered()` during that signal sees a value larger than the new duration (media signal-ordering violation). | Clamp `buffered_` and emit `durationChanged` before the position correction, or update `buffered_` first. |
| TZ-04 | `components/src/video.cpp:1657` | P2 | `position_ + kTickIntervalMs` is a signed `qint64` addition with no saturation; when `duration_ <= 0` `setPosition()` accepts `INT64_MAX`, so the next tick is signed-overflow UB (INT32-C). | Saturating add, or clamp an unknown-duration position to a documented maximum. |
| TZ-05 | `components/src/waterfall_chart.cpp:272` | P2 | `traceAt()` writes `*traceIndex = -1` on entry, but `waterfall_chart.h:103-108` promises the out parameters are "left untouched" when it returns `false`; a caller that reuses the previous value gets it clobbered. | Move the initialisation after the early-outs, or correct the header contract. |
| TZ-06 | `components/src/topography_chart.cpp:228` | P2 | `setField()` accepts up to 2048×2048 (32 MiB `QVector` allocated synchronously, upper clamp undocumented), and the first paint / `totalContourSegments()` run marching squares over 4.2 M cells for 7–24 levels — seconds of GUI-thread work. | Document and lower the cell cap (e.g. 1 M), cache `totalContourSegments()`, move large-field contouring off the paint path. |
| TZ-08 | `components/src/virtual_select.cpp:449` | P2 | `VirtualSelect` and `VirtualTreeSelect` hold a non-owning `VirtualSource*`/`VirtualTreeSource*` that is a plain interface (not a `QObject`), so destruction cannot be observed; both headers say only "non-owning" — the dangling-pointer failure mode and the `setSource(nullptr)` requirement are undocumented (unlike `virtual_table.h:11-14`). | Document the detach-before-destroy rule in both headers; optionally observe `QObject`-derived sources through `destroyed`. |
| TZ-09 | `components/src/virtual_tree_select.cpp:907` | P2 | `rebuildRows()` eagerly walks *every* root and every expanded child and calls `source_->label()` for each; `setSource()` on a 10^5-root source does 10^5 virtual calls on the GUI thread, defeating the "virtual" contract. | Keep the row list windowed/lazy like `VirtualSelect::ListContent`, or document that `rootCount()` must be small. |
| TZ-10 | `components/src/virtual_select.cpp:1054` | P2 | `applyFilter()` scans and calls `label()` on every item of the source on each `textChanged`; with the documented million-row use case one keystroke blocks the UI for seconds (documented as the one exception, but still a hang path). | Debounce the filter, show a busy state, or run the scan off-thread for large sources. |
| TZ-11 | `components/src/table.cpp:655` | P2 | `rowsInserted`/`rowsRemoved` handlers ignore the `parent` argument, so inserting or removing *child* rows of a hierarchical model shifts or clears the table's current row even though the top-level rows did not move. | Only apply the index shift when `parent` is invalid (top-level change). |
| TZ-12 | `components/src/z_transform_chart.cpp:188` | P2 | `setSpectrum()` silently clears the whole spectrum when either axis is not ascending, and accepts a non-uniform axis without any diagnostic although `sampleAt()`/`nearestIndex()` assume uniform spacing; the caller sees an empty chart and no error. | Emit a rejection signal / `qWarning` and keep the last valid data; validate uniformity or document it. |
| TZ-13 | `components/src/topography_chart.cpp:230` | P2 | `setField()` commits `rows_`/`columns_` before the `QVector` allocation it describes; if that allocation throws, `values_` keeps the old size and the `values_.size() == rows_*columns_` invariant breaks, so a later `values_.at(...)` can run past the end (assert/UB). | Build into a local vector, then commit dimensions and values together. |
| TZ-14 | `components/src/theme_switcher.cpp:95` | P2 | `applyPreset()` emits `presetActivated(index)` before `themeApplied(theme)`, where `theme` is a reference into `presets_`; a slot that calls `addPreset()` reallocates the list and the second emission reads freed memory (MEM50-CPP). | Copy the `Theme`, or emit `themeApplied` before any re-entrant signal. |

TZ-07 (`Table`/`VirtualTable`/`TreeSelect`/`VirtualSelect` paint-time model
throws) was **withdrawn**: `View::paintEvent()` already guards `paintContent()`,
so the premise of that finding was wrong. The evidence is in its section; it is
not counted in the severity totals.

---

## TZ-01 — ZTransformChart: one draw call per grid cell, on every frame (P1)

**Call path.** `md::ZTransformChart::setSpectrum(re, im, values)` (public,
`z_transform_chart.cpp:181`) stores an arbitrary grid. Any paint then runs
`ChartBase::paint` → `paintPlot(..., progress)` (`:586`) → `paintField()`
(`:617`), whose inner loop is:

```cpp
for (int realIndex = 0; realIndex + 1 < realCount; ++realIndex) {
    for (int imaginaryIndex = 0; imaginaryIndex + 1 < imaginaryCount; ++imaginaryIndex) {
        ...
        painter.fillRect(cell.adjusted(-0.5, -0.5, 0.5, 0.5), colourFor(sum / finite));
    }
}
```

(`:671-703`). There is no cap on `realCount`/`imaginaryCount` and no raster
cache: the cost is `(n-1)×(m-1)` `fillRect` calls per invocation. Repaints are
frequent by construction — the reveal animation repaints at ~60 Hz for the
widget's Level-2 motion, and `mouseMoveEvent()` (`:1084-1097`) calls
`requestUpdate()` for every pointer move over the plane. At 512×512 that is
~260 k draw calls per frame; at 1024×1024 ~1.05 M. On the GUI thread this
stalls the event loop for hundreds of milliseconds to seconds per frame — a
hang, not a slowdown.

**Patch sketch.** Bake the field once per (data, geometry, theme, range,
log flag) and blit it, exactly like `TopographyChart::ensureBands()` does for
its bands:

```cpp
void ZTransformChart::ensureFieldRaster(const QRectF& plane)
{
    if (fieldCacheGeneration_ == dataGeneration_ && fieldCacheSize_ == plane.size()
        && !fieldCache_.isNull())
        return;

    const int w = std::clamp(int(std::ceil(plane.width())), 32, 512);
    const int h = std::clamp(int(std::ceil(plane.height())), 32, 512);
    QImage image(w, h, QImage::Format_ARGB32);
    // ... fill one pixel per (u, v) sample as the current colourFor() does ...
    fieldCache_ = std::move(image);
    fieldCacheGeneration_ = dataGeneration_;
    fieldCacheSize_ = plane.size();
}

// paintField():
painter.setClipRect(sweep);           // reveal sweep only
painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
painter.drawImage(plane, fieldCache_);
```

Also reject or downsample oversized grids in `setSpectrum()` (e.g. a documented
`kMaximumGrid = 2048`), so the rasterisation cost itself is bounded.

## TZ-02 — Video: frame-provider exceptions cross the Qt event boundary (P1)

**Call path.** `Video::setFrameProvider(std::function<QImage(qint64)>)`
(`video.cpp:677`) accepts any callable; the header (`video.h:93-97`) documents
the call convention but not an exception contract. `pullFrame()` invokes it
directly:

```cpp
void Video::pullFrame()
{
    if (!provider_)
        return;
    const QImage next = provider_(position_);   // :1724 — no guard
    ...
}
```

`pullFrame()` is reached from four places: `onTick()` (the `QTimer::timeout`
slot, `:1674`), `mouseReleaseEvent()` while scrubbing (`:1831`), `applyState()`
(`:1691`) and `setPositionInternal()` (`:1714`). The mouse path runs inside
`View::event()`'s guard (`view/src/view.cpp:669-695`), so a throw there is caught
and the handler is aborted. The `onTick()` path is **not** inside any guard: a
`QTimer` timeout is delivered directly to the slot by the event loop, outside
`View::event()`/`View::paintEvent()`, so an exception escapes into Qt's event
dispatch — undefined, and fatal on the platforms that terminate (ERR50-CPP, E.1).
A decoder that throws on corrupt media — the normal way to report a decode
error — therefore kills the application on the next 16 ms tick. On the guarded
scrub-release path the abort still leaves `scrubbing_ == true` and `scrubResume_`
uncleared, because the throw happens at `setPosition()` (`:1831`) before
`:1832-1835` restores them; playback then stays suspended and `isScrubbing()`
reports true with no button held (the latch the parallel audit found at
`avatar.cpp:544`).

**Patch sketch.**

```cpp
void Video::pullFrame()
{
    if (!provider_)
        return;

    QImage next;
    try {
        next = provider_(position_);
    } catch (const std::exception& e) {
        qWarning("md::Video frame provider failed: %s", e.what());
        return;                       // keep the last frame; placeholder not forced
    } catch (...) {
        qWarning("md::Video frame provider failed with an unknown exception");
        return;
    }

    if (next.isNull())
        return;
    frame_ = next;
    emit frameChanged();
    requestUpdate();
}
```

Optionally add `void frameFailed(QString)` so the host can surface the error,
and state in `video.h` that a provider may not throw.

## TZ-03 — Video: `positionChanged` is emitted before the new duration is coherent (P2)

**Call path.** `Video::setDuration(qint64)` (`video.cpp:692-712`):

```cpp
duration_ = next;                             // :698
if (position_ > duration_)
    setPositionInternal(duration_, true);     // :700 -> emits positionChanged
if (duration_ <= 0) buffered_ = 0;            // :705
else buffered_ = std::min(...);               // :708
emit durationChanged(duration_);              // :710
```

A listener of `positionChanged` that queries `buffered()` sees the *old*
buffer value, which can exceed the new `duration_`; the `durationChanged`
notification it would use to resynchronise has not been delivered yet. This is
the media-player signal-order invariant the brief calls out (state published
before the signal that describes it).

**Patch sketch.** Make the duration change atomic before any signal:

```cpp
duration_ = next;
buffered_ = duration_ <= 0
    ? 0
    : std::min(buffered_ > 0 ? buffered_ : duration_, duration_);
emit durationChanged(duration_);
if (position_ > duration_)
    setPositionInternal(duration_, true);
requestUpdate();
```

## TZ-04 — Video: signed overflow of `position_` when the duration is unknown (P2)

**Call path.** With the default `duration_ == 0`, `Video::setPosition(qint64)`
(`:735`) goes through `clampPosition()` (`:1472-1479`), which returns
`std::max<qint64>(0, ms)` — no upper bound. `setPosition(std::numeric_limits<qint64>::max())`
followed by `play()` makes `onTick()` evaluate `position_ + kTickIntervalMs`
(`:1657`), a signed-overflow UB (INT32-C / ERR50-CPP). Even before the
overflow, `formatTime()` and the provider then receive an absurd position.

**Patch sketch.**

```cpp
qint64 Video::clampPosition(qint64 ms) const
{
    constexpr qint64 kMaximumUnknownDurationPosition = 100LL * 3600 * 1000; // 100 h
    const qint64 low = std::max<qint64>(0, ms);
    const qint64 high = duration_ > 0 ? duration_ : kMaximumUnknownDurationPosition;
    return std::min(low, high);
}
```
and/or a saturating add in `onTick()`.

## TZ-05 — WaterfallChart: `traceAt()` contradicts its documented out-parameter contract (P2)

**Call path.** `waterfall_chart.h:103-108` documents: "Returns false outside the
plot or when no painted spectrum is within the hit slop; the out parameters are
left untouched then." The implementation (`waterfall_chart.cpp:269-276`) starts
with:

```cpp
if (traceIndex != nullptr)
    *traceIndex = -1;          // written before any early return
```

so after a miss the caller's previous value is replaced by `-1`. Any host that
keeps a "last hit" index and tests only the boolean will silently lose it.

**Patch sketch.** Either initialise on the success path only (move the write to
just before `return true`), or change the header to document the `-1`
sentinel — the first matches the current documentation and is a two-line move.

## TZ-06 — TopographyChart: 32 MiB-from-input allocation and a multi-second contour pass (P2)

**Call path.** `setField(rows, columns, values)` (`topography_chart.cpp:228-247`)
clamps each dimension to 2048 and then does
`QVector<qreal> normalized(expected, NaN)` — up to 4 194 304 doubles, ~32 MiB,
allocated synchronously on the GUI thread (and `std::bad_alloc` would propagate
out of the public setter). The upper clamp is not mentioned in
`topography_chart.h:58-63`, which only says negative dimensions become zero.

The first paint then calls `ensureContours()` (`:982`) → `buildPolylines()` for
every contour level: `(rows-1)×(columns-1)` cells × 7 levels by default (up to
24 via `setLevels()`), each cell running four `samplePoint()` divisions and four
edge interpolations — on the order of 29 M–100 M cell evaluations, i.e. seconds
of blocked input on the first frame after `setField()`. The public
`totalContourSegments()` (`:755-761`) re-runs the same work for every level and
is not cached.

**Patch sketch.**
- Cap the lattice in `setField()` at a documented cell budget, e.g.
  `constexpr qsizetype kMaximumCells = 1 << 20;` and clear (or clamp) beyond
  it; add the rule to the header.
- Cache `totalContourSegments()` in the existing geometry cache (it already has
  `geometryGeneration_`/`contourCacheGeneration_`).
- For larger fields, compute `ensureContours()` off the paint path (worker or
  `QtConcurrent`) and paint a "computing" state meanwhile.

## TZ-07 — withdrawn after the framework guard check (was P2)

**Verdict: not a defect.** The original finding said that the model-backed views
call `QAbstractItemModel`/source virtuals inside `paintEvent` with no guard,
unlike the charts, so a throwing model would unwind into Qt. That is wrong for
this repository: all four classes derive from `md::View`, and `View` already
owns the exception boundary.

**Evidence.**

- `view/include/md/view/view.h:74-77`: "The entry points that run component code
  — paintEvent(), measure(), arrange() and event() — go through the error guard
  (md/view/error_guard.h): a hook that throws is caught, reported once per entry
  point and skipped instead of unwinding into Qt."
- `view/src/view.cpp:429-491`: `View::paintEvent()` runs the whole pipeline
  (shadow, background, `paintContent(painter, content)` at `:466`, children,
  overlays) inside `view::guard("View::paintEvent", kPaintSite, guardReport_, …)`.
- `view/include/md/view/error_guard.h:79-114`: both `guard()` overloads are
  `noexcept`, catch `std::exception` and `...`, log once per site, and store the
  diagnostic in `GuardReport::lastError` (exposed as `View::lastError()`).
- `view/src/view.cpp:669-695`: `View::event()` wraps `QWidget::event(event)` —
  and therefore the mouse/key/leave handlers — in the same guard (`kEventSite`).

So a throw from `model_->data()`, `source_->cell()` or `source_->label()` during
`paintContent()` is caught by the paint guard: the frame is abandoned after the
background, the widget stays paintable, and the next update paints again. A
throw from a mouse/key handler is caught by the event guard. The local
`try/catch` blocks in the charts (`z_transform_chart.cpp:610` etc.) are a second,
redundant layer; they catch before the guard sees the exception, which means
their failures are logged with `qWarning` but are not recorded in
`guardReport_.lastError` — an observability difference, not a robustness defect.

**Residual note (not a finding).** The guard's contract is "catch, report once,
skip the rest of the entry point". A model that throws on every paint therefore
produces a half-painted frame each time and only one log line per site; that is
the documented framework behaviour, and the component headers do not need a
"must not throw" clause. Had the guard not covered `paintEvent()`, `Table` and
`VirtualTable` would have been the worst of the four: they call
`index()/data()`/`cell()` once per painted cell (hundreds of virtual calls per
frame, more under `JitteredNx` antialiasing), so a throwing model would have
been both the most likely and the most frequent failure.

## TZ-08 — Virtual selects: non-owning source with no destruction observation (P2)

**Call path.** `VirtualSelect::setSource(structure::VirtualSource*)`
(`virtual_select.cpp:449-464`) and
`VirtualTreeSelect::setSource(structure::VirtualTreeSource*)`
(`virtual_tree_select.cpp:382-396`) store a raw pointer. Both interfaces are
plain C++ classes with a virtual destructor but no `QObject` base
(`structure/include/md/structure/virtual_source.h:20-30`, `:52-71`), so the
widget cannot hook `destroyed`. The header for `VirtualSelect` says only
"Non-owning" (`virtual_select.h:51`); `virtual_table.h:11-14` documents the
required `setSource(nullptr)`-before-destruction rule, but the two select
headers do not. If the host drops the source first, the next paint/reveal calls
`source_->count()`/`label()` through a dangling pointer — a use-after-free crash
(MEM50-CPP).

**Patch sketch.** Minimum: copy the `VirtualTable` paragraph into both headers.
Better: give the interfaces an optional observer hook
(`virtual void addDestructionListener(std::function<void()>)`), or accept
`std::shared_ptr<const VirtualSource>` together with a weak handle, and clear
`source_` from the destructor callback.

## TZ-09 — VirtualTreeSelect: eager label materialisation on `setSource` (P2)

**Call path.** `rebuildRows()` (`virtual_tree_select.cpp:907-916`) iterates
`source_->rootCount()` and calls `appendNode()` for every root; `appendNode()`
(`:918-933`) appends a `Row` containing `source_->label(node)` for each node and
recurses into every expanded child. `setSource()` (`:393`) and `setExpanded()`
(`:459`) call it synchronously. A source with 10^5 roots performs 10^5 virtual
`label()` calls (and allocations) on the GUI thread before the widget can paint
— the opposite of the windowing that `VirtualSelect` implements.

**Patch sketch.** Keep the roots/children counts and materialise labels only for
the painted window (mirror `VirtualSelect::ListContent`: a first-row plus a page
count, with `rowLabel()` calling the source on demand and caching per session),
or document a hard limit on `rootCount()` and reject larger sources with a
warning.

## TZ-10 — VirtualSelect: full-source filter scan per keystroke (P2)

**Call path.** `QLineEdit::textChanged` (`virtual_select.cpp:411-420`) →
`applyFilter()` (`:1054-1075`), which clears `filtered_`, `reserve(count)` and
calls `source_->label(index)` for every item, case-insensitively. The class
header explicitly targets "a million rows" (`virtual_select.h:9-10`) and then
notes that filtering is the one path that must read everything (`:17-18`). With
such a source each typed character blocks the GUI thread for the full scan
(seconds), and a `reserve(count)` on a huge count can throw `std::bad_alloc`
through the signal emission as well.

**Patch sketch.** Debounce with a single-shot `QTimer` (~150 ms) so typing does
not scan per character; cap `filtered_.reserve()` with a sane upper bound; for
`count()` above a threshold, run the scan on a worker and publish the result, or
show a "filtering…" row while it runs.

## TZ-11 — Table: child-row inserts/removes shift the current cell (P2)

**Call path.** `Table` displays top-level rows only (`rowCount()` without a
parent), but its change handlers ignore the `parent` argument:

```cpp
connect(model_, &QAbstractItemModel::rowsInserted, this,
        [this](const QModelIndex&, int first, int last) {
            const int count = last - first + 1;
            if (currentRow_ >= first)
                currentRow_ += count;         // table.cpp:658-659
            ...
        });
```

(`:655-665`; the `rowsRemoved` handler at `:666-689` has the same shape and may
clear `currentRow_`/`currentColumn_` when a child row in that index range
disappears). For a hierarchical `QAbstractItemModel` — legal input for a class
documented as "backed by any QAbstractItemModel" (`table.h:1-20`) — inserting a
child under row 0 shifts the table's current row, and removing a child clears
the selection although no top-level row moved.

**Patch sketch.**

```cpp
connect(model_, &QAbstractItemModel::rowsInserted, this,
        [this](const QModelIndex& parent, int first, int last) {
            if (parent.isValid())
                return;                   // a child insert does not move our rows
            ...
        });
```
with the same guard in `rowsRemoved` (and, symmetrically, nothing to change for
the flat column signals).

## TZ-12 — ZTransformChart: silent rejection of a malformed spectrum (P2)

**Call path.** `setSpectrum()` (`z_transform_chart.cpp:181-219`) checks
`std::is_sorted` on both axes and, on failure, clears `re_`, `im_` and `values_`
(`:188-192`) with no signal, log or return value. The header
(`z_transform_chart.h:90-94`) states the axes "are ascending and uniformly
spaced" but the implementation never checks uniformity — a uniformly
*sorted-but-not-uniform* grid is accepted, while `sampleAt()`/`nearestIndex()`
linearly interpolate the index and the colour field assumes cell corners, so
the hover readout and marker positions are silently wrong for such data. Either
way the caller gets no diagnostic (ERR50-CPP-adjacent swallowed failure; E.1).

**Patch sketch.** Add `void spectrumRejected(const QString& reason)` (or at
least `qWarning`), keep the previous valid spectrum instead of clearing it, and
validate uniformity (constant `(back-front)/(n-1)` within an epsilon) before
accepting the grid.

## TZ-13 — TopographyChart: dimensions committed before the allocation (P2)

**Call path.** `TopographyChart::setField(int rows, int columns, QVector<qreal>)`
(`topography_chart.cpp:228-247`) assigns the new dimensions and only then
allocates the normalised buffer:

```cpp
rows_ = std::clamp(rows, 0, 2048);                     // :230
columns_ = std::clamp(columns, 0, 2048);               // :231
...
const qsizetype expected = static_cast<qsizetype>(rows_) * columns_;
QVector<qreal> normalized(expected, emptyValue());     // :239 — may throw
...
values_ = std::move(normalized);                       // :246 — not reached on throw
```

The class relies on `values_.size() == rows_ * columns_`: `value()` compares
`row * columns_ + column` with `values_.size()` (`:277`), `sampleNormalised()`
indexes `values_.at(row0 * columns_ + column0)` (`:463-466`) and
`buildSegments()` samples `values_.at(row * columns_ + column)` (`:538-539`).
If the `QVector` allocation throws `std::bad_alloc` — 2048×2048 is a 32 MiB
request, exactly the size this setter allows — `rows_`/`columns_` describe the
new lattice while `values_` still holds the old one. The invariant is broken,
and the next paint or hover indexes past the end (`QList::at` asserts in debug;
release is UB). The exception propagates out of the public setter, so the caller
sees the failure, but the object is left inconsistent.

**Patch sketch.** Stage the new state and commit it atomically:

```cpp
const int newRows = std::clamp(rows, 0, 2048);
const int newColumns = std::clamp(columns, 0, 2048);
QVector<qreal> normalized;
if (newRows > 0 && newColumns > 0) {
    normalized = QVector<qreal>(qsizetype(newRows) * newColumns, emptyValue());
    // fill from the `values` argument exactly as today
}
values_ = std::move(normalized);   // commit together, after the fallible step
rows_ = newRows;
columns_ = newColumns;
```

## TZ-14 — ThemeSwitcher: `presetActivated` can reallocate the theme it is about to pass (P2)

**Call path.** `ThemeSwitcher::applyPreset(int)` (`theme_switcher.cpp:83-97`):

```cpp
current_ = index;
const Theme& theme = presets_.at(index).theme;   // :89 — reference into presets_
Theme::setApplicationTheme(theme);
...
Q_EMIT presetActivated(index);                   // :95 — re-entrant
Q_EMIT themeApplied(theme);                      // :96 — theme may now dangle
```

`themeApplied` takes `const Theme&` (`theme_switcher.h:77`), and `addPreset()`
(`:61-68`) appends to `presets_`, a `QList<Preset>`; the append can reallocate
the list and invalidate the reference. A host that listens to `presetActivated`
to persist or append a custom preset therefore turns the second emission into a
use-after-free read (MEM50-CPP). `applyPreset()` is public, so the slot can also
call it directly, with the same result.

**Patch sketch.**

```cpp
const Theme theme = presets_.at(index).theme;   // value, not reference
Theme::setApplicationTheme(theme);
...
Q_EMIT themeApplied(theme);
Q_EMIT presetActivated(index);                  // observers learn the cause last
```

---

## Cross-component patterns

The same defect shapes recur across the component set. Each entry names the
in-scope instances; the fix shape is shared, so one review rule or one helper
covers all of them.

### (a) A caller-supplied `std::function`/callback invoked with no exception guard

Instances:

- `video.cpp:1724` — `provider_(position_)` in `pullFrame()` (TZ-02). This is the
  one in-scope instance that reaches Qt's event loop **unguarded**, because the
  16 ms `QTimer` slot is outside `View::event()`. The mouse call sites are inside
  that guard and instead latch `scrubbing_`/`scrubResume_`.
- Parallel audit: `avatar.cpp:544` (same shape, reported separately).

Checked and *not* an instance: the model/source virtuals in `Table`,
`VirtualTable`, `TreeSelect` and `VirtualSelect` run inside
`View::paintEvent()`'s guard (`view/src/view.cpp:429-491`), which is the
framework's documented catch-and-skip contract (see the withdrawn TZ-07).

Shared fix: every user callback invoked from a non-`View` entry point (timer,
queued lambda, `std::function` provider) is wrapped at the call site, or the
callback contract says `noexcept` and a test enforces it. `md::view::guard` is
usable from any of these sites — the timer slot can call
`view::guard("Video::onTick", …)` around the tick body — so the fix is local.

### (b) An unbounded per-element loop inside `paintEvent` or a GUI-thread setter

Instances:

- `z_transform_chart.cpp:671` — `(re-1)×(im-1)` `fillRect` calls per paint
  (TZ-01); re-runs per reveal frame and per mouse move.
- `topography_chart.cpp:982` / `:755` — 4.2 M-cell marching squares per contour
  level on first paint / per `totalContourSegments()` call (TZ-06).
- `virtual_tree_select.cpp:907` — one `label()` per root on `setSource()`
  (TZ-09).
- `virtual_select.cpp:1054` — one `label()` per item on every filter keystroke
  (TZ-10).
- Same shape, accepted: `waterfall_chart.cpp:333-343` — `hasLevels()` scans every
  bin once per `setTraces()`, i.e. proportional to the data the caller just
  handed over, not per frame.

Shared fix: bound the unit of work — cache a raster
(`TopographyChart::ensureBands` is the in-tree pattern), cap the accepted input
(a documented `kMaximumCells`), window the data (`VirtualSelect::ListContent`),
or debounce and move the scan off the GUI thread.

### (c) State committed before the allocation or computation it describes

Instances:

- `topography_chart.cpp:230` — `rows_`/`columns_` before the `values_`
  allocation (TZ-13).

Benign near-instances (checked, no defect): `TopographyChart::ensureBands()`
sets `bandCacheArea_` before building the image (`:786`), but validity also
requires the generation tag and a non-null image, so a failed build is never
treated as current. `VirtualTreeSelect::rebuildRows()` clears `rows_` before the
rebuild (`:909`); a throwing `label()` leaves a partial list, but every accessor
range-checks, so it cannot index out of bounds.

Shared fix: stage in locals and commit members together after the fallible step;
where partial state is unavoidable, make every accessor check (as these files
mostly do).

### (d) A state mutation that emits a signal before the accompanying fields are consistent

Instances:

- `video.cpp:699` — `setDuration()` emits `positionChanged` before `buffered_`
  is clamped and before `durationChanged` (TZ-03).
- `theme_switcher.cpp:95` — `presetActivated` is emitted while
  `themeApplied(theme)` still holds a reference into a mutable container
  (TZ-14).

Shared fix: normalise every derived field first, then emit in dependency order
(duration/buffer before position; theme before cause); pass by value when a slot
can re-enter the same object.

### (e) An out-parameter written when the header promises it untouched

Instance:

- `waterfall_chart.cpp:272` — `traceAt()` writes `*traceIndex = -1` before its
  early-outs, contradicting `waterfall_chart.h:103-108` (TZ-05).

Shared fix: write out-parameters only at the success point, or correct the
header; add a test that a miss leaves the caller's previous values intact.

---

## Gap closure

The five files previously covered only by a targeted read were read end-to-end at
the same standard as the rest of the scope. One new finding came out of it
(TZ-14); the earlier conclusions about `virtual_tree_select.cpp` were confirmed
rather than revised.

| file | result |
|---|---|
| `typography.cpp` | No finding. Same `QTextLayout` algorithm as `text.cpp`, including the 4096-line cap (`:122-124`), the `max(1 px)` paint width and the empty-text early-outs; measure and paint share `resolvedStyle()`/`naturalWidth()`. |
| `timeline.cpp` | No finding. `itemRect`/`markerRect`/`timeRect`/`textRect` all range-check the index; scrolling is clamped through `maximumScrollOffset()`; the paint loop reads `items_.at(index)` only for indices it just bounds-checked; no user callbacks run during paint. |
| `tooltip.cpp` | No finding. `applyGeometry()` dereferences `anchor_`/`host_`, and the invariant `open_ ⇒ anchor_ != nullptr ∧ host_ != nullptr` is preserved by every mutator: `setAnchor()` hides before changing the anchor, the anchor's `Destroy` filter calls `hide()` (which clears `open_`) before nulling `anchor_`, and `show()` checks both before setting `open_ = true`. The delay timer is a child object connected with `this` context. |
| `theme_switcher.cpp` | **TZ-14** (P2). Everything else checks out: modal lifetime (`QPointer`, `deleteLater`, `destroyed` cleanup, destructor `disconnect`), `presetRect`/`swatchRect` bounds, and the empty-preset guards. |
| `virtual_tree_select.cpp` | No new finding. Confirms TZ-08 (the header says "borrowed" but still states no detach-before-destroy rule) and TZ-09 (eager `label()` per root in `rebuildRows`). Also confirms that — unlike `VirtualSelect` — its menu paints cached labels (`rowLabel()` reads `rows_`, `:493-499`), so the paint path performs no source calls. |

---

## Checked and found sound

Coverage: every in-scope `.cpp` was read end-to-end (**F**). The headers that
declare the reviewed contracts were read in full; the rest were scanned for
`noexcept`, ownership and signal-shape declarations. `TZ-07` is withdrawn and is
not counted as a finding (evidence in its section).

| file | coverage | what was verified |
|---|---|---|
| `tab.cpp`, `tab.h` | F | `TabLayout::layout()` returns before the Fixed-mode division when `count == 0` or the viewport is empty; `contentWidth()` returns 0 for an empty list; no allocation driven by caller data. |
| `tabbar.cpp`, `tabbar.h` | F | `indicatorTimer_` is a value member connected with `this` as context, so it dies with the widget and is stopped by `snapIndicator()`, `setIndicatorAnimationEnabled(false)`, resize and theme change; the metrics cache is invalidated by every `tabs_` mutation, so `tabs_.at(index)`/`metrics.at(index)` stay in step; scroll offset clamped against `maximumScrollOffset()`. |
| `table.cpp`, `table.h` | F | Row/column counts are re-read per paint and every index is bounds-checked (`cellRect`, `rowRect`, `columnAtPosition`); `model_` is cleared from `QObject::destroyed`; scrollbar thumb/travel math guards zero viewports. Defect: TZ-11. |
| `tag.cpp`, `tag.h` | F | Paint and hit test share `layoutFor()`/`closeRectFor()`; a release only fires when the press and release agree; no container indexing from caller data; `closeClicked` is the last thing before return. |
| `text.cpp`, `text.h` | F | `QTextLayout` pass capped at 4096 lines for zero/one-pixel widths; natural width splits hard line breaks; paint width clamped to ≥ 1 px; empty text and empty content return early. |
| `theme_switcher.cpp`, `theme_switcher.h` | F | Modal lifetime: `QPointer` self capture, `deleteLater` on `closeFinished`, `destroyed` clears `selector_`; a second `openSelector()` closes the existing surface; keyboard path bounds-checks `presets_`. Defect: TZ-14. |
| `time_picker.cpp`, `time_picker.h` | F | `animator_` is a child `QTimer` connected with context `this`, stopped by `refreshColumns()`, `finishAnimation()` and `onHidden()`; every `% count` is guarded (all fields have ≥ 2 values) and `normalized()` returns 0 for `count <= 0`; wheel/Home/End clamp through `stepColumn`/`setCurrentColumn`. |
| `timeline.cpp`, `timeline.h` | F | Every rect helper (`itemRect`, `markerRect`, `timeRect`, `textRect`) range-checks the index; paint skips rows outside the content area; scroll is clamped; no user callbacks run during paint. No finding. |
| `toolbar.cpp`, `toolbar.h` | F | `actions()` filters null children; `actionViews.size() - 1` is only evaluated when `size() > 1`; every child is measured with the actual content size before arranging. |
| `tooltip.cpp`, `tooltip.h` | F | `delayTimer_` is a child `QTimer` connected with context `this`; `anchor_` is a `QPointer` cleared on `Destroy`; `applyGeometry()`'s `open_ ⇒ anchor_/host_` invariant is preserved by every mutator; `showText()` reuses the existing direct child; press/leave/wheel hide the surface. No finding. |
| `topography_chart.cpp`, `topography_chart.h` | F | Every accessor takes `values_.size() == rows_*columns_` as its invariant and bounds-checks against `values_.size()`; the invariant itself can be broken by a failed allocation in `setField()` (TZ-13); the band raster is capped at 480×480 and cached by generation + field size; theme change invalidates the raster; contour/cache lifetimes are keyed by `geometryGeneration_`. Defects: TZ-06, TZ-13. |
| `transfer.cpp`, `transfer.h` | F | Selections are index sets re-validated against `itemsFor(pane).size()` before every move; `moveSelected()` removes descending so indices stay valid; both panes clamp scroll after every mutation; no child-view indexing from caller data. |
| `tree_select.cpp`, `tree_select.h` | F | Selection is a `QPersistentModelIndex`; the `Adapter` is parented and its signals are connected with `this` as context; all `rows_` accessors range-check; popup/dropdown destruction is handled in the destructor and via `destroyed`; paint-time model throws are caught by `View::paintEvent()`'s guard (TZ-07 withdrawal). No finding. |
| `typography.cpp`, `typography.h` | F | Same `QTextLayout` algorithm as `text.cpp`; 4096-line cap, paint width clamped ≥ 1 px, empty-text early-outs; measure and paint share `resolvedStyle()`/`naturalWidth()`. No finding. |
| `video.cpp`, `video.h` | F | `ticker_`/`fadeTimer_` are child `QTimer`s connected with context `this` and stopped by `onHidden()`/`applyState()`; frame/`QImage` null checks before every draw; aspect math guards empty image sizes; seek is refused when `duration_ <= 0`; `paintGlyph()` tolerates a null paint device and an empty path. Defects: TZ-02, TZ-03, TZ-04. |
| `virtual_select.cpp`, `virtual_select.h` | F | Popup/dropdown teardown in `~VirtualSelect()` is ordered (flag first, content detached, then surface deleted) and the `destroyed` hook clears both pointers; label cache is cleared per session; `sourceIndexAt()`/`positionOfSourceIndex()` bounds-check; paint-time source throws are caught by `View::paintEvent()`'s guard. Defects: TZ-08, TZ-10. |
| `virtual_table.cpp`, `virtual_table.h` | F | The paint loops are bounded by the counts cached at the last `refresh()`, so `source_->cell()`/`header()` never receive an index outside that snapshot; the frozen-band paint splits the clip so hidden scrolling cells are never queried; thumb math guards zero tracks; paint-time source throws are caught by `View::paintEvent()`'s guard. No finding. |
| `virtual_tree_select.cpp`, `virtual_tree_select.h` | F | Row list accessors range-check; expansion keys are per-session and cleared on source change; popup destruction handling matches `VirtualSelect`; the paint path reads cached labels (no source calls). Defects: TZ-08, TZ-09. |
| `waterfall_chart.cpp`, `waterfall_chart.h` | F | `span > 0` guards every level→pixel mapping; `bins == 1` avoids the `bins-1` division; empty/NaN traces skip without closing the path; `plot.isEmpty()` and finite-position checks before all hit tests; paint wrapped in `catch`. Defect: TZ-05. |
| `waveform_chart.cpp`, `waveform_chart.h` | F | `duration()`/`sampleRate()` guarded against zero; `count-1` only used when `count >= 2`; `stride`/`columns` never zero; envelope and RMS paths clamp their widths and skip non-finite samples; paint wrapped in `catch`. No finding. |
| `z_transform_chart.cpp`, `z_transform_chart.h` | F | `sampleMagnitude()`/`sampleAt()` bounds-check the flat index; `nearestIndex()` rejects short/non-finite axes; `domainExtent()` guards zero; paint, measure and mouse handlers are wrapped in `catch`. Defects: TZ-01, TZ-12. |

## Cross-cutting observations (not findings)

- **Timers.** All four timer owners in scope (`Video`, `TabBar`, `TimePicker`,
  `Tooltip`) use either a child `QTimer` or a value member; none can fire after
  the widget is destroyed, and every `connect` passes `this` as the context
  object. No lambda in scope is connected without a receiver context.
- **Painting outside `rect()`.** Every component that draws a label, badge or
  hover box clamps it to its plot/content rectangle first. `ChartBase` also
  applies a 16384 px raster limit and a direct-painter fallback, which the
  reviewed charts inherit.
- **Empty images.** `Video::paintContent()` and
  `TopographyChart::ensureBands()/paintPlot()` both check `isNull()`/`isNull`
  and the image size before using a raster; no `QPixmap`/`QImage` is painted
  while null.
- **`QMediaPlayer`.** The video component is deliberately decoder-agnostic (it
  owns no `QMediaPlayer`/`QVideoSink`); the ordering finding TZ-03 is the
  equivalent state/signal invariant for its own transport.
- **Header `noexcept`.** No in-scope getter marked `noexcept` allocates,
  throws or mutates; the marker is used only for scalar/pointer returns.

## Limitations

- Static review only: no build and no test run, so a defect that is only
  observable through a particular platform theme, device-pixel ratio or Qt
  version cannot be excluded.
- Several reachability arguments rely on `ChartBase`'s paint dispatch
  (`components/src/chart_common.cpp`) and on the `View` error boundary
  (`view/src/view.cpp`, `view/include/md/view/error_guard.h`), both outside the
  audited file set; they were consulted only to confirm call paths, the
  once-per-reveal-frame paint behaviour and the paint/event guards.
- Every in-scope `.cpp` was read end-to-end. The header sections that state a
  reviewed contract were read in full (`waterfall_chart.h`, `video.h`,
  `table.h`, `virtual_select.h`, `virtual_table.h`, `virtual_tree_select.h`,
  `topography_chart.h`, `z_transform_chart.h`, `waveform_chart.h`); the remaining
  headers were scanned for `noexcept`, ownership and signal declarations rather
  than reviewed line by line.
