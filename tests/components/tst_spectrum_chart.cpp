// Tests for the SpectrumChart component.
//
// The suite is offscreen and deterministic. Every chart is given an explicit
// geometry and rendered into a transparent QImage, so the pixel assertions
// look at what the component painted rather than at a window-system surface.
// Floats are compared with a tolerance only where a pixel position is derived
// from text metrics; values and tick sets are compared exactly.
//
// The cases cover the documented contract: bin-to-frequency mapping, linear
// and dB magnitude axes, linear and logarithmic frequency axes, phase on the
// right-hand axis, peak detection, the cursor readout, hit-testing, the empty
// state, and both branches of the reveal animation.

#include <md/components/spectrum_chart.h>
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

#include <algorithm>
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

// True when a small neighbourhood around `logical` contains a pixel close to
// `colour`. The radius covers antialiasing and the half-pixel rounding of the
// text-metric-derived data rectangle.
bool hasColourNear(const QImage& image, const QPointF& logical, const QColor& colour,
                   int radius = 3)
{
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const QColor sample = pixelAt(image, logical + QPointF(dx, dy));
            if (colourDistance(sample, colour) <= 60)
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

// 129 bins at 48 kHz: bin 20 is a strong 3750 Hz tone, bin 64 a 12 kHz tone,
// the floor is 0.001. Values are in linear magnitude.
std::vector<double> makeSpectrum()
{
    std::vector<double> values(129, 0.001);
    values.at(0) = 0.0; // the DC bin sits at the dB floor
    values.at(20) = 1.0;
    values.at(21) = 0.20;
    values.at(64) = 0.5;
    values.at(64 - 1) = 0.05;
    return values;
}

std::vector<double> makePhase()
{
    std::vector<double> phase(129, 0.0);
    for (std::size_t i = 0; i < phase.size(); ++i)
        phase.at(i) = -180.0 + 360.0 * static_cast<double>(i)
            / static_cast<double>(phase.size() - 1);
    return phase;
}

} // namespace

class TestSpectrumChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void constructionAndMeasurement();
    void binToFrequencyMapping();
    void decibelTicksAndFloor();
    void logFrequencyTicks();
    void phaseAddsRightAxisAndLegend();
    void peakIndicesAndCursorReadout();
    void paintedDataMarks();
    void hitTestMapsPixels();
    void emptyStatePaintsLabel();
    void revealReachesOne();
    void reducedMotionSnaps();
};

void TestSpectrumChart::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestSpectrumChart::constructionAndMeasurement()
{
    md::SpectrumChart chart;
    chart.setMagnitudes(makeSpectrum(), 48000.0);
    prepare(chart);

    QCOMPARE(chart.binCount(), 129);
    QCOMPARE(chart.sampleRate(), 48000.0);
    QCOMPARE(chart.nyquistFrequency(), 24000.0);
    QCOMPARE(chart.scale(), md::SpectrumChart::Scale::Linear);
    QCOMPARE(chart.frequencyAxis(), md::SpectrumChart::FrequencyAxis::Linear);
    QCOMPARE(chart.dbFloor(), -120.0);
    QVERIFY(chart.showsPeaks());
    QCOMPARE(chart.maxPeakLabels(), 3);
    QVERIFY(!chart.hasPhase());
    QCOMPARE(chart.revealProgress(), 1.0);

    // The base chart's measurement contract: 360 × 260 dp preferred.
    chart.measure(md::MeasureSpec::unspecified(), md::MeasureSpec::unspecified());
    QVERIFY(chart.measuredSize().width() > 0.0);
    QVERIFY(chart.measuredSize().height() > 0.0);

    chart.setScale(md::SpectrumChart::Scale::Db);
    chart.setFrequencyAxis(md::SpectrumChart::FrequencyAxis::Logarithmic);
    chart.setDbFloor(-80.0);
    QCOMPARE(chart.scale(), md::SpectrumChart::Scale::Db);
    QCOMPARE(chart.frequencyAxis(), md::SpectrumChart::FrequencyAxis::Logarithmic);
    QCOMPARE(chart.dbFloor(), -80.0);

    // The dB floor is clamped into a drawable range.
    chart.setDbFloor(5.0);
    QCOMPARE(chart.dbFloor(), -1.0);

    // An invalid sample rate clears the chart instead of producing a bogus
    // frequency axis.
    chart.setMagnitudes(makeSpectrum(), 0.0);
    QCOMPARE(chart.binCount(), 0);
}

