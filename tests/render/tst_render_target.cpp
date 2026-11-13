// render layer tests: the offscreen surface.
//
// RenderTarget is where the device pixel ratio becomes real pixels, and where
// an allocation is refused instead of clamped. These assertions pin the three
// rules the rest of the layer relies on: invalid input allocates nothing, a
// ratio multiplies the pixel size exactly, and the pixels a caller reads back
// are a copy rather than a window into the live surface.

#include <md/render/render_target.h>

#include <QColor>
#include <QImage>
#include <QSize>
#include <QSizeF>
#include <QTest>

#include <limits>

using md::render::RenderTarget;
using md::render::RenderTargetFormat;

namespace {

bool allPixelsEqual(const QImage& image, const QColor& colour)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y) != colour)
                return false;
        }
    }
    return true;
}

bool isValidSize(const QSizeF& size)
{
    return std::isfinite(static_cast<double>(size.width()))
        && std::isfinite(static_cast<double>(size.height())) && size.width() > 0.0
        && size.height() > 0.0;
}

} // namespace

class TestRenderTarget : public QObject
{
    Q_OBJECT

private slots:
    void defaultTargetIsInvalid();
    void invalidSizesAllocateNothing();
    void invalidPixelRatiosAreRefused();
    void oversizedTargetsAreRefused();
    void devicePixelRatioSetsThePixelSize();
    void requestedFormatIsKept();
    void clearFillsEveryPixel();
    void readBackIsACopy();
    void resizeReplacesOrInvalidates();
};

void TestRenderTarget::defaultTargetIsInvalid()
{
    RenderTarget target;

    QVERIFY(!target.isValid());
    QVERIFY(target.image().isNull());
    QVERIFY(target.imageRef().isNull());
    QVERIFY(target.readBack().isNull());
    QVERIFY(target.pixelSize().isEmpty());
    QCOMPARE(target.logicalSize(), QSizeF());
    QCOMPARE(target.devicePixelRatio(), 1.0);
}

void TestRenderTarget::invalidSizesAllocateNothing()
{
    const QSizeF invalidSizes[] = {
        QSizeF(),
        QSizeF(0.0, 10.0),
        QSizeF(10.0, 0.0),
        QSizeF(-4.0, 8.0),
        QSizeF(8.0, -4.0),
        QSizeF(std::numeric_limits<qreal>::quiet_NaN(), 8.0),
        QSizeF(8.0, std::numeric_limits<qreal>::infinity()),
        QSizeF(std::numeric_limits<qreal>::infinity(),
               std::numeric_limits<qreal>::infinity()),
    };

    for (const QSizeF& size : invalidSizes) {
        QVERIFY2(!isValidSize(size), "the table must only contain invalid sizes");
        RenderTarget target(size);
        QVERIFY(!target.isValid());
        QVERIFY(target.image().isNull());
        QVERIFY(target.readBack().isNull());
    }
}

void TestRenderTarget::invalidPixelRatiosAreRefused()
{
    const qreal invalidRatios[] = {
        0.0,
        -1.0,
        std::numeric_limits<qreal>::quiet_NaN(),
        std::numeric_limits<qreal>::infinity(),
    };

    for (const qreal ratio : invalidRatios) {
        RenderTargetFormat format;
        format.devicePixelRatio = ratio;

        RenderTarget target(QSizeF(8.0, 8.0), format);
        QVERIFY(!target.isValid());
        QVERIFY(target.image().isNull());
    }
}

void TestRenderTarget::oversizedTargetsAreRefused()
{
    // 10 000 x 10 000 is 100 million pixels, far above the gfx target cap.
    RenderTarget huge(QSizeF(10000.0, 10000.0));
    QVERIFY(!huge.isValid());
    QVERIFY(huge.image().isNull());

    // The cap applies to the allocated pixels, not the logical size: a target
    // that fits at ratio 1 no longer fits at ratio 2.
    RenderTargetFormat doubled;
    doubled.devicePixelRatio = 2.0;
    RenderTarget overCap(QSizeF(4096.0, 2048.0), doubled);
    QVERIFY(!overCap.isValid());
    QVERIFY(overCap.image().isNull());

    // The same size at ratio 1 is exactly the cap and is accepted.
    RenderTargetFormat plain;
    RenderTarget atCap(QSizeF(4096.0, 2048.0), plain);
    QVERIFY(atCap.isValid());
    QCOMPARE(atCap.pixelSize(), QSize(4096, 2048));

    RenderTarget small(QSizeF(16.0, 16.0));
    QVERIFY(small.isValid());
    QCOMPARE(small.pixelSize(), QSize(16, 16));
}

