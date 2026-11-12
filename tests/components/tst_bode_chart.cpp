// Tests for the BodeChart component.
//
// The suite renders offscreen at a fixed geometry and checks the two stacked
// plot areas independently: the magnitude axis in dB above, the wrapped phase
// axis in degrees below, and the shared logarithmic frequency ticks. The
// pixel assertions use the chart's own settled points, so they cannot drift
// from the geometry paintPlot() uses.

#include <md/components/bode_chart.h>
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
#include <vector>

namespace {

constexpr int kChartWidth = 640;
constexpr int kChartHeight = 480;

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

void setDefaultResponse(md::BodeChart& chart)
{
    chart.setResponse({10.0, 100.0, 1000.0, 10000.0, 100000.0},
                      {-30.0, -10.0, 0.0, -3.0, -40.0},
                      {0.0, -45.0, 90.0, 170.0, -170.0});
}

} // namespace

class TestBodeChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void constructionAndMeasurement();
    void responseIsSortedAndPhaseWrapped();
    void twoStackedAreasShareFrequencyAxis();
    void decibelAndPhaseTicks();
    void logFrequencyTicks();
    void dataMarksUseSeriesColour();
    void hitTestSelectsAreaThenSample();
    void emptyStatePaintsLabel();
    void revealReachesOne();
    void reducedMotionSnaps();
};

void TestBodeChart::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestBodeChart::constructionAndMeasurement()
{
    md::BodeChart chart;
    setDefaultResponse(chart);
    prepare(chart);

    QCOMPARE(chart.pointCount(), 5);
    QCOMPARE(chart.frequencyAxis(), md::BodeChart::FrequencyAxis::Logarithmic);
    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(chart.xAxisTitle().contains(QStringLiteral("Hz")));
    QVERIFY(chart.magnitudeAxisTitle().contains(QStringLiteral("dB")));
    QVERIFY(chart.phaseAxisTitle().contains(QStringLiteral("°")));

    chart.measure(md::MeasureSpec::unspecified(), md::MeasureSpec::unspecified());
    QVERIFY(chart.measuredSize().width() > 0.0);
    QVERIFY(chart.measuredSize().height() > 0.0);

    chart.setFrequencyAxis(md::BodeChart::FrequencyAxis::Linear);
    QCOMPARE(chart.frequencyAxis(), md::BodeChart::FrequencyAxis::Linear);

    // Out-of-range accessors stay neutral.
    QCOMPARE(chart.frequencyAt(-1), 0.0);
    QCOMPARE(chart.magnitudeDbAt(100), 0.0);
    QCOMPARE(chart.phaseDegAt(-100), 0.0);
}

void TestBodeChart::responseIsSortedAndPhaseWrapped()
{
    md::BodeChart chart;
    chart.setResponse({1000.0, 10.0, 100.0, 50.0}, {0.0, -20.0, -10.0, 3.0},
                      {190.0, -190.0, 45.0, 360.0});

    QCOMPARE(chart.pointCount(), 4);
    QCOMPARE(chart.frequencyAt(0), 10.0);
    QCOMPARE(chart.frequencyAt(1), 50.0);
    QCOMPARE(chart.frequencyAt(2), 100.0);
    QCOMPARE(chart.frequencyAt(3), 1000.0);

    // Every stored phase is wrapped into −180..180.
    for (int index = 0; index < chart.pointCount(); ++index) {
        const double phase = chart.phaseDegAt(index);
        QVERIFY(phase >= -180.0 && phase <= 180.0);
    }
    QVERIFY(std::abs(chart.phaseDegAt(0) - 170.0) < 1.0e-9);

    // A non-finite sample is dropped as a whole row.
    chart.setResponse({10.0, 20.0}, {0.0, std::nan("")}, {0.0, 45.0});
    QCOMPARE(chart.pointCount(), 1);
}

void TestBodeChart::twoStackedAreasShareFrequencyAxis()
{
    md::BodeChart chart;
    setDefaultResponse(chart);
    prepare(chart);

    const QRectF magnitude = chart.magnitudeRect();
    const QRectF phase = chart.phaseRect();
    QVERIFY(!magnitude.isEmpty());
    QVERIFY(!phase.isEmpty());

    // Magnitude is above phase, the two share the same x range, and the gap is
    // positive rather than the areas overlapping.
    QVERIFY(magnitude.bottom() < phase.top());
    QVERIFY(std::abs(magnitude.left() - phase.left()) < 0.5);
    QVERIFY(std::abs(magnitude.width() - phase.width()) < 0.5);
    QVERIFY(magnitude.height() > phase.height() * 0.5);

    // A sample is drawn in each area at the same x coordinate.
    QCOMPARE(chart.magnitudePoint(2).x(), chart.phasePoint(2).x());
}

