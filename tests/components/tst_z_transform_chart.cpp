// Unit tests for the ZTransformChart component.
//
// Offscreen rendering like the other chart suites: the application theme is
// pinned to baseline light and the widget is rendered into a transparent image.
//
// Pinned contract: the (Re, Im) grid is accepted, the plane is square and the
// unit circle is round, the colour ramp separates the magnitude endpoints in
// both the linear and the log transfer, the critical markers land on their
// computed pixels, sampleAt() round-trips, the empty state paints text, and the
// reveal reaches 1 (or snaps under reduced motion).

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QTest>
#include <QWidget>

#include <md/components/z_transform_chart.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>
#include <md/tokens/motion.h>

#include <cmath>
#include <complex>
#include <vector>

namespace {

int colourDistance(const QColor& first, const QColor& second)
{
    return qAbs(first.red() - second.red()) + qAbs(first.green() - second.green())
        + qAbs(first.blue() - second.blue()) + qAbs(first.alpha() - second.alpha());
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

QColor pixelAt(const QImage& image, const QPointF& logical)
{
    const qreal ratio = image.devicePixelRatio();
    const int x = qBound(0, qRound(logical.x() * ratio), image.width() - 1);
    const int y = qBound(0, qRound(logical.y() * ratio), image.height() - 1);
    return image.pixelColor(x, y);
}

int pixelsDifferingFrom(const QImage& image, const QColor& background, const QRectF& region)
{
    int count = 0;
    const QRect area = region.isNull()
        ? image.rect()
        : region.toRect().intersected(image.rect());

    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            if (colourDistance(image.pixelColor(x, y), background) > 24)
                ++count;
        }
    }
    return count;
}

bool hasColourNear(const QImage& image, const QPointF& centre, const QColor& target,
                   int radius)
{
    const int cx = qRound(centre.x());
    const int cy = qRound(centre.y());

    for (int y = cy - radius; y <= cy + radius; ++y) {
        for (int x = cx - radius; x <= cx + radius; ++x) {
            if (x < 0 || y < 0 || x >= image.width() || y >= image.height())
                continue;
            if (colourDistance(image.pixelColor(x, y), target) < 90)
                return true;
        }
    }
    return false;
}

md::Theme reducedMotionTheme()
{
    md::Theme theme = md::Theme::baselineLight();
    md::MotionScheme motion = theme.motion();
    motion.reducedMotion = true;
    return theme.withMotion(motion);
}

struct Spectrum
{
    std::vector<double> re;
    std::vector<double> im;
    std::vector<std::complex<double>> values;
};

// H(z) = 1 / (z - 0.5), sampled on a regular 21 × 21 window over [-2, 2]².
Spectrum makeSpectrum()
{
    Spectrum spectrum;
    for (int i = 0; i < 21; ++i)
        spectrum.re.push_back(-2.0 + 4.0 * static_cast<double>(i) / 20.0);
    for (int j = 0; j < 21; ++j)
        spectrum.im.push_back(-2.0 + 4.0 * static_cast<double>(j) / 20.0);

    for (double re : spectrum.re) {
        for (double im : spectrum.im) {
            const std::complex<double> z(re, im);
            spectrum.values.push_back(1.0 / (z - 0.5));
        }
    }
    return spectrum;
}

} // namespace

class TestZTransformChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void constructionAndMeasurement();
    void colourMappingAndTicks();
    void logMagnitudeTransfer();
    void unitCircleIsRound();
    void polesAndZerosLandOnPixels();
    void paintsPlaneAndColourBar();
    void sampleAtMapsPixelsToZPlane();
    void emptyStatePaintsText();
    void revealReachesOne();
    void reducedMotionSnaps();
};

void TestZTransformChart::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestZTransformChart::constructionAndMeasurement()
{
    md::ZTransformChart chart;
    QVERIFY(!chart.hasSpectrum());
    QVERIFY(chart.sizeHint().width() > 0);

    chart.resize(520, 400);
    QVERIFY(chart.plotRect().isValid());
    QVERIFY(chart.zPlaneRect().isValid());
    QVERIFY(chart.colourBarRect().isValid());
    QVERIFY(chart.unitCircleRadius() > 0.0);
    QVERIFY(!chart.isLogMagnitude());
    QCOMPARE(chart.revealProgress(), 1.0);

    const Spectrum spectrum = makeSpectrum();
    chart.setSpectrum(spectrum.re, spectrum.im, spectrum.values);
    QVERIFY(chart.hasSpectrum());
    QCOMPARE(chart.realAxis().size(), std::size_t(21));
    QCOMPARE(chart.imaginaryAxis().size(), std::size_t(21));
    QCOMPARE(chart.values().size(), std::size_t(441));
}

