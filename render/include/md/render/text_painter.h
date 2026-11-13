// Periodicity — render layer: the single bridge to QPainter.
//
// Glyph shaping, font fallback, sub-pixel positioning and path filling stay
// with Qt. This class is the only place in render/ that touches QPainter, and
// both devices (software today, OpenGL in phase 3) use it, so text and paths
// look identical whichever device is active.
//
// The helpers are stateless and never throw. Every call that touches the
// painter saves its state first and restores it before returning, so a caller
// can interleave bridge calls with its own painter work without the font, pen,
// brush, clip or render hints leaking between them. A painter that is not
// active is a no-op rather than a crash.

#pragma once

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>
#include <QSizeF>
#include <QString>

namespace md::render {

class TextPainter
{
public:
    // Draws `text` inside `rectangle` with the given font, colour and alignment
    // flags (Qt::Alignment / Qt::TextFlag). Empty text is a no-op.
    static void drawText(QPainter& painter, const QRectF& rectangle,
                         const QString& text, const QFont& font,
                         const QColor& colour,
                         int flags = Qt::AlignLeft | Qt::AlignTop) noexcept;

    // Fills `path` with `brush` and strokes it with `pen`, both in the
    // painter's current coordinate system.
    static void drawPath(QPainter& painter, const QPainterPath& path,
                         const QPen& pen, const QBrush& brush) noexcept;

    // Draws `image` from `source` (empty means the whole image) into
    // `destination`. `smooth` selects SmoothPixmapTransform for the call.
    static void drawImage(QPainter& painter, const QImage& image,
                          const QRectF& destination,
                          const QRectF& source = QRectF(),
                          bool smooth = true) noexcept;

    // The advance width and line height of `text` in `font`, in the font's own
    // coordinate system. Zero width for empty text; never negative.
    static QSizeF measure(const QString& text, const QFont& font) noexcept;
};

} // namespace md::render
