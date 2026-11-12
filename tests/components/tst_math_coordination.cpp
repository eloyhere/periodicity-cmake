// Unit tests for MathematicsCoordination and the ChartBase contract
// (framework.md §14).
//
// The tests are headless: the widget is sized through the framework's
// arrange() entry point, no window is shown, and the theme is pinned with a
// ThemeScope so a machine's application theme cannot influence the result.
//
// The transform is asserted analytically, not by pixels: toData(toPixel(p))
// must return p, the Y axis must run upwards, the anchor of a zoom must not
// move, and a pan must translate every point by the requested pixel delta.
// niceTicks() is exercised for the four shapes the contract names, including
// the single-value range.
//
// Rendering is only used where the contract is about “must not throw”: a
// subclass whose paint hook throws must still leave the widget paintable, both
// through ChartBase's internal catch and through View's paint guard.

#include <md/components/chart_common.h>
#include <md/components/math_coordination.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>
#include <md/tokens/motion.h>

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QRegion>
#include <QSize>
#include <QTest>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {

bool nearly(qreal actual, qreal expected, qreal tolerance = 1.0e-9)
{
    const qreal scale = std::max<qreal>(
        1.0, std::max(std::abs(actual), std::abs(expected)));
    return std::abs(actual - expected) <= tolerance * scale;
}

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

// A coordination widget whose paint hook throws. View's guard must catch the
// exception, record it in lastError() and keep the next paint working.
class ThrowingCoordination : public md::MathematicsCoordination
{
public:
    ThrowingCoordination() = default;

protected:
    void paintContent(QPainter& painter, const QRectF& content) override
    {
        Q_UNUSED(painter);
        Q_UNUSED(content);
        throw std::runtime_error("coordination paint boom");
    }
};

// A chart whose plot hook throws. ChartBase catches it inside paintContent(),
// so View::lastError() must stay empty and the widget must stay paintable.
class ThrowingChart : public md::ChartBase
{
public:
    ThrowingChart() = default;

protected:
    void paintPlot(QPainter& painter, const QRectF& plot) override
    {
        Q_UNUSED(painter);
        Q_UNUSED(plot);
        throw std::runtime_error("plot boom");
    }
};

// A chart with no marks, used to exercise the shared furniture and hitTest().
class TestChart : public md::ChartBase
{
public:
    TestChart() = default;

protected:
    void paintPlot(QPainter& painter, const QRectF& plot) override
    {
        Q_UNUSED(painter);
        Q_UNUSED(plot);
    }
};

} // namespace

class TestMathematicsCoordination : public QObject
{
    Q_OBJECT

private slots:
    void transformRoundTrip();
    void yAxisIsFlipped();
    void niceTickValues();
    void autoFitChangesTransform();
    void reducedMotionSnaps();
    void zoomKeepsAnchorFixed();
    void zoomClampAndInvalidFactor();
    void panShiftsViewport();
    void boundsValidationAndUnitSpan();
    void manualBoundsSurviveDataChanges();
    void seriesBoundsPaletteAndHitTest();
    void throwingOverrideLeavesWidgetPaintable();
};

void TestMathematicsCoordination::transformRoundTrip()
{
    md::MathematicsCoordination coordinates;
    [[maybe_unused]] md::ThemeScope* scope =
        md::ThemeScope::install(&coordinates, reducedMotionTheme());
    coordinates.arrange(QRect(0, 0, 480, 320));
    coordinates.setVisibleBounds(QPointF(-5.0, -5.0), QPointF(5.0, 5.0));

    const QRectF plot = coordinates.plotArea();
    QVERIFY(!plot.isEmpty());
    QVERIFY(plot.width() > 100.0);
    QVERIFY(plot.height() > 100.0);

    const QVector<QPointF> samples = {
        QPointF(-5.0, -5.0),
        QPointF(0.0, 0.0),
        QPointF(5.0, 5.0),
        QPointF(2.5, -3.25),
        QPointF(-1.0, 4.75),
    };

    for (const QPointF& data : samples) {
        const QPointF roundTrip = coordinates.toData(coordinates.toPixel(data));
        QVERIFY2(nearly(roundTrip.x(), data.x()),
                 qPrintable(QStringLiteral("x round-trip for %1").arg(data.x())));
        QVERIFY2(nearly(roundTrip.y(), data.y()),
                 qPrintable(QStringLiteral("y round-trip for %1").arg(data.y())));
    }

    // The visible range maps exactly onto the plot rectangle.
    const QPointF bottomLeft = coordinates.toPixel(QPointF(-5.0, -5.0));
    const QPointF topRight = coordinates.toPixel(QPointF(5.0, 5.0));

    QVERIFY(nearly(bottomLeft.x(), plot.left()));
    QVERIFY(nearly(bottomLeft.y(), plot.bottom()));
    QVERIFY(nearly(topRight.x(), plot.right()));
    QVERIFY(nearly(topRight.y(), plot.top()));
}

