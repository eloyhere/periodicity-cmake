// Unit tests for the Canvas and its three drawing contexts.
//
// The tests are headless (ctest sets QT_QPA_PLATFORM=offscreen): they render
// widgets into ARGB images, inspect pixels and exercise the software projection
// path directly. The OpenGL backend is only tested for its documented
// fallback: with no context available the Canvas must remain paintable, warn
// once and keep drawing through QPainter.

#include <md/components/canvas.h>
#include <md/theme/theme.h>

#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QFont>
#include <QImage>
#include <QMessageLogContext>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRectF>
#include <QRegion>
#include <QSize>
#include <QTest>
#include <QVector3D>
#include <QWidget>

#include <cmath>
#include <exception>
#include <optional>
#include <stdexcept>

namespace {

int gFallbackWarningCount = 0;

void countFallbackWarnings(QtMsgType type, const QMessageLogContext& context,
                           const QString& message)
{
    Q_UNUSED(context);
    if (type == QtWarningMsg
        && message.contains(QStringLiteral("falling back to software"))) {
        ++gFallbackWarningCount;
    }
}

QImage renderWidget(QWidget& widget)
{
    const qreal ratio = widget.devicePixelRatioF();
    QImage image(widget.size() * ratio, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    widget.render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    painter.end();

    return image;
}

QColor pixelAt(const QImage& image, const QPointF& logical)
{
    const qreal ratio = image.devicePixelRatio();
    const int x = qBound(0, qRound(logical.x() * ratio), image.width() - 1);
    const int y = qBound(0, qRound(logical.y() * ratio), image.height() - 1);
    return image.pixelColor(x, y);
}

bool isWhite(const QColor& colour)
{
    return colour.alpha() > 200 && colour.red() > 240 && colour.green() > 240
        && colour.blue() > 240;
}

int nonWhitePixelsIn(const QImage& image, const QRectF& area)
{
    int count = 0;
    const int left = qMax(0, qFloor(area.left()));
    const int top = qMax(0, qFloor(area.top()));
    const int right = qCeil(area.right());
    const int bottom = qCeil(area.bottom());

    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            if (!isWhite(pixelAt(image, QPointF(x, y))))
                ++count;
        }
    }

    return count;
}

} // namespace

class TestCanvas : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void modeSwitching();
    void context2dPrimitivesPaint();
    void context2dTransformMapsOrigin();
    void context1dClampsAndMapsMonotonically();

    void context3dProjectsKnownCube();
    void context3dPerspectiveShrinksAndOrthographicDoesNot();
    void context3dSoftwareRendererPaints();

    void throwingPrimitiveLeavesCanvasPaintable();
    void openGLBackendFallsBackWithoutContext();
};

class ThrowingCanvas final : public md::Canvas
{
public:
    explicit ThrowingCanvas(QWidget* parent = nullptr)
        : md::Canvas(parent)
    {
    }

    int paintAttempts = 0;

protected:
    void paintContent(QPainter& painter, const QRectF& content) override
    {
        ++paintAttempts;
        Canvas::paintContent(painter, content);

        // Simulate a primitive that fails after the context has emitted valid
        // drawing commands. View's paint guard must catch this and the next
        // paint must still work.
        throw std::runtime_error("canvas test exception");
    }
};

void TestCanvas::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestCanvas::modeSwitching()
{
    md::Canvas canvas;
    QCOMPARE(canvas.mode(), md::Canvas::Mode::OneD);
    QVERIFY(canvas.context1d() != nullptr);
    QVERIFY(canvas.context2d() != nullptr);
    QVERIFY(canvas.context3d() != nullptr);

    const md::Canvas& constCanvas = canvas;
    QVERIFY(constCanvas.context1d() != nullptr);
    QVERIFY(constCanvas.context2d() != nullptr);
    QVERIFY(constCanvas.context3d() != nullptr);

    canvas.context1d()->setRange(2.0, 8.0);
    canvas.context1d()->setValues({3.0, 5.0, 7.0});

    canvas.setMode(md::Canvas::Mode::TwoD);
    QCOMPARE(canvas.mode(), md::Canvas::Mode::TwoD);
    canvas.setMode(md::Canvas::Mode::ThreeD);
    QCOMPARE(canvas.mode(), md::Canvas::Mode::ThreeD);
    canvas.setMode(md::Canvas::Mode::OneD);

    // Each context survives a mode round trip.
    QCOMPARE(canvas.context1d()->minimum(), 2.0);
    QCOMPARE(canvas.context1d()->maximum(), 8.0);
    QCOMPARE(canvas.context1d()->values().size(), 3);

    canvas.setBackground(Qt::white);
    QCOMPARE(canvas.background(), QColor(Qt::white));
    canvas.requestDraw();
}

