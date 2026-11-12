// Tests for the LineChart component.
//
// The suite is offscreen and deterministic: the application theme is pinned to
// the baseline light scheme with reduced motion (so auto-fit snaps instead of
// animating), each chart is given an explicit geometry and then rendered into
// an explicitly transparent QImage. Points are located through the chart's own
// MathematicsCoordination, which is the same transform paintPlot() uses, so the
// "inside plotRect" assertions prove the auto-fit bounds rather than a
// coincidence of the test's own arithmetic.
//
// The last two cases cover the two failure surfaces the component owns: an
// empty series must paint its empty state instead of dividing by a zero range,
// and a throwing paintPlot() must be contained by the exception boundary.

#include <md/components/line_chart.h>
#include <md/theme/theme.h>

#include <QApplication>
#include <QImage>
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

// Every finite sample of every series must map inside the plot. The tolerance is
// a quarter pixel: the transform rounds to device pixels and the bounds may be
// padded by the coordination, but neither may push a sample out of the area.
void verifyPointsInsidePlot(md::LineChart& chart)
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

// A chart that fails in the one hook a subclass is allowed to replace.
class ThrowingLineChart : public md::LineChart
{
public:
    ThrowingLineChart() = default;

protected:
    void paintPlot(QPainter&, const QRectF&) override
    {
        throw std::runtime_error("intentional LineChart paint failure");
    }
};

} // namespace

class TestLineChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void constructsWithTwoSeriesAndDefaults();
    void pointMapsInsidePlotRect();
    void autoFitCoversNegativeValues();
    void smoothChangesPixels();
    void emptySeriesPaintsWithoutCrashing();
    void throwingPaintPlotDoesNotCrash();
};

void TestLineChart::initTestCase()
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

void TestLineChart::constructsWithTwoSeriesAndDefaults()
{
    md::LineChart chart;
    chart.setSeries({
        makeSeries(QStringLiteral("alpha"), {{0.0, 0.0}, {1.0, 2.0}, {2.0, 1.0}}),
        makeSeries(QStringLiteral("beta"), {{0.0, 3.0}, {2.0, 0.0}}),
    });

    QCOMPARE(chart.series().size(), qsizetype(2));
    QCOMPARE(chart.series().at(0).name, QStringLiteral("alpha"));
    QCOMPARE(chart.series().at(0).points.size(), qsizetype(3));
    QCOMPARE(chart.series().at(1).name, QStringLiteral("beta"));
    QCOMPARE(chart.series().at(1).points.size(), qsizetype(2));

    QVERIFY(!chart.isSmooth());
    QVERIFY(!chart.showsMarkers());
    QCOMPARE(chart.lineWidth(), 2.0);

    // The chart palette is part of the ChartBase contract and must be usable
    // without any series carrying an explicit colour.
    QVERIFY(chart.chartPalette().size() >= 8);
}

void TestLineChart::pointMapsInsidePlotRect()
{
    md::LineChart chart;
    chart.setSeries({
        makeSeries(QStringLiteral("alpha"), {{0.0, 0.0}, {1.0, 2.0}, {2.0, 1.0}}),
        makeSeries(QStringLiteral("beta"), {{0.5, -1.0}, {1.5, 3.0}, {2.5, 0.5}}),
    });
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    verifyPointsInsidePlot(chart);
}

void TestLineChart::autoFitCoversNegativeValues()
{
    md::LineChart chart;
    chart.setSeries({
        makeSeries(QStringLiteral("mixed"),
                   {{-4.0, -3.0}, {-1.0, 2.5}, {3.0, -1.5}, {5.0, 4.0}}),
    });
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    verifyPointsInsidePlot(chart);

    // A zero-clamped y axis would put the negative sample on or above zero
    // instead of below it (pixel y grows downwards).
    const QPointF zero = chart.math()->toPixel(QPointF(0.0, 0.0));
    const QPointF lowest = chart.math()->toPixel(QPointF(-4.0, -3.0));
    QVERIFY2(lowest.y() > zero.y(),
             "the negative minimum must map below the zero line");
}

void TestLineChart::smoothChangesPixels()
{
    md::LineChart chart;
    chart.setSeries({
        makeSeries(QStringLiteral("wave"),
                   {{0.0, 0.0}, {1.0, 2.0}, {2.0, 0.0}, {3.0, 2.0}, {4.0, 0.0}}),
    });
    prepare(chart);

    const QImage straight = renderWidget(chart);
    QVERIFY(!straight.isNull());

    chart.setSmooth(true);
    QVERIFY(chart.isSmooth());

    const QImage smoothed = renderWidget(chart);
    QVERIFY(!smoothed.isNull());

    // Catmull-Rom rounds the corners, so at least one pixel must differ.
    QVERIFY2(straight != smoothed, "setSmooth(true) must change the painted pixels");
}

void TestLineChart::emptySeriesPaintsWithoutCrashing()
{
    md::LineChart chart;
    prepare(chart);

    QVERIFY(chart.series().isEmpty());

    const QImage empty = renderWidget(chart);
    QVERIFY(!empty.isNull());

    // An explicitly empty series list takes the same path.
    chart.setSeries({});
    const QImage stillEmpty = renderWidget(chart);
    QVERIFY(!stillEmpty.isNull());
}

void TestLineChart::throwingPaintPlotDoesNotCrash()
{
    ThrowingLineChart chart;
    chart.setSeries({
        makeSeries(QStringLiteral("alpha"), {{0.0, 0.0}, {1.0, 1.0}}),
    });
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY2(!image.isNull(), "a throwing paintPlot must be caught, not unwind into Qt");
}

QTEST_MAIN(TestLineChart)
#include "tst_line_chart.moc"
