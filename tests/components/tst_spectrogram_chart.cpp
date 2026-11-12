// Tests for the SpectrogramChart component.
//
// The suite is offscreen and deterministic: the application theme is pinned to
// a reduced-motion baseline so the reveal has snapped by the first render, and
// every geometry assertion uses the chart's own matrixRect()/cellRect() rather
// than a test-local guess. The reveal cases install a normal-motion ThemeScope
// explicitly, which is the only place the animator is allowed to run.
//
// The painted-pixel case compares the two ends of the dB ramp with the theme
// roles the ramp is built from; the hover case drives a real mouse event and
// then checks the readout values against sampleAt().

#include <md/components/spectrogram_chart.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QEvent>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QRegion>
#include <QTest>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kChartWidth = 520;
constexpr int kChartHeight = 380;

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

QMouseEvent moveEvent(QWidget& widget, QPointF position)
{
    return QMouseEvent(QEvent::MouseMove, position,
                       widget.mapToGlobal(position.toPoint()), Qt::NoButton,
                       Qt::NoButton, Qt::NoModifier);
}

} // namespace

class TestSpectrogramChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void constructsWithDefaults();
    void axisTicksUseSecondsAndKilohertz();
    void logFrequencyTicksAreOrdered();
    void paintedCellsDifferAcrossDbRange();
    void hoverResolvesTimeFrequencyAndLevel();
    void emptyMatrixPaints();
    void revealAnimationReachesOne();
    void reducedMotionSnaps();
};

void TestSpectrogramChart::initTestCase()
{
    const md::Theme theme = reducedMotionTheme();
    md::Theme::setApplicationTheme(theme);
    QApplication::setPalette(theme.toPalette());
}

void TestSpectrogramChart::cleanupTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestSpectrogramChart::constructsWithDefaults()
{
    md::SpectrogramChart chart;

    QCOMPARE(chart.frequencyBins(), 0);
    QCOMPARE(chart.frameCount(), 0);
    QVERIFY(!chart.isMagnitudeScale());
    QCOMPARE(chart.sampleRate(), 48000.0);
    QCOMPARE(chart.hopSize(), 512.0);
    QCOMPARE(chart.minimumDb(), -100.0);
    QCOMPARE(chart.maximumDb(), 0.0);
    QVERIFY(!chart.isLogFrequency());
    QVERIFY(chart.isColourBarVisible());
    QVERIFY(!chart.hasHover());
    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(!chart.isRevealing());
    QVERIFY(!chart.isAxesVisible());
    QVERIFY(chart.sizeHint().isValid());
    QVERIFY(chart.sizeHint().width() > 0);
    QVERIFY(chart.sizeHint().height() > 0);

    chart.setFrames({{-100.0, -40.0, 0.0}, {-90.0, -30.0, -10.0}});
    QCOMPARE(chart.frequencyBins(), 2);
    QCOMPARE(chart.frameCount(), 3);
    QVERIFY(!chart.isMagnitudeScale());

    chart.setSampleRate(8000.0);
    chart.setHopSize(128.0);
    QCOMPARE(chart.sampleRate(), 8000.0);
    QCOMPARE(chart.hopSize(), 128.0);
    QVERIFY(std::abs(chart.duration() - 3.0 * 128.0 / 8000.0) < 1e-12);
    QCOMPARE(chart.nyquistFrequency(), 4000.0);

    // A reversed dB range is normalised; a degenerate one is widened.
    chart.setDbRange(0.0, -60.0);
    QCOMPARE(chart.minimumDb(), -60.0);
    QCOMPARE(chart.maximumDb(), 0.0);
    chart.setDbRange(5.0, 5.0);
    QVERIFY(chart.maximumDb() > chart.minimumDb());

    // A rejected geometry update leaves the previous values in place.
    chart.setSampleRate(-1.0);
    chart.setHopSize(0.0);
    QCOMPARE(chart.sampleRate(), 8000.0);
    QCOMPARE(chart.hopSize(), 128.0);

    chart.setMagnitude({{0.0, 1.0}, {0.5, 0.0}, {0.1, 0.01}}, 16000.0, 64.0);
    QVERIFY(chart.isMagnitudeScale());
    QCOMPARE(chart.frequencyBins(), 3);
    QCOMPARE(chart.frameCount(), 2);
    QCOMPARE(chart.sampleRate(), 16000.0);
    QCOMPARE(chart.hopSize(), 64.0);

    chart.setLogFrequency(true);
    chart.setColourBarVisible(false);
    QVERIFY(chart.isLogFrequency());
    QVERIFY(!chart.isColourBarVisible());
}

