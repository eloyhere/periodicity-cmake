// render layer tests: the QPainter bridge.
//
// TextPainter is the only place in render/ that talks to QPainter, and it is
// stateless on purpose: every call saves and restores the painter so a caller's
// font, pen, brush and render hints survive. These tests exercise the four
// entry points and that state contract, without asserting glyph shapes, which
// belong to the platform's font engine rather than to this library.

#include <md/render/text_painter.h>

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>
#include <QSizeF>
#include <QTest>

using md::render::TextPainter;

namespace {

QImage whiteImage(int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::white);
    return image;
}

int countPixelsDifferentFrom(const QImage& image, const QRect& rect,
                             const QColor& background)
{
    const QRect bounded = rect.intersected(image.rect());
    int count = 0;
    for (int y = bounded.top(); y <= bounded.bottom(); ++y) {
        for (int x = bounded.left(); x <= bounded.right(); ++x) {
            if (image.pixelColor(x, y) != background)
                ++count;
        }
    }
    return count;
}

QFont testFont()
{
    QFont font;
    font.setPixelSize(16);
    return font;
}

} // namespace

class TestRenderTextPainter : public QObject
{
    Q_OBJECT

private slots:
    void drawTextPaintsInsideItsRect();
    void measureReportsPositiveSize();
    void drawPathFillsTheInterior();
    void drawImageBlitsTheSource();
    void painterStateIsRestored();
    void emptyAndInactiveInputsAreNoOps();
};

void TestRenderTextPainter::drawTextPaintsInsideItsRect()
{
    QImage image = whiteImage(96, 32);
    QPainter painter(&image);

    TextPainter::drawText(painter, QRectF(0.0, 0.0, 96.0, 32.0),
                          QStringLiteral("Hello"), testFont(), QColor(Qt::black),
                          Qt::AlignLeft | Qt::AlignVCenter);
    painter.end();

    QVERIFY(countPixelsDifferentFrom(image, QRect(0, 0, 96, 32), QColor(Qt::white))
            > 0);

    // "Hello" at 16 px is far narrower than 96 px, so the right margin is clean.
    QCOMPARE(image.pixelColor(94, 2), QColor(Qt::white));
    QCOMPARE(image.pixelColor(94, 30), QColor(Qt::white));
}

void TestRenderTextPainter::measureReportsPositiveSize()
{
    const QSizeF measured = TextPainter::measure(QStringLiteral("Hello"), testFont());
    QVERIFY(measured.width() > 0.0);
    QVERIFY(measured.height() > 0.0);

    const QSizeF empty = TextPainter::measure(QString(), testFont());
    QCOMPARE(empty, QSizeF());

    // A longer string never measures narrower than a shorter one.
    const QSizeF longer =
        TextPainter::measure(QStringLiteral("Hello, rendering"), testFont());
    QVERIFY(longer.width() > measured.width());
}

void TestRenderTextPainter::drawPathFillsTheInterior()
{
    QImage image = whiteImage(32, 32);
    QPainter painter(&image);

    QPainterPath path;
    path.addRect(QRectF(8.0, 8.0, 16.0, 16.0));
    TextPainter::drawPath(painter, path, QPen(Qt::NoPen), QBrush(QColor(Qt::red)));
    painter.end();

    QCOMPARE(image.pixelColor(16, 16), QColor(Qt::red));
    QCOMPARE(image.pixelColor(9, 9), QColor(Qt::red));
    QCOMPARE(image.pixelColor(2, 2), QColor(Qt::white));
    QCOMPARE(image.pixelColor(30, 30), QColor(Qt::white));
}

