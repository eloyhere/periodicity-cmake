# Component backlog and batch plan

This file is the implementation backlog requested by the product owner. It is also the
coordination contract for the subagents that build the library: each batch owns a disjoint
set of files, and every batch is completed and verified before the next one starts.

Status legend: `done` · `running` · `queued`.

## Dependency order

```
core → tokens → event → theme → animation → drawable → layer → view → layout → components → gallery
```

`components` is where every widget in this backlog lives, except the layouts, which live in
`layout`. `RecyclerView` and its adapter machinery live under `components/recycler/`.

## Batches

| Batch | Scope | Files | Status |
| --- | --- | --- | --- |
| 0 | event layer: dispatch, capture/intercept/bubble, gestures, Qt bridge | `event/**`, `tests/event/**` | done |
| 1 | theme + animation | `theme/**`, `animation/**`, `tests/theme/**`, `tests/animation/**` | queued |
| 2 | drawable + layer | `drawable/**`, `layer/**`, `tests/drawable/**`, `tests/layer/**` | queued |
| 3 | view kernel | `view/**`, `tests/view/**` | queued |
| 4 | layouts | `layout/**`, `tests/layout/**` | queued |
| 5 | basic widgets | `components/**` (see 5a) | queued |
| 6 | input widgets | `components/**` (see 6a) | queued |
| 7 | overlays and surfaces | `components/**` (see 7a) | queued |
| 8 | RecyclerView infrastructure | `components/recycler/**` | queued |
| 9 | gallery | `apps/gallery/**` | queued |

## Batch 4 — layouts

| Widget | Notes |
| --- | --- |
| `LinearLayout` | Horizontal/vertical, weight, gravity, spacing from tokens |
| `FrameLayout` | Children stacked in declaration order; later children paint over earlier ones; gravity |
| `ConstraintLayout` | Anchors (start/end/top/bottom/baseline), biases, chains, aspect ratio, gone margins |
| `DrawerLayout` | Edge drawer with scrim, drag-to-open, `DrawerListener` |
| `CoordinatorLayout` | Behaviour objects attached to children (`CoordinatorBehaviour`), dependency ordering, nested scroll hook |
| `AppbarLayout` | Collapsing toolbar targets, scroll flags (`scroll`, `enterAlways`, `snap`) |
| `CollapseLayout` | Single expandable/collapsible section with header, animated height |

## Batch 5a — basic widgets

`Text`, `Icon`, `Button`, `CheckBox`, `Radio`, `Rate`, `Progress`, `Ring`, `Seekbar`, `Colour`.

| Widget | Notes |
| --- | --- |
| `Text` | Label with a type-scale style, ellipsis/wrap, alignment |
| `Icon` | Tinted `QIcon`/pixmap, optional RTL mirroring |
| `Button` | Filled, filled tonal, elevated, outlined, text variants; icon + label; disabled state |
| `CheckBox` | Tri-state, indeterminate, state-layer interaction |
| `Radio` | Group semantics, keyboard traversal within a group |
| `Rate` | Star rating, half steps optional, hover preview, keyboard adjustment |
| `Progress` | Linear and circular, determinate/indeterminate, buffer |
| `Ring` | Android-style ring animation: determinate arc plus indeterminate rotation/trim animation |
| `Seekbar` | Continuous/discrete slider with value label and optional ticks |
| `Colour` | Saturation/value panel + hue strip + alpha, hex input, recent colours |

## Batch 6a — input widgets

`TextInput`, `NumberInput`, `Dropdown`, `Select`, `Spinner`, `NumberPicker`, `Complete`, `Date`,
`DateTime`.

| Widget | Notes |
| --- | --- |
| `TextInput` | Material filled and outlined text fields: floating label, helper/error text, leading/trailing icons, character counter |
| `NumberInput` | Numeric text field with increment/decrement affordances, min/max, step, formatting |
| `Dropdown` | Anchored list of actions/options opened from a trigger widget |
| `Select` | Single-choice field that opens a `Dropdown` and renders the current value |
| `Spinner` | Android-style spinner: modal list selection, adapter-backed |
| `NumberPicker` | Wheel picker: adapter-driven items, cyclic scrolling, typed value with autocomplete, divider and fading edges |
| `Complete` | Editable field with adapter-backed suggestion dropdown, filtering and inline completion |
| `Date` | Calendar month grid, year/month navigation, min/max, ISO formatting |
| `DateTime` | Date plus time selection, timezone-naive local `QDateTime` |

## Batch 7a — overlays and surfaces

`Toolbar`, `Snackbar`, `Dialog`, `Popover`, `Tooltip`, `Menu`.

| Widget | Notes |
| --- | --- |
| `Toolbar` | Top app bar: title, navigation icon, actions, overflow menu, scroll elevation |
| `Snackbar` | Transient bottom message with action, queueing, swipe/auto dismiss |
| `Dialog` | Modal scrim, title/content/actions, focus trap, Escape handling |
| `Popover` | Anchored floating surface with arrow, placement flipping, outside-click dismissal |
| `Tooltip` | Hover/focus triggered label with delay, placement, plain or rich text |
| `Menu` | Anchored menu with items, separators, submenus, keyboard navigation, checkable items |

## Batch 8 — RecyclerView

`RecyclerView` (virtual scrolling viewport), `Adapter` (item count, view types, create/bind),
`ViewHolder` (view reuse), `LayoutManager` (linear, grid), `ItemDecoration` and
`DecorationHelper` (insets and dividers), plus an object pool with a reuse policy and
`notifyItemChanged/Inserted/Removed` semantics.

Rules: only visible items plus a small prefetch margin are realised; recycled holders are
returned to the pool; layout managers own positioning and scrolling; decorations are drawn
before and after items; no per-item heap allocation in the steady-state scroll path.

## Cross-batch rules

1. Every widget is an `md::View` subclass and paints only through the `View` pipeline.
2. Only tokens provide colours, type, shape, elevation and motion; no component-local hex values.
3. Events flow through `md::EventDispatcher`; widgets never depend on Qt event inheritance for bubbling.
4. Each batch adds tests under `tests/<module>/` and must leave `ctest` green.
5. `tools/check_layering.cmake` must pass after every batch.
