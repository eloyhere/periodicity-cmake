// Tests for the CoherenceChart component.
//
// The chart is exercised offscreen with an explicit geometry, like the other
// component suites. The cases pin the parts of the contract a caller depends
// on: the 0..1 axis, the clamping and sorting done by setCoherence(), the
// significance band and its label, the linear and logarithmic frequency ticks,
// hit-testing, the empty state, and the reveal animation in both its animated
// and reduced-motion forms.

#include <md/components/coherence_chart.h>
#include <md/core/measure_spec.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>
#include <md/tokens/motion.h>

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QRegion>
#include <QTest>
#include <QWidget>

#include <cmath>
#include <limits>
#include <vector>

namespace {

constexpr int kChartWidth = 640;
constexpr int kChartHeight = 440;

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

int colourDistance(const QColor& left, const QColor& right)
{
    return qAbs(left.red() - right.red()) + qAbs(left.green() - right.green())
        + qAbs(left.blue() - right.blue()) + qAbs(left.alpha() - right.alpha());
}

bool hasColourNear(const QImage& image, const QPointF& logical, const QColor& colour,
                   int radius = 3)
{
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (colourDistance(pixelAt(image, logical + QPointF(dx, dy)), colour) <= 60)
                return true;
        }
    }
    return false;
}

int pixelsDifferingFrom(const QImage& image, const QColor& background)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (colourDistance(image.pixelColor(x, y), background) > 24)
                ++count;
        }
    }
    return count;
}

void prepare(QWidget& widget)
{
    widget.resize(kChartWidth, kChartHeight);
}

// Three decades so the logarithmic axis has a real decade tick set.
std::vector<double> makeFrequencies()
{
    return {100.0, 500.0, 1000.0, 5000.0, 10000.0, 20000.0};
}

std::vector<double> makeCoherence()
{
    return {0.10, 0.80, 0.55, 0.90, 0.30, 0.70};
}

} // namespace

class TestCoherenceChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void constructionAndMeasurement();
    void coherenceIsCleanedAndClamped();
    void fixedYAxisTicks();
    void logFrequencyTicks();
    void significanceBandIsPainted();
    void dataMarksUseSeriesColour();
    void hitTestMapsPixels();
    void emptyStatePaintsLabel();
    void revealReachesOne();
    void reducedMotionSnaps();
};

void TestCoherenceChart::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestCoherenceChart::constructionAndMeasurement()
{
    md::CoherenceChart chart;
    chart.setCoherence(makeFrequencies(), makeCoherence());
    prepare(chart);

    QCOMPARE(chart.sampleCount(), 6);
    QCOMPARE(chart.threshold(), 0.5);
    QCOMPARE(chart.frequencyAxis(), md::CoherenceChart::FrequencyAxis::Linear);
    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(chart.xAxisTitle().contains(QStringLiteral("Hz")));
    QCOMPARE(chart.yAxisTitle(), QStringLiteral("Coherence"));

    chart.measure(md::MeasureSpec::unspecified(), md::MeasureSpec::unspecified());
    QVERIFY(chart.measuredSize().width() > 0.0);
    QVERIFY(chart.measuredSize().height() > 0.0);

    chart.setFrequencyAxis(md::CoherenceChart::FrequencyAxis::Logarithmic);
    QCOMPARE(chart.frequencyAxis(), md::CoherenceChart::FrequencyAxis::Logarithmic);

    chart.setThreshold(0.75);
    QCOMPARE(chart.threshold(), 0.75);
    QCOMPARE(chart.thresholdLabel(), QStringLiteral("0.75"));

    // The threshold is clamped into the axis range.
    chart.setThreshold(-1.0);
    QCOMPARE(chart.threshold(), 0.0);
    chart.setThreshold(2.0);
    QCOMPARE(chart.threshold(), 1.0);
}

void TestCoherenceChart::coherenceIsCleanedAndClamped()
{
    md::CoherenceChart chart;
    chart.setCoherence({2.0, 0.0, 1.0, 3.0}, {-0.5, 1.5, 0.5, std::nan("")});

    // The non-finite pair is dropped, the remaining values are clamped and the
    // samples are sorted by frequency.
    QCOMPARE(chart.sampleCount(), 3);
    QCOMPARE(chart.frequencyAt(0), 0.0);
    QCOMPARE(chart.coherenceAt(0), 1.0);
    QCOMPARE(chart.frequencyAt(1), 1.0);
    QCOMPARE(chart.coherenceAt(1), 0.5);
    QCOMPARE(chart.frequencyAt(2), 2.0);
    QCOMPARE(chart.coherenceAt(2), 0.0);

    QCOMPARE(chart.frequencyAt(-1), 0.0);
    QCOMPARE(chart.coherenceAt(100), 0.0);

    // Mismatched vector lengths are truncated to the shorter one.
    chart.setCoherence({10.0, 20.0, 30.0}, {0.1, 0.2});
    QCOMPARE(chart.sampleCount(), 2);
}

void TestCoherenceChart::fixedYAxisTicks()
{
    md::CoherenceChart chart;
    chart.setCoherence(makeFrequencies(), makeCoherence());
    prepare(chart);

    const QVector<double> ticks = chart.coherenceTicks();
    QCOMPARE(ticks.size(), qsizetype(6));
    QCOMPARE(ticks.first(), 0.0);
    QCOMPARE(ticks.last(), 1.0);
    for (int i = 1; i < ticks.size(); ++i)
        QVERIFY(std::abs((ticks.at(i) - ticks.at(i - 1)) - 0.2) < 1.0e-9);

    const QStringList labels = chart.coherenceTickLabels();
    QCOMPARE(labels.size(), ticks.size());
    QVERIFY(labels.contains(QStringLiteral("0.0")));
    QVERIFY(labels.contains(QStringLiteral("0.6")));
    QVERIFY(labels.contains(QStringLiteral("1.0")));
}

