// Unit tests for the HilbertChart component.
//
// Offscreen rendering like the other chart suites. The analytic signal is
// checked numerically as well as visually: a pure cosine has a sine quadrature,
// a unit envelope and a linear unwrapped phase, which exercises the local
// DFT/IDFT routine without depending on any other module.
//
// Pinned contract: the derived arrays match the signal length, the transform
// recovers the quadrature/envelope/phase, the phase portrait and the envelope
// panel paint, the mode swaps the panel proportions, sampleAt() round-trips,
// the empty state paints text, and the reveal reaches 1 (or snaps under reduced
// motion).

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QTest>
#include <QWidget>

#include <md/components/hilbert_chart.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>
#include <md/tokens/motion.h>

#include <cmath>
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

int pixelsNear(const QImage& image, const QRectF& region, const QColor& target, int tolerance)
{
    int count = 0;
    const QRect area = region.toRect().intersected(image.rect());

    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            if (colourDistance(image.pixelColor(x, y), target) < tolerance)
                ++count;
        }
    }
    return count;
}

md::Theme reducedMotionTheme()
{
    md::Theme theme = md::Theme::baselineLight();
    md::MotionScheme motion = theme.motion();
    motion.reducedMotion = true;
    return theme.withMotion(motion);
}

// x[n] = cos(2π·cycles·n / count), a tone the analytic signal should recover
// exactly: quadrature = sin(...), envelope = 1, phase = linear.
std::vector<double> makeCosine(int count = 64, int cycles = 4)
{
    std::vector<double> signal;
    signal.reserve(static_cast<std::size_t>(count));

    for (int index = 0; index < count; ++index) {
        const double angle = 2.0 * kPi * static_cast<double>(cycles)
            * static_cast<double>(index) / static_cast<double>(count);
        signal.push_back(std::cos(angle));
    }
    return signal;
}

} // namespace

class TestHilbertChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void constructionAndMeasurement();
    void analyticSignalRecoversCosine();
    void colourMappingAndTicks();
    void modeSwapsPanelProportions();
    void paintsPortraitAndEnvelope();
    void sampleAtMapsPixelsToSamples();
    void emptyStatePaintsText();
    void revealReachesOne();
    void reducedMotionSnaps();
};

void TestHilbertChart::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestHilbertChart::constructionAndMeasurement()
{
    md::HilbertChart chart;
    QVERIFY(!chart.hasSignal());
    QVERIFY(chart.sizeHint().width() > 0);

    chart.resize(520, 380);
    QVERIFY(chart.plotRect().isValid());

    chart.setSignal(makeCosine());
    QVERIFY(chart.hasSignal());
    QCOMPARE(chart.signal().size(), std::size_t(64));
    QCOMPARE(chart.quadrature().size(), std::size_t(64));
    QCOMPARE(chart.envelope().size(), std::size_t(64));
    QCOMPARE(chart.phase().size(), std::size_t(64));
    QVERIFY(chart.mode() == md::HilbertMode::Quadrature);
    QCOMPARE(chart.sampleInterval(), 1.0);
    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(chart.quadratureRect().isValid());
    QVERIFY(chart.envelopeRect().isValid());
    QVERIFY(chart.colourBarRect().isValid());
    QVERIFY(chart.colourBarVisible());
    chart.setColourBarVisible(false);
    QVERIFY(!chart.colourBarVisible());
    QVERIFY(chart.colourBarRect().isEmpty());
    chart.setColourBarVisible(true);
}

