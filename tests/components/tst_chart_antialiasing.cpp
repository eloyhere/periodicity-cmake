// Periodicity — chart antialiasing modes (chart_common.h).
//
// The base class owns the strategy, so every chart inherits the same choice.
// These cases pin the three observable properties: the default is the grey
// raster, the aliased mode really removes intermediate edge shades, and the
// supersampled modes exist and are applied to the data marks.

#include <md/components/chart_common.h>

#include <QApplication>
#include <QImage>
#include <QPainter>
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
        // carry one finite point; the pen below is what the assertions look at.
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

        QPen pen(palette().color(QPalette::WindowText));
        pen.setWidthF(2.0);
        painter.setPen(pen);
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
    // the stroke. The raster antialiaser mixes the two and produces many more.
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
