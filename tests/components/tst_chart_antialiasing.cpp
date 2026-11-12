// Periodicity — chart antialiasing modes (chart_common.h).
//
// The base class owns the strategy, so every chart inherits the same choice.
// These cases pin the observable properties: the default is the grey raster, the
// aliased mode really removes intermediate edge shades, each strategy family
// changes the data marks, and switching back reproduces the default byte for
// byte.

#include <md/components/chart_common.h>

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QSet>
#include <QTest>

namespace {

class EdgedChart : public md::ChartBase
{
public:
    explicit EdgedChart(QWidget* parent = nullptr)
        : ChartBase(parent)
    {
        setInteractionFeedbackEnabled(false);
        setAnimated(false);

        // hasData() is decided by the base from the series, so the fixture has to
        // carry one finite point; the marks below are what the assertions look at.
        md::ChartSeries series;
        series.name = QStringLiteral("edge");
        series.points = {QPointF(0.0, 0.0), QPointF(1.0, 1.0)};
        setSeries({series});
    }

    void setEdged(bool edged) { edged_ = edged; }

protected:
    void paintPlot(QPainter& painter, const QRectF& plot) override
    {
        if (!edged_)
            return;

        // A filled disc gives the morphological filters a wide, smooth boundary:
        // its cross profile is monotone, unlike a text stroke or the thin
        // diagonal. The diagonal pins the aliased/coverage comparison.
        painter.setPen(Qt::NoPen);
        painter.setBrush(chartPalette().at(1));
        painter.drawEllipse(plot.center(), plot.width() * 0.24, plot.height() * 0.24);

        QPen pen(palette().color(QPalette::WindowText));
        pen.setWidthF(2.0);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(plot.topLeft(), plot.bottomRight());
    }

private:
    bool edged_ = true;
};

QImage render(QWidget& widget)
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

int distinctColours(const QImage& image)
{
    QSet<QRgb> seen;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x)
            seen.insert(image.pixel(x, y));
    }
    return seen.size();
}

} // namespace

class TestChartAntialiasing : public QObject
{
    Q_OBJECT

private slots:
    void defaultIsGrayscale();
    void aliasedEdgesHaveFewerShades();
    void supersamplingIsSelectable();
    void postProcessFiltersSoftenEdges();
    void newModesAreRealAlgorithms_data();
    void newModesAreRealAlgorithms();
    void everyModeSurvivesAnEmptyPlot();
    void switchingBackRestoresTheDefaultLook();
};

void TestChartAntialiasing::defaultIsGrayscale()
{
    EdgedChart chart;
    QCOMPARE(chart.antialiasing(), md::ChartAntialiasing::Grayscale);
}

void TestChartAntialiasing::aliasedEdgesHaveFewerShades()
{
    EdgedChart chart;
    chart.resize(240, 180);

    chart.setAntialiasing(md::ChartAntialiasing::None);
    const int aliased = distinctColours(render(chart));

    chart.setAntialiasing(md::ChartAntialiasing::Grayscale);
    const int smoothed = distinctColours(render(chart));

    // A diagonal stroke with antialiasing off has hard edges: background plus
    // the mark colours. The raster antialiaser mixes them and produces many more.
    QVERIFY2(smoothed > aliased,
             qPrintable(QStringLiteral("grayscale %1 shades vs aliased %2")
                            .arg(smoothed)
                            .arg(aliased)));
}

void TestChartAntialiasing::supersamplingIsSelectable()
{
    EdgedChart chart;
    chart.resize(240, 180);

    chart.setAntialiasing(md::ChartAntialiasing::None);
    const int aliased = distinctColours(render(chart));

    chart.setAntialiasing(md::ChartAntialiasing::Supersample2x);
    QCOMPARE(chart.antialiasing(), md::ChartAntialiasing::Supersample2x);
    const int twice = distinctColours(render(chart));
    QVERIFY(twice > aliased);

    chart.setAntialiasing(md::ChartAntialiasing::Supersample4x);
    QCOMPARE(chart.antialiasing(), md::ChartAntialiasing::Supersample4x);
    const int fourTimes = distinctColours(render(chart));
    QVERIFY(fourTimes > aliased);
}

