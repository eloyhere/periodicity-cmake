// Tests for the AutocorrelationChart component.
//
// The suite pins a reduced-motion application theme so the stems are settled
// before the first render. The computed-sequence case uses a sine whose period
// is exactly eight samples, so the correlation at lag 8 is strongly positive
// and at lag 4 strongly negative; that checks the formula without coupling the
// test to a particular normalisation constant. Geometry and hit testing share
// stemPoint(), and the reveal cases install a normal-motion ThemeScope
// explicitly.

#include <md/components/autocorrelation_chart.h>
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

std::vector<double> periodicSamples(int count, int period)
{
    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(count));

    for (int index = 0; index < count; ++index) {
        samples.push_back(std::sin(2.0 * kPi * static_cast<double>(index)
                                   / static_cast<double>(period)));
    }

    return samples;
}

} // namespace

class TestAutocorrelationChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void constructsWithDefaults();
    void setValuesKeepsLagIndices();
    void setSamplesComputesNormalisedCorrelation();
    void axisTicksFollowTheLagUnit();
    void stemsPaintAtStemPoint();
    void hitTestResolvesStems();
    void emptyValuesPaint();
    void revealAnimationReachesOne();
    void reducedMotionSnaps();
};

void TestAutocorrelationChart::initTestCase()
{
    const md::Theme theme = reducedMotionTheme();
    md::Theme::setApplicationTheme(theme);
    QApplication::setPalette(theme.toPalette());
}

void TestAutocorrelationChart::cleanupTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestAutocorrelationChart::constructsWithDefaults()
{
    md::AutocorrelationChart chart;

    QVERIFY(chart.values().empty());
    QVERIFY(chart.samples().empty());
    QCOMPARE(chart.sampleRate(), 44100.0);
    QCOMPARE(chart.maxLag(), 0);
    QCOMPARE(chart.effectiveMaxLag(), 0);
    QCOMPARE(chart.threshold(), 0.5);
    QVERIFY(chart.lagUnit() == md::AutocorrelationChart::LagUnit::Samples);
    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(!chart.isRevealing());
    QVERIFY(chart.isAxesVisible());
    QVERIFY(chart.sizeHint().width() > 0);
    QVERIFY(chart.sizeHint().height() > 0);

    chart.setValues({1.0, 0.4, -0.2});
    QCOMPARE(static_cast<int>(chart.values().size()), 3);
    QCOMPARE(chart.effectiveMaxLag(), 2);
    QCOMPARE(chart.lagForIndex(2), 2.0);

    chart.setThreshold(1.5);
    QCOMPARE(chart.threshold(), 1.0);
    chart.setThreshold(-1.0);
    QCOMPARE(chart.threshold(), 0.0);

    // setMaxLag() does not truncate a sequence installed by setValues().
    chart.setMaxLag(7);
    QCOMPARE(chart.maxLag(), 7);
    QCOMPARE(static_cast<int>(chart.values().size()), 3);

    chart.setLagUnit(md::AutocorrelationChart::LagUnit::Milliseconds);
    QVERIFY(chart.lagUnit() == md::AutocorrelationChart::LagUnit::Milliseconds);
    QVERIFY(std::abs(chart.lagForIndex(1) - 1000.0 / 44100.0) < 1e-12);
}

void TestAutocorrelationChart::setValuesKeepsLagIndices()
{
    md::AutocorrelationChart chart;
    const std::vector<double> values = {1.0, 0.5, -0.5, 0.25, -0.25};
    chart.setValues(values);

    QCOMPARE(static_cast<int>(chart.values().size()), 5);
    QCOMPARE(chart.effectiveMaxLag(), 4);
    for (int index = 0; index < 5; ++index) {
        QCOMPARE(chart.values().at(static_cast<size_t>(index)),
                 values.at(static_cast<size_t>(index)));
        QCOMPARE(chart.lagForIndex(index), static_cast<double>(index));
    }

    chart.setValues({});
    QVERIFY(chart.values().empty());
    QCOMPARE(chart.effectiveMaxLag(), 0);
}

void TestAutocorrelationChart::setSamplesComputesNormalisedCorrelation()
{
    md::AutocorrelationChart chart;
    chart.setSamples(periodicSamples(64, 8), 8000.0);
    chart.setMaxLag(16);

    QCOMPARE(chart.maxLag(), 16);
    QCOMPARE(static_cast<int>(chart.values().size()), 17);
    QVERIFY(std::abs(chart.values().front() - 1.0) < 1e-9);

    for (const double value : chart.values())
        QVERIFY(std::abs(value) <= 1.0 + 1e-9);

    // One period (8 samples) correlates strongly; half a period (4 samples)
    // correlates strongly in the opposite direction.
    QVERIFY2(chart.values().at(8) > 0.8, "lag 8 must be a positive period peak");
    QVERIFY2(chart.values().at(4) < -0.8, "lag 4 must be a negative half-period trough");

    // A constant signal has no variance and reports only the lag-zero value.
    md::AutocorrelationChart constant;
    constant.setSamples({3.0, 3.0, 3.0, 3.0}, 1000.0);
    QCOMPARE(constant.values().front(), 1.0);
    for (size_t index = 1; index < constant.values().size(); ++index)
        QCOMPARE(constant.values().at(index), 0.0);
}

