# Rendering architecture

This document fixes the responsibilities of the drawing stack. It is the contract
that `gfx/`, `render/`, the two drawing contexts and the effect layer are held to.
Anything that does not fit these boundaries belongs in a different layer.

Decisions already taken (they are not open questions):

1. **Pipeline-first.** Rendering is owned by `gfx/` and `render/`; `Context2D` and
   `Context3D` are façades that *build* draw data and never rasterize anything
   themselves.
2. **Qt draws the text and the paths.** Glyph shaping, font fallback, sub-pixel
   positioning and path filling stay with `QPainter`, reached through one narrow
   `TextPainter`/`PathRasterizer` bridge. Everything else — meshes, textures,
   shaders, depth, blending — goes through our own rasterizer so it behaves the
   same on every machine and can be tested pixel by pixel.

## Layer order

```
core → mathematic → structure → store → pool → schedule → tokens → theme → event
     → drawable → layer → animation → ripple → view → layout
     → gfx → render → components
```

`gfx` may depend on `core`/`mathematic`/`structure` only. `render` may depend on
`gfx` plus `drawable`/`layer`. `components` may depend on `render` and everything
below it. Nothing below may include anything above: no `gfx` header may include a
`components` header, and `render` must not include `view`.

## Responsibilities

### `gfx/` — data and the software rasterizer

Backend-neutral, no widgets, no window system, no `QOpenGLWidget`.