void TestSpectrumChart::binToFrequencyMapping()
{
    md::SpectrumChart chart;
    chart.setMagnitudes(makeSpectrum(), 48000.0);
    prepare(chart);

    QCOMPARE(chart.frequencyAt(0), 0.0);
    QVERIFY(std::abs(chart.frequencyAt(64) - 12000.0) < 1.0e-9);
    QVERIFY(std::abs(chart.frequencyAt(128) - 24000.0) < 1.0e-9);
    QCOMPARE(chart.magnitudeAt(20), 1.0);
    QVERIFY(chart.decibelsAt(20) > -1.0e-9);

    // dB relative to full scale: 0.5 is about -6.02 dB.
    QVERIFY(std::abs(chart.decibelsAt(64) + 6.0206) < 0.01);
    QCOMPARE(chart.decibelsAt(0), chart.dbFloor());

    // Out-of-range accessors stay safe and neutral.
    QCOMPARE(chart.frequencyAt(-1), 0.0);
    QCOMPARE(chart.frequencyAt(1000), 0.0);
    QCOMPARE(chart.magnitudeAt(1000), 0.0);
}

void TestSpectrumChart::decibelTicksAndFloor()
{
    md::SpectrumChart chart;
    chart.setMagnitudes(makeSpectrum(), 48000.0);
    chart.setScale(md::SpectrumChart::Scale::Db);
    prepare(chart);

    const QVector<double> ticks = chart.magnitudeTicks();
    QVERIFY2(ticks.size() >= 2, "a dB axis must produce at least two ticks");
    QVERIFY(ticks.contains(-120.0));
    QVERIFY(ticks.contains(-100.0));
    QVERIFY(ticks.contains(-60.0));
    QVERIFY(ticks.contains(0.0));

    const QStringList labels = chart.magnitudeTickLabels();
    QCOMPARE(labels.size(), ticks.size());
    QVERIFY(labels.contains(QStringLiteral("-120")));
    QVERIFY(labels.contains(QStringLiteral("0")));

    // A custom floor moves the bottom tick and keeps 20 dB spacing.
    chart.setDbFloor(-87.0);
    const QVector<double> shifted = chart.magnitudeTicks();
    QVERIFY(shifted.size() >= 2);
    QCOMPARE(shifted.first(), -87.0);
    QVERIFY(std::abs((shifted.at(1) - shifted.at(0)) - 20.0) < 1.0e-9);

    // The axis title names the unit.
    QVERIFY(chart.yAxisTitle().contains(QStringLiteral("dB")));
}

void TestSpectrumChart::logFrequencyTicks()
{
    md::SpectrumChart chart;
    chart.setMagnitudes(makeSpectrum(), 48000.0);
    chart.setFrequencyAxis(md::SpectrumChart::FrequencyAxis::Logarithmic);
    prepare(chart);

    const QVector<double> ticks = chart.frequencyTicks();
    QVERIFY(ticks.contains(1000.0));
    QVERIFY(ticks.contains(10000.0));

    const QStringList labels = chart.frequencyTickLabels();
    QCOMPARE(labels.size(), ticks.size());
    QVERIFY(labels.contains(QStringLiteral("1 kHz")));
    QVERIFY(labels.contains(QStringLiteral("10 kHz")));

    // A sample on the log axis maps inside the data rectangle; the DC bin is
    // deliberately not drawable on a logarithmic axis.
    const QRectF data = chart.dataRect();
    QVERIFY(!data.isEmpty());
    QVERIFY2(chart.pointForBin(0).isNull(),
             "a DC bin has no position on a logarithmic frequency axis");
    const QPointF point = chart.pointForBin(64);
    QVERIFY(data.adjusted(-0.5, -0.5, 0.5, 0.5).contains(point));

    // The frequency axis title names the unit.
    QVERIFY(chart.xAxisTitle().contains(QStringLiteral("Hz")));
}

void TestSpectrumChart::phaseAddsRightAxisAndLegend()
{
    md::SpectrumChart chart;
    chart.setMagnitudes(makeSpectrum(), 48000.0);
    chart.setPhase(makePhase());
    prepare(chart);

    QVERIFY(chart.hasPhase());
    QCOMPARE(chart.phase().size(), std::size_t(129));
    QVERIFY(chart.phaseAxisTitle().contains(QStringLiteral("°")));

    // The base legend has one named entry for each trace.
    QCOMPARE(chart.series().size(), qsizetype(2));
    QCOMPARE(chart.series().at(0).name, QStringLiteral("Magnitude"));
    QCOMPARE(chart.series().at(1).name, QStringLiteral("Phase"));

    // Removing the phase removes the entry and the right-hand axis.
    chart.clearPhase();
    QVERIFY(!chart.hasPhase());
    QCOMPARE(chart.series().size(), qsizetype(1));
}

