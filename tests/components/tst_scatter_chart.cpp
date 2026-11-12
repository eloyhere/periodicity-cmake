// Tests for the ScatterChart component.
//
// The suite is offscreen and deterministic: the application theme is pinned to
// the baseline light scheme with reduced motion (so auto-fit snaps instead of
// animating), the chart is given an explicit geometry, and it is rendered into
// an explicitly transparent QImage. Point positions come from the chart's own
// MathematicsCoordination, so the plotRect assertions exercise the same
// auto-fit transform paintPlot() uses.
//
// Hover is driven with a real QMouseEvent rather than a synthetic call to the
// protected handler: that covers View's pointer dispatch, ChartBase::hitTest()
// and the repaint in one path. Leave is a real QEvent::Leave, which is the only
// event Qt sends when the pointer leaves the widget.
//
// The final cases cover the empty state and a throwing paintPlot(): neither may
// unwind into Qt's paint machinery.

#include <md/components/scatter_chart.h>
#include <md/theme/theme.h>

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QRegion>
#include <QTest>
#include <QWidget>

#include <cmath>
#include <stdexcept>

namespace {

constexpr int kChartWidth = 480;
constexpr int kChartHeight = 320;

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

md::ChartSeries makeSeries(const QString& name, const QVector<QPointF>& points)
{
    md::ChartSeries series;
    series.name = name;
    series.points = points;
    return series;
}

// Gives the chart an explicit geometry. QWidget::render() below paints it even
// while it is hidden, which keeps the suite independent of the window system.
void prepare(QWidget& widget)
{
    widget.resize(kChartWidth, kChartHeight);
}

void verifyPointsInsidePlot(md::ScatterChart& chart)
{
    const QRectF plot = chart.plotRect();
    QVERIFY2(!plot.isEmpty(), "plotRect must not be empty after the chart is laid out");
    QVERIFY(chart.math() != nullptr);

    for (const md::ChartSeries& series : chart.series()) {
        for (const QPointF& point : series.points) {
            if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
                continue;

            const QPointF pixel = chart.math()->toPixel(point);
            const QString message = QStringLiteral("data point (%1, %2) mapped to (%3, %4), "
                                                   "outside plotRect")
                                        .arg(point.x())
                                        .arg(point.y())
                                        .arg(pixel.x())
                                        .arg(pixel.y());
            QVERIFY2(plot.adjusted(-0.25, -0.25, 0.25, 0.25).contains(pixel),
                     qPrintable(message));
        }
    }
}

class ThrowingScatterChart : public md::ScatterChart
{
public:
    ThrowingScatterChart() = default;

protected:
    void paintPlot(QPainter&, const QRectF&) override
    {
        throw std::runtime_error("intentional ScatterChart paint failure");
    }
};

} // namespace

class TestScatterChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void constructsWithTwoSeriesAndDefaults();
    void pointMapsInsidePlotRect();
    void autoFitCoversNegativeValues();
    void hoverSetsIndicesAndLeaveClearsThem();
    void hitTestFindsNearPointAndRejectsFarPoint();
    void emptySeriesPaintsWithoutCrashing();
    void throwingPaintPlotDoesNotCrash();
};

void TestScatterChart::initTestCase()
{
    // Reduced motion makes the auto-fit snap instead of animating, so the
    // transform is already at its fitted range when the test samples it.
    md::Theme theme = md::Theme::baselineLight();
    md::MotionScheme motion = theme.motion();
    motion.reducedMotion = true;
    theme = theme.withMotion(motion);

    md::Theme::setApplicationTheme(theme);
    QApplication::setPalette(theme.toPalette());
}

void TestScatterChart::constructsWithTwoSeriesAndDefaults()
{
    md::ScatterChart chart;
    chart.setSeries({
        makeSeries(QStringLiteral("alpha"), {{0.0, 0.0}, {1.0, 2.0}}),
        makeSeries(QStringLiteral("beta"), {{0.5, -1.0}, {1.5, 3.0}, {2.5, 0.5}}),
    });

    QCOMPARE(chart.series().size(), qsizetype(2));
    QCOMPARE(chart.series().at(0).name, QStringLiteral("alpha"));
    QCOMPARE(chart.series().at(1).points.size(), qsizetype(3));

    QCOMPARE(chart.pointSize(), 8.0);
    QVERIFY(chart.pointShape() == md::ScatterChart::PointShape::Circle);
    QCOMPARE(chart.hoveredSeries(), -1);
    QCOMPARE(chart.hoveredPoint(), -1);

    chart.setPointSize(12.0);
    QCOMPARE(chart.pointSize(), 12.0);

    chart.setShape(md::ScatterChart::PointShape::Diamond);
    QVERIFY(chart.pointShape() == md::ScatterChart::PointShape::Diamond);

    QVERIFY(chart.chartPalette().size() >= 8);
}