void TestAutocorrelationChart::axisTicksFollowTheLagUnit()
{
    md::AutocorrelationChart chart;
    chart.setValues({1.0, 0.5, 0.25, 0.1, 0.0, -0.1, -0.25, -0.5, -0.75, -0.9, -1.0});
    prepare(chart);

    const QVector<qreal> lagTicks = chart.lagTicks();
    QVERIFY(lagTicks.size() >= 2);
    for (const qreal tick : lagTicks) {
        QVERIFY(tick >= 0.0);
        QVERIFY(tick <= 10.0 + 1e-9);
    }

    const QVector<qreal> valueTicks = chart.valueTicks();
    QVERIFY(valueTicks.size() >= 2);
    for (const qreal tick : valueTicks) {
        QVERIFY(tick >= -1.0 - 1e-9);
        QVERIFY(tick <= 1.0 + 1e-9);
    }
    QVERIFY(chart.lagAxisLabel().contains(QStringLiteral("samples")));

    chart.setSamples(periodicSamples(64, 8), 1000.0);
    chart.setMaxLag(10);
    chart.setLagUnit(md::AutocorrelationChart::LagUnit::Milliseconds);

    QVERIFY(chart.lagAxisLabel().contains(QStringLiteral("ms")));
    const QVector<qreal> milliseconds = chart.lagTicks();
    QVERIFY(milliseconds.size() >= 2);
    for (const qreal tick : milliseconds) {
        QVERIFY(tick >= 0.0);
        QVERIFY(tick <= 10.0 + 1e-9);
    }
}

void TestAutocorrelationChart::stemsPaintAtStemPoint()
{
    md::AutocorrelationChart chart;
    chart.setValues({1.0, 0.8, 0.5, 0.1, -0.4});
    prepare(chart);

    const QPointF point = chart.stemPoint(1);
    QVERIFY(!point.isNull());
    QVERIFY(chart.plotRect().contains(point));

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    const QColor background = pixelAt(image, QPointF(2.0, 2.0));
    const QRectF around(point.x() - 3.0, point.y() - 3.0, 6.0, 6.0);
    QVERIFY2(paintedPixels(image, around, background) > 0,
             "the stem or its tip marker must paint at stemPoint()");
}

void TestAutocorrelationChart::hitTestResolvesStems()
{
    md::AutocorrelationChart chart;
    chart.setValues({1.0, 0.8, 0.5, 0.1, -0.4});
    prepare(chart);

    const QPointF point = chart.stemPoint(1);
    QVERIFY(!point.isNull());

    int seriesIndex = -1;
    int pointIndex = -1;
    QVERIFY(chart.hitTest(point, &seriesIndex, &pointIndex));
    QCOMPARE(seriesIndex, 0);
    QCOMPARE(pointIndex, 1);

    int stemIndex = -1;
    double lag = 0.0;
    double value = 0.0;
    QVERIFY(chart.stemAt(point, &stemIndex, &lag, &value));
    QCOMPARE(stemIndex, 1);
    QCOMPARE(lag, 1.0);
    QCOMPARE(value, 0.8);

    QVERIFY(!chart.stemAt(QPointF(1.0, 1.0), &stemIndex, &lag, &value));
}

void TestAutocorrelationChart::emptyValuesPaint()
{
    md::AutocorrelationChart chart;
    prepare(chart);

    QVERIFY(chart.values().empty());
    QVERIFY(!renderWidget(chart).isNull());

    chart.setValues({});
    QVERIFY(!renderWidget(chart).isNull());
}

void TestAutocorrelationChart::revealAnimationReachesOne()
{
    md::AutocorrelationChart chart;
    chart.setValues({1.0, 0.6, 0.2, -0.2, -0.6});

    md::ThemeScope* scope = md::ThemeScope::install(&chart, md::Theme::baselineLight());
    QVERIFY(scope != nullptr);

    chart.resize(kChartWidth, kChartHeight);
    chart.show();
    renderWidget(chart); // the first visible paint starts the shared reveal

    QVERIFY2(chart.revealProgress() < 1.0, "a normal-motion show must grow the stems");
    QVERIFY(chart.isRevealing());

    QTRY_VERIFY_WITH_TIMEOUT(!chart.isRevealing(), 5000);
    QCOMPARE(chart.revealProgress(), 1.0);
}

void TestAutocorrelationChart::reducedMotionSnaps()
{
    md::AutocorrelationChart chart;

    md::MotionScheme motion;
    motion.reducedMotion = true;
    md::ThemeScope* scope = md::ThemeScope::install(
        &chart, md::Theme::baselineLight().withMotion(motion));
    QVERIFY(scope != nullptr);

    chart.setValues({1.0, 0.8, 0.5, 0.1, -0.4});

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.resize(kChartWidth, kChartHeight);
    chart.show();
    renderWidget(chart); // a reduced-motion paint settles synchronously

    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(!chart.isRevealing());

    const QImage image = renderWidget(chart);
    const QPointF point = chart.stemPoint(1);
    QVERIFY(!point.isNull());
    const QColor background = pixelAt(image, QPointF(2.0, 2.0));
    QVERIFY(paintedPixels(image, QRectF(point.x() - 3.0, point.y() - 3.0, 6.0, 6.0),
                          background) > 0);
}

QTEST_MAIN(TestAutocorrelationChart)
#include "tst_autocorrelation_chart.moc"