void TestChartAntialiasing::postProcessFiltersSoftenEdges()
{
    EdgedChart chart;
    chart.resize(240, 180);

    chart.setAntialiasing(md::ChartAntialiasing::None);
    const int aliased = distinctColours(render(chart));

    chart.setAntialiasing(md::ChartAntialiasing::Fxaa);
    QCOMPARE(chart.antialiasing(), md::ChartAntialiasing::Fxaa);
    const int fxaa = distinctColours(render(chart));
    QVERIFY2(fxaa > aliased, qPrintable(QStringLiteral("fxaa %1 vs aliased %2").arg(fxaa).arg(aliased)));

    chart.setAntialiasing(md::ChartAntialiasing::Fmaa);
    QCOMPARE(chart.antialiasing(), md::ChartAntialiasing::Fmaa);
    const int fmaa = distinctColours(render(chart));
    QVERIFY2(fmaa > aliased, qPrintable(QStringLiteral("fmaa %1 vs aliased %2").arg(fmaa).arg(aliased)));
}

void TestChartAntialiasing::newModesAreRealAlgorithms_data()
{
    QTest::addColumn<int>("modeValue");

    QTest::newRow("mlaa") << static_cast<int>(md::ChartAntialiasing::Mlaa);
    QTest::newRow("smaa") << static_cast<int>(md::ChartAntialiasing::Smaa);
    QTest::newRow("cmaa") << static_cast<int>(md::ChartAntialiasing::Cmaa);
    QTest::newRow("jittered4x") << static_cast<int>(md::ChartAntialiasing::Jittered4x);
    QTest::newRow("jittered8x") << static_cast<int>(md::ChartAntialiasing::Jittered8x);
}

void TestChartAntialiasing::newModesAreRealAlgorithms()
{
    QFETCH(int, modeValue);
    const auto mode = static_cast<md::ChartAntialiasing>(modeValue);

    EdgedChart chart;
    chart.resize(240, 180);

    chart.setAntialiasing(md::ChartAntialiasing::Grayscale);
    const QImage baseline = render(chart);
    const quint64 furnitureBuilds = chart.staticLayerBuilds();

    chart.setAntialiasing(md::ChartAntialiasing::None);
    const int aliased = distinctColours(render(chart));

    // (a) the new mode round-trips through the setter.
    chart.setAntialiasing(mode);
    QCOMPARE(chart.antialiasing(), mode);

    const QImage image = render(chart);
    const int shades = distinctColours(image);

    // (b) a real antialiasing pass adds intermediate colours.
    QVERIFY2(shades > aliased,
             qPrintable(QStringLiteral("%1: %2 shades vs aliased %3")
                            .arg(modeValue)
                            .arg(shades)
                            .arg(aliased)));

    // (c) it is not the grey raster under another name.
    QVERIFY2(image != baseline,
             qPrintable(QStringLiteral("%1 reproduced the grayscale image").arg(modeValue)));

    // Changing the strategy never touches the cached furniture layer.
    QCOMPARE(chart.staticLayerBuilds(), furnitureBuilds);

    // (d) switching back restores the default look byte for byte.
    chart.setAntialiasing(md::ChartAntialiasing::Grayscale);
    QCOMPARE(render(chart), baseline);
}

void TestChartAntialiasing::everyModeSurvivesAnEmptyPlot()
{
    const md::ChartAntialiasing modes[] = {
        md::ChartAntialiasing::None,        md::ChartAntialiasing::Grayscale,
        md::ChartAntialiasing::Supersample2x, md::ChartAntialiasing::Supersample4x,
        md::ChartAntialiasing::Fxaa,        md::ChartAntialiasing::Fmaa,
        md::ChartAntialiasing::Mlaa,        md::ChartAntialiasing::Smaa,
        md::ChartAntialiasing::Cmaa,        md::ChartAntialiasing::Jittered4x,
        md::ChartAntialiasing::Jittered8x,
    };

    EdgedChart chart;
    chart.resize(1, 1); // no room for a plot rectangle
    for (const md::ChartAntialiasing mode : modes) {
        chart.setAntialiasing(mode);
        QCOMPARE(chart.antialiasing(), mode);
        QCOMPARE(render(chart).size(), QSize(1, 1));
    }
}

void TestChartAntialiasing::switchingBackRestoresTheDefaultLook()
{
    EdgedChart chart;
    chart.resize(240, 180);

    const QImage baseline = render(chart);

    chart.setAntialiasing(md::ChartAntialiasing::None);
    render(chart);

    chart.setAntialiasing(md::ChartAntialiasing::Grayscale);
    const QImage restored = render(chart);
    QCOMPARE(restored, baseline);
}

QTEST_MAIN(TestChartAntialiasing)

#include "tst_chart_antialiasing.moc"
