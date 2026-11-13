# Robustness audit — components `d`–`h`, theme, tokens

Read-only audit. No source file was modified; this document is the only artefact.

**Repository:** `D:\project\periodicity-cmake` (C++20, Qt 6.8 Widgets).
**Scope:** `components/src/{d,e,f,g,h}*.cpp` (16 files), the matching
`components/include/md/components/*.h`, and `theme/` + `tokens/` (include and src).
**Note on the stated file count:** the glob `[defgh]*.cpp` matches 16 translation
units, not 19 — `date_picker`, `datetime_picker`, `dialog`, `divider`, `drawer`,
`dropdown`, `epicycle_chart`, `fab`, `font`, `form`, `funnel_chart`, `gpu_canvas`,
`guide`, `heatmap_chart`, `hilbert_chart`, `histogram_chart`. All 16 are covered.

**Method:** full read of every in-scope source and header, plus the base classes
(`view/`, `view_group`) needed to evaluate lifetime and paint contracts.

---

## Summary

| id | file:line | severity | issue | fix |
|----|-----------|----------|-------|-----|
| DRW-1 | `components/src/drawer.cpp:755-763` | **P0** | `activateItem()` keeps touching `this` after `selectionChanged` may have deleted the drawer — use-after-free | Create the `QPointer` guard before `applySelection()` and bail out after every emission |
| THM-1 | `theme/src/theme.cpp:283-286` | **P0** | `setApplicationTheme()` iterates raw `QWidget*` from `allWidgets()` while `themeChanged()` slots may delete widgets — use-after-free | Snapshot into `QList<QPointer<QWidget>>` and re-check each entry |
| THM-2 | `theme/src/theme.cpp:57-67`, `tokens/include/md/tokens/color_scheme.h:26-95` | P1 | A palette is accepted on 3 valid roles; a partially built `ColorScheme` reaches painting with ~40 invalid `QColor`s (silently black) | Fill every invalid role from the baseline, or require a complete scheme |
| DP-1 | `components/src/date_picker.cpp:106-110, 126-130` | P1 | `setDisplayedMonth()` emits `displayedMonthChanged` mid-mutation; the following `requestUpdate()`/emit run on a possibly destroyed picker | Emit after the last member write, or guard with a `QPointer` |
| FNT-1 | `components/src/font.cpp:538-549` | P1 | `notifyChanged()` emits `changed()` and then dereferences `host_`; a slot deleting the host destroys `this` (and any child deletion invalidates the snapshot) | `QPointer<Font>` self-guard plus a null host check, and a `QPointer` child snapshot |
| FNT-2 | `components/src/font.cpp:380-391` | P1 | `FontSelector::activate()` calls the host callback and only then calls `requestUpdate()` on `this` | Update before invoking the callback (or copy the callback and invoke last) |
| HIL-1 | `components/src/hilbert_chart.cpp:244-299` | P1 | `setSignal()` runs an unbounded O(N²) direct DFT on caller data; large input freezes the GUI thread | Cap the sample count, or move the transform off the paint/host thread |
| HST-1 | `components/src/heatmap_chart.cpp:205-214, 313-329` | P1 | Every `setValue()` rebuilds the presence series and re-scans the whole matrix: filling a matrix cell-by-cell is O(N²·M²) | Keep a dirty flag, resync at most once per paint |
| DTP-1 | `components/src/datetime_picker.cpp:152-165` | P1 | `step()` adds seconds to the `QTime` only; 23:59 + 1 min becomes 00:00 of the *same* day | Step the `QDateTime` (or clamp at the day edge) |
| FRM-1 | `components/src/form.cpp:54-57` | P1 | `Form::measureOverride()` measures every child with a `1e9` width bound and lets that leak into the Form's own natural width | Measure children at the Form's known width; never publish `1e9` |
| DD-1 | `components/src/dropdown.cpp:188-193` | P2 | Reparenting to the window skips `ViewGroup` bookkeeping, so a former parent keeps arranging a foreign child | Remove from the owning `ViewGroup` before `setParent()`, as `Drawer` does |
| DRW-2 | `components/src/drawer.cpp:341-349, 436-456` | P2 | A modal drawer permanently leaves its layout group on the first `open()`; `close()` never restores the parent | Remember the original parent/group and re-attach on close, or document the constraint |
| GPU-1 | `components/src/gpu_canvas.cpp:94-121` | P2 | `setView()` detaches the old view with `setParent(nullptr)` (a top-level), losing the canvas's ownership without transferring it | Hide and keep the child, or hand ownership back explicitly with a documented contract |
| TS-1 | `theme/src/theme_colours.cpp:149-194` | P2 | `Theme::colour("typo")` returns an invalid `QColor` with no diagnostic | Warn once and/or return a documented fallback role |
| TS-2 | `tokens/include/md/tokens/motion.h:19-32`, `tokens/src/motion.cpp:12-19` | P2 | `CubicBezier` accepts NaN / out-of-range control points; `valueForProgress` then yields non-monotonic or NaN progress | Validate in `toEasingCurve()` and fall back to `Linear` |
| DP-2 | `components/src/date_picker.cpp:114-118` | P2 | `setDateRange()` silently ignores an invalid or inverted range; the caller cannot tell | Log and return `bool`, or add `trySetDateRange()` |
| DT-2 | `components/src/datetime_picker.cpp:42, 211-222`; `datetime_picker.h:89` | P2 | The calendar child is held by a raw pointer although `datePicker()` is public; deleting it dangles every later `syncCalendar()` | Hold `QPointer<DatePicker>` |
| FRM-2 | `components/src/form.cpp:34-41` | P2 | An editor without a `"value"` property silently validates as an invalid `QVariant` (reported as "empty") | Check `indexOfProperty("value")` and warn |
| FAB-1 | `components/src/fab.cpp:107-118` | P2 | `setIconSize(+inf)` is accepted (`dp > 0.0`); the infinite dp reaches `QRectF` and `QIcon::setSize` | Require `std::isfinite` and clamp to a sane maximum |