void TestCanvas::context2dPrimitivesPaint()
{
    md::Canvas canvas;
    canvas.resize(220, 180);
    canvas.setBackground(Qt::white);
    canvas.setMode(md::Canvas::Mode::TwoD);

    md::Context2D* context = canvas.context2d();
    QVERIFY(context != nullptr);

    const QColor red(200, 30, 30);
    context->setPen(QPen(red, 2.0));
    context->setBrush(red);
    context->drawRect(QRectF(10.0, 10.0, 40.0, 30.0));

    context->setPen(QPen(QColor(30, 160, 60), 2.0));
    context->setBrush(QColor(30, 160, 60));
    context->drawEllipse(QRectF(70.0, 10.0, 40.0, 30.0));

    context->setPen(QPen(QColor(40, 80, 200), 2.0));
    context->setBrush(QColor(40, 80, 200));
    context->drawRoundedRect(QRectF(130.0, 10.0, 60.0, 30.0), 8.0, 8.0);

    QPolygonF polyline;
    polyline << QPointF(10.0, 60.0) << QPointF(90.0, 80.0)
             << QPointF(160.0, 60.0);
    context->setPen(QPen(QColor(160, 40, 160), 2.0));
    context->setBrush(QBrush(Qt::NoBrush));
    context->drawPolyline(polyline);
    context->drawLine(QPointF(10.0, 100.0), QPointF(70.0, 100.0));

    context->setPen(QPen(QColor(220, 130, 20), 2.0));
    context->drawArc(QRectF(85.0, 90.0, 55.0, 40.0), 0, 180 * 16);

    QPainterPath path;
    path.moveTo(160.0, 105.0);
    path.lineTo(205.0, 105.0);
    path.lineTo(182.0, 138.0);
    path.closeSubpath();
    context->setPen(QPen(QColor(30, 120, 120), 2.0));
    context->setBrush(QColor(30, 120, 120));
    context->drawPath(path);

    QFont font;
    font.setPixelSize(16);
    context->setFont(font);
    context->setPen(QColor(Qt::black));
    context->setBrush(QBrush(Qt::NoBrush));
    context->drawText(QPointF(10.0, 165.0), QStringLiteral("Canvas 2D"));

    canvas.requestDraw();
    const QImage image = renderWidget(canvas);
    QVERIFY(!image.isNull());
    QVERIFY(nonWhitePixelsIn(image, QRectF(0.0, 0.0, 220.0, 180.0)) > 250);

    QCOMPARE(pixelAt(image, QPointF(30.0, 25.0)).rgb(), red.rgb());
    QCOMPARE(pixelAt(image, QPointF(90.0, 25.0)).rgb(),
             QColor(30, 160, 60).rgb());
    QCOMPARE(pixelAt(image, QPointF(160.0, 25.0)).rgb(),
             QColor(40, 80, 200).rgb());
    QVERIFY(nonWhitePixelsIn(image, QRectF(85.0, 85.0, 60.0, 50.0)) > 20);
    QVERIFY(nonWhitePixelsIn(image, QRectF(10.0, 145.0, 200.0, 25.0)) > 20);
}