void TestRenderTextPainter::drawImageBlitsTheSource()
{
    QImage image = whiteImage(16, 16);

    QImage source(2, 2, QImage::Format_ARGB32);
    source.fill(QColor(Qt::red));

    QPainter painter(&image);
    TextPainter::drawImage(painter, source, QRectF(4.0, 4.0, 8.0, 8.0), QRectF(), false);
    painter.end();

    QCOMPARE(image.pixelColor(6, 6), QColor(Qt::red));
    QCOMPARE(image.pixelColor(11, 11), QColor(Qt::red));
    QCOMPARE(image.pixelColor(1, 1), QColor(Qt::white));

    // A source rectangle selects the matching part of the image.
    QImage half(2, 1, QImage::Format_ARGB32);
    half.setPixelColor(0, 0, QColor(Qt::green));
    half.setPixelColor(1, 0, QColor(Qt::blue));

    QImage second = whiteImage(8, 8);
    QPainter secondPainter(&second);
    TextPainter::drawImage(secondPainter, half, QRectF(0.0, 0.0, 8.0, 8.0),
                           QRectF(1.0, 0.0, 1.0, 1.0), false);
    secondPainter.end();
    QCOMPARE(second.pixelColor(4, 4), QColor(Qt::blue));
}

void TestRenderTextPainter::painterStateIsRestored()
{
    QImage image = whiteImage(32, 32);
    QPainter painter(&image);

    const QPen markerPen{QColor(Qt::green)};
    const QBrush markerBrush{QColor(Qt::blue)};
    QFont markerFont = testFont();
    markerFont.setBold(true);

    painter.setPen(markerPen);
    painter.setBrush(markerBrush);
    painter.setFont(markerFont);
    const bool textAntialiasing = painter.testRenderHint(QPainter::TextAntialiasing);
    const bool smoothPixmaps = painter.testRenderHint(QPainter::SmoothPixmapTransform);

    QPainterPath path;
    path.addRect(QRectF(2.0, 2.0, 8.0, 8.0));

    TextPainter::drawPath(painter, path, QPen(Qt::NoPen), QBrush(QColor(Qt::red)));
    QCOMPARE(painter.pen(), markerPen);
    QCOMPARE(painter.brush(), markerBrush);

    TextPainter::drawText(painter, QRectF(0.0, 0.0, 32.0, 32.0),
                          QStringLiteral("x"), testFont(), QColor(Qt::black),
                          Qt::AlignCenter);
    QCOMPARE(painter.font(), markerFont);
    QCOMPARE(painter.pen(), markerPen);
    QCOMPARE(painter.brush(), markerBrush);
    QCOMPARE(painter.testRenderHint(QPainter::TextAntialiasing), textAntialiasing);

    QImage source(1, 1, QImage::Format_ARGB32);
    source.fill(QColor(Qt::red));
    TextPainter::drawImage(painter, source, QRectF(20.0, 20.0, 4.0, 4.0), QRectF(),
                           true);
    QCOMPARE(painter.testRenderHint(QPainter::SmoothPixmapTransform), smoothPixmaps);

    painter.end();
}

void TestRenderTextPainter::emptyAndInactiveInputsAreNoOps()
{
    // No active painter: every entry point returns instead of crashing.
    QPainter inactive;
    TextPainter::drawText(inactive, QRectF(0.0, 0.0, 10.0, 10.0),
                          QStringLiteral("x"), testFont(), QColor(Qt::black), 0);
    QPainterPath path;
    path.addRect(QRectF(0.0, 0.0, 4.0, 4.0));
    TextPainter::drawPath(inactive, path, QPen(Qt::NoPen), QBrush(QColor(Qt::red)));
    TextPainter::drawImage(inactive, QImage(), QRectF(0.0, 0.0, 4.0, 4.0));
    QVERIFY(!inactive.isActive());

    // Active painter, but nothing to draw: the surface stays untouched.
    QImage image = whiteImage(16, 16);
    QPainter painter(&image);
    TextPainter::drawText(painter, QRectF(0.0, 0.0, 16.0, 16.0), QString(),
                          testFont(), QColor(Qt::black), 0);
    TextPainter::drawPath(painter, QPainterPath(), QPen(Qt::NoPen),
                          QBrush(QColor(Qt::red)));
    TextPainter::drawImage(painter, QImage(), QRectF(0.0, 0.0, 16.0, 16.0));
    painter.end();

    QCOMPARE(countPixelsDifferentFrom(image, image.rect(), QColor(Qt::white)), 0);

    // Measuring empty text is defined, not a failure.
    QCOMPARE(TextPainter::measure(QString(), testFont()), QSizeF());
}

QTEST_MAIN(TestRenderTextPainter)

#include "tst_render_text_painter.moc"