void TestMathematicsCoordination::yAxisIsFlipped()
{
    md::MathematicsCoordination coordinates;
    [[maybe_unused]] md::ThemeScope* scope =
        md::ThemeScope::install(&coordinates, reducedMotionTheme());
    coordinates.arrange(QRect(0, 0, 400, 300));
    coordinates.setVisibleBounds(QPointF(0.0, 0.0), QPointF(10.0, 10.0));

    const QPointF dataOrigin = coordinates.toPixel(QPointF(0.0, 0.0));
    const QPointF dataTop = coordinates.toPixel(QPointF(0.0, 10.0));
    const QPointF dataRight = coordinates.toPixel(QPointF(10.0, 0.0));

    // Data +Y is up: the minimum becomes the bottom pixel row.
    QVERIFY(dataOrigin.y() > dataTop.y());
    QVERIFY(nearly(dataOrigin.y() - dataTop.y(), coordinates.plotArea().height()));

    // X is not flipped.
    QVERIFY(dataRight.x() > dataOrigin.x());
    QVERIFY(nearly(dataRight.x() - dataOrigin.x(), coordinates.plotArea().width()));
}

void TestMathematicsCoordination::niceTickValues()
{
    // 1..9 with six requested ticks: the step is 2 and the ticks inside the
    // range are 2, 4, 6 and 8.
    const QVector<qreal> integers = md::MathematicsCoordination::niceTicks(1.0, 9.0, 6);
    QCOMPARE(integers.size(), 4);
    QVERIFY(nearly(integers.at(0), 2.0));
    QVERIFY(nearly(integers.at(1), 4.0));
    QVERIFY(nearly(integers.at(2), 6.0));
    QVERIFY(nearly(integers.at(3), 8.0));

    // 0..0.1 with six requested ticks: the step is 0.02 and both endpoints are
    // ticks, so there are six values.
    const QVector<qreal> decimals = md::MathematicsCoordination::niceTicks(0.0, 0.1, 6);
    QCOMPARE(decimals.size(), 6);
    for (int index = 0; index < decimals.size(); ++index)
        QVERIFY(nearly(decimals.at(index), 0.02 * static_cast<qreal>(index)));

    // -5..5: the step is 2 and zero is a tick.
    const QVector<qreal> signedTicks = md::MathematicsCoordination::niceTicks(-5.0, 5.0, 6);
    QCOMPARE(signedTicks.size(), 5);
    QVERIFY(nearly(signedTicks.at(0), -4.0));
    QVERIFY(nearly(signedTicks.at(1), -2.0));
    QVERIFY(nearly(signedTicks.at(2), 0.0));
    QVERIFY(nearly(signedTicks.at(3), 2.0));
    QVERIFY(nearly(signedTicks.at(4), 4.0));

    // A single value keeps one tick. A reversed range is normalised; invalid
    // input and a non-positive count are empty rather than NaN-filled.
    const QVector<qreal> single = md::MathematicsCoordination::niceTicks(3.0, 3.0, 6);
    QCOMPARE(single.size(), 1);
    QVERIFY(nearly(single.at(0), 3.0));

    QVERIFY(md::MathematicsCoordination::niceTicks(9.0, 1.0, 6) == integers);
    QVERIFY(md::MathematicsCoordination::niceTicks(
                std::numeric_limits<qreal>::quiet_NaN(), 1.0, 6).isEmpty());
    QVERIFY(md::MathematicsCoordination::niceTicks(
                0.0, std::numeric_limits<qreal>::infinity(), 6).isEmpty());
    QVERIFY(md::MathematicsCoordination::niceTicks(0.0, 1.0, 0).isEmpty());
}

