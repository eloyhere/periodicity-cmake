// Tests for the WaveformChart component.
//
// The suite pins a reduced-motion application theme so the reveal is settled
// before the first render. The waveform is a 1 kHz sine sampled at 1 kHz, which
// gives exact sample values at quarter periods; samplePoint() and sampleAt()
// are therefore checked against known times and amplitudes rather than against
// a tolerance derived from the paint code. The reveal cases install a
// normal-motion ThemeScope explicitly.

#include <md/components/waveform_chart.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>

#include <QApplication>
#include <QColor>
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
#include <vector>

namespace {

constexpr int kChartWidth = 520;
constexpr int kChartHeight = 380;
constexpr double kPi = 3.14159265358979323846;

md::Theme reducedMotionTheme()
{
    md::Theme theme = md::Theme::baselineLight();
    md::MotionScheme motion = theme.motion();
    motion.reducedMotion = true;
    return theme.withMotion(motion);
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

void prepare(QWidget& widget)
{
    widget.resize(kChartWidth, kChartHeight);
    widget.show();
    QCoreApplication::processEvents();
}

QColor pixelAt(const QImage& image, QPointF logical)
{
    const qreal ratio = image.devicePixelRatio();
    const int x = std::clamp(static_cast<int>(std::lround(logical.x() * ratio)),
                             0, image.width() - 1);
    const int y = std::clamp(static_cast<int>(std::lround(logical.y() * ratio)),
                             0, image.height() - 1);
    return image.pixelColor(x, y);
}

int paintedPixels(const QImage& image, const QRectF& logicalRect, const QColor& background)
{
    const qreal ratio = image.devicePixelRatio();
    const int left = std::clamp(static_cast<int>(std::floor(logicalRect.left() * ratio)),
                                0, image.width() - 1);
    const int top = std::clamp(static_cast<int>(std::floor(logicalRect.top() * ratio)),
                               0, image.height() - 1);
    const int right = std::clamp(static_cast<int>(std::ceil(logicalRect.right() * ratio)),
                                 0, image.width() - 1);
    const int bottom = std::clamp(static_cast<int>(std::ceil(logicalRect.bottom() * ratio)),
                                  0, image.height() - 1);

    int count = 0;
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            if (image.pixelColor(x, y) != background)
                ++count;
        }
    }

    return count;
}

std::vector<double> sineSamples(int count)
{
    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(count));

    for (int index = 0; index < count; ++index) {
        const double phase = 2.0 * kPi * static_cast<double>(index)
            / static_cast<double>(count);
        samples.push_back(std::sin(phase));
    }

    return samples;
}

} // namespace

class TestWaveformChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void constructsWithDefaults();
    void settersRebuildDerivedData();
    void timeAxisChoosesMillisecondsBelowOneSecond();
    void waveformPaintsAtSamplePoint();
    void hitTestResolvesKnownSamples();
    void emptySignalPaints();
    void revealAnimationReachesOne();
    void reducedMotionSnaps();
};

void TestWaveformChart::initTestCase()
{
    const md::Theme theme = reducedMotionTheme();
    md::Theme::setApplicationTheme(theme);
    QApplication::setPalette(theme.toPalette());
}

void TestWaveformChart::cleanupTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestWaveformChart::constructsWithDefaults()
{
    md::WaveformChart chart;

    QVERIFY(chart.samples().empty());
    QCOMPARE(chart.sampleRate(), 44100.0);
    QCOMPARE(chart.duration(), 0.0);
    QCOMPARE(chart.peakAmplitude(), 0.0);
    QVERIFY(chart.showsRms());
    QCOMPARE(chart.rmsWindow(), 256);
    QVERIFY(!chart.hasEnvelope());
    QVERIFY(!chart.usesMilliseconds());
    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(!chart.isRevealing());
    QVERIFY(chart.isAxesVisible());
    QVERIFY(chart.sizeHint().width() > 0);
    QVERIFY(chart.sizeHint().height() > 0);

    const std::vector<double> samples = sineSamples(1000);
    chart.setSamples(samples, 1000.0);
    QCOMPARE(static_cast<qsizetype>(chart.samples().size()),
             static_cast<qsizetype>(samples.size()));
    QCOMPARE(chart.sampleRate(), 1000.0);
    QCOMPARE(chart.duration(), 1.0);
    QVERIFY(std::abs(chart.peakAmplitude() - 1.0) < 1e-9);
    QCOMPARE(static_cast<qsizetype>(chart.rmsValues().size()), qsizetype(4));

    // The axes expose the unit the labels use.
    QVERIFY(chart.timeAxisLabel().contains(QStringLiteral("s")));
    QVERIFY(!chart.usesMilliseconds());
}

void TestWaveformChart::settersRebuildDerivedData()
{
    md::WaveformChart chart;
    chart.setSamples(sineSamples(1000), 1000.0);

    chart.setRmsWindow(100);
    QCOMPARE(chart.rmsWindow(), 100);
    QCOMPARE(static_cast<qsizetype>(chart.rmsValues().size()), qsizetype(10));

    // A smaller window changes the curve, not the number of legend entries.
    const std::vector<double> blockRms = chart.rmsValues();
    QVERIFY(!blockRms.empty());
    QVERIFY(blockRms.front() > 0.0);

    chart.setShowRms(false);
    QVERIFY(!chart.showsRms());
    QCOMPARE(chart.series().size(), qsizetype(1));

    chart.setShowRms(true);
    QVERIFY(chart.showsRms());
    QCOMPARE(chart.series().size(), qsizetype(2));

    chart.setEnvelope({-1.0, -0.5, 0.0}, {1.0, 0.5, 0.0});
    QVERIFY(chart.hasEnvelope());
    QCOMPARE(chart.series().size(), qsizetype(3));

    chart.setEnvelope({}, {});
    QVERIFY(!chart.hasEnvelope());
    QCOMPARE(chart.series().size(), qsizetype(2));

    // A rejected rate leaves the previous one in place.
    chart.setSamples(sineSamples(100), -1.0);
    QCOMPARE(chart.sampleRate(), 1000.0);

    chart.setRmsWindow(0);
    QCOMPARE(chart.rmsWindow(), 1);
}