void TestCoherenceChart::logFrequencyTicks()
{
    md::CoherenceChart chart;
    chart.setCoherence(makeFrequencies(), makeCoherence());
    chart.setFrequencyAxis(md::CoherenceChart::FrequencyAxis::Logarithmic);
    prepare(chart);

    const QVector<double> ticks = chart.frequencyTicks();
    QVERIFY(ticks.contains(100.0));
    QVERIFY(ticks.contains(1000.0));
    QVERIFY(ticks.contains(10000.0));

    const QStringList labels = chart.frequencyTickLabels();
    QCOMPARE(labels.size(), ticks.size());
    QVERIFY(labels.contains(QStringLiteral("100 Hz")));
    QVERIFY(labels.contains(QStringLiteral("1 kHz")));
    QVERIFY(labels.contains(QStringLiteral("10 kHz")));

    const QRectF data = chart.dataRect();
    QVERIFY(!data.isEmpty());
    for (int index : {0, 2, 4}) {
        const QPointF point = chart.pointForIndex(index);
        QVERIFY(!point.isNull());
        QVERIFY(data.adjusted(-0.5, -0.5, 0.5, 0.5).contains(point));
    }
}

void TestCoherenceChart::significanceBandIsPainted()
{
    md::CoherenceChart chart;
    chart.setCoherence(makeFrequencies(), makeCoherence());
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    const QRectF data = chart.dataRect();
    QVERIFY(!data.isEmpty());

    // A pixel just under the top of the data rectangle is inside the shaded
    // region above the 0.5 threshold, so it must carry the band's alpha even
    // where the trace is far below it.
    const QPointF insideBand(data.center().x(), data.top() + data.height() * 0.05);
    const QColor bandPixel = pixelAt(image, insideBand);
    QVERIFY2(bandPixel.alpha() > 0,
             "the significance band above the threshold must be shaded");

    // The threshold badge is part of the frame and uses the series palette's
    // primary accent; at least some pixel in the image must differ from the
    // background.
    const QColor background = pixelAt(image, QPointF(2.0, 2.0));
    QVERIFY(pixelsDifferingFrom(image, background) > 100);
}

void TestCoherenceChart::dataMarksUseSeriesColour()
{
    md::CoherenceChart chart;
    chart.setCoherence(makeFrequencies(), makeCoherence());
    prepare(chart);

    const QImage image = renderWidget(chart);
    const QColor trace = chart.seriesColour(0);
    QVERIFY(trace.isValid());

    for (int index = 0; index < chart.sampleCount(); ++index)
        QVERIFY(hasColourNear(image, chart.pointForIndex(index), trace));
}

void TestCoherenceChart::hitTestMapsPixels()
{
    md::CoherenceChart chart;
    chart.setCoherence(makeFrequencies(), makeCoherence());
    prepare(chart);

    for (int index = 0; index < chart.sampleCount(); ++index) {
        const QPointF point = chart.pointForIndex(index);
        QVERIFY(!point.isNull());
        QCOMPARE(chart.binAt(point), index);
    }

    QCOMPARE(chart.binAt(QPointF(-20.0, -20.0)), -1);
    QCOMPARE(chart.binAt(QPointF(chart.dataRect().right() + 200.0,
                                 chart.dataRect().center().y())),
             -1);

    chart.setFrequencyAxis(md::CoherenceChart::FrequencyAxis::Logarithmic);
    for (int index = 0; index < chart.sampleCount(); ++index)
        QCOMPARE(chart.binAt(chart.pointForIndex(index)), index);
}

void TestCoherenceChart::emptyStatePaintsLabel()
{
    md::CoherenceChart chart;
    prepare(chart);

    QCOMPARE(chart.sampleCount(), 0);
    QCOMPARE(chart.binAt(QPointF(100.0, 100.0)), -1);
    QVERIFY(chart.frequencyTicks().isEmpty());

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());
    const QColor background = pixelAt(image, QPointF(2.0, 2.0));
    QVERIFY2(pixelsDifferingFrom(image, background) > 0,
             "the empty state must paint its themed label");
}

void TestCoherenceChart::revealReachesOne()
{
    md::CoherenceChart chart;
    chart.setCoherence(makeFrequencies(), makeCoherence());
    prepare(chart);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.show();
    chart.repaint();

    // The reveal starts on the first visible paint, so wait for that paint
    // rather than for the show() call itself.
    QTRY_VERIFY_WITH_TIMEOUT(chart.revealProgress() < 1.0, 2000);
    QVERIFY(chart.isRevealing());

    QTRY_VERIFY_WITH_TIMEOUT(!chart.isRevealing(), 5000);
    QCOMPARE(chart.revealProgress(), 1.0);
}

void TestCoherenceChart::reducedMotionSnaps()
{
    md::CoherenceChart chart;
    md::MotionScheme motion;
    motion.reducedMotion = true;
    md::ThemeScope* scope = md::ThemeScope::install(
        &chart, md::Theme::baselineLight().withMotion(motion));
    QVERIFY(scope != nullptr);

    chart.setCoherence(makeFrequencies(), makeCoherence());
    prepare(chart);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.show();

    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(!chart.isRevealing());

    const QImage image = renderWidget(chart);
    QVERIFY(hasColourNear(image, chart.pointForIndex(2), chart.seriesColour(0)));
}

QTEST_MAIN(TestCoherenceChart)
#include "tst_coherence_chart.moc"