Counts: **P0 = 2, P1 = 8, P2 = 9** (19 findings).

---

## P0 — crash or undefined behaviour reachable from the public API

### DRW-1 — `Drawer::activateItem()` uses the drawer after `selectionChanged`

`components/src/drawer.cpp:750-767`

```cpp
void Drawer::activateItem(int index) {
    if (index < 0 || index >= itemCount()) return;
    applySelection(index, true);       // 755: emits selectionChanged(selected_)
    ensureItemVisible(index);          // 756: reads this
    const QString label = itemLabel(index);  // 758: reads this
    const QPointer<Drawer> guard(this);      // 762: guard created too late
    emit itemActivated(index, label);
    if (!guard.isNull() && isModal() && open_) close();
}
```

Reachable path: `mouseReleaseEvent()` (`drawer.cpp:1074-1075`) or
`keyPressEvent()` Return/Space (`drawer.cpp:1143-1148`) → `activateItem()` →
`applySelection()` → `emit selectionChanged()` (`drawer.cpp:747`). A host slot
that navigates and destroys/recreates the drawer on selection — a normal pattern
for a navigation drawer — frees `this`. `ensureItemVisible()` then calls
`itemRect()` → `entryForItem()` → `entries_.at()` on freed storage, and
`itemLabel()` does the same. The existing `QPointer` guard proves the hazard is
known; it is simply installed after the two offending calls
(**MEM50-CPP**, **ES.21** — declare the guard where it is needed, **I.11**).

Patch sketch:

```cpp
void Drawer::activateItem(int index) {
    if (index < 0 || index >= itemCount()) return;
    const QPointer<Drawer> guard(this);     // before the first emission
    applySelection(index, true);
    if (guard.isNull()) return;
    ensureItemVisible(index);
    const QString label = itemLabel(index);
    emit itemActivated(index, label);
    if (!guard.isNull() && isModal() && open_) close();
}
```

The same shape recurs in `DatePicker::setSelectedDate()` (DP-1) and
`FontSelector::activate()` (FNT-2); a project-wide rule — "a signal is the last
statement of a mutating member" — removes the class of defect.

### THM-1 — `Theme::setApplicationTheme()` walks a stale `allWidgets()` list

`theme/src/theme.cpp:283-291`

```cpp
const QWidgetList widgets = QApplication::allWidgets();
for (QWidget* widget : widgets) {
    if (ThemeScope::find(widget) != nullptr)            // dereferences widget
        ThemeScope::refreshAdaptersInSubtree(widget, nullptr);
}
```

`QWidgetList` is a list of raw pointers. `refreshAdaptersInSubtree()` reaches
`ThemeScope::refreshInternal()` → `applyTheme()` → `emit themeChanged()`
(`theme_scope.cpp:198-203`), i.e. arbitrary application code. A slot that
rebuilds or deletes a widget subtree during a theme switch — common for panels
that own theme-dependent children — leaves every deleted widget (and each of its
descendants, which are also in the list) dangling. The loop's next
`ThemeScope::find(widget)` calls `host->findChild<...>()` on freed memory
(**MEM50-CPP**, **R.1**).

Patch sketch:

```cpp
QList<QPointer<QWidget>> widgets;
for (QWidget* widget : QApplication::allWidgets())
    widgets.append(widget);

for (const QPointer<QWidget>& widget : widgets) {
    if (widget != nullptr && ThemeScope::find(widget) != nullptr)
        ThemeScope::refreshAdaptersInSubtree(widget, nullptr);
}
```

Collecting the *scopes* as `QPointer<ThemeScope>` first would be stronger still,
because `install()` on the same host during a slot deletes and replaces the
scope object (the widget itself survives, so `QPointer<QWidget>` is sufficient
today, but it couples this loop to that detail).

---

## P1 — wrong output, swallowed failure, or hang

### THM-2 — a three-role palette check lets invalid `QColor`s reach the painter

`theme/src/theme.cpp:57-67`, `tokens/include/md/tokens/color_scheme.h:26-95`

```cpp
bool hasUsablePalette(const ColorScheme& colours) {
    return colours.surface.isValid() && colours.onSurface.isValid()
        && colours.primary.isValid();
}
```

`ColorScheme` declares ~45 `QColor` members with no default member initialisers,
so a default-constructed (or hand-filled) scheme has invalid colours. The theme
accepts it as soon as three roles are set. Every in-scope component then paints
with the invalid ones, for example `Dialog::paintContent()` uses
`colours.scrim`, `colours.shadow` and `colours.surfaceContainerHigh`
(`dialog.cpp:573, 591, 602`), `Drawer::paintRows()` uses
`colours.secondaryContainer`/`error` (`drawer.cpp:980, 1000`), and
`Fab::syncAppearance()` uses `primaryContainer`/`onPrimaryContainer`
(`fab.cpp:192-197`). `QPen`/`QBrush` built from an invalid `QColor` paint black,
silently. `withAlpha()`/`setAlphaF()` on an invalid colour also mutate nothing,
so a "32 % scrim" becomes an opaque black rectangle. The same weak predicate is
repeated in `Theme::toPalette()` (`theme.cpp:192-193`) and the
`colourPrimaryDark()`/`colourSecondaryDark()` helpers compute
`blend(invalid, black, …)` (`theme_colours.cpp:124-142`) — a *valid* colour
derived from an invalid one, which is the silent-failure case the audit brief
asks about (**ES.20** — always initialise; **E.1** — pick an explicit
error-handling strategy).

Patch sketch — complete the scheme role-by-role instead of accepting it whole:

```cpp
ColorScheme sanitisedColours(ColorScheme colours) {
    const ColorScheme base = ColorScheme::baselineLight();
    const std::pair<QColor*, QColor> roles[] = {
        {&colours.surface, base.surface}, {&colours.onSurface, base.onSurface},
        {&colours.primary, base.primary}, {&colours.primaryContainer, base.primaryContainer},
        // … every role the pipeline reads
    };
    for (auto& [role, fallback] : roles)
        if (!role->isValid()) *role = fallback;
    return colours;
}
```

`ColorScheme` should also give every member an initialiser
(`QColor primary = QColor(Qt::magenta);` or a named sentinel) so "unset" is
observable rather than a side effect of `QColor`'s invalid state.

### DP-1 — `DatePicker` emits in the middle of a mutation

`components/src/date_picker.cpp:104-111` and `:122-130`

`setSelectedDate()` writes `selected_`, calls `setDisplayedMonth()`, and only
then calls `requestUpdate()` and emits `selectedDateChanged()`. But
`setDisplayedMonth()` emits `displayedMonthChanged` at line 166. A slot that
rebuilds or deletes the picker (a calendar embedded in a dismissible panel, the
normal case) frees `this`; lines 109-110 then call a member on freed memory.
`setDateRange()` has the same ordering (126-130). The base class itself avoids
exactly this at `view.cpp:453` — it copies the theme before running component
code — so the pattern is inconsistent with the framework's own contract
(**MEM50-CPP**, **ES.21**).

Patch sketch: reorder so the last statement is the only emission, and drop the
redundant `requestUpdate()` that follows a call which already repaints:

```cpp
void DatePicker::setSelectedDate(QDate date) {
    if (!date.isValid()) return;
    const QDate next = clampDate(date);
    const bool changed = next != selected_;
    selected_ = next;
    setDisplayedMonth(next);            // may emit displayedMonthChanged
    if (!changed) return;
    requestUpdate();
    emit selectedDateChanged(selected_); // last statement
}
```

For full safety, wrap the body in `const QPointer<DatePicker> self(this);` and
return after the first emission if `self.isNull()`.

### FNT-1 — `Font::notifyChanged()` outlives the host

