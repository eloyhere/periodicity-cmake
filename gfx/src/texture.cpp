// Periodicity — gfx layer: texture sampling and the mip chain.
//
// Every path through sample() goes through one wrap helper and one texel
// fetch, so Nearest, Bilinear and MipmapBilinear agree about where a UV lands
// and Clamp/Repeat/Mirror cannot disagree about edge behaviour.
//
// Determinism notes: the image is stored as ARGB32, channels interpolate in
// non-premultiplied sRGB, and the box filter averages integer channels with
// rounding rather than floating point, so a mip level is bit-identical on every
// platform. The chain is built once and never mutated afterwards.

#include <md/gfx/texture.h>

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace md::gfx {

namespace {

bool isFinite(qreal value)
{
    return std::isfinite(static_cast<double>(value));
}

// Reduce a finite UV into [0, 1] under the given wrap mode.
qreal wrapCoordinate(qreal value, Wrap wrap)
{
    switch (wrap) {
    case Wrap::Clamp:
        return qBound(0.0, value, 1.0);

    case Wrap::Repeat:
        // std::floor keeps the result in [0, 1) for negative inputs too, and is
        // exact enough for the huge-but-finite inputs the contract allows.
        return value - std::floor(value);

    case Wrap::Mirror: {
        qreal period = std::fmod(value, 2.0);
        if (period < 0.0)
            period += 2.0;
        return period <= 1.0 ? period : 2.0 - period;
    }
    }

    return 0.0;
}

// Map a continuous texel coordinate back into [0, size) under the wrap mode.
int wrapIndex(int index, int size, Wrap wrap)
{
    if (size <= 1)
        return 0;

    switch (wrap) {
    case Wrap::Clamp:
        return qBound(0, index, size - 1);

    case Wrap::Repeat: {
        int wrapped = index % size;
        if (wrapped < 0)
            wrapped += size;
        return wrapped;
    }

    case Wrap::Mirror: {
        const int period = size * 2;
        int wrapped = index % period;
        if (wrapped < 0)
            wrapped += period;
        return wrapped < size ? wrapped : period - 1 - wrapped;
    }
    }

    return 0;
}

QRgb texel(const QImage& image, int x, int y, Wrap wrap)
{
    return image.pixel(wrapIndex(x, image.width(), wrap),
                       wrapIndex(y, image.height(), wrap));
}

int blendChannel(int low, int high, qreal weight)
{
    const qreal value = static_cast<qreal>(low)
        + (static_cast<qreal>(high) - static_cast<qreal>(low)) * weight;
    return qBound(0, static_cast<int>(std::lround(value)), 255);
}

// One destination pixel of the box filter: average every source pixel whose
// centre maps onto it. Integer arithmetic keeps the result stable.
QRgb boxAverage(const QImage& source, int x0, int x1, int y0, int y1)
{
    qint64 red = 0;
    qint64 green = 0;
    qint64 blue = 0;
    qint64 alpha = 0;
    qint64 count = 0;

    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const QRgb pixel = source.pixel(x, y);
            red += qRed(pixel);
            green += qGreen(pixel);
            blue += qBlue(pixel);
            alpha += qAlpha(pixel);
            ++count;
        }
    }

    if (count == 0)
        return qRgba(0, 0, 0, 0);

    const qint64 half = count / 2;
    return qRgba(static_cast<int>((red + half) / count),
                 static_cast<int>((green + half) / count),
                 static_cast<int>((blue + half) / count),
                 static_cast<int>((alpha + half) / count));
}

int levelForLod(qreal lod, int levelCount)
{
    if (levelCount <= 1)
        return 0;

    const qreal requested = std::floor(lod + 0.5);
    if (!(requested > 0.0))
        return 0;
    if (requested >= static_cast<qreal>(levelCount - 1))
        return levelCount - 1;
    return static_cast<int>(requested);
}

} // namespace

Texture::Texture(const QImage& image, Wrap wrap, Filter filter)
    : wrap_(wrap)
    , filter_(filter)
{
    if (!image.isNull())
        image_ = image.convertToFormat(QImage::Format_ARGB32);
}

