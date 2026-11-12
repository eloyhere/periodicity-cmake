// Tests for the BoxPlotChart component.
//
// The suite is offscreen and deterministic: the application theme is pinned to
// the baseline light scheme, the chart is sized and shown, and it is rendered
// into a transparent QImage.
//
// The assertions cover the statistical contract (type-7 quartiles for 1..10,
// the 1.5 * IQR outlier fence), the orientation contract (the box thickness
// moves from the category axis to the value axis when the chart is horizontal)
// and the interaction contract (boxAt() hits the painted box, misses far away,
// and hovering repaints). Empty data and a throwing paintPlot() are the two
// failure surfaces: neither may unwind into Qt.

#include <md/components/box_plot_chart.h>
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
    for (qsizetype index = 0; index < values.size(); ++index) {
        series.points.append(
            QPointF(static_cast<qreal>(index), values.at(index)));
    }
    return series;
}

QVector<qreal> oneToTen()
{
    return {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0};
}

class ThrowingBoxPlotChart : public md::BoxPlotChart
{
public:
    ThrowingBoxPlotChart() = default;

protected:
    void paintPlot(QPainter&, const QRectF&) override
    {
        throw std::runtime_error("intentional BoxPlotChart paint failure");
    }
};

} // namespace

class TestBoxPlotChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void defaultsAndLegendOptOut();
    void quartilesUseLinearInterpolation();
    void outlierBeyondFenceIsDetected();
    void horizontalSwapsTheAxes();
    void boxAtHitsAndMisses();
    void hoverHighlightsAndLeaveClears();
    void hidingOutliersChangesPixels();
    void emptyDataPaintsWithoutCrashing();
    void throwingPaintPlotDoesNotCrash();
};

void TestBoxPlotChart::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
    QApplication::setPalette(md::Theme::baselineLight().toPalette());
}

void TestBoxPlotChart::defaultsAndLegendOptOut()
{
    md::BoxPlotChart chart;

    QVERIFY(!chart.isHorizontal());
    QVERIFY(chart.showsOutliers());
    QCOMPARE(chart.boxWidthRatio(), 0.6);
    QCOMPARE(chart.hoveredBox(), -1);
    QVERIFY(chart.summaries().isEmpty());

    // ChartBase owns the legend; the component must leave the opt-out intact.
    chart.setLegendVisible(false);
    QVERIFY(!chart.isLegendVisible());
    QVERIFY(!chart.isHorizontal());

    chart.setBoxWidthRatio(2.0);
    QCOMPARE(chart.boxWidthRatio(), 0.9);
    chart.setBoxWidthRatio(-1.0);
    QCOMPARE(chart.boxWidthRatio(), 0.1);
}

void TestBoxPlotChart::quartilesUseLinearInterpolation()
{
    md::BoxPlotChart chart;
    chart.setSeries({makeSeries(QStringLiteral("one to ten"), oneToTen())});

    const md::BoxPlotChart::BoxSummary summary = chart.summaryAt(0);
    QVERIFY2(summary.valid, "a series with ten values must produce a box");
    QCOMPARE(summary.minimum, 1.0);
    QCOMPARE(summary.maximum, 10.0);

    // Type 7 for n = 10: h(Q1) = 2.25, h(median) = 4.5, h(Q3) = 6.75.
    QVERIFY2(std::abs(summary.q1 - 3.25) < 1e-9, "Q1 must be 3.25");
    QVERIFY2(std::abs(summary.median - 5.5) < 1e-9, "the median must be 5.5");
    QVERIFY2(std::abs(summary.q3 - 7.75) < 1e-9, "Q3 must be 7.75");

    QCOMPARE(summary.lowerWhisker, 1.0);
    QCOMPARE(summary.upperWhisker, 10.0);
    QVERIFY(summary.outliers.isEmpty());

    QCOMPARE(chart.summaries().size(), qsizetype(1));
    QVERIFY(!chart.summaryAt(-1).valid);
    QVERIFY(!chart.summaryAt(1).valid);
}

void TestBoxPlotChart::outlierBeyondFenceIsDetected()
{
    QVector<qreal> values = oneToTen();
    values.append(100.0);

    md::BoxPlotChart chart;
    chart.setSeries({makeSeries(QStringLiteral("with outlier"), values)});

    const md::BoxPlotChart::BoxSummary summary = chart.summaryAt(0);
    QVERIFY(summary.valid);
    QCOMPARE(summary.outliers.size(), qsizetype(1));
    QCOMPARE(summary.outliers.first(), 100.0);
    QVERIFY2(summary.upperWhisker <= 10.0 + 1e-9,
             "the upper whisker must stop at the largest non-outlier");
    QVERIFY(summary.upperWhisker < 100.0);
}