`components/src/font.cpp:538-549`

```cpp
void Font::notifyChanged() {
    emit changed();                       // arbitrary slots
    if (host_ == nullptr) return;
    QEvent event(QEvent::FontChange);
    QApplication::sendEvent(host_.data(), &event);
    for (QWidget* child : host_->findChildren<QWidget*>())
        QApplication::sendEvent(child, &event);
}
```

`Font` is a `QObject` child of `host_`, so a `changed()` slot that deletes the
host also destroys `this`; the function then continues on a freed object. Even
without deleting the host, `findChildren()` returns raw pointers and a
`FontChange` handler on an early child can delete a later one — the loop then
sends to freed widgets. Note the class already relies on `QPointer<QWidget>
host_` elsewhere (`font.h:116`), so the guard is available (**MEM50-CPP**,
**F.6** — a callback may not re-enter a half-destroyed object).

Patch sketch:

```cpp
void Font::notifyChanged() {
    const QPointer<Font> self(this);
    const QPointer<QWidget> host = host_;
    emit changed();
    if (self.isNull() || host.isNull()) return;

    QList<QPointer<QWidget>> targets;
    targets.append(host);
    for (QWidget* child : host->findChildren<QWidget*>())
        targets.append(child);

    QEvent event(QEvent::FontChange);
    for (const QPointer<QWidget>& target : targets)
        if (target != nullptr) QApplication::sendEvent(target, &event);
}
```

### FNT-2 — `FontSelector::activate()` runs host code before finishing

`components/src/font.cpp:380-391`

```cpp
active_ = index;
highlighted_ = index;
current_ = rows_.at(index).value;
if (onActivated_) onActivated_(current_);   // 388-389: host may close/delete us
requestUpdate();                            // 390: member call on `this`
```

Reachable path: the selector returned by `Font::createSelector()` is placed in a
host surface (documented use at `font.h:102-105`); choosing a row runs the host
callback, which may hide/delete the selector, and then `requestUpdate()` touches
the dead widget. In the `openSelector()` path the callback also calls
`guard->close()`, which is safe today only because `Modal::close()` defers
deletion — an implementation detail the selector does not own.

Patch sketch: copy the callback, update first, invoke last.

```cpp
requestUpdate();
if (!onActivated_) return;
const auto handler = onActivated_;   // copy; the call may destroy *this
handler(current_);
```

### HIL-1 — unbounded O(N²) transform on caller-supplied data

`components/src/hilbert_chart.cpp:170-176, 244-299`

`setSignal(std::vector<double>)` copies the vector and immediately calls
`computeAnalyticSignal()`, whose forward and inverse DFT are both O(N²) with
`std::complex<double>` work per sample. A caller passing 50 000 samples performs
2.5·10⁹ complex multiply-adds on the GUI thread; 200 000 samples is minutes.
Nothing caps the input, and `paintEnvelope()`/`sampleAt()` then walk the result
linearly on every mouse move on top of that. This is the "loop whose bound is
caller-controlled" case from the brief (**E.1** — define a contract for
oversized input; **INT32-C** — `static_cast<int>(signal_.size())` at line 244
narrows `size_t` to `int` without a range check, so a vector above `INT_MAX`
turns into a negative `count` and silently disables the whole chart).

Patch sketch:

```cpp
void HilbertChart::setSignal(std::vector<double> x) {
    constexpr std::size_t kMaxSamples = 4096;   // 4096² ≈ 1.7·10⁷ ops, ~10 ms
    if (x.size() > kMaxSamples) {
        qCWarning(mdChart) << "HilbertChart: truncating" << x.size()
                           << "samples to" << kMaxSamples;
        x.resize(kMaxSamples);
    }
    signal_ = std::move(x);
    computeAnalyticSignal();   // or schedule on a worker and swap under a mutex
    …
}
```

Add the same guard for `count` before the `int` cast, and expose the limit as a
setter so a host can raise it deliberately.

### HST-1 — `HeatmapChart::setValue()` re-scans the matrix per cell

`components/src/heatmap_chart.cpp:205-214` and `:313-329`

Every `setValue()` calls `syncPresenceSeries()`, which calls `hasHeatValues()`
(O(rows·columns)) and then `ChartBase::setSeries()` — which itself unions all
points, invalidates the palette (`chart_common.cpp:261`) and the pixel cache,
refreshes the legend signature and calls `math_->setDataBounds()` (another scan).
Filling an `R×C` matrix cell-by-cell therefore costs O(R²·C²): 200×200 is
1.6·10⁹ finite checks plus 40 000 series rebuilds, and 500×500 exceeds 10¹⁰ —
seconds to minutes of frozen UI. The public API invites exactly this loop (there
is no per-cell update path other than `setValue`), and a chart whose data arrives
incrementally hits it per sample.

