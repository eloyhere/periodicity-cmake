// Unit tests for the PieChart component (framework.md §5.9, §14).
//
// The widget is exercised offscreen like the other component suites: the
// application theme is pinned to baseline light and widgets are rendered into an
// explicitly transparent QImage with QWidget::render, so the assertions look at
// what the component itself painted. Pointer input is delivered with
// QApplication::sendEvent; no window activation and no virtual cursor is needed.
//
// The tests pin the contract the header documents: 3 slices close the circle,
// the 5 % percentage threshold is both reported and painted, a donut hole shows
// the background, sliceAt() maps angle+radius (and rejects the outside and the
// hole), reduced motion snaps the reveal synchronously, RTL mirrors the sweep,
// empty data paints the themed state instead of crashing, and a throwing
// paintPlot() never unwinds into Qt.

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QRegion>
#include <QSignalSpy>
#include <QSize>
#include <QTest>
#include <QWidget>
#include <Qt>

#include <md/components/pie_chart.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>
#include <md/tokens/motion.h>

#include <cmath>
#include <stdexcept>

namespace {

constexpr qreal kPi = 3.14159265358979323846;

const QColor kRed(198, 40, 40);
const QColor kGreen(46, 125, 50);
const QColor kBlue(21, 101, 192);

// A slice is one ChartSeries whose value is points[0].y().
md::ChartSeries makeSlice(const QString& name, qreal value, const QColor& colour)
{
    md::ChartSeries series;
    series.name = name;
    series.points.append(QPointF(0.0, value)); // y is the slice value; x is ignored
    series.colour = colour;
    return series;
}

// A widget-local point on the circle at `degrees` clockwise from twelve
// o'clock, which is the angle convention the component documents.
QPointF pointAt(const QPointF& centre, qreal degrees, qreal radius)
{
    const qreal radians = degrees * kPi / 180.0;
    return QPointF(centre.x() + std::sin(radians) * radius,
                   centre.y() - std::cos(radians) * radius);
}

void sendMouse(QWidget& widget, QEvent::Type type, const QPointF& position,
               Qt::MouseButton button, Qt::MouseButtons buttons)
{
    const QPoint global = widget.mapToGlobal(position.toPoint());
    QMouseEvent event(type, position, QPointF(global), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

void clickAt(QWidget& widget, const QPointF& position)
{
    sendMouse(widget, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton);
    sendMouse(widget, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
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

// How many pixels differ visibly from `background`: the empty state has no
// geometry to measure, so its painted label is verified statistically.
int pixelsDifferingFrom(const QImage& image, const QColor& background)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            const int delta = qAbs(pixel.red() - background.red())
                + qAbs(pixel.green() - background.green())
                + qAbs(pixel.blue() - background.blue())
                + qAbs(pixel.alpha() - background.alpha());
            if (delta > 24)
                ++count;
        }
    }
    return count;
}

} // namespace

class TestPieChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void slicesCoverFullCircle();
    void percentagesOnlyOnWideSlices();
    void percentagesPaintWhenEnabled();
    void donutHoleShowsBackground();
    void sliceAtMapsAngleAndRadius();
    void clickActivatesSlice();
    void reducedMotionSnaps();
    void revealAnimationRuns();
    void rtlMirrorsTheSweep();
    void emptyDataPaintsState();
    void throwingPaintPlotDoesNotCrash();
};

void TestPieChart::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestPieChart::slicesCoverFullCircle()
{
    md::PieChart chart;
    chart.setSeries({makeSlice(QStringLiteral("A"), 25.0, kRed),
                     makeSlice(QStringLiteral("B"), 50.0, kGreen),
                     makeSlice(QStringLiteral("C"), 25.0, kBlue)});

    QCOMPARE(chart.sliceCount(), 3);

    qreal total = 0.0;
    for (int index = 0; index < 3; ++index)
        total += chart.sliceSpan(index);

    QVERIFY2(qAbs(total - 360.0) < 0.5, "three slices must close the circle");
    QVERIFY2(qAbs(chart.sliceSpan(0) - 90.0) < 0.5, "25 % is a quarter turn");
    QVERIFY2(qAbs(chart.sliceSpan(1) - 180.0) < 0.5, "50 % is a half turn");
    QVERIFY2(qAbs(chart.sliceSpan(2) - 90.0) < 0.5, "25 % is a quarter turn");

    // A series that carries no angle keeps its index, so later slices do not
    // shift and the index the signal reports still addresses the series.
    chart.setSeries({makeSlice(QStringLiteral("empty"), 0.0, kRed),
                     makeSlice(QStringLiteral("negative"), -5.0, kBlue),
                     makeSlice(QStringLiteral("real"), 10.0, kGreen)});

    QCOMPARE(chart.sliceCount(), 1);
    QCOMPARE(chart.sliceSpan(0), 0.0);
    QCOMPARE(chart.sliceSpan(1), 0.0);
    QVERIFY2(qAbs(chart.sliceSpan(2) - 360.0) < 0.5, "the only slice takes the whole circle");
}

