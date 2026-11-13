// Periodicity — gfx layer: an ordered list of draw commands.
//
// CommandBuffer records intent; it never executes anything. A context fills one
// and hands it to a device, which (in phase 1) is the software rasterizer. The
// command kinds mirror the drawing surface the contexts will keep:
//
//   DrawMesh2D/DrawMesh3D  a mesh, a pipeline and its uniforms
//   DrawText               text, position, font and colour for the text bridge
//   DrawPath               a QPainterPath plus pen and brush for the path bridge
//   DrawImage              an image stretched into the viewport
//   SetScissor             narrows the scissor for following commands
//   Clear                  fills the viewport/scissor with `colour`
//
// A command's QRect members use empty to mean "unset": an empty viewport is the
// whole target, an empty scissor means no additional scissor. SetScissor state
// and a command's own scissor intersect. Viewports and scissors are clipped to
// the target by the rasterizer, never trusted.
//
// Text and paths keep their QPen/QBrush/QFont payload here because phase 1 draws
// them through the QPainter bridge; phase 2 moves that bridge to render/ without
// changing this structure.

#pragma once

#include <md/gfx/mesh.h>
#include <md/gfx/pipeline.h>

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QImage>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QVector>

#include <utility>

namespace md::gfx {

enum class CommandKind
{
    DrawMesh2D,
    DrawMesh3D,
    DrawText,
    DrawPath,
    DrawImage,
    SetScissor,
    Clear,
};

struct DrawCommand
{
    CommandKind kind = CommandKind::Clear;

    Pipeline pipeline;
    Mesh2D mesh2d;
    Mesh3D mesh3d;

    // Bridge payloads, used only by their matching kind.
    QImage image;
    QPainterPath path;
    QPen pen;
    QBrush brush;
    QString text;
    QPointF textPosition;
    QFont font;

    // Clear colour for Clear; pen colour for DrawText. White when unset.
    QColor colour = QColor(Qt::white);

    Uniforms uniforms;

    // Empty means the whole target (viewport) or no extra clip (scissor).
    QRect viewport;
    QRect scissor;
};

class CommandBuffer
{
public:
    void clear() { commands_.clear(); }
    void add(DrawCommand command) { commands_.append(std::move(command)); }

    int size() const { return static_cast<int>(commands_.size()); }
    bool isEmpty() const { return commands_.isEmpty(); }
    const QVector<DrawCommand>& commands() const { return commands_; }

private:
    QVector<DrawCommand> commands_;
};

} // namespace md::gfx