void TestMathematicsCoordination::autoFitChangesTransform()
{
    md::MathematicsCoordination coordinates;
    [[maybe_unused]] md::ThemeScope* scope =
        md::ThemeScope::install(&coordinates, reducedMotionTheme());
    coordinates.arrange(QRect(0, 0, 480, 320));

    QVERIFY(coordinates.isAutoFit());

    // 1..9 fits to 0..10: the raw range is expanded to a 1/2/5 boundary, which
    // is exactly what auto-fit promises.
    coordinates.setDataBounds(QPointF(1.0, 1.0), QPointF(9.0, 9.0));

    QVERIFY(!coordinates.isAnimating());
    QVERIFY(nearly(coordinates.visibleBounds().left(), 0.0));
    QVERIFY(nearly(coordinates.visibleBounds().top(), 0.0));
    QVERIFY(nearly(coordinates.visibleBounds().right(), 10.0));
    QVERIFY(nearly(coordinates.visibleBounds().bottom(), 10.0));
    QVERIFY(nearly(coordinates.fittedBounds().right(), 10.0));

    // dataBounds() stays raw: the fit changes the view, not the data.
    QVERIFY(nearly(coordinates.dataBounds().left(), 1.0));
    QVERIFY(nearly(coordinates.dataBounds().right(), 9.0));

    const QRectF plot = coordinates.plotArea();
    const QPointF pixel = coordinates.toPixel(QPointF(1.0, 1.0));
    QVERIFY(nearly(pixel.x(), plot.left() + plot.width() * 0.1));
    QVERIFY(nearly(pixel.y(), plot.bottom() - plot.height() * 0.1));

    // Disabling auto-fit leaves manual mode at the fitted range, and a manual
    // range then survives a data update (the viewport is not re-fitted).
    coordinates.setAutoFit(false);
    const QRectF manual = coordinates.visibleBounds();
    QVERIFY(nearly(manual.width(), 10.0));

    coordinates.setDataBounds(QPointF(0.0, 0.0), QPointF(100.0, 100.0));
    QVERIFY(coordinates.visibleBounds() == manual);
    QVERIFY(nearly(coordinates.dataBounds().right(), 100.0));

    // Re-enabling auto-fit fits the new data (reduced motion snaps).
    coordinates.setAutoFit(true);
    QVERIFY(coordinates.isAutoFit());
    QVERIFY(nearly(coordinates.visibleBounds().right(), 100.0));
    QVERIFY(nearly(coordinates.visibleBounds().left(), 0.0));
}

void TestMathematicsCoordination::reducedMotionSnaps()
{
    md::MathematicsCoordination snapping;
    [[maybe_unused]] md::ThemeScope* snappingScope =
        md::ThemeScope::install(&snapping, reducedMotionTheme());
    snapping.arrange(QRect(0, 0, 480, 320));
    snapping.setDataBounds(QPointF(1.0, 1.0), QPointF(9.0, 9.0));

    QVERIFY(!snapping.isAnimating());
    QVERIFY(nearly(snapping.visibleBounds().right(), 10.0));

    // The same update under the baseline motion scheme animates: the visible
    // range is still the old 0..1 immediately after the call, and reaches the
    // fitted target when the animator finishes.
    md::MathematicsCoordination animating;
    [[maybe_unused]] md::ThemeScope* animatingScope =
        md::ThemeScope::install(&animating, md::Theme::baselineLight());
    animating.arrange(QRect(0, 0, 480, 320));
    animating.setDataBounds(QPointF(1.0, 1.0), QPointF(9.0, 9.0));

    QVERIFY(animating.isAnimating());
    QVERIFY(!nearly(animating.visibleBounds().right(),
                    animating.fittedBounds().right(), 1.0e-6));

    QTRY_VERIFY_WITH_TIMEOUT(!animating.isAnimating(), 3000);
    QVERIFY(nearly(animating.visibleBounds().right(), animating.fittedBounds().right()));
    QVERIFY(nearly(animating.visibleBounds().left(), animating.fittedBounds().left()));
}

