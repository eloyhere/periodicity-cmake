# GPU rendering path

Periodicity always has a correct software paint path. This document describes
the second, optional path: what it actually accelerates, what it does not, how
a caller selects it, and what happens when the platform cannot provide a GL
surface.

## Summary

- **Default: Software.** Every `View`, including every component, paints with
  `QPainter` into the widget backing store, unchanged from before this API
  existed. No existing code has to opt in and no existing behaviour changes.
- **Opt-in: OpenGL.** `md::GpuCompositor` hosts child views, uploads each
  enabled child's painted frame as a texture, and composites those textures on
  the GPU. `md::GpuCanvas` is the single-view variant, which paints one view
  through `QPainter` on a `QOpenGLWidget` surface.
- **Honest acceleration.** The GPU accelerates *compositing and blitting of
  already-painted frames*. It does not reimplement any component's painting:
  the first paint of a changed widget still rasterizes on the CPU.
- **Fallback is automatic and reported.** If the build has no
  `Qt6::OpenGLWidgets`, or the platform has no usable GL context, the request
  stays on Software and `View::lastError()` explains why.

## API

```cpp
#include <md/view/render_backend.h>

const bool canUseGpu = md::isBackendAvailable(md::RenderBackend::OpenGL);
const md::RenderBackend preferred = md::preferredBackend(); // advice only
```

On the view itself — the switch is on the kernel, so every component supports
it without a per-component API:

```cpp
view->setGpuAccelerated(true);   // or setRenderBackend(RenderBackend::OpenGL)
if (!view->isGpuAccelerated())
    qInfo() << view->lastError(); // why the software path is still active

view->setGpuAccelerated(false);  // back to Software for the view and subtree
```

`setGpuAccelerated()` applies to the view and, recursively, to its current
`View` descendants, so a caller can flip one root and have the subtree follow.
The flag is applied at call time; a view added to the subtree later starts on
Software and receives the next call.

The flag is only meaningful **inside a GPU container**. A `View` enabled outside
a `GpuCompositor` keeps painting on the software path; the setter records that
in `lastError()` and leaves the flag off. In-place per-widget surface swapping is
not offered (see the limitations below).

### GpuCompositor (many children)

```cpp
md::GpuCompositor* surface = new md::GpuCompositor(parent);
md::ChartBase* chart = new md::ChartBase(surface);
chart->setGpuAccelerated(true);
surface->addView(chart);        // or setRootView(chart) to fill the surface
```

Each enabled direct `View` child is painted once with the normal `QPainter`
pipeline into an offscreen `QImage` (the same code path as software), uploaded
to a `QOpenGLTexture` only when that child reports a change, and composited with
`QOpenGLTextureBlitter`. Children that did not opt in are left as ordinary
widgets, painted by Qt through the backing store on top of the GL surface.

### GpuCanvas (one view)

```cpp
md::GpuCanvas* canvas = new md::GpuCanvas(chart, parent);
```

