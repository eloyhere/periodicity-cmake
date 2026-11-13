# Robustness audit — a\*/b\*/c\* components and `core/`

Read-only audit. No code was modified and nothing was built. Scope was exactly:
31 sources matching `components/src/a*.cpp`, `b*.cpp`, `c*.cpp`; the same-named
`components/include/md/components/*.h`; and `core/include` + `core/src`.
Everything below is derived from reading the source; no runtime evidence exists.

## Headline

| Severity | Count |
|---|---|
| P0 | **0** |
| P1 | **2** |
| P2 | **5** |

No P0 was found. That is a deliberate result, not an omission: every classic
P0 shape on the audit list was traced to a guard (empty-container access,
`size()-1` index, division by a caller-controlled range, NaN reaching
`std::sort` or `QRectF`, index from a model that can shrink, throw crossing a
Qt virtual, use-after-free of a child widget). Nine tenths of the code is
defended by explicit early returns and comparisons written so that NaN fails
them (`!(x > 0.0)` rather than `x <= 0.0`); section
[Verified sound](#checked-and-found-sound) names each guard and its line, so
the absence of a finding there is evidence rather than silence.

The two P1s are both **liveness/state** defects rather than memory errors: an
uncontained user callback in `Avatar`, and a superlinear synchronous
computation in `AutocorrelationChart`.

---

## Findings

| id | file:line | severity | issue | fix |
|---|---|---|---|---|
| P1-1 | `components/src/avatar.cpp:544` | P1 | User-supplied `imageLoader_` is called unguarded; a throw leaves `loading_ == true` forever, emits no `loadFailed`, and escapes `setUrl()`/`setImageLoader()` (E.1, C.30 invariant) | Wrap the call in `try`/`catch`, `qWarning`, `failLoad()` |
| P1-2 | `components/src/autocorrelation_chart.cpp:244` | P1 | `recomputeFromSamples()` is O(N·L) (≈512·N) on the GUI thread from `setSamples()`/`setMaxLag()`; minutes of audio freeze the UI with no cancellation (bounded-loop criterion) | Cap lag count, decimate N, or move to a worker/FFT |
| P2-1 | `components/src/chart_common.cpp:868` | P2 | No `QPixmap::isNull()` check; `staticLayerValid_` is set true for a null pixmap, so a failed allocation re-renders and re-warns every frame instead of once | Check `isNull()`, warn once, leave the flag false |
| P2-2 | `components/src/chart_common.cpp:178` | P2 | `setTitle()`/`setLegendVisible()`/`setAxesVisible()` change `plotRect()` but never invalidate the chart cache; a subclass that follows the documented `updateChartCache()` contract keeps stale pixel geometry | Also clear `chartCacheValid_` in those three setters |
| P2-3 | `core/src/i18n.cpp:119` | P2 | Process-global `translatorInstalled`/`installedLocale` survive destruction and recreation of `QCoreApplication`; `install()` then returns `true` without installing → silent wrong language | Record the installing `QCoreApplication*` and compare, or clear on `aboutToQuit` |
| P2-4 | `components/src/context3d.cpp:100` | P2 | `delete` of the embedded `QOpenGLWidget` runs `makeCurrent()`/`doneCurrent()` during parent teardown; `deleteLater()` is Qt's documented teardown path | `hide()` + `deleteLater()` when an event loop runs |
| P2-5 | `components/src/connectivity_chart.cpp:139`<br>`components/src/chord_diagram.cpp:132` | P2 | `setMatrix()` squares the input with no dimension cap (20 000×1 → 400 M qreal ≈ 3.2 GB) and is not transactional (`size_` assigned after the loop that can throw `bad_alloc`) | Cap the dimension; build locals and swap |

---

### P1-1 — `Avatar` leaves a permanent busy state when the image loader throws

**file:line** `components/src/avatar.cpp:544-552` (setters at `280-318`)

Reachable call path:

```
Avatar::setUrl(QUrl)                       avatar.cpp:278
  → setLoading(true)                       avatar.cpp:297   (loading_ = true)
  → beginLoad()                            avatar.cpp:298, 540
      → if (imageLoader_) imageLoader_(url_);   avatar.cpp:545   <-- user code, unguarded
      → applyLoadedImage() / failLoad()    avatar.cpp:519 / 529
```

and identically from `Avatar::setImageLoader(std::function<...>)`
(`avatar.cpp:306-319` → `beginLoad()` at 317).

The `std::function<QImage(const QUrl&)>` is the only user callback in the
audited scope, and the only path out of `beginLoad()` that can throw. If the
loader throws — a `QImage` from a bad decoder, a filesystem probe on a dead
mount, a test double that asserts — the following happens:

1. `loading_` was already set to `true` by `setLoading(true)` and is never
   reset; `setLoading(false)` only exists on the success (`519-527`) and
   failure (`529-538`) paths.
2. `failLoad()` is not called, so `loadFailed(url_)` is never emitted and a
   host cannot react.
3. `image_`/`icon_` were cleared earlier in `setUrl` (`286-288`, `314-315`),
   so the widget paints its empty/placeholder frame while reporting
   `isLoading() == true` indefinitely — a permanent spinner.
4. The exception propagates out of the public setter into the caller. That is
   legal (neither setter is `noexcept`, and neither is called from a Qt
   virtual), but it leaves the object in a state that violates its own
   invariant "`loading_` is true iff a transfer is in flight".

Rules: C++ Core Guidelines **E.1** (design error handling: a component that
invokes user code must decide who handles its failure, and must leave itself
coherent), **E.3** (RAII/state guard for recovering from a failure), and the
**C.30-ish invariant** that `loading_` mirrors an outstanding load. It is not
an ERR50-CPP/ERR51-CPP violation: no `noexcept` boundary and no Qt virtual is
crossed.

Patch sketch:

```cpp
    if (imageLoader_) {
        QImage loaded;
        try {
            loaded = imageLoader_(url_);
        } catch (const std::exception& error) {
            qWarning("md::Avatar: image loader failed: %s", error.what());
            if (generation == loadGeneration_)
                failLoad();              // clears loading_, emits loadFailed
            return;
        } catch (...) {
            qWarning("md::Avatar: image loader failed with an unknown exception");
            if (generation == loadGeneration_)
                failLoad();
            return;
        }
        if (generation != loadGeneration_)
            return;
        ...
    }
```

A `qScopeGuard` that calls `failLoad()` unless the function reached
`applyLoadedImage()` would also satisfy E.3 if you prefer a single exit path.
The generation check after the callback (`546`) is correct and should stay —
it already makes a re-entrant `setUrl()` from inside the loader safe.

---

### P1-2 — `AutocorrelationChart::setSamples()` can freeze the UI for seconds

**file:line** `components/src/autocorrelation_chart.cpp:231-252` (called from
`setSamples` at `96` and `setMaxLag` at `121`)

Reachable call path:

```
AutocorrelationChart::setSamples(std::vector<double> samples, double rate)
  → recomputeFromSamples()                       autocorrelation_chart.cpp:96, 204
      → limit = maxLag_ > 0 ? min(maxLag_, count-1) : min(count-1, 512)   :231
      → for (lag = 0..limit) for (index = 0..count-lag-1)                 :244
          sum += centred[i] * centred[i+lag];                             :248
```

The inner loop runs `count - lag` times, so the work is
`Σ_{lag≤limit}(count-lag) ≈ count · limit`. With the documented default
(`kAutomaticLagLimit = 512`, `41`) and a realistic audio buffer:

| input | operations | wall time (order of magnitude) |
|---|---|---|
| 1 s @ 44.1 kHz (`44 100`) | ≈ 2.3·10⁷ | a few ms — fine |
| 10 s (`441 000`) | ≈ 2.3·10⁸ | ~0.1–0.3 s |
| 60 s (`2 646 000`) | ≈ 1.4·10⁹ | ~1–3 s |
| 10 min (`26 460 000`) | ≈ 1.4·10¹⁰ | **tens of seconds** |

All of it runs synchronously inside the setter on the GUI thread: no repaint,
no input, no way to cancel, and the window is reported as unresponsive by the
platform. `setMaxLag()` makes it worse — it accepts any `int` and recomputes
(`112-126`), so `setSamples(big); setMaxLag(INT_MAX)` promotes the algorithm
to O(N²) (`limit = count-1`).

`setValues()` is unaffected, so a caller that already has the sequence pays
nothing; the cost is specific to `setSamples()`.

Rules: this is the "loops bounded by a value the caller controls" hazard on
the audit list — `samples.size()` is unbounded and chosen by the caller —
combined with the P1 definition ("a hang"). No memory-safety rule is
implicated; `centred.at()` and `values_.at()` are in range because
`length = count - lag ≥ 1` and the vector was resized to `limit + 1` at `235`.

Patch sketch (cheapest correct change — bound the work and say so):

```cpp
    // Reject or decimate the input instead of freezing the GUI thread.
    constexpr std::size_t kMaximumSamples = 1u << 20;   // ~1 M samples
    const qsizetype count = static_cast<qsizetype>(clean.size());
    ...
    if (count > static_cast<qsizetype>(kMaximumSamples)) {
        qWarning("md::AutocorrelationChart: decimating %lld samples for the "
                 "autocorrelation estimate", static_cast<long long>(count));
        // keep every stride-th sample, stride chosen so the result fits
    }
```

Better, if the feature matters: compute the correlation with an FFT
(`O(N log N)`) on a worker and deliver the result to the GUI thread through a
queued signal, with a generation counter so a newer `setSamples()` cancels an
older run — the same pattern `Avatar` already uses for its async reply
(`avatar.cpp:577-600`, `loadGeneration_`).

---

### P2-1 — a failed static-layer allocation re-renders every frame

**file:line** `components/src/chart_common.cpp:866-907`, key at `829-864`

`rebuildStaticLayer()` assigns `staticLayer_ = QPixmap()` (`868`), returns
early when the widget has no size (`870-871`), then builds
`QPixmap pixmap(pixelSize)` (`877`) and paints into it. `ensureStaticLayer()`
unconditionally sets `staticLayerValid_ = true` afterwards (`862`) and
increments the build counter. There is no `pixmap.isNull()` test, and
`QPainter layerPainter(&pixmap)` (`881`) on a null pixmap fails `begin()`,
leaving every furniture draw as a silent no-op with a Qt warning.

The consequences are self-consistent but noisy: `isStaticLayerCached()`
(`334-337`) requires `!staticLayer_.isNull()`, so it disagrees with
`staticLayerValid_`; the next `ensureStaticLayer()` re-runs its `matches`
test, sees `staticLayer_.isNull()`, and rebuilds again — once per paint, each
time attempting the same failed allocation and emitting a `QPainter::begin`
warning. Under memory pressure this turns one diagnosable failure into
continuous log spam and wasted work.

Rule: E.1 (a failed resource acquisition must be reported once and recorded,
not retried invisibly). Patch sketch:

```cpp
    QPixmap pixmap(pixelSize);
    if (pixmap.isNull()) {
        qWarning("md::ChartBase: static layer allocation failed (%dx%d)",
                 pixelSize.width(), pixelSize.height());
        staticLayerValid_ = false;      // do not pretend it was built
        return;
    }
```

and only set `staticLayerValid_ = true` in `ensureStaticLayer()` when
`!staticLayer_.isNull()`.

---

### P2-2 — layout-changing setters do not invalidate the chart cache

**file:line** `components/src/chart_common.cpp:178-224` vs `344-348`,
`781-783`, `842-845`

`plotRect()` is derived from the title, legend and axes flags as well as the
widget size (`496-598`). `setTitle()`, `setLegendVisible()` and
`setAxesVisible()` clear `layoutValid_` and `staticLayerValid_` but **not**
`chartCacheValid_`. `chartCacheValid_` is cleared only by a theme change
(`1767`), a resize (`815`), a new series (`262`), a transform change (`159`),
`setAntialiasing()` (`1467`) and `invalidateChartCache()` (`344-348`).

`updateChartCache()` is documented as the place where a chart maps data to
pixels — "A chart that maps data to pixels here must call `plotRect()` first"
(`chart_common.h:387-389`) — and `ensureChartCache()` runs it at most once per
flag (`818-827`). So a subclass that follows that contract and then has
`setTitle()` or `setLegendVisible()` called keeps geometry computed for the
*previous* plot rectangle until some unrelated event invalidates it: bars,
stems or label positions drawn against the old gutters (P1-grade wrong output
for that subclass).

Honest scope note: **no in-repo chart currently overrides `updateChartCache()`**
— a search of `components/src` finds only the empty base definition at `781`
— so this is latent today and reachable only by a consumer subclass. That is
why it is graded P2 rather than P1, and why it is reported at all: the base
class's documented contract is the thing that is wrong.

Patch sketch — make the layout setters invalidate the dependent cache, which
also covers a future change to the tick/fitted-bounds keys:

```cpp
void ChartBase::setTitle(const QString& text)
{
    if (title_ == text)
        return;
    title_ = text;
    layoutValid_ = false;
    staticLayerValid_ = false;
    chartCacheValid_ = false;     // plotRect() moved
    requestUpdate();
}
```

(the same line in `setLegendVisible()` and `setAxesVisible()`), or centralise
it in `rebuildLayout()`'s invalidators.

---

### P2-3 — `md::i18n` keeps stale state across a recreated `QCoreApplication`

**file:line** `core/src/i18n.cpp:119-120`, `142-149`, `152-159`

`translatorInstalled` (`60`) and `installedLocale` (`35`) are namespace-scope
and are not tied to the `QCoreApplication` they describe. The supported
pattern of destroying and recreating the application object (test harnesses
that build a fresh `QApplication` per case, plugin hosts that tear down and
re-initialise the UI layer) leaves them set:

```cpp
    if (translatorInstalled && installedLocale == newLocale)
        return true;                    // i18n.cpp:119-120
```

After the first application is gone, the static `QTranslator` is no longer
installed anywhere, but `install(sameLocale)` now takes this early return and
never re-installs it. Every `tr()` continues to resolve to the source language
while `md::i18n::locale()` reports the requested one — a silent wrong-language
failure of exactly the kind the header promises not to have ("Unknown locale
codes and known codes whose `.qm` is not embedded return false and leave the
current language untouched", `i18n.h:33-38`).

The same functions are also not safe to call from a non-GUI thread
(`installedLocale`, `translatorInstalled` and the single `QTranslator` are
unsynchronised); the header does not claim otherwise, so this is a
documentation/hardening note rather than a defect.

Rule: E.1 / C.30 (the module's invariant — "`translatorInstalled` means
`translator` is installed on the current application" — is not maintained
across the application's lifetime).

Patch sketch:

```cpp
QPointer<QCoreApplication> translatorHost;   // where it was installed

bool install(const QString& newLocale)
{
    ...
    if (translatorInstalled && installedLocale == newLocale
        && translatorHost == QCoreApplication::instance())
        return true;
    ...
    if (QCoreApplication::installTranslator(&translator)) {
        translatorInstalled = true;
        translatorHost = QCoreApplication::instance();
        installedLocale = newLocale;
    }
```

and clear `translatorInstalled`/`translatorHost` in `uninstall()`. Connecting
the host's `aboutToQuit` to `uninstall()` is the alternative.

---

### P2-4 — synchronous `delete` of the `QOpenGLWidget` during teardown

**file:line** `components/src/context3d.cpp:94-101` (`new` at `1136`)

```cpp
Context3D::~Context3D()
{
    if (glWidget_)
        delete glWidget_.data();
}
```

The object-lifetime reasoning in the comment is correct: the widget is a child
of the *host* (`new Context3DGlWidget(this, host_.data())`, `1136`), while
`Context3D` is a value member of `Canvas`, so `~Context3D` runs before
`~QWidget` destroys the host's children — deleting it here is what keeps the
`QPointer` from being nulled under the context. `QPointer` also makes a double
delete impossible.

The residual risk is GL-context teardown, not object lifetime: destroying a
`QOpenGLWidget` synchronously runs its context cleanup (`makeCurrent()` /
`doneCurrent()`) inside the destructor chain of the containing window. If the
window is being destroyed as part of platform teardown, that context may
already be invalid. Qt documents `deleteLater()` as the safe way to destroy a
GL widget, and a destructor is exactly the place where `deleteLater()` cannot
be used (no event loop afterwards), so the honest recommendation is a
conditional:

```cpp
Context3D::~Context3D()
{
    if (!glWidget_)
        return;
    if (QCoreApplication::instance() != nullptr && !QCoreApplication::closingDown()) {
        glWidget_->hide();
        glWidget_->deleteLater();       // deferred, context still valid
    } else {
        delete glWidget_.data();        // teardown: nothing will spin the loop
    }
}
```

This is a hardening note (P2), not a demonstrated crash: I did not trace a
concrete platform path that destroys a `Canvas` with a live GL child during
window teardown.

---

### P2-5 — `setMatrix()` amplifies input to N² without a cap and is not transactional

**file:line** `components/src/connectivity_chart.cpp:137-174`,
`components/src/chord_diagram.cpp:130-168`

Both components normalise their input into a square N×N matrix where
`N = max(names.size(), rows, longest row)` (`connectivity_chart.cpp:139-143`,
`chord_diagram.cpp:132-136`). There is no upper bound. A caller handing a
20 000-row single-column matrix — a perfectly ordinary "one row per node"
shape — allocates `N²` `qreal`s: 4·10⁸ values ≈ 3.2 GB, attempted with
`QVector<qreal> cells(dimension, …)` inside the loop. `QVector` raises
`std::bad_alloc` on failure, so this becomes an exception out of a public
setter.

The second issue is ordering. `names_` is replaced at `145`/`138`, `values_`
is cleared and refilled in the loop (`151-166` / `144-158`), but `size_` is
assigned only afterwards (`168` / `160`). A throw from the loop therefore
leaves `names()`, `size()` and `matrix()` describing different dimensions. No
out-of-bounds read follows — `value()` re-checks `row/column` against
`size_` *and* against `values_.size()`/`row.size()`
(`connectivity_chart.cpp:191-199`, `chord_diagram.cpp:185-193`), and
`geometryFor()` refuses to run when `size_` is inconsistent with an empty
plot (`357`) — so this is a coherence/robustness issue, not memory corruption.

Patch sketch:

```cpp
    constexpr int kMaximumNodes = 4096;             // 4096² qreal = 128 MiB
    if (dimension > kMaximumNodes) {
        qWarning("md::ConnectivityChart: refusing a %d-node matrix (max %d)",
                 dimension, kMaximumNodes);
        dimension = kMaximumNodes;                  // or return unchanged
    }

    QVector<QVector<qreal>> built;
    built.reserve(dimension);
    for (...) built.append(std::move(cells));
    // commit only after every allocation succeeded
    values_ = std::move(built);
    size_ = dimension;
```

Building into locals and swapping satisfies E.1/E.3 (no partial update) and
turns an unbounded allocation into a diagnosable refusal.

---

## Checked and found sound

Grouped by the criterion each item answers. Line numbers are from the files as
read.

### Boundary handling — empty containers, `size()-1`, zero ranges

- `chart_common.cpp:93-102` `tickStepOf()` — requires `size() >= 2`, and a
  non-finite fallback cannot reach the callers' divisions.
- `chart_common.cpp:106-134` `ChartSeries::boundingRect()` — skips non-finite
  points; empty input returns a null `QRectF` rather than `points[0]`.
- `chart_common.cpp:358-416` `rebuildPalette()` — both fill loops are bounded
  by a constant (`lighter/darker <= 300`) and the documented minimum of eight
  entries is enforced by a final loop (`412-413`), so `palette_.at(index %
  palette_.size())` (`430`) can never divide by zero.
- `chart_common.cpp:438-447` `seriesColour()` — negative and out-of-range
  indices return an invalid colour; `index < seriesColours_.size()` guards
  the second lookup.
- `chart_common.cpp:496-598` `rebuildLayout()` — `content`/`inner` are
  validated for validity and positive extent (`515`, `523`) and the plot is
  abandoned when `plotRight <= plotLeft || plotBottom <= plotTop` (`594`), so
  no negative-width plot reaches the transform.
- `chart_common.cpp:1782-1837` `hitTest()` — rejects non-finite positions,
  empty plots, resets both out-parameters to −1, and iterates with
  `seriesAt < series_.size()` / `pointAt < points.size()`.
- `autocorrelation_chart.cpp:231-251` — indices are safe by construction:
  `values_.resize(limit + 1)` with `limit >= 0`, `length = count - lag >= 1`,
  `centred` sized from `clean`. The `energy == 0` case is branched before the
  division (`237-242`), which is why `values_.at(0)` at `240` is inside the
  freshly resized vector.
- `bode_chart.cpp:166-213` `setResponse()` — truncates the three vectors to
  `min(size)` (`169-170`) **before** the parallel indexing at `183-185` and
  the paired rebuild at `686-689`, so the three arrays can never diverge;
  non-finite and negative frequencies are dropped (`186-189`).
- `bode_chart.cpp:476-501`, `604-671`, `526-549` — every `minimum/maximum`
  division is guarded by the NaN-safe forms `!(range.maximum > range.minimum)`
  and `!(range.minimum > 0.0)`; `frequencyRange()` returns before
  `frequencies_.back()` when the vector is empty (`478`).
- `bode_chart.cpp:805`, `823`, `839` — label loops are bounded by both the
  tick vector and the label list (`i < y.size() && i < labels.size()`).
- `coherence_chart.cpp:386-411`, `453-505` — same empty/`!>` guards;
  `samples.first()`/`samples.last()` (`748-756`) only run inside
  `samples.size() >= 2` (`746`).
- `box_plot_chart.cpp:63-77` `quantileOfSorted()` — explicit `size <= 0` and
  `size == 1` returns; `upper` clamped to `size-1`; `lower` is derived from a
  fraction clamped to `[0,1]`.
- `box_plot_chart.cpp:79-128` + `333-349` — this one matters for UB: every
  `y` is filtered by `std::isfinite` in `summaries()` (`342`) *before*
  `std::sort` (`86`), so the comparator is a strict weak ordering and NaN can
  never reach `std::sort` (which would be UB, not merely a wrong order).
- `box_plot_chart.cpp:476-509` `boxRectForSummary()` — `count <= 0`,
  non-positive/non-finite slot and non-finite mapped coordinates all return an
  empty rect instead of a NaN `QRectF`.
- `bar_chart.cpp:232-244` — `ticks.at(1)` only under `size() >= 2`;
  degenerate span falls back to `1.0`.
- `chord_diagram.cpp:295-324` `ribbonPath()` — rejects `source.size() < 2 ||
  target.size() < 2` before `first()`/`last()`; the backward loop uses
  `qsizetype` (signed) from `size()-2`, so no unsigned underflow.
- `chord_diagram.cpp:326-460` `buildLayout()` — `nodeCount_ < 2` and the
  radius threshold return an invalid layout (`329`, `341`); `totalWeight` is
  positive whenever ribbons exist (`374-409`); `total > 0.0` is required
  before dividing by it (`415`, `445`); a degenerate `available` span falls
  back to a smaller gap and then gives up (`399-403`).
- `chord_diagram.cpp:130-168`, `connectivity_chart.cpp:137-174` — the matrix
  is rebuilt square with `dimension = max(...)` and `std::max(0, dimension)`,
  and every row read is guarded by `row < values.size()`.
- `carousel.cpp:81-92` `wrapIndex()` — returns 0 before `index % count`, so
  the modulo can never divide by zero.
- `carousel.cpp:409-425` `transitionDelta()` — the `count / 2` comparisons run
  only under `loop_ && count > 1`.
- `carousel.cpp:132-134`, `163-165`, `198-199`, `233-235` — `current_` and
  `previous_` are clamped after every add, remove, clear and index change, so
  the `slides_.at(index)` calls in `syncVisibility()` (`472`) and
  `arrangeChildren()` (`582`) cannot outrun a shrunk list.
- `carousel.cpp:546`, `560` — indicator/heat geometry checks `index < 0 ||
  index >= slideCount()` before indexing.
- `cascader.cpp:59-105` — `nodesAt()` checks `level < indices_.size()` before
  `indices_.at(level)`, `nodeAt()` checks the node index, and `columnCount()`
  returns `1` when `levels_` or `indices_` is empty (`90-91`) so the
  `indices_.last()` at `94` is only reached with a non-empty list.
- `cascader.cpp:196-204` `hasCompletePath()` — explicit `indices_.isEmpty()`
  guard before `indices_.last()` at `202`.
- `cascader.cpp:271-279` `moveCursor()` — returns before the `% count` when
  `count <= 0`.
- `cascader.cpp:116-124` — `columnWidth = area.width() / std::max(1,
  columnCount())`.
- `calendar.cpp:160-181` `cellRect()` — `offset` bounds-checked against
  `[0, kDayCells)` before the row/column arithmetic; invalid `QDate` rejected.
- `calendar.cpp:225-249` — `indexOfCell()` is bounded by `cells_.size()` and
  both `storedCell()`/`isDateEnabled()` test `index >= 0` before
  `cells_.at(index)`.
- `calendar.cpp:191-223` — month/year indices are range-checked before the
  `% 3` / `/ 3` cell arithmetic.
- `context1d.cpp:239-265` — non-finite and non-positive area height returns
  the centre (`241`), the `(max-min)` division is guarded by
  `maximum_ > minimum_` (`245`), and `pointAt()` handles the empty vector
  (`253`) and a single value (`258`).
- `context1d.cpp:99-104` — tick count clamped to `[0, 1000]`, so the tick loop
  is bounded by a component-owned constant, not by the caller.
- `context3d.cpp:519-584` — `viewPoint()` detects `w ≈ 0` and yields a NaN
  vector (`523-525`) that `finiteVector()` rejects; `projectView()` rejects a
  non-finite/non-positive aspect, a non-positive viewport, depth outside
  `[near, far]`, a degenerate tangent and non-positive ortho spans
  (`534-573`), then re-checks the final screen coordinates (`575-581`).
  `paint()` passes `area.width()/area.height()` (`305`) into that guarded
  function, so a zero-height viewport cannot produce an infinity.
- `context3d.cpp:586-607` `isFrontFacing()` — requires three vertices before
  `vertices.at(0..2)`.
- `context3d.cpp:103-190` — `setCamera()`, `perspective()` and
  `orthographic()` reject non-finite values, a zero-length direction/up, a
  parallel cross product, `fov ∉ (0,180)`, `near <= 0` and `far <= near`.
- `avatar.cpp:335-362` `effectiveInitials()` — `words.isEmpty()` guard before
  `words.at(0)` (`356`), and the `size() >= 2` branch is the only one that
  touches `at(1)`.
- `anchor.cpp:69-89`, `136-163` — `anchors_.at(index)` calls are all preceded
  by bounds checks on `anchors_.size()`/`current_` (verified at the four call
  sites); the list is only mutated through `setAnchors`, which re-derives
  `active_`.
- `box_plot_chart.cpp:351-407`, `connectivity_chart.cpp:430-454` — public
  geometry queries return an empty rect / `(-1,-1)` for out-of-range indices
  rather than indexing.

### Error handling — callbacks, `noexcept`, destructors

- `chart_common.cpp:1741` `throw std::runtime_error` is intentional and lands
  in `view::guard`, which is declared `noexcept` with `catch (const
  std::exception&)` and `catch (...)` (`view/include/md/view/error_guard.h:82-89`,
  `100-110`). No `std::terminate` (ERR50-CPP satisfied) and the diagnostic is
  recorded in `GuardReport::lastError` for `View::lastError()`.
- `chart_common.cpp:1476-1637` — `painter.save()` at `1478` is matched by
  `painter.restore()` at `1630`, which sits **after** both catch blocks, so the
  painter is balanced on the exception path; the opaque background is filled
  first (`1484`), so a failure cannot leave stale pixels.
- `chart_common.cpp:1693-1712` `onAnimatorValue()` — the virtual hook is
  invoked inside a `try`, `reportAnimationFailure()` stops the animator and
  settles the run so the chart is never left half-revealed, and the message is
  re-thrown later through the guarded paint entry point (`1730-1742`).
- `chart_common.cpp` — no destructor throws; the only `throw` in the audited
  scope is the one above, called from `paintContent()`.
- `canvas.cpp:25-35` — `guardCanvas()` wraps `setMode`, `setBackground`,
  `paintContent` and `showEvent`; both handlers emit a `qWarning` diagnostic
  (no silent swallow).
- `context1d.cpp:27-37`, `context2d.cpp:38-46`, `context3d.cpp:52-61` — the
  same pattern for every context entry point, each with a message.
- `context2d.cpp:397-471` — each replay command is individually wrapped in
  `try`/`catch` with `qScopeGuard` restoring the painter (`406`), so one bad
  primitive does not abort the rest of the scene.
- `container_transform.cpp:207-222` — the animator callback contains its own
  `try`/`catch` with a diagnostic, and re-checks `surface_.isNull()` after the
  callback boundary (`211`).
- `chart_common.cpp:911-923` `measureOverride()` — catches and falls back to a
  degenerate measured size instead of unwinding into the layout pass; both
  handlers diagnose.
- `bode_chart.cpp:1009`, `coherence_chart.cpp:808`, `connectivity_chart.cpp:751`,
  `chord_diagram.cpp:771`, `box_plot_chart.cpp:605`, `bar_chart.cpp:662`,
  `autocorrelation_chart.cpp:467` — every `paintPlot` has its own
  `try`/`catch (std::exception)` + `catch (...)` with a `qWarning`.
- ERR51-CPP (a handler must not throw) — no `catch` block in scope concludes
  with a `throw`; I checked all of them.

### Resource and lifetime

- Every `new` in scope has an owner: `new Button(this)` (`alert.cpp:206`,
  `banner.cpp:94,100`), `new QTimer(this)` (`carousel.cpp:70,75`,
  `bottom_sheet.cpp:100`), `new MathematicsCoordination(this)`
  (`chart_common.cpp:138`), `new animation::Animator(this)`
  (`animation.cpp:53`, `container_transform.cpp:103`), `new
  QNetworkAccessManager(this)` (`avatar.cpp:622`), `new Context3DGlWidget(this,
  host_.data())` (`context3d.cpp:1136`), `new QGraphicsOpacityEffect(child)`
  (`animation.cpp:547`). No raw owning pointer is stored without a QObject
  parent or a `QPointer`/`unique_ptr` holder.
- `context3d.cpp:99-100` — the single `delete` is `QPointer`-guarded, so no
  double delete.
- `container_transform.cpp:275-310` — `effect_` ownership is transferred to
  the widget by `setGraphicsEffect()` (`281`) and every later use re-checks
  `surface_.isNull()`, `effect_.isNull()` and `surface_->graphicsEffect() ==
  effect_` before dereferencing (`301-306`).
- `animation.cpp:94-131` — the replaced child is hidden then `deleteLater()`d
  (`103-104`); the `destroyed` connection is installed with `this` as context
  and its lambda re-checks `child_.data() != child` (`123`), so a
  `deleteLater()`-induced callback cannot clear a newer child; `setChild()`
  rejects `child == this` and an ancestor (`83-86`).
- `card.cpp:154-159`, `178-181` — `destroyed` handlers are pointer-filtered
  (`if (media_ == media)`) and `addAction`'s handler removes by value, so a
  destroyed widget cannot leave a stale entry; `setMedia`/`clearActions`
  disconnect before removal (`146`, `197`).
- `carousel.cpp:123` — the slide `destroyed` handler is installed with `this`
  as context and prunes by pointer.
- Timers are all QObject children (`carousel.cpp:70,75`, `bottom_sheet.cpp:100`,
  `banner.cpp:107`, `clock.cpp:343`) connected with `this` as the receiver
  context, so Qt removes the connection when the widget dies; no timer
  outlives its owner and none is fired into a destroyed receiver.
- `avatar.cpp:577-617` — the reply handler is context-guarded, carries a
  generation counter, checks `reply_ != reply` (`578`), and every exit path
  calls `deleteLater()`; `cancelTransfer()` disconnects, aborts and defers
  (`614-616`). A superseded reply cannot write into the new state.
- `bottom_sheet.cpp:1043-1087` `advanceFocus()` — returns on an empty chain
  (`1066`) and computes `next` modulo a positive `count` (`1069-1086`).
- `QPainter` balance — every component paint path uses one of three idioms and
  I checked each: `PainterStateGuard` RAII (`autocorrelation_chart.cpp:43-54`,
  `bode_chart.cpp:57-68`, `chord_diagram.cpp:102-113`), `qScopeGuard`
  (`context1d.cpp:284`, `context2d.cpp:406`), or save-before-`try` /
  restore-after-catch (`chart_common.cpp:1478/1630`).
- Virtual destructors — `ChartBase` declares `~ChartBase() override`
  (`chart_common.h:176`); `Context1D/2D/3D` are non-polymorphic members with
  virtual-free interfaces; no class in scope with virtual functions lacks a
  virtual destructor (C.35). Overrides are marked `override` throughout
  (C.63).
- Initialisation — all members I read have default member initialisers
  (`chart_common.h:343-529`, `cascader.h:109-113`, `context2d.cpp:65-92`); no
  uninitialised read was found (ES.20/ES.21).

### State machine and Qt conventions

- `chart_common.cpp:1476-1509` — `paintContent()` fills an opaque background
  first (matching `WA_OpaquePaintEvent` set in the constructor at `146`) and
  neither setters nor hooks re-enter `paintEvent`: every setter only flips
  flags and calls `requestUpdate()`, which schedules.
- `chart_common.cpp:763-767` — the mid-reveal clip is `IntersectClip` on the
  plot, so an animating chart cannot paint outside the data area; the settled
  frame deliberately restores the unclipped contract (`752-758`), which is
  documented in `chart_common.h:346-365`.
- `chart_common.cpp:818-827` — a throwing `updateChartCache()` leaves
  `chartCacheValid_ == false`, so the next paint retries rather than
  permanently serving a half-built cache.
- `chart_common.cpp:503-508`, `627-638` — the layout key includes size, theme
  revision, title, both flags, fitted bounds, tick count and axis-label
  presence; the static-layer key (`834-845`) additionally includes the content
  and plot rects, device pixel ratio, visible bounds and legend signature.
- `chart_common.cpp:1511-1621` — every offscreen buffer is gated by
  `withinRasterLimit()` (non-empty and ≤ 16384 px in both axes) before
  `QImage` construction, and all buffers are `Format_ARGB32_Premultiplied`,
  which is what makes the `reinterpret_cast<QRgb*>` scanline access in
  `LumaPlane`/`pixelAt`/`setPixelAt`/`renderJittered` (`964-991`, `1441-1450`)
  well-defined. Each filter's loop bounds were checked against its neighbour
  reads (`filterFxaa` 1007-1033, `filterFmaa` 1051-1097, `filterMlaa`
  1133-1225, `filterSmaa` 1285-1342, `filterCmaa` 1366-1396): the ±1/±2
  accesses stay inside `[0, width)`/`[0, height)`.
- `chart_common.cpp:1041-1455` — the morphological filters re-read a private
  `QImage` copy for the second (column) pass, so the passes cannot interfere
  through aliasing.
- Division-by-zero sweep over the 35 in-scope TUs — the only `/` on a
  caller-influenced value that I found unguarded is `connectivity_chart.cpp:399`
  (`/ size_`), which is unreachable because `geometryFor()` returns at `357`
  when `size_ <= 0`; every other site is guarded, bounded by a constant, or
  divides by a `std::max(1, …)`.
- `QString`→number conversions — there are none in scope (`grep` for
  `toInt|toDouble|toFloat|toLongLong|atoi` returns no hits in these files), so
  the "silent parse failure" class of defect cannot occur here. Formatting in
  the reverse direction is used instead (`QString::number`).
- `QPainter::begin` on a null device — `context1d/2d/3d` and `Canvas` all test
  `painter.isActive()` before drawing (`context1d.cpp:270`,
  `context2d.cpp:400`, `context3d.cpp:486`). The one place a painter is
  constructed without that test is `chart_common.cpp:881` on the static-layer
  pixmap, which is the P2-1 finding above.
- Paint outside `rect()` — the chart contract deliberately allows a chart to
  draw marks outside `plotRect()` (documented at `chart_common.h:19-21`) and
  the mid-reveal clip contains it; the components that draw labels outside
  their data area (`bode_chart.cpp:725-789`, `coherence_chart.cpp:728-733`,
  `connectivity_chart.cpp:730-733`) install their own `setClipRect` first, and
  `connectivity_chart.cpp:733` clamps the hover readout into the plot.

---

## Coverage and method

- **Read in full:** `core/` (all 10 files), `canvas.cpp`/`canvas.h`,
  `context1d.cpp`, `context2d.cpp`, `chart_common.h`, `chart_common.cpp`
  (all 1844 lines), `autocorrelation_chart.cpp`, `cascader.h`, and
  `cascader.cpp:40-320`.
- **Audited along public entry points plus every flagged site:** the remaining
  25 sources, including `bode_chart.cpp` (1036 lines, ~500 read),
  `coherence_chart.cpp`, `context3d.cpp` (1143 lines, ~400 read),
  `box_plot_chart.cpp`, `bar_chart.cpp`, `connectivity_chart.cpp`,
  `chord_diagram.cpp`, `calendar.cpp`, `carousel.cpp`, `avatar.cpp`,
  `animation.cpp`, `card.cpp`, `container_transform.cpp`,
  `bottom_sheet.cpp`. For these I traced each public setter to the paint /
  measure / query path and inspected every container index, division, `new`,
  `connect`, `throw` and `QPainter::save` found by an exhaustive pattern
  search over the scope (patterns: `.front() .back() .first() .last() .at(
  [i] / %`, `new`, `deleteLater`, `connect(`, `QTimer`, `std::function`,
  `noexcept`, `throw`, `catch`, `toInt|toDouble`, `reinterpret_cast`,
  `static_cast<int>`, `resize(`, `qRound|lround`).
- **Not claimed as sound:** `affix.cpp`, `anchor.cpp`, `alert.cpp`,
  `avatar.cpp` image-scaling internals, `badge.cpp`, `banner.cpp`,
  `button.cpp`, `chip.cpp`, `checkbox.cpp`, `clock.cpp`, `chart_animation.cpp`,
  `animation.cpp` step interpolation and `context3d.cpp` GL-buffer paths were
  covered by the pattern search and their public setters, but not read
  line-by-line end to end. I found nothing in them, and I am not asserting
  they are defect-free.
- No code was edited and no build was run; findings are static-analysis
  conclusions, and the P1-2 timings are order-of-magnitude estimates from the
  operation count, not measurements.

## Counts

- **P0: 0**
- **P1: 2** (P1-1 `avatar.cpp:544`, P1-2 `autocorrelation_chart.cpp:244`)
- **P2: 5** (P2-1 `chart_common.cpp:868`, P2-2 `chart_common.cpp:178`,
  P2-3 `core/src/i18n.cpp:119`, P2-4 `context3d.cpp:100`,
  P2-5 `connectivity_chart.cpp:139` + `chord_diagram.cpp:132`)
- **Total: 7**

---

## Gap closure

Second pass over the files the first pass only pattern-searched, plus the two
areas that were explicitly not traced. **The counts above are superseded by the
"Updated counts" section at the end of this appendix**; the findings above are
unchanged and keep their ids.

Method: every file in this appendix was read end-to-end. Where a public entry
point leads into a child widget, a timer or a signal, the path was followed to
its final member access. The P0 bar is unchanged (memory safety or UB reachable
from a public API).

### New findings

| id | file:line | severity | issue | fix |
|---|---|---|---|---|
| G1 | `components/src/chip.cpp:553` | **P0** | `activate()` emits `clicked()` and then reads/writes `this` (`isSelectable()`, `toggleSelected()`); a slot that deletes the chip leaves a dangling `this` dereference | `QPointer` self-guard after the emit, or toggle before emitting |
| G2 | `components/src/anchor.cpp:157` | **P0** | `activate()` calls `setActiveIndex()`, which emits `activeAnchorChanged`, then reads `anchors_.at(index)`; a slot that deletes the anchor is a use-after-free | Capture the item *before* `setActiveIndex()`, or self-guard |
| G3 | `components/src/affix.cpp:171` | **P0** | `setScrollOffset()`/`setTargetOffset()` accept NaN/Inf and `arrangeChildren()` passes the result to `qRound()` — non-finite `double`→`int` conversion is UB | Reject non-finite offsets at the setter, or clamp before `qRound` |
| G4 | `components/src/banner.cpp:254` | P1 | The close hit-test ignores `closeVisible_`: an invisible 40 dp close region still fires `close()` and hides the banner | Return an empty rect from `closeButtonRect()` when `!closeVisible_` |
| G5 | `components/src/badge.cpp:56` | P2 | `hiddenByCount_` is cleared only by `setCount(>0)`; after `setCount(0)` hid the badge, `setText()` leaves it invisible | Clear the latch (and `show()`) in `setText()` |
| G6 | `components/src/chart_animation.cpp:204` | P2 | `std::clamp` does not sanitise NaN, so `setRunProgress(NaN)` latches `running_ == true` forever and paints with `setOpacity(NaN)` | `if (!std::isfinite(p)) p = 1.0;` before the clamp |
| G7 | `components/src/context3d.cpp:849` | P2 | `~Context3DGlWidget` writes `context_->glReady_` through a raw back-pointer; correctness depends on `~Context3D` deleting the widget first. **This also invalidates the P2-4 patch sketch**: see below | Null the back-pointer before any deferred delete |

---

### G1 — `Chip::activate()` dereferences `this` after emitting `clicked()`

**file:line** `components/src/chip.cpp:553-559` (callers at `351`, `401`)

```cpp
void Chip::activate()
{
    emit clicked();                 // :555  <- direct connection: the slot runs here

    if (isSelectable())             // :557  reads variant_ (freed if the slot deleted us)
        toggleSelected();           // :558  reads selected_, calls syncAppearance()/update()
}
```

Reachable path: a `Chip` in a layout, host connected with
`connect(chip, &Chip::clicked, this, [chip] { delete chip; });` (a legal and
common "remove this chip" idiom; `deleteLater()` would be safe, `delete` is
what makes this blow up), then any of:

```
Chip::mouseReleaseEvent   chip.cpp:326  -> activate()  :351
Chip::keyReleaseEvent     chip.cpp:380  -> activate()  :401   (Space/Return/Enter)
```

`QObject::connect` defaults to `Qt::AutoConnection`, which is direct for
same-thread objects, so the slot runs inside the `emit` and the object is gone
by the time control returns to line 557. `isSelectable()` reads `variant_`,
`toggleSelected()` reads `selected_` and calls `syncAppearance()` → `setShape`,
`setContentColour`, `requestUpdate()` — all member access on freed memory.
This is MEM50-CPP by construction.

Note the asymmetry with everything else in the scope: `Button` emits `clicked()`
as its last statement (`button.cpp:354-355`), `CheckBox::applyState()` emits
last (`checkbox.cpp:120-121`), `Clock::onTick()` emits last (`clock.cpp:508-509`),
and `Chip`'s own `mouseReleaseEvent` emits `closed()` last (`chip.cpp:345-347`).
`activate()` is the one function that does work after the emit.

Patch sketch (either form):

```cpp
void Chip::activate()
{
    if (isSelectable())
        toggleSelected();          // mutate before notifying

    emit clicked();                // last statement
}
```

or, if the toggle must follow the click:

```cpp
void Chip::activate()
{
    const QPointer<Chip> self(this);
    emit clicked();
    if (!self)                     // a slot deleted us
        return;
    if (isSelectable())
        toggleSelected();
}
```

The `QPointer` works for a `QWidget` because `~QObject` nulls it; the ordering
fix is cheaper and is the same rule the rest of the codebase already follows.

---

### G2 — `Anchor::activate()` reads the list after a signal that can delete the anchor

**file:line** `components/src/anchor.cpp:157-165` (`setActiveIndex` emits at `154`)

```cpp
void Anchor::activate(int index)
{
    if (index < 0 || index >= anchorCount())
        return;
    setActiveIndex(index);                      // :162  emits activeAnchorChanged :154
    const AnchorItem item = anchors_.at(index); // :163  <- freed if the slot deleted us
    emit anchorActivated(item.key, item.offset);
}
```

Reachable path — a click on a row that is *not* the active one:

```
Anchor::mouseReleaseEvent  anchor.cpp:371 -> activate(pressedIndex)  :385
Anchor::keyPressEvent      anchor.cpp:388 -> activate(active_)       :411
```

`setActiveIndex()` only emits when `next != active_` (`anchor.cpp:149-150`), which
is exactly the "click a different row" case; the `Enter` path usually passes the
current index and therefore does not emit, so the click path is the reliable
trigger. A host that responds to `activeAnchorChanged` by rebuilding or deleting
the anchor list — a plausible reaction, since the signal means "the selection
moved" — frees `anchors_` and then line 163 reads it.

Patch sketch: take the copy first, and use the signal for notification only:

```cpp
void Anchor::activate(int index)
{
    if (index < 0 || index >= anchorCount())
        return;

    const AnchorItem item = anchors_.at(index);  // copy before any emission
    setActiveIndex(index);                       // may emit; may delete us
    emit anchorActivated(item.key, item.offset); // item is a value, no member reads
}
```

If `anchorActivated` itself can delete the anchor, that emit is already the last
statement and needs nothing.

---

### G3 — `Affix` accepts non-finite offsets and converts them to `int`

**file:line** `components/src/affix.cpp:101-133` (setters), `171` (`qRound`)

`setScrollOffset(qreal)` (`affix.h:44`) and `setTargetOffset(qreal)`
(`affix.h:56`) store the argument unvalidated. Neither `qFuzzyCompare(offset + 1.0,
scrollOffset_ + 1.0)` nor `std::max<qreal>(0.0, threshold)` rejects a NaN
(`setThreshold` is accidentally safe: `std::max(0.0, NaN)` returns `0.0`).
`offset()` (`46-52`) then returns NaN — either directly as
`-(NaN - target)` or as `-threshold_` when stuck — and `arrangeChildren()`
does:

```cpp
const qreal top = content.top() + offset();          // affix.cpp:169  -> NaN
content_->arrange(QRect(qRound(content.left()), qRound(top), ...));  // :171
```

Converting a NaN or out-of-range `double` to `int` is undefined behaviour
(`[conv.fpint]`), and Qt's `qRound` documents its result as undefined outside
the `int` range. Reachable path:
`affix.setContent(child); affix.setScrollOffset(std::numeric_limits<qreal>::quiet_NaN());`
— `arrangeChildren` runs directly from the setter (`108`), no paint needed.

Honest severity note: this is **UB, not memory corruption**. On the targets this
code runs on, `qRound(NaN)` yields `INT_MIN`, the child is arranged at a wildly
negative `y` (Qt clamps widget geometry to 24-bit ranges), so the visible symptom
is a vanished or misplaced bar. I have graded it P0 because the bar you set is
"memory safety **or UB** reachable from a public API"; if you would rather keep
P0 for memory-unsafe defects, this is a P1. It is worth fixing either way
because the fix is two lines and the same two lines cover the whole class
(see pattern P-6).

Patch sketch:

```cpp
void Affix::setScrollOffset(qreal offset)
{
    if (!std::isfinite(offset))
        offset = 0.0;                    // or return; leave the old value
    ...
}
```

and the same guard in `setTargetOffset`. A shared `md::finiteOr(value, fallback)`
helper (pattern P-6) would cover `Anchor::setScrollOffset`, `Button::setIconSize`
and the paint-side conversions too.

---

### G4 — a hidden Banner close button is still clickable

**file:line** `components/src/banner.cpp:254-267` (geometry), `499-517` (handlers)

`closeButtonRect()` returns `closeButtonArea().translated(0.0, revealOffset())`
unconditionally — unlike `Alert::closeButtonRect()`, which returns an empty
`QRectF` when the affordance is off (`alert.cpp:408-411`). The Banner's three
mouse handlers use that rect without consulting `closeVisible_`:

```cpp
void Banner::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        pressedClose_ = closeButtonRect().contains(event->position());  // :504
}

void Banner::mouseReleaseEvent(QMouseEvent* event)
{
    const bool activate = pressedClose_ && ... && closeButtonRect().contains(...); // :511-512
    pressedClose_ = false;
    if (activate)
        close();                                                        // :516
}
```

Reachable path: `banner.setCloseButtonVisible(false);` then click the top-right
40×40 dp region → `pressedClose_ = true` → release → `close()` → `closedEmitted_`
fires, `hideAnimated()` runs, `closed()` is emitted. A banner that was
explicitly configured with no close affordance disappears when the user clicks
empty space where the button would have been. Meanwhile the paint path
(`477`) and the layout path (`285`) both honour `closeVisible_`, so the three
consumers of the same geometry disagree — which is exactly what `Alert` avoids
by making the geometry function itself return empty.

Patch sketch (mirror Alert):

```cpp
QRectF Banner::closeButtonRect() const
{
    if (!closeVisible_)
        return QRectF();

    return closeButtonArea().translated(0.0, revealOffset());
}
```

This fixes press, release and hover in one place; `mouseMoveEvent` (`523`) then
stops reporting a hover over nothing as well.

---

### G5 — `Badge`'s count-owned visibility latch is not released by `setText()`

**file:line** `components/src/badge.cpp:56-91`

```cpp
void Badge::setCount(int count)
{
    ...
    if (value == 0) {
        text_.clear();
        hiddenByCount_ = !isHidden();
        hide();                          // :79-80
    } else if (hiddenByCount_) {         // :84
        hiddenByCount_ = false;
        show();
    }
}

void Badge::setText(QString text)
{
    dot_ = false;
    count_ = 0;
    if (text_ == text) { refreshGeometry(); return; }
    text_ = std::move(text);
    refreshGeometry();                   // :67  never touches hiddenByCount_ or show()
}
```

Reachable path: `badge.setCount(0);` (hides, `hiddenByCount_ == true`) then
`badge.setText("New");` → the badge stays hidden with non-empty text. The latch
is only cleared by `setCount(>0)` (`84-87`), so a host that switches a badge
from a numeric count to a text label — the documented use of `setText()`,
`badge.h:38` — sees nothing. The same happens in reverse order only by luck:
`setText` then `setCount(0)` hides correctly.

The bookkeeping itself is subtle-but-correct at line 79: `hiddenByCount_` records
"we were visible, so we own the hiding" and therefore does not steal ownership
from an owner that had already hidden the badge. The defect is that the latch
has exactly one release site.

Patch sketch:

```cpp
void Badge::setText(QString text)
{
    dot_ = false;
    count_ = 0;

    if (hiddenByCount_) {          // a text badge is a visible badge again
        hiddenByCount_ = false;
        show();
    }
    ...
}
```

---

### G6 — `ChartAnimation::setRunProgress()` latches on NaN

**file:line** `components/src/chart_animation.cpp:204-209`

```cpp
void ChartAnimation::setRunProgress(qreal progress) noexcept
{
    progress_ = std::clamp(progress, 0.0, 1.0);   // NaN passes through: both < are false
    if (progress_ >= 1.0)                          // false for NaN
        running_ = false;                          // never cleared
}
```

`setRunProgress()` and `beginRun()` are public (`chart_animation.h`), and the
value object is reachable through `ChartBase::animation()`. With a NaN argument
the state machine reports `isRunning() == true` forever (nothing else clears it
except `finishRun()`/`stop()`), and `ChartBase::paintPlot` then computes
`painter.setOpacity(opacity * NaN)`. Reachability is "a caller passes a
non-finite value"; the in-scope Animator always delivers 0..1, so this is a
defensive fix, not a live bug — hence P2. It is the same latch shape as G5.

Patch sketch: `if (!std::isfinite(progress)) progress = 1.0;` before the clamp
(a non-finite frame is a finished run, which is the safe settle).

---

### G7 — the GL widget's back-pointer is a load-bearing destruction order (and a correction to P2-4)

**file:line** `components/src/context3d.cpp:847-865`, `99-100`

File state audited: `context3d.cpp`, 1143 lines, last write
2026-11-12T21:01:45+08:00, SHA-256 prefix `72FF2FDDCDF8FE4E` (git `cde6b3c`).
Another agent may edit this file; **the line numbers below are valid only for
that revision**, and I have not proposed patch code for it.

```cpp
Context3DGlWidget::~Context3DGlWidget()
{
    if (context_ != nullptr)
        context_->glReady_ = false;      // :849-850  raw back-pointer to a non-QObject
    ...
}
```

`Context3D` is a value member of `Canvas`; the GL widget is a child of the
*host*, and `context_` is a bare `Context3D*` (line 824). The invariant that
makes line 850 safe is that `~Context3D` (`94-101`) deletes the widget
**before** the `Context3D` object stops existing. That invariant holds today and
is why the explicit `delete` is there rather than left to Qt.

**Correction to P2-4.** My first-pass patch sketch suggested
`hide(); deleteLater();` when an event loop is running. That recommendation was
unsafe as written: a deferred delete runs *after* `~Context3D`, so line 850
would write through a dangling `context_` — it converts a GL-teardown question
into a use-after-free. The correct deferred form needs a detach step that nulls
the back-pointer first (`context_->glWidget_` cleared and `child.context_ =
nullptr`, both non-QObject-side, so a plain `Context3D::detachGlWidget()` or a
`QPointer`-style cleared pointer is required), or the widget must be destroyed
synchronously as it is now. Given that, the current synchronous delete is the
*safer* choice and the only hardening worth doing is documenting the ordering
invariant at both sites and asserting `context_` is either null or alive.
(P2-4 therefore stands only as "document/assert the ordering", not "switch to
deleteLater".)

The rest of the GL branch was traced and is sound; see the second-pass sound
list below.

---

### Canvas mode switching — traced, found sound

The first pass read `canvas.cpp` fully but did not argue the switch path, so it
is recorded here.

- **No null context is possible.** The three contexts are value members
  (`canvas.h:82-84`) constructed in the member-init list (`canvas.cpp:41-43`);
  `context1d()/2d()/3d()` return `&member` (`66-94`) and are never null. There
  is no "context not created yet" state, so no call site needs a null check.
- **Round trips preserve state.** `setMode()` only assigns `mode_`, calls
  `Context3D::setHostModeActive(mode_ == ThreeD)` and requests a draw
  (`49-59`). Switching never clears `Context1D::values_`,
  `Context2D`'s command list or `Context3D::primitives_`, so 1D→2D→3D→1D
  returns each mode to exactly the scene it had. `paintContent()` dispatches on
  `mode_` every paint (`132-147`).
- **3D→anything.** `setHostModeActive(false)` hides the GL child
  (`context3d.cpp:734-743`). `effectiveBackend()` requires
  `glWidget_ && glWidget_->isVisible() && glReady_` (`459-464`), so the next
  `Context3D::paint()` correctly falls through to `renderSoftware()` instead of
  returning early and leaving the area blank.
- **anything→3D while hidden.** `setHostModeActive(true)` calls
  `ensureOpenGLWidget()`, which returns immediately when the host is not visible
  (`context3d.cpp:1125`) and `updateOpenGLGeometry()` returns when there is no
  widget (`750`); creation is deferred to `Canvas::showEvent()`
  (`canvas.cpp:149-158`), which sets the viewport first and then enables the
  host mode. No half-created widget is ever shown.
- **Switching during a drag.** `Canvas` holds no gesture state; the only
  per-context gesture state in scope is `Context2D`'s save stack
  (`context2d.cpp:112, 129-149`) and the 3D camera, and `setMode()` touches
  neither. A child `View` drag is not interrupted (no reparent, no hide, no
  focus change). A drag cannot be "in progress on the GL child" because that
  widget sets `Qt::WA_TransparentForMouseEvents` (`context3d.cpp:842`) and never
  receives pointer input.
- **A throwing context cannot unwind into Qt.** `Canvas::paintContent` wraps the
  dispatch in `guardCanvas` (`132-147`, helper at `25-35`), which reports and
  returns; `View::paintEvent` has already painted the background, so the next
  frame is complete.

---

### Checked and found sound — second pass

Each entry names the guard, so this list is auditable the same way as the first.

**affix.cpp** — empty-content early returns (`137`, `160`); `qFuzzyCompare`
no-ops (`103`, `115`, `126`); the shadow band is clamped to the content height
(`187`); `stuckChanged` is the last statement of `syncStuck()` (`81`), so a slot
that deletes the affix is safe; `applyStuckAppearance` is the single writer of
the stuck state (`59-70`). Only G3.

**anchor.cpp** — six bounds-checked accessors (`78`, `86`, `94-95`, `131-132`,
`148`, `159`); `clampActive()` maps empty→`-1` and re-clamps after every
mutation (`167-182`); `insertAnchor` shifts `active_` (`235-236`) and
`removeAnchor` decrements or retreats (`249-252`) before re-clamping;
`keyPressEvent`'s `End` on an empty list passes `-1` into `setActiveIndex`,
which returns early; `pressed_` uses a `(-1,-1)` sentinel and is cleared before
any signal (`373-374`). Only G2.

**alert.cpp** — this is the model the Banner should follow: `closeButtonRect()`
returns empty when `!closable_` (`408-411`) *and* all three mouse handlers
re-check `closable_` (`542`, `549-550`, `561`); `close()` is guarded by
`closedEmitted_` and emits last (`582-591`); `onShown()` resets the latch
(`598-601`); text width is clamped at zero in both measure and paint (`399`,
`483`); the wrapped-text helpers reject empty text, zero width and zero lines
(`53`, `85`); the action button is a parented child and is hidden when its text
is empty (`423-426`).

**badge.cpp** — `target_` is a `QPointer` checked at every one of its seven uses
(`136`, `141`, `181`, `190`, `193`, `214`); `positionOverTarget()` returns for a
null host and handles the self-host case (`193-201`); `measureOverride` and
`paintContent` both derive from the same `text_`/`dot_` pair (`249-256`,
`273-285`); `readableOn()` is total over any `QColor` (`38-43`). Only G5.

**banner.cpp** — `revealTimer_` is a value member connected with `this` as
context (`106-107`); `tickReveal()` divides by `max(1, duration)` and clamps the
raw progress (`331-334`); `finishReveal()` stops the timer before hiding
(`347-356`); `closedEmitted_` prevents a double `closed()` (`388-396`);
`place()` hides an absent action button rather than arranging it (`287-291`);
`painter.save()`/`restore()` bracket the whole paint with a clip and translate
(`422`, `496`). Only G4.

**button.cpp** — `setIconSize()` maps a negative or NaN argument to `0.0`
(`170`); the elide budget is clamped to zero and an empty budget paints no label
(`274-286`); the visual surface is `min(content.height(), 40 dp)` so a squeezed
button cannot invert (`232`); every painter branch is save/restore paired
(`238-243`, `252-258`, `328-332`); `clicked()` is the last statement of both the
mouse and key paths (`354-355`, `366-367`); `changeEvent` re-syncs the
appearance on enable/theme changes (`370-376`).

**chip.cpp** — `layoutFor()` clamps the text width to zero (`452`) and mirrors
logical to physical once (`455-461`); `closeRectFor()` is the single geometry
producer shared by paint, press, release and hover (`317`, `330`, `361`,
`469-481`); `mouseReleaseEvent` resolves close-vs-body *before* emitting and
emits last (`332-351`); `keyReleaseEvent` ignores auto-repeat and emits last
(`382-401`); `setClosable()` clears both `closePressed_` and `closeHovered_`
(`177-179`); `setVariant()` clears a selection that no longer applies and emits
after the state is consistent (`115-123`). Only G1.

**checkbox.cpp** — `pressedInside_` is set only on a left press and cleared on
every release (`226`, `235`); auto-repeat is ignored (`245`); activation is
gated on `isEnabled()` in both the mouse and key paths (`239`, `245`); the
tristate cycle is exhaustive over the three enum values (`126-141`);
`applyState()` emits `toggled` last (`120-121`); `stateLayerFor` returns
transparent for a disabled control (`50-51`).

**clock.cpp** — the best exception discipline in the scope: every override and
the timer slot has its own boundary with a diagnostic (`504-515`, `519-541`,
`543-605`, `607-629`), and `paintContent` saves before the `try` and restores
after the catch (`548`/`604`), so a throw cannot unbalance the painter; invalid
time/date/zone fall back rather than propagate (`84`, `104`, `113`, `332-333`,
`455`, `463-467`); `squareIn()` rejects a non-positive side (`121-123`);
`paintDial()` rejects an empty rect, an invalid time and a non-positive radius
(`242-250`) and skips non-positive tick lengths (`262`); `syncTimer()` is
idempotent because it checks `isActive()` (`496-501`); `onTick()` emits `tick`
last (`508-509`), so a slot that deletes the clock is safe.

**chart_animation.cpp** — copy and assignment deliberately drop the driver and
force `running_ = false` (`36-65`); every enum setter clamps to its highest
valid enumerator (`69-71`, `81-83`, `93-95`); `play()` with no driver settles
instead of latching at progress 0 (`137-142`); `setReducedMotion(true)` stops an
in-flight run through the driver and then finishes it (`110-118`); `specFor()`
zeroes both duration and delay under reduced motion (`184-187`). Only G6.

**context3d.cpp GL branch** — `initializeGL()` null-checks `context_`, `context()`
and `functions()`, validates every allocation (`program_` link, VAO create,
buffer create) and unwinds the earlier resources on failure (`867-933`);
`resizeGL()` null-checks the context and functions (`935-945`); `paintGL()`
null-checks everything, wraps the body in try/catch, floors the pixel viewport at
1 (`956-960`), guards the aspect against a zero height (`974-976`), skips a
primitive with too few vertices before indexing it (`1026`, `1035`, `1043`),
and only binds/draws when `vertices` is non-empty (`1086-1088`); the vertex and
batch arrays are built from the same loop, so `batch.first`/`batch.count` cannot
desync from the buffer; `ensureOpenGLWidget()` refuses the `offscreen` platform
and returns early without creating a context-less widget (`1122-1141`); the
`#if`/`#else` shape leaves a no-op when OpenGL is compiled out, and
`effectiveBackend()` then stays `Software` (`459-464`).

---

## Cross-component patterns

The individual ids above are less useful than the shapes behind them: each shape
recurred, each is checkable mechanically, and each has a known-safe
counter-example already in the tree. The counts are "how many findings this pass
attributed to the pattern", not "how many sites exist".

### P-1. Emit, then touch `this` (2 findings: G1, G2)

A public handler emits a signal and then reads or writes a member. With a direct
connection the slot runs inside the `emit`, so a slot that deletes the sender —
the ordinary "remove this widget" reaction — leaves a dangling `this`.

- Broken: `Chip::activate()` (`chip.cpp:553-559`), `Anchor::activate()`
  (`anchor.cpp:157-165`).
- Safe, and the majority: `Button::mouseReleaseEvent` (`button.cpp:354-355`),
  `Button::keyReleaseEvent` (`366-367`), `CheckBox::applyState`
  (`checkbox.cpp:120-121`), `Clock::onTick` (`clock.cpp:508-509`),
  `Alert::close` (`alert.cpp:582-591`), `Banner::close` (`banner.cpp:388-396`),
  `Chip::mouseReleaseEvent`'s `closed()` (`chip.cpp:345-347`),
  `Anchor::setActiveIndex`/`removeAnchor`/`addAnchor` (`anchor.cpp:154`, `181`,
  `213`).

**Rule:** the `emit` is the last statement of the function, or the object is
re-checked after it. A `QPointer<T> self(this)` immediately before the emit and
`if (!self) return;` after it is the mechanical fix for the cases where work
must follow. This is worth a review checklist item because it cannot be seen
from the signal's declaration — only from the function body.

### P-2. A latch owned by one setter and released by none of the others (2 findings: G5, G6)

A boolean encodes "somebody else's state" and only one setter knows how to clear
it, so a different setter leaves the component stuck.

- `Badge::hiddenByCount_`: set by `setCount(0)` (`badge.cpp:79`), cleared only by
  `setCount(>0)` (`84-87`); `setText()` leaves it set.
- `ChartAnimation::running_`: set by `play()`/`beginRun()`, cleared by a frame
  `>= 1.0` or `finishRun()`; a NaN frame clears neither (`chart_animation.cpp:204-209`).
- The intentional counter-example, worth copying: `Alert::closedEmitted_` and
  `Banner::closedEmitted_` are both reset by the matching "the component came
  back" entry point (`alert.cpp:598-601`, `banner.cpp:360`).

**Rule:** every latch gets (a) one named owner, (b) an enumeration of the events
that clear it written next to its declaration, and (c) a reset in every setter
that makes the latched condition false again. "Set in one place, cleared in
another place, forgotten in a third" is the defect.

### P-3. Cache validity written before the cached resource is validated (2 findings: P2-1, P2-2)

Two mirror-image failures, both in `ChartBase`:

- the flag is set even though the resource failed to build
  (`staticLayerValid_ = true` after a null `QPixmap`, `chart_common.cpp:862`),
- or the flag stays set although an input it depends on changed
  (`chartCacheValid_` survives a `plotRect()` change, `chart_common.cpp:178-224`).

**Rule:** a cache has exactly two pieces of state — the value and a key — and
`valid` may only be true when *both* the build succeeded and the key still
matches every input the value reads. Write the flag in the same statement group
as the successful build, and derive the key as the superset of the cache's real
inputs (for `ChartBase` that includes the layout rectangle, not just size and
theme).

### P-4. State committed before the operation that can throw (2 findings: P2-5, P1-1)

The setter has already advanced its members when a later step throws, so the
object is observably half-updated.

- `ConnectivityChart::setMatrix`/`ChordDiagram::setMatrix`: `names_` replaced,
  `values_` cleared and refilled, `size_` assigned last — a `bad_alloc` in the
  loop leaves the three disagreeing (`connectivity_chart.cpp:145-168`).
- `Avatar::setUrl`/`setImageLoader` → `beginLoad`: `loading_ = true` is committed
  before the user callback that can throw (`avatar.cpp:297`, `544-552`).

**Rule:** build into locals, commit at the end (strong exception guarantee), or
make the failure path restore the previous state. For a boolean that gates a UI
affordance, a `qScopeGuard`-based "unless I reached the success line, clear it"
is enough.

### P-5. Hit-test geometry diverges from paint geometry (1 finding: G4)

`Banner` paints and lays out the close affordance conditionally on
`closeVisible_` (`banner.cpp:285`, `477`) but hit-tests the same rectangle
unconditionally (`254-267`, `499-517`); `Alert` avoids this by making the
geometry function itself return an empty rect when the affordance is off
(`alert.cpp:408-411`).

**Rule:** one function produces the active rectangle, and it returns an empty
`QRectF` whenever the affordance is not painted. Paint, layout, press, release
and hover all consume that one function. The same discipline already protects
`Anchor::anchorRect`/`indicatorRect` and `Chip::closeRectFor`.

### P-6. A public `qreal` setter accepts non-finite input that becomes an `int` later (1 finding: G3)

`Affix::setScrollOffset`/`setTargetOffset` store NaN/Inf and `qRound` consumes
it (`affix.cpp:169-171`). Near-misses and benign accidents in the same class:
`Anchor::setScrollOffset(NaN)` silently selects index 0 instead of the previous
one (`anchor.cpp:129-141`, `286-288`); `Button::setIconSize(NaN)` becomes `0`
only because `dp > 0.0` is false (`button.cpp:170`); `Affix::setThreshold(NaN)`
becomes `0` only because `std::max(0.0, NaN)` returns its first argument
(`affix.cpp:114`). `ChartAnimation::setRunProgress` (G6) is the same shape
without the `int` conversion.

**Rule:** a public setter that takes a `qreal` validates `std::isfinite` at the
boundary — or the paint/arrange path uses a `qRoundFinite()` helper that clamps
to the `int` range and maps non-finite to 0. Relying on `std::max`/comparison
order to reject NaN works by accident and is the kind of thing a refactor
silently breaks.

### P-7. A QObject child holds a raw back-pointer to its non-QObject owner (1 finding: G7)

`Context3DGlWidget::context_` (`context3d.cpp:824`, used in the destructor at
`849-850`) and, benignly, the `Animator` children that call back into a
`ChartBase`/`ContainerTransform`. `QPointer` cannot help because the owner is not
a `QObject`; the invariant is pure destruction order and is invisible from the
child's header.

**Rule:** if a child must call into a non-QObject owner, the owner exposes a
detach step that nulls the back-pointer, and the owner calls it before any path
that could let the child outlive it (`deleteLater`, reparenting, a deferred
delete in a destructor).

### Review checklist distilled from the seven patterns

1. Does every `emit` come last — or is there a self-guard after it?
2. Does every boolean latch have an owning setter and a listed release event?
3. Is every cache `valid` flag written only with a successful build, keyed on
   *all* of its inputs?
4. Does every setter commit its members only after the last throwing step?
5. Does every conditional affordance have one geometry producer that returns
   empty when inactive?
6. Is every public `qreal`/`double` argument checked with `std::isfinite` before
   it reaches `qRound`, an index, or a divisor?
7. Does every non-QObject owner of a QObject child have a detach step?

---

## Updated counts

Superseding the counts at the top of the report; no existing finding was
renumbered or changed.

- **P0: 3** — G1 `chip.cpp:553` (use-after-free), G2 `anchor.cpp:157`
  (use-after-free), G3 `affix.cpp:171` (UB; see its severity note).
- **P1: 3** — P1-1 `avatar.cpp:544`, P1-2 `autocorrelation_chart.cpp:244`,
  G4 `banner.cpp:254`.
- **P2: 8** — P2-1 `chart_common.cpp:868`, P2-2 `chart_common.cpp:178`,
  P2-3 `core/src/i18n.cpp:119`, P2-4 `context3d.cpp:100` (scope narrowed to
  "document/assert the ordering" by G7), P2-5
  `connectivity_chart.cpp:139` + `chord_diagram.cpp:132`, G5 `badge.cpp:56`,
  G6 `chart_animation.cpp:204`, G7 `context3d.cpp:849`.
- **Total: 14** (7 from the first pass, 7 from gap closure).

Coverage after the second pass: all 31 `a*`/`b*`/`c*` sources have now been read
end-to-end except the middle of `bar_chart.cpp`, `box_plot_chart.cpp`,
`bode_chart.cpp`, `calendar.cpp`, `carousel.cpp`, `chord_diagram.cpp`,
`coherence_chart.cpp`, `connectivity_chart.cpp`, `context3d.cpp` (software
renderer only) and `bottom_sheet.cpp`, whose public paths and every
index/ownership/exception site were audited but whose full bodies were not read
line by line. Those are the honest remaining gaps.


