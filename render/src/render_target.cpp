#include <md/render/render_target.h>

#include <md/gfx/rasterizer.h>

#include <QDebug>
#include <QtMath>

#include <cmath>

namespace md::render {

namespace {

bool isFiniteReal(qreal value)
{
    return std::isfinite(static_cast<double>(value));
}

int targetPixelCap()
{
    return gfx::RasterCaps{}.maxTargetPixels;
}

} // namespace

RenderTarget::RenderTarget(const QSizeF& logicalSize, const RenderTargetFormat& format)
{
    allocate(logicalSize, format);
}

bool RenderTarget::resize(const QSizeF& logicalSize, const RenderTargetFormat& format)
{
    return allocate(logicalSize, format);
}

bool RenderTarget::allocate(const QSizeF& logicalSize, const RenderTargetFormat& format)
{
    image_ = QImage();
    format_ = format;
    logicalSize_ = logicalSize;

    if (!isFiniteReal(logicalSize.width()) || !isFiniteReal(logicalSize.height())
        || logicalSize.width() <= 0.0 || logicalSize.height() <= 0.0) {
        return false;
    }

    if (!isFiniteReal(format.devicePixelRatio) || format.devicePixelRatio <= 0.0)
        return false;

    if (format.format == QImage::Format_Invalid) {
        qWarning() << "render::RenderTarget: refused an invalid image format";
        return false;
    }

    const int cap = targetPixelCap();
    const qreal pixelWidthF = logicalSize.width() * format.devicePixelRatio;
    const qreal pixelHeightF = logicalSize.height() * format.devicePixelRatio;

    if (!isFiniteReal(pixelWidthF) || !isFiniteReal(pixelHeightF)
        || pixelWidthF > static_cast<qreal>(cap)
        || pixelHeightF > static_cast<qreal>(cap)
        || pixelWidthF * pixelHeightF > static_cast<qreal>(cap)) {
        qWarning() << "render::RenderTarget: refused a target of" << pixelWidthF << "x"
                   << pixelHeightF << "pixels (cap" << cap << ")";
        return false;
    }

    const int pixelWidth = qCeil(pixelWidthF);
    const int pixelHeight = qCeil(pixelHeightF);
    if (pixelWidth <= 0 || pixelHeight <= 0
        || static_cast<qint64>(pixelWidth) * static_cast<qint64>(pixelHeight) > cap) {
        qWarning() << "render::RenderTarget: refused a target of" << pixelWidth << "x"
                   << pixelHeight << "pixels (cap" << cap << ")";
        return false;
    }

    image_ = QImage(pixelWidth, pixelHeight, format.format);
    if (image_.isNull()) {
        qWarning() << "render::RenderTarget: could not allocate" << pixelWidth << "x"
                   << pixelHeight << "image";
        return false;
    }

    return true;
}

void RenderTarget::clear(const QColor& colour)
{
    if (!isValid())
        return;

    image_.fill(colour);
}

QImage RenderTarget::readBack() const
{
    return image_.copy();
}

} // namespace md::render
