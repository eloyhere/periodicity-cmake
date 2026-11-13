# Robustness audit — components `i*`–`m*` and module `view/`

- Repository: `D:\project\periodicity-cmake` (C++20, Qt 6.8 Widgets).
- Scope: `components/src/{i,l,m}*.cpp` and the matching
  `components/include/md/components/*.h` (18 .cpp / 18 headers), plus all of
  `view/` (`view.h`, `view.cpp`, `view_group.*`, `accessible.*`, `lifecycle.h`,
  `error_guard.h`, `render_backend.*`, `gpu_compositor.*`).
- Method: static review of the files above. No build, no execution, no runtime
  instrumentation. Reachability below is argued from the public API and the Qt
  event model, not from a reproducer.
- Severity: **P0** = crash or undefined behaviour reachable from a public API;
  **P1** = wrong output, swallowed failure or hang; **P2** = hardening.

## Result

| id | file:line | sev | issue | fix |
|---|---|---|---|---|
| A-01 | `view/src/gpu_compositor.cpp:182-200` | P0 | GL resources torn down without a guaranteed current context; `makeCurrent()` result ignored | check `makeCurrent()`; clean on `QOpenGLContext::aboutToBeDestroyed`; never call GL without a context |
| A-02 | `components/include/md/components/modal.h:189` (uses `modal.cpp:322, 593, 601, 662`) | P0 | parent window held as raw `QWidget*`; reparent + host destruction leaves it dangling → UAF in `removeEventFilter`/`rect()` | `QPointer<QWidget> host_`, re-resolve on open, null-check every use |
| A-03 | `components/include/md/components/list.h:130` (use `list.cpp:456`) | P0 | non-owning shared `QStyledItemDelegate*` stored raw; a deleted delegate is dereferenced on the next paint | `QPointer<QStyledItemDelegate>`; re-check before `paint` |
| A-04 | `view/src/gpu_compositor.cpp:296-340`, `196-199` | P0 | a view reparented out of the compositor keeps the compositor as event filter; destroying the compositor leaves a dangling filter on a live view | `removeEventFilter` before erasing a layer; keep an explicit filtered-view list for the destructor |
| B-01 | `view/src/gpu_compositor.cpp:580-600`, `102-131`, `613-671` | P1 | after the QOpenGLWidget context is recreated, `initializeGL` skips recreating the blitter and textures keep old object names → stale GL objects/blank frames | remember the `QOpenGLContext*`; rebuild blitter + all textures when it changes |
| B-02 | `view/src/gpu_compositor.cpp:73-97`, `111-130` | P1 | raster/texture allocation failure is silent: null `QImage`, inactive painter, `dirty=false`, oversized texture skipped forever | check `image.isNull()`, GL max texture size; keep dirty and report |
| B-03 | `view/src/view.cpp:748-752`, `242-269` | P1 | `+inf` measured size survives `setMeasuredSize`; `qCeil(inf)` converts out-of-range double to `int` (UB) | reject non-finite sizes; clamp hints to `[0, QWIDGETSIZE_MAX]` |
| B-04 | `components/src/list.cpp:202-210`, `221-231`, `281-294` | P1 | `setScrollOffset(NaN/inf)` accepted; `floor(NaN)`→`int` UB (INT32-C); infinite scroll range | require finite input; clamp defensively in every reader |
| B-05 | `components/src/list.cpp:389-402`, `407-413`, `456` | P1 | delegate `paint()` is user code; if it deletes the model, the cached loop continues and `paintRow` null-derefs `model_` | re-check `model_` per row and inside `paintRow`; break on removal |
| B-06 | `components/src/modal.cpp:134-167` | P1 | `setCard()` deletes the previous card synchronously; when called from the card's own handler the card (or the modal, for `setCard(this)`) is freed mid-stack | use `QPointer` + `hide()`/`deleteLater()`; reject `card == this` |
| B-07 | `components/src/menu_item.cpp:198-205`, `menu.cpp:814-824`, `list.cpp:545-548`, `input.cpp:1976-1981`, `icon_button.cpp:218-232`, `language.cpp:586-588` | P1 | signals are emitted and then `this` is touched again; a slot that deletes the sender yields UAF | guard with `QPointer<X> self(this)` and return when null, or emit last |
| B-08 | `view/src/view.cpp:823-848`, `994-1000` | P1 | virtual `onStoreChanged()` runs unguarded from `bindStore()` and from a `Store::valueChanged` lambda (exception crosses Qt's signal boundary) | run it through `view::guard`; catch at the signal lambda |
| C-01 | `view/src/view.cpp:499-514`, `630-667`, `697-736`, `669-695` | P2 | only the four documented entry points are guarded directly; resize/show/hide/change are protected transitively through `event()`, and a throw aborts Qt's remaining event work and is reported as "unhandled" | document the transitive contract; consider per-handler guards |
| C-02 | `view/src/view.cpp:927-937` | P2 | `setGpuAccelerated()` iterates a raw `findChildren` snapshot while emitting per descendant; a slot that deletes a later descendant leaves a dangling entry | iterate `QPointer<View>` and re-check |
| C-03 | `view/src/view.cpp:114-125` | P2 | a throwing `measureOverride` leaves a partially written size cache and `measuredExplicitly_ == false` | snapshot/restore `measuredSize_` on failure (as `hintSize()` already does) |
| C-04 | `view/include/md/view/error_guard.h:55-77` | P2 | diagnostic construction (QString/qWarning) happens inside `noexcept` catch handlers; bad_alloc here calls `std::terminate` (ERR50-CPP) | wrap the diagnostic in its own try/catch; fall back to a static string |
| C-05 | `view/src/gpu_compositor.cpp:217-230` | P2 | `addView()` accepts `view == this` or an ancestor cycle before `setParent()` | reject self/ancestor before reparenting |
| C-06 | `view/src/render_backend.cpp:45-53` | P2 | the availability probe caches a transient failure forever, contrary to its comment; probe runs on whichever thread calls first | cache only success, or re-probe on failure; assert GUI thread |
| C-07 | `components/src/model.cpp:65-88`, `205-213` | P2 | `data()` does not verify `index.model() == this`; `refresh()` emits `dataChanged` over `index(0,0)..index(rows-1,0)` without validating the range (0-column source, or call during removal) | check the model and validate both endpoints; call `refresh()` outside begin/endRemoveRows |
| C-08 | `components/src/language.cpp:961-973` | P2 | `notifyScope()` re-reads `scopeHost_` after synchronously sending it an event; a handler that deletes the host leaves a null QPointer that is dereferenced | copy the `QPointer` and re-check after every `sendEvent` |
| C-09 | `components/src/marquee.cpp:125-134`, `155-163`, `272-295` | P2 | non-finite `speed`/`gap` accepted; `fmod` on the resulting non-finite offset produces NaN paint coordinates | require `std::isfinite`; keep the last finite value |

Counts: **P0 = 4, P1 = 8, P2 = 9** (21 findings).

---

## P0 findings

### A-01 — `GpuCompositor` destroys GL objects without a guaranteed current context

**Rules:** C.36/P.4 (a destructor must not fail and must not invoke code with preconditions that may not hold), R.1 (own resources through RAII that can be released deterministically), ERR50-CPP (no abrupt termination from a destructor path). This is the audit's "GL call after the context is gone" case.

**Call path**

1. `GpuCompositor` is shown; `initializeGL()` sets `d_->gpuReady = true` (`gpu_compositor.cpp:598`) and `paintGL()` → `uploadLayerTexture()` creates `QOpenGLTexture` objects.
2. The widget is destroyed while its context is missing or invalid — e.g. the compositor was reparented/hidden, `setFormat()` recreated the context and the new one has not been made current, or the platform dropped the surface.
3. `~GpuCompositor()` at line 187 guards on `context() != nullptr && context()->isValid()`. If that is false the whole block is skipped. `d_` (the `Private` with `std::unique_ptr<QOpenGLTexture>` members and a created `QOpenGLTextureBlitter`) is then destroyed *after the destructor body* by `std::unique_ptr` with no current context. `QOpenGLTexture::~QOpenGLTexture()` and `QOpenGLTextureBlitter::~QOpenGLTextureBlitter()` still run their destroy paths; the blitter's cached `QOpenGLFunctions*` calls go to the driver with no current context (undefined; the texture destroy at best warns and leaks).
4. Even when the guard passes, `makeCurrent()` at line 188 returns `bool` and is ignored. A failed `makeCurrent()` still leads to `d_->layers.clear()` (texture destruction) and `d_->blitter.destroy()` at lines 189-191.

Context loss is a normal QOpenGLWidget transition (reparent to another window, `setFormat`, screen change), so this is reachable without any misuse.

**Patch sketch**

```cpp
GpuCompositor::~GpuCompositor()
{
    // Resource destruction must happen while the owning context is current.
    const bool current = context() != nullptr && context()->isValid()
                      && makeCurrent();          // <-- check the result
    if (current) {
        d_->layers.clear();                      // textures die here
        if (d_->blitter.isCreated())
            d_->blitter.destroy();
        doneCurrent();
    } else {
        // Context already gone: never touch GL. Abandon the handles
        // (leaking is preferable to undefined behaviour).
        for (GpuLayer& layer : d_->layers)
            layer.texture.release();
        d_->layers.clear();
    }
    d_->gpuReady = false;
    ...
}
```

Also connect the context's destruction early, while it is still current:

```cpp
connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] {
    makeCurrent();
    d_->layers.clear();
    if (d_->blitter.isCreated()) d_->blitter.destroy();
    doneCurrent();
    d_->gpuReady = false;
});
```

### A-02 — `Modal::host_` is a raw pointer to the parent window

**Rules:** I.11 / R.3 (a raw `T*` is non-owning and carries no lifetime guarantee), MEM50-CPP (do not access freed memory).

**Call path**

1. `Modal::open()` stores `host_ = window();` (`modal.cpp:248`) and reparents itself to that window (line 250). Every other external widget in the class is a `QPointer` (`card_`, `anchor_`, `focusReturn_`, `previousFocus_`); `host_` is the one raw pointer (`modal.h:189`).
2. The modal is open. The application reparents it — `modal.setParent(otherTopLevel)` is a public QWidget call, and `Language::showSelector()` does exactly this at `language.cpp:658-659` when the chip's window changed. `open()` early-returns while `open_` is true, so `host_` is never re-resolved.
3. The old host window is destroyed. The modal is no longer its child, so it survives with a dangling `host_`.
4. Any of these now dereferences freed memory:
   - `Modal::close()` → `finishClose()`/`abort()` → `removeFilters()` → `host_->removeEventFilter(this)` (`modal.cpp:593`);
   - `unregisterLayer()` → `restack(host_)`;
   - `applyWindowGeometry()` → `host_->rect()` (`modal.cpp:324`) from the host-resize filter;
   - `measureOverride()` → `host_->width()` (`modal.cpp:601`), e.g. from a `sizeHint()` query.

A second, weaker path exists even without reparenting: a modal that is still a child of the host is destroyed by `QWidget`'s child deletion while the host is already in its own destructor; `removeFilters()` then calls `removeEventFilter()` through a `QWidget*` whose QWidget subobject is gone. Making the pointer a `QPointer` also fixes the "don't call into a dying host" case because the pointer is nulled when the QObject dies.

**Patch sketch**

```cpp
// modal.h
QPointer<QWidget> host_;
```

```cpp
void Modal::open()
{
    host_ = window();                        // QPointer; null-safe
    ...
}

void Modal::removeFilters()
{
    if (host_ && host_ != this)
        host_->removeEventFilter(this);
    ...
}
```

Re-resolve `host_` in `open()` even when already open (or on `QEvent::ParentChange`), and null-check it in `applyWindowGeometry()`, `measureOverride()` and `unregisterLayer()`.

### A-03 — `List` stores a non-owning delegate as a raw pointer

**Rules:** I.11/R.3 (raw non-owning pointer has no death notification), MEM50-CPP.

**Call path**

1. `list.setItemDelegate(shared)` records the pointer (`list.cpp:93-99`; member `QStyledItemDelegate* delegate_`, `list.h:130`). The header documents that the list never owns the delegate, exactly like `QAbstractItemView` — but Qt's own view keeps a `QPointer<QAbstractItemDelegate>` for this reason.
2. The host deletes the shared delegate (normal end of a document/toolbar lifetime).
3. Any repaint reaches `List::paintRow()` → `delegate_->paint(&painter, option, index)` (`list.cpp:456`) on freed memory. There is no `destroyed` connection and no `QPointer`.

**Patch sketch**

```cpp
// list.h
#include <QPointer>
...
QPointer<QStyledItemDelegate> delegate_;
```

`paintRow()` keeps its existing `delegate_ != nullptr` test, which then becomes meaningful; `setItemDelegate()` should be the only writer.

### A-04 — A reparented view keeps a dangling event filter into the compositor

**Rules:** MEM50-CPP (freed-memory access), E.6/R.1 (ownership of the observation relationship is not released), Qt event-filter lifetime.

**Call path**

1. `GpuCompositor::addView(v)` installs the compositor as an event filter on the view (`gpu_compositor.cpp:329`).
2. The view is reparented away from the compositor by ordinary widget code (`v->setParent(other)`), which is an anticipated path: `syncViews()` explicitly documents and implements "Drop layers whose view was destroyed or reparented away" (`gpu_compositor.cpp:300-306`).
3. That erase path calls neither `removeEventFilter()` nor `removeView()`; `removeView()` afterwards also fails because the layer is already gone.
4. The compositor is destroyed. `~GpuCompositor()`'s filter-removal loop (`gpu_compositor.cpp:196-199`) iterates `d_->layers`, which was cleared at line 189 when the GPU path was active — so it removes nothing. (Qt 6.8 does not remove a filter object from other objects' filter lists when it is destroyed; the filtered object's list is only freed with that object.)
5. The still-alive `v` receives its next event (paint/update), and Qt dispatches it to the freed compositor's `eventFilter` — UAF.

The same hole exists in the destructor even without a reparent for any view that was separately removed from `d_->layers`; the filter list and the texture layer list are not the same set, but only the layer list is maintained.

**Patch sketch**

```cpp
void GpuCompositor::syncViews()
{
    // Before dropping a layer, release the filter it owns.
    for (auto it = d_->layers.begin(); it != d_->layers.end(); ) {
        View* view = it->view;
        const bool keep = view && direct.contains(view);
        if (!keep) {
            if (view) view->removeEventFilter(this);
            it = d_->layers.erase(it);
        } else {
            ++it;
        }
    }
    ...
}
```

and in the destructor remove filters first, then clear the layers:

```cpp
for (GpuLayer& layer : d_->layers)
    if (layer.view) layer.view->removeEventFilter(this);
d_->layers.clear();
```

---

## P1 findings

### B-01 — Context recreation reuses blitter and texture object names from the dead context

**Rules:** R.1 (each GL resource belongs to one context), Qt `QOpenGLWidget` context lifecycle.

**Call path.** `initializeGL()` runs again after Qt recreates the context (reparent to a new top-level, screen change, `setFormat()`). It only creates the blitter when `!d_->blitter.isCreated()` (`gpu_compositor.cpp:591`); `isCreated()` is a plain boolean and stays true. `d_->gpuReady` also stays true. In `uploadLayerTexture()` the texture is reallocated only when the pixel size changed (`gpu_compositor.cpp:111-126`), so a same-size repaint calls `setData()` on a name that does not exist in the new context; `paintGL()` then blits `layer.texture->textureId()` with the old blitter program. Result: GL_INVALID_OPERATION / black or stale layer output, with no diagnostic.

**Patch sketch.** Store the owning `QOpenGLContext*` next to `gpuReady`. In `initializeGL()`, if `context() != d_->context` (or `!d_->gpuReady` after a loss signal), reset `d_->blitter = QOpenGLTextureBlitter{}` (or track `created` per context), clear all `GpuLayer::texture`, set `dirty = true` on every layer, and only then create the new blitter. Invalidate the cached textures in `QOpenGLContext::aboutToBeDestroyed`.

### B-02 — GPU raster and upload failures are swallowed and cached as success

**Rules:** E.2/E.4 (report failure instead of silently succeeding), Qt `QPainter::begin` on a null image, `QOpenGLTexture` allocation contract.

**Call path.** `renderLayerImage()` computes `pixels = size * dpr` (`gpu_compositor.cpp:73-76`) and allocates `QImage(pixels, …)`. On a very large or DPR-scaled view the allocation can fail (null image); the painter then fails to begin and `view->render(&painter, …)` is a no-op. Line 96 still sets `layer.dirty = false`, so the layer stays blank until something else marks it dirty. `uploadLayerTexture()` has the same shape: `create()` failure is handled, but `allocateStorage()` is unchecked (line 125), `setData()` has no failure return, and a texture larger than `GL_MAX_TEXTURE_SIZE` is dropped by `paintGL()`'s `continue` (`gpu_compositor.cpp:655`) with no warning — the child is simply invisible.

**Patch sketch**

```cpp
if (layer.image.isNull()) {
    qWarning("md::GpuCompositor: %s: image allocation failed (%dx%d)",
             qUtf8Printable(view->objectName()), pixels.width(), pixels.height());
    return false;                 // keep layer.dirty = true
}
...
if (!layer.texture->create() || !layer.texture->allocateStorage()) {
    layer.texture.reset();
    return false;
}
```

Also query `GL_MAX_TEXTURE_SIZE` once per context and skip with one diagnostic per layer instead of silently skipping every frame.

### B-03 — `View` converts a non-finite measured size to `int`

**Rules:** INT32-C (conversion of a floating value that is not representable in the target integer type is undefined); Qt `QSize` contract.

**Call path.** `measureOverride()` is a user extension point; `setMeasuredSize()` (`view.cpp:748-752`) clamps only negative values. `std::max(0.0, +inf)` returns `+inf`, so `measuredSize_` can be infinite (NaN is incidentally clamped to 0 by `std::max`). The geometry queries then call `qCeil(hint.width())` / `qCeil(hint.height())` (`view.cpp:248`, `255`, `268`). `qCeil()` converts the `qreal` to `int`; for `+inf` that conversion is UB (typically `INT_MIN` on MSVC/x86-64, but not guaranteed). A container that sizes a child from `sizeHint()` then lays it out with a garbage/inverted rectangle.

**Patch sketch**

```cpp
void View::setMeasuredSize(QSizeF size)
{
    const auto finite = [](qreal v) {
        return std::isfinite(v) ? std::clamp(v, qreal(0.0), qreal(QWIDGETSIZE_MAX))
                                : qreal(0.0);
    };
    measuredSize_ = QSizeF(finite(size.width()), finite(size.height()));
}
```

and clamp at every `qCeil` site (`std::clamp(qCeil(...), 0, QWIDGETSIZE_MAX)`) so a stored bad value can never narrow out of range.

### B-04 — `List` accepts a non-finite scroll offset and converts it to `int`

**Rules:** INT32-C, ARR30-C (row computation must stay inside the model).

**Call path.** `List::setScrollOffset(qreal)` (`list.cpp:202-210`) uses `std::clamp(offset, 0.0, maximumScroll())`. `std::clamp` with NaN comparisons returns NaN; `qFuzzyCompare(NaN+1, NaN+1)` is false, so `scrollOffset_ = NaN`. The next paint calls `firstVisibleRow()` (`list.cpp:230`), which does `static_cast<int>(std::floor(NaN / rh))` — UB. `maximumScroll()` can also be `+inf` when `rowHeightPx()` is infinite (the setter only checks `dp > 0`), and `rowAtPosition()` has the same `floor(...)→int` conversion (`list.cpp:288`). An infinite offset also makes `ensureVisible()`/`clampScroll()` meaningless.

**Patch sketch**

```cpp
void List::setScrollOffset(qreal offset)
{
    if (!std::isfinite(offset))
        return;                       // or clamp to maximumScroll
    ...
}
```

Guard `setRowHeight()` with `std::isfinite` as well, and use a helper that computes the row index only after both operands are finite.

### B-05 — `List` paints through a cached row count while a delegate can mutate the model

**Rules:** Qt item-model contract (indexes must be re-validated after arbitrary user code), ARR30-C (no row beyond the model), F.6 (a `paint()` that runs user code is a boundary).

**Call path.** `List::paintContent()` caches `count`, `first`, `last` (`list.cpp:389-392`) and `paintRow()` is called in a loop. `paintRow()` invokes `delegate_->paint()` (`list.cpp:456`), which is user code and may reset/replace/destroy the model. `paintRow()` recomputes `model_->index(row, 0)` (`list.cpp:413`) using `QPointer` `model_` without a null check; if the delegate deleted the model, the next iteration null-dereferences `model_` (QPointer's `operator->` yields `nullptr`). If the model merely shrank, the loop still paints rows that no longer exist (invalid indexes passed to the delegate).

**Patch sketch**

```cpp
for (int index = first; index <= last; ++index) {
    if (model_ == nullptr || index >= rowCount())
        break;
    ...
}
```

and start `paintRow()` with `if (model_ == nullptr) return;`.

### B-06 — `Modal::setCard()` deletes the old card synchronously

**Rules:** E.6 (deterministic ownership), MEM50-CPP; Qt widget lifetime (a widget must not be deleted while one of its handlers is on the stack).

**Call path.** `Modal::setCard()` (`modal.cpp:134-167`) does `delete card_.data()` for the previous card. The card is normally a child of the modal (`card_->setParent(this)`) whose own buttons/widgets call back into the host; a handler that swaps the modal content (or closes it) from a card-child's signal runs `delete` on the ancestor of the widget currently executing its event handler. Everything after the `delete` in that widget's stack is then use-after-free. `setCard(this)` then a later `setCard(other)` deletes the modal itself inside its own member function. The `QPointer` makes the pointer null but does not make the destruction safe.

**Patch sketch**

```cpp
if (QWidget* old = card_.data()) {
    old->hide();
    old->setParent(nullptr);
    old->deleteLater();      // safe from inside its own signal
}
```

and reject `card == this` (or an ancestor) at the top of `setCard()`.

### B-07 — Emit-then-touch: signals are followed by member access on `this`

**Rules:** F.6 (don't let a callback leave the object usable after deletion), MEM50-CPP, Qt signal/slot reentrancy.

**Call path (worst first).**

1. `MenuItem::activate()` (`menu_item.cpp:198-205`): `emit clicked(); emit activated();`. A `clicked` handler that deletes the menu (the common "choose → close and delete" flow) destroys the item's parent group and the item; the subsequent `emit activated()` runs on a destroyed QObject.
2. `Menu::activateIndex()` (`menu.cpp:814-824`): `emit itemTriggered(index, text);` then `Menu* root = rootMenu(); if (root) root->close();`. A slot that deletes the menu makes both the `rootMenu()` member read and `root->close()` UAF.
3. `List::mouseReleaseEvent()` (`list.cpp:545-548`): `emit clicked(...)` then `requestUpdate()`.
4. `DropDown::handlePopupActivated()` (`input.cpp:1976-1981`): `emit activated(row); hidePopup();`.
5. `IconButton::activate()` (`icon_button.cpp:218-232`): `emit toggled()` inside `applyChecked()`, then `emit clicked()`.
6. `Language::setCurrentLanguage()` (`language.cpp:586-588`): `emit localeChanged(); emit languageChanged(); notifyScope();` — a slot on the first signal can delete the Language, then the second emit uses it.

Each is a direct public-API path: connecting a deleting slot is legal in Qt, and `deleteLater` is not mandatory for the caller.

**Patch sketch.** The minimal, uniform fix:

```cpp
void MenuItem::activate()
{
    if (!isEnabled()) return;
    const QPointer<MenuItem> self(this);
    emit clicked();
    if (self.isNull()) return;      // handler deleted us
    emit activated();
}
```

For `Menu::activateIndex`, take `QPointer<Menu> self(this)` (and a `QPointer<Menu> root` before the emit) and bail out when null. For `List`/`DropDown`/`Language`, move `requestUpdate()`/`hidePopup()`/`notifyScope()` before the emit, or guard with `QPointer`.

### B-08 — `View::onStoreChanged()` crosses Qt's signal boundary without a guard

**Rules:** ERR51-CPP (catch all exceptions at a module boundary), F.6, E.1/E.4 (one error-handling strategy for component hooks).

**Call path.** `View::bindStore(Store*)` (`view.cpp:823-848`) calls the virtual `onStoreChanged()` synchronously for every value already in the store — a component override can throw out of the public `bindStore()` call. It also connects a lambda to `Store::valueChanged` that calls `onStoreChanged()`; that lambda is invoked from `QMetaObject::activate` inside `Store::setValue`. An exception then unwinds through Qt's signal-emission machinery, which is not an exception boundary — at best it escapes into an unrelated caller, at worst it terminates inside Qt. This is the one component hook that is neither a guarded `View` entry point nor called from one.

**Patch sketch**

```cpp
connect(store, &Store::valueChanged, this,
        [this, store](const QString& key, const QVariant& value) {
            static_cast<void>(view::guard(
                "View::onStoreChanged", kEventSite, guardReport_,
                [&] { onStoreChanged(store, key, value); }));
            emit storeChanged(key, value);
        });
```

and the same wrapper around the initial `values()` loop in `bindStore()`.

---

## P2 findings

### C-01 — Guard coverage is transitive for all non-paint entry points

**Rules:** E.1/E.4 (make the boundary explicit), F.6.

The four documented entry points really are wrapped: `measure()` (`view.cpp:119`), `arrange()` (`view.cpp:129`), `paintEvent()` (`437`), `event()` (`673`), plus `hintSize()` (`280`). The mouse/key/resize/show/hide/change overrides are reached through `View::event()` when Qt delivers them, so a throwing `arrangeOverride()`, `onShown()`, `onThemeChanged()` is caught — but only by the *event* guard: `QWidget::event()` is abandoned half-way, and `View::event()` returns `false`, so Qt sees the event as unhandled and may propagate the same press/key to the parent widget (double handling). There is also no guard at all for a direct `EventDispatcher::dispatch(event, view)` into the public `onEvent()` override (`view.h:198`, `view.cpp:326-420`). This is a contract gap, not a crash.

**Patch sketch.** Either wrap each Qt handler where it is defined (`mousePressEvent`, `resizeEvent`, `showEvent`, …) or state in `error_guard.h` that only the four entry points are boundaries and that direct `onEvent()` calls bypass them; add a guard around the `EventDispatcher` call in the dispatch path.

### C-02 — `View::setGpuAccelerated()` iterates a raw snapshot while emitting

**Rules:** MEM50-CPP.

`findChildren<View*>()` returns a raw list (`view.cpp:932`); the loop then calls `applyGpuAccelerated()` which emits `gpuAccelerationChanged`. A slot connected to that signal can delete another view in the snapshot; the next iteration dereferences the freed pointer. Use `QList<QPointer<View>>` and check each entry.

### C-03 — A throwing `measureOverride()` leaves a stale cache

**Rules:** E.4/E.6.

`View::measure()` (`view.cpp:114-125`) leaves whatever `setMeasuredSize()` calls the hook made before throwing in `measuredSize_`, and keeps `measuredExplicitly_` unchanged. A later `sizeHint()` exposes the partial value. `hintSize()` already saves/restores around the hook (`view.cpp:277-288`); `measure()` should do the same.

### C-04 — Error diagnostics allocate inside `noexcept`

**Rules:** ERR50-CPP, F.6, E.4.

`detail::run()` is `noexcept` (`error_guard.h:80`). Its catch handler calls `reportFailure()`, which builds a `QString` from `what` and `where` and calls `qWarning` (`error_guard.h:55-77`). If that allocation throws `std::bad_alloc`, the exception leaves a `noexcept` function → `std::terminate`. The header correctly argues that a second exception must not escape, but the catch body itself is unprotected. Wrap the body:

```cpp
try {
    ...
} catch (...) {
    qWarning("md::view: %s failed (diagnostic construction failed)", guardWhere(where));
}
```

### C-05 — `GpuCompositor::addView()` can create a parent cycle

**Rules:** I.11, E.6.

`addView()` (`gpu_compositor.cpp:217-230`) reparents unconditionally after checking only `view == nullptr`. Passing the compositor itself or one of its ancestors creates a QObject parent cycle (`setParent`), which breaks child deletion. Reject `view == this || view->isAncestorOf(this) || this->isAncestorOf(view)` before `setParent`.

### C-06 — The backend probe caches a transient failure forever

**Rules:** E.1/E.2 (report the actual capability), Qt GL context lifecycle.

`isBackendAvailable()` (`render_backend.cpp:45-53`) initializes `static const bool available` on first use and caches both success and failure, although the comment says "cached after a successful probe". A transient failure during startup (driver not ready, remote session) permanently reports Software. Also the probe runs on whichever thread calls first; `QOpenGLContext::create()` from a worker thread initializes the driver on that thread. Cache only `true`, re-probe on failure, and require the GUI thread (or document the thread requirement).

### C-07 — `Model`/`Adapter` index identity and `dataChanged` range

**Rules:** Qt model/view conventions (valid indexes; no `dataChanged` for rows that no longer exist), ARR30-C (row bounds).

`Model::data()` (`model.cpp:65-88`) and `Adapter::data()` (`model.cpp:130-154`) check `isValid()`/row bounds but never `index.model() == this`; a foreign index with a small row returns this model's data for the wrong model. `Adapter::refresh()` (`model.cpp:205-213`) emits `dataChanged(index(0,0), index(rows-1,0), …)` without checking that `columnCount() > 0` (an `QAbstractItemModel` with rows and zero columns is legal) and without re-reading the row count after a caller may have mutated the source; if the call happens from a `rowsAboutToBeRemoved` handler it can reference a row that is being removed. Check the model pointer and both endpoints before emitting; call `refresh()` outside the begin/end remove block.

### C-08 — `Language::notifyScope()` reuses a `QPointer` after synchronous dispatch

**Rules:** MEM50-CPP, Qt event dispatch reentrancy.

`notifyScope()` (`language.cpp:961-973`) checks `scopeHost_` once, then calls `QApplication::sendEvent(scopeHost_.data(), &event)` and afterwards iterates `scopeHost_->findChildren<QWidget*>()`. A `LanguageChange` handler may delete the host or the Language; the QPointer is then null but `scopeHost_->…` is still dereferenced. Copy the pointer to a local `QPointer`, and re-check both it and `this` after each `sendEvent`.

### C-09 — `Marquee` accepts non-finite speed/gap

**Rules:** INT32-C (indirect: NaN offsets feed `drawText` rectangles), E.4.

`setSpeed()` (`marquee.cpp:125-134`) and `setGap()` (`marquee.cpp:155-163`) only clamp to `>= 0`; `+inf` passes. `tick()` then computes `offset_ += dp(speed) * seconds` and `std::fmod(offset_, travel)` — `fmod(inf, finite)` is NaN, so every subsequent paint uses NaN coordinates (`marquee.cpp:272-295, 326-339`). Require `std::isfinite()` and ignore non-finite input (or keep the previous value).

---

## Checked and found sound

These were explicitly examined and no defect was found; they are listed so the
next audit does not repeat the work.

**View pipeline / `error_guard.h`**

- `paintEvent()` is `final` and guarded; a throwing `paintContent()` cannot
  escape into Qt. The `QPainter` is constructed inside the guard, so stack
  unwinding ends painting and no `save()`/`restore()` imbalance survives to the
  next frame. `paintContent` implementations that do `save()`/`restore()` are
  extra safety, not a requirement.
- `guard()`/`detail::run()` are re-entrant: they keep no state beyond the
  caller-owned `GuardReport`; a nested guarded call cannot corrupt an outer one.
  The shared `reportedSites` bitmask only suppresses duplicate logging (the
  nested failure marks the bit first); `lastError` is last-writer-wins, which is
  the documented contract. UI-thread-only, so no synchronisation is needed.
- `arrangeDepth_` + `qScopeGuard` correctly prevent the `setGeometry()` →
  `resizeEvent()` → `arrangeOverride()` recursion, and the depth is decremented
  on the throw path too.
- `hintSize()` preserves an explicit measurement (`measuredSize_` /
  `measuredExplicitly_`) across Qt geometry queries; the const casts are safe
  because only the hint cache is written.
- The paint pipeline's ordering (shadow outside, background, content, children,
  foreground, ripple, focus ring) is fixed and no stage can be skipped by a
  subclass. `ViewGroup` renders a snapshot and re-checks membership, so a child
  adding/removing siblings during paint is handled.

**Boundary conditions**

- Zero-size widgets: every `paintContent()` implementation in scope early-returns
  on an empty `content`/`rect` (icon, icon_button, image, link, list, marquee,
  language, menu, menu_item, mention, the charts); `QPainter` on a zero-size
  widget is legal. `Modal::positionCard()` falls back through
  `sizeHint → size → minimumSize → 320×240 dp`; `Menu::positionPanel()` returns
  without arranging when the available bounds are empty.
- `QPixmap`/`QPainter::begin` on an empty pixmap: none of the scoped files
  creates a `QPixmap`; `Image` and `Icon` keep `QImage`/`QIcon` and delegate
  painting. The only null-image painter is the GPU one, found as B-02.
- Model index validity in `List`: `indexAt()`, `rowAtPosition()`, `rowCount()`
  and the paint loop all bounds-check; `current_` is a `QPersistentModelIndex`
  and is cleared on reset. `rowsAboutToBeRemoved` caches the selected row before
  the persistent index invalidates, and `rowsRemoved` clears the selection once.
- `QSortFilterProxyModel` use in `Complete`: `invalidate()`/`setFilterFixedString`
  are only called from public setters and text/focus handlers, never from a
  paint or an iteration over `rowCount()`; `MenuPopup` caches `rows` and then
  indexes the proxy read-only. No invalidation-during-iteration path.
- Layout loops: no component calls `arrange()`/`measure()` on itself from
  `arrangeOverride()`/`measureOverride()`; `MenuGroup`/charts arrange children or
  only store geometry. The only re-entrancy is the resize suppression above.
- Charts (`laplace_chart.cpp`, `mellin_chart.cpp`, `line_chart.cpp`): every
  `values_.at()` is reached through `sampleMagnitude()`/`sampleAt()`, which
  validate both axis indices and `index < values_.size()`; `setSpectrum()`
  truncates min/max scans to `min(values.size, axis product)`; log-t axis
  rejects `t <= 0`; `niceTicks` caps the tick count; `line_chart`'s
  Catmull-Rom path clamps its endpoints and only iterates `points.size() > 1`.
  `PainterStateGuard` + per-chart `try/catch` keep painter state intact.
- `MathematicsCoordination::toPixel()`/`toData()` guard zero plot extents and
  non-finite input; `niceTicks()` rejects non-finite ranges and caps tick totals.

**GPU lifetime (outside A-01/A-04)**

- A child deleted while queued for upload is safe: `GpuLayer::view` is a
  `QPointer`, `isGpuLayer()` null-checks it, and `GpuLayer` is only a value in
  `d_->layers`. The deferred `requestRepaint()` task captures a
  `QPointer<GpuCompositor>`.
- `context()` is null-checked in `initializeGL()`, `resizeGL()` and `paintGL()`;
  `paintGL()` also re-checks `usingGpu`/`isCreated()` and returns without GL
  calls. `windowHandle()` is never dereferenced by this module (QOpenGLWidget
  owns the surface).
- `QOpenGLWidget`'s virtuals (`initializeGL`, `resizeGL`, `paintGL`) are only
  called by Qt with the context current; the defect is confined to the
  destructor path and context recreation.
- `View::hasGpuCompositorAncestor()` uses a dynamic property instead of a
  concrete type check, keeping `view/` free of a `components/` dependency, and
  the flag is applied to the current subtree only (documented).

**Modal / menu lifetime (outside A-02/B-06/B-07)**

- `Modal` uses `QPointer` for `card_`, `anchor_`, `focusReturn_`,
  `previousFocus_`, and guards `emit opened()`/`closed()` with `QPointer<Modal>`
  self checks; `open()`/`close()` are synchronous state machines with **no
  nested `QEventLoop`/`exec()`**, so a parent destruction cannot leave a nested
  loop running.
- `Modal` removes its application/host filters in `removeFilters()` and in its
  destructor, so no filter points at a dead modal after a normal close.
- `Menu::anchor_` is a `QPointer` with a `destroyed` connection that closes the
  menu; `Menu` submenus are parented to the window and shut down explicitly
  (`shutdown()` nulls `parentMenu_` before deleting), so the raw `parentMenu_`
  back-pointer never outlives its parent.
- `MenuGroup` rows are raw `MenuItem*` in `items_`, but each is a child widget
  with a `destroyed` connection that erases it; the group destructor disconnects
  before the child deletion runs.
- `Mention`'s `editor_`, `popup_`, `objectHandler_` are all `QObject` children of
  the `Mention`; the object handler is registered with the editor's document and
  is destroyed after the editor, so the layout never sees a stale handler.
- `Language` deletes its modal in its destructor while it is still alive, and
  all modal pointers are `QPointer`s; the `LanguagePanel` callbacks capture the
  `Language` but cannot outlive it because the panel is a child of the modal
  which `Language` owns/deletes.

**Other components**

- `Image`: generation counter invalidates stale synchronous/async loads;
  `reply->deleteLater()` on every path; `setSource` clears `image_` before a new
  load; `paintContent` guards empty content/image size and clamps the corner
  radius to half the short side.
- `Model`/`Adapter` row bounds are checked before `values_` access; `append()`
  keeps `labels_` in step under `beginInsertRows`/`endInsertRows`.
- `Language`, `Link`, `Icon`, `IconButton`, `MenuItem`, `MenuGroup`, `Marquee`:
  no bare `QPainter` on a null target, no unchecked array indexing, no manual
  `begin`/`end` painting pairs.