void TestCanvas::context2dTransformMapsOrigin()
{
    md::Canvas canvas;
    canvas.resize(200, 150);
    canvas.setBackground(Qt::white);
    canvas.setMode(md::Canvas::Mode::TwoD);

    md::Context2D* context = canvas.context2d();
    QVERIFY(context != nullptr);

    QCOMPARE(context->mapToWidget(QPointF(0.0, 0.0)), QPointF(0.0, 0.0));

    context->setTransform(2.0, 2.0, 20.0, 10.0);
    QCOMPARE(context->transform().m11(), 2.0);
    QCOMPARE(context->transform().m22(), 2.0);
    QCOMPARE(context->mapToWidget(QPointF(0.0, 0.0)), QPointF(20.0, 10.0));
    QCOMPARE(context->mapToWidget(QPointF(5.0, 5.0)), QPointF(30.0, 20.0));
    QCOMPARE(context->mapToData(QPointF(20.0, 10.0)), QPointF(0.0, 0.0));

    const QColor red(210, 20, 20);
    context->setPen(QPen(Qt::NoPen));
    context->setBrush(red);
    context->drawRect(QRectF(0.0, 0.0, 20.0, 10.0));

    canvas.requestDraw();
    const QImage image = renderWidget(canvas);
    QCOMPARE(pixelAt(image, QPointF(30.0, 15.0)).rgb(), red.rgb());
    QVERIFY(isWhite(pixelAt(image, QPointF(150.0, 100.0))));
}

void TestCanvas::context1dClampsAndMapsMonotonically()
{
    md::Context1D context;
    context.setRange(10.0, 0.0);
    QCOMPARE(context.minimum(), 0.0);
    QCOMPARE(context.maximum(), 10.0);

    context.setValues({-5.0, 5.0, 15.0});
    QCOMPARE(context.values().size(), 3);
    QCOMPARE(context.values().at(0), 0.0);
    QCOMPARE(context.values().at(1), 5.0);
    QCOMPARE(context.values().at(2), 10.0);

    const QRectF area(0.0, 0.0, 100.0, 50.0);
    QCOMPARE(context.valueToY(0.0, area), area.bottom());
    QCOMPARE(context.valueToY(5.0, area), 25.0);
    QCOMPARE(context.valueToY(10.0, area), area.top());
    QCOMPARE(context.valueToY(-100.0, area), area.bottom());
    QCOMPARE(context.valueToY(1000.0, area), area.top());

    QVERIFY(context.pointAt(0, area).x() < context.pointAt(1, area).x());
    QVERIFY(context.pointAt(1, area).x() < context.pointAt(2, area).x());
    QVERIFY(context.pointAt(0, area).y() > context.pointAt(1, area).y());
    QVERIFY(context.pointAt(1, area).y() > context.pointAt(2, area).y());

    context.setTicks(-3);
    QCOMPARE(context.ticks(), 0);
    context.setTicks(5000);
    QCOMPARE(context.ticks(), 1000);

    md::Canvas canvas;
    canvas.resize(200, 80);
    canvas.setBackground(Qt::white);
    canvas.setMode(md::Canvas::Mode::OneD);
    canvas.context1d()->setRange(0.0, 100.0);
    canvas.context1d()->setValues({0.0, 25.0, 80.0, 100.0, 40.0});
    canvas.context1d()->setTicks(3);
    canvas.requestDraw();

    const QImage image = renderWidget(canvas);
    QVERIFY(nonWhitePixelsIn(image, QRectF(5.0, 5.0, 190.0, 70.0)) > 30);
}

