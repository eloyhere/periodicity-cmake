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
| `SurfaceStyle` | `PrincipledMaterial` fields mapped from the theme tokens |
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

