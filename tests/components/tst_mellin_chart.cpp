// Unit tests for the MellinChart component.
//
// Offscreen rendering like the other chart suites: the application theme is
// pinned to baseline light and the widget is rendered into a transparent image,
// so a pixel assertion sees only what the component painted.
//
// Pinned contract: the (c, t) grid is accepted, the colour ramp separates the
// magnitude endpoints, the linear ticks stay inside their axis, the critical
// line is drawn where setCriticalLine() puts it, the log-t switch maps each
// decade to the same number of pixels, sampleAt() round-trips, the empty state
// paints text, and the reveal reaches 1 (or snaps under reduced motion).

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QTest>
#include <QWidget>

#include <md/components/mellin_chart.h>
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
            if (colourDistance(image.pixelColor(x, y), target) < 170)
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
    std::vector<double> c;
    std::vector<double> t;
    std::vector<std::complex<double>> values;
};

// F(c + jt) = 1 / (s + 1), sampled on a regular 21 × 21 window.
Spectrum makeLinearSpectrum()
{
    Spectrum spectrum;
    for (int i = 0; i < 21; ++i)
        spectrum.c.push_back(-2.0 + 4.0 * static_cast<double>(i) / 20.0);
    for (int j = 0; j < 21; ++j)
        spectrum.t.push_back(-4.0 + 8.0 * static_cast<double>(j) / 20.0);

    for (double c : spectrum.c) {
        for (double t : spectrum.t) {
            const std::complex<double> s(c, t);
            spectrum.values.push_back(1.0 / (s + 1.0));
        }
    }
    return spectrum;
}

// t samples three decades, 0.1 .. 100, for the logarithmic-axis test.
Spectrum makeLogSpectrum()
{
    Spectrum spectrum;
    for (int i = 0; i < 21; ++i)
        spectrum.c.push_back(-2.0 + 4.0 * static_cast<double>(i) / 20.0);
    for (int j = 0; j < 25; ++j) {
        const double exponent = -1.0 + 3.0 * static_cast<double>(j) / 24.0;
        spectrum.t.push_back(std::pow(10.0, exponent));
    }

    for (double c : spectrum.c) {
        for (double t : spectrum.t) {
            const std::complex<double> s(c, t);
            spectrum.values.push_back(1.0 / (s + 1.0));
        }
    }
    return spectrum;
}

} // namespace

class TestMellinChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void constructionAndMeasurement();
    void colourMappingAndTicks();
    void logAxisMapsDecadesEvenly();
    void paintsFieldAndColourBar();
    void criticalLinePaints();
    void sampleAtMapsPixelsToST();
    void emptyStatePaintsText();
    void revealReachesOne();
    void reducedMotionSnaps();
};

void TestMellinChart::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestMellinChart::constructionAndMeasurement()
{
    md::MellinChart chart;
    QVERIFY(!chart.hasSpectrum());
    QVERIFY(chart.sizeHint().width() > 0);

    chart.resize(520, 380);
    QVERIFY(chart.plotRect().isValid());
    QVERIFY(chart.fieldRect().isValid());
    QVERIFY(chart.colourBarRect().isValid());
    QVERIFY(!chart.isLogT());
    QVERIFY(!chart.hasCriticalLine());
    QCOMPARE(chart.revealProgress(), 1.0);

    const Spectrum spectrum = makeLinearSpectrum();
    chart.setSpectrum(spectrum.c, spectrum.t, spectrum.values);
    QVERIFY(chart.hasSpectrum());
    QCOMPARE(chart.realAxis().size(), std::size_t(21));
    QCOMPARE(chart.imaginaryAxis().size(), std::size_t(21));
    QCOMPARE(chart.values().size(), std::size_t(441));
}

void TestMellinChart::colourMappingAndTicks()
{
    md::MellinChart chart;
    const Spectrum spectrum = makeLinearSpectrum();
    chart.setSpectrum(spectrum.c, spectrum.t, spectrum.values);
    chart.resize(520, 380);

    QVERIFY(chart.maximumMagnitude() > chart.minimumMagnitude());
    const QColor low = chart.colourForMagnitude(chart.minimumMagnitude());
    const QColor high = chart.colourForMagnitude(chart.maximumMagnitude());
    QCOMPARE(low.alpha(), 255);
    QVERIFY2(colourDistance(low, high) > 24, "the ramp must separate the range endpoints");

    const QVector<double> cTicks = chart.cTicks();
    QVERIFY(cTicks.size() >= 2);
    for (int index = 0; index < cTicks.size(); ++index) {
        QVERIFY(cTicks.at(index) >= spectrum.c.front() - 1e-9);
        QVERIFY(cTicks.at(index) <= spectrum.c.back() + 1e-9);
        if (index > 0)
            QVERIFY(cTicks.at(index) > cTicks.at(index - 1));
    }

    const QVector<double> tTicks = chart.tTicks();
    QVERIFY(tTicks.size() >= 2);
    for (int index = 0; index < tTicks.size(); ++index) {
        QVERIFY(tTicks.at(index) >= spectrum.t.front() - 1e-9);
        QVERIFY(tTicks.at(index) <= spectrum.t.back() + 1e-9);
        if (index > 0)
            QVERIFY(tTicks.at(index) > tTicks.at(index - 1));
    }

    QVERIFY(std::isnan(chart.criticalLine()));
    chart.setCriticalLine(-0.75);
    QCOMPARE(chart.criticalLine(), -0.75);
    QVERIFY(chart.hasCriticalLine());
}

