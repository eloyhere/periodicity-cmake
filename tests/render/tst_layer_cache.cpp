// Unit tests for the retained-surface cache (render/): Layer and LayerCache.
//
// Everything here is headless — a Layer is a QImage and neither class needs a
// window. The frame-pinning test is the one that encodes the eviction
// guarantee: a layer acquired in an open frame is never a candidate, so a
// second acquire that cannot free space is refused instead of dropping it.

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QSize>
#include <QSizeF>
#include <QTest>
#include <QtGlobal>

#include <md/render/layer.h>
#include <md/render/layer_cache.h>

namespace {

constexpr qreal kWidth = 64.0;
constexpr qreal kHeight = 32.0;

qint64 bytesForOneLayer()
{
    const md::render::Layer probe(QSizeF(kWidth, kHeight), 1.0);
    return probe.byteSize();
}

} // namespace

class TestLayerCache : public QObject
{
    Q_OBJECT

private slots:
    void acquireReleaseReacquire();
    void lruEvictsLeastRecentlyUsed();
    void invalidateAllForcesMiss();
    void layerIsNotEvictedMidFrame();
    void statsAddUp();
    void geometryChangeReallocatesAndInvalidates();
    void themeAndFontChangeInvalidate();
    void oversizedLayerIsRefused();

    void damageCoalescesAndClears();
    void painterIsScaledAndClipped();
};

void TestLayerCache::acquireReleaseReacquire()
{
    md::render::LayerCache cache;

    QVERIFY(!cache.contains(7));

    md::render::Layer* first = cache.acquire(7, QSizeF(kWidth, kHeight), 1.0, 5);
    QVERIFY(first != nullptr);
    QCOMPARE(first->pixelSize(), QSize(64, 32));
    QCOMPARE(first->themeRevision(), quint64(5));
    QVERIFY(first->isDirty());
    QVERIFY(first->isFullDamage());

    QCOMPARE(cache.layerCount(), 1);
    QCOMPARE(cache.stats().misses, quint64(1));
    QCOMPARE(cache.stats().hits, quint64(0));
    QCOMPARE(cache.stats().bytes, first->byteSize());
    QCOMPARE(cache.bytes(), first->byteSize());

    cache.release(7);

    // Release keeps the surface; the next acquire of the same key is a hit and
    // hands back the very same Layer.
    md::render::Layer* again = cache.acquire(7, QSizeF(kWidth, kHeight), 1.0, 5);
    QCOMPARE(again, first);
    QCOMPARE(cache.stats().hits, quint64(1));
    QCOMPARE(cache.stats().misses, quint64(1));
    QCOMPARE(cache.stats().releases, quint64(1));
    QCOMPARE(cache.layerCount(), 1);
}

void TestLayerCache::lruEvictsLeastRecentlyUsed()
{
    const qint64 oneLayer = bytesForOneLayer();
    QVERIFY(oneLayer > 0);

    // Room for exactly two layers.
    md::render::LayerCache cache(oneLayer * 2);

    md::render::Layer* a = cache.acquire(1, QSizeF(kWidth, kHeight), 1.0);
    md::render::Layer* b = cache.acquire(2, QSizeF(kWidth, kHeight), 1.0);
    QVERIFY(a != nullptr);
    QVERIFY(b != nullptr);

    // Touch the older layer so the second one becomes the least recently used.
    QCOMPARE(cache.acquire(1, QSizeF(kWidth, kHeight), 1.0), a);

    md::render::Layer* c = cache.acquire(3, QSizeF(kWidth, kHeight), 1.0);
    QVERIFY(c != nullptr);

    QCOMPARE(cache.stats().evicted, quint64(1));
    QVERIFY(cache.contains(1));
    QVERIFY(!cache.contains(2));
    QVERIFY(cache.contains(3));
    QCOMPARE(cache.layerCount(), 2);
    QCOMPARE(cache.bytes(), a->byteSize() + c->byteSize());
}

void TestLayerCache::invalidateAllForcesMiss()
{
    md::render::LayerCache cache;

    QVERIFY(cache.acquire(1, QSizeF(kWidth, kHeight), 1.0) != nullptr);
    QCOMPARE(cache.layerCount(), 1);

    cache.invalidateAll();

    QCOMPARE(cache.layerCount(), 0);
    QCOMPARE(cache.bytes(), qint64(0));
    QCOMPARE(cache.stats().invalidated, quint64(1));

    QVERIFY(cache.acquire(1, QSizeF(kWidth, kHeight), 1.0) != nullptr);
    QCOMPARE(cache.stats().misses, quint64(2));
    QCOMPARE(cache.stats().hits, quint64(0));
}

