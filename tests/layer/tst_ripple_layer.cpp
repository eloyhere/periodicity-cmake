// Periodicity — ripple layer tests (framework.md §5.5, §5.11).
//
// The layer must be a normal pipeline stage: it paints where the stack puts it,
// it reports animation state so a host can keep frames coming, and it does not
// need the surface to know anything about RippleController's internals.

#include <md/layer/ripple_layer.h>
#include <md/layer/surface_context.h>

#include <QImage>
#include <QPainter>
#include <QTest>

#include <chrono>

using namespace md;

class TestRippleLayer : public QObject
{
    Q_OBJECT

private slots:
    void paintsAfterPointerPress();
    void reportsAnimationState();
    void clipsToBounds();
    void emptyPaintBoundsAreIgnored();
    void clipsToPaintBounds();
    void nonNullParentIsNotOwned();
};

namespace {

Theme lightTheme() { return Theme::baselineLight(); }

QImage render(const layer::RippleLayer& rippleLayer, const Theme& theme, int size = 200)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    layer::SurfaceContext context{theme, ViewState::Enabled, Shape::medium(),
                                  Elevation::Level0, QColor(), QColor()};

    QPainter painter(&image);
    rippleLayer.paint(painter, QRectF(0, 0, size, size), context);
    painter.end();
    return image;
}

} // namespace

void TestRippleLayer::paintsAfterPointerPress()
{
    const Theme theme = lightTheme();
    layer::RippleLayer rippleLayer(theme);
    rippleLayer.setBounds(QRectF(0, 0, 200, 200));
    rippleLayer.setCornerRadius(0.0);

    rippleLayer.controller().pointerPressed(QPointF(100, 100));
    QTest::qWait(300);

    const QImage image = render(rippleLayer, theme);
    QVERIFY(image.pixelColor(100, 100).alpha() > 0);
}

void TestRippleLayer::reportsAnimationState()
{
    const Theme theme = lightTheme();
    layer::RippleLayer rippleLayer(theme);
    rippleLayer.setBounds(QRectF(0, 0, 200, 200));

    QVERIFY(!rippleLayer.isAnimated());

    rippleLayer.controller().pointerEntered(QPointF(40, 40));
    QVERIFY(rippleLayer.isAnimated());

    QTRY_VERIFY_WITH_TIMEOUT(!rippleLayer.isAnimated(), 3000);
    rippleLayer.controller().setEnabled(false);
}

void TestRippleLayer::clipsToBounds()
{
    const Theme theme = lightTheme();
    layer::RippleLayer rippleLayer(theme);
    rippleLayer.setBounds(QRectF(0, 0, 100, 100));

    // Slow the one-shot down so the sample lands mid-expansion rather than
    // after the fade has already removed the ripple.
    md::ripple::RippleSpec spec = md::ripple::RippleSpec::fromTheme(theme);
    spec.expansion.duration = std::chrono::milliseconds(1000);
    spec.fade.duration = std::chrono::milliseconds(1000);
    rippleLayer.controller().setSpec(spec);

    rippleLayer.controller().pointerPressed(QPointF(50, 50));
    QTest::qWait(600);

    const QImage image = render(rippleLayer, theme);
    QVERIFY(image.pixelColor(50, 50).alpha() > 0);
    QCOMPARE(image.pixelColor(150, 50).alpha(), 0);
}

QTEST_MAIN(TestRippleLayer)
#include "tst_ripple_layer.moc"

// The layer, not the controller, owns the paint rectangle: an empty paint rect
// contributes nothing even when the controller's cached bounds are not empty.
void TestRippleLayer::emptyPaintBoundsAreIgnored()
{
    const Theme theme = lightTheme();
    layer::RippleLayer rippleLayer(theme);
    rippleLayer.setBounds(QRectF(0, 0, 200, 200));
    rippleLayer.setCornerRadius(0.0);

    rippleLayer.controller().pointerPressed(QPointF(100, 100));
    QTest::qWait(300);

    // The controller does have a visible ripple; only the empty paint rectangle
    // hides it.
    QVERIFY(render(rippleLayer, theme).pixelColor(100, 100).alpha() > 0);

    QImage image(200, 200, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    layer::SurfaceContext context{theme, ViewState::Enabled, Shape::medium(),
                                  Elevation::Level0, QColor(), QColor()};

    QPainter painter(&image);
    rippleLayer.paint(painter, QRectF(), context);
    painter.end();

    QCOMPARE(image.pixelColor(100, 100).alpha(), 0);
}

// A stale or larger controller bound must not let the ripple escape the
// rectangle the stack asked the stage to paint.
void TestRippleLayer::clipsToPaintBounds()
{
    const Theme theme = lightTheme();
    layer::RippleLayer rippleLayer(theme);
    rippleLayer.setBounds(QRectF(0, 0, 200, 200));
    rippleLayer.setCornerRadius(0.0);

    // Slow the expansion so the sample lands with a radius well past the paint
    // rectangle's right edge.
    md::ripple::RippleSpec spec = md::ripple::RippleSpec::fromTheme(theme);
    spec.expansion.duration = std::chrono::milliseconds(1000);
    spec.fade.duration = std::chrono::milliseconds(1000);
    rippleLayer.controller().setSpec(spec);

    rippleLayer.controller().pointerPressed(QPointF(50, 50));
    QTest::qWait(600);

    // By now the unrestricted ripple has grown past x = 100, so the layer's
    // paint rectangle is the only thing that can keep that pixel clear.
    QVERIFY(render(rippleLayer, theme).pixelColor(101, 50).alpha() > 0);

    QImage image(200, 200, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    layer::SurfaceContext context{theme, ViewState::Enabled, Shape::medium(),
                                  Elevation::Level0, QColor(), QColor()};

    QPainter painter(&image);
    rippleLayer.paint(painter, QRectF(0, 0, 100, 100), context);
    painter.end();

    QVERIFY(image.pixelColor(50, 50).alpha() > 0);

    // The controller still has 200x200 bounds; only the layer's clip keeps this
    // pixel transparent.
    QCOMPARE(image.pixelColor(101, 50).alpha(), 0);
}

// Regression for the P1 ownership bug: a non-null parent must not adopt the
// controller value member. Destroying that parent first previously destroyed
// the controller and left the layer's destructor to free it a second time.
void TestRippleLayer::nonNullParentIsNotOwned()
{
    const Theme theme = lightTheme();

    auto* parent = new QObject;
    layer::RippleLayer rippleLayer(theme, parent);

    QVERIFY(rippleLayer.controller().parent() == nullptr);

    // The external parent dies first, before the layer.
    delete parent;

    rippleLayer.setBounds(QRectF(0, 0, 200, 200));
    rippleLayer.setCornerRadius(0.0);
    rippleLayer.controller().pointerPressed(QPointF(100, 100));
    QTest::qWait(300);

    // The controller is still alive and paints; the layer destroys it once.
    const QImage image = render(rippleLayer, theme);
    QVERIFY(image.pixelColor(100, 100).alpha() > 0);
}
