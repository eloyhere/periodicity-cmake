// Unit tests for the drawable module (L3a): painting primitives.
//
// Every test paints into a QImage with no widget behind it, which is also the
// contract the drawables make: they receive a QPainter and a rectangle and
// must work headlessly. Pixels are read back with pixelColor() so an assertion
// names the exact colour a user would see.

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QTest>
#include <QtGlobal>
#include <QtMath>

#include <md/core/view_state.h>
#include <md/drawable/drawable.h>
#include <md/drawable/icon_drawable.h>
#include <md/drawable/layer_drawable.h>
#include <md/drawable/shadow_drawable.h>
#include <md/drawable/shape_drawable.h>
#include <md/drawable/state_layer_drawable.h>
#include <md/tokens/elevation.h>
#include <md/tokens/shape.h>
#include <md/tokens/state_layer.h>

#include <memory>
#include <stdexcept>

using md::Elevation;
using md::Shape;
using md::ViewState;
using md::ViewStates;
using md::drawable::Drawable;
using md::drawable::IconDrawable;
using md::drawable::LayerDrawable;
using md::drawable::ShadowDrawable;
using md::drawable::ShapeDrawable;
using md::drawable::StateLayerDrawable;

namespace {

constexpr int kImageSize = 160;

QImage newImage()
{
    QImage image(kImageSize, kImageSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}

// Renders one drawable into a transparent image, optionally with an explicit
// layout direction so the icon mirroring path can be exercised.
QImage render(const Drawable& drawable, const QRectF& bounds,
              Qt::LayoutDirection direction = Qt::LeftToRight)
{
    QImage image = newImage();

    QPainter painter(&image);
    painter.setLayoutDirection(direction);
    drawable.draw(painter, bounds);
    painter.end();

    return image;
}

// Like render(), but the target device reports `ratio` as its device pixel
// ratio. A QImage stores whatever it is given, including 0 and NaN, which are
// the values a broken device can hand a drawable.
//
// The ratio is changed after the painter begins on purpose: QPainter takes a
// snapshot of the device ratio into the high-DPI part of its transform in
// begin(), so painting through a painter opened on a 0/NaN image would be
// suppressed before the drawable ever saw the device. Changing the image
// afterwards reproduces the device state the guard is about while leaving the
// painter's own transform usable.
QImage renderWithDevicePixelRatio(const Drawable& drawable, const QRectF& bounds,
                                  qreal ratio)
{
    QImage image = newImage();

    QPainter painter(&image);
    image.setDevicePixelRatio(ratio);
    drawable.draw(painter, bounds);
    painter.end();

    return image;
}

bool isFullyTransparent(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) != 0)
                return false;
        }
    }
    return true;
}

int alphaAt(const QImage& image, int x, int y)
{
    return image.pixelColor(x, y).alpha();
}

// A drawable that only records what the base class tells it, so the protected
// onStateChanged() contract can be tested without a real primitive.
class StateProbe : public Drawable
{
public:
    void draw(QPainter&, const QRectF&) const override {}

    int changes = 0;
    ViewStates lastPrevious;

protected:
    void onStateChanged(ViewStates previous) override
    {
        ++changes;
        lastPrevious = previous;
    }
};

// A drawable that fails every paint, so the composite's isolation can be
// tested without a real primitive failing for an unrelated reason.
class ThrowingDrawable : public Drawable
{
public:
    void draw(QPainter&, const QRectF&) const override
    {
        throw std::runtime_error("paint failure");
    }
};

} // namespace

class TestDrawable : public QObject
{
    Q_OBJECT

private slots:
    void shapeFillsInsideAndRoundsCorners();
    void shapeStrokeDrawsEdgesOnly();
    void shapeTransparentFillDrawsNothing();
    void stateLayerUsesTokenOpacity();
    void iconTintsAndReportsIntrinsicSize();
    void iconMirrorsOnlyInRightToLeft();
    void layerDrawablePaintsInInsertionOrder();
    void shadowMarginsGrowWithElevation();
    void shadowPaintsOutsideItsBounds();
    void shadowNegativeBlurScaleDisablesOffset();
    void shadowHugeBoundsDoesNotAllocate();
    void iconRejectsInvalidSizes();
    void iconHandlesInvalidDevicePixelRatio();
    void shadowHandlesInvalidDevicePixelRatio();
    void layerIsolatesThrowingChild();
    void revisionAndStateChangeInvalidate();
};

