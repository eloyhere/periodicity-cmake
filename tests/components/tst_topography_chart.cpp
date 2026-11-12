// Unit tests for the TopographyChart component.
//
// The field is a radial hill, so every interior level must produce segments and
// the contour count must follow setLevels(). The suite also pins the bilinear
// fieldAt() mapping against the lattice, the pixel hit test, the filled-band
// switch, the hover readout, reduced-motion snapping and the reveal completing.

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QRegion>
#include <QSizeF>
#include <QTest>
#include <QWidget>
#include <Qt>

#include <md/components/topography_chart.h>
#include <md/core/measure_spec.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>
#include <md/tokens/motion.h>

#include <cmath>

namespace {

void sendMouseMove(QWidget& widget, const QPointF& position)
{
    const QPoint global = widget.mapToGlobal(position.toPoint());
    QMouseEvent event(QEvent::MouseMove, position, QPointF(global), Qt::NoButton,
                      Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
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

QVector<qreal> radialField(int rows, int columns)
{
    QVector<qreal> values;
    values.reserve(rows * columns);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const qreal x = (column - (columns - 1) / 2.0) / (columns - 1);
            const qreal y = (row - (rows - 1) / 2.0) / (rows - 1);
            values.append(std::exp(-(x * x + y * y) * 8.0) * 100.0);
        }
    }
    return values;
}

QPointF samplePoint(const QRectF& field, int rows, int columns, int row, int column)
{
    const qreal x = columns > 1
        ? field.left() + field.width() * column / (columns - 1.0)
        : field.center().x();
    const qreal y = rows > 1
        ? field.top() + field.height() * row / (rows - 1.0)
        : field.center().y();
    return QPointF(x, y);
}

} // namespace

class TestTopographyChart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void constructionAndMeasurement();
    void fieldGeometryAndContours();
    void bilinearHitTest();
    void hoverReadout();
    void filledBandsToggle();
    void paintedPixels();
    void emptyStatePaints();
    void reducedMotionSnaps();
    void revealReachesOne();
};

void TestTopographyChart::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestTopographyChart::constructionAndMeasurement()
{
    md::TopographyChart chart;
    QCOMPARE(chart.rows(), 0);
    QCOMPARE(chart.columns(), 0);
    QVERIFY(chart.values().isEmpty());
    QCOMPARE(chart.levels(), 7);
    QVERIFY(chart.hasFilledBands());
    QVERIFY(chart.contourLevels().isEmpty());
    QCOMPARE(chart.totalContourSegments(), 0);
    QCOMPARE(chart.contourSegmentCount(0.5), 0);

    chart.measure(md::MeasureSpec::exactly(320.0), md::MeasureSpec::exactly(260.0));
    QCOMPARE(chart.measuredSize(), QSizeF(320.0, 260.0));

    chart.resize(360, 320);
    QVERIFY(chart.plotRect().isValid());
    QVERIFY(chart.fieldRect().isEmpty());
}

void TestTopographyChart::fieldGeometryAndContours()
{
    md::TopographyChart chart;
    chart.setLevels(5);
    chart.setField(21, 21, radialField(21, 21));
    chart.resize(400, 360);

    QCOMPARE(chart.rows(), 21);
    QCOMPARE(chart.columns(), 21);
    QCOMPARE(chart.values().size(), 441);
    QCOMPARE(chart.levels(), 5);

    const QRectF field = chart.fieldRect();
    QVERIFY(field.isValid());
    QVERIFY(field.width() > 100.0);
    QVERIFY(field.height() > 100.0);
    QVERIFY2(field.width() < chart.plotRect().width(),
             "the field must leave room for the colour bar");

    const QVector<qreal> levels = chart.contourLevels();
    QCOMPARE(levels.size(), 5);
    for (qsizetype index = 1; index < levels.size(); ++index)
        QVERIFY2(levels.at(index) > levels.at(index - 1), "levels are increasing");

    QVERIFY2(chart.totalContourSegments() > 20,
             "a radial hill crosses every interior level");
    QCOMPARE(chart.contourLines(levels.at(2)).size(),
             chart.contourSegmentCount(levels.at(2)));
    QVERIFY(chart.contourSegmentCount(levels.at(2)) > 0);

    chart.setLevels(8);
    QCOMPARE(chart.contourLevels().size(), 8);
}

