// Periodicity — render layer: an offscreen surface.
//
// A RenderTarget is an image plus the two numbers that describe what it means:
// a size in logical (device-independent) coordinates and a device pixel ratio.
// The allocation is logicalSize * devicePixelRatio pixels, so a 320 x 200 target
// at ratio 2 owns a 640 x 400 image. Callers keep drawing in logical
// coordinates; RenderDevice::submit() applies the ratio when it rasterizes.
//
// Validation is the same rule the rest of the drawing stack follows: invalid
// input is refused as a whole rather than clamped. A non-finite or non-positive
// logical size, a non-finite or non-positive ratio and an invalid image format
// all produce an invalid target that allocates nothing. A target above
// gfx::RasterCaps::maxTargetPixels is refused with a qWarning, because the
// rasterizer could not execute it anyway.
//
// The QImage deliberately keeps a device pixel ratio of 1: its coordinates are
// always physical pixels. The ratio lives in RenderTargetFormat, where the
// device can read it and where it cannot silently turn every QPainter call into
// a scaled one.

#pragma once

#include <QColor>
#include <QImage>
#include <QSize>
#include <QSizeF>

namespace md::render {

struct RenderTargetFormat
{
    QImage::Format format = QImage::Format_ARGB32_Premultiplied;
    qreal devicePixelRatio = 1.0;
};

class RenderTarget
{
public:
    // An invalid, unallocated target.
    RenderTarget() = default;

    // Allocates `logicalSize * format.devicePixelRatio` pixels. An invalid size
    // or ratio leaves the target invalid and allocates nothing.
    explicit RenderTarget(const QSizeF& logicalSize,
                          const RenderTargetFormat& format = RenderTargetFormat{});

    // Reallocates for a new size and format. Returns isValid() afterwards, so a
    // refused resize leaves the target invalid rather than holding stale pixels.
    bool resize(const QSizeF& logicalSize,
                const RenderTargetFormat& format = RenderTargetFormat{});

    bool isValid() const { return !image_.isNull(); }

    const QImage& image() const { return image_; }

    // Mutable access for devices and the text/path bridge. The image stays in
    // the format the target was created with.
    QImage& imageRef() { return image_; }

    // The size the caller draws in.
    QSizeF logicalSize() const { return logicalSize_; }

    // The allocated size in physical pixels; empty for an invalid target.
    QSize pixelSize() const { return image_.size(); }

    // The requested format, including the ratio. An invalid target reports the
    // format it was asked for, so a failed resize can be diagnosed.
    RenderTargetFormat format() const { return format_; }

    // The actual layout of image(), which is `format().format` for a valid
    // target. Provided because image() is what most callers inspect.
    QImage::Format imageFormat() const { return image_.format(); }

    qreal devicePixelRatio() const { return format_.devicePixelRatio; }

    // Fills every pixel with `colour`. A no-op on an invalid target; the
    // colour's alpha is premultiplied by QImage for premultiplied formats.
    void clear(const QColor& colour = QColor(Qt::transparent));

    // A deep copy of the pixels, safe to keep or modify after the target is
    // resized or reused. Null for an invalid target.
    QImage readBack() const;

private:
    bool allocate(const QSizeF& logicalSize, const RenderTargetFormat& format);

    QImage image_;
    RenderTargetFormat format_;
    QSizeF logicalSize_;
};

} // namespace md::render
