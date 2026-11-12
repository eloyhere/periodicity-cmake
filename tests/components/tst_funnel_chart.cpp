// Tests for the FunnelChart component.
//
// The suite is offscreen and deterministic: the application theme is pinned to
// the baseline light scheme, each chart is sized and shown, and it is rendered
// into an explicitly transparent QImage. Stage geometry comes from the chart's
// own stageRect(), so the hit and orientation assertions use the same polygons
// stageAt() tests instead of a test-local reconstruction.
//
// The orientation case compares the first stage's centre against the plot
// centre: it is above the centre vertically and to the left of it horizontally
// (to the right under Qt::RightToLeft). That proves the layout really swapped
// axes, not merely that the pixels changed.
//
// The final cases cover empty data and a throwing paintPlot().

#include <md/components/funnel_chart.h>
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

#include <algorithm>
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

class ThrowingFunnelChart : public md::FunnelChart
{
public:
    ThrowingFunnelChart() = default;

protected:
    void paintPlot(QPainter&, const QRectF&) override
    {
        throw std::runtime_error("intentional FunnelChart paint failure");
    }
};

} // namespace

class TestFunnelChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void constructsWithStages();
    void firstStageIsOneHundredPercent();
    void horizontalSwapsAxes();
    void stageAtHitsAndMisses();
    void percentagesAndGapChangePixels();
    void emptyDataPaints();
    void throwingPaintPlotDoesNotCrash();
};

void TestFunnelChart::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
    QApplication::setPalette(md::Theme::baselineLight().toPalette());
}

void TestFunnelChart::constructsWithStages()
{
    md::FunnelChart chart;

    QVERIFY(chart.stageLabels().isEmpty());
    QCOMPARE(chart.gap(), 4.0);
    QVERIFY(chart.showsPercentages());
    QVERIFY(!chart.isHorizontal());
    QCOMPARE(chart.stageCount(), 0);
    QCOMPARE(chart.hoveredStage(), -1);
    QVERIFY(!chart.isAxesVisible());

    chart.setStageLabels({QStringLiteral("Visit"), QStringLiteral("Signup"),
                          QStringLiteral("Purchase")});
    QCOMPARE(chart.stageLabels().size(), qsizetype(3));
    QCOMPARE(chart.stageLabels().at(1), QStringLiteral("Signup"));

    chart.setSeries({makeSeries(QStringLiteral("funnel"), {1000.0, 500.0, 100.0})});
    QCOMPARE(chart.stageCount(), 3);

    chart.setGap(6.0);
    QCOMPARE(chart.gap(), 6.0);
    chart.setGap(-2.0);
    QCOMPARE(chart.gap(), 0.0);
    chart.setGap(std::numeric_limits<qreal>::quiet_NaN());
    QCOMPARE(chart.gap(), 0.0);

    chart.setShowPercentages(false);
    QVERIFY(!chart.showsPercentages());

    chart.setHorizontal(true);
    QVERIFY(chart.isHorizontal());
}

void TestFunnelChart::firstStageIsOneHundredPercent()
{
    md::FunnelChart chart;
    chart.setStageLabels({QStringLiteral("Visit"), QStringLiteral("Signup"),
                          QStringLiteral("Purchase")});
    chart.setSeries({makeSeries(QStringLiteral("funnel"), {1000.0, 500.0, 100.0})});
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    QCOMPARE(chart.stagePercentage(0), 100.0);
    QCOMPARE(chart.stagePercentage(1), 50.0);
    QVERIFY(std::abs(chart.stagePercentage(2) - 10.0) < 1e-9);
    QCOMPARE(chart.stagePercentage(-1), 0.0);
    QCOMPARE(chart.stagePercentage(3), 0.0);

    for (int stage = 0; stage < chart.stageCount(); ++stage) {
        const QRectF rect = chart.stageRect(stage);
        QVERIFY2(!rect.isEmpty(), "every stage must have geometry");
        QVERIFY2(chart.stageAt(rect.center()) == stage,
                 "a stage centre must hit its own stage");
    }

    // A decreasing series narrows as it descends.
    const QRectF first = chart.stageRect(0);
    const QRectF last = chart.stageRect(2);
    QVERIFY(first.width() > last.width());
}

