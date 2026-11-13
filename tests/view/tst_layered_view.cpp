// Tests for the layer-backed paint path of md::View (L2).
//
// The shape of every test is the same: paint a small view once, change exactly
// one input, paint again, and assert both what the second pass rasterised and
// that the frame equals a full repaint of the new content. "What the second
// pass rasterised" is read from the painter's clip inside paintContent(), which
// is the region the layer let through; that is the direct measurement of
// "only the damaged region was re-rasterised".
//
// The uncached debug mode is the reference: md::view::setLayerDebugUncached(true)
// makes every view paint straight into its widget, so comparing the two frames
// is the ghost detector the architecture document requires.

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QTest>
#include <QtGlobal>

#include <md/core/measure_spec.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>
#include <md/view/layer_debug.h>
#include <md/view/view.h>

namespace {

constexpr int kWidth = 160;
constexpr int kHeight = 96;

// The rectangle the second frame changes. It is comfortably inside the view so
// a retained frame and a full frame can be compared at the edges.
QRect markRect()
{
    return QRect(24, 16, 12, 12);
}

// A leaf view that paints its theme surface and one small mark. It records what
// the current pass was allowed to rasterise: paintCount() counts content hooks,
// lastClip() is the painter's clip (the region the layer allowed through: its
// damage for a partial pass, the whole surface for a full invalidation) and
// lastDamage() is what currentDamage() reported.
class LayeredTestView : public md::View
{
public:
    explicit LayeredTestView(QWidget* parent = nullptr)
        : md::View(parent)
    {
    }

    void setMarkColour(const QColor& colour) { mark_ = colour; }
    void setLayerDpr(qreal dpr) { layerDpr_ = dpr; }

    [[nodiscard]] int paintCount() const noexcept { return paintCount_; }
    [[nodiscard]] QRectF lastClip() const noexcept { return lastClip_; }
    [[nodiscard]] QRectF lastDamage() const noexcept { return lastDamage_; }

    void resetObservations()
    {
        paintCount_ = 0;
        lastClip_ = QRectF();
        lastDamage_ = QRectF();
    }

protected:
    void measureOverride(md::MeasureSpec width, md::MeasureSpec height) override
    {
        setMeasuredSize(QSizeF(width.resolve(kWidth), height.resolve(kHeight)));
    }

    void paintContent(QPainter& painter, const QRectF& content) override
    {
        ++paintCount_;
        lastClip_ = painter.clipBoundingRect();
        lastDamage_ = currentDamage().bounds();

        const md::Theme theme = md::Theme::forWidget(this);
        painter.fillRect(content, theme.colours().surface);
        painter.fillRect(QRectF(markRect()), mark_);
    }

    [[nodiscard]] qreal layerDevicePixelRatio() const override { return layerDpr_; }

private:
    QColor mark_ = QColor(Qt::red);
    qreal layerDpr_ = 1.0;
    int paintCount_ = 0;
    QRectF lastClip_;
    QRectF lastDamage_;
};

// Shows the widget, lets the show-time paint settle and returns the frame it
// produced. Every test starts from this known, fully painted state.
QImage settle(md::View& view)
{
    view.show();
    QTest::qWait(0);
    return view.grab().toImage();
}

// An uncached frame of the same view, produced by the debug mode. Turning the
// mode on drops the retained layer at the next paint, which is exactly the
// comparison being made.
QImage uncachedFrame(md::View& view)
{
    md::view::setLayerDebugUncached(true);
    const QImage frame = view.grab().toImage();
    md::view::setLayerDebugUncached(false);
    return frame;
}

} // namespace

class TestLayeredView : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void cachedAndUncachedFramesAreIdentical();
    void secondFrameRasterisesOnlyDamage();
    void cleanFrameRepaintsNothing();
    void resizeInvalidatesTheLayer();
    void dprChangeInvalidatesTheLayer();
    void themeChangeInvalidatesTheLayer();
    void debugModeLeavesNoLayers();
};

void TestLayeredView::init()
{
    md::view::setLayerDebugUncached(false);
}

void TestLayeredView::cleanup()
{
    md::view::setLayerDebugUncached(false);
}

void TestLayeredView::cachedAndUncachedFramesAreIdentical()
{
    LayeredTestView view;
    view.resize(kWidth, kHeight);
    view.show();
    QTest::qWait(0);

    md::view::setLayerDebugUncached(true);
    const QImage uncached = view.grab().toImage();
    QVERIFY(!uncached.isNull());
    QVERIFY(!view.hasLayerSurface());

    md::view::setLayerDebugUncached(false);
    view.setLayerBacked(true);
    QVERIFY(view.isLayerBacked());
    QVERIFY(!view.hasLayerSurface()); // nothing has painted yet

    view.resetObservations();
    const QImage cached = view.grab().toImage();
    QVERIFY(!cached.isNull());

    QCOMPARE(cached.size(), uncached.size());
    QVERIFY2(cached == uncached,
             "the retained frame differs from the direct frame");
    QVERIFY(view.hasLayerSurface());
    QVERIFY(md::view::liveLayerCount() >= 1);
    QCOMPARE(view.paintCount(), 1);
}