void TestMellinChart::logAxisMapsDecadesEvenly()
{
    md::MellinChart chart;
    const Spectrum spectrum = makeLogSpectrum();
    chart.setSpectrum(spectrum.c, spectrum.t, spectrum.values);
    chart.setLogT(true);
    chart.resize(520, 380);

    QVERIFY(chart.isLogT());

    const QVector<double> ticks = chart.tTicks();
    QVERIFY(ticks.size() >= 3);
    for (double value : ticks)
        QVERIFY(value > 0.0);

    const QPointF decadeOne = chart.toPixel(0.0, 1.0);
    const QPointF decadeTen = chart.toPixel(0.0, 10.0);
    const QPointF decadeHundred = chart.toPixel(0.0, 100.0);
    QVERIFY(!decadeOne.isNull());
    QVERIFY(!decadeTen.isNull());
    QVERIFY(!decadeHundred.isNull());

    const qreal firstGap = decadeOne.y() - decadeTen.y();
    const qreal secondGap = decadeTen.y() - decadeHundred.y();
    QVERIFY2(firstGap > 0.0 && secondGap > 0.0, "larger t must map higher on the screen");
    QVERIFY2(std::abs(firstGap - secondGap) < 2.0,
             "each decade must occupy the same number of pixels");

    // A non-positive t has no place on a log axis.
    QVERIFY(chart.toPixel(0.0, 0.0).isNull());
    QVERIFY(chart.toPixel(0.0, -1.0).isNull());

    const md::MellinSample sample = chart.sampleAt(
        chart.toPixel(0.0, std::sqrt(10.0)));
    QVERIFY(sample.valid);
    QVERIFY(sample.t > 0.0);
}

void TestMellinChart::paintsFieldAndColourBar()
{
    md::MellinChart chart;
    const Spectrum spectrum = makeLinearSpectrum();
    chart.setSpectrum(spectrum.c, spectrum.t, spectrum.values);
    chart.resize(520, 380);

    const QImage image = renderWidget(chart);
    const QColor background = md::Theme::baselineLight().colours().surface;

    const QRectF field = chart.fieldRect();
    const QPointF centre = chart.toPixel(0.0, 0.0);
    QVERIFY(field.contains(centre));
    QVERIFY2(colourDistance(pixelAt(image, centre), background) > 24,
             "the field must paint its centre cell");
    QVERIFY(pixelsDifferingFrom(image, background, field) > 500);

    const QRectF bar = chart.colourBarRect();
    QVERIFY(bar.isValid());
    QVERIFY(colourDistance(pixelAt(image, bar.center()), background) > 24);
}

void TestMellinChart::criticalLinePaints()
{
    md::MellinChart chart;
    const Spectrum spectrum = makeLinearSpectrum();
    chart.setSpectrum(spectrum.c, spectrum.t, spectrum.values);
    chart.setCriticalLine(0.5);
    chart.resize(520, 380);

    const QImage image = renderWidget(chart);
    const QRectF field = chart.fieldRect();
    const qreal x = chart.toPixel(0.5, 0.0).x();
    QVERIFY(field.contains(QPointF(x, field.center().y())));

    const QColor tertiary = md::Theme::baselineLight().colours().tertiary;
    QVERIFY2(hasColourNear(image, QPointF(x, field.center().y()), tertiary, 8),
             "the critical line must paint at c = 0.5");
}

void TestMellinChart::sampleAtMapsPixelsToST()
{
    md::MellinChart chart;
    const Spectrum spectrum = makeLinearSpectrum();
    chart.setSpectrum(spectrum.c, spectrum.t, spectrum.values);
    chart.resize(520, 380);

    const md::MellinSample sample = chart.sampleAt(chart.toPixel(0.5, 2.0));
    QVERIFY(sample.valid);
    QVERIFY(std::abs(sample.c - 0.5) < 0.25);
    QVERIFY(std::abs(sample.t - 2.0) < 0.5);

    const std::complex<double> expected = 1.0 / (std::complex<double>(sample.c, sample.t) + 1.0);
    QVERIFY(std::abs(sample.magnitude - std::abs(expected)) < 0.1);
    QVERIFY(std::abs(sample.phase - std::arg(expected)) < 0.2);

    QVERIFY(!chart.sampleAt(chart.fieldRect().topLeft() - QPointF(10.0, 10.0)).valid);
    QVERIFY(!chart.sampleAt(QPointF(-100.0, -100.0)).valid);
}

void TestMellinChart::emptyStatePaintsText()
{
    md::MellinChart chart;
    chart.resize(420, 320);

    const QImage image = renderWidget(chart);
    const QColor background = md::Theme::baselineLight().colours().surface;
    QVERIFY(pixelsDifferingFrom(image, background, QRectF()) > 40);
    QVERIFY(!chart.hasSpectrum());
}

void TestMellinChart::revealReachesOne()
{
    md::MellinChart chart;
    const Spectrum spectrum = makeLinearSpectrum();
    chart.setSpectrum(spectrum.c, spectrum.t, spectrum.values);
    chart.resize(480, 360);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.show();
    renderWidget(chart); // the first visible paint starts the reveal
    QVERIFY2(chart.revealProgress() < 1.0, "the default theme sweeps the field in");
    QVERIFY(chart.isRevealing());

    QTRY_VERIFY_WITH_TIMEOUT(!chart.isRevealing(), 5000);
    QCOMPARE(chart.revealProgress(), 1.0);
}

void TestMellinChart::reducedMotionSnaps()
{
    md::MellinChart chart;
    md::ThemeScope* scope = md::ThemeScope::install(&chart, reducedMotionTheme());
    QVERIFY(scope != nullptr);

    const Spectrum spectrum = makeLinearSpectrum();
    chart.setSpectrum(spectrum.c, spectrum.t, spectrum.values);
    chart.setCriticalLine(0.0);
    chart.resize(420, 320);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.show();
    renderWidget(chart);
    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(!chart.isRevealing());
}

QTEST_MAIN(TestMellinChart)
#include "tst_mellin_chart.moc"