void TestBodeChart::decibelAndPhaseTicks()
{
    md::BodeChart chart;
    setDefaultResponse(chart);
    prepare(chart);

    const QVector<double> dbTicks = chart.magnitudeTicks();
    QVERIFY(dbTicks.size() >= 3);
    QCOMPARE(dbTicks.first(), -40.0);
    QCOMPARE(dbTicks.last(), 0.0);
    for (int i = 1; i < dbTicks.size(); ++i)
        QVERIFY(std::abs((dbTicks.at(i) - dbTicks.at(i - 1)) - 20.0) < 1.0e-9);

    const QStringList dbLabels = chart.magnitudeTickLabels();
    QCOMPARE(dbLabels.size(), dbTicks.size());
    QVERIFY(dbLabels.contains(QStringLiteral("-40")));
    QVERIFY(dbLabels.contains(QStringLiteral("0")));

    const QVector<double> phaseTicks = chart.phaseTicks();
    QCOMPARE(phaseTicks.size(), qsizetype(5));
    QCOMPARE(phaseTicks.first(), -180.0);
    QCOMPARE(phaseTicks.last(), 180.0);
    QVERIFY(phaseTicks.contains(0.0));

    const QStringList phaseLabels = chart.phaseTickLabels();
    QVERIFY(phaseLabels.contains(QStringLiteral("-180")));
    QVERIFY(phaseLabels.contains(QStringLiteral("180")));
}

void TestBodeChart::logFrequencyTicks()
{
    md::BodeChart chart;
    setDefaultResponse(chart);
    prepare(chart);

    const QVector<double> ticks = chart.frequencyTicks();
    QVERIFY(ticks.contains(1000.0));
    QVERIFY(ticks.contains(10000.0));

    const QStringList labels = chart.frequencyTickLabels();
    QCOMPARE(labels.size(), ticks.size());
    QVERIFY(labels.contains(QStringLiteral("1 kHz")));
    QVERIFY(labels.contains(QStringLiteral("10 kHz")));
}

void TestBodeChart::dataMarksUseSeriesColour()
{
    md::BodeChart chart;
    setDefaultResponse(chart);
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    const QColor magnitudeColour = chart.seriesColour(0);
    const QColor phaseColour = chart.seriesColour(1);
    QVERIFY(magnitudeColour.isValid());
    QVERIFY(phaseColour.isValid());

    QVERIFY(hasColourNear(image, chart.magnitudePoint(2), magnitudeColour));
    QVERIFY(hasColourNear(image, chart.phasePoint(2), phaseColour));

    const QColor background = pixelAt(image, QPointF(2.0, 2.0));
    QVERIFY2(pixelsDifferingFrom(image, background) > 100,
             "both plot areas must paint a frame and a trace");
}

void TestBodeChart::hitTestSelectsAreaThenSample()
{
    md::BodeChart chart;
    setDefaultResponse(chart);
    prepare(chart);

    for (int index = 0; index < chart.pointCount(); ++index) {
        QCOMPARE(chart.nearestIndex(chart.magnitudePoint(index)), index);
        QCOMPARE(chart.nearestIndex(chart.phasePoint(index)), index);
    }

    QCOMPARE(chart.nearestIndex(QPointF(-40.0, -40.0)), -1);
    QCOMPARE(chart.nearestIndex(QPointF(chart.magnitudeRect().right() + 200.0,
                                        chart.magnitudeRect().center().y())),
             -1);
}

void TestBodeChart::emptyStatePaintsLabel()
{
    md::BodeChart chart;
    prepare(chart);

    QCOMPARE(chart.pointCount(), 0);
    QCOMPARE(chart.nearestIndex(QPointF(100.0, 100.0)), -1);
    QVERIFY(chart.frequencyTicks().isEmpty());
    QVERIFY(chart.magnitudeTicks().isEmpty());

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());
    const QColor background = pixelAt(image, QPointF(2.0, 2.0));
    QVERIFY2(pixelsDifferingFrom(image, background) > 0,
             "the empty state must paint its themed label");
}

void TestBodeChart::revealReachesOne()
{
    md::BodeChart chart;
    setDefaultResponse(chart);
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

void TestBodeChart::reducedMotionSnaps()
{
    md::BodeChart chart;
    md::MotionScheme motion;
    motion.reducedMotion = true;
    md::ThemeScope* scope = md::ThemeScope::install(
        &chart, md::Theme::baselineLight().withMotion(motion));
    QVERIFY(scope != nullptr);

    setDefaultResponse(chart);
    prepare(chart);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.show();

    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(!chart.isRevealing());

    const QImage image = renderWidget(chart);
    QVERIFY(hasColourNear(image, chart.magnitudePoint(2), chart.seriesColour(0)));
}

QTEST_MAIN(TestBodeChart)
#include "tst_bode_chart.moc"