Patch sketch: defer the mirror to the next paint, and only when presence changed.

```cpp
void HeatmapChart::setValue(int row, int column, qreal cellValue) {
    … checks …
    const bool wasPresent = hasHeatValues();
    values_[row][column] = cellValue;
    if (hasHeatValues() != wasPresent) presenceDirty_ = true;
    requestUpdate();
}

void HeatmapChart::paintPlot(QPainter& painter, const QRectF& plot) {
    if (presenceDirty_) { presenceDirty_ = false; syncPresenceSeries(); }
    …
}
```

(`hasHeatValues()` itself should become O(1) with a `finiteCount_` member.)

### DTP-1 — time stepping silently keeps the date

`components/src/datetime_picker.cpp:152-165`

```cpp
const QTime next = value_.time().addSecs(seconds * amount);
applyValue(QDateTime(value_.date(), next));
```

`QTime::addSecs()` wraps modulo 24 h. At 23:59 pressing Up on the minute segment
yields `00:00` of the same day instead of `00:00` the next day; at 00:00 pressing
Down yields `23:59` of the same day. The date is the other half of the widget's
value, so the displayed value and the calendar disagree with the arithmetic the
user just performed. The clamp in `applyValue()` can also pin the result to the
range, hiding the wrap entirely. This is the day-boundary case the brief calls
out (**E.1** — the component needs an explicit rule for boundary stepping).

Patch sketch (datetime arithmetic, the correct model for a `QDateTime` control):

```cpp
void DateTimePicker::step(int amount) {
    if (amount == 0) return;
    int seconds = field_ == 1 ? 60 : (field_ == 2 ? 1 : 3600);
    applyValue(value_.addSecs(seconds * amount));   // QDateTime carries the date
}
```

If the intended semantics is "time of day only", then clamp at the ends instead
(`if (next < QTime(0,0) || next > …) return;`) and document it; silently
changing the date is the one behaviour that must not happen.

### FRM-1 — `1e9` leaks out of `Form::measureOverride()`

`components/src/form.cpp:54-58`

```cpp
for (View* child : children) {
    child->measure(MeasureSpec::atMost(w.mode()==MeasureSpec::Mode::Unspecified?1e9:w.size()), …);
    height += child->measuredSize().height();
    width = std::max(width, child->measuredSize().width());
}
setMeasuredSize({w.resolve(width), h.resolve(height)});
```

`MeasureSpec::atMost()` is adopted as the measured length (`divider.cpp:158-164`
documents this contract), so a `FormItem` measured with `AtMost(1e9)` reports a
1e9-pixel width, and `width` then carries that value into the Form's own natural
size. A `Form` measured with an unspecified width (the normal way a vertical
scroll container measures its content) reports 1e9 → an enormous scroll range, a
1e9-pixel child geometry, and layout that never wraps. The magic constant also
defeats any density-aware wrapping a child would otherwise do.

Patch sketch:

```cpp
const qreal bound = w.mode() == MeasureSpec::Mode::Unspecified
                        ? density.dp(kFormPreferredWidthDp)   // e.g. 320 dp
                        : w.size();
for (View* child : children) {
    child->measure(MeasureSpec::atMost(bound), MeasureSpec::unspecified());
    height += child->measuredSize().height();
    width = std::max(width, std::min(child->measuredSize().width(), bound));
}
```

---

## P2 — hardening

### DD-1 — `Dropdown` reparents without telling its `ViewGroup`

`components/src/dropdown.cpp:188-193`

`open()` does `setParent(host_)` directly. If the dropdown was a child of a
`ViewGroup`, that group's `views_` still contains it, so `arrangeChildren()` keeps
calling `arrange()` on a widget that is no longer its child — the popup jumps back
to the group's geometry on the next layout pass. `Drawer::open()` handles this
explicitly (`drawer.cpp:345-348`, `group->removeView(this)`), and
`ViewGroup::addView()` also removes from the old group (`view_group.cpp:50-51`),
so the contract exists; `Dropdown` is the outlier (**I.11** — ownership transfer
must be explicit).

```cpp
if (parentWidget() != host_) {
    if (auto* group = qobject_cast<ViewGroup*>(parentWidget()))
        group->removeView(this);
    setParent(host_);
}
```

### DRW-2 — a modal drawer never returns to its layout group

`components/src/drawer.cpp:341-349`, `:364-396`, `:436-456`