void TestPieChart::percentagesOnlyOnWideSlices()
{
    md::PieChart chart;
    chart.setSeries({makeSlice(QStringLiteral("big"), 96.0, kRed),
                     makeSlice(QStringLiteral("tiny-a"), 2.0, kGreen),
                     makeSlice(QStringLiteral("tiny-b"), 2.0, kBlue)});

    QVERIFY(chart.showsPercentages());
    QCOMPARE(chart.sliceLabel(0), QStringLiteral("96%"));
    QVERIFY2(chart.sliceLabel(1).isEmpty(), "a 2 % slice is below the 5 % threshold");
    QVERIFY2(chart.sliceLabel(2).isEmpty(), "a 2 % slice is below the 5 % threshold");

    chart.setShowPercentages(false);
    QVERIFY2(chart.sliceLabel(0).isEmpty(), "the threshold does not override the switch");
}

void TestPieChart::percentagesPaintWhenEnabled()
{
    md::PieChart chart;
    chart.setSeries({makeSlice(QStringLiteral("big"), 96.0, kRed),
                     makeSlice(QStringLiteral("tiny-a"), 2.0, kGreen),
                     makeSlice(QStringLiteral("tiny-b"), 2.0, kBlue)});
    chart.resize(320, 320);

    chart.setShowPercentages(true);
    const QImage withLabels = renderWidget(chart);
    chart.setShowPercentages(false);
    const QImage withoutLabels = renderWidget(chart);

    QVERIFY2(withLabels != withoutLabels,
             "enabling percentages must change the painted pixels of the wide slice");
}

void TestPieChart::donutHoleShowsBackground()
{
    md::PieChart chart;
    chart.setDonut(true);
    chart.setDonutRatio(0.55);
    chart.setShowPercentages(false);
    chart.setSeries({makeSlice(QStringLiteral("A"), 25.0, kRed),
                     makeSlice(QStringLiteral("B"), 25.0, kGreen),
                     makeSlice(QStringLiteral("C"), 25.0, kBlue),
                     makeSlice(QStringLiteral("D"), 25.0, kRed)});
    chart.resize(320, 320);

    const QRectF pie = chart.pieRect();
    QVERIFY(pie.isValid());
    QVERIFY(pie.width() > 40.0);

    const QImage image = renderWidget(chart);
    const QPointF centre = pie.center();

    // The background reference is a plot corner: inside the widget, outside the
    // disc, and never covered by a slice.
    QRectF plot = chart.plotRect();
    if (!plot.isValid())
        plot = QRectF(chart.rect());
    const QColor background = pixelAt(image, plot.topLeft() + QPointF(4.0, 4.0));

    const QColor hole = pixelAt(image, centre);
    QCOMPARE(hole.rgb(), background.rgb());
    QCOMPARE(hole.alpha(), background.alpha());

    // The ring around the hole is still painted in the slice colour. The sample
    // sits away from the mid-angle label position and inside the ring.
    const QPointF ring = pointAt(centre, 45.0, pie.width() * 0.5 * 0.9);
    QCOMPARE(pixelAt(image, ring).rgb(), kRed.rgb());
}

void TestPieChart::sliceAtMapsAngleAndRadius()
{
    md::PieChart chart;
    // 0..90 clockwise is slice 0, 90..270 is slice 1, 270..360 is slice 2.
    chart.setSeries({makeSlice(QStringLiteral("A"), 25.0, kRed),
                     makeSlice(QStringLiteral("B"), 50.0, kGreen),
                     makeSlice(QStringLiteral("C"), 25.0, kBlue)});
    chart.resize(320, 320);

    const QRectF pie = chart.pieRect();
    QVERIFY(pie.isValid());
    const QPointF centre = pie.center();
    const qreal radius = pie.width() / 2.0;

    QCOMPARE(chart.sliceAt(pointAt(centre, 45.0, radius * 0.6)), 0);
    QCOMPARE(chart.sliceAt(pointAt(centre, 180.0, radius * 0.6)), 1);
    QCOMPARE(chart.sliceAt(pointAt(centre, 300.0, radius * 0.6)), 2);
    QCOMPARE(chart.sliceAt(pointAt(centre, 359.0, radius * 0.6)), 2);

    QCOMPARE(chart.sliceAt(pointAt(centre, 45.0, radius * 1.3)), -1);
    QCOMPARE(chart.sliceAt(pointAt(centre, 45.0, radius * 1.05)), -1);

    // A donut hole is not a slice either.
    chart.setDonut(true);
    chart.setDonutRatio(0.55);
    QCOMPARE(chart.sliceAt(centre), -1);
    QCOMPARE(chart.sliceAt(pointAt(centre, 45.0, radius * 0.2)), -1);
    QCOMPARE(chart.sliceAt(pointAt(centre, 45.0, radius * 0.8)), 0);
}

