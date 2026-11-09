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
