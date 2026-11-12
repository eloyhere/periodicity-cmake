// Tests for the RadarChart component.
//
// The suite is offscreen and deterministic: the application theme is pinned to
// the baseline light scheme, each chart is sized and shown, and it is rendered
// into an explicitly transparent QImage. The geometry assertions use the
// chart's own polygonForSeries(), which is the exact polygon paintPlot() draws,
// so "one vertex per axis" and the range clamping are tested against the
// production geometry instead of a reimplementation in the test.
//
// The ring-count case only requires that the two renders differ: the number of
// rings is a visual property, and a pixel comparison is the least brittle way
// to prove the setter reaches the paint path.
//
// The final cases cover the empty state and a throwing paintPlot().

#include <md/components/radar_chart.h>
#include <md/theme/theme.h>

#include <QApplication>
#include <QCoreApplication>
#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QRegion>
#include <QTest>
#include <QWidget>

#include <cmath>
#include <limits>
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

void prepare(QWidget& widget)
{
    widget.resize(kChartWidth, kChartHeight);
    // Pixel assertions need the settled frame: the reveal animation stays off here.
    if (auto* chartBase = qobject_cast<md::ChartBase*>(&widget))
        chartBase->setAnimated(false);
    widget.show();
    QCoreApplication::processEvents();
}

md::ChartSeries makeSeries(const QString& name, const QVector<qreal>& values)
{
    md::ChartSeries series;
    series.name = name;
    series.points.reserve(values.size());

    for (qsizetype i = 0; i < values.size(); ++i)
        series.points.append(QPointF(static_cast<qreal>(i), values.at(i)));

    return series;
}

class ThrowingRadarChart : public md::RadarChart
{
public:
    ThrowingRadarChart() = default;

protected:
    void paintPlot(QPainter&, const QRectF&) override
    {
        throw std::runtime_error("intentional RadarChart paint failure");
    }
};

} // namespace

class TestRadarChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void constructsWithAxesAndDefaults();
    void polygonHasOneVertexPerAxis();
    void rangeClampsOutOfBoundsValues();
    void ringCountChangesPixels();
    void fillOpacityIsClamped();
    void emptySeriesPaints();
    void throwingPaintPlotDoesNotCrash();
};

void TestRadarChart::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
    QApplication::setPalette(md::Theme::baselineLight().toPalette());
}

void TestRadarChart::constructsWithAxesAndDefaults()
{
    md::RadarChart chart;

    QVERIFY(chart.axes().isEmpty());
    QCOMPARE(chart.rangeMinimum(), 0.0);
    QCOMPARE(chart.rangeMaximum(), 100.0);
    QCOMPARE(chart.ringCount(), 4);
    QCOMPARE(chart.fillOpacity(), 0.22);
    QVERIFY(!chart.isAxesVisible());

    chart.setAxes({QStringLiteral("Speed"), QStringLiteral("Power"),
                   QStringLiteral("Range"), QStringLiteral("Armour"),
                   QStringLiteral("Agility")});
    QCOMPARE(chart.axes().size(), qsizetype(5));
    QCOMPARE(chart.axes().at(0), QStringLiteral("Speed"));

    chart.setRange(-10.0, 40.0);
    QCOMPARE(chart.rangeMinimum(), -10.0);
    QCOMPARE(chart.rangeMaximum(), 40.0);

    // Reversed endpoints are normalised, equal or non-finite ranges ignored.
    chart.setRange(40.0, -10.0);
    QCOMPARE(chart.rangeMinimum(), -10.0);
    QCOMPARE(chart.rangeMaximum(), 40.0);
    chart.setRange(5.0, 5.0);
    QCOMPARE(chart.rangeMinimum(), -10.0);
    chart.setRange(std::numeric_limits<qreal>::quiet_NaN(), 10.0);
    QCOMPARE(chart.rangeMaximum(), 40.0);

    QVERIFY(chart.chartPalette().size() >= 8);
}