void TestZTransformChart::colourMappingAndTicks()
{
    md::ZTransformChart chart;
    const Spectrum spectrum = makeSpectrum();
    chart.setSpectrum(spectrum.re, spectrum.im, spectrum.values);
    chart.resize(520, 400);

    QVERIFY(chart.maximumMagnitude() > chart.minimumMagnitude());
    const QColor low = chart.colourForMagnitude(chart.minimumMagnitude());
    const QColor high = chart.colourForMagnitude(chart.maximumMagnitude());
    QCOMPARE(low.alpha(), 255);
    QVERIFY2(colourDistance(low, high) > 24, "the ramp must separate the range endpoints");

    const QVector<double> realTicks = chart.realTicks();
    QVERIFY(realTicks.size() >= 2);
    for (int index = 1; index < realTicks.size(); ++index)
        QVERIFY(realTicks.at(index) > realTicks.at(index - 1));

    const QVector<double> imaginaryTicks = chart.imaginaryTicks();
    QVERIFY(imaginaryTicks.size() >= 2);
    for (int index = 1; index < imaginaryTicks.size(); ++index)
        QVERIFY(imaginaryTicks.at(index) > imaginaryTicks.at(index - 1));

    QVERIFY(std::abs(realTicks.first() + realTicks.last()) < 1e-6);
}

void TestZTransformChart::logMagnitudeTransfer()
{
    md::ZTransformChart chart;
    const Spectrum spectrum = makeSpectrum();
    chart.setSpectrum(spectrum.re, spectrum.im, spectrum.values);
    chart.resize(520, 400);

    const QColor linearLow = chart.colourForMagnitude(chart.minimumMagnitude());
    const QColor linearHigh = chart.colourForMagnitude(chart.maximumMagnitude());
    QVERIFY(colourDistance(linearLow, linearHigh) > 24);

    chart.setLogMagnitude(true);
    QVERIFY(chart.isLogMagnitude());
    const QColor logLow = chart.colourForMagnitude(chart.minimumMagnitude());
    const QColor logHigh = chart.colourForMagnitude(chart.maximumMagnitude());
    QVERIFY2(colourDistance(logLow, logHigh) > 24,
             "the log transfer must still separate the endpoints");

    // The reported range stays |H|, not its logarithm.
    QVERIFY(chart.minimumMagnitude() >= 0.0);
    QVERIFY(chart.maximumMagnitude() > 1.0);
}

void TestZTransformChart::unitCircleIsRound()
{
    md::ZTransformChart chart;
    const Spectrum spectrum = makeSpectrum();
    chart.setSpectrum(spectrum.re, spectrum.im, spectrum.values);
    chart.resize(520, 400);

    const QRectF plane = chart.zPlaneRect();
    QVERIFY(plane.isValid());
    QVERIFY2(std::abs(plane.width() - plane.height()) < 0.5, "the plane must be square");

    const QPointF centre = plane.center();
    const QPointF top = chart.toPixel(0.0, 1.0);
    const QPointF right = chart.toPixel(1.0, 0.0);
    const double radius = chart.unitCircleRadius();

    QVERIFY(std::abs(std::hypot(top.x() - centre.x(), top.y() - centre.y()) - radius) < 0.01);
    QVERIFY(std::abs(std::hypot(right.x() - centre.x(), right.y() - centre.y()) - radius) < 0.01);
    QVERIFY(radius < plane.width() / 2.0);
}

