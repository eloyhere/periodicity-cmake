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
| 0b | ripple layer: pointer-origin hover and press feedback | `ripple/**`, `tests/ripple/**` | done |
| 0c | demo application (Element-Plus-style showcase) | `demo/**` | done |
| 0d | schedule layer: priority queue (user input before UI) | `schedule/**`, `tests/schedule/**` | done |
| 1 | theme + animation | `theme/**`, `animation/**`, `tests/theme/**`, `tests/animation/**` | done |
| 2 | drawable + layer | `drawable/**`, `layer/**`, `tests/drawable/**`, `tests/layer/**` | done |
| 3 | view kernel | `view/**`, `tests/view/**` | done |
| 4 | layouts | `layout/**`, `tests/layout/**` | done |
| 5 | basic widgets | `components/**` (see 5a) | done |
| 6 | input widgets | `components/**` (see 6a) | done |
| 6b | Form + FormField | `components/**` (Form/FormItem/FormRule) | done |
| 7 | overlays and surfaces | `components/**` (see 7a) | done |
| 8 | RecyclerView infrastructure | `components/recycler/**` | done |
| 9 | gallery | `apps/gallery/**` | done |

The component catalogue (names, layer, and test file) lives in `docs/components.md`.

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
| `Ring` | Declarative-style ring animation: determinate arc plus indeterminate rotation/trim animation |
| `Seekbar` | Continuous/discrete slider with value label and optional ticks |
| `Colour` | Saturation/value panel + hue strip + alpha, hex input, recent colours |

## Batch 6a — input widgets

`TextInput`, `NumberInput`, `Dropdown`, `Select`, `Spinner`, `NumberPicker`, `Complete`, `Date`,
`DateTime`.

| Widget | Notes |
| --- | --- |
| `TextInput` | Design-baseline filled and outlined text fields: floating label, helper/error text, leading/trailing icons, character counter |
| `NumberInput` | Numeric text field with increment/decrement affordances, min/max, step, formatting |
| `Dropdown` | Anchored list of actions/options opened from a trigger widget |
| `Select` | Single-choice field that opens a `Dropdown` and renders the current value |
| `Spinner` | Declarative-style spinner: modal list selection, adapter-backed |
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
| `Dialog` | Modal family: Message, Confirm, SingleChoice, MultipleChoice, Input — see "Dialog variants" below |
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

## Dialog variants

`Dialog` is a family, not one configurable box. All variants share the same shell — scrim,
`extraLarge` (28 dp) container, title, body, action row, Escape/backdrop dismissal flags and a
focus trap — and differ in body and result:

| Variant | Body | Result | Notes |
| --- | --- | --- | --- |
| `MessageDialog` | Text (optionally rich) | `Accepted` only | Single confirming action; used for information and errors |
| `ConfirmDialog` | Text | `Accepted` / `Rejected` | Positive and negative actions; optional `destructive` styling for the positive action; default action is the negative one |
| `SingleChoiceDialog` | Radio list | selected index (`-1` when cancelled) | Optional preselection; list scrolls beyond ~6 items |
| `MultipleChoiceDialog` | Checkbox list | sorted list of selected indices | Optional minimum/maximum selection with the action disabled until satisfied |
| `InputDialog` | One text field (single or multi-line) | entered text | Optional validator returning an error string; helper/error text below the field; the action stays disabled while invalid |

Rules that apply to every variant:

1. The dialog is modal and owns a scrim layer (`scrim` role at the framework's scrim opacity).
2. Return values are delivered through a result type, not through mutable out-parameters.
3. Static convenience helpers (`message()`, `confirm()`, `singleChoice()`, `multipleChoice()`,
   `input()`) run the dialog modally and return the result, so callers do not manage lifetimes.
4. Keyboard: Escape maps to the negative result, Return to the default action; Tab cycles inside
   the dialog only (focus trap).
5. Width is capped (about 560 dp) and the body scrolls instead of the dialog growing without bound.

### Customising a dialog

Every variant derives from a base `Dialog` that can be composed by hand, so the convenience
helpers are a shortcut rather than the only entry point:

| API | Effect |
| --- | --- |
| `setTitle(QString)` / `setTitleView(QWidget*)` | Plain title, or a custom title area (icon + text, rich text, close button) |
| `setView(QWidget*)` | Arbitrary body content; the dialog takes ownership, replaces and deletes the previous body on the next event-loop turn so a widget can never be deleted while it is painting |
| `setPositiveButton(QString)`, `setNegativeButton(QString)`, `setNeutralButton(QString)` | Action labels; an empty text hides that action |
| `setPositiveAction(std::function<void()>)` etc. | Callbacks instead of only a modal result, for non-blocking (modeless) use |
| `setCancelable(bool)`, `setDismissOnBackdrop(bool)` | Whether Escape / clicking the scrim closes the dialog |
| `setWidthDp(qreal)` | Overrides the 560 dp cap for a deliberately narrow or wide dialog |
| `setIcon(const QIcon&)` | Optional leading icon in the title row |

`setView` is what makes the dialog a container: the Input and choice variants are just the base
dialog with a text field or a list installed as the body.

## Component lifecycle

Every widget inherits a lifecycle from `md::View`; components do not invent their own states.

```
Created ──attach()──▶ Attached ──show()──▶ Shown ⇄ Hidden ──hide()──▶ Detached ──▶ Destroyed
```

| Hook | Called when | A component uses it for |
| --- | --- | --- |
| `onAttached()` | The widget enters a parent (first show or reparenting) | Resolve the theme, allocate caches, connect to the model |
| `onShown()` | `QEvent::Show` | Start animations (Ring, indeterminate Progress), subscribe to timers |
| `onHidden()` | `QEvent::Hide` | Stop animations and timers, pause heavy work |
| `onDetached()` | `QEvent::ParentChange` away from a parent | Release caches and disconnect |
| `onThemeChanged()` | `QEvent::ThemeChange` | Re-resolve colours, invalidate pixmaps |
| `onEnabledChanged(bool)` | `QEvent::EnabledChange` | Cancel in-flight interaction, disable ripples |

Rules:

1. A component must not paint before `onAttached()`; the base class only guarantees a paint after
   the widget is parented.
2. Animations and timers run only between `onShown()` and `onHidden()` — a hidden Progress must not
   keep a timer alive.
3. `onDetached()` releases caches, not the widget's model links; destruction follows Qt ownership.
4. Destructors stay empty of side effects: everything must be reversible in `onDetached()`.
5. `lifecycleState()` is readable, so a component can assert its own invariants.

## Form and FormField

`FormField` wraps one control and owns its label, helper text, validation state and required
marker; `Form` owns a list of fields, validates them together and emits a submit signal only when
every field passes.

| Type | API | Notes |
| --- | --- | --- |
| `FormField` | `setLabel(QString)`, `setControl(QWidget*)`, `setHelperText(QString)`, `setRequired(bool)`, `setValidator(std::function<QString(const QVariant&)>)`, `validate()`, `setError(QString)`, `isValid()`, `value()` / `setValue(QVariant)` | The validator returns an empty string for valid input; the error text is shown below the control and the control gets the `Invalid` state bit |
| `Form` | `addField(FormField*)`, `removeField(FormField*)`, `fields()`, `validate()`, `submit()`, `reset()`, `isDirty()`, signal `submitted()` | Validates top-to-bottom, focuses the first invalid field, and does not emit `submitted()` while invalid |

Layout: a field is label, control, helper/error line, in that order, with the error line reserving
its height so validation does not jump the form. A `Form` stacks fields vertically with the token
spacing and stops at a scroll boundary rather than growing without bound.

Both follow the component lifecycle: `FormField` re-validates on its control's value change, and
`Form` releases validation state in `onDetached()`.

## Cross-batch rules

1. Every widget is an `md::View` subclass and paints only through the `View` pipeline.
2. Only tokens provide colours, type, shape, elevation and motion; no component-local hex values.
3. Events flow through `md::EventDispatcher`; widgets never depend on Qt event inheritance for bubbling.
4. Each batch adds tests under `tests/<module>/` and must leave `ctest` green.
5. `tools/check_layering.cmake` must pass after every batch.