void TestHilbertChart::analyticSignalRecoversCosine()
{
    md::HilbertChart chart;
    chart.setSignal(makeCosine(64, 4));

    const std::vector<double>& quadrature = chart.quadrature();
    const std::vector<double>& envelope = chart.envelope();
    const std::vector<double>& phase = chart.phase();

    // q[n] = sin(2π·4·n/64); the centre of the record is free of edge effects.
    for (int index = 8; index < 56; ++index) {
        const double angle = 2.0 * kPi * 4.0 * static_cast<double>(index) / 64.0;
        QVERIFY2(std::abs(quadrature.at(static_cast<std::size_t>(index)) - std::sin(angle)) < 0.05,
                 "the quadrature must recover sin() for a cosine input");
        QVERIFY2(std::abs(envelope.at(static_cast<std::size_t>(index)) - 1.0) < 0.05,
                 "the envelope of a unit cosine is one");
    }

    // The unwrapped phase advances by one radian step per sample.
    for (int index = 9; index < 56; ++index) {
        const double step = phase.at(static_cast<std::size_t>(index))
            - phase.at(static_cast<std::size_t>(index - 1));
        QVERIFY(std::abs(step - 2.0 * kPi * 4.0 / 64.0) < 0.05);
    }

    chart.setSignal({1.0, 2.0, 3.0});
    QVERIFY(!chart.hasSignal());
}

void TestHilbertChart::colourMappingAndTicks()
{
    md::HilbertChart chart;
    chart.setSignal(makeCosine(64, 4));
    chart.resize(520, 380);

    const QVector<double> timeTicks = chart.timeTicks();
    QVERIFY(timeTicks.size() >= 2);
    for (int index = 1; index < timeTicks.size(); ++index)
        QVERIFY(timeTicks.at(index) > timeTicks.at(index - 1));
    QVERIFY(std::abs(timeTicks.first()) < 1e-9);
    QVERIFY(timeTicks.last() <= 63.0 + 1e-9);
    QVERIFY(timeTicks.last() > 40.0);

    const QVector<double> amplitudeTicks = chart.amplitudeTicks();
    QVERIFY(amplitudeTicks.size() >= 2);
    for (int index = 0; index < amplitudeTicks.size(); ++index)
        QVERIFY(amplitudeTicks.at(index) >= 0.0);
    QVERIFY(amplitudeTicks.last() >= 1.0);

    const QVector<double> phaseTicks = chart.phaseTicks();
    QVERIFY(phaseTicks.size() >= 2);
    for (int index = 1; index < phaseTicks.size(); ++index)
        QVERIFY(phaseTicks.at(index) > phaseTicks.at(index - 1));

    const double first = phaseTicks.first();
    const double last = phaseTicks.last();
    const QColor low = chart.colourForPhase(first);
    const QColor high = chart.colourForPhase(last);
    QCOMPARE(low.alpha(), 255);
    QVERIFY2(colourDistance(low, high) > 24,
             "the phase ramp must separate the phase range endpoints");

    chart.setSampleInterval(0.5);
    QCOMPARE(chart.sampleInterval(), 0.5);
    QVERIFY(chart.timeTicks().last() <= 31.5 + 1e-9);
    QVERIFY(chart.timeTicks().last() > 20.0);
}

void TestHilbertChart::modeSwapsPanelProportions()
{
    md::HilbertChart chart;
    chart.setSignal(makeCosine());
    chart.resize(520, 380);

    QVERIFY(chart.mode() == md::HilbertMode::Quadrature);
    const qreal quadratureWide = chart.quadratureRect().width();
    const qreal envelopeNarrow = chart.envelopeRect().width();
    QVERIFY2(quadratureWide > envelopeNarrow,
             "Quadrature mode must emphasize the phase portrait");

    chart.setMode(md::HilbertMode::Envelope);
    QVERIFY(chart.mode() == md::HilbertMode::Envelope);
    QVERIFY2(chart.envelopeRect().width() > chart.quadratureRect().width(),
             "Envelope mode must emphasize the envelope panel");
}