void TestCanvas::context3dProjectsKnownCube()
{
    md::Context3D context;
    context.setViewport(QRectF(0.0, 0.0, 200.0, 200.0));
    context.setCamera(QVector3D(0.0f, 0.0f, 5.0f), QVector3D(0.0f, 0.0f, 0.0f),
                      QVector3D(0.0f, 1.0f, 0.0f));
    context.perspective(60.0, 0.1, 100.0);

    const std::optional<QPointF> centre = context.project(QVector3D(0.0f, 0.0f, 1.0f));
    QVERIFY(centre.has_value());
    QVERIFY(std::abs(centre->x() - 100.0) < 1e-6);
    QVERIFY(std::abs(centre->y() - 100.0) < 1e-6);

    // The camera looks down -Z at +Z cube face. All four corners of that face
    // are in front of the near plane and project in the expected screen order.
    const std::optional<QPointF> bottomLeft =
        context.project(QVector3D(-1.0f, -1.0f, 1.0f));
    const std::optional<QPointF> bottomRight =
        context.project(QVector3D(1.0f, -1.0f, 1.0f));
    const std::optional<QPointF> topLeft =
        context.project(QVector3D(-1.0f, 1.0f, 1.0f));
    const std::optional<QPointF> topRight =
        context.project(QVector3D(1.0f, 1.0f, 1.0f));

    QVERIFY(bottomLeft.has_value());
    QVERIFY(bottomRight.has_value());
    QVERIFY(topLeft.has_value());
    QVERIFY(topRight.has_value());
    QVERIFY(bottomLeft->x() < bottomRight->x());
    QVERIFY(topLeft->y() < bottomLeft->y());
    QVERIFY(topRight->x() > 100.0);
    QVERIFY(topRight->x() < 200.0);

    // A point inside the near plane, behind the camera, and beyond the far
    // plane is not projected.
    QVERIFY(!context.project(QVector3D(0.0f, 0.0f, 4.95f)).has_value());
    QVERIFY(!context.project(QVector3D(0.0f, 0.0f, 6.0f)).has_value());
    QVERIFY(!context.project(QVector3D(0.0f, 0.0f, -2000.0f)).has_value());

    context.setBackFaceCulling(true);
    QVERIFY(context.backFaceCulling());
    context.setBackFaceCulling(false);
    QVERIFY(!context.backFaceCulling());

    QCOMPARE(context.primitiveCount(), 0);
    context.line3D(QVector3D(0.0f, 0.0f, 0.0f), QVector3D(1.0f, 0.0f, 0.0f));
    context.triangle3D(QVector3D(0.0f, 0.0f, 0.0f), QVector3D(1.0f, 0.0f, 0.0f),
                       QVector3D(0.0f, 1.0f, 0.0f));
    context.quad3D(QVector3D(0.0f, 0.0f, 0.0f), QVector3D(1.0f, 0.0f, 0.0f),
                   QVector3D(1.0f, 1.0f, 0.0f), QVector3D(0.0f, 1.0f, 0.0f));
    context.points3D(QVector<QVector3D>{QVector3D(0.0f, 0.0f, 0.0f)});
    QCOMPARE(context.primitiveCount(), 4);
    context.clearPrimitives();
    QCOMPARE(context.primitiveCount(), 0);
}

void TestCanvas::context3dPerspectiveShrinksAndOrthographicDoesNot()
{
    md::Context3D context;
    context.setViewport(QRectF(0.0, 0.0, 200.0, 200.0));
    context.setCamera(QVector3D(0.0f, 0.0f, 5.0f), QVector3D(0.0f, 0.0f, 0.0f),
                      QVector3D(0.0f, 1.0f, 0.0f));

    context.perspective(60.0, 0.1, 100.0);
    const std::optional<QPointF> nearPoint =
        context.project(QVector3D(1.0f, 0.0f, 1.0f));
    const std::optional<QPointF> farPoint =
        context.project(QVector3D(1.0f, 0.0f, -1.0f));
    QVERIFY(nearPoint.has_value());
    QVERIFY(farPoint.has_value());
    QVERIFY(std::abs(nearPoint->x() - 100.0)
            > std::abs(farPoint->x() - 100.0) + 1.0);

    context.orthographic(4.0, 0.1, 100.0);
    QCOMPARE(context.projectionMode(), md::Context3D::Projection::Orthographic);
    const std::optional<QPointF> orthoNear =
        context.project(QVector3D(1.0f, 0.0f, 1.0f));
    const std::optional<QPointF> orthoFar =
        context.project(QVector3D(1.0f, 0.0f, -1.0f));
    QVERIFY(orthoNear.has_value());
    QVERIFY(orthoFar.has_value());
    QVERIFY(std::abs(orthoNear->x() - orthoFar->x()) < 1e-6);
}