void TestRenderTarget::devicePixelRatioSetsThePixelSize()
{
    RenderTarget target(QSizeF(10.0, 5.0));
    QVERIFY(target.isValid());
    QCOMPARE(target.pixelSize(), QSize(10, 5));
    QCOMPARE(target.logicalSize(), QSizeF(10.0, 5.0));
    QCOMPARE(target.devicePixelRatio(), 1.0);
    QCOMPARE(target.image().size(), target.pixelSize());

    RenderTargetFormat doubled;
    doubled.devicePixelRatio = 2.0;
    RenderTarget retina(QSizeF(10.0, 5.0), doubled);
    QVERIFY(retina.isValid());
    QCOMPARE(retina.pixelSize(), QSize(20, 10));
    QCOMPARE(retina.image().size(), QSize(20, 10));
    QCOMPARE(retina.logicalSize(), QSizeF(10.0, 5.0));
    QCOMPARE(retina.devicePixelRatio(), 2.0);

    // A fractional ratio rounds up: 10 x 1.5 = 15, 5 x 1.5 = 7.5 -> 8.
    RenderTargetFormat fractional;
    fractional.devicePixelRatio = 1.5;
    RenderTarget scaled(QSizeF(10.0, 5.0), fractional);
    QVERIFY(scaled.isValid());
    QCOMPARE(scaled.pixelSize(), QSize(15, 8));
}

void TestRenderTarget::requestedFormatIsKept()
{
    RenderTargetFormat format;
    format.format = QImage::Format_ARGB32;
    format.devicePixelRatio = 2.0;

    RenderTarget target(QSizeF(4.0, 4.0), format);
    QVERIFY(target.isValid());
    QCOMPARE(target.format().format, QImage::Format_ARGB32);
    QCOMPARE(target.format().devicePixelRatio, 2.0);
    QCOMPARE(target.imageFormat(), QImage::Format_ARGB32);
    QCOMPARE(target.image().format(), QImage::Format_ARGB32);

    RenderTargetFormat invalidFormat;
    invalidFormat.format = QImage::Format_Invalid;
    RenderTarget refused(QSizeF(4.0, 4.0), invalidFormat);
    QVERIFY(!refused.isValid());
    QVERIFY(refused.image().isNull());
}

void TestRenderTarget::clearFillsEveryPixel()
{
    RenderTarget target(QSizeF(4.0, 3.0));
    QVERIFY(target.isValid());

    target.clear(QColor(Qt::red));
    QVERIFY(allPixelsEqual(target.image(), QColor(Qt::red)));
    QCOMPARE(target.image().pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(target.image().pixelColor(3, 2), QColor(Qt::red));

    target.clear(QColor(10, 20, 30, 255));
    QVERIFY(allPixelsEqual(target.image(), QColor(10, 20, 30, 255)));

    target.clear(Qt::transparent);
    QVERIFY(allPixelsEqual(target.image(), QColor(0, 0, 0, 0)));

    // Clearing an invalid target is a no-op rather than a crash.
    RenderTarget invalid;
    invalid.clear(Qt::red);
    QVERIFY(!invalid.isValid());
}

void TestRenderTarget::readBackIsACopy()
{
    RenderTarget target(QSizeF(4.0, 4.0));
    target.clear(QColor(Qt::red));
    QVERIFY(target.isValid());

    const QImage first = target.readBack();
    QCOMPARE(first.size(), target.pixelSize());
    QVERIFY(allPixelsEqual(first, QColor(Qt::red)));

    QImage modified = target.readBack();
    modified.setPixelColor(1, 1, QColor(Qt::blue));
    QCOMPARE(modified.pixelColor(1, 1), QColor(Qt::blue));

    // The modification landed in the copy, not in the target.
    QCOMPARE(target.image().pixelColor(1, 1), QColor(Qt::red));
    QCOMPARE(target.readBack().pixelColor(1, 1), QColor(Qt::red));

    // A copy taken before a clear keeps the old pixels.
    target.clear(QColor(Qt::green));
    QCOMPARE(first.pixelColor(1, 1), QColor(Qt::red));
    QCOMPARE(target.image().pixelColor(1, 1), QColor(Qt::green));
}

void TestRenderTarget::resizeReplacesOrInvalidates()
{
    RenderTarget target(QSizeF(8.0, 8.0));
    QVERIFY(target.isValid());
    QCOMPARE(target.pixelSize(), QSize(8, 8));

    RenderTargetFormat format;
    format.devicePixelRatio = 2.0;
    QVERIFY(target.resize(QSizeF(6.0, 3.0), format));
    QVERIFY(target.isValid());
    QCOMPARE(target.pixelSize(), QSize(12, 6));
    QCOMPARE(target.logicalSize(), QSizeF(6.0, 3.0));
    QCOMPARE(target.devicePixelRatio(), 2.0);

    // A refused resize leaves the target invalid rather than holding the old
    // pixels under a new size.
    QVERIFY(!target.resize(QSizeF(-1.0, 4.0)));
    QVERIFY(!target.isValid());
    QVERIFY(target.image().isNull());

    RenderTargetFormat hugeRatio;
    hugeRatio.devicePixelRatio = 4.0;
    QVERIFY(!target.resize(QSizeF(4096.0, 2048.0), hugeRatio));
    QVERIFY(!target.isValid());
}

QTEST_GUILESS_MAIN(TestRenderTarget)

#include "tst_render_target.moc"
