// Periodicity — the Canvas component.
//
// Canvas is deliberately thin: View owns the background, shadow, focus and
// interaction layers; each Context owns its scene; Canvas only measures a
// sensible default, forwards its content rectangle to the 3D viewport and
// dispatches paintContent() to the active mode.

#include <md/components/canvas.h>

#include <md/core/density.h>
#include <md/theme/theme.h>

#include <QPainter>
#include <QShowEvent>
#include <QtGlobal>

#include <exception>

namespace md {
namespace {

constexpr qreal kDefaultWidthDp = 320.0;
constexpr qreal kDefaultHeightDp = 240.0;

template <class Fn>
void guardCanvas(const char* where, Fn&& fn)
{
    try {
        fn();
    } catch (const std::exception& error) {
        qWarning("md::Canvas: %s failed: %s", where, error.what());
    } catch (...) {
        qWarning("md::Canvas: %s failed: unknown exception", where);
    }
}

} // namespace

Canvas::Canvas(QWidget* parent)
    : View(parent)
    , context1d_(this)
    , context2d_(this)
    , context3d_(this)
{
    setFocusPolicy(Qt::NoFocus);
    context3d_.setHostModeActive(false);
}

void Canvas::setMode(Mode mode)
{
    guardCanvas("Canvas::setMode", [this, mode] {
        if (mode_ == mode)
            return;

        mode_ = mode;
        context3d_.setHostModeActive(mode_ == Mode::ThreeD);
        requestDraw();
    });
}

Canvas::Mode Canvas::mode() const noexcept
{
    return mode_;
}

Context1D* Canvas::context1d() noexcept
{
    return &context1d_;
}

const Context1D* Canvas::context1d() const noexcept
{
    return &context1d_;
}

Context2D* Canvas::context2d() noexcept
{
    return &context2d_;
}

const Context2D* Canvas::context2d() const noexcept
{
    return &context2d_;
}

Context3D* Canvas::context3d() noexcept
{
    return &context3d_;
}

const Context3D* Canvas::context3d() const noexcept
{
    return &context3d_;
}

void Canvas::setBackground(const QColor& colour)
{
    guardCanvas("Canvas::setBackground", [this, &colour] {
        if (background_ == colour)
            return;

        background_ = colour;
        setBackgroundColour(colour);
        context3d_.setBackground(colour);
        requestDraw();
    });
}

QColor Canvas::background() const
{
    return background_;
}

void Canvas::requestDraw()
{
    requestUpdate();
    context3d_.requestOpenGLUpdate();
}

void Canvas::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    setMeasuredSize(QSizeF(width.resolve(density.dp(kDefaultWidthDp)),
                           height.resolve(density.dp(kDefaultHeightDp))));
}

void Canvas::arrangeOverride(const QRectF& content)
{
    context3d_.setViewport(content);
}

void Canvas::paintContent(QPainter& painter, const QRectF& content)
{
    guardCanvas("Canvas::paintContent", [this, &painter, &content] {
        switch (mode_) {
        case Mode::OneD:
            context1d_.paint(painter, content);
            break;
        case Mode::TwoD:
            context2d_.paint(painter, content);
            break;
        case Mode::ThreeD:
            context3d_.paint(painter, content);
            break;
        }
    });
}

void Canvas::showEvent(QShowEvent* event)
{
    View::showEvent(event);

    guardCanvas("Canvas::showEvent", [this] {
        // Give the lazily-created GL child the final content rectangle and let
        // Context3D decide whether this platform can host a context.
        context3d_.setViewport(contentRect());
        context3d_.setHostModeActive(mode_ == Mode::ThreeD);
    });
}

} // namespace md