`GpuCanvas::paintGL()` opens a `QPainter` on the `QOpenGLWidget` and calls
`QWidget::render()` on the hosted view. Qt implements that painter target with
its OpenGL paint engine, so the component's paint code is unchanged while its
primitives are rasterized by the GPU. This is the documented QPainter-on-GL
path. See [QOpenGLWidget](https://doc.qt.io/archives/qt-6.6/qopenglwidget.html)
and the [2D painting on OpenGL example](https://doc.qt.io/qt-6/fr/qtopengl-2dpainting-example.html).

## What is actually accelerated

Accelerated:

- texture upload and GPU-side composition of each enabled child's frame;
- texture blits with `QOpenGLTextureBlitter`, Qt's supported helper for
  blitting textures in a `QOpenGLWidget`; it owns the shader, the source/target
  orientation and the context's texture format
  ([QOpenGLTextureBlitter](https://doc.qt.io/qt-6.12/qopengltextureblitter.html));
- the GL paint engine's rasterization for the single-view `GpuCanvas` path.

Not accelerated:

- a component's `paintContent()`, layout, text shaping or path construction —
  these stay in the CPU paint pipeline;
- a full-screen repaint: rendering a changed widget into its `QImage` and
  uploading it is CPU work, even though the final composite is on the GPU;
- widget backing-store composition for children that did not opt in;
- window management and the native window's own composition.

## Cost model

- Useful when many widgets move or overlap: the frames are cached as textures
  and only the changed ones are re-uploaded, then composited in one GPU pass.
- Not useful for one static label: the upload and GL round trip cost more than
  simply painting the label in software.
- Every embedded texture costs `width × height × 4` bytes at the device pixel
  ratio, plus driver overhead. A 4K-wide surface at 2× DPI is tens of MB.
- The upload rule is per change, not per frame: an unchanged child keeps its
  texture. A child that repaints every frame re-renders and re-uploads every
  frame, which is the worst case for this design.
- Cap the number of composited children on memory-constrained devices; the
  compositor deliberately does not impose a cache limit.

## Fallback rules

1. `View` starts on Software. `isGpuAccelerated()` is false until a successful
   request.
2. If `Qt6::OpenGLWidgets` is absent at build time, the compositor/canvas
   compile as `QWidget` stubs and report the backend unavailable.
3. If the QPA platform is headless (`offscreen`, `minimal`, `vnc`, `linuxfb`) or
   no `QOpenGLContext` can be created, `isBackendAvailable(OpenGL)` is false.
   `setGpuAccelerated(true)` / `setRenderBackend(OpenGL)` keeps the view on
   Software and records the reason in `lastError()`. The child widgets stay
   ordinary widgets, so the output is identical to the software path.
4. A view enabled outside a `GpuCompositor` stays on Software and records why.
5. If `initializeGL()` or the texture-blitter setup fails at run time, the
   compositor switches every child back to normal QWidget painting and emits
   `gpuStateChanged(false)`.
6. `lastError()` keeps the most recent diagnostic; a later paint exception may
   replace it, as with the other guarded entry points.

## QOpenGLWidget limitations

These are properties of the platform surface, not of this implementation, and
they are why the flag lives on a container rather than replacing each widget's
surface:

- a `QOpenGLWidget` is a native child window with its own context; stacking and
  clipping against sibling widgets and other native children are constrained;
- its context is destroyed with the widget, so resources must be released with
  it (the compositor destroys its textures in the widget destructor order);
- it is not a transparent overlay over arbitrary widget hierarchies;
- the GL paint engine is a rasterization target, not a way to skip the CPU
  paint pipeline.

See the "Limitations and Other Considerations" section of the
[QOpenGLWidget documentation](https://doc.qt.io/archives/qt-6.6/qopenglwidget.html).

`QRhiWidget` would be the preferred container on a Qt build that also ships the
RHI private headers and the shader tooling: it can render through Direct3D,
Vulkan, Metal or OpenGL instead of forcing OpenGL. It has no `QPainter` path,
though, and the installed Qt used by this project does not carry
`Qt6::GuiPrivate`/`qsb`, so a full RHI pipeline cannot be built and validated
here. The current implementation therefore keeps `QOpenGLWidget` and
`QOpenGLTextureBlitter`. The intended migration is a separate compositor
backend behind the same `GpuCompositor` API; see
[QRhiWidget](https://doc.qt.io/archives/qt-6.9/qrhiwidget.html).

## Threading

The rule from [threading.md](threading.md) is unchanged and strict: context
creation, `QImage` painting, texture upload and every GL call happen on the UI
thread. No worker or event-thread callback touches a widget, a texture or a GL
context. Repaint requests are coalesced through `md::schedule::UiThread` when it
is installed, so a burst of child updates becomes one canvas repaint per frame;
without a UI runtime the compositor falls back to ordinary `update()`.

## Why not accelerate every widget in place?

A per-widget surface swap would require every component to own a native GL
child, with one context each. That breaks stacking and clipping across the
widget tree, multiplies contexts and memory, and makes a single label pay the
full GL setup cost. The compositor keeps the software paint pipeline as the
source of truth and accelerates only the composition step, which is the part
that benefits from the GPU when many frames are moving at once.
