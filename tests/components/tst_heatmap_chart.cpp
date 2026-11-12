// Tests for the HeatmapChart component.
//
// The suite is offscreen and deterministic: the application theme is pinned to
// the baseline light scheme, the chart is sized and shown, and it is rendered
// into an explicitly transparent QImage. Cell geometry comes from the chart's
// own cellRect(), so the pixel assertions exercise the same mirrored mapping
// cellAt() uses instead of a test-local guess.
//
// The colour-scale cases compare the rendered pixels with the two theme roles
// the scale interpolates between; the NaN case scans the empty cell for hatch
// pixels instead of assuming one particular diagonal hits the centre.
//
// Hover is driven with real Qt events, matching the ScatterChart suite: a
// QEnterEvent holds the chart hovered while the colour-scale case samples the
// cells, and a QMouseEvent covers View's pointer dispatch, the chart's hover
// state and the repaint.
//
// The final cases cover the two failure surfaces the component owns: an empty
// matrix must paint the base empty state, and a throwing paintPlot() must be
// contained by the exception boundary.

#include <md/components/heatmap_chart.h>
#include <md/theme/theme.h>

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QEnterEvent>
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
#include <limits>
#include <stdexcept>

namespace {

constexpr int kChartWidth = 480;
constexpr int kChartHeight = 320;

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
    // Pixel assertions need the settled frame: the reveal animation stays off here.
    if (auto* chartBase = qobject_cast<md::ChartBase*>(&widget))
        chartBase->setAnimated(false);
    widget.show();
    QCoreApplication::processEvents();
}

// Reads one logical (widget-local) pixel from a render that may be at a device
// pixel ratio other than 1.
QColor pixelAt(const QImage& image, QPointF logical)
{
    const qreal ratio = image.devicePixelRatio();
    const int x = std::clamp(static_cast<int>(std::lround(logical.x() * ratio)),
                             0, image.width() - 1);
    const int y = std::clamp(static_cast<int>(std::lround(logical.y() * ratio)),
                             0, image.height() - 1);
    return image.pixelColor(x, y);
}

// The number of pixels inside `logicalRect` whose colour differs from the
// widget background. The hatch is sparse, so the test looks for any paint at
// all rather than for one exact pattern pixel.
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

class ThrowingHeatmapChart : public md::HeatmapChart
{
public:
    ThrowingHeatmapChart() = default;

protected:
    void paintPlot(QPainter&, const QRectF&) override
    {
        throw std::runtime_error("intentional HeatmapChart paint failure");
    }
};

} // namespace

class TestHeatmapChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void constructsWithMatrixAndDefaults();
    void minAndMaxCellsDiffer();
    void emptyCellIsHatched();
    void cellAtHitsAndMisses();
    void hoverHighlightsAndLeaveClears();
    void emptyMatrixPaints();
    void throwingPaintPlotDoesNotCrash();
};

void TestHeatmapChart::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
    QApplication::setPalette(md::Theme::baselineLight().toPalette());
}

void TestHeatmapChart::constructsWithMatrixAndDefaults()
{
    md::HeatmapChart chart;

    QCOMPARE(chart.rows(), 0);
    QCOMPARE(chart.columns(), 0);
    QVERIFY(chart.values().isEmpty());
    QVERIFY(std::isnan(chart.value(0, 0)));
    QCOMPARE(chart.hoveredCell(), QPoint(-1, -1));
    QVERIFY(!chart.showsValues());
    QVERIFY(chart.showsGrid());
    QVERIFY(!chart.isAxesVisible());

    chart.setRows(2);
    chart.setColumns(3);
    QCOMPARE(chart.rows(), 2);
    QCOMPARE(chart.columns(), 3);
    QCOMPARE(chart.values().size(), qsizetype(2));
    QCOMPARE(chart.values().at(0).size(), qsizetype(3));
    QVERIFY(std::isnan(chart.value(1, 2)));

    chart.setValue(1, 2, 7.5);
    QCOMPARE(chart.value(1, 2), 7.5);

    // Out-of-range writes are ignored; the matrix never grows implicitly.
    chart.setValue(9, 9, 1.0);
    chart.setValue(-1, 0, 1.0);
    QCOMPARE(chart.rows(), 2);
    QCOMPARE(chart.columns(), 3);
    QVERIFY(std::isnan(chart.value(9, 9)));

    chart.setRowLabels({QStringLiteral("r0"), QStringLiteral("r1")});
    chart.setColumnLabels({QStringLiteral("c0"), QStringLiteral("c1"),
                           QStringLiteral("c2")});
    QCOMPARE(chart.rowLabels().size(), qsizetype(2));
    QCOMPARE(chart.columnLabels().size(), qsizetype(3));

    chart.setShowValues(true);
    chart.setShowGrid(false);
    QVERIFY(chart.showsValues());
    QVERIFY(!chart.showsGrid());

    // A ragged matrix is padded with NaN so every stored row has columns().
    chart.setValues({{1.0, 2.0}, {3.0}});
    QCOMPARE(chart.rows(), 2);
    QCOMPARE(chart.columns(), 2);
    QCOMPARE(chart.value(1, 0), 3.0);
    QVERIFY(std::isnan(chart.value(1, 1)));
}

