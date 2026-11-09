// Periodicity — ShadowDrawable (framework.md §3.4, §7.3).
//
// The pipeline is deliberately explicit:
//
//   1. paint the silhouette of the shape, opaque white, into a transparent
//      ARGB32_Premultiplied canvas sized to the padded bounds at the painter's
//      device pixel ratio;
//   2. collapse the canvas to its alpha channel (one byte per pixel);
//   3. blur that channel with three box passes in each direction — six linear
//      passes with a sliding-window sum and no per-pixel allocation;
//   4. tint the blurred mask with the shadow colour, premultiplied once per
//      pixel;
//   5. cache the finished pixmap in QPixmapCache and blit it.
//
// Step 4 runs after the blur so the blur itself works on a single channel;
// blurring full ARGB would touch four times as much memory for the same result.
// QGraphicsDropShadowEffect is not used anywhere (framework.md §7.3).

#include <md/drawable/shadow_drawable.h>

#include "drawable_hash_p.h"

#include <md/tokens/elevation.h>

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPixmapCache>
#include <QRectF>
#include <QString>
#include <QtMath>

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace md::drawable {

namespace {

// The blur model. A Gaussian with this sigma is what the three box passes
// approximate; tying sigma to the elevation ramp makes softness grow
// monotonically with height, as the M3 elevation table requires.
constexpr qreal kBaseSigmaDp = 1.0;
constexpr qreal kSigmaPerElevationDp = 0.5;

// The light comes from above, so the shadow drops slightly below the surface.
constexpr qreal kOffsetPerElevationDp = 0.25;

// Cache dimensions are rounded up to this many device pixels (header).
constexpr int kSizeBucketPx = 8;

// A little slack beyond the nominal 3 * sigma so integer radius rounding and
// the vertical offset can never clip the blur at the canvas edge.
constexpr qreal kMarginSlack = 2.0;

[[nodiscard]] qreal sigmaDp(Elevation elevation) noexcept
{
    const qreal dp = elevationDp(elevation);
    return dp <= 0.0 ? 0.0 : kBaseSigmaDp + kSigmaPerElevationDp * dp;
}

[[nodiscard]] qreal offsetDp(Elevation elevation) noexcept
{
    return kOffsetPerElevationDp * elevationDp(elevation);
}

[[nodiscard]] int bucketize(int value) noexcept
{
    return ((value + kSizeBucketPx - 1) / kSizeBucketPx) * kSizeBucketPx;
}

// One box-blur pass along a row. The window is a sliding sum with clamped edge
// indices: O(width) per row, no temporary buffer.
void boxBlurHorizontal(const quint8* source, quint8* target, int width, int height, int radius)
{
    const int window = 2 * radius + 1;
    for (int y = 0; y < height; ++y) {
        const quint8* row = source + static_cast<qsizetype>(y) * width;
        quint8* out = target + static_cast<qsizetype>(y) * width;

        int sum = 0;
        for (int k = -radius; k <= radius; ++k)
            sum += row[std::clamp(k, 0, width - 1)];

        for (int x = 0; x < width; ++x) {
            out[x] = static_cast<quint8>(sum / window);
            const int addend = std::clamp(x + radius + 1, 0, width - 1);
            const int dropped = std::clamp(x - radius, 0, width - 1);
            sum += row[addend] - row[dropped];
        }
    }
}

// The vertical twin. The outer loop is over columns, so each inner pass walks
// one stride at a time; the images are small enough that this is comfortably
// cache-resident in practice.
void boxBlurVertical(const quint8* source, quint8* target, int width, int height, int radius)
{
    const int window = 2 * radius + 1;
    for (int x = 0; x < width; ++x) {
        int sum = 0;
        for (int k = -radius; k <= radius; ++k)
            sum += source[std::clamp(k, 0, height - 1) * width + x];

        for (int y = 0; y < height; ++y) {
            target[y * width + x] = static_cast<quint8>(sum / window);
            const int addend = std::clamp(y + radius + 1, 0, height - 1);
            const int dropped = std::clamp(y - radius, 0, height - 1);
            sum += source[addend * width + x] - source[dropped * width + x];
        }
    }
}

// Three box passes horizontally and vertically. Two buffers are reused across
// the passes; after the loop `buffer` holds the result.
void blurMask(std::vector<quint8>& buffer, int width, int height, int radius)
{
    if (radius <= 0)
        return;

    std::vector<quint8> scratch(buffer.size());
    for (int pass = 0; pass < 3; ++pass) {
        boxBlurHorizontal(buffer.data(), scratch.data(), width, height, radius);
        boxBlurVertical(scratch.data(), buffer.data(), width, height, radius);
    }
}

QImage renderMask(const Shape& shape, const QRectF& imageRect, const QRectF& shadowRect,
                  qreal dpr, const QSize& pixelSize, const QColor& colour, int blurRadius)
{
    const int width = pixelSize.width();
    const int height = pixelSize.height();

    // 1. The silhouette. ARGB32_Premultiplied is the raster engine's fastest
    //    format; only the alpha channel is read back, but painting into it is
    //    cheaper than into a single-channel image the engine may not optimise.
    QImage mask(pixelSize, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    {
        QPainter painter(&mask);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.scale(dpr, dpr);
        painter.translate(-imageRect.left(), -imageRect.top());
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawPath(shape.path(shadowRect));
    }

    // 2. Collapse to a single alpha channel.
    std::vector<quint8> buffer(static_cast<std::size_t>(width)
                               * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        const uchar* line = mask.constScanLine(y);
        quint8* row = buffer.data() + static_cast<std::size_t>(y) * width;
        for (int x = 0; x < width; ++x)
            row[x] = line[4 * x + 3];
    }

    // 3. Blur the coverage.
    blurMask(buffer, width, height, blurRadius);

    // 4. Tint. qPremultiply rounds once per pixel and yields a raster that
    //    QPainter blends without a second conversion.
    QImage result(pixelSize, QImage::Format_ARGB32_Premultiplied);
    const int red = colour.red();
    const int green = colour.green();
    const int blue = colour.blue();
    const int colourAlpha = colour.alpha();

    for (int y = 0; y < height; ++y) {
        auto* line = reinterpret_cast<QRgb*>(result.scanLine(y));
        const quint8* row = buffer.data() + static_cast<std::size_t>(y) * width;
        for (int x = 0; x < width; ++x) {
            const int alpha = row[x] * colourAlpha / 255;
            line[x] = qPremultiply(qRgba(red, green, blue, alpha));
        }
    }

    return result;
}

} // namespace

void ShadowDrawable::setElevation(Elevation elevation)
{
    if (elevation_ == elevation)
        return;
    elevation_ = elevation;
    invalidate();
}

void ShadowDrawable::setShape(Shape shape)
{
    if (shape_ == shape)
        return;
    shape_ = shape;
    invalidate();
}

void ShadowDrawable::setColour(QColor colour)
{
    if (colour_ == colour)
        return;
    colour_ = std::move(colour);
    invalidate();
}

void ShadowDrawable::setBlurScale(qreal scale)
{
    if (qFuzzyCompare(blurScale_, scale))
        return;
    blurScale_ = scale;
    invalidate();
}

QMarginsF ShadowDrawable::shadowMargins() const
{
    // Level 0 has no shadow at all; a non-positive blur scale collapses the
    // canvas onto the shape, so nothing extends beyond it either.
    if (elevation_ == Elevation::Level0 || blurScale_ <= 0.0)
        return {};

    // The box blur reaches 3 * radius on each side and the shape is offset
    // downwards; the slack absorbs integer rounding of the radius.
    const qreal spread = (3.0 * sigmaDp(elevation_) + offsetDp(elevation_)) * blurScale_
                         + kMarginSlack;
    return QMarginsF(spread, spread, spread, spread);
}

void ShadowDrawable::draw(QPainter& painter, const QRectF& bounds) const
{
    if (elevation_ == Elevation::Level0 || bounds.isEmpty() || !painter.isActive())
        return;
    if (!colour_.isValid() || colour_.alpha() == 0)
        return;

    const qreal dpr = painter.device() != nullptr ? painter.device()->devicePixelRatioF() : 1.0;
    const QMarginsF margins = shadowMargins();
    const QRectF imageRect = bounds.adjusted(-margins.left(), -margins.top(),
                                             margins.right(), margins.bottom());
    const QSize exact(qCeil(imageRect.width() * dpr), qCeil(imageRect.height() * dpr));
    if (exact.isEmpty())
        return;

    // The bucket bounds the cache; the raster is stretched back into
    // imageRect, which is imperceptible for a blur mask.
    const QSize pixelSize(bucketize(exact.width()), bucketize(exact.height()));

    const QString key = QStringLiteral("md.shadow.%1.%2.%3x%4.%5.%6")
                            .arg(cacheKey(), 16)
                            .arg(static_cast<int>(elevation_))
                            .arg(pixelSize.width())
                            .arg(pixelSize.height())
                            .arg(qRound(dpr * 100.0))
                            .arg(revision());

    QPixmap pixmap;
    if (!QPixmapCache::find(key, &pixmap)) {
        // A blur of radius r is about a Gaussian of sigma r; keeping the
        // radius in device pixels makes the mask identical at 100 % and
        // 200 % scaling rather than softer at 200 %.
        const int blurRadius = blurScale_ > 0.0
            ? qMax(1, qRound(sigmaDp(elevation_) * blurScale_ * dpr))
            : 0;
        const QRectF shadowRect = bounds.translated(0.0, offsetDp(elevation_) * blurScale_);

        QImage image = renderMask(shape_, imageRect, shadowRect, dpr, pixelSize,
                                  colour_, blurRadius);
        pixmap = QPixmap::fromImage(std::move(image));
        // framework.md §7.2: a cached pixmap is tagged with the ratio it was
        // rendered at, or Qt resamples it and it looks soft at 150 %.
        pixmap.setDevicePixelRatio(dpr);
        QPixmapCache::insert(key, pixmap);
    }

    // A bucket can make the raster a few device pixels larger than imageRect;
    // the extra ring is transparent (or faintly blurred), so aligning the top
    // left is exact and adds no visible overdraw.
    painter.drawPixmap(imageRect.topLeft(), pixmap);
}

quint64 ShadowDrawable::cacheKey() const
{
    quint64 key = detail::hashShape(shape_);
    key = detail::mix(key, static_cast<quint64>(elevation_));
    key = detail::mix(key, detail::hashColour(colour_));
    key = detail::mix(key, detail::hashScalar(blurScale_));
    return key;
}

} // namespace md::drawable