void TestDrawable::shapeFillsInsideAndRoundsCorners()
{
    const QRectF bounds(10.0, 10.0, 140.0, 140.0);
    ShapeDrawable drawable(Shape::medium(), QColor(Qt::red));

    const QImage image = render(drawable, bounds);

    // The interior is the fill colour.
    QCOMPARE(image.pixelColor(80, 80), QColor(Qt::red));

    // A 12 dp rounded corner leaves the corner pixel transparent...
    QCOMPARE(alphaAt(image, 10, 10), 0);
    QCOMPARE(alphaAt(image, 12, 12), 0);

    // ...and nothing is painted outside the bounds.
    QCOMPARE(alphaAt(image, 2, 80), 0);
}

void TestDrawable::shapeStrokeDrawsEdgesOnly()
{
    const QRectF bounds(10.0, 10.0, 140.0, 140.0);

    ShapeDrawable strokeOnly(Shape::medium());
    strokeOnly.setStroke(QColor(Qt::black), 2.0);
    QCOMPARE(strokeOnly.strokeWidth(), 2.0);

    const QImage image = render(strokeOnly, bounds);

    // The 2 dp stroke is centred on the path, so it covers y = 9..11 and the
    // top-edge pixel is fully opaque; the interior stays untouched.
    QCOMPARE(image.pixelColor(80, 10), QColor(Qt::black));
    QCOMPARE(alphaAt(image, 80, 80), 0);
    QCOMPARE(alphaAt(image, 10, 10), 0);

    // A zero-width stroke is invisible even with a valid colour.
    ShapeDrawable zeroWidth(Shape::medium());
    zeroWidth.setStroke(QColor(Qt::black), 0.0);
    QVERIFY(isFullyTransparent(render(zeroWidth, bounds)));
}

void TestDrawable::shapeTransparentFillDrawsNothing()
{
    // A fully transparent fill is the default and must not spend a draw call,
    // leave a fringe, or otherwise touch the target.
    ShapeDrawable drawable(Shape::medium(), QColor(Qt::transparent));
    QVERIFY(isFullyTransparent(render(drawable, QRectF(10.0, 10.0, 140.0, 140.0))));

    ShapeDrawable defaulted{Shape::medium()};
    QVERIFY(isFullyTransparent(render(defaulted, QRectF(10.0, 10.0, 140.0, 140.0))));
}

void TestDrawable::stateLayerUsesTokenOpacity()
{
    const QRectF bounds(0.0, 0.0, kImageSize, kImageSize);

    StateLayerDrawable overlay{QColor(Qt::black)};

    // Hover is the 8 % state-layer token, not a hard-coded grey.
    overlay.setState(ViewStates(ViewState::Hovered));
    const QImage hovered = render(overlay, bounds);
    const int expectedHover = qRound(md::stateLayerOpacity(ViewState::Hovered) * 255.0);
    QVERIFY(qAbs(alphaAt(hovered, 80, 80) - expectedHover) <= 2);
    QCOMPARE(hovered.pixelColor(80, 80).red(), 0);

    // Pressed is the stronger 12 % token.
    overlay.setState(ViewStates(ViewState::Pressed));
    const QImage pressed = render(overlay, bounds);
    const int expectedPressed = qRound(md::stateLayerOpacity(ViewState::Pressed) * 255.0);
    QVERIFY(qAbs(alphaAt(pressed, 80, 80) - expectedPressed) <= 2);
    QVERIFY(alphaAt(pressed, 80, 80) > alphaAt(hovered, 80, 80));

    // An enabled-but-idle state has no overlay at all.
    overlay.setState(ViewStates(ViewState::Enabled));
    QVERIFY(isFullyTransparent(render(overlay, bounds)));

    // An empty state (no Enabled bit) likewise produces nothing.
    overlay.setState(ViewStates());
    QVERIFY(isFullyTransparent(render(overlay, bounds)));
}

void TestDrawable::iconTintsAndReportsIntrinsicSize()
{
    QPixmap solid(64, 64);
    solid.fill(Qt::white);

    IconDrawable icon;
    icon.setPixmap(solid);
    icon.setSize(QSizeF(64.0, 64.0));
    icon.setColour(QColor(Qt::red));

    QCOMPARE(icon.intrinsicSize(), QSizeF(64.0, 64.0));

    const QImage image = render(icon, QRectF(0.0, 0.0, kImageSize, kImageSize));

    // The 64 dp box is centred in the 160 dp image, so its centre is the tint
    // colour and the pixels outside the box are untouched.
    QCOMPARE(image.pixelColor(80, 80), QColor(Qt::red));
    QCOMPARE(alphaAt(image, 80, 10), 0);
    QCOMPARE(alphaAt(image, 10, 80), 0);

    // An invalid colour leaves the source untinted.
    icon.setColour(QColor());
    QCOMPARE(render(icon, QRectF(0.0, 0.0, kImageSize, kImageSize)).pixelColor(80, 80),
             QColor(Qt::white));
}