void TestTopographyChart::bilinearHitTest()
{
    md::TopographyChart chart;
    QVector<qreal> plane;
    plane.reserve(5 * 5);
    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 5; ++column)
            plane.append(row * 10.0 + column);
    }
    chart.setField(5, 5, plane);
    chart.resize(360, 320);

    const QRectF field = chart.fieldRect();
    QVERIFY(field.isValid());

    const QPointF corner = samplePoint(field, 5, 5, 0, 0);
    QCOMPARE(chart.cellAt(corner), QPoint(0, 0));
    QVERIFY(qAbs(chart.fieldAt(corner) - 0.0) < 1e-9);

    const QPointF middle = samplePoint(field, 5, 5, 2, 3);
    QCOMPARE(chart.cellAt(middle), QPoint(3, 2));
    QVERIFY(qAbs(chart.fieldAt(middle) - 23.0) < 1e-9);

    const QPointF between = samplePoint(field, 5, 5, 1, 1) + QPointF(field.width() / 8.0, 0.0);
    const qreal interpolated = chart.fieldAt(between);
    QVERIFY(interpolated > 11.0);
    QVERIFY(interpolated < 12.0);

    QCOMPARE(chart.cellAt(QPointF(1.0, 1.0)), QPoint(-1, -1));
    QVERIFY(std::isnan(chart.fieldAt(QPointF(1.0, 1.0))));
    QVERIFY(std::isnan(chart.value(-1, 0)));
}

void TestTopographyChart::hoverReadout()
{
    md::TopographyChart chart;
    chart.setField(15, 15, radialField(15, 15));
    chart.resize(360, 320);

    const QRectF field = chart.fieldRect();
    const QPointF target = samplePoint(field, 15, 15, 6, 9);

    const QImage before = renderWidget(chart);
    sendMouseMove(chart, target);

    QCOMPARE(chart.hoveredCell(), QPoint(9, 6));
    QVERIFY(std::isfinite(chart.hoveredValue()));
    const QImage after = renderWidget(chart);
    QVERIFY2(before != after, "hovering the field must paint a marker and readout");

    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&chart, &leave);
    QCOMPARE(chart.hoveredCell(), QPoint(-1, -1));
    QVERIFY(std::isnan(chart.hoveredValue()));
}

void TestTopographyChart::filledBandsToggle()
{
    md::TopographyChart chart;
    chart.setField(16, 16, radialField(16, 16));
    chart.resize(360, 320);

    QVERIFY(chart.hasFilledBands());
    const QImage filled = renderWidget(chart);

    chart.setFilledBands(false);
    QVERIFY(!chart.hasFilledBands());
    const QImage plain = renderWidget(chart);

    QVERIFY2(filled != plain, "the band switch must change the painted field");
}

void TestTopographyChart::paintedPixels()
{
    md::TopographyChart chart;
    chart.setField(24, 20, radialField(24, 20));
    chart.resize(400, 360);

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());
    QVERIFY(pixelsDifferingFrom(image, Qt::transparent) > 1000);
}

void TestTopographyChart::emptyStatePaints()
{
    md::TopographyChart chart;
    chart.resize(320, 260);

    QCOMPARE(chart.rows(), 0);
    QCOMPARE(chart.fieldRect(), QRectF());

    const QImage image = renderWidget(chart);
    QVERIFY(!image.isNull());
    QVERIFY2(pixelsDifferingFrom(image, Qt::transparent) > 0,
             "the empty state must paint its label");
}

void TestTopographyChart::reducedMotionSnaps()
{
    md::TopographyChart chart;

    md::MotionScheme motion;
    motion.reducedMotion = true;
    md::ThemeScope* scope = md::ThemeScope::install(
        &chart, md::Theme::baselineLight().withMotion(motion));
    QVERIFY(scope != nullptr);

    chart.setField(12, 12, radialField(12, 12));
    chart.resize(300, 280);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.show();
    QCOMPARE(chart.revealProgress(), 1.0);
    QVERIFY(!chart.isRevealing());
    QVERIFY(pixelsDifferingFrom(renderWidget(chart), Qt::transparent) > 100);
}

void TestTopographyChart::revealReachesOne()
{
    md::TopographyChart chart;
    chart.setField(18, 18, radialField(18, 18));
    chart.resize(340, 320);

    QCOMPARE(chart.revealProgress(), 1.0);
    chart.show();

    QTRY_VERIFY_WITH_TIMEOUT(chart.isRevealing(), 2000);
    QVERIFY2(chart.revealProgress() < 1.0, "the default theme draws the contours on");
    QTRY_VERIFY_WITH_TIMEOUT(!chart.isRevealing(), 5000);
    QCOMPARE(chart.revealProgress(), 1.0);
}

QTEST_MAIN(TestTopographyChart)
#include "tst_topography_chart.moc"