void TestPieChart::clickActivatesSlice()
{
    md::PieChart chart;
    chart.setSeries({makeSlice(QStringLiteral("A"), 25.0, kRed),
                     makeSlice(QStringLiteral("B"), 50.0, kGreen),
                     makeSlice(QStringLiteral("C"), 25.0, kBlue)});
    chart.resize(320, 320);

    const QRectF pie = chart.pieRect();
    QVERIFY(pie.isValid());
    const QPointF centre = pie.center();
    const qreal radius = pie.width() / 2.0;

    QSignalSpy spy(&chart, &md::PieChart::sliceActivated);

    clickAt(chart, pointAt(centre, 180.0, radius * 0.6));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 1);
    QCOMPARE(chart.selectedSlice(), 1);

    // A click outside the disc activates nothing.
    clickAt(chart, pointAt(centre, 180.0, radius * 1.5));
    QCOMPARE(spy.count(), 1);
}

void TestPieChart::reducedMotionSnaps()
{
    md::PieChart chart;

    md::MotionScheme motion;
    motion.reducedMotion = true;
    md::ThemeScope* scope = md::ThemeScope::install(
        &chart, md::Theme::baselineLight().withMotion(motion));
    QVERIFY(scope != nullptr);

    chart.setSeries({makeSlice(QStringLiteral("A"), 25.0, kRed),
                     makeSlice(QStringLiteral("B"), 50.0, kGreen),
                     makeSlice(QStringLiteral("C"), 25.0, kBlue)});
    chart.resize(300, 300);

    // A chart that was never shown paints finished, so an offscreen render is
    // complete rather than blank.
    QCOMPARE(chart.revealProgress(), 1.0);

    chart.show();

    // Reduced motion snaps synchronously: no frame ever shows a partial sweep.
    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(!chart.isRevealing());

    const QImage image = renderWidget(chart);
    const QRectF pie = chart.pieRect();
    QVERIFY(pie.isValid());
    const QPointF sample = pointAt(pie.center(), 45.0, pie.width() * 0.5 * 0.4);
    QCOMPARE(pixelAt(image, sample).rgb(), kRed.rgb());
}

void TestPieChart::revealAnimationRuns()
{
    md::PieChart chart;
    chart.setSeries({makeSlice(QStringLiteral("A"), 25.0, kRed),
                     makeSlice(QStringLiteral("B"), 50.0, kGreen),
                     makeSlice(QStringLiteral("C"), 25.0, kBlue)});
    chart.resize(300, 300);
    QCOMPARE(chart.revealProgress(), 1.0);

    chart.show();

    QVERIFY2(chart.revealProgress() < 1.0, "the default theme animates the sweep in");
    QVERIFY(chart.isRevealing());

    QTRY_VERIFY_WITH_TIMEOUT(!chart.isRevealing(), 5000);
    QCOMPARE(chart.revealProgress(), 1.0);
}

void TestPieChart::rtlMirrorsTheSweep()
{
    md::PieChart chart;
    chart.setLayoutDirection(Qt::RightToLeft);
    chart.setSeries({makeSlice(QStringLiteral("A"), 25.0, kRed),
                     makeSlice(QStringLiteral("B"), 50.0, kGreen),
                     makeSlice(QStringLiteral("C"), 25.0, kBlue)});
    chart.resize(320, 320);

    const QRectF pie = chart.pieRect();
    QVERIFY(pie.isValid());
    const QPointF centre = pie.center();
    const qreal radius = pie.width() / 2.0;

    // Counter-clockwise from twelve: slice 0 occupies 270..360, slice 1
    // occupies 90..270 and slice 2 occupies 0..90.
    QCOMPARE(chart.sliceAt(pointAt(centre, 300.0, radius * 0.6)), 0);
    QCOMPARE(chart.sliceAt(pointAt(centre, 180.0, radius * 0.6)), 1);
    QCOMPARE(chart.sliceAt(pointAt(centre, 45.0, radius * 0.6)), 2);
}

void TestPieChart::emptyDataPaintsState()
{
    md::PieChart chart;
    chart.resize(320, 200);

    QCOMPARE(chart.sliceCount(), 0);
    QCOMPARE(chart.sliceSpan(0), 0.0);
    QVERIFY(chart.sliceLabel(0).isEmpty());
    QCOMPARE(chart.sliceAt(QPointF(160.0, 100.0)), -1);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    const QColor background = pixelAt(image, QPointF(2.0, 2.0));
    QVERIFY2(pixelsDifferingFrom(image, background) > 0,
             "the empty state must paint its themed label");
}

void TestPieChart::throwingPaintPlotDoesNotCrash()
{
    class ThrowingPieChart : public md::PieChart
    {
    public:
        using md::PieChart::PieChart;

    protected:
        void paintPlot(QPainter&, const QRectF&) override
        {
            throw std::runtime_error("pie paint failure");
        }
    };

    ThrowingPieChart chart;
    chart.setSeries({makeSlice(QStringLiteral("A"), 25.0, kRed)});
    chart.resize(240, 240);

    const QImage first = renderWidget(chart);
    QVERIFY(!first.isNull());

    // A second frame proves the failed paint did not corrupt the widget state.
    const QImage second = renderWidget(chart);
    QVERIFY(!second.isNull());
}

QTEST_MAIN(TestPieChart)
#include "tst_pie_chart.moc"