void TestSpectrogramChart::axisTicksUseSecondsAndKilohertz()
{
    md::SpectrogramChart chart;
    chart.setFrames(std::vector<std::vector<double>>(5, std::vector<double>(8, -20.0)));
    chart.setSampleRate(8000.0);
    chart.setHopSize(128.0);
    chart.setDbRange(-100.0, 0.0);

    const double duration = chart.duration();
    QVERIFY(duration > 0.0);

    const QVector<qreal> timeTicks = chart.timeTicks();
    QVERIFY(timeTicks.size() >= 2);
    for (const qreal tick : timeTicks) {
        QVERIFY(tick >= 0.0);
        QVERIFY(tick <= duration + 1e-12);
        QVERIFY(!chart.timeTickLabel(tick).isEmpty());
    }

    const QVector<qreal> frequencyTicks = chart.frequencyTicks();
    QVERIFY(frequencyTicks.size() >= 2);
    for (qsizetype index = 1; index < frequencyTicks.size(); ++index)
        QVERIFY(frequencyTicks.at(index) > frequencyTicks.at(index - 1));

    QCOMPARE(chart.frequencyTickLabel(500.0), QStringLiteral("500"));
    QCOMPARE(chart.frequencyTickLabel(2000.0), QStringLiteral("2k"));
    QCOMPARE(chart.frequencyTickLabel(15000.0), QStringLiteral("15k"));
    QVERIFY(chart.frequencyTickLabel(2000.0).endsWith(QLatin1Char('k')));

    // The colour bar is part of the layout when visible.
    QVERIFY(!chart.colourBarRect().isEmpty());
    chart.setColourBarVisible(false);
    QVERIFY(chart.colourBarRect().isEmpty());
}

void TestSpectrogramChart::logFrequencyTicksAreOrdered()
{
    md::SpectrogramChart chart;
    chart.setFrames(std::vector<std::vector<double>>(65, std::vector<double>(4, -30.0)));
    chart.setSampleRate(48000.0);
    chart.setHopSize(256.0);
    chart.setLogFrequency(true);

    const QVector<qreal> ticks = chart.frequencyTicks();
    QVERIFY(ticks.size() >= 2);
    QVERIFY(ticks.first() > 0.0);
    for (qsizetype index = 1; index < ticks.size(); ++index) {
        QVERIFY(ticks.at(index) > ticks.at(index - 1));
        QVERIFY(ticks.at(index) <= chart.nyquistFrequency() + 1e-9);
    }

    // A 48 kHz / 65-bin setup spans roughly 750 Hz .. 24 kHz in log mode.
    QVERIFY(ticks.first() >= 100.0);
    QVERIFY(ticks.last() >= 10000.0);
}

void TestSpectrogramChart::paintedCellsDifferAcrossDbRange()
{
    md::SpectrogramChart chart;
    chart.setSampleRate(8000.0);
    chart.setHopSize(128.0);
    chart.setDbRange(-100.0, 0.0);
    chart.setFrames({{-100.0, 0.0, -100.0, 0.0}, {0.0, -100.0, 0.0, -100.0}});
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    const QRectF lowCell = chart.cellRect(0, 0);
    const QRectF highCell = chart.cellRect(1, 0);
    QVERIFY2(!lowCell.isEmpty(), "the low cell must have geometry");
    QVERIFY2(!highCell.isEmpty(), "the high cell must have geometry");
    QVERIFY(chart.matrixRect().contains(lowCell.center()));
    QVERIFY(chart.matrixRect().contains(highCell.center()));

    const QColor background = pixelAt(image, QPointF(2.0, 2.0));
    // Sample away from the tick grid: both cell centres happen to sit on a
    // 1000 Hz grid line in this matrix, which would paint outlineVariant over
    // the cell colour the ramp is being tested for.
    const QPointF lowSample(lowCell.center().x(), lowCell.top() + lowCell.height() * 0.25);
    const QPointF highSample(highCell.center().x(), highCell.top() + highCell.height() * 0.25);
    const QColor low = pixelAt(image, lowSample);
    const QColor high = pixelAt(image, highSample);

    QVERIFY2(low != high, "the two ends of the dB range must not share a colour");
    QVERIFY2(low != background, "the low cell must be painted");
    QVERIFY2(high != background, "the high cell must be painted");

    const md::ColorScheme& colours = md::Theme::forWidget(&chart).colours();
    QCOMPARE(low.rgb(), colours.surfaceContainerHighest.rgb());
    QVERIFY(high != colours.surfaceContainerHighest);
}

