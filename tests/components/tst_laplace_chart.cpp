// Unit tests for the LaplaceChart component.
//
// The widget is exercised offscreen: the application theme is pinned to baseline
// light and every widget is rendered into an explicitly transparent QImage with
// QWidget::render, so a pixel assertion looks at what the chart itself painted.
//
// The tests pin the documented contract: the row-major spectrum is accepted and
// flagged as present, the colour ramp separates the minimum from the maximum,
// the nice ticks stay inside the stored axes, the field, the slices and the
// colour bar paint visible pixels, sampleAt() round-trips through toPixel(),
// poles and zeros land on their pixel centres, the empty state paints text,
// the reveal reaches 1, and reduced motion snaps synchronously.

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QTest>
#include <QWidget>

#include <md/components/laplace_chart.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>
#include <md/tokens/motion.h>

#include <cmath>
#include <complex>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

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
    std::vector<double> sigmas;
    std::vector<double> omegas;
    std::vector<std::complex<double>> values;
};

// F(s) = 1 / (s + 0.5), sampled on a regular 25 × 25 window.
Spectrum makeSpectrum()
{
    Spectrum spectrum;
    for (int i = 0; i < 25; ++i) {
        const double sigma = -2.0 + 4.0 * static_cast<double>(i) / 24.0;
        spectrum.sigmas.push_back(sigma);
    }
    for (int j = 0; j < 25; ++j) {
        const double omega = -6.0 + 12.0 * static_cast<double>(j) / 24.0;
        spectrum.omegas.push_back(omega);
    }

    for (double sigma : spectrum.sigmas) {
        for (double omega : spectrum.omegas) {
            const std::complex<double> s(sigma, omega);
            spectrum.values.push_back(1.0 / (s + 0.5));
        }
    }
    return spectrum;
}

} // namespace

class TestLaplaceChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void constructionAndMeasurement();
    void colourMappingAndTicks();
    void paintsFieldSlicesAndColourBar();
    void sampleAtMapsPixelsToSPlane();
    void polesAndZerosLandOnPixels();
    void emptyStatePaintsText();
    void revealReachesOne();
    void reducedMotionSnaps();
};

void TestLaplaceChart::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestLaplaceChart::constructionAndMeasurement()
{
    md::LaplaceChart chart;
    QVERIFY(!chart.hasSpectrum());
    QVERIFY(chart.sizeHint().width() > 0);
    QVERIFY(chart.sizeHint().height() > 0);

    chart.resize(520, 420);
    QVERIFY(chart.plotRect().isValid());
    QVERIFY(chart.fieldRect().isValid());
    QVERIFY(chart.colourBarRect().isValid());
    QVERIFY(chart.slicesVisible());
    QVERIFY(chart.showsUnitGrid());
    QVERIFY(!chart.isRevealing());
    QCOMPARE(chart.revealProgress(), 1.0);

    const Spectrum spectrum = makeSpectrum();
    chart.setSpectrum(spectrum.sigmas, spectrum.omegas, spectrum.values);
    QVERIFY(chart.hasSpectrum());
    QCOMPARE(chart.sigmas().size(), std::size_t(25));
    QCOMPARE(chart.omegas().size(), std::size_t(25));
    QCOMPARE(chart.values().size(), std::size_t(625));
}

void TestLaplaceChart::colourMappingAndTicks()
{
    md::LaplaceChart chart;
    const Spectrum spectrum = makeSpectrum();
    chart.setSpectrum(spectrum.sigmas, spectrum.omegas, spectrum.values);
    chart.resize(520, 420);

    QVERIFY(chart.maximumMagnitude() > chart.minimumMagnitude());
    const QColor low = chart.colourForMagnitude(chart.minimumMagnitude());
    const QColor high = chart.colourForMagnitude(chart.maximumMagnitude());
    QVERIFY(low.isValid());
    QVERIFY(high.isValid());
    QCOMPARE(low.alpha(), 255);
    QVERIFY2(colourDistance(low, high) > 24, "the ramp must separate the range endpoints");

    const QVector<double> sigmaTicks = chart.sigmaTicks();
    QVERIFY(sigmaTicks.size() >= 2);
    for (int index = 0; index < sigmaTicks.size(); ++index) {
        QVERIFY(sigmaTicks.at(index) >= spectrum.sigmas.front() - 1e-9);
        QVERIFY(sigmaTicks.at(index) <= spectrum.sigmas.back() + 1e-9);
        if (index > 0)
            QVERIFY(sigmaTicks.at(index) > sigmaTicks.at(index - 1));
    }

    const QVector<double> omegaTicks = chart.omegaTicks();
    QVERIFY(omegaTicks.size() >= 2);
    for (int index = 0; index < omegaTicks.size(); ++index) {
        QVERIFY(omegaTicks.at(index) >= spectrum.omegas.front() - 1e-9);
        QVERIFY(omegaTicks.at(index) <= spectrum.omegas.back() + 1e-9);
        if (index > 0)
            QVERIFY(omegaTicks.at(index) > omegaTicks.at(index - 1));
    }

    QVERIFY(std::isnan(chart.convergenceAbscissa()));
    chart.setConvergenceAbscissa(-0.5);
    QCOMPARE(chart.convergenceAbscissa(), -0.5);
}