void TestScatterChart::pointMapsInsidePlotRect()
{
    md::ScatterChart chart;
    chart.setSeries({
        makeSeries(QStringLiteral("alpha"), {{0.0, 0.0}, {1.0, 2.0}, {2.0, 1.0}}),
        makeSeries(QStringLiteral("beta"), {{0.5, -1.0}, {1.5, 3.0}, {2.5, 0.5}}),
    });
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    verifyPointsInsidePlot(chart);
}

void TestScatterChart::autoFitCoversNegativeValues()
{
    md::ScatterChart chart;
    chart.setSeries({
        makeSeries(QStringLiteral("mixed"),
                   {{-4.0, -3.0}, {-1.0, 2.5}, {3.0, -1.5}, {5.0, 4.0}}),
    });
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    verifyPointsInsidePlot(chart);

    const QPointF zero = chart.math()->toPixel(QPointF(0.0, 0.0));
    const QPointF lowest = chart.math()->toPixel(QPointF(-4.0, -3.0));
    QVERIFY2(lowest.y() > zero.y(),
             "the negative minimum must map below the zero line");
}

void TestScatterChart::hoverSetsIndicesAndLeaveClearsThem()
{
    md::ScatterChart chart;
    chart.setSeries({
        makeSeries(QStringLiteral("alpha"), {{0.0, 0.0}, {1.0, 2.0}, {2.0, 1.0}}),
    });
    prepare(chart);

    const QImage before = renderWidget(chart);
    QVERIFY(!before.isNull());

    const QPointF position = chart.math()->toPixel(chart.series().at(0).points.at(2));
    QMouseEvent move(QEvent::MouseMove, position, chart.mapToGlobal(position.toPoint()),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&chart, &move);

    QCOMPARE(chart.hoveredSeries(), 0);
    QCOMPARE(chart.hoveredPoint(), 2);

    const QImage hovered = renderWidget(chart);
    QVERIFY(!hovered.isNull());
    QVERIFY2(before != hovered, "the hover halo must repaint the chart");

    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(&chart, &leave);

    QCOMPARE(chart.hoveredSeries(), -1);
    QCOMPARE(chart.hoveredPoint(), -1);

    const QImage cleared = renderWidget(chart);
    QVERIFY(!cleared.isNull());
    QVERIFY2(cleared != hovered, "leaving must repaint without the hover halo");
}

void TestScatterChart::hitTestFindsNearPointAndRejectsFarPoint()
{
    md::ScatterChart chart;
    chart.setSeries({
        makeSeries(QStringLiteral("alpha"), {{0.0, 0.0}, {1.0, 2.0}, {2.0, 1.0}}),
        makeSeries(QStringLiteral("beta"), {{0.5, -1.0}, {1.5, 3.0}}),
    });
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    const QPointF target = chart.series().at(1).points.at(1);
    const QPointF pixel = chart.math()->toPixel(target);

    int seriesIndex = -1;
    int pointIndex = -1;
    QVERIFY2(chart.hitTest(pixel, &seriesIndex, &pointIndex),
             "hitTest must find the sample under its own pixel");
    QCOMPARE(seriesIndex, 1);
    QCOMPARE(pointIndex, 1);

    seriesIndex = -1;
    pointIndex = -1;
    QVERIFY2(!chart.hitTest(QPointF(-5000.0, -5000.0), &seriesIndex, &pointIndex),
             "hitTest must reject a pixel far away from every sample");
}

void TestScatterChart::emptySeriesPaintsWithoutCrashing()
{
    md::ScatterChart chart;
    prepare(chart);

    QVERIFY(chart.series().isEmpty());

    const QImage empty = renderWidget(chart);
    QVERIFY(!empty.isNull());

    chart.setSeries({});
    const QImage stillEmpty = renderWidget(chart);
    QVERIFY(!stillEmpty.isNull());
}

void TestScatterChart::throwingPaintPlotDoesNotCrash()
{
    ThrowingScatterChart chart;
    chart.setSeries({
        makeSeries(QStringLiteral("alpha"), {{0.0, 0.0}, {1.0, 1.0}}),
    });
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY2(!image.isNull(), "a throwing paintPlot must be caught, not unwind into Qt");
}

QTEST_MAIN(TestScatterChart)
#include "tst_scatter_chart.moc"
