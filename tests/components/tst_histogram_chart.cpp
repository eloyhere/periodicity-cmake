// Tests for the HistogramChart component.
//
// The suite is offscreen and deterministic: the application theme is pinned to
// the baseline light scheme, each chart is sized, shown (so Qt delivers the
// events a live component receives) and rendered into a transparent QImage.
//
// The binning assertions read the same Bin vector the paint path consumes:
//   1. 100 samples under the default Auto/Sturges rule produce 8 bins;
//   2. explicit bins cover [min, max] and count every finite sample once;
//   3. density bars integrate to 1;
//   4. cumulative bars never decrease;
//   5. Freedman–Diaconis falls back to Sturges for a zero IQR;
//   6. an empty chart paints its empty state and a throwing paintPlot() is
//      contained by the exception boundary.

#include <md/components/histogram_chart.h>
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

QVector<qreal> consecutiveSamples(int count)
{
    QVector<qreal> samples;
    samples.reserve(static_cast<qsizetype>(count));
    for (int index = 0; index < count; ++index)
        samples.append(static_cast<qreal>(index));
    return samples;
}

class ThrowingHistogramChart : public md::HistogramChart
{
public:
    ThrowingHistogramChart() = default;

protected:
    void paintPlot(QPainter&, const QRectF&) override
    {
        throw std::runtime_error("intentional HistogramChart paint failure");
    }
};

} // namespace

class TestHistogramChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void autoBinCountUsesSturges();
    void binRangesCoverTheSampleRange();
    void densityIntegratesToOne();
    void cumulativeIsMonotone();
    void freedmanDiaconisFallsBackWhenIqrIsZero();
    void seriesYValuesArePooled();
    void explicitBinCountIsClamped();
    void emptyDataPaintsWithoutCrashing();
    void throwingPaintPlotDoesNotCrash();
};

void TestHistogramChart::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
    QApplication::setPalette(md::Theme::baselineLight().toPalette());
}

void TestHistogramChart::autoBinCountUsesSturges()
{
    md::HistogramChart chart;
    chart.setSamples(consecutiveSamples(100));

    QCOMPARE(chart.binCount(), 0);
    QCOMPARE(chart.binMethod(), md::HistogramChart::BinMethod::Auto);
    QCOMPARE(chart.effectiveBinCount(), 8);
    QCOMPARE(chart.bins().size(), qsizetype(8));
}

void TestHistogramChart::binRangesCoverTheSampleRange()
{
    const QVector<qreal> samples{
        -7.5, -3.25, -1.0, 0.0, 0.5, 1.25, 2.0, 3.75, 4.5, 6.0, 8.25, 11.0,
        13.5, 17.25, 21.0,
    };

    md::HistogramChart chart;
    chart.setSamples(samples);
    chart.setBinCount(9);

    const QVector<md::HistogramChart::Bin> bins = chart.bins();
    QCOMPARE(bins.size(), qsizetype(9));

    const auto [minimum, maximum] = std::minmax_element(samples.cbegin(), samples.cend());
    QVERIFY2(bins.first().lower <= *minimum + 1e-9,
             "the first lower edge must not be above the sample minimum");
    QVERIFY2(bins.last().upper >= *maximum - 1e-9,
             "the last upper edge must not be below the sample maximum");

    qreal counted = 0.0;
    for (qsizetype index = 0; index < bins.size(); ++index) {
        const md::HistogramChart::Bin& bin = bins.at(index);
        QVERIFY2(bin.upper > bin.lower, "every bin must have a positive width");
        QVERIFY(bin.count >= 0.0);
        counted += bin.count;

        if (index > 0) {
            QVERIFY2(std::abs(bin.lower - bins.at(index - 1).upper) < 1e-9,
                     "bins must be contiguous");
        }
    }

    QCOMPARE(counted, static_cast<qreal>(samples.size()));
}

void TestHistogramChart::densityIntegratesToOne()
{
    md::HistogramChart chart;
    chart.setSamples(consecutiveSamples(100));
    chart.setDensity(true);

    qreal integral = 0.0;
    for (const md::HistogramChart::Bin& bin : chart.bins())
        integral += bin.value * (bin.upper - bin.lower);

    QVERIFY2(std::abs(integral - 1.0) < 1e-9,
             "density bars must integrate to one over the sample range");
}

void TestHistogramChart::cumulativeIsMonotone()
{
    md::HistogramChart chart;
    chart.setSamples(consecutiveSamples(100));
    chart.setCumulative(true);

    const QVector<md::HistogramChart::Bin> bins = chart.bins();
    QVERIFY(!bins.isEmpty());

    qreal previous = -1.0;
    for (const md::HistogramChart::Bin& bin : bins) {
        QVERIFY2(bin.value >= previous - 1e-12, "a cumulative histogram must not decrease");
        previous = bin.value;
    }

    QCOMPARE(bins.last().value, 100.0);
}

void TestHistogramChart::freedmanDiaconisFallsBackWhenIqrIsZero()
{
    md::HistogramChart chart;
    chart.setSamples({2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0});
    chart.setBinMethod(md::HistogramChart::BinMethod::FreedmanDiaconis);

    // Sturges for n = 10: ceil(log2(10)) + 1 = 5.
    QCOMPARE(chart.effectiveBinCount(), 5);
    QCOMPARE(chart.bins().size(), qsizetype(5));
}

void TestHistogramChart::seriesYValuesArePooled()
{
    md::ChartSeries alpha;
    alpha.name = QStringLiteral("alpha");
    alpha.points = {{0.0, 1.0}, {1.0, 2.0}, {2.0, 3.0}};

    md::ChartSeries beta;
    beta.name = QStringLiteral("beta");
    beta.points = {{0.0, 4.0}, {1.0, 5.0}, {2.0, 6.0}};

    md::HistogramChart chart;
    chart.setSeries({alpha, beta});

    QCOMPARE(chart.samples().size(), qsizetype(6));
    QCOMPARE(chart.series().size(), qsizetype(2));

    qreal counted = 0.0;
    for (const md::HistogramChart::Bin& bin : chart.bins())
        counted += bin.count;
    QCOMPARE(counted, 6.0);
}

void TestHistogramChart::explicitBinCountIsClamped()
{
    md::HistogramChart chart;
    chart.setSamples(consecutiveSamples(10));

    chart.setBinCount(1000);
    QCOMPARE(chart.binCount(), 64);
    QCOMPARE(chart.effectiveBinCount(), 64);
    QCOMPARE(chart.bins().size(), qsizetype(64));

    chart.setBinCount(-4);
    QCOMPARE(chart.binCount(), 0);
    QCOMPARE(chart.effectiveBinCount(), 5);
}

void TestHistogramChart::emptyDataPaintsWithoutCrashing()
{
    md::HistogramChart chart;
    prepare(chart);

    QVERIFY(chart.samples().isEmpty());
    QVERIFY(chart.bins().isEmpty());
    QCOMPARE(chart.effectiveBinCount(), 0);

    const QImage empty = renderWidget(chart);
    QVERIFY(!empty.isNull());

    chart.setSamples({});
    const QImage stillEmpty = renderWidget(chart);
    QVERIFY(!stillEmpty.isNull());
}

void TestHistogramChart::throwingPaintPlotDoesNotCrash()
{
    ThrowingHistogramChart chart;
    chart.setSamples(consecutiveSamples(20));
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY2(!image.isNull(), "a throwing paintPlot must be caught, not unwind into Qt");
}

QTEST_MAIN(TestHistogramChart)
#include "tst_histogram_chart.moc"