void TestMathematicsCoordination::zoomKeepsAnchorFixed()
{
    md::MathematicsCoordination coordinates;
    [[maybe_unused]] md::ThemeScope* scope =
        md::ThemeScope::install(&coordinates, reducedMotionTheme());
    coordinates.arrange(QRect(0, 0, 480, 320));
    coordinates.setDataBounds(QPointF(0.0, 0.0), QPointF(10.0, 10.0));
    QVERIFY(coordinates.isAutoFit());

    const QRectF plot = coordinates.plotArea();
    const QPointF anchor(plot.center());
    const QPointF before = coordinates.toData(anchor);

    coordinates.zoomAt(anchor, 2.0);

    // Zooming takes manual control and the data under the anchor does not move.
    QVERIFY(!coordinates.isAutoFit());
    const QPointF after = coordinates.toData(anchor);
    QVERIFY(nearly(after.x(), before.x(), 1.0e-7));
    QVERIFY(nearly(after.y(), before.y(), 1.0e-7));

    // A factor of 2 halves both spans.
    QVERIFY(nearly(coordinates.visibleBounds().width(), 5.0));
    QVERIFY(nearly(coordinates.visibleBounds().height(), 5.0));

    // The anchor may be off-centre too: the invariant is about the data point,
    // not about the middle of the plot.
    coordinates.setVisibleBounds(QPointF(0.0, 0.0), QPointF(100.0, 100.0));
    const QPointF offCentre(plot.left() + plot.width() * 0.25,
                            plot.top() + plot.height() * 0.75);
    const QPointF dataBefore = coordinates.toData(offCentre);
    coordinates.zoomAt(offCentre, 0.5);
    const QPointF dataAfter = coordinates.toData(offCentre);
    QVERIFY(nearly(dataAfter.x(), dataBefore.x(), 1.0e-7));
    QVERIFY(nearly(dataAfter.y(), dataBefore.y(), 1.0e-7));
    QVERIFY(nearly(coordinates.visibleBounds().width(), 200.0));
}

void TestMathematicsCoordination::zoomClampAndInvalidFactor()
{
    md::MathematicsCoordination coordinates;
    [[maybe_unused]] md::ThemeScope* scope =
        md::ThemeScope::install(&coordinates, reducedMotionTheme());
    coordinates.arrange(QRect(0, 0, 480, 320));
    coordinates.setVisibleBounds(QPointF(0.0, 0.0), QPointF(100.0, 100.0));

    const QPointF anchor = coordinates.plotArea().center();

    // The factor is clamped to 1e3: 100 / 1000 = 0.1.
    coordinates.zoomAt(anchor, 1.0e9);
    QVERIFY(nearly(coordinates.visibleBounds().width(), 0.1, 1.0e-6));
    QVERIFY(nearly(coordinates.visibleBounds().height(), 0.1, 1.0e-6));

    // The lower clamp is 1e-3: 0.1 / 0.001 = 100.
    coordinates.zoomAt(anchor, 1.0e-9);
    QVERIFY(nearly(coordinates.visibleBounds().width(), 100.0, 1.0e-6));

    // A non-positive or non-finite factor is rejected without disturbing the
    // transform or the auto-fit mode.
    const QRectF before = coordinates.visibleBounds();
    const bool autoFitBefore = coordinates.isAutoFit();

    coordinates.zoomAt(anchor, 0.0);
    QVERIFY(coordinates.visibleBounds() == before);
    QCOMPARE(coordinates.isAutoFit(), autoFitBefore);

    coordinates.zoomAt(anchor, -2.0);
    QVERIFY(coordinates.visibleBounds() == before);

    coordinates.zoomAt(anchor, std::numeric_limits<qreal>::infinity());
    QVERIFY(coordinates.visibleBounds() == before);
}