void TestDrawable::iconMirrorsOnlyInRightToLeft()
{
    // A direction-sensitive mask: the leading half is opaque, the trailing half
    // is transparent.
    QPixmap mask(32, 32);
    mask.fill(Qt::transparent);
    {
        QPainter painter(&mask);
        painter.fillRect(QRect(0, 0, 16, 32), Qt::white);
    }

    IconDrawable icon;
    icon.setPixmap(mask);
    icon.setSize(QSizeF(32.0, 32.0));
    icon.setColour(QColor(Qt::blue));
    icon.setMirrored(true);

    const QRectF bounds(0.0, 0.0, kImageSize, kImageSize);
    const QImage leftToRight = render(icon, bounds, Qt::LeftToRight);
    const QImage rightToLeft = render(icon, bounds, Qt::RightToLeft);

    // The 32 dp box is centred at 64..96; the leading half is 64..80.
    QCOMPARE(leftToRight.pixelColor(72, 80), QColor(Qt::blue));
    QCOMPARE(alphaAt(leftToRight, 88, 80), 0);

    QCOMPARE(alphaAt(rightToLeft, 72, 80), 0);
    QCOMPARE(rightToLeft.pixelColor(88, 80), QColor(Qt::blue));

    // With the flag off, right-to-left paints the same way as left-to-right.
    icon.setMirrored(false);
    const QImage unmirrored = render(icon, bounds, Qt::RightToLeft);
    QCOMPARE(unmirrored.pixelColor(72, 80), QColor(Qt::blue));
    QCOMPARE(alphaAt(unmirrored, 88, 80), 0);
}

void TestDrawable::layerDrawablePaintsInInsertionOrder()
{
    LayerDrawable composite;
    composite.add(std::make_unique<ShapeDrawable>(Shape::none(), QColor(Qt::red)));
    composite.add(std::make_unique<ShapeDrawable>(Shape::none(), QColor(Qt::blue)));

    QVERIFY(composite.size() == 2);
    QVERIFY(!composite.empty());
    QVERIFY(composite.at(0) != nullptr);
    QVERIFY(composite.at(2) == nullptr);

    const QRectF bounds(0.0, 0.0, kImageSize, kImageSize);

    // The later child covers the earlier one.
    QCOMPARE(render(composite, bounds).pixelColor(80, 80), QColor(Qt::blue));

    // Reversing the insertion order reverses the winner: the parents' order is
    // the only thing that decides the result.
    LayerDrawable reversed;
    reversed.add(std::make_unique<ShapeDrawable>(Shape::none(), QColor(Qt::blue)));
    reversed.add(std::make_unique<ShapeDrawable>(Shape::none(), QColor(Qt::red)));
    QCOMPARE(render(reversed, bounds).pixelColor(80, 80), QColor(Qt::red));

    composite.clear();
    QVERIFY(composite.size() == 0);
    QVERIFY(composite.empty());
}

void TestDrawable::shadowMarginsGrowWithElevation()
{
    ShadowDrawable shadow;
    QCOMPARE(shadow.elevation(), Elevation::Level0);
    QCOMPARE(shadow.shadowMargins(), QMarginsF());

    shadow.setElevation(Elevation::Level1);
    const QMarginsF low = shadow.shadowMargins();

    shadow.setElevation(Elevation::Level3);
    const QMarginsF high = shadow.shadowMargins();

    QVERIFY(low.left() > 0.0);
    QVERIFY(high.left() > low.left());
    QVERIFY(high.top() > low.top());
    QVERIFY(high.right() > low.right());
    QVERIFY(high.bottom() > low.bottom());

    // The blur scale widens the shadow, so a component that reserves the
    // margin still never clips it.
    shadow.setBlurScale(2.0);
    QVERIFY(shadow.shadowMargins().left() > high.left());

    shadow.setBlurScale(0.0);
    QCOMPARE(shadow.shadowMargins(), QMarginsF());
}