`open()` removes the drawer from its `ViewGroup` (via `removeView`, which also
reparents it to `nullptr` first — `view_group.cpp:76-81`) and reparents it to the
window. `close()`/`finishTransition()` only hide it. A host that switches
`Mode::Modal` → `Mode::Standard` afterwards gets a drawer that is not in any
layout and keeps the window-wide geometry it was given as an overlay. If the
drawer is meant to be a layout member in Standard mode, the original parent and
group must be remembered and restored (**E.6**, **I.11**).

```cpp
// open(), before removing:
restoreParent_ = parentWidget();
restoreGroup_  = qobject_cast<ViewGroup*>(parentWidget());
…
// close(), after hiding:
if (restoreGroup_ != nullptr && !restoreGroup_->views().contains(this))
    restoreGroup_->addView(this);
else if (restoreParent_ != nullptr)
    setParent(restoreParent_);
```

If the overlay behaviour is deliberate and permanent, say so in the header
(`drawer.h:24`) — today the only hint is a comment about "leaving a container".

### GPU-1 — `GpuCanvas::setView()` orphans the previous view

`components/src/gpu_canvas.cpp:94-121`

```cpp
if (view_ != nullptr) {
    view_->removeEventFilter(this);
    view_->setAttribute(Qt::WA_DontShowOnScreen, false);
    if (view_->parentWidget() == this)
        view_->setParent(nullptr);   // becomes a top-level, ownership undefined
}
```

`setParent(nullptr)` promotes the view to a top-level widget; the canvas then
forgets it (`view_ = view`) and `~GpuCanvas()` no longer deletes it. A host that
lets the canvas own its view (the constructor overload takes a raw `View*` and
reparents it — `gpu_canvas.cpp:111`) leaks a hidden top-level per `setView()`
call. Either keep the view as a child (and only toggle the attribute), or return
the old view to the caller and document the transfer (**E.6** — RAII ownership;
**I.11**).

### TS-1 — an unknown colour attribute yields an invalid `QColor`

`theme/src/theme_colours.cpp:149-194`

`Theme::colour()` normalises the key and falls through to `return QColor();`.
The header documents it (`theme.h:124`), but a host that writes
`theme.colour("colorSurfaceContainerHigh")` — a role that exists on
`ColorScheme` but not in this attribute table — gets an invalid colour and paints
black, with no way to distinguish "unknown" from "black". This is the "missing
token key returns a default silently" case in the brief (**E.1**).

```cpp
qCWarning(mdTheme) << "Theme::colour(): unknown attribute" << attribute;
return QColor();
```

and add the surface-container/outline/inverse roles the components actually use,
or expose a `std::optional<QColor>` / `bool colour(const QString&, QColor*)`
overload so the failure is testable.

### TS-2 — `CubicBezier` is unvalidated

`tokens/include/md/tokens/motion.h:19-32`, `tokens/src/motion.cpp:12-19`

`toEasingCurve()` feeds the four numbers straight into
`QEasingCurve::addCubicBezierSegment()`. A `MotionScheme` built by hand (or read
from a store) can carry `NaN` or `x1/x2` outside `[0,1]`; the curve then returns
`NaN`/non-monotonic values for `valueForProgress()`. `Drawer::easedProgress()`
(`drawer.cpp:463-469`) puts that straight into `progress_`, so the panel geometry
and the scrim become `NaN` for the whole transition. It terminates (the timer
uses linear elapsed time), but nothing is painted correctly meanwhile, and Qt's
raster engine receives `NaN` rectangles.

```cpp
QEasingCurve CubicBezier::toEasingCurve() const {
    if (!std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(x2)
        || !std::isfinite(y2) || x1 < 0.0 || x1 > 1.0 || x2 < 0.0 || x2 > 1.0)
        return QEasingCurve(QEasingCurve::Linear);   // or qCWarning + assert in debug
    …
}
```

### DP-2 — `DatePicker::setDateRange()` swallows an invalid range

`components/src/date_picker.cpp:114-118`

```cpp
if (!minimum.isValid() || !maximum.isValid() || minimum > maximum)
    return;   // no signal, no log, no return value
```

A caller that computes bounds at runtime (a booking window, a filter) has no way
to detect that its range was rejected; the picker keeps the previous
`1..9999` default and silently accepts every date. The header documents the
no-op (`date_picker.h:55`), so this is hardening rather than a defect
(**E.1** — make the failure visible).

```cpp
bool DatePicker::setDateRange(QDate minimum, QDate maximum) {
    if (!minimum.isValid() || !maximum.isValid() || minimum > maximum) {
        qCWarning(mdWidgets) << "DatePicker: rejected range" << minimum << maximum;
        return false;
    }
    …
    return true;
}
```