void TestLayerCache::layerIsNotEvictedMidFrame()
{
    const qint64 oneLayer = bytesForOneLayer();
    md::render::LayerCache cache(oneLayer);

    cache.beginFrame();
    QVERIFY(cache.isInFrame());

    md::render::Layer* a = cache.acquire(1, QSizeF(kWidth, kHeight), 1.0);
    QVERIFY(a != nullptr);

    // The only cached layer is pinned by the open frame, so the cache refuses
    // rather than evicting a layer the frame is compositing.
    md::render::Layer* b = cache.acquire(2, QSizeF(kWidth, kHeight), 1.0);
    QVERIFY(b == nullptr);
    QCOMPARE(cache.stats().refused, quint64(1));
    QVERIFY(!cache.lastError().isEmpty());
    QVERIFY(cache.contains(1));
    QVERIFY(!cache.contains(2));
    QVERIFY(cache.bytes() <= cache.budgetBytes());

    cache.endFrame();
    QVERIFY(!cache.isInFrame());

    // At the frame boundary the pin is gone, so the same acquire evicts the
    // least recently used layer normally.
    md::render::Layer* b2 = cache.acquire(2, QSizeF(kWidth, kHeight), 1.0);
    QVERIFY(b2 != nullptr);
    QCOMPARE(cache.stats().evicted, quint64(1));
    QVERIFY(!cache.contains(1));
    QVERIFY(cache.contains(2));
}

void TestLayerCache::statsAddUp()
{
    const qint64 oneLayer = bytesForOneLayer();
    md::render::LayerCache cache(oneLayer * 2);

    md::render::Layer* a = cache.acquire(1, QSizeF(kWidth, kHeight), 1.0);
    md::render::Layer* b = cache.acquire(2, QSizeF(kWidth, kHeight), 1.0);
    QVERIFY(a != nullptr);
    QVERIFY(b != nullptr);

    cache.release(1);
    QCOMPARE(cache.acquire(1, QSizeF(kWidth, kHeight), 1.0), a); // hit
    cache.invalidate(2);
    md::render::Layer* c = cache.acquire(3, QSizeF(kWidth, kHeight), 1.0); // miss
    QVERIFY(c != nullptr);

    const md::render::LayerCacheStats stats = cache.stats();
    QCOMPARE(stats.acquires, quint64(4));
    QCOMPARE(stats.hits, quint64(1));
    QCOMPARE(stats.misses, quint64(3));
    QCOMPARE(stats.refused, quint64(0));
    QCOMPARE(stats.acquires, stats.hits + stats.misses + stats.refused);
    QCOMPARE(stats.invalidated, quint64(1));
    QCOMPARE(stats.layers, cache.layerCount());
    QCOMPARE(stats.layers, 2);
    QCOMPARE(stats.bytes, cache.bytes());
    QCOMPARE(stats.bytes, a->byteSize() + c->byteSize());
    QVERIFY(stats.peakBytes >= stats.bytes);
}

void TestLayerCache::geometryChangeReallocatesAndInvalidates()
{
    md::render::LayerCache cache;

    md::render::Layer* layer = cache.acquire(1, QSizeF(kWidth, kHeight), 1.0);
    QVERIFY(layer != nullptr);
    layer->markClean();
    QVERIFY(!layer->isDirty());

    // A device pixel ratio change keeps the surface object but reallocates it
    // and marks every pixel stale.
    md::render::Layer* scaled = cache.acquire(1, QSizeF(kWidth, kHeight), 2.0);
    QCOMPARE(scaled, layer);
    QCOMPARE(layer->pixelSize(), QSize(128, 64));
    QVERIFY(layer->isDirty());
    QVERIFY(layer->isFullDamage());
    QCOMPARE(cache.stats().hits, quint64(1));
    QCOMPARE(cache.layerCount(), 1);
    QCOMPARE(cache.bytes(), layer->byteSize());

    layer->markClean();
    QVERIFY(!layer->isDirty());

    // A logical size change does the same.
    md::render::Layer* resized = cache.acquire(1, QSizeF(20.0, 10.0), 1.0);
    QCOMPARE(resized, layer);
    QCOMPARE(layer->pixelSize(), QSize(20, 10));
    QVERIFY(layer->isDirty());
    QVERIFY(layer->isFullDamage());
    QCOMPARE(cache.bytes(), layer->byteSize());
}

void TestLayerCache::themeAndFontChangeInvalidate()
{
    md::render::LayerCache cache;

    md::render::Layer* layer = cache.acquire(1, QSizeF(kWidth, kHeight), 1.0, 0);
    QVERIFY(layer != nullptr);
    layer->markClean();
    QVERIFY(!layer->isDirty());

    // A theme revision change is a full invalidation; it is a hit, because the
    // surface itself was found by key and only its pixels are stale.
    QCOMPARE(cache.acquire(1, QSizeF(kWidth, kHeight), 1.0, 3), layer);
    QCOMPARE(layer->themeRevision(), quint64(3));
    QVERIFY(layer->isDirty());
    QVERIFY(layer->isFullDamage());
    QCOMPARE(cache.stats().hits, quint64(1));

    layer->markClean();
    layer->setFontSignature(QByteArrayLiteral("sans|11|400"));
    QVERIFY(layer->isDirty());
    QVERIFY(layer->isFullDamage());

    // The same signature again is not a change.
    layer->markClean();
    layer->setFontSignature(QByteArrayLiteral("sans|11|400"));
    QVERIFY(!layer->isDirty());

    // syncEnvironment() is the one-call form for both inputs.
    layer->markClean();
    QVERIFY(layer->syncEnvironment(4, QByteArrayLiteral("serif|13|700")));
    QVERIFY(layer->isDirty());
    QVERIFY(layer->isFullDamage());
    QVERIFY(!layer->syncEnvironment(4, QByteArrayLiteral("serif|13|700")));
}