void TestDrawable::shadowPaintsOutsideItsBounds()
{
    const QRectF bounds(40.0, 40.0, 80.0, 80.0);

    // Level 0 draws nothing at all.
    ShadowDrawable flat;
    flat.setShape(Shape::medium());
    QVERIFY(isFullyTransparent(render(flat, bounds)));

    ShadowDrawable shadow;
    shadow.setShape(Shape::medium());
    shadow.setElevation(Elevation::Level3);

    const QImage image = render(shadow, bounds);

    // The mask covers the shape interior...
    QVERIFY(alphaAt(image, 80, 80) > 0);

    // ...and, unlike a background, it paints outside the bounds rectangle.
    bool paintedOutside = false;
    for (int y = 0; y < kImageSize && !paintedOutside; ++y) {
        for (int x = 0; x < kImageSize; ++x) {
            const QPointF centre(x + 0.5, y + 0.5);
            if (!bounds.contains(centre) && alphaAt(image, x, y) > 0) {
                paintedOutside = true;
                break;
            }
        }
    }
    QVERIFY(paintedOutside);

    // The drop offset makes the shadow strongest below the surface.
    QVERIFY(alphaAt(image, 80, 125) > 0);
}

void TestDrawable::shadowNegativeBlurScaleDisablesOffset()
{
    const QRectF bounds(40.0, 40.0, 80.0, 80.0);

    ShadowDrawable shadow;
    shadow.setShape(Shape::medium());
    shadow.setElevation(Elevation::Level3);
    shadow.setBlurScale(-2.0);

    // The setter clamps to zero, so the offset and the margins agree: the
    // documented "disable the blur and the offset" behaviour. Before the
    // clamp the offset was mirrored while the margins stayed zero.
    QCOMPARE(shadow.blurScale(), 0.0);
    QCOMPARE(shadow.shadowMargins(), QMarginsF());

    // A non-finite scale is clamped the same way: NaN can be neither compared
    // nor rendered, and infinity would make every margin non-finite.
    ShadowDrawable nonFinite;
    nonFinite.setElevation(Elevation::Level3);
    nonFinite.setBlurScale(qQNaN());
    QCOMPARE(nonFinite.blurScale(), 0.0);
    QCOMPARE(nonFinite.shadowMargins(), QMarginsF());
    nonFinite.setBlurScale(qInf());
    QCOMPARE(nonFinite.blurScale(), 0.0);

    const QImage image = render(shadow, bounds);

    // The unshifted silhouette still paints inside the bounds...
    QVERIFY(alphaAt(image, 80, 80) > 0);

    // ...all the way to the bottom edge, which a shadow shifted upwards by the
    // negative scale could not reach...
    QVERIFY(alphaAt(image, 80, 119) > 0);

    // ...and nothing at all above the bounds.
    QCOMPARE(alphaAt(image, 80, 39), 0);
}

void TestDrawable::shadowHugeBoundsDoesNotAllocate()
{
    // Without the device-pixel cap this asks for a 40000 x 40000 ARGB image
    // (6.4 GB) and then a 1.6 GB alpha vector. The call must return without
    // painting and without attempting either allocation.
    ShadowDrawable shadow;
    shadow.setShape(Shape::medium());
    shadow.setElevation(Elevation::Level5);

    QVERIFY(isFullyTransparent(render(shadow, QRectF(0.0, 0.0, 40000.0, 40000.0))));

    // Non-finite bounds must take the same early exit rather than reaching
    // qCeil() or QImage with NaN/infinity.
    QVERIFY(isFullyTransparent(render(shadow, QRectF(0.0, 0.0, qQNaN(), 100.0))));
    QVERIFY(isFullyTransparent(render(shadow, QRectF(0.0, 0.0, qInf(), 100.0))));
}

void TestDrawable::iconRejectsInvalidSizes()
{
    IconDrawable icon;
    QCOMPARE(icon.size(), QSizeF(24.0, 24.0));

    // A negative extent is "no preference", not an inverted box.
    icon.setSize(QSizeF(-8.0, 32.0));
    QCOMPARE(icon.size(), QSizeF(0.0, 32.0));
    QCOMPARE(icon.intrinsicSize(), QSizeF(0.0, 32.0));

    // A non-finite size is rejected outright and leaves the previous value
    // (and the revision) untouched.
    icon.setSize(QSizeF(24.0, 24.0));
    const quint64 before = icon.revision();
    icon.setSize(QSizeF(qQNaN(), 24.0));
    icon.setSize(QSizeF(24.0, qInf()));
    QCOMPARE(icon.size(), QSizeF(24.0, 24.0));
    QCOMPARE(icon.revision(), before);

    // A clamped-to-zero box paints nothing and must not divide by NaN.
    QPixmap solid(8, 8);
    solid.fill(Qt::white);
    IconDrawable collapsed;
    collapsed.setPixmap(solid);
    collapsed.setSize(QSizeF(-8.0, -8.0));
    QCOMPARE(collapsed.size(), QSizeF(0.0, 0.0));
    QVERIFY(isFullyTransparent(render(collapsed, QRectF(0.0, 0.0, kImageSize, kImageSize))));
}

