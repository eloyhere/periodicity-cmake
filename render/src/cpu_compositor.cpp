#include <md/render/compositor.h>

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace md::render {

namespace {

bool isFiniteRect(const QRectF& rect)
{
    return std::isfinite(static_cast<double>(rect.x()))
        && std::isfinite(static_cast<double>(rect.y()))
        && std::isfinite(static_cast<double>(rect.width()))
        && std::isfinite(static_cast<double>(rect.height()));
}

} // namespace

void CpuCompositor::compose(RenderTarget& target, const QList<LayerRef>& layers)
{
    lastError_.clear();

    if (!target.isValid()) {
        lastError_ = QStringLiteral("render::CpuCompositor: compose() got an invalid target");
        return;
    }

    QPainter painter(&target.imageRef());
    if (!painter.isActive()) {
        lastError_ = QStringLiteral(
            "render::CpuCompositor: could not begin a painter on the target image");
        return;
    }

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    // The target image is in device pixels; the caller's list is in logical
    // ones. This is the same conversion RenderDevice::submit() performs.
    painter.scale(target.devicePixelRatio(), target.devicePixelRatio());
    composeInto(painter, layers);
}

const char* CpuCompositor::name() const
{
    return "cpu";
}

void CpuCompositor::composeInto(QPainter& painter, const QList<LayerRef>& layers)
{
    if (!painter.isActive()) {
        lastError_ = QStringLiteral(
            "render::CpuCompositor: composeInto() got an inactive painter");
        return;
    }

    for (const LayerRef& ref : layers) {
        if (ref.layer == nullptr || !ref.layer->isValid())
            continue;

        if (ref.destination.isEmpty() || !isFiniteRect(ref.destination))
            continue;

        // A non-finite opacity is a caller bug. Skipping the layer is the
        // conservative answer: drawing it opaque could hide the mistake, and
        // QPainter's behaviour for a NaN opacity is not specified.
        if (!std::isfinite(static_cast<double>(ref.opacity)))
            continue;

        const qreal opacity = std::clamp(ref.opacity, 0.0, 1.0);
        if (opacity <= 0.0)
            continue;

        painter.save();
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.setOpacity(opacity);

        // An empty (or invalid) clip means "no additional clip", matching the
        // LayerRef contract.
        if (!ref.clip.isEmpty() && isFiniteRect(ref.clip))
            painter.setClipRect(ref.clip, Qt::IntersectClip);

        const QImage& image = ref.layer->target().image();
        painter.drawImage(ref.destination, image, QRectF(image.rect()));
        painter.restore();
    }
}

} // namespace md::render