void TestSpectrumChart::peakIndicesAndCursorReadout()
{
    md::SpectrumChart chart;
    chart.setMagnitudes(makeSpectrum(), 48000.0);
    prepare(chart);

    const std::vector<int> peaks = chart.peakIndices();
    QVERIFY(peaks.size() >= 2);
    QCOMPARE(peaks.at(0), 20); // strongest first
    QVERIFY(std::find(peaks.begin(), peaks.end(), 64) != peaks.end());

    // The cursor snaps to the nearest bin by frequency, including across a log
    // axis, and the readout reports all three quantities.
    chart.setCursorFrequency(12000.0);
    QVERIFY(chart.hasCursor());
    QCOMPARE(chart.cursorBin(), 64);
    QVERIFY(std::abs(chart.cursorFrequency() - 12000.0) < 1.0e-9);

    const QString readout = chart.cursorReadout();
    QVERIFY(readout.contains(QStringLiteral("12 kHz")));
    QVERIFY(readout.contains(QStringLiteral("0.500")));
    QVERIFY(readout.contains(QStringLiteral("dB")));

    chart.clearCursor();
    QVERIFY(!chart.hasCursor());
    QCOMPARE(chart.cursorBin(), -1);
    QVERIFY(chart.cursorReadout().isEmpty());
}

void TestSpectrumChart::paintedDataMarks()
{
    md::SpectrumChart chart;
    chart.setMagnitudes(makeSpectrum(), 48000.0);
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    const QColor background = pixelAt(image, QPointF(2.0, 2.0));
    QVERIFY2(pixelsDifferingFrom(image, background) > 100,
             "the spectrum must paint a frame and a trace");

    const QColor trace = chart.seriesColour(0);
    QVERIFY(trace.isValid());
    const QPointF peak = chart.pointForBin(20);
    QVERIFY(hasColourNear(image, peak, trace));

    // Switching to the dB scale keeps the samples inside the same data
    // rectangle but changes the pixels.
    chart.setScale(md::SpectrumChart::Scale::Db);
    const QImage decibelImage = renderWidget(chart);
    QVERIFY2(image != decibelImage, "the dB scale must change the painted trace");

    // The set cursor draws a crosshair and a readout box.
    chart.setCursorFrequency(12000.0);
    const QImage cursorImage = renderWidget(chart);
    QVERIFY2(decibelImage != cursorImage, "the cursor must change the painted frame");
}

void TestSpectrumChart::hitTestMapsPixels()
{
    md::SpectrumChart chart;
    chart.setMagnitudes(makeSpectrum(), 48000.0);
    prepare(chart);

    for (int bin : {1, 20, 64, 100}) {
        const QPointF point = chart.pointForBin(bin);
        QVERIFY2(!point.isNull(), "a finite bin must have a settled position");
        QCOMPARE(chart.binAt(point), bin);
    }

    // A point outside the data rectangle names no bin.
    QCOMPARE(chart.binAt(QPointF(-40.0, -40.0)), -1);
    QCOMPARE(chart.binAt(QPointF(chart.dataRect().right() + 200.0,
                                 chart.dataRect().center().y())),
             -1);

    chart.setFrequencyAxis(md::SpectrumChart::FrequencyAxis::Logarithmic);
    for (int bin : {1, 20, 64, 100}) {
        const QPointF point = chart.pointForBin(bin);
        QVERIFY(!point.isNull());
        QCOMPARE(chart.binAt(point), bin);
    }
}

void TestSpectrumChart::emptyStatePaintsLabel()
{
    md::SpectrumChart chart;
    prepare(chart);

    QCOMPARE(chart.binCount(), 0);
    QCOMPARE(chart.binAt(QPointF(100.0, 100.0)), -1);
    QCOMPARE(chart.cursorBin(), -1);
    QVERIFY(chart.cursorReadout().isEmpty());
    QVERIFY(chart.peakIndices().empty());

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());
    const QColor background = pixelAt(image, QPointF(2.0, 2.0));
    QVERIFY2(pixelsDifferingFrom(image, background) > 0,
             "the empty state must paint its themed label");
}

void TestSpectrumChart::revealReachesOne()
{
    md::SpectrumChart chart;
    chart.setMagnitudes(makeSpectrum(), 48000.0);
    prepare(chart);

    // A never-shown chart paints finished.
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

void TestSpectrumChart::reducedMotionSnaps()
{
    md::SpectrumChart chart;
    md::MotionScheme motion;
    motion.reducedMotion = true;
    md::ThemeScope* scope = md::ThemeScope::install(
        &chart, md::Theme::baselineLight().withMotion(motion));
    QVERIFY(scope != nullptr);

    chart.setMagnitudes(makeSpectrum(), 48000.0);
    prepare(chart);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.show();

    // Reduced motion snaps synchronously: no frame ever shows a partial trace.
    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(!chart.isRevealing());

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());
    QVERIFY(hasColourNear(image, chart.pointForBin(20), chart.seriesColour(0)));
}

QTEST_MAIN(TestSpectrumChart)
#include "tst_spectrum_chart.moc"