void TestLaplaceChart::paintsFieldSlicesAndColourBar()
{
    md::LaplaceChart chart;
    const Spectrum spectrum = makeSpectrum();
    chart.setSpectrum(spectrum.sigmas, spectrum.omegas, spectrum.values);
    chart.resize(520, 420);

    const QImage image = renderWidget(chart);
    const QColor background = md::Theme::baselineLight().colours().surface;

    const QRectF field = chart.fieldRect();
    QVERIFY(field.isValid());

    const QPointF centre = chart.toPixel(0.0, 0.0);
    QVERIFY(field.contains(centre));
    QVERIFY2(colourDistance(pixelAt(image, centre), background) > 24,
             "the field must paint its centre cell");

    QVERIFY(pixelsDifferingFrom(image, background, field) > 500);

    const QRectF bar = chart.colourBarRect();
    QVERIFY(bar.isValid());
    QVERIFY2(colourDistance(pixelAt(image, bar.center()), background) > 24,
             "the colour bar must paint a gradient");

    const QRectF sigmaSlice = chart.sigmaSliceRect();
    QVERIFY(sigmaSlice.isValid());
    QVERIFY(pixelsDifferingFrom(image, background, sigmaSlice) > 5);

    const QRectF omegaSlice = chart.omegaSliceRect();
    QVERIFY(omegaSlice.isValid());
    QVERIFY(pixelsDifferingFrom(image, background, omegaSlice) > 5);
}

void TestLaplaceChart::sampleAtMapsPixelsToSPlane()
{
    md::LaplaceChart chart;
    const Spectrum spectrum = makeSpectrum();
    chart.setSpectrum(spectrum.sigmas, spectrum.omegas, spectrum.values);
    chart.resize(520, 420);

    const md::LaplaceSample sample = chart.sampleAt(chart.toPixel(0.5, 2.0));
    QVERIFY(sample.valid);
    QVERIFY(std::abs(sample.sigma - 0.5) < 0.2);
    QVERIFY(std::abs(sample.omega - 2.0) < 0.3);
    const std::complex<double> expected = 1.0 / (std::complex<double>(sample.sigma, sample.omega) + 0.5);
    QVERIFY(std::abs(sample.magnitude - std::abs(expected)) < 0.15);
    QVERIFY(std::abs(sample.phase - std::arg(expected)) < 0.15);

    QVERIFY(!chart.sampleAt(chart.fieldRect().topLeft() - QPointF(8.0, 8.0)).valid);
    QVERIFY(!chart.sampleAt(QPointF(-50.0, -50.0)).valid);
}

void TestLaplaceChart::polesAndZerosLandOnPixels()
{
    md::LaplaceChart chart;
    const Spectrum spectrum = makeSpectrum();
    chart.setSpectrum(spectrum.sigmas, spectrum.omegas, spectrum.values);
    chart.setPoles({std::complex<double>(-0.5, 2.0)});
    chart.setZeros({std::complex<double>(1.0, -1.0)});
    chart.resize(520, 420);

    const QPointF pole = chart.polePixel(0);
    const QPointF expectedPole = chart.toPixel(-0.5, 2.0);
    QVERIFY(!pole.isNull());
    QVERIFY(std::hypot(pole.x() - expectedPole.x(), pole.y() - expectedPole.y()) < 0.01);

    const QPointF zero = chart.zeroPixel(0);
    const QPointF expectedZero = chart.toPixel(1.0, -1.0);
    QVERIFY(std::hypot(zero.x() - expectedZero.x(), zero.y() - expectedZero.y()) < 0.01);

    const QImage image = renderWidget(chart);
    const QColor poleColour = md::Theme::baselineLight().colours().error;
    const QColor zeroColour = md::Theme::baselineLight().colours().primary;
    QVERIFY2(hasColourNear(image, pole, poleColour, 8), "the pole × must be painted at its pixel");
    QVERIFY2(hasColourNear(image, zero, zeroColour, 8), "the zero ○ must be painted at its pixel");

    QVERIFY(chart.polePixel(-1).isNull());
    QVERIFY(chart.zeroPixel(5).isNull());
}

void TestLaplaceChart::emptyStatePaintsText()
{
    md::LaplaceChart chart;
    chart.resize(420, 320);

    const QImage image = renderWidget(chart);
    const QColor background = md::Theme::baselineLight().colours().surface;
    QVERIFY(pixelsDifferingFrom(image, background, QRectF()) > 40);
    QVERIFY(!chart.hasSpectrum());
}

void TestLaplaceChart::revealReachesOne()
{
    md::LaplaceChart chart;
    const Spectrum spectrum = makeSpectrum();
    chart.setSpectrum(spectrum.sigmas, spectrum.omegas, spectrum.values);
    chart.resize(480, 380);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.show();
    renderWidget(chart); // the first visible paint starts the reveal
    QVERIFY2(chart.revealProgress() < 1.0, "the default theme animates the field in");
    QVERIFY(chart.isRevealing());

    QTRY_VERIFY_WITH_TIMEOUT(!chart.isRevealing(), 5000);
    QCOMPARE(chart.revealProgress(), 1.0);
}

void TestLaplaceChart::reducedMotionSnaps()
{
    md::LaplaceChart chart;
    md::ThemeScope* scope = md::ThemeScope::install(&chart, reducedMotionTheme());
    QVERIFY(scope != nullptr);

    const Spectrum spectrum = makeSpectrum();
    chart.setSpectrum(spectrum.sigmas, spectrum.omegas, spectrum.values);
    chart.resize(420, 320);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.show();
    const QImage image = renderWidget(chart);
    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(!chart.isRevealing());
    const QPointF centre = chart.toPixel(0.0, 0.0);
    QVERIFY(colourDistance(pixelAt(image, centre),
                           md::Theme::baselineLight().colours().surface) > 24);
}

QTEST_MAIN(TestLaplaceChart)
#include "tst_laplace_chart.moc"