void TestFunnelChart::horizontalSwapsAxes()
{
    md::FunnelChart chart;
    chart.setStageLabels({QStringLiteral("Visit"), QStringLiteral("Signup"),
                          QStringLiteral("Purchase")});
    chart.setSeries({makeSeries(QStringLiteral("funnel"), {1000.0, 500.0, 100.0})});
    prepare(chart);

    const QImage vertical = renderWidget(chart);
    QVERIFY(!vertical.isNull());

    const QRectF verticalFirst = chart.stageRect(0);
    const QRectF plot = chart.plotRect();
    QVERIFY2(verticalFirst.center().y() < plot.center().y(),
             "vertically the first stage must sit above the plot centre");

    chart.setHorizontal(true);
    const QImage horizontal = renderWidget(chart);
    QVERIFY(!horizontal.isNull());
    QVERIFY2(vertical != horizontal, "the orientation must change the rendered pixels");

    const QRectF horizontalFirst = chart.stageRect(0);
    QVERIFY2(horizontalFirst.center().x() < plot.center().x(),
             "horizontally the first stage must start on the reading-start side");
    QCOMPARE(chart.stageAt(horizontalFirst.center()), 0);

    // The same layout mirrors under RightToLeft.
    chart.setLayoutDirection(Qt::RightToLeft);
    const QRectF mirroredFirst = chart.stageRect(0);
    QVERIFY2(mirroredFirst.center().x() > plot.center().x(),
             "the horizontal funnel must mirror under RightToLeft");
    QCOMPARE(chart.stageAt(mirroredFirst.center()), 0);
}

void TestFunnelChart::stageAtHitsAndMisses()
{
    md::FunnelChart chart;
    chart.setStageLabels({QStringLiteral("Visit"), QStringLiteral("Signup"),
                          QStringLiteral("Purchase")});
    chart.setSeries({makeSeries(QStringLiteral("funnel"), {1000.0, 500.0, 100.0})});
    chart.setGap(8.0);
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    for (int stage = 0; stage < chart.stageCount(); ++stage)
        QCOMPARE(chart.stageAt(chart.stageRect(stage).center()), stage);

    QCOMPARE(chart.stageAt(QPointF(-5000.0, -5000.0)), -1);

    // The middle of the gap between stage 0 and stage 1 is not a stage.
    const QRectF first = chart.stageRect(0);
    QCOMPARE(chart.stageAt(QPointF(chart.plotRect().center().x(),
                                   first.bottom() + chart.gap() * 0.5)),
             -1);
}

void TestFunnelChart::percentagesAndGapChangePixels()
{
    md::FunnelChart chart;
    chart.setStageLabels({QStringLiteral("Visit"), QStringLiteral("Signup"),
                          QStringLiteral("Purchase")});
    chart.setSeries({makeSeries(QStringLiteral("funnel"), {1000.0, 500.0, 100.0})});
    prepare(chart);

    const QImage withPercentages = renderWidget(chart);
    QVERIFY(!withPercentages.isNull());

    chart.setShowPercentages(false);
    const QImage withoutPercentages = renderWidget(chart);
    QVERIFY2(withPercentages != withoutPercentages,
             "the percentage text must reach the paint path");

    chart.setGap(0.0);
    const QImage withoutGap = renderWidget(chart);
    QVERIFY2(withoutPercentages != withoutGap, "the gap must change the stage geometry");
}

void TestFunnelChart::emptyDataPaints()
{
    md::FunnelChart chart;
    chart.setStageLabels({QStringLiteral("Visit"), QStringLiteral("Signup")});
    prepare(chart);

    QCOMPARE(chart.stageCount(), 0);
    QVERIFY(!renderWidget(chart).isNull());

    // No finite value means the base paints its empty state.
    const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
    chart.setSeries({makeSeries(QStringLiteral("empty"), {nan, nan})});
    QVERIFY(!renderWidget(chart).isNull());

    // A zero reference is finite data but every width collapses to zero; the
    // chart must still paint instead of dividing by a zero reference.
    chart.setSeries({makeSeries(QStringLiteral("zero"), {0.0, 0.0})});
    QCOMPARE(chart.stagePercentage(0), 0.0);
    QVERIFY(!renderWidget(chart).isNull());
}

void TestFunnelChart::throwingPaintPlotDoesNotCrash()
{
    ThrowingFunnelChart chart;
    chart.setStageLabels({QStringLiteral("Visit"), QStringLiteral("Signup")});
    chart.setSeries({makeSeries(QStringLiteral("funnel"), {100.0, 50.0})});
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY2(!image.isNull(), "a throwing paintPlot must be caught, not unwind into Qt");
}

QTEST_MAIN(TestFunnelChart)
#include "tst_funnel_chart.moc"
