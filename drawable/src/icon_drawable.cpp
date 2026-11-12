// Periodicity — IconDrawable (framework.md §7.6).
//
// The tint is applied with CompositionMode_SourceIn into a pixmap that is
// cached under the source identity, the colour, the pixel size, the device
// pixel ratio, the resolved direction and the revision. The source is scaled
// with a smooth transform because the icon set is made of vector-derived
// masks; the tinted result is then drawn without further scaling (the pixmap
// carries the device pixel ratio, so QPainter lays it out at the requested
// logical size).

#include <md/drawable/icon_drawable.h>

#include "drawable_hash_p.h"

#include <QImage>
#include <QPainter>
#include <QPixmapCache>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QString>

#include <utility>

namespace md::drawable {

namespace {

// A corrupt size must not turn into a gigapixel raster. The limit is well
// above any real icon (even at 800 % scaling) and only rejects geometry that
// could not be painted anyway.
constexpr qreal kMaxIconDeviceSide = 8192.0;

} // namespace

IconDrawable::IconDrawable(QIcon icon)
    : icon_(std::move(icon))
{
}

void IconDrawable::setIcon(QIcon icon)
{
    // The source identity is the icon's cache key; assigning the same icon
    // again must not invalidate a cached raster.
    if (pixmap_.isNull() && icon_.cacheKey() == icon.cacheKey())
        return;

    icon_ = std::move(icon);
    pixmap_ = QPixmap();
    invalidate();
}

void IconDrawable::setPixmap(QPixmap pixmap)
{
    if (icon_.isNull() && pixmap_.cacheKey() == pixmap.cacheKey())
        return;

    pixmap_ = std::move(pixmap);
    icon_ = QIcon();
    invalidate();
}

void IconDrawable::setSize(QSizeF size)
{
    // A non-finite size would survive every later isEmpty() test, divide by
    // NaN in alignedTarget() and reach qCeil() as an undefined float-to-int
    // conversion, so it is rejected outright. Negative extents are merely "no
    // preference" and clamp to zero instead of inverting the fitted box.
    if (!qIsFinite(size.width()) || !qIsFinite(size.height()))
        return;

    const QSizeF clamped(qMax(0.0, size.width()), qMax(0.0, size.height()));
    if (size_ == clamped)
        return;
    size_ = clamped;
    invalidate();
}

void IconDrawable::setColour(QColor colour)
{
    if (colour_ == colour)
        return;
    colour_ = std::move(colour);
    invalidate();
}

void IconDrawable::setMirrored(bool mirrored)
{
    if (mirrored_ == mirrored)
        return;
    mirrored_ = mirrored;
    invalidate();
}

void IconDrawable::setAlignment(Qt::Alignment alignment)
{
    if (alignment_ == alignment)
        return;
    alignment_ = alignment;
    invalidate();
}

QRectF IconDrawable::alignedTarget(const QRectF& bounds) const
{
    if (bounds.isEmpty() || size_.isEmpty())
        return {};

    // The requested size is an intrinsic box, not a stretch target. The icon
    // shrinks when the available rectangle is smaller and is aligned inside a
    // larger one; it never grows beyond the box, so a 24 dp icon asked to
    // paint into a huge rectangle does not become a blurry poster.
    const qreal scale = qMin(1.0, qMin(bounds.width() / size_.width(),
                                      bounds.height() / size_.height()));
    const QSizeF fitted(size_.width() * scale, size_.height() * scale);
    if (fitted.isEmpty())
        return {};

    qreal x = bounds.left();
    if (alignment_ & Qt::AlignHCenter)
        x = bounds.center().x() - fitted.width() / 2.0;
    else if (alignment_ & Qt::AlignRight)
        x = bounds.right() - fitted.width();

    qreal y = bounds.top();
    if (alignment_ & Qt::AlignVCenter)
        y = bounds.center().y() - fitted.height() / 2.0;
    else if (alignment_ & Qt::AlignBottom)
        y = bounds.bottom() - fitted.height();

    return QRectF(QPointF(x, y), fitted);
}

QPixmap IconDrawable::renderTinted(const QSize& pixelSize, qreal dpr) const
{
    if (pixelSize.isEmpty())
        return {};

    QImage source;
    if (!pixmap_.isNull()) {
        source = pixmap_.toImage();
    } else if (!icon_.isNull()) {
        // Ask the icon for exactly the device resolution we need, so an icon
        // theme can pick its sharpest representation instead of upscaling.
        source = icon_.pixmap(pixelSize, dpr).toImage();
    }

    if (source.isNull())
        return {};

    // The source's own ratio is irrelevant: it is scaled to explicit device
    // pixels below.
    source.setDevicePixelRatio(1.0);

    QImage canvas(pixelSize, QImage::Format_ARGB32_Premultiplied);
    if (canvas.isNull())
        return {};
    canvas.fill(Qt::transparent);

    {
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const QImage scaled = source.scaled(pixelSize, Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation);
        if (!scaled.isNull()) {
            // Letterbox rather than stretch when the source aspect differs
            // from the requested box.
            const QPoint origin((pixelSize.width() - scaled.width()) / 2,
                                (pixelSize.height() - scaled.height()) / 2);
            painter.drawImage(origin, scaled);
        }

        if (colour_.isValid()) {
            // SourceIn keeps the destination alpha and replaces every covered
            // pixel with the tint, which turns any mask (including a
            // multi-colour pixmap with alpha) into a single-colour icon.
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(QRect(QPoint(0, 0), pixelSize), colour_);
        }
    }

    QPixmap result = QPixmap::fromImage(canvas);
    if (result.isNull())
        return {};
    result.setDevicePixelRatio(dpr);
    return result;
}

void IconDrawable::draw(QPainter& painter, const QRectF& bounds) const
{
    if (bounds.isEmpty() || size_.isEmpty() || !painter.isActive())
        return;

    const QRectF target = alignedTarget(bounds);
    if (target.isEmpty() || !qIsFinite(target.x()) || !qIsFinite(target.y())
        || !qIsFinite(target.width()) || !qIsFinite(target.height())) {
        return;
    }

    const qreal dpr = Drawable::effectiveDevicePixelRatio(painter);
    const bool mirror = mirrored_ && painter.layoutDirection() == Qt::RightToLeft;

    // setSize() rejects non-finite extents, but `bounds` still passes through
    // a division, so the product is validated before it becomes a QSize: a
    // value above INT_MAX would make qCeil() undefined, and an enormous value
    // would ask Qt for an unbounded raster.
    const qreal deviceWidth = target.width() * dpr;
    const qreal deviceHeight = target.height() * dpr;
    if (!qIsFinite(deviceWidth) || !qIsFinite(deviceHeight)
        || deviceWidth <= 0.0 || deviceHeight <= 0.0
        || deviceWidth > kMaxIconDeviceSide || deviceHeight > kMaxIconDeviceSide) {
        return;
    }

    const QSize pixelSize(qMax(1, qCeil(deviceWidth)), qMax(1, qCeil(deviceHeight)));

    QPixmap pixmap;
    try {
        // The key itself allocates, so it is built inside the same guard as
        // the raster it describes.
        const QString key = QStringLiteral("md.icon.%1.%2x%3.%4.%5.%6")
                                .arg(cacheKey(), 16)
                                .arg(pixelSize.width())
                                .arg(pixelSize.height())
                                .arg(qRound(dpr * 100.0))
                                .arg(mirror ? 1 : 0)
                                .arg(revision());

        if (!QPixmapCache::find(key, &pixmap)) {
            pixmap = renderTinted(pixelSize, dpr);
            if (pixmap.isNull())
                return;
            QPixmapCache::insert(key, pixmap);
        }
    } catch (...) {
        // Allocation or cache failure: nothing to paint, but the composite
        // around this icon is unaffected.
        return;
    }
    if (pixmap.isNull())
        return;

    painter.save();
    try {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        // The tinted pixmap has the exact logical size of `target`, so
        // mirroring around the target's centre keeps the icon in place.
        if (mirror) {
            painter.translate(target.center());
            painter.scale(-1.0, 1.0);
            painter.translate(-target.center());
        }

        painter.drawPixmap(target.topLeft(), pixmap);
    } catch (...) {
        // A failed primitive is skipped; the painter state is restored below.
    }
    painter.restore();
}

quint64 IconDrawable::cacheKey() const
{
    const quint64 source = pixmap_.isNull()
        ? static_cast<quint64>(icon_.cacheKey())
        : static_cast<quint64>(pixmap_.cacheKey());

    quint64 key = detail::mix(0xa4093822299f31d0ULL, source);
    key = detail::mix(key, detail::hashColour(colour_));
    key = detail::mix(key, detail::hashScalar(size_.width()));
    key = detail::mix(key, detail::hashScalar(size_.height()));
    key = detail::mix(key, mirrored_ ? 1u : 0u);
    return key;
}

} // namespace md::drawable