void TestMathematicsCoordination::panShiftsViewport()
{
    md::MathematicsCoordination coordinates;
    [[maybe_unused]] md::ThemeScope* scope =
        md::ThemeScope::install(&coordinates, reducedMotionTheme());
    coordinates.arrange(QRect(0, 0, 480, 320));
    coordinates.setDataBounds(QPointF(0.0, 0.0), QPointF(10.0, 10.0));
    QVERIFY(coordinates.isAutoFit());

    const QRectF plot = coordinates.plotArea();
    const qreal startLeft = coordinates.visibleBounds().left();
    const qreal startTop = coordinates.visibleBounds().top();

    const QPointF dataPoint(5.0, 5.0);
    const QPointF pixelBefore = coordinates.toPixel(dataPoint);

    const qreal deltaX = 10.0;
    const qreal deltaY = 5.0;
    coordinates.pan(QPointF(deltaX, deltaY));

    // Panning takes manual control, keeps the span and shifts the data-space
    // origin opposite to the pixel delta on X and with it on Y (the flip).
    QVERIFY(!coordinates.isAutoFit());
    QVERIFY(nearly(coordinates.visibleBounds().width(), 10.0));
    QVERIFY(nearly(coordinates.visibleBounds().height(), 10.0));
    QVERIFY(nearly(coordinates.visibleBounds().left(),
                   startLeft - deltaX / plot.width() * 10.0));
    QVERIFY(nearly(coordinates.visibleBounds().top(),
                   startTop + deltaY / plot.height() * 10.0));

    // Every plotted point moves by exactly the pixel delta.
    const QPointF pixelAfter = coordinates.toPixel(dataPoint);
    QVERIFY(nearly(pixelAfter.x() - pixelBefore.x(), deltaX));
    QVERIFY(nearly(pixelAfter.y() - pixelBefore.y(), deltaY));

    // A zero delta is a no-op: it must not switch auto-fit off.
    md::MathematicsCoordination idle;
    [[maybe_unused]] md::ThemeScope* idleScope =
        md::ThemeScope::install(&idle, reducedMotionTheme());
    idle.arrange(QRect(0, 0, 320, 240));
    idle.setDataBounds(QPointF(0.0, 0.0), QPointF(10.0, 10.0));
    QVERIFY(idle.isAutoFit());
    const QRectF idleBefore = idle.visibleBounds();
    idle.pan(QPointF(0.0, 0.0));
    QVERIFY(idle.isAutoFit());
    QVERIFY(idle.visibleBounds() == idleBefore);
}

void TestMathematicsCoordination::boundsValidationAndUnitSpan()
{
    md::MathematicsCoordination coordinates;
    [[maybe_unused]] md::ThemeScope* scope =
        md::ThemeScope::install(&coordinates, reducedMotionTheme());
    coordinates.arrange(QRect(0, 0, 400, 300));

    // Manual mode so the data setter updates only the data bounds and the
    // rejected calls can be observed without an animation in the way.
    coordinates.setVisibleBounds(QPointF(0.0, 0.0), QPointF(10.0, 10.0));
    coordinates.setDataBounds(QPointF(0.0, 0.0), QPointF(4.0, 4.0));

    const QRectF accepted = coordinates.dataBounds();
    QVERIFY(nearly(accepted.left(), 0.0));
    QVERIFY(nearly(accepted.right(), 4.0));

    const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
    const qreal infinity = std::numeric_limits<qreal>::infinity();

    coordinates.setDataBounds(QPointF(nan, 0.0), QPointF(4.0, 4.0));
    QVERIFY(coordinates.dataBounds() == accepted);

    coordinates.setDataBounds(QPointF(0.0, 0.0), QPointF(infinity, 4.0));
    QVERIFY(coordinates.dataBounds() == accepted);

    coordinates.setDataBounds(QPointF(0.0, 0.0), QPointF(4.0, -infinity));
    QVERIFY(coordinates.dataBounds() == accepted);

    // An equal pair is widened to a unit span around the value.
    coordinates.setDataBounds(QPointF(3.0, 7.0), QPointF(3.0, 7.0));
    QVERIFY(nearly(coordinates.dataBounds().width(), 1.0));
    QVERIFY(nearly(coordinates.dataBounds().height(), 1.0));
    QVERIFY(nearly(coordinates.dataBounds().center().x(), 3.0));
    QVERIFY(nearly(coordinates.dataBounds().center().y(), 7.0));

    // The same rule applies to an explicit viewport.
    coordinates.setVisibleBounds(QPointF(5.0, 5.0), QPointF(5.0, 5.0));
    QVERIFY(nearly(coordinates.visibleBounds().width(), 1.0));
    QVERIFY(nearly(coordinates.visibleBounds().height(), 1.0));
    QVERIFY(nearly(coordinates.visibleBounds().center().x(), 5.0));

    const QRectF viewport = coordinates.visibleBounds();
    coordinates.setVisibleBounds(QPointF(nan, 0.0), QPointF(1.0, 1.0));
    QVERIFY(coordinates.visibleBounds() == viewport);
}

