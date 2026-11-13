#include <md/render/software_device.h>

#include <QFont>
#include <QPen>
#include <QTransform>

namespace md::render {

namespace {

// The gfx rasterizer works in physical pixels, while a device receives commands
// in the target's logical coordinates. These helpers build the device-space
// copy of a command buffer; at ratio 1 nothing is copied beyond the buffer
// itself.
QRect scaledRect(const QRect& rectangle, qreal ratio)
{
    if (rectangle.isEmpty())
        return QRect();

    return QRect(qRound(rectangle.x() * ratio), qRound(rectangle.y() * ratio),
                 qRound(rectangle.width() * ratio),
                 qRound(rectangle.height() * ratio));
}

QFont scaledFont(const QFont& font, qreal ratio)
{
    QFont scaled = font;
    if (font.pixelSize() > 0) {
        scaled.setPixelSize(qMax(1, qRound(font.pixelSize() * ratio)));
    } else if (font.pointSizeF() > 0.0) {
        scaled.setPointSizeF(font.pointSizeF() * ratio);
    }
    return scaled;
}

gfx::CommandBuffer scaledCommands(const gfx::CommandBuffer& commands, qreal ratio)
{
    if (qFuzzyCompare(ratio, 1.0))
        return commands;

    const QTransform transform = QTransform::fromScale(ratio, ratio);

    gfx::CommandBuffer scaled;
    for (const gfx::DrawCommand& original : commands.commands()) {
        gfx::DrawCommand command = original;
        command.viewport = scaledRect(original.viewport, ratio);
        command.scissor = scaledRect(original.scissor, ratio);

        switch (original.kind) {
        case gfx::CommandKind::DrawMesh2D:
            // Positions are in target pixels for the rasterizer; scale the
            // geometry, not the texture coordinates.
            command.mesh2d = original.mesh2d.transformed(transform);
            break;
        case gfx::CommandKind::DrawMesh3D:
            // Model space is untouched; the viewport carries the scale.
            break;
        case gfx::CommandKind::DrawText:
            command.textPosition = original.textPosition * ratio;
            command.font = scaledFont(original.font, ratio);
            break;
        case gfx::CommandKind::DrawPath: {
            command.path = transform.map(original.path);
            QPen pen = original.pen;
            if (pen.widthF() > 0.0)
                pen.setWidthF(pen.widthF() * ratio);
            command.pen = pen;
            break;
        }
        case gfx::CommandKind::DrawImage:
            // A set viewport (scaled above) is the dpr-correct destination; an
            // empty one keeps gfx's native-pixel placement.
            break;
        case gfx::CommandKind::SetScissor:
        case gfx::CommandKind::Clear:
            break;
        }

        scaled.add(std::move(command));
    }
    return scaled;
}

qint64 primitiveCount(const gfx::Mesh2D& mesh)
{
    if (!mesh.isValid())
        return 0;

    const qsizetype indices = mesh.indices().size();
    switch (mesh.topology()) {
    case gfx::PrimitiveTopology::Triangles:
        return indices / 3;
    case gfx::PrimitiveTopology::Lines:
        return indices / 2;
    case gfx::PrimitiveTopology::Points:
        return indices;
    }
    return 0;
}

qint64 primitiveCount(const gfx::Mesh3D& mesh)
{
    if (!mesh.isValid())
        return 0;

    const qsizetype indices = mesh.indices().size();
    switch (mesh.topology()) {
    case gfx::PrimitiveTopology::Triangles:
        return indices / 3;
    case gfx::PrimitiveTopology::Lines:
        return indices / 2;
    case gfx::PrimitiveTopology::Points:
        return indices;
    }
    return 0;
}

} // namespace

SoftwareDevice::SoftwareDevice()
    : caps_(gfx::RasterCaps{})
{
}

gfx::RasterStats SoftwareDevice::submit(RenderTarget& target,
                                        const gfx::CommandBuffer& commands)
{
    if (!target.isValid()) {
        lastError_ = QStringLiteral("refused to draw into an invalid render target");
        return {};
    }

    const QSize pixelSize = target.pixelSize();
    const qint64 pixels = static_cast<qint64>(pixelSize.width())
        * static_cast<qint64>(pixelSize.height());
    if (pixels > caps_.maxTargetPixels) {
        lastError_ = QStringLiteral("refused a target of %1x%2 pixels (cap %3)")
                         .arg(pixelSize.width())
                         .arg(pixelSize.height())
                         .arg(caps_.maxTargetPixels);
        return {};
    }

    qint64 primitives = 0;
    for (const gfx::DrawCommand& command : commands.commands()) {
        switch (command.kind) {
        case gfx::CommandKind::DrawMesh2D:
            primitives += primitiveCount(command.mesh2d);
            break;
        case gfx::CommandKind::DrawMesh3D:
            primitives += primitiveCount(command.mesh3d);
            break;
        case gfx::CommandKind::DrawImage:
            primitives += 2;
            break;
        case gfx::CommandKind::DrawText:
        case gfx::CommandKind::DrawPath:
        case gfx::CommandKind::SetScissor:
        case gfx::CommandKind::Clear:
            break;
        }

        if (primitives > caps_.maxTriangles) {
            lastError_ = QStringLiteral("refused %1 primitives (cap %2)")
                             .arg(primitives)
                             .arg(caps_.maxTriangles);
            return {};
        }
    }

    // gfx::rasterize() converts the surface to ARGB32 while it works, so the
    // device keeps the target's own format by converting the result back. Both
    // caps were checked above, which means a zero result from here on really is
    // "the buffer drew nothing", not a refusal.
    const QImage::Format format = target.image().format();
    QImage surface = target.imageRef();
    const gfx::RasterStats stats =
        gfx::rasterize(surface, scaledCommands(commands, target.devicePixelRatio()),
                       caps_);

    if (surface.isNull()) {
        lastError_ = QStringLiteral("rasterization produced no surface");
        return stats;
    }

    if (surface.format() != format) {
        const QImage converted = surface.convertToFormat(format);
        if (!converted.isNull())
            surface = converted;
    }

    target.imageRef() = surface;
    return stats;
}

DeviceCapabilities SoftwareDevice::capabilities() const
{
    DeviceCapabilities capabilities;
    capabilities.text = true;
    capabilities.paths = true;
    capabilities.images = true;
    capabilities.depthBuffer = true;
    capabilities.shaders = true;
    capabilities.maxTriangles = caps_.maxTriangles;
    capabilities.maxTargetPixels = caps_.maxTargetPixels;
    capabilities.backendName = name();
    return capabilities;
}

QString SoftwareDevice::name() const
{
    return QStringLiteral("software");
}

bool SoftwareDevice::isAvailable() const
{
    return true;
}

QString SoftwareDevice::lastError() const
{
    return lastError_;
}

void SoftwareDevice::setFallbackReason(const QString& reason)
{
    lastError_ = reason;
}

} // namespace md::render