void TestHilbertChart::paintsPortraitAndEnvelope()
{
    md::HilbertChart chart;
    chart.setSignal(makeCosine());
    chart.resize(520, 380);

    const QImage image = renderWidget(chart);
    const QColor background = md::Theme::baselineLight().colours().surface;
    const QRectF quadrature = chart.quadratureRect();
    const QRectF envelope = chart.envelopeRect();
    QVERIFY(quadrature.isValid());
    QVERIFY(envelope.isValid());

    const QColor primary = md::Theme::baselineLight().colours().primary;
    const QColor secondary = md::Theme::baselineLight().colours().secondary;
    const QColor tertiary = md::Theme::baselineLight().colours().tertiary;

    const int portraitPixels = pixelsNear(image, quadrature, primary, 90)
        + pixelsNear(image, quadrature, secondary, 90)
        + pixelsNear(image, quadrature, tertiary, 90);
    QVERIFY2(portraitPixels > 20, "the phase portrait must paint a visible trajectory");

    const QColor envelopeColour = chart.seriesColour(0);
    QVERIFY(envelopeColour.isValid());

    QVERIFY2(pixelsNear(image, envelope, envelopeColour, 140) > 20,
             "the envelope panel must paint its curve");

    // The phase-colour bar repeats the portrait's mapping.
    const QRectF bar = chart.colourBarRect();
    QVERIFY(bar.isValid());
    QVERIFY2(colourDistance(pixelAt(image, bar.center()), background) > 24,
             "the phase colour bar must paint a gradient");

    // The two panels occupy disjoint rectangles.
    QVERIFY(!quadrature.intersects(envelope));
}

void TestHilbertChart::sampleAtMapsPixelsToSamples()
{
    md::HilbertChart chart;
    chart.setSignal(makeCosine());
    chart.resize(520, 380);

    const md::HilbertSample fromEnvelope = chart.sampleAt(
        chart.toEnvelopePixel(10.0, chart.envelope().at(10)));
    QVERIFY(fromEnvelope.valid);
    QCOMPARE(fromEnvelope.index, 10);
    QVERIFY(std::abs(fromEnvelope.time - 10.0) < 1e-9);

    const md::HilbertSample fromPortrait = chart.sampleAt(
        chart.toQuadraturePixel(chart.signal().at(3), chart.quadrature().at(3)));
    QVERIFY(fromPortrait.valid);
    QCOMPARE(fromPortrait.index, 3);
    QVERIFY(std::abs(fromPortrait.envelope - 1.0) < 0.05);

    QVERIFY(!chart.sampleAt(QPointF(-100.0, -100.0)).valid);
    QVERIFY(!chart.sampleAt(chart.plotRect().topLeft() + QPointF(1.0, 1.0)).valid);
}

void TestHilbertChart::emptyStatePaintsText()
{
    md::HilbertChart chart;
    chart.resize(420, 320);

    const QImage image = renderWidget(chart);
    const QColor background = md::Theme::baselineLight().colours().surface;
    QVERIFY(pixelsDifferingFrom(image, background, QRectF()) > 40);
    QVERIFY(!chart.hasSignal());

    chart.setSignal({1.0, 2.0});
    QVERIFY(!chart.hasSignal());
    QVERIFY(chart.quadrature().empty());
    QVERIFY(chart.phase().empty());
}

void TestHilbertChart::revealReachesOne()
{
    md::HilbertChart chart;
    chart.setSignal(makeCosine());
    chart.resize(480, 360);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.show();
    renderWidget(chart); // the first visible paint starts the reveal
    QVERIFY2(chart.revealProgress() < 1.0, "the default theme traces the signal in");
    QVERIFY(chart.isRevealing());

    QTRY_VERIFY_WITH_TIMEOUT(!chart.isRevealing(), 5000);
    QCOMPARE(chart.revealProgress(), 1.0);
}

void TestHilbertChart::reducedMotionSnaps()
{
    md::HilbertChart chart;
    md::ThemeScope* scope = md::ThemeScope::install(&chart, reducedMotionTheme());
    QVERIFY(scope != nullptr);

    chart.setSignal(makeCosine());
    chart.resize(420, 320);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.show();
    const QImage image = renderWidget(chart);
    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(!chart.isRevealing());
    const QColor background = md::Theme::baselineLight().colours().surface;
    QVERIFY(pixelsDifferingFrom(image, background, chart.quadratureRect()) > 20);
}

QTEST_MAIN(TestHilbertChart)
#include "tst_hilbert_chart.moc"
