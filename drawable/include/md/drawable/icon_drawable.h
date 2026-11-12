// Periodicity — a tinted, optionally mirrored icon (framework.md §7.6).
//
// Icons are masks: the source QIcon or QPixmap supplies the alpha channel and
// setColour() supplies the colour. Tinting happens at paint time with
// CompositionMode_SourceIn into a cached pixmap that is keyed by the source's
// cache key, the colour, the device pixel ratio, the direction and the target
// size, exactly as framework.md §7.6 requires.
//
// Mirroring is opt-in per icon because most icons in the icon set are
// direction-agnostic while arrows and chevrons are not. setMirrored(true) only
// takes effect when the painter's layout direction is right-to-left, so a
// caller can set the flag once and let the layout decide.
//
// draw() fits the icon into `bounds` preserving its aspect ratio and honours
// alignment: the requested size is the intrinsic box, and the icon is centred
// (or aligned to an edge) inside the available rectangle, never stretched and
// never enlarged beyond that box. An invalid colour means "draw the source
// unchanged".

#pragma once

#include <md/drawable/drawable.h>

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QSizeF>
#include <Qt>

class QPainter;
class QRectF;

namespace md::drawable {

class IconDrawable : public Drawable
{
public:
    // The default requested size is the design baseline's 24 dp icon box.
    IconDrawable() = default;

    explicit IconDrawable(QIcon icon);

    // The source. Setting one clears the other, so the last call wins: an icon
    // drawable always has exactly one source.
    void setIcon(QIcon icon);
    [[nodiscard]] QIcon icon() const { return icon_; }

    void setPixmap(QPixmap pixmap);
    [[nodiscard]] QPixmap pixmap() const { return pixmap_; }

    // The requested icon box, in logical pixels. draw() fits this box into the
    // target rectangle without stretching and without enlarging it beyond the
    // requested size. Negative extents clamp to zero (which means "no
    // preference" and paints nothing); a non-finite size is rejected and
    // leaves the previous value in place.
    void setSize(QSizeF size);
    [[nodiscard]] QSizeF size() const noexcept { return size_; }
    [[nodiscard]] QSizeF intrinsicSize() const override { return size_; }

    // The tint. An invalid colour disables tinting and paints the source as
    // it is.
    void setColour(QColor colour);
    [[nodiscard]] QColor colour() const noexcept { return colour_; }

    // Mirror horizontally when the painter's layout direction is
    // right-to-left. Off by default.
    void setMirrored(bool mirrored);
    [[nodiscard]] bool isMirrored() const noexcept { return mirrored_; }

    // Where the aspect-fitted icon box sits inside the target rectangle.
    // Qt::AlignCenter by default.
    void setAlignment(Qt::Alignment alignment);
    [[nodiscard]] Qt::Alignment alignment() const noexcept { return alignment_; }

    void draw(QPainter& painter, const QRectF& bounds) const override;

    // Hashes the source identity, the size, the tint and the mirroring flag.
    // draw() adds the resolved direction, the pixel size, the device pixel
    // ratio and the revision.
    [[nodiscard]] quint64 cacheKey() const override;

private:
    // The aspect-fitted, aligned box for `bounds`, in logical pixels.
    [[nodiscard]] QRectF alignedTarget(const QRectF& bounds) const;

    // Renders the tinted pixmap at `pixelSize` device pixels, tagged with
    // `dpr` so QPainter lays it out at the requested logical size.
    [[nodiscard]] QPixmap renderTinted(const QSize& pixelSize, qreal dpr) const;

    QIcon icon_;
    QPixmap pixmap_;
    QSizeF size_{24.0, 24.0};
    QColor colour_;
    bool mirrored_ = false;
    Qt::Alignment alignment_ = Qt::AlignCenter;
};

} // namespace md::drawable
