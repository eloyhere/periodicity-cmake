# Robustness audit — `n*`/`p*`/`r*` components, `layer/` and `layout/`

Read-only audit. No file outside `docs/audit/n-r.md` was created or modified by
this audit, and nothing was built. Scope was exactly:

- the 15 sources matching `components/src/n*.cpp`, `p*.cpp`, `r*.cpp`:
  `navigation_bar`, `navigation_view`, `notification`, `page`, `pager`,
  `pagination`, `pie_chart`, `polar_plot_chart`, `popup`, `progress`,
  `radar_chart`, `radio`, `rate`, `recycler`, `ring`;
- the 15 matching `components/include/md/components/*.h`;
- `layer/include`, `layer/src`, `layout/include`, `layout/src` (24 files).

The tree at the time of the audit is commit `5a26e13`. There is no
`components/src/ripple.cpp`: `ripple` is a separate top-level module and was
outside the exact file list, so `ring.cpp` is the fifteenth `r*` source that was
audited. The audit is source-only; no runtime evidence exists.

## Headline

| Severity | Count |
|---|---|
| P0 | **3** |
| P1 | **4** |
| P2 | **9** |

The three P0s are re-entrancy use-after-frees, not missing null checks: each is
reachable through a documented public extension point (a Qt signal handler or a
`CoordinatorBehaviour` callback) without violating any stated precondition. The
rest is one state-consistency P1 in `Recycler`, one exception-boundary P1, one
hit-test P1 and one arithmetic/state P1 in `AppBarBehavior`, plus hardening.