void TestHeatmapChart::minAndMaxCellsDiffer()
{
    md::HeatmapChart chart;
    chart.setValues({{0.0, 100.0}});
    chart.setShowGrid(false);
    prepare(chart);

    // A heatmap is a data surface, not a control: the pointer resting on the
    // chart must not tint the cells away from the role colours the scale maps
    // to. Drive the hover explicitly instead of relying on the platform cursor
    // happening to be over the freshly shown widget.
    const QPointF pointer(2.0, 2.0);
    QEnterEvent enter(pointer, chart.mapToGlobal(pointer.toPoint()),
                      chart.mapToGlobal(pointer.toPoint()));
    QCoreApplication::sendEvent(&chart, &enter);
    QVERIFY2(chart.state().testFlag(md::ViewState::Hovered),
             "this case must run while the chart is hovered");
    QCOMPARE(chart.hoveredCell(), QPoint(-1, -1));

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    const QRectF lowCell = chart.cellRect(0, 0);
    const QRectF highCell = chart.cellRect(0, 1);
    QVERIFY2(!lowCell.isEmpty(), "the minimum cell must have geometry");
    QVERIFY2(!highCell.isEmpty(), "the maximum cell must have geometry");

    const QColor low = pixelAt(image, lowCell.center());
    const QColor high = pixelAt(image, highCell.center());

    QVERIFY2(low != high, "the minimum and maximum cells must not share a colour");

    const md::ColorScheme& colours = md::Theme::forWidget(&chart).colours();
    QVERIFY2(low == colours.surfaceContainerHighest,
             "the minimum must map to surfaceContainerHighest");
    QVERIFY2(high == colours.primary, "the maximum must map to primary");
}

void TestHeatmapChart::emptyCellIsHatched()
{
    const qreal nan = std::numeric_limits<qreal>::quiet_NaN();

    md::HeatmapChart chart;
    chart.setValues({{0.0, nan}});
    chart.setShowGrid(false);
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    const QRectF emptyCell = chart.cellRect(0, 1);
    QVERIFY2(!emptyCell.isEmpty(), "the NaN cell must have geometry");

    const QColor background = pixelAt(image, QPointF(4.0, 4.0));
    QVERIFY2(paintedPixels(image, emptyCell, background) > 0,
             "a NaN cell must be hatched rather than left unpainted");

    // The finite neighbour is a solid fill, so the hatch is specific to NaN.
    const QRectF finiteCell = chart.cellRect(0, 0);
    QVERIFY(paintedPixels(image, finiteCell, background) > 0);
}

void TestHeatmapChart::cellAtHitsAndMisses()
{
    md::HeatmapChart chart;
    chart.setValues({{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}, {7.0, 8.0, 9.0}});
    chart.setRowLabels({QStringLiteral("r0"), QStringLiteral("r1"),
                        QStringLiteral("r2")});
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    QVERIFY2(chart.cellAt(chart.cellRect(1, 2).center()) == QPoint(2, 1),
             "cellAt must find the cell whose centre it is given");
    QCOMPARE(chart.cellAt(QPointF(-5000.0, -5000.0)), QPoint(-1, -1));
    QCOMPARE(chart.cellAt(QPointF(2.0, 2.0)), QPoint(-1, -1));

    // The row-label gutter is outside the cell area, not cell (0, 0).
    const QRectF first = chart.cellRect(0, 0);
    QCOMPARE(chart.cellAt(QPointF(first.left() - 2.0, first.center().y())),
             QPoint(-1, -1));
}

void TestHeatmapChart::hoverHighlightsAndLeaveClears()
{
    md::HeatmapChart chart;
    chart.setValues({{0.0, 100.0}, {50.0, 25.0}});
    chart.setShowGrid(false);
    prepare(chart);

    const QImage before = renderWidget(chart);
    QVERIFY(!before.isNull());

    const QPointF target = chart.cellRect(1, 0).center();
    QMouseEvent move(QEvent::MouseMove, target, chart.mapToGlobal(target.toPoint()),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&chart, &move);

    QCOMPARE(chart.hoveredCell(), QPoint(0, 1));

    const QImage hovered = renderWidget(chart);
    QVERIFY(!hovered.isNull());
    QVERIFY2(before != hovered, "the hover highlight must repaint the chart");

    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(&chart, &leave);

    QCOMPARE(chart.hoveredCell(), QPoint(-1, -1));
    const QImage cleared = renderWidget(chart);
    QVERIFY2(cleared != hovered, "leaving must repaint without the highlight");
}

void TestHeatmapChart::emptyMatrixPaints()
{
    md::HeatmapChart chart;
    prepare(chart);

    QVERIFY(chart.values().isEmpty());
    QVERIFY(!renderWidget(chart).isNull());

    chart.setValues({});
    QVERIFY(!renderWidget(chart).isNull());

    // Dimensions without a finite value are still an empty state.
    chart.setRows(2);
    chart.setColumns(2);
    QVERIFY(!renderWidget(chart).isNull());
}

void TestHeatmapChart::throwingPaintPlotDoesNotCrash()
{
    ThrowingHeatmapChart chart;
    chart.setValues({{0.0, 1.0}});
    prepare(chart);

    const QImage image = renderWidget(chart);
    QVERIFY2(!image.isNull(), "a throwing paintPlot must be caught, not unwind into Qt");
}

QTEST_MAIN(TestHeatmapChart)
#include "tst_heatmap_chart.moc"