### DT-2 — the calendar child is a raw pointer

`components/src/datetime_picker.cpp:42`, `:211-222`, `datetime_picker.h:89`

`calendar_` is created with `this` as parent and exposed through
`datePicker()`. Neither `syncCalendar()` nor `arrangeOverride()` can detect that
the child was deleted — their null checks only cover the constructor window.
Deleting the returned child (a legitimate operation for a caller that wants to
replace it) leaves `datePicker()` and every later `applyValue()` →
`syncCalendar()` dereferencing freed memory (**MEM50-CPP**, **I.11**).
`DateTimePicker` should mirror `GpuCanvas::view_` / `Font::host_` and store
`QPointer<DatePicker>`; that also makes the null checks in `syncCalendar()`
meaningful.

### FRM-2 — a missing `"value"` property validates as "empty"

`components/src/form.cpp:34-41`

```cpp
if (auto* line = qobject_cast<QLineEdit*>(editor_)) value = line->text();
else if (editor_) value = editor_->property("value");
```

`QObject::property()` returns an invalid `QVariant` for a name that does not
exist. A `required` rule then reports "missing" for an editor that actually holds
a value (a `QComboBox`, a custom `View` with a differently named property), and a
`matches()` rule silently fails. No diagnostic distinguishes the two cases
(**E.1**).

```cpp
if (editor_ != nullptr) {
    if (editor_->metaObject()->indexOfProperty("value") >= 0)
        value = editor_->property("value");
    else
        qCWarning(mdWidgets) << "FormItem: editor has no 'value' property:"
                             << editor_->metaObject()->className();
}
```

### FAB-1 — `setIconSize()` accepts infinity

`components/src/fab.cpp:107-118`

`const qreal clamped = dp > 0.0 ? dp : 0.0;` — `+inf` passes, `NaN` maps to 0.
An infinite value reaches `iconSizeDp_`, then `density.dp(iconSizeDp_)` and
`iconDrawable_.setSize(QSizeF(inf, inf))`; the icon engine converts that to pixels
and, depending on the backend, allocates or converts out of range
(**INT32-C** — float-to-integer conversion outside the representable range is
undefined).

```cpp
const qreal clamped = std::isfinite(dp) && dp > 0.0 ? std::min(dp, 512.0) : 0.0;
```

The same pattern already exists (correctly) in `Divider::nonNegativeLength()`
(`divider.cpp:34-37`) and `FunnelChart::setGap()` (`funnel_chart.cpp:159-167`);
`Fab` is inconsistent with its siblings.

---

## Checked and found sound

These were examined against the audit criteria and are reported as sound, so a
later reader does not have to re-check them:

* **Painter state balance.** Every in-scope `paintContent()`/`paintPlot()` path
  pairs `save()`/`restore()` on all exits: `Dialog` (early `restore()` before the
  empty-surface return, `dialog.cpp:575-579`), `Drawer` (two nested saves, both
  restored, `drawer.cpp:822/832, 869-875, 878-917`), `Dropdown`, `Divider`,
  `GuideView`, `Fab`, and the charts. `FunnelChart`, `HeatmapChart`,
  `EpicycleChart` and `HilbertChart` additionally use an RAII `PainterStateGuard`
  so an exception taken inside the `try` still restores (`heatmap_chart.cpp:63-76`
  documents the reasoning). No unbalanced path found.
* **`QPainter` device validity.** All painters are constructed in a
  `paintEvent`/`paintGL` on a live widget (`view.cpp:443`, `gpu_canvas.cpp:383`);
  `Divider::painterDevicePixelRatio()` tolerates a null device anyway
  (`divider.cpp:39-47`). No painter is created on an empty `QImage`/`QPixmap`.
* **Division by zero and empty ranges.** `FunnelChart::stagePolygon()` guards
  `stageLength > 0` and `count > 0` (`funnel_chart.cpp:239-241`);
  `HistogramChart::bins()` guards `count ≥ 1` and `width > 0` before dividing
  (`histogram_chart.cpp:401-406`); `HeatmapChart` guards `rows_/columns_ > 0`
  before both divisions (`heatmap_chart.cpp:350-353, 402-405, 480-481`);
  `DatePicker` clamps every cell dimension with `std::max(0.0, …)` and skips empty
  cells (`date_picker.cpp:274-280, 460-464`); `EpicycleChart::radiusBound()` has a
  `1e-9` floor (`epicycle_chart.cpp:347-350`).