void TestSpectrogramChart::hoverResolvesTimeFrequencyAndLevel()
{
    md::SpectrogramChart chart;
    chart.setSampleRate(8000.0);
    chart.setHopSize(128.0);
    chart.setDbRange(-100.0, 0.0);
    chart.setFrames(std::vector<std::vector<double>>(5, std::vector<double>(6, -40.0)));
    prepare(chart);

    const QRectF cell = chart.cellRect(2, 3);
    QVERIFY(!cell.isEmpty());

    double time = 0.0;
    double frequency = 0.0;
    double level = 0.0;
    QVERIFY(chart.sampleAt(cell.center(), &time, &frequency, &level));
    QVERIFY(std::abs(time - (3.0 + 0.5) * 128.0 / 8000.0) < 1e-12);
    QVERIFY(std::abs(frequency - 2.0 * 4000.0 / 4.0) < 1e-9);
    QVERIFY(std::abs(level - (-40.0)) < 1e-12);

    QCOMPARE(chart.cellAt(cell.center()), QPoint(3, 2));
    QCOMPARE(chart.cellAt(QPointF(1.0, 1.0)), QPoint(-1, -1));

    QMouseEvent move = moveEvent(chart, cell.center());
    QCoreApplication::sendEvent(&chart, &move);

    QVERIFY(chart.hasHover());
    QCOMPARE(chart.hoveredFrame(), 3);
    QCOMPARE(chart.hoveredBin(), 2);
    QVERIFY(std::abs(chart.hoveredTime() - time) < 1e-12);
    QVERIFY(std::abs(chart.hoveredFrequency() - frequency) < 1e-9);
    QVERIFY(std::abs(chart.hoveredLevelDb() - level) < 1e-12);

    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(&chart, &leave);
    QVERIFY(!chart.hasHover());
}

void TestSpectrogramChart::emptyMatrixPaints()
{
    md::SpectrogramChart chart;
    prepare(chart);

    QVERIFY(chart.frames().empty());
    QVERIFY(!chart.matrixRect().isEmpty());
    const QImage emptyImage = renderWidget(chart);
    QVERIFY2(!emptyImage.isNull(), "an empty spectrogram must still paint its frame");

    // Rows without a finite level are still empty data.
    chart.setFrames({{}, {}});
    QVERIFY(!renderWidget(chart).isNull());
}

void TestSpectrogramChart::revealAnimationReachesOne()
{
    md::SpectrogramChart chart;
    chart.setFrames(std::vector<std::vector<double>>(8, std::vector<double>(12, -30.0)));

    md::ThemeScope* scope = md::ThemeScope::install(&chart, md::Theme::baselineLight());
    QVERIFY(scope != nullptr);

    chart.resize(kChartWidth, kChartHeight);
    chart.show();
    renderWidget(chart); // the first visible paint starts the shared reveal

    QVERIFY2(chart.revealProgress() < 1.0, "a normal-motion show must animate the columns in");
    QVERIFY(chart.isRevealing());

    QTRY_VERIFY_WITH_TIMEOUT(!chart.isRevealing(), 5000);
    QCOMPARE(chart.revealProgress(), 1.0);
}

void TestSpectrogramChart::reducedMotionSnaps()
{
    md::SpectrogramChart chart;

    md::MotionScheme motion;
    motion.reducedMotion = true;
    md::ThemeScope* scope = md::ThemeScope::install(
        &chart, md::Theme::baselineLight().withMotion(motion));
    QVERIFY(scope != nullptr);

    chart.setSampleRate(8000.0);
    chart.setHopSize(128.0);
    chart.setDbRange(-100.0, 0.0);
    chart.setFrames({{-100.0, 0.0}, {0.0, -100.0}});

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.resize(kChartWidth, kChartHeight);
    chart.show();
    renderWidget(chart); // a reduced-motion paint settles synchronously

    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(!chart.isRevealing());

    const QImage image = renderWidget(chart);
    const QRectF cell = chart.cellRect(1, 0);
    QVERIFY(!cell.isEmpty());
    const QColor background = pixelAt(image, QPointF(2.0, 2.0));
    QVERIFY(paintedPixels(image, cell.adjusted(2.0, 2.0, -2.0, -2.0), background) > 0);
}

QTEST_MAIN(TestSpectrogramChart)
#include "tst_spectrogram_chart.moc"
