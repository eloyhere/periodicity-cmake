// Unit tests for the BarChart component (framework.md §5.9, §14).
//
// The widget is exercised offscreen like the other component suites: the
// application theme is pinned to baseline light, geometry comes from barRect()
// (the same geometry the paint pass uses) and widgets are rendered into an
// explicitly transparent QImage with QWidget::render. Pointer input is delivered
// with QApplication::sendEvent.
//
// The tests pin the documented semantics: a ChartSeries is a category and its
// points are the bars in that group, grouped bars sit side by side while stacked
// bars share the slot and build upwards, negative values are drawn below the
// zero line, the horizontal orientation swaps the axes, a click on a bar emits
// barActivated(), RTL mirrors the category order, empty data paints the themed
// state, and a throwing paintPlot() never unwinds into Qt.

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

#include <md/components/bar_chart.h>
#include <md/theme/theme.h>

#include <stdexcept>

namespace {

const QColor kRed(198, 40, 40);
const QColor kBlue(21, 101, 192);

// A category is one ChartSeries; its points are the bars in the group. As in
// the rest of the chart family, a point is (position along the category axis,
// value): the bar's value is the y coordinate, which is the coordinate the
// value axis and paintPlot() read.
md::ChartSeries makeCategory(const QString& name, const QVector<qreal>& values,
                             const QColor& colour)
{
    md::ChartSeries series;
    series.name = name;
    series.points.reserve(values.size());
    for (qsizetype index = 0; index < values.size(); ++index) {
        series.points.append(
            QPointF(static_cast<qreal>(index), values.at(index)));
    }
    series.colour = colour;
    return series;
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

class TestBarChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void groupedBarsSitSideBySide();
    void stackedBarsShareASlot();
    void groupedAndStackedPaintingDiffers();
    void negativeValuesDrawBelowZero();
    void horizontalOrientationSwapsAxes();
    void barActivatedFiresOnClick();
    void valueLabelsAppearWhenTheyFit();
    void rtlMirrorsCategoryOrder();
    void emptyDataPaintsState();
    void throwingPaintPlotDoesNotCrash();
};

void TestBarChart::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestBarChart::groupedBarsSitSideBySide()
{
    md::BarChart chart;
    chart.setSeries({makeCategory(QStringLiteral("A"), {4.0, 6.0}, kRed),
                     makeCategory(QStringLiteral("B"), {8.0, 2.0}, kBlue)});
    chart.resize(420, 320);

    QVERIFY(!chart.isStacked());
    QCOMPARE(chart.categoryCount(), 2);

    const QRectF first = chart.barRect(0, 0);
    const QRectF second = chart.barRect(0, 1);
    const QRectF other = chart.barRect(1, 0);

    QVERIFY(first.isValid());
    QVERIFY(second.isValid());
    QVERIFY(other.isValid());

    QVERIFY2(qAbs(first.left() - second.left()) > 1.0,
             "grouped bars must sit side by side, not on top of each other");
    QVERIFY2(first.right() <= second.left() + 0.5, "grouped bars must not overlap");
    QVERIFY2(qAbs(first.width() - second.width()) < 0.6,
             "bars in one group share the slot width");
    QVERIFY2(qAbs(first.bottom() - second.bottom()) < 0.6,
             "both positive bars start at the same zero line");
    QVERIFY2(other.left() > second.right() - 0.5,
             "the second category follows the first along the category axis");
}

void TestBarChart::stackedBarsShareASlot()
{
    md::BarChart chart;
    chart.setSeries({makeCategory(QStringLiteral("A"), {4.0, 6.0}, kRed)});
    chart.resize(420, 320);

    const QRectF groupedFirst = chart.barRect(0, 0);

    chart.setStacked(true);
    QVERIFY(chart.isStacked());

    const QRectF first = chart.barRect(0, 0);
    const QRectF second = chart.barRect(0, 1);
    QVERIFY(first.isValid());
    QVERIFY(second.isValid());

    QVERIFY2(qAbs(first.left() - second.left()) < 0.6, "stacked bars share one slot");
    QVERIFY2(qAbs(first.width() - second.width()) < 0.6,
             "stacked bars share the slot width");
    QVERIFY2(second.bottom() <= first.top() + 0.6,
             "the second segment sits on top of the first");
    QVERIFY2(qAbs(first.height() - second.height()) > 1.0,
             "each segment is sized by its own value");

    // The stack starts at the shared zero line, exactly like the grouped bar.
    QVERIFY2(qAbs(first.bottom() - groupedFirst.bottom()) < 0.6,
             "the stack starts at the zero line");
}

void TestBarChart::groupedAndStackedPaintingDiffers()
{
    const QVector<md::ChartSeries> data{makeCategory(QStringLiteral("A"), {4.0, 6.0}, kRed),
                                        makeCategory(QStringLiteral("B"), {8.0, 2.0}, kBlue)};

    md::BarChart grouped;
    grouped.setSeries(data);
    grouped.resize(420, 320);

    md::BarChart stacked;
    stacked.setSeries(data);
    stacked.setStacked(true);
    stacked.resize(420, 320);

    QVERIFY2(renderWidget(grouped) != renderWidget(stacked),
             "grouped and stacked painting must not be identical");
}

void TestBarChart::negativeValuesDrawBelowZero()
{
    md::BarChart chart;
    chart.setSeries({makeCategory(QStringLiteral("A"), {10.0, -10.0}, kRed)});
    chart.resize(360, 320);

    const QRectF plot = chart.plotRect();
    QVERIFY(plot.isValid());

    // The axis always contains zero; with a symmetric value range the zero line
    // is the middle of the plot, so the sign of each bar is directly readable.
    const qreal zeroY = plot.center().y();

    const QRectF positive = chart.barRect(0, 0);
    const QRectF negative = chart.barRect(0, 1);
    QVERIFY(positive.isValid());
    QVERIFY(negative.isValid());

    QVERIFY2(positive.bottom() <= zeroY + 1.0, "the positive bar ends at the zero line");
    QVERIFY2(positive.top() < zeroY - 1.0, "the positive bar grows upwards");
    QVERIFY2(negative.top() >= zeroY - 1.0, "the negative bar starts at the zero line");
    QVERIFY2(negative.bottom() > zeroY + 1.0, "the negative bar grows downwards");
    QVERIFY2(negative.top() < negative.bottom(), "the negative rectangle is not inverted");

    const QImage image = renderWidget(chart);
    QCOMPARE(pixelAt(image, positive.center()).rgb(), kRed.rgb());
    QCOMPARE(pixelAt(image, negative.center()).rgb(), kRed.rgb());
}

void TestBarChart::horizontalOrientationSwapsAxes()
{
    md::BarChart chart;
    chart.setSeries({makeCategory(QStringLiteral("A"), {10.0}, kRed),
                     makeCategory(QStringLiteral("B"), {4.0}, kBlue)});
    // A wide gap keeps the bars thin, so the two orientations are unambiguous
    // from the rectangle's aspect ratio alone.
    chart.setGroupGap(0.8);
    chart.resize(420, 320);

    QVERIFY(chart.orientation() == md::BarChart::Orientation::Vertical);

    const QRectF verticalPlot = chart.plotRect();
    QVERIFY(verticalPlot.isValid());

    const QRectF vertical = chart.barRect(0, 0);
    QVERIFY(vertical.isValid());
    QVERIFY2(vertical.height() > vertical.width() * 2.0, "a vertical bar is tall");
    QVERIFY2(qAbs(vertical.bottom() - verticalPlot.bottom()) < 2.0,
             "a vertical bar grows up from the bottom of the plot");

    chart.setOrientation(md::BarChart::Orientation::Horizontal);
    QVERIFY(chart.orientation() == md::BarChart::Orientation::Horizontal);

    // The gutters depend on which axis carries the tick labels, so the plot
    // rectangle is re-read after the orientation change.
    const QRectF horizontalPlot = chart.plotRect();
    QVERIFY(horizontalPlot.isValid());

    const QRectF horizontal = chart.barRect(0, 0);
    QVERIFY(horizontal.isValid());
    QVERIFY2(horizontal.width() > horizontal.height() * 2.0, "a horizontal bar is wide");
    QVERIFY2(qAbs(horizontal.left() - horizontalPlot.left()) < 2.0,
             "a positive horizontal bar grows right from the value axis at zero");
    QVERIFY2(horizontal.right() > horizontalPlot.center().x(),
             "the bar reaches beyond the middle of the plot");
}

void TestBarChart::barActivatedFiresOnClick()
{
    md::BarChart chart;
    chart.setSeries({makeCategory(QStringLiteral("A"), {10.0, 4.0}, kRed),
                     makeCategory(QStringLiteral("B"), {8.0}, kBlue)});
    chart.resize(420, 320);

    QSignalSpy spy(&chart, &md::BarChart::barActivated);

    const QRectF target = chart.barRect(0, 1);
    QVERIFY(target.isValid());
    clickAt(chart, target.center());

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 0);
    QCOMPARE(spy.at(0).at(1).toInt(), 1);
    QCOMPARE(chart.selectedSeries(), 0);
    QCOMPARE(chart.selectedPoint(), 1);