void TestBoxPlotChart::horizontalSwapsTheAxes()
{
    md::BoxPlotChart chart;
    chart.setSeries({makeSeries(QStringLiteral("wide"), oneToTen())});
    chart.setBoxWidthRatio(0.8);
    prepare(chart);

    QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    const QRectF vertical = chart.boxRectAt(0);
    QVERIFY2(vertical.isValid(), "the vertical box must have a rectangle");
    QVERIFY2(vertical.width() > vertical.height(),
             "a vertical box is wider (category slot) than the Q1..Q3 span");

    chart.setHorizontal(true);
    QVERIFY(chart.isHorizontal());

    image = renderWidget(chart);
    QVERIFY(!image.isNull());

    const QRectF horizontal = chart.boxRectAt(0);
    QVERIFY2(horizontal.isValid(), "the horizontal box must have a rectangle");
    QVERIFY2(horizontal.width() < horizontal.height(),
             "a horizontal box is taller (category slot) than the Q1..Q3 span");
    QCOMPARE(chart.boxAt(horizontal.center()), 0);
}

void TestBoxPlotChart::boxAtHitsAndMisses()
{
    md::BoxPlotChart chart;
    chart.setSeries({
        makeSeries(QStringLiteral("low"), oneToTen()),
        makeSeries(QStringLiteral("high"), {20.0, 21.0, 22.0, 23.0, 24.0, 25.0,
                                            26.0, 27.0, 28.0, 29.0}),
    });
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    const QRectF first = chart.boxRectAt(0);
    const QRectF second = chart.boxRectAt(1);
    QVERIFY(first.isValid());
    QVERIFY(second.isValid());
    QCOMPARE(chart.boxAt(first.center()), 0);
    QCOMPARE(chart.boxAt(second.center()), 1);

    // Far outside the plot, and far outside the widget.
    QCOMPARE(chart.boxAt(QPointF(-5000.0, -5000.0)), -1);
    QCOMPARE(chart.boxAt(QPointF(chart.width() + 500.0, chart.height() + 500.0)), -1);
    QCOMPARE(chart.boxAt(QPointF(-1.0, -1.0)), -1);
}

void TestBoxPlotChart::hoverHighlightsAndLeaveClears()
{
    md::BoxPlotChart chart;
    chart.setSeries({makeSeries(QStringLiteral("hover"), oneToTen())});
    prepare(chart);

    const QImage before = renderWidget(chart);
    QVERIFY(!before.isNull());

    const QPointF center = chart.boxRectAt(0).center();
    QMouseEvent move(QEvent::MouseMove, center, chart.mapToGlobal(center.toPoint()),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&chart, &move);
    QCOMPARE(chart.hoveredBox(), 0);

    const QImage hovered = renderWidget(chart);
    QVERIFY2(before != hovered, "the hover highlight must change the painted pixels");

    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(&chart, &leave);
    QCOMPARE(chart.hoveredBox(), -1);

    const QImage cleared = renderWidget(chart);
    QVERIFY2(cleared != hovered, "leaving the chart must repaint without the highlight");
}

void TestBoxPlotChart::hidingOutliersChangesPixels()
{
    QVector<qreal> values = oneToTen();
    values.append(100.0);

    md::BoxPlotChart chart;
    chart.setSeries({makeSeries(QStringLiteral("outlier"), values)});
    prepare(chart);

    const QImage withDots = renderWidget(chart);
    QVERIFY(!withDots.isNull());

    chart.setShowOutliers(false);
    QVERIFY(!chart.showsOutliers());

    const QImage withoutDots = renderWidget(chart);
    QVERIFY2(withDots != withoutDots,
             "setShowOutliers(false) must remove the outlier dots");
}

void TestBoxPlotChart::emptyDataPaintsWithoutCrashing()
{
    md::BoxPlotChart chart;
    prepare(chart);

    QVERIFY(chart.summaries().isEmpty());
    QVERIFY(chart.boxRectAt(0).isEmpty());
    QCOMPARE(chart.boxAt(QPointF(10.0, 10.0)), -1);

    const QImage empty = renderWidget(chart);
    QVERIFY(!empty.isNull());

    chart.setSeries({});
    const QImage stillEmpty = renderWidget(chart);
    QVERIFY(!stillEmpty.isNull());
}

void TestBoxPlotChart::throwingPaintPlotDoesNotCrash()
{
    ThrowingBoxPlotChart chart;
    chart.setSeries({makeSeries(QStringLiteral("alpha"), oneToTen())});
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY2(!image.isNull(), "a throwing paintPlot must be caught, not unwind into Qt");
}

QTEST_MAIN(TestBoxPlotChart)
#include "tst_box_plot_chart.moc"