| Type | Responsibility | Explicitly not |
| --- | --- | --- |
| `VertexLayout`, `Attribute` | Describe how a vertex buffer is read (offset, stride, type, normalised). | Owning the buffer. |
| `Mesh` | Vertex data plus topology (triangle list, line list, point list), validation, bounds, transforms. | Drawing. |
| `Texture`, `SamplerState`, `MipChain` | Pixels plus sampling rules (wrap, filter, mip selection). `sample(uv, lod)` is pure and bounds-safe for any uv. | Knowing a device or a context. |
| `Shader` | Program interface: attribute/uniform reflection, `Uniforms` bag, a software stage pair (vertex + fragment), and an optional GLSL source pair. | Compiling GLSL (that is `render`'s job). |
| `Pipeline` | Shader + blend state + depth state + raster state, as one immutable value. | Mutating itself during a draw. |
| `CommandBuffer` | An ordered list of draw commands: pipeline, geometry, bindings, viewport, scissor, and text/path commands. | Executing them. |
| `Rasterizer` | Executes a `CommandBuffer` into an `QImage`: barycentric interpolation, perspective-correct uv, z-buffer, blending, clipping. | Owning a window or a GL context. |

Rules for `gfx`:

- Every public setter validates: non-finite input is rejected with a `qWarning`
  and leaves the value unchanged; empty geometry is a no-op; an index outside the
  vertex range, or an allocation beyond the documented cap, is refused rather
  than clamped silently.
- `Rasterizer` is deterministic. The same `CommandBuffer` and the same target
  produce identical pixels on every platform, which is what makes the pixel
  assertions in the tests possible.
- No function in `gfx` allocates per draw call beyond one documented scratch
  buffer sized to the target.

### `render/` — the device

| Type | Responsibility |
| --- | --- |
| `RenderDevice` | Interface: create a target, create/update a texture, create a pipeline, `submit(CommandBuffer)`, `readBack`, `capabilities()`. |
| `SoftwareDevice` | Executes through `gfx::Rasterizer`; always available. |
| `GlDevice` | Same interface over an OpenGL FBO; compiled only under `PERIODICITY_HAS_OPENGL`; reports why it is unavailable otherwise. |
| `RenderTarget` | An offscreen surface (image or FBO) with a size, a device pixel ratio and a format. |
| `TextPainter` / `PathRasterizer` | The single bridge to `QPainter` for glyphs and paths. Both devices use it, so text looks identical whichever device is active. |
| `DeviceFactory` | Chooses the device from a capability probe, records the reason for the fallback. |

Rules for `render`:

- The two devices are interchangeable behind `RenderDevice`: a caller must not be
  able to tell them apart in the output, only in `capabilities()`.
- A device is created, used and destroyed on the UI thread. Texture *data* may be
  prepared on a worker thread; upload happens on the UI thread.
- A GL failure at any point degrades to `SoftwareDevice` for the rest of the
  frame and records the reason; it never throws and never leaves a half-bound
  pipeline.

### `components/` — the contexts

`Context2D` and `Context3D` own **state and intent**, never pixels.

| Type | Responsibility | Explicitly not |
| --- | --- | --- |
| `Context2D` | The painter's state stack (pen, brush, font, transform, clip, opacity, blend), the path builder, and the list of drawing intents (paths, text, images, meshes, effect passes). On submit it emits a `CommandBuffer`. | Rasterizing, allocating targets, knowing which device runs it. |
| `Context3D` | The scene: nodes, meshes, materials, camera, lights. Produces one `CommandBuffer` per frame from the visible nodes. Picking is CPU-side ray casting against the scene, not a render pass. | Owning a GL context, compiling shaders, drawing to a widget. |
| `Canvas` | Hosts a `RenderDevice` and a `RenderTarget`, decides software versus GL, forwards input, and repaints on the framework's frame scheduling. | Knowing 2D from 3D internals. |
| `SurfaceStyle`, `SamplerState`, `BlendMode`, `DepthState` | Value types shared by 2D and 3D. One definition each, in `gfx`. | Duplicated per context. |

Compatibility is a hard requirement: every method that exists today keeps its
signature and its observable behaviour. The contexts move from "paint right now
into a QPainter" to "record an intent", but a caller cannot tell the difference.

### `effects/` — post-process passes

Effects consume a `RenderTarget` and produce a new one. They never reach into a
context. Blur, glow, drop shadow, inner shadow, halftone, dither, posterize,
noise and the boolean path operations live here. Every effect declares a
`costEstimate()` so a caller can refuse an expensive stack before it runs, and
every effect is bounded by a documented pixel cap.

## Data flow

```
caller → Context2D/3D (state + intents)
       → CommandBuffer (gfx)
       → RenderDevice::submit (render)
           ├─ SoftwareDevice → Rasterizer (gfx) + TextPainter (QPainter)
           └─ GlDevice       → GL FBO pipeline + TextPainter (QPainter texture)
       → RenderTarget
       → Canvas::paintContent (blit into the widget)
```

A context never holds a device pointer beyond the submit call. A device never
holds a context pointer. The only object that knows both is `Canvas`.

## Threading

- Contexts are not thread-safe and are used on the UI thread only, which is the
  rule the rest of the framework already follows (`docs/threading.md`).
- Texture and mesh *construction* is pure data and may happen anywhere; upload
  and submission are UI-thread operations.
- A device probe (`DeviceFactory`) may be cached process-wide, but the decision
  is re-validated on the UI thread the first time it is used, because a GL
  context can disappear between frames.

## Failure model

- Invalid input: rejected at the boundary with a diagnostic, previous state kept.
- Missing capability: degrade to software, record the reason in the device's
  diagnostic string; drawing must still be correct.
- An exception from caller code (a shader fragment stage, an effect, a callback):
  caught at the boundary the framework already defines (`view::guard`), the frame
  is abandoned and the error recorded; the next frame must still work.

## Testing contract

- `gfx` is tested by pixel assertions on a `QImage`: interpolation, z-buffer
  ordering, wrap modes, blend results, culling.
- `render` is tested for interface equivalence: the same `CommandBuffer` under
  `SoftwareDevice` and `GlDevice` must agree within a documented tolerance, and
  the fallback path must be exercised when GL is unavailable.
- `components` keeps its existing tests; the contexts gain tests that assert the
  emitted `CommandBuffer` for a given sequence of calls (structure, not pixels),
  plus a small number of end-to-end pixel tests through `Canvas`.

## Decision record: how necessary is the OpenGL backend?

Scored on 13 November 2026, before `gfx` existed. The question was whether the
OpenGL path should block the software path, be built in parallel, or stay last.

| Dimension | Weight | Score | Why |
| --- | --- | --- | --- |
| The library's current workload (74 widgets, 25 charts, a few thousand triangles for the topography view) | 30 % | 35 | The software rasterizer with a z-buffer covers this comfortably; the measured chart budget is already well inside a frame. |
| Headroom for heavy 3D (≳50k triangles, large fill rates, real-time shader animation) | 25 % | 70 | This is where a CPU rasterizer stops being competitive, but nothing in the tree needs it yet. |
| Delivery risk | 20 % | 20 | `QOpenGLWidget` is a native child surface: stacking, clipping and context lifetime already caused one startup hang in a consumer application. Every GL feature carries that tax. |
| Portability of the design | 15 % | 55 | A `RenderDevice` interface is valuable regardless; GL is one implementation of it, and not the one that would cover D3D or Metal. |
| Testability | 10 % | 25 | The software path is deterministic and pixel-assertable; the GL path needs a context, so CI can only assert the fallback. |

**Weighted result: ≈ 40 %.** Necessary enough to keep a place in the design,
not necessary enough to lead with.

Consequences, which are what the number is for:

- The software device ships first and is the reference implementation.
- The GL device follows immediately after `render/` (phase 3), behind the same
  `RenderDevice`, opt-in, default off, with the parity test in place as soon as
  both devices exist. Approved on 13 November 2026.
- Nothing above `render/` may depend on GL being present.
- If the workload changes to heavy 3D, the number rises towards 75 % and the
  phase moves earlier. If the library stays a widget and chart toolkit, it falls
  towards 15 % and the phase can wait indefinitely without cost.
- A future `RhiDevice` (Vulkan/D3D/Metal) would replace the GL device's *value*,
  not its *interface*; that is why the device boundary is drawn where it is.

### Direct3D: not now

A Direct3D backend scores about 8 % on the same weighting. It buys nothing that
the design does not already have: on Windows the GL device works through the
driver, and where GL is unavailable the software device still renders correctly,
so D3D closes a *performance* gap only, and only on one platform. The costs are
real and specific: a second shader language and toolchain (HLSL, `dxc`), a second
set of shader variants, a Windows-only test matrix, and a backend that no CI job
here can exercise.

If a Direct3D path is ever genuinely needed, the right route is a `RhiDevice`
over Qt's RHI, which covers D3D11/12, Metal, Vulkan and GL from one shader
source. That requires `Qt6::ShaderTools` and the `qsb` tool, neither of which is
present in the Qt installation used here (checked 13 November 2026), so the
decision is deferred rather than refused: `RenderDevice` already leaves the slot
open and no code above it would change.



## Migration phases

1. `gfx`: geometry, texture, shader, pipeline, command buffer, software
   rasterizer, plus their tests. No change to any existing file. **Done
   (commit 64485d4).**
2. `render`: `RenderDevice`, `SoftwareDevice`, the `QPainter` text/path bridge,
   `DeviceFactory`, `RenderTarget`, `Renderer`, plus tests. Still no change to
   `Context*`. **Done (commit 113001c).**
3. `GlDevice` behind `PERIODICITY_HAS_OPENGL`, opt-in and default off, with the
   software/GL parity test. Moved ahead of the context migration because the two
   devices must agree before anything is built on top of them.
4. `components`: `Context2D` re-implemented on top of `gfx` commands, its public
   API unchanged; `Canvas` submits through a device. Followed by the additive
   fine-drawing API (path builder, stroke/fill styles, gradients, composited
   transforms, text alignment and measurement, images, render state).
5. `components`: `Context3D` re-implemented as a scene that emits commands;
   camera, lighting, picking preserved and extended.
6. `effects`: post-process layer wired to `RenderTarget`.
7. `Quick3DSurface`: the opt-in Quick/Quick3D component described below.

Each phase must leave the tree building with `ctest` at 100 % before the next
starts. Phases 4 and 5 are the only ones that touch public headers, and they may
not change a signature.

## Phase 7: Quick/Quick3D interop (opt-in, after phase 6)

Approved on 13 November 2026 as a **properly integrated component**, not a
convenience wrapper. It is sequenced last because it depends on the device layer,
on the theme bridge and on the Quick packages being present; it must never block
phases 3–6.

**What it is.** `Quick3DSurface`, a `View` that hosts a Quick3D scene inside the
widget tree and presents the same scene-level API the caller already knows from
`Context3D`: camera, nodes, materials, lights. The caller writes C++ and never
sees QML; a `setSource(QUrl)` escape hatch exists for callers who want their own
QML, and it is documented as "you are on your own for theming and input".

**Why a separate component rather than a mode of `Context3D`.** `QQuickWidget`
owns a scene graph, a render thread and its own graphics context. Putting that
behind the same object as a hand-written rasteriser would make every invariant in
this document conditional. Two components, one shared C++ scene description, is
the honest split.

**Rules it must follow**

- Compiled only when `PERIODICITY_HAS_QUICK3D` (`find_package(Qt6
  OPTIONAL_COMPONENTS QuickWidgets Quick3D)`). Absent packages mean the component
  reports unavailable; the rest of the library is untouched.
- The QML engine is created on the first `showEvent` of a visible top-level, not
  in the constructor. This is the rule `QOpenGLWidget` taught us when a consumer
  application hung on startup.
- No `QQuickWidget` exists while the widget is hidden; hiding stops the render
  thread's animations and releases the scene.
- Theme and language reach QML through **injected context properties** fed by
  `Theme::forWidget(this)` and `md::i18n`, so a QML scene uses the same tokens and
  the same translations as the painted components. Hard-coded colours in the
  built-in scene are a defect.
- Input: mouse and wheel are forwarded to the QML item; keyboard goes through the
  existing `EventDispatcher` path so shortcuts stay consistent.
- Failure: a missing module, a broken `setSource`, or a Quick3D backend failure
  leaves the widget showing a documented "unavailable" state with `lastError()`
  set. It never aborts the application and never half-renders.
- Accessibility: the surface exposes a description and a fallback text, so screen
  readers are not left with a blank rectangle.

**Mapping to the existing stack**

| `Context3D` concept | Quick3D equivalent |
| --- | --- |
| Camera (eye/target/up, perspective/orthographic) | `Quick3D.Camera` + `PerspectiveCamera`/`OrthographicCamera` |
| Mesh node | `Model` with a procedural `Geometry` or an imported mesh |
| `SurfaceStyle` | Surface-material fields mapped from the theme tokens |
| Directional/point light | `DirectionalLight`/`PointLight` |
| Picking | `View3D` ray picking, returned through the same `pickNode(QPointF)` signature |

**Cost, stated plainly.** A Quick3D surface costs a second graphics context, a
render thread and a QML engine per instance; it does not compose with the
backing-store painting order the way a painted View does. It is for a small number
of large, immersive scenes — not for many small widgets. The built-in scene is one
QML module compiled into the library, not a file the application must ship.

**Testing.** Component tests `QSKIP` when the packages are absent. When present,
they cover: construction while hidden creates no engine; showing creates it and
hiding releases it; theme tokens reach the scene (assert a known property value);
a broken source sets `lastError()` and keeps the widget alive; and the software
Quick backend (`QT_QUICK_BACKEND=software`) still paints in a headless run.

## Performance workstream (agreed 13 November 2026)

Two decisions were taken: **attack repainted area before raw rendering speed**, and
**go to Qt RHI directly** for the GPU backend rather than polishing the GL device
first.

### The model: retained layers with damage

Today every visible widget paints itself into the backing store whenever it is
asked to, so a window-level repaint redraws all 74 components. The change is:

1. A state change marks a **damage rectangle**, not an opaque "repaint" —
   `View::requestUpdate(QRect damage)`.
2. The compositor blits cached layers for clean views and rasterises only the
   damaged region of dirty ones.
3. Charts, lists and icon-dense rows are the first candidates for a cached layer,
   because they are the most expensive per unit of area.

New pieces, all inside `render/`:

| Unit | Responsibility |
| --- | --- |
| `cache/DamageTracker` | Accumulates and coalesces damage rectangles per view, and unions children into parents. |
| `cache/LayerCache` | One cached surface per opted-in view, keyed by size, device pixel ratio, theme revision and a view-specific content revision. |
| `cache/GlyphAtlas` | Caches shaped text runs as textures/bitmaps so a frame does not re-shape or re-rasterise them. |
| `cache/IconAtlas` | Rasterises `QSvgRenderer` output per (size, colour, dpr) — SVG is never rendered per frame. |
| `compositor/CpuCompositor` | Blits cached layers through `QPainter`; the reference path. |
| `compositor/GpuCompositor` | Blits layers as RHI textures; the current `GpuCompositor` grows into a `RhiDevice` consumer. |

### Order of work, each step ending with a measured number

1. **Damage first** — `DamageTracker` plus damage-aware `requestUpdate`. No renderer
   change; it only reduces how much gets repainted.
2. **Layer cache** — opt-in per view class, starting with the chart, list and
   icon-dense row subtrees. Verified by pixel equality against the uncached path.
3. **Atlases** — glyph and SVG icon atlases; text shaping stays with Qt, only the
   rasterised result is reused.
4. **RHI device** — `RhiDevice` over Qt RHI behind `PERIODICITY_HAS_SHADERTOOLS`,
   with `qsb`-compiled shaders built into the library. D3D11/12, Metal, Vulkan and
   GL then share one shader source. The GL device stays as the fallback when
   ShaderTools is absent, and the software device remains the reference.
5. **Tiled threaded rasterisation** — split the software rasteriser into tiles and
   run them on `schedule::TaskQueue`, so machines without a usable GPU still hold
   the frame budget.
6. **Quick3D for heavy scenes** — phase 7 above, now justified as the 3D ceiling
   rather than a convenience.

### Acceptance

Every step lands with a benchmark that asserts a budget rather than a description:
a window of 74 components, a 25-chart page, and a chart reveal animation, each
measured for mean and p95 frame time on the software device and (where present) on
the RHI device. A step that cannot show a lower number does not land.

### Risks

- **Invalidation correctness is the hard part.** Any state that affects appearance
  must mark damage; a missed invalidation is a ghost, not a crash, so the cache
  needs a debug mode that renders everything uncached and compares. That mode ships
  as a test flag.
- **Memory**: one surface per opted-in view at device resolution. The cache is
  opt-in and capped, with an LRU eviction policy shared with `pool/`.
- **Device-pixel-ratio, theme and font changes** must invalidate every layer;
  those three are the only global invalidations, and each has a test.
- **Two worlds**: RHI for compositing and Quick3D for scenes are separate contexts.
  Neither is allowed to leak into the painted-component path, and the software
  device must always be able to produce the same picture.

## Target architecture: window-level compositor (decided 13 November 2026)

The decision is to make **L2 the target**: the window is composited by this
library instead of by the backing-store per-widget loop. This section is the
contract for that refactor.

### What changes and what does not

| Stays | Changes |
| --- | --- |
| The `QWidget` tree, layout, focus, input, IME, accessibility, window handle | `paintEvent` no longer draws to the window; a view paints into its own **layer** |
| The public API of every component | The frame path: layers → composite → present |
| Hit testing (already geometric, never pixel based) | Repaint scope: damage rectangles instead of whole-widget invalidation |
| The software device as the reference implementation | Default backend becomes the compositor's, not `QBackingStore` |

### Host model

Two hosts, chosen at runtime, same semantics:

1. **`QRhiWidget` root** (preferred). Qt 6.7+ gives a widget whose surface is a
   `QRhi` render target, so layers are composited on the GPU and presented without
   an extra full-window copy. Requires `Qt6::ShaderTools`/`qsb` only for our own
   shaders, not for the host.
2. **Offscreen texture + blit** (fallback). `RhiDevice` renders the composited
   frame into a texture, the host blits that single texture into the widget's
   backing store. One full-window copy per frame; the cost is stated, not hidden.
3. **CPU compositor** (always available). Layers are `QImage`s, composited with
   `QPainter`, including clipping, opacity and blend modes. This is the reference
   that the GPU paths must match.

### Layer model

- Every opted-in `View` owns a layer: a `RenderTarget` at device resolution, with a
  content revision and a damage rectangle set.
- A view paints by recording a `gfx::CommandBuffer` into its layer, never into the
  window. `paintContent(QPainter&)` stays as a compatibility shim that records the
  equivalent commands.
- The compositor walks the visible tree in z-order and draws each layer with its
  accumulated clip and opacity. A layer that is unchanged is reused verbatim; a
  layer that only moved (scroll, drag) is reused with a different destination rect.
- Global state that changes appearance — theme revision, font, device pixel ratio,
  size — bumps a revision that invalidates every layer. Those four are the only
  global invalidations, and each has a test.

### Order of work

1. `DamageTracker` and `View::requestUpdate(QRect damage)`; damage unions up the
   tree. No visual change, all 109 tests must stay green.
2. `LayerCache` with a CPU compositor for a single view class (the chart) behind a
   flag, asserted equal to the current output pixel for pixel.
3. `Compositor` abstraction frozen: CPU implementation first, window host wired so
   a whole page can be composited from layers.
4. `RhiDevice` + `RhiCompositor` behind `PERIODICITY_HAS_SHADERTOOLS`, parity-tested
   against the CPU compositor.
5. Coverage expands class by class, each expansion justified by the benchmark.

### Acceptance

- The three benchmarks (74-component window, 25-chart page, chart reveal) measured
  on all three compositors where available; a step lands only with a lower number.
- Visual equivalence: every migrated view compared against the pre-compositor
  output; differences explained before any baseline is re-recorded.
- The debug mode that renders everything uncached must produce an identical frame
  to the cached path; it ships as a test flag, not a hidden env var.

### Risks, stated plainly

- **Invalidation correctness** is the dominant risk: a missed damage rectangle is a
  ghost, not a crash. The uncached-comparison mode is the mitigation and must exist
  before the first class is migrated.
- **Memory**: one layer per opted-in view at device resolution; capped and LRU
  evicted through `pool/`.
- **Host limits**: a `QRhiWidget` root is a native child surface, so anything that
  must paint above it (menus, tooltips, modals) stays in a normal widget layer on
  top — that layering rule needs its own design and test.
- **Quick3D stays outside this path**: a Quick3D scene keeps its own context and is
  composited as one more texture, never as a participant in the layer model.

## Layer, Modal, Overlay (decided 13 November 2026)

A floating surface used to solve the window-level problems itself: `Modal` found
its window, disabled it, saved the focus and kept a layer registry; `Dialog`
painted a private scrim and installed a parent event filter; `SwitchingOverlay`
had its own content guard. The split is now explicit, and a unit that reaches
past it is a bug in the split, not a feature of the unit.

| Unit | Owns | Must not own |
| --- | --- | --- |
| `layer/` | Painting inside one widget's surface: background, surface, state layer, focus ring, ripple, elevation, scrim paint helper | Windows, stacking, input arbitration, focus, disabling |
| `Modal` (`components/`) | The interaction contract: scrim, card, focus trap, `scrimDismissible`, Escape/dismiss policy, enter/exit motion | Finding the window, `setEnabled`, saving focus, owning a stack |
| `Overlay` (`view/`) | The window-level host: the stack of live surfaces, z-order, top-most input, content-root disable with exact restore, focus save/restore, teardown when the window dies | Design-system semantics, card painting, motion curves, translations |
| `Dialog` (`components/`) | Title, text, action row, the `paintBody`/`handleBodyClick`/`handleBodyKey`/`arrangeBody` body contract, `accepted`/`rejected` | Its own scrim painting, its own `eventFilter` input interception, judging whether it is top-most |

### Z-order contract

`Overlay::forWidget()` lazily creates one Overlay per top-level window, parented
to the window and never leaked; its transparent host widget is created on the
first push. The host sits above the content root and is raised once when the
overlay stack opens. Every `push()` then reparents the surface to the window
and raises it, so the stack order **is** the sibling order: the newest surface
is above every older surface and above the host. `top()` is the last stack
entry, not a scan of visible children, and nothing here depends on creation
order or the order the window happened to add children in. Nested pushes are
expected: a Dialog opened from a PickerSheet is pushed after the sheet's Modal
and is therefore above it. `Modal::Level` remains for the legacy layer
registry, but it is not the stack that `Modal` lives in.

The stack is the only definition of top-most: `Modal` traps Tab and consumes
Escape only while `overlay->top() == this`; a Dialog never asks, because a
surface that is not on top does not receive the input at all.

### Input and content

While the stack is non-empty, `Overlay` is an application event filter. Events
delivered to the top surface or its descendants are forwarded untouched, so the
surface's own key handling and focus trap still apply. Events delivered
anywhere else in the window are swallowed when the top surface asked for
`blockInput`: mouse press/release/double-click/move, wheel, context menu, touch,
tablet, drag-and-drop, key press/release, `ShortcutOverride` and `Shortcut`.
Hover, enter/leave and focus events are forwarded because they are state
notifications, not activation. Events for other windows are always forwarded.

The filter cannot latch: it is removed on the last pop, on the last surface
being destroyed, and in the destructor, and it returns immediately while the
stack is empty. `blockInput = false` is therefore also the escape hatch for a
non-modal surface.

`disableContent` is a request, not a new state model. The content roots — the
direct children of the window except the host and the pushed surfaces, and
excluding transient popups that are windows of their own — are disabled when the
first request appears and re-enabled with their exact saved states when the last
one disappears. Descendants are never touched, so a child that was already
disabled stays disabled. `setContentDisabled()` arms the same request after a
push; the busy switch surface uses it so the disabled repaint happens behind its
opaque scrim and not under its entrance fade.

Focus is captured from `QApplication::focusWidget()` on the outermost push and
restored on the last pop if the widget still exists, is visible and is enabled.
An explicitly named focus-return target (Modal's `setFocusReturn()`) wins over
the captured widget; the remaining fallbacks are the invoker
(`setFocusFallback()`), then the content root. Inner pops restore nothing, which
is what makes a nested stack restore focus exactly once. Restoration calls
`QWidget::setFocus()` and does not activate the window: on an inactive window Qt
records the focus child for activation instead of exposing it through
`QApplication::focusWidget()`, and raising a window is the host's decision.

Destroying a surface while it is pushed is the one path whose window-level
restore is queued: the entry leaves the stack immediately, but the content
state, focus, host and filter are settled on the next event-loop turn, after
`~QWidget` has finished. `QWidget` emits `destroyed()` from inside its own
destructor, before its children are gone, so restoring there would drive focus
and enabled state through a tree that is still being taken apart.

### Native-window boundary

`Overlay` guarantees the top-most *in-window* surface only. Native menus,
native tooltips, platform file dialogs and any other top-level `QWindow` are
separate windows; the platform compositor places them above this window by
definition, and `Overlay` neither sees nor orders them. A surface that itself
creates a native window is outside this contract.

Transitional: an in-window surface that still uses the legacy `Modal::Level`
registry and has not been migrated (`Dropdown`, `Menu`, `Tooltip`, a modal
`BottomSheet`) is not on the Overlay stack. Opening one above a Modal or Dialog
therefore puts it outside the contract, and Overlay will treat its input as
input below the top surface. Migrate such a surface to `push()`/`pop()` before
relying on it above an Overlay surface.

### The rule for new surfaces

A new overlay surface must not touch the window. It pushes itself with the
behaviour it needs, asks `Overlay` for the top-most check, and pops when its
exit is invisible. If it needs something from the window that `Overlay` does not
offer — another lifecycle signal, another input class, another saved state — the
`Overlay` is missing an operation: add it there, next to the state it belongs
to, instead of reaching for `window()` inside the component.
