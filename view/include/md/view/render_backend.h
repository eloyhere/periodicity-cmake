// Periodicity — the optional GPU rendering path (framework.md §5.7, §5.12).
//
// A view always has a correct software path: its paint pipeline runs with
// QPainter on the widget backing store. This header exposes the second,
// optional path:
//
//   * the backend identifiers and the capability query (isBackendAvailable());
//   * preferredBackend(), the backend a caller should ask for on this machine;
//   * the compile-time feature macro below, which is 1 when the build has a
//     usable OpenGL widget surface (Qt6::OpenGLWidgets).
//
// GPU acceleration is never silent. A view starts on Software; asking for
// OpenGL on a machine where it is unavailable leaves the view on Software and
// records the reason in View::lastError(). The concrete container that turns
// the opt-in into actual GPU work is md::GpuCompositor, which uploads each
// enabled child's painted frame as a texture and composites those textures on
// the GPU. A View that opted in but is not inside a compositor keeps painting
// on the software path (and says so in lastError()).
//
// The availability query is honest about headless platforms: the offscreen,
// minimal, vnc and linuxfb QPA plugins have no usable GL surface, so
// isBackendAvailable(OpenGL) returns false there even when the Qt build ships
// Qt6::OpenGLWidgets. Tests run offscreen and therefore never assert GPU
// output; they assert the fallback contract.

#pragma once

#include <QtGlobal>

// MD_HAS_OPENGL_WIDGETS: compile-time availability of the QOpenGLWidget
// surface. The module build defines PERIODICITY_HAS_OPENGL (PUBLIC, so every
// consumer of periodicity::view sees the same value). The __has_include
// fallback keeps a standalone translation unit that includes this header
// without the module definition consistent with a Qt installation that ships
// the QtOpenGLWidgets headers.
#if defined(PERIODICITY_HAS_OPENGL)
#    define MD_HAS_OPENGL_WIDGETS (PERIODICITY_HAS_OPENGL ? 1 : 0)
#elif defined(__has_include)
#    if __has_include(<QtOpenGLWidgets/QOpenGLWidget>)
#        define MD_HAS_OPENGL_WIDGETS 1
#    else
#        define MD_HAS_OPENGL_WIDGETS 0
#    endif
#else
#    define MD_HAS_OPENGL_WIDGETS 0
#endif

namespace md {

// The two UI painting paths a caller may request.
//
//   Software — QPainter on the widget backing store. Always available and the
//              default; every component works with no extra setup.
//   OpenGL   — a GPU surface owned by md::GpuCompositor (or md::GpuCanvas for a
//              single view). Requires Qt6::OpenGLWidgets and a platform with a
//              usable GL context.
enum class RenderBackend {
    Software,
    OpenGL,
};

// True when `backend` can actually be used on this build and this platform.
// Software is always available. OpenGL is checked at run time: the build must
// carry Qt6::OpenGLWidgets, a QGuiApplication must exist, the QPA platform must
// not be a headless one, and a QOpenGLContext must be creatable. The result is
// cached after the first successful probe.
[[nodiscard]] bool isBackendAvailable(RenderBackend backend);

// OpenGL when isBackendAvailable(OpenGL), otherwise Software. This is advice,
// not the default: a View still starts on Software and only switches when the
// caller asks (or when a GpuCompositor enables it).
[[nodiscard]] RenderBackend preferredBackend();

// A stable, human-readable name ("Software"/"OpenGL") for logs and tests.
[[nodiscard]] const char* backendName(RenderBackend backend) noexcept;

} // namespace md