    // A click that hits no bar activates nothing.
    clickAt(chart, QPointF(1.0, 1.0));
    QCOMPARE(spy.count(), 1);
}

void TestBarChart::valueLabelsAppearWhenTheyFit()
{
    md::BarChart chart;
    chart.setSeries({makeCategory(QStringLiteral("A"), {10.0}, kRed)});
    chart.resize(420, 320);

    QVERIFY(!chart.showsValues());

    chart.setShowValues(true);
    QVERIFY(chart.showsValues());
    const QImage withValues = renderWidget(chart);

    chart.setShowValues(false);
    const QImage withoutValues = renderWidget(chart);

    QVERIFY2(withValues != withoutValues,
             "a value label that fits inside the bar must change the painted pixels");
}

void TestBarChart::rtlMirrorsCategoryOrder()
{
    md::BarChart chart;
    chart.setLayoutDirection(Qt::RightToLeft);
    chart.setSeries({makeCategory(QStringLiteral("A"), {5.0}, kRed),
                     makeCategory(QStringLiteral("B"), {5.0}, kBlue)});
    chart.resize(420, 320);

    const QRectF plot = chart.plotRect();
    QVERIFY(plot.isValid());

    const QRectF first = chart.barRect(0, 0);
    const QRectF second = chart.barRect(1, 0);
    QVERIFY(first.isValid());
    QVERIFY(second.isValid());

    QVERIFY2(first.center().x() > second.center().x(),
             "under RTL category 0 sits on the reading-start (right) side");
    QVERIFY2(first.center().x() > plot.center().x(), "category 0 is on the right half");
    QVERIFY2(second.center().x() < plot.center().x(), "category 1 is on the left half");
}