void TestMathematicsCoordination::manualBoundsSurviveDataChanges()
{
    md::MathematicsCoordination coordinates;
    [[maybe_unused]] md::ThemeScope* scope =
        md::ThemeScope::install(&coordinates, reducedMotionTheme());
    coordinates.arrange(QRect(0, 0, 480, 320));

    coordinates.setVisibleBounds(QPointF(2.0, 2.0), QPointF(8.0, 8.0));
    const QRectF manual = coordinates.visibleBounds();
    QVERIFY(!coordinates.isAutoFit());

    coordinates.setDataBounds(QPointF(0.0, 0.0), QPointF(100.0, 100.0));

    // Manual mode means "the viewport is mine"; a data update must not move it.
    QVERIFY(coordinates.visibleBounds() == manual);
    QVERIFY(nearly(coordinates.dataBounds().right(), 100.0));

    // Handing control back refits to the full data range.
    coordinates.setAutoFit(true);
    QVERIFY(coordinates.isAutoFit());
    QVERIFY(nearly(coordinates.visibleBounds().left(), 0.0));
    QVERIFY(nearly(coordinates.visibleBounds().right(), 100.0));
}

void TestMathematicsCoordination::seriesBoundsPaletteAndHitTest()
{
    // ChartSeries::boundingRect() ignores non-finite points and reports the
    // axis-aligned bounds in data coordinates.
    md::ChartSeries boundsSeries;
    boundsSeries.points = {
        QPointF(-2.0, 3.0),
        QPointF(4.0, -5.0),
        QPointF(4.0, 3.0),
        QPointF(std::numeric_limits<qreal>::quiet_NaN(), 100.0),
    };

    const QRectF seriesBounds = boundsSeries.boundingRect();
    QVERIFY(nearly(seriesBounds.left(), -2.0));
    QVERIFY(nearly(seriesBounds.top(), -5.0));
    QVERIFY(nearly(seriesBounds.right(), 4.0));
    QVERIFY(nearly(seriesBounds.bottom(), 3.0));

    md::ChartSeries emptySeries;
    QVERIFY(emptySeries.boundingRect().isEmpty());

    md::ChartSeries pointSeries;
    pointSeries.points = {QPointF(7.0, 9.0)};
    const QRectF pointBounds = pointSeries.boundingRect();
    QVERIFY(pointBounds.isEmpty());
    QVERIFY(nearly(pointBounds.center().x(), 7.0));
    QVERIFY(nearly(pointBounds.center().y(), 9.0));

    TestChart chart;
    [[maybe_unused]] md::ThemeScope* lightScope =
        md::ThemeScope::install(&chart, reducedMotionTheme());
    chart.arrange(QRect(0, 0, 480, 320));

    md::ChartSeries series;
    series.name = QStringLiteral("Series A");
    series.points = {QPointF(0.0, 0.0), QPointF(10.0, 10.0)};
    chart.setSeries({series});

    // At least eight themed categorical colours, and an explicit colour wins
    // over the palette.
    const QVector<QColor> palette = chart.chartPalette();
    QVERIFY(palette.size() >= 8);
    for (const QColor& colour : palette)
        QVERIFY(colour.isValid());

    QVERIFY(chart.seriesColour(0).isValid());

    md::ChartSeries explicitColour = series;
    explicitColour.colour = QColor(Qt::red);
    chart.setSeries({explicitColour});
    QCOMPARE(chart.seriesColour(0).rgb(), QColor(Qt::red).rgb());

    // A dark scheme must produce a full palette too.
    TestChart darkChart;
    [[maybe_unused]] md::ThemeScope* darkScope =
        md::ThemeScope::install(&darkChart, md::Theme::baselineDark());
    darkChart.arrange(QRect(0, 0, 480, 320));
    QVERIFY(darkChart.chartPalette().size() >= 8);

    // hitTest() works before the first paint because it installs plotRect().
    // The expected pixel comes from the chart's plot area after one render has
    // installed it on the transform.
    chart.setSeries({series});
    const QImage rendered = renderWidget(chart);
    QVERIFY(!rendered.isNull());

    const QRectF plot = chart.plotRect();
    QVERIFY(!plot.isEmpty());

    const QPointF target = chart.math()->toPixel(QPointF(10.0, 10.0));
    int seriesIndex = -1;
    int pointIndex = -1;
    QVERIFY(chart.hitTest(target, &seriesIndex, &pointIndex));
    QCOMPARE(seriesIndex, 0);
    QCOMPARE(pointIndex, 1);

    // A point in the middle of the plot is far from both samples.
    QVERIFY(!chart.hitTest(plot.center(), &seriesIndex, &pointIndex));
    QCOMPARE(seriesIndex, -1);
    QCOMPARE(pointIndex, -1);

    // Empty series are legal: the transform falls back to 0..1, nothing is
    // hit-testable, and plotRect() still reserves the furniture.
    chart.setSeries({});
    QVERIFY(chart.series().isEmpty());
    QVERIFY(!chart.hitTest(plot.center()));
    QVERIFY(!chart.plotRect().isEmpty());

    // The title and legend take space from the plot.
    chart.setSeries({series});
    const QRectF withoutFurniture = chart.plotRect();
    chart.setTitle(QStringLiteral("Revenue"));
    const QRectF withTitle = chart.plotRect();
    QVERIFY(withTitle.height() < withoutFurniture.height());

    chart.setLegendVisible(false);
    QVERIFY(!chart.isLegendVisible());
}

