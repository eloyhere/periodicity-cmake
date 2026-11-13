#include <md/render/text_painter.h>

#include <QFontMetricsF>

namespace md::render {

void TextPainter::drawText(QPainter& painter, const QRectF& rectangle,
                           const QString& text, const QFont& font,
                           const QColor& colour, int flags) noexcept
{
    if (!painter.isActive() || text.isEmpty())
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setFont(font);
    painter.setPen(colour.isValid() ? colour : QColor(Qt::white));
    painter.drawText(rectangle, flags, text);
    painter.restore();
}

void TextPainter::drawPath(QPainter& painter, const QPainterPath& path,
                           const QPen& pen, const QBrush& brush) noexcept
{
    if (!painter.isActive() || path.isEmpty())
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(pen);
    painter.setBrush(brush);
    painter.drawPath(path);
    painter.restore();
}

void TextPainter::drawImage(QPainter& painter, const QImage& image,
                            const QRectF& destination, const QRectF& source,
                            bool smooth) noexcept
{
    if (!painter.isActive() || image.isNull() || destination.isEmpty())
        return;

    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
    painter.drawImage(destination, image,
                      source.isEmpty() ? QRectF(image.rect()) : source);
    painter.restore();
}

QSizeF TextPainter::measure(const QString& text, const QFont& font) noexcept
{
    if (text.isEmpty())
        return QSizeF();

    const QFontMetricsF metrics(font);
    return QSizeF(metrics.horizontalAdvance(text), metrics.height());
}

} // namespace md::render