void TestLayerCache::oversizedLayerIsRefused()
{
    md::render::LayerCache cache(512);

    QVERIFY(cache.acquire(1, QSizeF(kWidth, kHeight), 1.0) == nullptr);
    QCOMPARE(cache.layerCount(), 0);
    QCOMPARE(cache.bytes(), qint64(0));
    QCOMPARE(cache.stats().refused, quint64(1));
    QVERIFY(!cache.lastError().isEmpty());

    // The refusal did not poison the cache: a layer that fits is still served.
    md::render::Layer* small = cache.acquire(2, QSizeF(8.0, 8.0), 1.0);
    QVERIFY(small != nullptr);
    QVERIFY(cache.lastError().isEmpty());
}

void TestLayerCache::damageCoalescesAndClears()
{
    md::render::Layer layer(QSizeF(kWidth, kHeight), 1.0);
    QVERIFY(layer.isValid());
    QCOMPARE(layer.pixelSize(), QSize(64, 32));

    // A fresh layer is fully dirty and starts transparent.
    QVERIFY(layer.isDirty());
    QVERIFY(layer.isFullDamage());
    QCOMPARE(layer.damageRectCount(), 1);

    layer.markClean();
    QVERIFY(!layer.isDirty());
    QCOMPARE(layer.damageRectCount(), 0);

    layer.addDamage(QRect(10, 10, 8, 8));
    QVERIFY(layer.isDirty());
    QCOMPARE(layer.damageRectCount(), 1);

    // Overlapping rectangles merge into their union.
    layer.addDamage(QRect(14, 14, 8, 8));
    QCOMPARE(layer.damageRectCount(), 1);
    QCOMPARE(layer.damageBounds().toAlignedRect(), QRect(10, 10, 12, 12));

    // Damage is clipped to the surface.
    layer.markClean();
    layer.addDamage(QRect(-10, -10, 20, 20));
    QCOMPARE(layer.damageBounds().toAlignedRect(), QRect(0, 0, 10, 10));

    // Covering the whole surface collapses to a full invalidation.
    layer.markClean();
    layer.addDamage(QRect(0, 0, 64, 32));
    QVERIFY(layer.isFullDamage());

    // A rectangle outside the surface is ignored entirely.
    layer.markClean();
    layer.addDamage(QRect(100, 100, 5, 5));
    QVERIFY(!layer.isDirty());
}

void TestLayerCache::painterIsScaledAndClipped()
{
    md::render::Layer layer(QSizeF(20.0, 20.0), 2.0);
    QVERIFY(layer.isValid());
    QCOMPARE(layer.pixelSize(), QSize(40, 40));

    layer.markClean();
    layer.addDamage(QRect(5, 5, 4, 4));
    QVERIFY(!layer.isFullDamage());

    QPainter* painter = layer.beginPaint();
    QVERIFY(painter != nullptr);
    QVERIFY(layer.isPainting());
    // The painter is in logical coordinates: filling the whole logical layer
    // must still be clipped to the damaged rectangle.
    painter->fillRect(QRectF(0.0, 0.0, 20.0, 20.0), QColor(Qt::red));
    layer.endPaint();
    QVERIFY(!layer.isPainting());

    const QImage& image = layer.target().image();
    QCOMPARE(image.pixelColor(12, 12).rgba(), QColor(Qt::red).rgba());
    QCOMPARE(image.pixelColor(2, 2).alpha(), 0);

    // A full invalidation clears the surface, so a painter can begin again and
    // the pixels outside the new drawing are transparent, not last frame's.
    layer.markClean();
    layer.addAll();
    QPainter* second = layer.beginPaint();
    QVERIFY(second != nullptr);
    QCOMPARE(layer.target().image().pixelColor(12, 12).alpha(), 0);
    second->fillRect(QRectF(0.0, 0.0, 10.0, 10.0), QColor(Qt::blue));
    layer.endPaint();

    QCOMPARE(layer.target().image().pixelColor(19, 19).rgba(), QColor(Qt::blue).rgba());
    QCOMPARE(layer.target().image().pixelColor(30, 30).alpha(), 0);
}

QTEST_MAIN(TestLayerCache)

// The test class carries Q_OBJECT, and the target compiles this file as a single
// translation unit, so AUTOMOC requires the generated header to be included here
// rather than linked in separately. Every other test in the tree does the same.
#include "tst_layer_cache.moc"
