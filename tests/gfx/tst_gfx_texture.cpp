// gfx layer tests: texture sampling and the mip chain.
//
// Wrapping, filtering and mip selection are the parts of a texture that are
// easiest to get subtly wrong, so these tests assert the sampled colour rather
// than a code path: Repeat differs from Clamp past 1, Nearest returns the texel
// itself, Bilinear returns the blend at a texel corner, and every wrap mode
// survives hostile UVs without reading outside the image.

#include <md/gfx/texture.h>

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QSize>
#include <QTest>

#include <cmath>
#include <limits>

using namespace md::gfx;

namespace {

QImage twoColourImage()
{
    QImage image(2, 1, QImage::Format_ARGB32);
    image.setPixelColor(0, 0, QColor(Qt::red));
    image.setPixelColor(1, 0, QColor(Qt::blue));
    return image;
}

QImage checkerboardImage()
{
    QImage image(4, 4, QImage::Format_ARGB32);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            image.setPixelColor(x, y, ((x + y) % 2 == 0) ? QColor(Qt::black)
                                                         : QColor(Qt::white));
        }
    }
    return image;
}

qint64 colourDistance(const QColor& left, const QColor& right)
{
    const qint64 dr = static_cast<qint64>(left.red()) - right.red();
    const qint64 dg = static_cast<qint64>(left.green()) - right.green();
    const qint64 db = static_cast<qint64>(left.blue()) - right.blue();
    return dr * dr + dg * dg + db * db;
}

} // namespace

class TestGfxTexture : public QObject
{
    Q_OBJECT

private slots:
    void invalidTextureSamplesTransparent();
    void repeatDiffersFromClampPastOne();
    void nearestReturnsTexelAndBilinearBlends();
    void mipmapIsCloserToTheMeanThanNearest();
    void wrapModesAreSafeForHostileUvs();
    void buildMipmapsIsIdempotentAndStopsAtOneByOne();
};

void TestGfxTexture::invalidTextureSamplesTransparent()
{
    const Texture invalid;
    QVERIFY(!invalid.isValid());
    QCOMPARE(invalid.sample(QPointF(0.5, 0.5)).alpha(), 0);
    QCOMPARE(invalid.sample(QPointF(-1.0e6, 3.0)).alpha(), 0);

    const Texture valid(twoColourImage());
    QVERIFY(valid.isValid());
    QCOMPARE(valid.sample(QPointF(std::numeric_limits<qreal>::quiet_NaN(), 0.5)).alpha(), 0);
    QCOMPARE(valid.sample(QPointF(0.5, std::numeric_limits<qreal>::infinity())).alpha(), 0);
}

void TestGfxTexture::repeatDiffersFromClampPastOne()
{
    const Texture clamp(twoColourImage(), Wrap::Clamp, Filter::Nearest);
    const Texture repeat(twoColourImage(), Wrap::Repeat, Filter::Nearest);

    // uv 1.25: Clamp saturates on the last texel, Repeat wraps to the first.
    QCOMPARE(clamp.sample(QPointF(1.25, 0.5)), QColor(Qt::blue));
    QCOMPARE(repeat.sample(QPointF(1.25, 0.5)), QColor(Qt::red));
    QVERIFY(clamp.sample(QPointF(1.25, 0.5)) != repeat.sample(QPointF(1.25, 0.5)));

    // Negative uv: Clamp pins to texel 0, Repeat wraps onto the last texel.
    QCOMPARE(clamp.sample(QPointF(-0.25, 0.5)), QColor(Qt::red));
    QCOMPARE(repeat.sample(QPointF(-0.25, 0.5)), QColor(Qt::blue));
}

void TestGfxTexture::nearestReturnsTexelAndBilinearBlends()
{
    const Texture nearest(twoColourImage(), Wrap::Clamp, Filter::Nearest);
    QCOMPARE(nearest.sample(QPointF(0.25, 0.5)), QColor(Qt::red));
    QCOMPARE(nearest.sample(QPointF(0.75, 0.5)), QColor(Qt::blue));

    // 0.5 is the corner between the two texel centres: the bilinear result is
    // the 50/50 blend, and therefore not either texel.
    const Texture bilinear(twoColourImage(), Wrap::Clamp, Filter::Bilinear);
    const QColor blended = bilinear.sample(QPointF(0.5, 0.5));
    QCOMPARE(blended, QColor(128, 0, 128));
    QVERIFY(blended != QColor(Qt::red));
    QVERIFY(blended != QColor(Qt::blue));
}

void TestGfxTexture::mipmapIsCloserToTheMeanThanNearest()
{
    Texture texture(checkerboardImage(), Wrap::Clamp, Filter::MipmapBilinear);
    texture.buildMipmaps();
    QVERIFY(texture.hasMipmaps());

    const QColor mean(128, 128, 128);
    const QColor nearest = Texture(checkerboardImage(), Wrap::Clamp, Filter::Nearest)
                               .sample(QPointF(0.5, 0.5));
    const QColor mipmapped = texture.sample(QPointF(0.5, 0.5), 4.0);

    QVERIFY(colourDistance(mipmapped, mean) < colourDistance(nearest, mean));
    QVERIFY(colourDistance(mipmapped, mean) <= 3);
}

void TestGfxTexture::wrapModesAreSafeForHostileUvs()
{
    const qreal uvs[] = {-3.7, -1.0, 0.0, 1.0, 1.9, 1.0e6};
    const Wrap wraps[] = {Wrap::Clamp, Wrap::Repeat, Wrap::Mirror};
    const Filter filters[] = {Filter::Nearest, Filter::Bilinear, Filter::MipmapBilinear};

    for (Wrap wrap : wraps) {
        for (Filter filter : filters) {
            Texture texture(checkerboardImage(), wrap, filter);
            texture.buildMipmaps();

            for (qreal u : uvs) {
                for (qreal v : uvs) {
                    const QColor colour = texture.sample(QPointF(u, v), 1.5);
                    QVERIFY2(colour.isValid(), "sample() returned an invalid colour");
                    QCOMPARE(colour.alpha(), 255);
                }
            }
        }
    }
}

void TestGfxTexture::buildMipmapsIsIdempotentAndStopsAtOneByOne()
{
    QImage image(5, 3, QImage::Format_ARGB32);
    image.fill(QColor(40, 80, 120, 255));

    Texture texture(image);
    QCOMPARE(texture.mipLevelCount(), 1);
    QCOMPARE(texture.size(), QSize(5, 3));
    QVERIFY(!texture.hasMipmaps());

    texture.buildMipmaps();
    const int levels = texture.mipLevelCount();
    QVERIFY(levels >= 2);
    QCOMPARE(texture.mipLevel(0).size(), QSize(5, 3));
    QCOMPARE(texture.mipLevel(levels - 1).size(), QSize(1, 1));
    QCOMPARE(texture.mipLevel(levels), QImage());

    // Every level halves the previous one, rounded up, and never goes below 1.
    for (int level = 1; level < levels; ++level) {
        const QSize previous = texture.mipLevel(level - 1).size();
        const QSize expected(qMax(1, (previous.width() + 1) / 2),
                             qMax(1, (previous.height() + 1) / 2));
        QCOMPARE(texture.mipLevel(level).size(), expected);
    }

    texture.buildMipmaps();
    QCOMPARE(texture.mipLevelCount(), levels);
    QCOMPARE(texture.mipLevel(levels - 1).size(), QSize(1, 1));
}

QTEST_GUILESS_MAIN(TestGfxTexture)

#include "tst_gfx_texture.moc"