void TestZTransformChart::polesAndZerosLandOnPixels()
{
    md::ZTransformChart chart;
    const Spectrum spectrum = makeSpectrum();
    chart.setSpectrum(spectrum.re, spectrum.im, spectrum.values);
    chart.setPoles({std::complex<double>(0.5, 0.5)});
    chart.setZeros({std::complex<double>(-0.5, 0.0)});
    chart.resize(520, 400);

    const QPointF pole = chart.polePixel(0);
    const QPointF expectedPole = chart.toPixel(0.5, 0.5);
    QVERIFY(std::hypot(pole.x() - expectedPole.x(), pole.y() - expectedPole.y()) < 0.01);

    const QPointF zero = chart.zeroPixel(0);
    const QPointF expectedZero = chart.toPixel(-0.5, 0.0);
    QVERIFY(std::hypot(zero.x() - expectedZero.x(), zero.y() - expectedZero.y()) < 0.01);

    const QImage image = renderWidget(chart);
    const QColor error = md::Theme::baselineLight().colours().error;
    const QColor primary = md::Theme::baselineLight().colours().primary;
    QVERIFY(hasColourNear(image, pole, error, 8));
    QVERIFY(hasColourNear(image, zero, primary, 8));
}

void TestZTransformChart::paintsPlaneAndColourBar()
{
    md::ZTransformChart chart;
    const Spectrum spectrum = makeSpectrum();
    chart.setSpectrum(spectrum.re, spectrum.im, spectrum.values);
    chart.resize(520, 400);

    const QImage image = renderWidget(chart);
    const QColor background = md::Theme::baselineLight().colours().surface;

    const QRectF plane = chart.zPlaneRect();
    const QPointF sample = chart.toPixel(0.75, 0.75);
    QVERIFY(plane.contains(sample));
    QVERIFY2(colourDistance(pixelAt(image, sample), background) > 24,
             "the magnitude field must paint inside the plane");
    QVERIFY(pixelsDifferingFrom(image, background, plane) > 500);

    const QRectF bar = chart.colourBarRect();
    QVERIFY(bar.isValid());
    QVERIFY(colourDistance(pixelAt(image, bar.center()), background) > 24);
}

void TestZTransformChart::sampleAtMapsPixelsToZPlane()
{
    md::ZTransformChart chart;
    const Spectrum spectrum = makeSpectrum();
    chart.setSpectrum(spectrum.re, spectrum.im, spectrum.values);
    chart.resize(520, 400);

    const md::ZSample sample = chart.sampleAt(chart.toPixel(0.4, 0.4));
    QVERIFY(sample.valid);
    QVERIFY(std::abs(sample.real - 0.4) < 0.25);
    QVERIFY(std::abs(sample.imaginary - 0.4) < 0.25);

    const std::complex<double> expected = 1.0 / (std::complex<double>(sample.real, sample.imaginary) - 0.5);
    QVERIFY(std::abs(sample.magnitude - std::abs(expected)) < 0.2);

    QVERIFY(!chart.sampleAt(chart.zPlaneRect().topLeft() - QPointF(10.0, 10.0)).valid);
    QVERIFY(!chart.sampleAt(QPointF(-100.0, -100.0)).valid);
}

void TestZTransformChart::emptyStatePaintsText()
{
    md::ZTransformChart chart;
    chart.resize(420, 340);

    const QImage image = renderWidget(chart);
    const QColor background = md::Theme::baselineLight().colours().surface;
    QVERIFY(pixelsDifferingFrom(image, background, QRectF()) > 40);
    QVERIFY(!chart.hasSpectrum());
}

void TestZTransformChart::revealReachesOne()
{
    md::ZTransformChart chart;
    const Spectrum spectrum = makeSpectrum();
    chart.setSpectrum(spectrum.re, spectrum.im, spectrum.values);
    chart.resize(480, 380);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.show();
    renderWidget(chart); // the first visible paint starts the reveal
    QVERIFY2(chart.revealProgress() < 1.0, "the default theme sweeps the plane in");
    QVERIFY(chart.isRevealing());

    QTRY_VERIFY_WITH_TIMEOUT(!chart.isRevealing(), 5000);
    QCOMPARE(chart.revealProgress(), 1.0);
}

void TestZTransformChart::reducedMotionSnaps()
{
    md::ZTransformChart chart;
    md::ThemeScope* scope = md::ThemeScope::install(&chart, reducedMotionTheme());
    QVERIFY(scope != nullptr);

    const Spectrum spectrum = makeSpectrum();
    chart.setSpectrum(spectrum.re, spectrum.im, spectrum.values);
    chart.resize(420, 340);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.show();
    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(!chart.isRevealing());
}

QTEST_MAIN(TestZTransformChart)
#include "tst_z_transform_chart.moc"