* **Indexing on possibly empty data.** `FunnelChart::stagePercentage()` returns
  before `points.first()` unless `stageCount() > 0` (`funnel_chart.cpp:197-206`);
  `HistogramChart` returns `{}` for an empty sample set and clamps every bin index
  (`histogram_chart.cpp:385-387, 413-417`); `HilbertChart::sampleAt()` clamps to
  `count-1` and `hasAnalyticData()` requires `size ≥ 4`
  (`hilbert_chart.cpp:198-202, 613-617`); `Drawer::entryForItem()`/`entryRect()`
  bound-check every public index (`drawer.cpp:625-658`).
* **`QDate`/`QTime` validity.** Every `DatePicker` public setter rejects invalid
  dates, every rectangle helper returns an empty rect for an invalid date, and
  keyboard arithmetic (`addDays/addMonths/addYears`) is re-validated before use
  (`date_picker.cpp:87-102, 259-266, 695-698`). `DateTimePicker::clampDateTime()`
  only ever receives values that passed `isValid()` (`datetime_picker.cpp:196-201`).
  `QDate::currentDate()` is read once for the initial selection and once for the
  "today" marker; both are valid by construction.
* **Timer lifetime.** `Drawer`'s `QTimer` is a child of the widget and is stopped
  in `setMode()`, `animateTo()` (zero duration) and `finishTransition()`
  (`drawer.cpp:108-128, 158-159, 436-439`); no timer survives the drawer, and the
  timeout lambda cannot outlive it because the timer is destroyed first.
* **Exceptions at the Qt boundary.** Chart paint/measure/mouse entry points catch
  `std::exception` and `...` with a `qWarning` diagnostic
  (`funnel_chart.cpp:457-461`, `heatmap_chart.cpp:629-633`,
  `hilbert_chart.cpp:701-705`, `histogram_chart.cpp:546-556`);
  `ThemeScope::refreshInternal()` isolates a throwing adapter with a diagnostic
  (`theme_scope.cpp:174-186`); `applyTheme()` isolates a throwing `themeChanged()`
  slot and then checks `QPointer<ThemeScope> self` before touching the object
  (`theme_scope.cpp:196-215`).
* **`ThemeScope` lifetime.** `hostDestroyed_` is set from the host's `destroyed()`
  signal before child deletion, `retired_` makes resolution skip a scope whose
  destructor is running, `notifyOnDestroy_` suppresses the duplicate notification
  when `install()` replaces a scope, and `find()` is direct-child only
  (`theme_scope.cpp:84-111, 113-136, 218-228`). Recursive collection uses
  `QPointer` and the refresh walk is bounded at 8 passes
  (`theme_scope.cpp:50-60, 230-254`).
* **`Theme` value semantics.** The application snapshot is published behind a
  mutex and every replaced snapshot is retained, so a `const Theme&` from
  `applicationTheme()` stays valid across switches (`theme.cpp:69-116, 294-300`);
  the setter rejects off-thread calls with a warning instead of posting to the
  widget tree from a worker (`theme.cpp:256-267`); density is normalised
  (`theme.cpp:38-52`).
* **`Shape` geometry.** `Shape::resolved()` clamps every corner to half the
  shorter side and turns non-positive/non-finite radii into zero
  (`shape.h:87-118`), so no `QPainterPath` is built from a hostile radius.
  `Divider::snapToDevicePixels()` widens a sub-pixel extent to one device pixel
  instead of vanishing (`divider.cpp:55-73`).
* **`Form`/`ViewGroup` membership.** `addView()` de-duplicates, detaches from a
  previous `ViewGroup`, and connects `destroyed()` so a child deleted directly
  cannot dangle in `views_` (`view_group.cpp:41-68`); `paintChildren()` paints a
  `QPointer` snapshot and re-checks membership (`view_group.cpp:107-134`).
* **`GuideView` targets.** Steps hold `QPointer<QWidget>`, the filter list is
  `QList<QPointer<QWidget>>`, and `updateHighlight()` returns an empty rect when
  the target is gone (`guide.h:44, 147`; `guide.cpp:394-411`) — the documented
  "target destroyed mid-tour" behaviour holds.

## Files with no finding

`components/src/divider.cpp`, `components/src/dialog.cpp` (beyond the shared
custom-content detach contract, which the header documents), and
`tokens/src/color_scheme.cpp`, `tokens/src/shape.cpp`, `tokens/src/state_layer.cpp`,
`tokens/src/type_scale.cpp`, `tokens/src/tokens.cpp` were read in full and yielded
no finding beyond those already attributed to the theme boundary.

---

## Counts

| severity | count |
|----------|-------|
| P0 | 2 |
| P1 | 8 |
| P2 | 9 |
| **total** | **19** |
