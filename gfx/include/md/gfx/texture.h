// Periodicity — gfx layer: texture pixels plus sampling rules.
//
// A Texture is an image and the rules for reading it: wrap mode, filter and an
// optional mip chain. It knows nothing about a device or a context.
//
// sample() is a pure function of its arguments and is safe for every input:
//
//   - an invalid texture returns transparent black;
//   - a NaN or infinite UV or LOD returns transparent black;
//   - negative and huge UVs are reduced by the wrap mode, and Clamp saturates,
//     so no arithmetic overflows and no pixel index escapes the image.
//
// Coordinates follow the image convention: (0, 0) is the top-left corner and
// (1, 1) the bottom-right, so texel centres sit at (i + 0.5) / width. Channels
// interpolate linearly in non-premultiplied sRGB. Every sampler resolves texels
// through the same wrap helper, which is what keeps Clamp/Repeat/Mirror
// consistent between Nearest and Bilinear.
//
// Mip levels are built with a box filter: each destination pixel averages the
// source rectangle that maps onto it (one or two texels per axis, edge-clamped
// for odd sizes). The chain stops when both dimensions reach 1, and building is
// idempotent. `MipmapBilinear` selects the level nearest the requested LOD and
// samples it bilinearly; without a built chain it falls back to level 0, so a
// caller never has to remember to build first.

#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QSize>
#include <QVector>

namespace md::gfx {

enum class Wrap
{
    Clamp,
    Repeat,
    Mirror,
};

enum class Filter
{
    Nearest,
    Bilinear,
    MipmapBilinear,
};

class Texture
{
public:
    Texture() = default;

    // The image is converted to ARGB32 so sampling is identical everywhere.
    // A null image produces an invalid texture.
    explicit Texture(const QImage& image,
                     Wrap wrap = Wrap::Clamp,
                     Filter filter = Filter::Bilinear);

    bool isValid() const;
    QSize size() const { return image_.size(); }
    const QImage& image() const { return image_; }

    Wrap wrap() const { return wrap_; }
    Filter filter() const { return filter_; }
    void setWrap(Wrap wrap) { wrap_ = wrap; }
    void setFilter(Filter filter) { filter_ = filter; }

    // Pure sampling at `uv` and mip `lod`. Returns transparent black for an
    // invalid texture or non-finite input; never reads outside the image.
    QColor sample(const QPointF& uv, qreal lod = 0.0) const;

    // Builds every level down to 1x1. A no-op when already built or invalid.
    void buildMipmaps();

    // True once buildMipmaps() has produced at least one smaller level.
    bool hasMipmaps() const { return mips_.size() > 1; }

    // Number of levels in the chain including level 0; 1 before a build.
    int mipLevelCount() const { return mips_.isEmpty() ? 1 : mips_.size(); }

    // Level 0 is the base image. A null image is returned for a level that has
    // not been built.
    QImage mipLevel(int level) const;

private:
    QImage image_;
    QVector<QImage> mips_;
    Wrap wrap_ = Wrap::Clamp;
    Filter filter_ = Filter::Bilinear;
};

} // namespace md::gfx