void TestWaveformChart::timeAxisChoosesMillisecondsBelowOneSecond()
{
    md::WaveformChart chart;
    chart.setSamples(sineSamples(500), 1000.0);
    prepare(chart);

    QVERIFY(chart.usesMilliseconds());
    QVERIFY(chart.timeAxisLabel().contains(QStringLiteral("ms")));

    const QVector<qreal> shortTicks = chart.timeTicks();
    QVERIFY(shortTicks.size() >= 2);
    for (const qreal tick : shortTicks) {
        QVERIFY(tick >= 0.0);
        QVERIFY(tick <= 500.0 + 1e-9);
    }

    const QVector<qreal> amplitudeTicks = chart.amplitudeTicks();
    QVERIFY(amplitudeTicks.size() >= 2);
    for (const qreal tick : amplitudeTicks) {
        QVERIFY(std::abs(tick) <= chart.peakAmplitude() * 1.1 + 1e-9);
    }

    chart.setSamples(sineSamples(1000), 1000.0);
    QVERIFY(!chart.usesMilliseconds());
    QVERIFY(chart.timeAxisLabel().contains(QStringLiteral("Time (s)")));

    const QVector<qreal> longTicks = chart.timeTicks();
    QVERIFY(longTicks.size() >= 2);
    for (const qreal tick : longTicks) {
        QVERIFY(tick >= 0.0);
        QVERIFY(tick <= 1.0 + 1e-9);
    }
}

void TestWaveformChart::waveformPaintsAtSamplePoint()
{
    md::WaveformChart chart;
    chart.setSamples(sineSamples(1000), 1000.0);
    prepare(chart);

    const QPointF peak = chart.samplePoint(250);
    QVERIFY(!peak.isNull());
    QVERIFY(chart.plotRect().contains(peak));

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    const QColor background = pixelAt(image, QPointF(2.0, 2.0));
    const QRectF around(peak.x() - 3.0, peak.y() - 3.0, 6.0, 6.0);
    QVERIFY2(paintedPixels(image, around, background) > 0,
             "the waveform stroke must paint at samplePoint()");
}

void TestWaveformChart::hitTestResolvesKnownSamples()
{
    md::WaveformChart chart;
    chart.setSamples(sineSamples(1000), 1000.0);
    prepare(chart);

    const QPointF peak = chart.samplePoint(250);
    QVERIFY(!peak.isNull());

    double time = 0.0;
    double amplitude = 0.0;
    QVERIFY(chart.sampleAt(peak, &time, &amplitude));
    QVERIFY(std::abs(time - 0.25) < 1e-9);
    QVERIFY(std::abs(amplitude - 1.0) < 1e-9);

    int seriesIndex = -1;
    int pointIndex = -1;
    QVERIFY(chart.hitTest(peak, &seriesIndex, &pointIndex));
    QCOMPARE(seriesIndex, 0);
    QCOMPARE(pointIndex, 250);

    QVERIFY(!chart.sampleAt(QPointF(1.0, 1.0), &time, &amplitude));
}

void TestWaveformChart::emptySignalPaints()
{
    md::WaveformChart chart;
    prepare(chart);

    QVERIFY(chart.samples().empty());
    QVERIFY(!renderWidget(chart).isNull());

    chart.setSamples({}, 1000.0);
    QVERIFY(!renderWidget(chart).isNull());

    // A signal of only NaNs has no finite point and is still an empty state.
    chart.setSamples({std::nan(""), std::nan("")}, 1000.0);
    QVERIFY(!renderWidget(chart).isNull());
}

void TestWaveformChart::revealAnimationReachesOne()
{
    md::WaveformChart chart;
    chart.setSamples(sineSamples(1000), 1000.0);

    md::ThemeScope* scope = md::ThemeScope::install(&chart, md::Theme::baselineLight());
    QVERIFY(scope != nullptr);

    chart.resize(kChartWidth, kChartHeight);
    chart.show();
    renderWidget(chart); // the first visible paint starts the shared reveal

    QVERIFY2(chart.revealProgress() < 1.0, "a normal-motion show must draw the signal in");
    QVERIFY(chart.isRevealing());

    QTRY_VERIFY_WITH_TIMEOUT(!chart.isRevealing(), 5000);
    QCOMPARE(chart.revealProgress(), 1.0);
}

void TestWaveformChart::reducedMotionSnaps()
{
    md::WaveformChart chart;

    md::MotionScheme motion;
    motion.reducedMotion = true;
    md::ThemeScope* scope = md::ThemeScope::install(
        &chart, md::Theme::baselineLight().withMotion(motion));
    QVERIFY(scope != nullptr);

    chart.setSamples(sineSamples(1000), 1000.0);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.resize(kChartWidth, kChartHeight);
    chart.show();
    renderWidget(chart); // a reduced-motion paint settles synchronously

    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(!chart.isRevealing());

    const QImage image = renderWidget(chart);
    const QPointF peak = chart.samplePoint(250);
    QVERIFY(!peak.isNull());
    const QColor background = pixelAt(image, QPointF(2.0, 2.0));
    QVERIFY(paintedPixels(image, QRectF(peak.x() - 3.0, peak.y() - 3.0, 6.0, 6.0),
                          background) > 0);
}

QTEST_MAIN(TestWaveformChart)
#include "tst_waveform_chart.moc"