void TestLayeredView::secondFrameRasterisesOnlyDamage()
{
    LayeredTestView view;
    view.resize(kWidth, kHeight);
    view.setLayerBacked(true);
    QVERIFY(!settle(view).isNull());

    view.resetObservations();
    view.setMarkColour(QColor(Qt::blue));
    view.requestUpdate(QRectF(markRect()));
    const QImage partial = view.grab().toImage();

    // One content hook, clipped to the damaged rectangle, and the content hook
    // saw the same rectangle through currentDamage().
    QCOMPARE(view.paintCount(), 1);
    const QRect clip = view.lastClip().toAlignedRect();
    QVERIFY2(clip.contains(markRect()),
             "the damaged rectangle was not fully rasterised");
    QVERIFY2(clip.width() <= markRect().width() + 1
                 && clip.height() <= markRect().height() + 1,
             "more than the damaged region was rasterised");
    QVERIFY(view.lastDamage().contains(QRectF(markRect())));

    // Reusing the rest of the surface must not change the picture: the
    // partially repainted frame equals a full repaint of the same content.
    QVERIFY2(partial == uncachedFrame(view),
             "partial repaint differs from a full repaint of the same content");
}

void TestLayeredView::cleanFrameRepaintsNothing()
{
    LayeredTestView view;
    view.resize(kWidth, kHeight);
    view.setLayerBacked(true);
    const QImage first = settle(view);
    QVERIFY(!first.isNull());

    view.resetObservations();
    const QImage second = view.grab().toImage();

    // No damage means no content hook at all: the frame is the retained layer.
    QCOMPARE(view.paintCount(), 0);
    QVERIFY(view.hasLayerSurface());
    QVERIFY2(second == first, "a clean frame changed the retained pixels");
}

void TestLayeredView::resizeInvalidatesTheLayer()
{
    LayeredTestView view;
    view.resize(kWidth, kHeight);
    view.setLayerBacked(true);
    QVERIFY(!settle(view).isNull());
    QCOMPARE(view.layerPixelSize(), QSize(kWidth, kHeight));

    view.resize(kWidth + 40, kHeight + 20);
    view.resetObservations();
    const QImage resized = view.grab().toImage();

    QCOMPARE(view.layerPixelSize(), QSize(kWidth + 40, kHeight + 20));
    QCOMPARE(view.paintCount(), 1);
    QCOMPARE(view.lastClip().toAlignedRect(), QRect(0, 0, kWidth + 40, kHeight + 20));

    QVERIFY2(resized == uncachedFrame(view),
             "a resized layer frame differs from a direct frame");
}

void TestLayeredView::dprChangeInvalidatesTheLayer()
{
    LayeredTestView view;
    view.resize(kWidth, kHeight);
    view.setLayerBacked(true);
    QVERIFY(!settle(view).isNull());
    QCOMPARE(view.layerPixelSize(), QSize(kWidth, kHeight));

    // Simulate a screen change: the layer must be reallocated at the new ratio
    // and every pixel repainted.
    view.setLayerDpr(2.0);
    view.resetObservations();
    view.grab();

    QCOMPARE(view.layerPixelSize(), QSize(kWidth * 2, kHeight * 2));
    QCOMPARE(view.paintCount(), 1);
    QCOMPARE(view.lastClip().toAlignedRect(), QRect(0, 0, kWidth, kHeight));
}

void TestLayeredView::themeChangeInvalidatesTheLayer()
{
    LayeredTestView view;
    view.resize(kWidth, kHeight);
    view.setLayerBacked(true);
    QVERIFY(!settle(view).isNull());

    const quint64 before = view.layerThemeRevision();
    md::ThemeScope* scope = md::ThemeScope::install(&view, md::Theme::baselineDark());
    QVERIFY(scope != nullptr);

    view.resetObservations();
    const QImage themed = view.grab().toImage();

    QVERIFY2(view.layerThemeRevision() > before,
             "the resolved theme change did not advance the layer revision");
    QCOMPARE(view.paintCount(), 1);
    QCOMPARE(view.lastClip().toAlignedRect(), QRect(0, 0, kWidth, kHeight));

    QVERIFY2(themed == uncachedFrame(view),
             "the re-themed layer frame differs from a direct frame");
}

void TestLayeredView::debugModeLeavesNoLayers()
{
    QCOMPARE(md::view::liveLayerCount(), 0);

    LayeredTestView view;
    view.resize(kWidth, kHeight);
    view.setLayerBacked(true);
    QVERIFY(!settle(view).isNull());

    QVERIFY(view.hasLayerSurface());
    QVERIFY(md::view::liveLayerCount() >= 1);

    // The debug mode bypasses every layer and paints directly, so no surface
    // survives the pass that enters it.
    md::view::setLayerDebugUncached(true);
    QVERIFY(!view.grab().isNull());
    QVERIFY(!view.hasLayerSurface());
    QCOMPARE(md::view::liveLayerCount(), 0);

    // Leaving the mode re-acquires a layer on the next paint.
    md::view::setLayerDebugUncached(false);
    QVERIFY(!view.grab().isNull());
    QVERIFY(view.hasLayerSurface());
    QVERIFY(md::view::liveLayerCount() >= 1);
}

QTEST_MAIN(TestLayeredView)

// Q_OBJECT in a single-translation-unit test needs the generated header, the
// same as every other test in this tree (AUTOMOC cannot link it separately).
#include "tst_layered_view.moc"