void TestDrawable::iconHandlesInvalidDevicePixelRatio()
{
    QPixmap solid(64, 64);
    solid.fill(Qt::white);

    IconDrawable icon;
    icon.setPixmap(solid);
    icon.setSize(QSizeF(64.0, 64.0));
    icon.setColour(QColor(Qt::red));

    const QRectF bounds(0.0, 0.0, kImageSize, kImageSize);

    // A ratio of 0 used to collapse the requested pixel size to 1 x 1 and tag
    // the cached pixmap with 0; NaN reached qCeil() as an undefined
    // conversion. Both now fall back to 1.0 for sizing, keying and tagging.
    QCOMPARE(renderWithDevicePixelRatio(icon, bounds, 0.0).pixelColor(80, 80),
             QColor(Qt::red));
    QCOMPARE(renderWithDevicePixelRatio(icon, bounds, qQNaN()).pixelColor(80, 80),
             QColor(Qt::red));

    // A usable ratio is still honoured; the fallback is not a cap.
    QCOMPARE(renderWithDevicePixelRatio(icon, bounds, 2.0).pixelColor(80, 80),
             QColor(Qt::red));
}

void TestDrawable::shadowHandlesInvalidDevicePixelRatio()
{
    const QRectF bounds(40.0, 40.0, 80.0, 80.0);

    ShadowDrawable shadow;
    shadow.setShape(Shape::medium());
    shadow.setElevation(Elevation::Level3);
    // A distinct colour keeps this test independent of the pixmap cached by
    // the other shadow tests.
    shadow.setColour(QColor(20, 20, 20));

    // At ratio 0 the old code computed an empty exact size and silently drew
    // nothing (and would have tagged a raster with 0); NaN reached qCeil().
    QVERIFY(alphaAt(renderWithDevicePixelRatio(shadow, bounds, 0.0), 80, 80) > 0);
    QVERIFY(alphaAt(renderWithDevicePixelRatio(shadow, bounds, qQNaN()), 80, 80) > 0);

    // A usable ratio is still honoured; the fallback is not a cap.
    QVERIFY(alphaAt(renderWithDevicePixelRatio(shadow, bounds, 2.0), 80, 80) > 0);
}

void TestDrawable::layerIsolatesThrowingChild()
{
    LayerDrawable composite;
    composite.add(std::make_unique<ThrowingDrawable>());
    composite.add(std::make_unique<ShapeDrawable>(Shape::none(), QColor(Qt::blue)));

    // The failing child must not abort the composite: the sibling after it
    // still paints, and no exception escapes to the caller.
    const QImage image = render(composite, QRectF(0.0, 0.0, kImageSize, kImageSize));
    QCOMPARE(image.pixelColor(80, 80), QColor(Qt::blue));
}

void TestDrawable::revisionAndStateChangeInvalidate()
{
    ShapeDrawable drawable(Shape::small(), QColor(Qt::red));
    QCOMPARE(drawable.revision(), quint64(0));

    drawable.invalidate();
    QCOMPARE(drawable.revision(), quint64(1));
    drawable.invalidate();
    QCOMPARE(drawable.revision(), quint64(2));

    // Appearance setters invalidate as well, so a cache keyed on
    // (cacheKey(), revision()) can never outlive its configuration.
    const quint64 beforeSetters = drawable.revision();
    drawable.setFill(QColor(Qt::blue));
    QVERIFY(drawable.revision() > beforeSetters);

    // A state change bumps the revision exactly once, and re-setting the same
    // state is a no-op.
    const quint64 beforeState = drawable.revision();
    drawable.setState(ViewStates(ViewState::Hovered));
    QVERIFY(drawable.revision() > beforeState);
    const quint64 afterState = drawable.revision();
    drawable.setState(ViewStates(ViewState::Hovered));
    QCOMPARE(drawable.revision(), afterState);

    // The protected hook sees the previous bitset.
    StateProbe probe;
    probe.setState(ViewStates(ViewState::Hovered));
    QCOMPARE(probe.changes, 1);
    QVERIFY(probe.lastPrevious == ViewStates(ViewState::Enabled));
    probe.setState(ViewStates(ViewState::Focused));
    QCOMPARE(probe.changes, 2);

    // cacheKey differs when the rendered appearance differs.
    const quint64 redKey = ShapeDrawable(Shape::small(), QColor(Qt::red)).cacheKey();
    const quint64 blueKey = ShapeDrawable(Shape::small(), QColor(Qt::blue)).cacheKey();
    QVERIFY(redKey != blueKey);
}

QTEST_MAIN(TestDrawable)

#include "tst_drawable.moc"