Everything else on the audit list was traced to a guard; the
[checked-and-found-sound](#checked-and-found-sound) section names those guards
so the absence of a finding there is evidence rather than silence.

---

## Findings

| id | file:line | severity | issue | fix |
|---|---|---|---|---|
| P0-1 | `components/src/navigation_view.cpp:357-362` | P0 | `activate()` keeps a reference into `entries_` across `selectionChanged`; a handler that calls `setEntries()`/`clearEntries()` frees it, then `entry.label` is read (UAF) | Copy the label before emitting, or emit the signal with a value captured first |
| P0-2 | `components/src/radio.cpp:127-131` | P0 | `uncheckSiblings()` iterates a copied `QObjectList` of raw siblings; `setChecked(false)` emits `toggled`, whose handler may delete a later sibling, then `qobject_cast` dereferences the freed object | Snapshot `QPointer<Radio>` (or re-check each pointer), never a raw `QObjectList` |
| P0-3 | `layout/src/coordinator.cpp:134-137`<br>`layout/src/coordinator.cpp:148-151`<br>`layout/src/coordinator.cpp:161-164` | P0 | Dispatch iterates `entries_` while calling user-virtual behaviour hooks; a hook may `removeBehaviour()`/`addBehaviour()` or delete a child, erasing/reallocating the vector and destroying the executing behaviour | Add a dispatch guard with deferred mutation (or a stable snapshot plus delayed destruction) |
| P1-1 | `layout/src/appbar.cpp:457-459` | P1 | `onNestedPreScroll()` reads `appBar_->scrollOffset()` again after `setScrollOffset()` emitted `offsetChanged`; a re-entrant offset change corrupts the consumed delta and a destroyed bar leaves a null `QPointer` dereference | Compute the clamped target first; never read the property back after a signal |
| P1-2 | `components/src/recycler.cpp:495-541` | P1 | Realised items are bound only when first acquired. `dataChanged` only repaints, and `rowsInserted`/`rowsRemoved`/`layoutChanged`/`modelReset`/`setModel` never rebind shifted items: wrong rows, stale factory content, blank rows after reset | Re-bind affected active items on `dataChanged`; release/rebind all active items on every structural signal and on `setModel` |
| P1-3 | `components/src/recycler.cpp:431`<br>`components/src/recycler.cpp:479` | P1 | `rowsInserted` and `layoutChanged` call `rebuild()` unguarded; the path reaches user `ItemFactory`/`ItemBinder` (`:560`, `:602`), so a throw unwinds through `QAbstractItemModel`'s signal emission (ERR50/ERR51) | Route every model-signal handler through one guarded rebuild that catches and reports |
| P1-4 | `components/src/navigation_view.cpp:311-318` | P1 | `entryIndexAt()` scans every row rectangle without the body clip applied by painting (`:413-414`); with a scroll offset, a click on the header hits the row scrolled underneath it | Reject positions above `entriesTop()` (or intersect with the body viewport) before the scan |
| P2-1 | `components/src/page.cpp:249-310` | P2 | `setContent()`/`setFooter()` do not reject a view that already has the other role (only `addHeaderAction()` checks); the view is reparented while the old role pointer/list keeps it, producing double arrangement and a hidden ghost | Enforce one role per view; detach from the previous role before adopting |
| P2-2 | `components/src/popup.cpp:568-587` | P2 | The outside-press branch calls `close()`, which can emit `closed()` and delete the popup, then still calls `ViewGroup::eventFilter()` on `this`; the default branch should return immediately | `close(); return false;` in the press branch; guard with `QPointer` if more work follows |
| P2-3 | `components/src/notification.cpp:449-451`<br>`components/src/notification.cpp:49-50` | P2 | `actionTriggered()` is followed by `dismissCurrent()` on `this`; `closed()` is emitted from the timer's `timeout` handler. A handler that deletes the notification while acting on either signal destroys the object/timer mid-emission | Guard the post-signal call with `QPointer`; queue the timer-initiated close; document `deleteLater` |
| P2-4 | `components/src/rate.cpp:73-83`<br>`components/src/rate.cpp:115-123` | P2 | `setMax()` accepts any positive int and paint loops `maximum_` times; `snap()` does `qRound(clamped / step_)`, which is undefined for a tiny positive `step_` and for a NaN `value_` (INT32-C) | Clamp `maximum_` to a sane bound; reject non-finite value/step and bound the ratio before `qRound` |
| P2-5 | `components/src/recycler.cpp:510-513` | P2 | `setPrefetchRows()` accepts up to `INT_MAX`; `ceil(...) + prefetchRows_` overflows `int` in `relayout()` (INT32-C) | Clamp prefetch to a small range and use `qint64` for the window arithmetic |
| P2-6 | `components/src/recycler.cpp:61-82`<br>`components/src/recycler.cpp:572-581` | P2 | `Item::pressed_` survives `releaseItem()`/re-acquisition; after a scroll while the button is held, releasing over the reused widget can activate a different row | Clear `pressed_` when pooling; store the pressed `QPersistentModelIndex` and require it to match on release |
| P2-7 | `components/src/recycler.cpp:152`<br>`components/src/recycler.cpp:681` | P2 | `delegate_` is a raw, non-owned pointer with no `QPointer`/`destroyed` hook, and `paintItem()` dereferences it; a delegate destroyed before the view leaves a dangling pointer | Use `QPointer<QStyledItemDelegate>`, or null it from a `destroyed` connection |
| P2-8 | `layout/src/appbar.cpp:64-69` | P2 | The `destroyed` connection is made before the `previous == button` early return, so calling `setNavigationButton()` repeatedly with the same button adds one connection per call (unbounded growth) | Connect only when the role actually changes, and disconnect the previous button when it loses the role |
| P2-9 | `layer/src/layer_stack.cpp:94-100` | P2 | `advance()` (and `hitTest()`) traverse `layers_` without the `painting_` guard used by `paint()`; a public `Layer` subclass that mutates the stack from `advance` invalidates the traversal (documented precondition only) | Reuse one mutation guard for all traversals, or snapshot stable pointers for `advance`/`hitTest` |

---

### P0-1 — `NavigationView::activate()` reads a freed `Entry` after `selectionChanged`

**file:line** `components/src/navigation_view.cpp:352-363` (signal at `:183-200`)

Reachable call path:

```text
mouse press                     navigation_view.cpp:538
  -> activate(index)            navigation_view.cpp:560, 352
     -> const Entry& entry = entries_.at(index)      :357
     -> setSelectedIndex(index)                      :361
        -> selected_ = index; requestUpdate()        :197-198
        -> emit selectionChanged(selected_)          :199
           -> host slot calls setEntries(...)        :106   (entries_ reallocated)
              or clearEntries()                      :122   (entries_ cleared)
     -> emit itemActivated(index, entry.label)       :362   <-- dangling reference
```

`QList::at()` returns a reference into the list's storage. `selectionChanged`
is emitted synchronously from inside `setSelectedIndex()`, before the second
signal is constructed. A handler that replaces or clears the entries — the
normal reaction when the selection determines which entries to show — frees the
storage that `entry` refers to. Reading `entry.label` then dereferences freed
memory. No precondition is broken: `setEntries()` and `clearEntries()` are
public, and a selection handler that calls them is ordinary code.

Rules: MEM50-CPP (do not access freed memory), C++ Core Guidelines E.3 (RAII /
state coherence across a callback) and E.1 (a component invoking user code must
survive it).

Patch sketch:

```cpp
void NavigationView::activate(int index)
{
    ...
    const Entry& entry = entries_.at(index);
    ...
    // Take the payload before any observer can replace entries_.
    const QString label = entry.label;
    setSelectedIndex(index);
    emit itemActivated(index, label);
}
```

An alternative is to emit `itemActivated` before `selectionChanged`; that
changes signal order, so the local copy is the smaller fix.

---

### P0-2 — `Radio::uncheckSiblings()` can dereference a deleted sibling

**file:line** `components/src/radio.cpp:121-133` (emission at `:79-98`)

Reachable call path:

```text
Radio::setChecked(true)                         radio.cpp:79
  -> uncheckSiblings()                           :121
     -> const QObjectList siblings = parent->children();   :127   (copy of raw pointers)
     -> for child in siblings
        -> qobject_cast<Radio*>(child)           :129
        -> radio->setChecked(false)              :131
           -> checked_ = false; emit toggled(false)        :85-87
              -> host slot deletes another Radio sibling    <-- direct delete
     -> next iteration: qobject_cast on the freed child   :129   (UAF)
```

`QObjectList` is copied, so the list itself is stable, but the copied
`QObject*` values are not guarded. A `toggled(false)` observer that removes a
radio option (a normal dynamic-form operation) frees one of those objects. The
next loop iteration calls `qobject_cast` on it, which dereferences the freed
`QObject`. If the handler deletes `this` instead, `checked_ = true` at `:95`
writes into freed memory after `uncheckSiblings()` returns.

`deleteLater()` in the handler happens to be safe; a direct `delete` — common in
widget-teardown code — is not. The component must not assume either.

Rules: MEM50-CPP, C++ Core Guidelines E.3 (a callback may destroy state the
caller still traverses), ES.20 (object state must be valid across calls).

Patch sketch:

```cpp
void Radio::uncheckSiblings()
{
    QWidget* parent = parentWidget();
    if (parent == nullptr)
        return;

    // Snapshot guarded handles: a toggled() handler may delete a sibling.
    QList<QPointer<Radio>> siblings;
    const QObjectList children = parent->children();
    for (QObject* child : children) {
        if (auto* radio = qobject_cast<Radio*>(child)) {
            if (radio != this && radio->groupId_ == groupId_)
                siblings.append(QPointer<Radio>(radio));
        }
    }

    for (const QPointer<Radio>& radio : siblings) {
        if (radio)
            radio->setChecked(false);
    }
}
```

Re-checking `this` after the loop is not necessary for the copy fix, but a
re-entrancy flag would also stop a handler from re-entering `setChecked(true)`
on the same radio while the first call is mid-flight.

---

### P0-3 — `Coordinator` dispatch invalidates its own behaviour list

**file:line** `layout/src/coordinator.cpp:133-137`, `:148-151`, `:161-166`
(registration/removal at `:40-64`, `:75-87`)

Reachable call path (in-tree behaviour):

```text
Coordinator::dispatchNestedPreScroll(delta, out)        coordinator.cpp:125
  -> for (Entry& entry : entries_)                      :134
     -> entry.behaviour->onNestedPreScroll(...)         :136
        AppBarBehavior::onNestedPreScroll               appbar.cpp:450
          -> appBar_->setScrollOffset(...)              appbar.cpp:458
             -> emit offsetChanged(offset)              appbar.cpp:174
                -> host slot deletes the AppBar/Coordinator
                   -> QObject::destroyed connection      coordinator.cpp:59
                      -> std::erase_if(entries_, ...)    :60
                         -> the executing behaviour is destroyed
  -> loop increments the invalidated iterator / entries_    <-- UAF
```

The same shape exists for `startNestedScroll()` (`:111-114`, virtual
`onStartNestedScroll`), `dispatchNestedScroll()` (`:148-151`) and
`stopNestedScroll()` (`:161-164`, followed by `emit nestedScrollStopped()` on
`this`). The header documents registration order as the dependency order, not
that callbacks may not unregister; `removeBehaviour()` is public. A custom
`CoordinatorBehaviour` that removes itself after a one-shot gesture, or adds a
behaviour for another child as scrolling starts, is therefore enough. Deleting a
child is worse: the `destroyed` connection erases the `Entry`, destroying the
`CoordinatorBehaviour` whose virtual function is currently on the stack.

Rules: MEM50-CPP, C++ Core Guidelines E.3/E.6 (re-entrancy and object lifetime
in callbacks), ES.20 (the container must remain valid during traversal).

Patch sketch — a dispatch guard with deferred mutation:

```cpp
// coordinator.h
bool dispatching_ = false;
std::vector<std::unique_ptr<CoordinatorBehaviour>> graveyard_;

// coordinator.cpp
void Coordinator::dispatchNestedPreScroll(const QPointF& delta, QPointF* consumed)
{
    if (!scrolling_)
        return;

    QPointF total;
    struct Guard {
        explicit Guard(bool& flag) : target(flag) { target = true; }
        ~Guard() { target = false; }
        bool& target;
    } guard(dispatching_);

    for (Entry& entry : entries_) {
        CoordinatorBehaviour* behaviour = entry.behaviour.get();
        if (behaviour == nullptr)
            continue;
        behaviour->onNestedPreScroll(delta, &total);
        // The callback may have queued a removal; entries_ itself is stable
        // because addBehaviour/removeBehaviour only mark under dispatching_.
    }

    if (consumed != nullptr)
        *consumed = total;
}

void Coordinator::removeBehaviour(View* child)
{
    ...
    if (dispatching_) {
        // Mark dead, keep the object alive until the loop unwinds.
        graveyard_.push_back(std::move(it->behaviour));
        entries_.erase(it);
        return true;
    }
    ...
}
```

The child-`destroyed` lambda must not `erase_if` during dispatch either; mark
`entry.child = nullptr` and let the post-dispatch pass apply removals. The
important property is that no `CoordinatorBehaviour` is destroyed while a
callback into it is on the stack.

---

### P1-1 — `AppBarBehavior` reads `scrollOffset()` back after emitting a signal

**file:line** `layout/src/appbar.cpp:450-464`

Reachable call path:

```text
Coordinator::dispatchNestedPreScroll                  coordinator.cpp:134-137
  -> AppBarBehavior::onNestedPreScroll                appbar.cpp:450
     -> const qreal before = appBar_->scrollOffset(); :457
     -> appBar_->setScrollOffset(before + delta.y()); :458
        -> emit offsetChanged(scrollOffset_)          appbar.cpp:174
           -> handler calls setScrollOffset()/setScrollRange()
              (or deletes the AppBar)
     -> consumed += appBar_->scrollOffset() - before; :459   <-- re-read
```

`setScrollOffset()` clamps and may be re-entered from `offsetChanged`. Reading
the property again assumes the value it just set is still there. A handler that
snaps the offset, changes the range, or sets an offset for another reason makes
`after - before` describe the handler's change instead of the delta this
behaviour applied, so the content scrolls by the wrong amount. If the handler
deletes the AppBar/coordinator, `appBar_` (a `QPointer`) becomes null and line
459 dereferences it. P0-3 destroys the behaviour first today, which is why this
is filed as P1 rather than a second P0; it must still be fixed because it
survives the P0-3 dispatch fix.

Rules: C++ Core Guidelines E.3, MEM50-CPP, and the property-readback rule: do
not read mutable state back after calling user code.

Patch sketch:

```cpp
void AppBarBehavior::onNestedPreScroll(const QPointF& delta, QPointF* consumed)
{
    if (appBar_ == nullptr || consumed == nullptr || delta.y() == 0.0)
        return;

    const qreal before = appBar_->scrollOffset();
    const qreal target = std::clamp(before + delta.y(), qreal(0.0),
                                    appBar_->scrollRange());
    appBar_->setScrollOffset(target);

    // The handler may have deleted the bar; do not touch it again.
    if (appBar_ == nullptr)
        return;

    consumed->setY(consumed->y() + (target - before));

    if (Coordinator* coordinator = this->coordinator(); coordinator != nullptr)
        coordinator->requestLayout();
}
```

Even here, the behaviour object itself can be deleted by the signal today; P0-3's
deferred-destruction rule is the prerequisite for this fix to be sufficient.

---

### P1-2 — `Recycler` binds a realised item once and never re-binds it

**file:line** `components/src/recycler.cpp:426-431`, `:443-479`, `:495-544`,
`:143-160`

Reachable call path:

```text
model->insertRows(...)                            QAbstractItemModel
  -> rowsInserted                                 recycler.cpp:431
     -> rebuild()                                  :482
        -> relayout()                              :495
           -> existing active_ items are kept while item->row in [first,last]  :516-522
           -> realised[item->row] = item                                       :524-527
           -> rows with an entry are reused with no bindItem()                 :529-537
           -> bindItem() runs only for newly acquired items                    :536
```

`bindItem()` (`:596-607`) is the only place that sets `item->index` and calls
the user `ItemBinder`. `dataChanged` (`:426-429`) only calls `updateItems()`,
which calls `QWidget::update()` (`:489-493`); it never re-runs the binder. Three
consequences:

1. **Insert/remove above the viewport.** `item->row` is a plain `int` and is not
   adjusted. The widget paints its old index / old bound content at the old row,
   while `QPersistentModelIndex` has moved to the shifted row. A factory row
   shows stale content; the default renderer shows the *wrong row's* data at
   that slot; a click selects the shifted row. Only a row that enters the
   window for the first time is bound; the rows already in `active_` keep their
   old index and payload.
2. **`modelReset`.** `current_` and the scroll offset are cleared (`:461-477`),
   but `active_` is not. Every item's persistent index becomes invalid, and the
   next `relayout()` keeps the items because `item->row` is still inside the
   window. The rows paint blank until they are scrolled out and re-acquired.
3. **`setModel`.** `:143-160` swaps `model_` and calls `rebuild()` without
   clearing `active_`. If the new model has rows in the same range, the old
   rows stay on screen and the old persistent indices are used for painting and
   clicking; if the old model was destroyed first, the indices are invalid and
   the rows are blank.

No out-of-bounds access occurs — the geometry uses `item->row`, and the paint
path converts the persistent index back through `QModelIndex(item.index)` — so
this is a wrong-output P1 rather than a crash.

Rules: ES.20/ES.21 (a realised item must represent one valid model index),
E.3 (a model signal and the view's cached state must stay coherent).

Patch sketch:

```cpp
// Re-bind the rows that stayed in the window; release the rest.
for (Item* item : std::as_const(active_)) {
    if (item->row >= first && item->row <= last) {
        bindItem(item, model_->index(item->row, 0));  // updates index + binder
        applyItemGeometry(item);
    } else {
        releaseItem(item);
    }
}
// easiest correct version for structural changes:
void Recycler::rebuild()
{
    for (Item* item : active_)
        releaseItem(item);
    active_.clear();
    clampScroll();
    relayout();
    requestUpdate();
}
```

`dataChanged` should call the same bind path for the affected `topLeft.row() ..
bottomRight.row()` span (or at least re-run `binder_`), then `update()`. Also
clear the items on `setModel` and connect `model_->destroyed` to a state reset;
`QPointer` nulling alone does not clear the visible rows.

---

### P1-3 — `Recycler` lets factory/binder exceptions cross Qt's model-signal frames

**file:line** `components/src/recycler.cpp:431`, `:479` (callbacks at `:559-566`,
`:601-602`)

Reachable call path:

```text
QAbstractItemModel::insertRows
  -> rowsInserted signal                          recycler.cpp:431
     -> rebuild() -> relayout()                   :482, :495
        -> acquireItem(viewType)                  :533, :546
           -> factory_(viewType, item)            :560   <-- std::function user code
        -> bindItem(item, index)                  :536, :596
           -> binder_(widget, index)              :602   <-- std::function user code
```

Neither `rebuild()` nor `relayout()` is inside a `try` on this path.
`rowsRemoved` (`:443-459`) and `modelReset` (`:461-477`) have `try`/`catch (...)`,
but `rowsInserted` and `layoutChanged` do not, and neither catch reports
anything. A throwing factory or binder therefore unwinds through
`QAbstractItemModel`'s `endInsertRows()`/`layoutChanged()` emission. Qt does not
define exceptions crossing its signal machinery; at best the model is left
half-notified and at worst the exception reaches a `noexcept` frame
(ERR50-CPP/ERR51-CPP). `setModel()` and `setScrollOffset()` can also throw from
the same path, but there the exception reaches the caller directly, which is at
least a defined boundary.

Rules: CERT ERR50-CPP, ERR51-CPP; C++ Core Guidelines E.1/E.2 (decide at the
callback boundary), E.3 (leave the item list coherent after a failure).

Patch sketch:

```cpp
void Recycler::rebuildGuarded(const char* where)
{
    try {
        clampScroll();
        relayout();
        requestUpdate();
    } catch (const std::exception& error) {
        qWarning("md::Recycler: %s failed: %s", where, error.what());
    } catch (...) {
        qWarning("md::Recycler: %s failed with an unknown exception", where);
    }
}

// in connectModel():
connect(model_, &QAbstractItemModel::rowsInserted, this,
        [this] { rebuildGuarded("rowsInserted"); });
connect(model_, &QAbstractItemModel::layoutChanged, this,
        [this] { rebuildGuarded("layoutChanged"); });
// rowsRemoved / modelReset: keep their repair code, but use the same reporter
// instead of a silent catch (...).
```

`bindItem()` already stores `item->index` before calling `binder_`, so the item
stays valid if the binder throws; the guarded rebuild can leave the window
partially populated and the next scroll repairs it.

---

### P1-4 — `NavigationView` hit-tests rows through the header

**file:line** `components/src/navigation_view.cpp:311-318` (paint clip at
`:412-414`)

Reachable call path:

```text
mouse press at (x, 60) with header visible and scroll_ = 48
  -> NavigationView::mousePressEvent              navigation_view.cpp:538
     -> entryIndexAt(position)                     :545, :311
        -> for i: entryRect(i).contains(position)  :313-314
           entryRect(0) = y 96 - 48 = 48 .. 96     :299-309
        -> returns 0                                <-- row under the header
     -> activate(0)                                 :560
```

Painting deliberately clips the entry run to the body (`:413-414`), and the
header is painted above it (`:387-410`), but `entryIndexAt()` iterates the full
widget rectangle. With a non-zero scroll offset the first (partly hidden) row
extends under the header. Clicking the header's title or subtitle activates that
hidden item; `mouseMoveEvent()` likewise highlights it. Nothing else upstream
clamps the position.

Rules: boundary correctness for a clipped viewport; the general hit-testing
rule that the painted clip is part of the target geometry.

Patch sketch:

```cpp
int NavigationView::entryIndexAt(const QPointF& position) const
{
    const qreal top = entriesTop();
    if (position.y() < top || position.y() >= height())
        return -1;

    for (int i = 0; i < entryCount(); ++i) {
        if (entryRect(i).contains(position))
            return i;
    }
    return -1;
}
```

Intersecting with `QRectF(0.0, top, width(), height() - top)` before the scan is
equivalent and also protects a header that is being resized.

---

### P2-1 — `Page` lets one view occupy two roles

**file:line** `components/src/page.cpp:208-224` (guard), `:249-276`
(`setContent`), `:283-310` (`setFooter`)

`addHeaderAction()` rejects `action == content_ || action == footer_`
(`:210-213`), but `setContent()` and `setFooter()` only guard against assigning
the same pointer to the same role. `page.setContent(footer)` and
`page.setFooter(content)` therefore reparent a view out of its old role while
the old field and list still refer to it. Example:

```text
page.addHeaderAction(&v)      -> v is a Page child, actions_ contains v
page.setContent(&v)           -> viewport_->addView(v) reparents it; actions_ still contains v
  -> Page::arrangeChildren() arranges v in the body (viewport) and then again
     in the header row (:580-599, using viewport-local geometry)
```

The result is wrong geometry rather than a crash, but the two code paths also
disagree about which role owns the widget (`headerActionsWidth()` measures it,
the destructor iterates `actions_`, and `setContent(nullptr)` hides a footer it
still references). Enforce the same exclusivity as `addHeaderAction()` in both
setters, or detach the view from the previous role before adopting it.

---

### P2-2 — `PopUp::eventFilter()` uses `this` after `close()` may delete it

**file:line** `components/src/popup.cpp:547-588` (press branch `:568-574`)

`close()` (`:515-526`) emits `closed()`. The outside-press branch calls
`close()` and then falls through to `return ViewGroup::eventFilter(watched,
event)` (`:587`). A `closed()` receiver that deletes the popup (or deletes the
parent window) frees `this`; the filter then makes a virtual member call on the
freed object. The Escape branch returns immediately (`:578-580`) and the anchor
`Hide` branch returns a literal (`:558-560`), so only the press branch has the
problem. A receiver using `deleteLater()` is safe; a direct `delete` is not,
and the component should not require the caller to know which one is used.

Fix: `close(); return false;` in the press branch, and use a `QPointer` guard if
any later code is added. (The filter is installed on the application, so the
press path is the common dismissal path, not an edge case.)

---

### P2-3 — `Notification` signals are delivered while the object is still on the stack

**file:line** `components/src/notification.cpp:449-451` (action), `:49-50` and
`:165-185` (timer/close), `:488-500` (`hideEvent`)

Two paths publish a signal and then keep using the notification:

```text
mouse release with an action hit       notification.cpp:431-455
  -> emit actionTriggered()            :450
     -> handler deletes the notification (or the parent window)
  -> dismissCurrent(true)              :451   <-- this is freed

QTimer::timeout                        :50
  -> dismissCurrent(true)              :165
     -> hide(); emit closed()          :176-180
        -> handler deletes the notification
           -> destroys the QTimer member while QTimer::timerEvent is unwinding
```

`close()` itself is safe after `actionTriggered` (it is idempotent and the
subsequent `dismissCurrent` sees an empty queue), but a direct `delete` is not.
Deleting the widget from a signal emitted during event handling is exactly the
case Qt documents `deleteLater()` for; the component should either queue the
timer-driven close (`QMetaObject::invokeMethod(this, [...], Qt::QueuedConnection)`)
or guard the post-signal call with a `QPointer<Notification>` and document that
receivers must use `deleteLater()`.

---

### P2-4 — `Rate` has no bound on `maximum_` and rounds an unbounded ratio

**file:line** `components/src/rate.cpp:73-83` (`setMax`), `:115-123` (`snap`),
`:189` (paint loop)

`setMax()` stores `std::max(1, maximum)` with no upper bound. Two consequences:

- `paintContent()` loops `for (int i = 0; i < maximum_; ++i)` and
  `measureOverride()` computes `star + (maximum_-1) * pitch`. A maximum coming
  from configuration or a model — e.g. `INT_MAX` from a malformed rating scale —
  makes one paint pass iterate two billion times and freezes the UI.
- `snap()` does `qRound(clamped / step_)`. `setStep()` accepts any positive
  qreal; a tiny positive step (or a `value_` that became NaN through
  `setValue(NaN())`, which `std::clamp` passes through) gives a NaN/Inf ratio,
  and converting an out-of-range floating value to `int` is undefined
  (CERT INT32-C). `setValue()` does not reject non-finite input.

Fix: clamp `maximum_` to a documented bound (e.g. 1..1000), reject
non-finite `value`/`step`, and guard the ratio:

```cpp
const qreal ratio = clamped / step_;
if (!std::isfinite(ratio) || std::abs(ratio) > 1.0e9)
    return clamped;                    // or std::clamp(qRound64(...), ...)
```

The same `qRound` pattern exists in other components but with bounded inputs;
`Rate` is the one whose public inputs are unbounded.

---

### P2-5 — `Recycler::setPrefetchRows()` can overflow the window arithmetic

**file:line** `components/src/recycler.cpp:221-229` (setter), `:508-513`
(arithmetic)

`setPrefetchRows()` clamps only the lower bound (`std::max(0, rows)`).
`relayout()` computes:

```cpp
const int first = std::max(0, static_cast<int>(std::floor(scrollOffset_ / rh))
                                  - prefetchRows_);
const int last = std::min(count - 1,
                          static_cast<int>(std::ceil((scrollOffset_ + height()) / rh))
                              + prefetchRows_);
```

For `prefetchRows_ == INT_MAX` and an ordinary viewport, the addition on the
`last` line overflows a signed `int` before `std::min` sees it — undefined
behaviour under CERT INT32-C. The resulting negative `last` also releases every
realised row. Clamp the setter (e.g. 0..1000) and/or compute the window in
`qint64`; `first`/`last` are then narrowed only after they are known to be in
range.

---

### P2-6 — A pooled `Recycler::Item` keeps its pressed state

**file:line** `components/src/recycler.cpp:61-82` (`Item` mouse handlers),
`:572-581` (`releaseItem`), `:546-554` (`acquireItem`)

`Item::pressed_` is set in `mousePressEvent()` and cleared only in
`mouseReleaseEvent()`. `releaseItem()` hides the row and returns it to the pool
without clearing `pressed_`; `acquireItem()` reuses it as-is. If the user presses
a row, scrolls with the wheel while holding the button (the scroll recycles the
pressed widget), and releases over the reused widget, the `mouseReleaseEvent`
sees `pressed_ == true` and `rect().contains(position)`, and calls
`handleItemClick(this)` for the **new** row. The row identity should be captured
at press time and compared on release (or at least clear `pressed_` in
`releaseItem()`); otherwise a click can activate a row the user never pressed.

---

### P2-7 — `Recycler` stores its delegate as a raw pointer

**file:line** `components/src/recycler.cpp:152` (member),
`:167-175` (`setItemDelegate`), `:655-681` (`paintItem`)

`delegate_` is documented as not owned, but it is not a `QPointer` and no
`destroyed` connection clears it. `paintItem()` passes it to
`delegate_->paint(...)` (`:681`). If the caller deletes the delegate before the
view (a stack delegate in a test, a document that replaces its delegate, a
teardown order different from the view), the next paint dereferences freed
memory. Use `QPointer<QStyledItemDelegate> delegate_` (the header already
includes `QPointer`) or connect `delegate->destroyed` to a setter that nulls the
member. The ownership note should stay, but the view should tolerate the
delegate disappearing.

---

### P2-8 — `AppBar::setNavigationButton()` accumulates duplicate connections

**file:line** `layout/src/appbar.cpp:59-89` (connect at `:64-69`, early return at
`:71-75`)

The `destroyed` connection is installed *before* the function checks whether the
button already has the role. Calling `setNavigationButton(button)` on every
rebuild with the same button adds one connection per call; each closure captures
the same `this`/`button` and runs on destruction. It is bounded only by how
often the host refreshes the bar. Move the `connect` into the
`previous != button` branch and disconnect the old button when it loses the
role, or keep a small set of connected buttons.

---

### P2-9 — `LayerStack::advance()` and `hitTest()` traverse unguarded

**file:line** `layer/src/layer_stack.cpp:94-100` (`advance`), `:102-112`
(`hitTest`); documented precondition in `layer/include/md/layer/layer_stack.h:16-17`

`paint()` takes the `painting_` guard and ignores mutators while it walks
`layers_` (`:54-92`). `advance()` and `hitTest()` use the same vector without
that guard. The header documents that their callbacks must not mutate the stack,
but `Layer` is a public polymorphic interface and a host may legitimately write
an animated custom layer. A layer that calls `add()`/`clear()` from `advance()`
reallocates or clears the vector under the range-for and produces a UAF; a
throwing `advance()` has no per-layer isolation either. Reuse the same busy
flag for all traversals (renamed `traversing_`), ignore mutators while it is
set, and isolate `advance()` exceptions the way `paint()` does. This is P2
because there is no in-tree caller that mutates from `advance`, and the header
already states the precondition.

---

## Checked and found sound

The following invariants were checked at the named sites. This list is limited
to things a finding above does **not** report.

### Boundary handling — zero/negative sizes, empty containers, ranges

- **Empty containers and `size()-1`.** `NavigationBar` (`destinationCount() == 0`
  at `:167`, `:220`, `count` division at `:145`, `:227`), `NavigationView`
  (`entries_.isEmpty()` at `:336`, bounds checks in `entry()`, `entryRect()`,
  `setEntryEnabled()`, `setSelectedIndex()`), `Pager` (`pageCount()` guards at
  `:231`, `:250`, `:262`, `:274`, `:485`, `:507`), `Pagination` (`pageCount_`
  is clamped to at least 1 at `:177`; `buttonSize()` divides by a slot count of
  at least five), `PieChart` (`buildSlices()` returns empty for a non-positive
  total, `:327`), `PolarPlotChart` (`hasPattern()`, `pointCount()` guards in
  every accessor), `RadarChart` (`axes_.size() < 3` at `:181`, `:213`),
  `Recycler` (`rowCount() == 0` at `:500`) and `LayerStack::at()` all guard.
- **Zero/negative rectangles.** `Page::bodyRect()`/`footerRect()`/`headerRect()`
  use `std::min`/`std::max`/`isEmpty()` (`page.cpp:364-416`);
  `PageViewport::arrangeChildren()` clamps width/height (`:89-102`);
  `Pager::pageArea()`/`indicatorBarRect()` use `max(0)` (`pager.cpp:453-475`);
  `Pagination::rowRect()`/`buttonSize()` use `std::max<qreal>(0.0, ...)`
  (`pagination.cpp:64-87`); `Recycler::applyItemGeometry()` uses
  `max(1, ...)` for item dimensions (`recycler.cpp:583-594`); `AppBar` and
  `Collapse` clamp available width/height before measuring
  (`appbar.cpp:292-296`, `collapse.cpp:202-231`); `Popup::bodyFor()` returns an
  empty rect when the arrow/padding consumes the surface, and
  `desiredSize()` is capped by the screen in `reposition()`.
- **Division by a zero range.** `AppBar::collapseFraction()` returns 0 when
  `scrollRange_ <= 0` (`:177-184`); `PieChart::buildSlices()` returns early for
  `!(total > 0)` (`:327`); `PolarPlotChart::radialFractionForValue()` checks
  `maximum > 0` and the dB span is bounded by `radialFloorDb_` in [-200, -1]
  (`:461-483`); `RadarChart::normalisedValue()` checks `span > 0`
  (`:169-177`); `Rate::valueFromX()` checks `pitch > 0` (`:131-132`);
  `Recycler::paintContent()` checks `contentHeight <= 0` before dividing
  (`:723-728`).
- **Scroll offset beyond content.** `NavigationView::clampScroll()`
  (`:256-261`), `Page::maxScrollOffset()/clampScroll()` (`:498-513`),
  `Pager::finishAnimation()`/`commitAnimation()` (`:343-368`) and
  `Recycler::clampScroll()/ensureVisible()` (`:398-419`) all clamp to
  `[0, maximum]`; `Pager::dampedShift()`'s denominator is
  `kEdgeLimit + |shift| > 0`.
- **Invalid model indices.** `Recycler` stores selection and item identity as
  `QPersistentModelIndex` (`current_`, `Item::index`), checks
  `QModelIndex(item->index).isValid()` in both click handlers (`:609-627`),
  bounds-checks `indexAtRow()` (`:282-288`), and computes geometry from a
  clamped row. The *binding freshness* problem is P1-2; it is not an
  out-of-bounds index.
- **Painting with an inactive painter / empty content.** Most components
  early-return on an empty content/plot rect (`navigation_bar.cpp:220`,
  `pagination.cpp:272`, `pie_chart.cpp:446`, `polar_plot_chart.cpp:519`,
  `radar_chart.cpp:207`, `progress.cpp:192`, `radio.cpp:148`, `rate.cpp:165`,
  `notification.cpp:346`, `popup.cpp:608`, `ring.cpp:162`). `NavigationView`,
  `Page` and `Pager` derive empty child rectangles instead of early-returning
  (`navigation_view.cpp:387-414`, `page.cpp:602-661`, `pager.cpp:607-645`), so
  their QPainter calls are no-ops on a zero-sized widget. Layer stages use
  `bounds.isEmpty() || !painter.isActive()` (`background.cpp:20`,
  `elevation.cpp:121`, `foreground.cpp:37/76/119`, `ripple_layer.cpp:58`). No
  `QImage`/`QPixmap` device is used in this scope, so the "null painter device"
  checklist item has no site here.

### Error handling — callbacks, exceptions, destructors

- **View hooks are guarded.** `View::measure/arrange/paintEvent/event` run
  through `view::guard` (`view/src/view.cpp:114-145`, `:429-497`;
  `view/include/md/view/error_guard.h`), so component `measureOverride`,
  `arrangeOverride`, `paintContent` and Qt event-handler overrides that throw do
  not unwind into Qt. This is why `Collapse::measureOverride`/`applyExpansion`
  and `Popup::measureOverride`, which have no local `try`, are not separate
  findings.
- **Chart-specific catches are local and complete.** `PieChart::paintPlot()`,
  `measureOverride()`, `onShown()` and `onThemeChanged()` (`pie_chart.cpp:431-655`),
  `PolarPlotChart::paintPlotAt()`/`paintEmptyState()`
  (`polar_plot_chart.cpp:512-770`), `RadarChart::paintPlot()` (`radar_chart.cpp:205-345`)
  catch `std::exception` and `...`; layer stages isolate decorative failures
  (`layer/src/*.cpp`). The catches are silent in the layer case — a persistent
  layer bug is invisible except as a blank frame — but no exception escapes a
  Qt paint frame. Adding a once-per-layer `qWarning` would improve
  observability; it is not a correctness finding.
- **`noexcept`/`throw` boundaries.** No audited component declares a `noexcept`
  virtual that calls user code, and none throws across a Qt virtual. The
  `Recycler` factory/binder path is the exception (P1-3). `Animator` explicitly
  swallows callback exceptions and guards destructor-time state
  (`animation/src/animator.cpp:369-393`), so `Progress`/`Ring`'s animation
  callbacks are safe.
- **Destructors.** `Layer` and `CoordinatorBehaviour` have virtual destructors
  (`layer.h:33`, `coordinator.h:47`); `AppBar`/`Collapse`/`Page` stop/hand off
  their owned work before the QWidget base tears down children. No audited
  destructor throws.

### Lifetime, timers, animations and Qt conventions

- **Lambdas capturing `this`.** Every connection in scope whose receiver is the
  component uses `this` as the context object, so Qt disconnects it on
  destruction: `NavigationView`'s row ripple (`navigation_view.cpp:63-64`),
  `Notification`'s timer (`:50`), `Pager`'s timer (`pager.cpp:65`),
  `Progress`/`Ring`'s animator finished handler (`progress.cpp:42-45`,
  `ring.cpp:38-41`), `Collapse`'s animation (`collapse.cpp:50-62`),
  `Recycler`'s model connections (`recycler.cpp:426-479`) and destroyed-child
  hooks (`appbar.cpp:65-68`, `collapse.cpp:104-107`, `page.cpp:218-221`,
  `page.cpp:265-271`, `page.cpp:300-305`, `coordinator.cpp:59-61`). The
  `Recycler` factory/binder are `std::function`s (user code, not connections)
  and are covered by P1-3.
- **Timers/animations stopped on destruction.** `Notification::timer_` is a
  value member with a context connection; its destructor stops it, and
  `dismissCurrent()`/`close()`/`hideEvent()` all stop it (`notification.cpp:167`,
  `:280`, `:496`). `Pager::timer_` is a child QTimer stopped by every terminal
  transition (`pager.cpp:292`, `:305`, `:316`, `:345`, `:357`) and destroyed
  with the widget. `Collapse::animation_` is a value member; `onHidden()` stops
  it (`collapse.cpp:328-342`) and its destructor stops it. `Progress`/`Ring`
  use `animation::Animator`, whose `~Animator()` invalidates the callback and
  stops both the animation and its delay timer
  (`animation/src/animator.cpp:105-114`), with a `QPointer` guard around the
  callback (`:380-391`). `RippleLayer` owns its controller by value
  (`ripple_layer.cpp:11-23`).
- **QPointer vs raw pointers.** `PopUp` uses `QPointer` for both anchor and
  content (`popup.h:115-116`) and checks them before every use;
  `Notification` uses `QPointer<QWidget> anchor_`; `AppBarBehavior` uses
  `QPointer<AppBar>`; `Recycler` uses `QPointer<QAbstractItemModel>`.
  `Page`/`AppBar`/`Collapse`/`Radio` keep raw child pointers but install
  `destroyed` hooks for every one (`page.cpp:265/300`, `appbar.cpp:65`,
  `collapse.cpp:104`). The two gaps are P1-1 (`QPointer` read-back) and P2-7
  (delegate).
- **Opacity, clip and painter state.** Every layer/component that calls
  `painter.save()` restores on all normal paths; the `try`/`catch` blocks in
  `layer/src/foreground.cpp`, `background.cpp`, `elevation.cpp` and
  `ripple_layer.cpp` restore the painter after a caught failure, and
  `PolarPlotChart`/`RadarChart` use a `PainterStateGuard` whose destructor
  restores (`polar_plot_chart.cpp:67-78`, `radar_chart.cpp:59-70`).
  `StateOverlayLayer`/`BackgroundLayer` use `Qt::IntersectClip`, not
  `ReplaceClip`, so a host clip is not discarded (`foreground.cpp:60`,
  `background.cpp:34`). `RippleLayer::paint()` intersects its clip with the
  bounds it was asked to paint (`ripple_layer.cpp:65-73`).
- **Painting outside `rect()`.** Rows/items are clipped by the widget or by an
  explicit clip (`NavigationView` body clip, `PageViewport::paintChildren()`,
  `NavigationBar` per-slot clip, `Rate` star clips, `PieChart`/`Polar`/`Radar`
  plot clip). No component paints into a different widget's painter.

### `layer/` and `layout/` modules

- **`LayerStack`** enforces paint-order forward / hit-test reverse
  (`layer_stack.cpp:81-112`), ignores null layers, clamps `insert()` indices
  (`:28-40`), keeps `painting_` reset on every exit via a scope guard
  (`:64-79`), and isolates a throwing stage (`:87-90`). The only gap is
  `advance()`/`hitTest()` mutation (P2-9).
- **`ElevationLayer`** normalises an out-of-range enum to Level0 on every entry
  point (`elevation.cpp:27-39`, `:100-108`), treats an invalid colour as "use
  the theme role" (`:110-116`) and skips empty bounds/inactive painters
  (`:121-123`).
- **`SurfaceContext`** keeps the theme as a documented non-owning reference
  (`surface_context.h:8-16`) and resolves explicit colours before the tonal
  fallback (`layer.cpp:12-30`); no context is stored, so a `ThemeScope`
  lifetime bug is not introduced here.
- **`Constraint`** rejects negative/non-finite anchors at set time
  (`constraint.h:256-266`), clamps a conflicting inset pair so it cannot push
  the child outside the content (`:156-171`, `:196-204`), bounds the measured
  child to the available rect (`:217-218`, `:240-241`) and guards child-hook
  exceptions in `arrangeChildren` (`:142-148`).
- **`Frame`/`Linear`** cap stacked children with `capTo()` (`frame.h:105-106`,
  `layout/alignment.h:98-105`), clamp negative/NaN margins and spacing through
  `layout_detail::length()` (`alignment.h:112-115`), use `qMax(0, ...)` for the
  available rect (`frame.h:127-128`, `linear.h:187-188`) and guard the arrange
  path against child exceptions (`linear.h:162-173`).
- **`Coordinator`** prevents recursive `LayoutRequest` arranging with
  `rearranging_` (`coordinator.cpp:223-236`), clamps behaviour-provided geometry
  to the content rect (`:214-218`) and nulls each behaviour's back-pointer
  before destroying it (`:26-38`). The dispatch mutation problem is P0-3.
- **`AppBar`/`Collapse`** clamp/normalise every public qreal length through
  `layout_detail::length()` (`appbar.cpp:123-161`, `collapse.cpp:76-86`),
  divide only after a positive-range check (`appbar.cpp:177-184`) and clamp
  body/header rects (`collapse.cpp:215-232`). `AppBar` wraps
  `measureBar()`/`arrangeBar()` in try/catch (`appbar.cpp:232-243`,
  `:274-285`); `Collapse` relies on the base-class `View::measure/arrange`
  guard for its own hooks, which is sufficient.

### Components with no finding beyond the table

`navigation_bar`, `pager`, `pagination`, `progress`, `pie_chart`,
`polar_plot_chart`, `radar_chart` and `ring` were read in full and checked for
every item above; no memory-safety, boundary, range, painter or timer defect was
found. `notification` and `popup` have only the noted lifecycle hardening;
`page` only role exclusivity. `navigation_view`, `radio` and `recycler` are the
files with the P0/P1 state defects listed above.

---

## Coverage and method

- Read in full: the 15 `components/src/{n,p,r}*.cpp`, their 15 headers, all 7
  `layer/include/md/layer/*.h` + 6 `layer/src/*.cpp`, and all 7
  `layout/include/md/layout/*.h` + 4 `layout/src/*.cpp` (54 files).
- Read as supporting contract, not audited: `view/include/md/view/view.h`,
  `view_group.h`, `error_guard.h`, `view/src/view.cpp`,
  `view/src/view_group.cpp`, `core/include/md/core/measure_spec.h`, and
  `animation/include/md/animation/animator.h` + `animation/src/animator.cpp`
  (to establish which entry points are already guarded and how animation
  callbacks are protected).
- Cross-checks: the `a-c.md` report in this directory covers a disjoint
  component set and was not used as evidence; `ripple/` is a separate module and
  was outside the exact scope; `components/src/ripple.cpp` does not exist.
- No build, no test run, no runtime inspection. Findings are source-derived;
  line numbers refer to commit `5a26e13` and are exact as of this write. Five
  files outside this scope (`affix.cpp`, `anchor.cpp`, `chip.cpp`, `drawer.cpp`,
  `theme/src/theme.cpp`) were already modified in the working tree; they are
  disjoint from this scope and were not touched.

## Counts

| Severity | Count |
|---|---|
| P0 | **3** |
| P1 | **4** |
| P2 | **9** |
| **Total** | **16** |