void TestRadarChart::polygonHasOneVertexPerAxis()
{
    md::RadarChart chart;
    chart.setAxes({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"),
                   QStringLiteral("D"), QStringLiteral("E")});
    chart.setRange(0.0, 100.0);
    chart.setSeries({
        makeSeries(QStringLiteral("alpha"), {80.0, 60.0, 70.0, 50.0, 90.0}),
        makeSeries(QStringLiteral("beta"), {20.0, 90.0, 40.0, 70.0, 30.0}),
    });
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    QCOMPARE(chart.polygonForSeries(0).size(), qsizetype(5));
    QCOMPARE(chart.polygonForSeries(1).size(), qsizetype(5));
    QVERIFY(chart.polygonForSeries(-1).isEmpty());
    QVERIFY(chart.polygonForSeries(2).isEmpty());

    // Every vertex lies on, or inside, the outer ring.
    const QPointF centre = chart.plotRect().center();
    const qreal outerRadius = std::min(chart.plotRect().width(),
                                       chart.plotRect().height()) * 0.5;
    for (const QPointF& vertex : chart.polygonForSeries(0))
        QVERIFY(std::hypot(vertex.x() - centre.x(), vertex.y() - centre.y())
                <= outerRadius);
}

void TestRadarChart::rangeClampsOutOfBoundsValues()
{
    md::RadarChart chart;
    chart.setAxes({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"),
                   QStringLiteral("D"), QStringLiteral("E")});
    chart.setRange(0.0, 100.0);
    chart.setSeries({
        makeSeries(QStringLiteral("clamped"),
                   {200.0, 100.0, -50.0, 0.0, 50.0}),
    });
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    const QVector<QPointF> polygon = chart.polygonForSeries(0);
    QCOMPARE(polygon.size(), qsizetype(5));

    const QPointF centre = chart.plotRect().center();
    const auto distance = [&](int index) {
        const QPointF vertex = polygon.at(index);
        return std::hypot(vertex.x() - centre.x(), vertex.y() - centre.y());
    };

    // 200 and 100 both clamp to the maximum, -50 and 0 both to the centre.
    QVERIFY(std::abs(distance(0) - distance(1)) < 1e-6);
    QVERIFY(distance(0) > distance(4));
    QVERIFY(distance(4) > distance(2));
    QVERIFY(distance(2) < 1e-6);
    QVERIFY(distance(3) < 1e-6);
}

void TestRadarChart::ringCountChangesPixels()
{
    md::RadarChart chart;
    chart.setAxes({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"),
                   QStringLiteral("D")});
    chart.setSeries({makeSeries(QStringLiteral("alpha"), {40.0, 70.0, 55.0, 90.0})});
    prepare(chart);

    chart.setRingCount(2);
    QCOMPARE(chart.ringCount(), 2);
    const QImage fewRings = renderWidget(chart);
    QVERIFY(!fewRings.isNull());

    chart.setRingCount(8);
    QCOMPARE(chart.ringCount(), 8);
    const QImage manyRings = renderWidget(chart);
    QVERIFY(!manyRings.isNull());

    QVERIFY2(fewRings != manyRings, "the ring count must change the rendered pixels");

    chart.setRingCount(0);
    QCOMPARE(chart.ringCount(), 1);
    chart.setRingCount(100);
    QCOMPARE(chart.ringCount(), 12);
}

void TestRadarChart::fillOpacityIsClamped()
{
    md::RadarChart chart;

    chart.setFillOpacity(0.5);
    QCOMPARE(chart.fillOpacity(), 0.5);

    chart.setFillOpacity(-1.0);
    QCOMPARE(chart.fillOpacity(), 0.0);

    chart.setFillOpacity(2.0);
    QCOMPARE(chart.fillOpacity(), 1.0);

    chart.setFillOpacity(std::numeric_limits<qreal>::quiet_NaN());
    QCOMPARE(chart.fillOpacity(), 0.0);
}

void TestRadarChart::emptySeriesPaints()
{
    md::RadarChart chart;
    chart.setAxes({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});
    prepare(chart);

    QVERIFY(chart.series().isEmpty());
    QVERIFY(!renderWidget(chart).isNull());

    // All-NaN values leave the base's hasData() false as well.
    const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
    chart.setSeries({makeSeries(QStringLiteral("empty"), {nan, nan, nan})});
    QVERIFY(!renderWidget(chart).isNull());

    // Fewer than three axes cannot form a polygon even with finite values.
    chart.setAxes({QStringLiteral("A"), QStringLiteral("B")});
    chart.setSeries({makeSeries(QStringLiteral("line"), {1.0, 2.0})});
    QVERIFY(!renderWidget(chart).isNull());
}

void TestRadarChart::throwingPaintPlotDoesNotCrash()
{
    ThrowingRadarChart chart;
    chart.setAxes({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});
    chart.setSeries({makeSeries(QStringLiteral("alpha"), {40.0, 70.0, 55.0})});
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY2(!image.isNull(), "a throwing paintPlot must be caught, not unwind into Qt");
}

QTEST_MAIN(TestRadarChart)
#include "tst_radar_chart.moc"