bool Texture::isValid() const
{
    return !image_.isNull() && image_.width() > 0 && image_.height() > 0;
}

QColor Texture::sample(const QPointF& uv, qreal lod) const
{
    if (!isValid())
        return QColor(0, 0, 0, 0);

    // The documented safe answer for a non-finite coordinate: nothing.
    if (!isFinite(uv.x()) || !isFinite(uv.y()) || !isFinite(lod))
        return QColor(0, 0, 0, 0);

    QImage image = image_;
    if (filter_ == Filter::MipmapBilinear && hasMipmaps())
        image = mipLevel(levelForLod(lod, mipLevelCount()));

    if (image.isNull() || image.width() <= 0 || image.height() <= 0)
        return QColor(0, 0, 0, 0);

    const qreal u = wrapCoordinate(uv.x(), wrap_);
    const qreal v = wrapCoordinate(uv.y(), wrap_);

    // Continuous texel-space coordinate; texel centres sit on integers.
    const qreal px = u * static_cast<qreal>(image.width()) - 0.5;
    const qreal py = v * static_cast<qreal>(image.height()) - 0.5;

    if (filter_ == Filter::Nearest) {
        const int x = static_cast<int>(std::floor(px + 0.5));
        const int y = static_cast<int>(std::floor(py + 0.5));
        return QColor::fromRgba(texel(image, x, y, wrap_));
    }

    const qreal left = std::floor(px);
    const qreal top = std::floor(py);
    const int x0 = static_cast<int>(left);
    const int y0 = static_cast<int>(top);
    const qreal fx = px - left;
    const qreal fy = py - top;

    const QRgb topLeft = texel(image, x0, y0, wrap_);
    const QRgb topRight = texel(image, x0 + 1, y0, wrap_);
    const QRgb bottomLeft = texel(image, x0, y0 + 1, wrap_);
    const QRgb bottomRight = texel(image, x0 + 1, y0 + 1, wrap_);

    const auto mix = [&](int a, int b, int c, int d) {
        const int upper = blendChannel(a, b, fx);
        const int lower = blendChannel(c, d, fx);
        return blendChannel(upper, lower, fy);
    };

    return QColor(mix(qRed(topLeft), qRed(topRight), qRed(bottomLeft), qRed(bottomRight)),
                  mix(qGreen(topLeft), qGreen(topRight), qGreen(bottomLeft), qGreen(bottomRight)),
                  mix(qBlue(topLeft), qBlue(topRight), qBlue(bottomLeft), qBlue(bottomRight)),
                  mix(qAlpha(topLeft), qAlpha(topRight), qAlpha(bottomLeft), qAlpha(bottomRight)));
}

void Texture::buildMipmaps()
{
    if (!isValid() || mips_.size() > 1)
        return;

    mips_.clear();
    mips_.append(image_);

    QImage current = image_;
    while (current.width() > 1 || current.height() > 1) {
        const int sourceWidth = current.width();
        const int sourceHeight = current.height();
        const int targetWidth = qMax(1, (sourceWidth + 1) / 2);
        const int targetHeight = qMax(1, (sourceHeight + 1) / 2);

        QImage next(targetWidth, targetHeight, QImage::Format_ARGB32);
        next.fill(Qt::transparent);

        for (int y = 0; y < targetHeight; ++y) {
            const int y0 = (y * sourceHeight) / targetHeight;
            const int y1 = qMin(sourceHeight,
                                qMax(y0 + 1, ((y + 1) * sourceHeight) / targetHeight));
            for (int x = 0; x < targetWidth; ++x) {
                const int x0 = (x * sourceWidth) / targetWidth;
                const int x1 = qMin(sourceWidth,
                                    qMax(x0 + 1, ((x + 1) * sourceWidth) / targetWidth));
                next.setPixel(x, y, boxAverage(current, x0, x1, y0, y1));
            }
        }

        mips_.append(next);
        current = next;
    }
}

QImage Texture::mipLevel(int level) const
{
    if (level < 0)
        return QImage();
    if (mips_.isEmpty())
        return level == 0 ? image_ : QImage();
    return level < mips_.size() ? mips_.at(level) : QImage();
}

} // namespace md::gfx