void TestBarChart::emptyDataPaintsState()
{
    md::BarChart chart;
    // Without the Cartesian frame the only ink left is the empty-state label,
    // so the pixel check below cannot be satisfied by grid or axis lines.
    chart.setAxesVisible(false);
    chart.resize(360, 240);

    QVERIFY(!chart.hasBarData());
    QCOMPARE(chart.categoryCount(), 0);
    QVERIFY(!chart.barRect(0, 0).isValid());

    int series = -1;
    int point = -1;
    QVERIFY(!chart.barAt(QPointF(20.0, 20.0), &series, &point));
    QCOMPARE(series, -1);
    QCOMPARE(point, -1);

    // A category with no finite point is not data either.
    chart.setSeries({makeCategory(QStringLiteral("empty"), {}, kRed)});
    QVERIFY(!chart.hasBarData());

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());

    const QColor background = pixelAt(image, QPointF(2.0, 2.0));
    QVERIFY2(pixelsDifferingFrom(image, background) > 0,
             "the empty state must paint its themed label");
}

void TestBarChart::throwingPaintPlotDoesNotCrash()
{
    class ThrowingBarChart : public md::BarChart
    {
    public:
        using md::BarChart::BarChart;

    protected:
        void paintPlot(QPainter&, const QRectF&) override
        {
            throw std::runtime_error("bar paint failure");
        }
    };

    ThrowingBarChart chart;
    chart.setSeries({makeCategory(QStringLiteral("A"), {4.0}, kRed)});
    chart.resize(320, 240);

    const QImage first = renderWidget(chart);
    QVERIFY(!first.isNull());

    // A second frame proves the failed paint did not corrupt the widget state.
    const QImage second = renderWidget(chart);
    QVERIFY(!second.isNull());
}

QTEST_MAIN(TestBarChart)
#include "tst_bar_chart.moc"