void TestCanvas::context3dSoftwareRendererPaints()
{
    md::Canvas canvas;
    canvas.resize(200, 160);
    canvas.setBackground(Qt::white);
    canvas.setMode(md::Canvas::Mode::ThreeD);

    md::Context3D* context = canvas.context3d();
    QVERIFY(context != nullptr);
    context->setCamera(QVector3D(0.0f, 0.0f, 5.0f),
                       QVector3D(0.0f, 0.0f, 0.0f),
                       QVector3D(0.0f, 1.0f, 0.0f));
    context->perspective(60.0, 0.1, 100.0);
    context->setBackFaceCulling(true);
    context->triangle3D(QVector3D(-1.0f, -1.0f, 1.0f),
                        QVector3D(1.0f, -1.0f, 1.0f),
                        QVector3D(0.0f, 1.0f, 1.0f),
                        QColor(220, 20, 20));

    canvas.requestDraw();
    const QImage image = renderWidget(canvas);
    QVERIFY(!image.isNull());

    const QColor centre = pixelAt(image, QPointF(100.0, 80.0));
    QVERIFY(centre.red() > 150);
    QVERIFY(centre.green() < 100);
    QVERIFY(nonWhitePixelsIn(image, QRectF(40.0, 30.0, 120.0, 100.0)) > 100);
}

void TestCanvas::throwingPrimitiveLeavesCanvasPaintable()
{
    ThrowingCanvas canvas;
    canvas.resize(120, 80);
    canvas.setBackground(Qt::white);
    canvas.setMode(md::Canvas::Mode::TwoD);

    const QColor red(220, 20, 20);
    canvas.context2d()->setPen(QPen(Qt::NoPen));
    canvas.context2d()->setBrush(red);
    canvas.context2d()->drawRect(QRectF(20.0, 20.0, 60.0, 35.0));
    canvas.requestDraw();

    const QImage first = renderWidget(canvas);
    QVERIFY(!first.isNull());
    QVERIFY(canvas.paintAttempts >= 1);
    QVERIFY(canvas.lastError().contains(QStringLiteral("canvas test exception")));
    QCOMPARE(pixelAt(first, QPointF(50.0, 35.0)).rgb(), red.rgb());

    const int attemptsAfterFirst = canvas.paintAttempts;
    const QImage second = renderWidget(canvas);
    QVERIFY(!second.isNull());
    QVERIFY(canvas.paintAttempts > attemptsAfterFirst);
    QVERIFY(canvas.lastError().contains(QStringLiteral("canvas test exception")));
    QCOMPARE(pixelAt(second, QPointF(50.0, 35.0)).rgb(), red.rgb());
}

void TestCanvas::openGLBackendFallsBackWithoutContext()
{
    md::Canvas canvas;
    canvas.resize(200, 160);
    canvas.setBackground(Qt::white);
    canvas.setMode(md::Canvas::Mode::ThreeD);

    md::Context3D* context = canvas.context3d();
    QVERIFY(context != nullptr);
    context->setCamera(QVector3D(0.0f, 0.0f, 5.0f),
                       QVector3D(0.0f, 0.0f, 0.0f),
                       QVector3D(0.0f, 1.0f, 0.0f));
    context->perspective(60.0, 0.1, 100.0);
    context->triangle3D(QVector3D(-1.0f, -1.0f, 1.0f),
                        QVector3D(1.0f, -1.0f, 1.0f),
                        QVector3D(0.0f, 1.0f, 1.0f),
                        QColor(220, 20, 20));

    context->setBackend(md::Context3D::Backend::OpenGL);
    QCOMPARE(context->backend(), md::Context3D::Backend::OpenGL);
    QCOMPARE(context->effectiveBackend(), md::Context3D::Backend::Software);

    gFallbackWarningCount = 0;
    const QtMessageHandler previous = qInstallMessageHandler(countFallbackWarnings);

    const QImage first = renderWidget(canvas);
    const QImage second = renderWidget(canvas);

    qInstallMessageHandler(previous);

    QVERIFY(!first.isNull());
    QVERIFY(!second.isNull());
    QCOMPARE(context->effectiveBackend(), md::Context3D::Backend::Software);
    QCOMPARE(gFallbackWarningCount, 1);

    const QColor centre = pixelAt(second, QPointF(100.0, 80.0));
    QVERIFY(centre.red() > 150);
    QVERIFY(centre.green() < 100);
}

QTEST_MAIN(TestCanvas)
#include "tst_canvas.moc"