void TestMathematicsCoordination::throwingOverrideLeavesWidgetPaintable()
{
    // A throwing component hook is caught by View's paint guard: the widget is
    // still paintable, the failure is reported, and a second paint works.
    ThrowingCoordination coordinates;
    coordinates.arrange(QRect(0, 0, 240, 180));
    QVERIFY(coordinates.lastError().isEmpty());

    const QImage first = renderWidget(coordinates);
    QVERIFY(!first.isNull());
    QVERIFY(!coordinates.lastError().isEmpty());
    QVERIFY(coordinates.lastError().contains(QStringLiteral("coordination paint boom")));

    const QImage second = renderWidget(coordinates);
    QVERIFY(!second.isNull());

    // ChartBase catches a throwing paintPlot() inside its own paintContent(),
    // so the exception never reaches View and never escapes into Qt.
    ThrowingChart chart;
    [[maybe_unused]] md::ThemeScope* scope =
        md::ThemeScope::install(&chart, reducedMotionTheme());
    chart.arrange(QRect(0, 0, 480, 320));

    md::ChartSeries series;
    series.name = QStringLiteral("Series A");
    series.points = {QPointF(0.0, 0.0), QPointF(1.0, 1.0)};
    chart.setSeries({series});

    const QImage chartImage = renderWidget(chart);
    QVERIFY(!chartImage.isNull());
    QVERIFY(chart.lastError().isEmpty());
}

QTEST_MAIN(TestMathematicsCoordination)
#include "tst_math_coordination.moc"
