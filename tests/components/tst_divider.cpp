// Unit tests for the Divider component (framework.md §14).
//
// The widget is exercised offscreen. The application theme is pinned to the
// baseline light scheme, the divider is rendered into an explicitly
// transparent QImage (so unpainted pixels are deterministic rather than
// uninitialised) and the rule is sampled in logical coordinates, exactly as
// tests/components/tst_basic_widgets.cpp does it.
//
// The inset tests scan a whole scanline instead of sampling one point: the
// contract has two halves that a single sample cannot prove — the measured
// rectangle keeps its full size, while the *painted* rule is shortened at the
// leading and the trailing end.

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QRegion>
#include <QSize>
#include <QSizePolicy>
#include <QTest>
#include <QWidget>

#include <md/components/divider.h>
#include <md/theme/theme.h>

namespace {

const md::ColorScheme& lightColours()
{
    return md::Theme::applicationTheme().colours();
}

// Renders a widget over a transparent image with DrawChildren only:
// QWidget::render() would otherwise paint the palette's window colour first,
// and QWidget::grab() leaves unpainted pixels uninitialised.
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

QColor pixelAt(const QImage& image, qreal x, qreal y)
{
    const qreal ratio = image.devicePixelRatio();
    const int px = qBound(0, qRound(x * ratio), image.width() - 1);
    const int py = qBound(0, qRound(y * ratio), image.height() - 1);
    return image.pixelColor(px, py);
}

// The first and last painted logical coordinate of one scanline. The painter
// snaps the rule to device pixels, so sampling back at the same logical
// coordinate lands on the device pixel the rule was drawn into.
struct PaintSpan
{
    int first = -1;
    int last = -1;
};

PaintSpan paintedSpanX(const QImage& image, qreal y)
{
    PaintSpan span;
    const int logicalWidth = qRound(image.width() / image.devicePixelRatio());
    for (int x = 0; x < logicalWidth; ++x) {
        if (pixelAt(image, x, y).alpha() == 0)
            continue;

        if (span.first < 0)
            span.first = x;
        span.last = x;
    }
    return span;
}

PaintSpan paintedSpanY(const QImage& image, qreal x)
{
    PaintSpan span;
    const int logicalHeight = qRound(image.height() / image.devicePixelRatio());
    for (int y = 0; y < logicalHeight; ++y) {
        if (pixelAt(image, x, y).alpha() == 0)
            continue;

        if (span.first < 0)
            span.first = y;
        span.last = y;
    }
    return span;
}

} // namespace

class TestDivider : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void horizontalDefaults();
    void verticalSwapsAxes();
    void insetsShortenThePaintedRule();
    void verticalInsetsShortenThePaintedRule();
    void insetsMirrorInRightToLeft();
    void thicknessOverride();
    void orientationCanBeChanged();
};

void TestDivider::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestDivider::horizontalDefaults()
{
    md::Divider divider;

    QCOMPARE(divider.orientation(), md::Divider::Orientation::Horizontal);
    QCOMPARE(divider.thickness(), 1.0);
    QCOMPARE(divider.insetStart(), 0.0);
    QCOMPARE(divider.insetEnd(), 0.0);
    QCOMPARE(divider.focusPolicy(), Qt::NoFocus);
    QCOMPARE(divider.accessibleName(), QStringLiteral("Divider"));

    // The thin axis is the natural size; the fill axis has no preferred
    // length of its own under an unspecified spec.
    QCOMPARE(divider.sizeHint().height(), 1);
    QCOMPARE(divider.sizeHint().width(), 0);

    // An AtMost bound on the fill axis is the length to fill, and the height
    // is still exactly the 1 dp line.
    divider.measure(md::MeasureSpec::atMost(200.0), md::MeasureSpec::atMost(1.0));
    QCOMPARE(divider.measuredSize().width(), 200.0);
    QCOMPARE(divider.measuredSize().height(), 1.0);

    divider.resize(200, 1);
    const QImage image = renderWidget(divider);

    QCOMPARE(pixelAt(image, 100.0, 0.0).rgb(), lightColours().outlineVariant.rgb());
    QCOMPARE(pixelAt(image, 100.0, 0.0).alpha(), 255);
}

void TestDivider::verticalSwapsAxes()
{
    md::Divider divider(md::Divider::Orientation::Vertical);

    QCOMPARE(divider.orientation(), md::Divider::Orientation::Vertical);
    QCOMPARE(divider.accessibleName(), QStringLiteral("Separator"));

    // The axes swap: the width is the natural thickness, the height is the
    // fill axis.
    QCOMPARE(divider.sizeHint().width(), 1);
    QCOMPARE(divider.sizeHint().height(), 0);

    divider.measure(md::MeasureSpec::exactly(1.0), md::MeasureSpec::atMost(120.0));
    QCOMPARE(divider.measuredSize().width(), 1.0);
    QCOMPARE(divider.measuredSize().height(), 120.0);

    divider.resize(1, 120);
    const QImage image = renderWidget(divider);

    QCOMPARE(pixelAt(image, 0.0, 60.0).rgb(), lightColours().outlineVariant.rgb());
    QCOMPARE(pixelAt(image, 0.0, 60.0).alpha(), 255);
}

void TestDivider::insetsShortenThePaintedRule()
{
    md::Divider divider;
    divider.setInset(16.0, 24.0);

    QCOMPARE(divider.insetStart(), 16.0);
    QCOMPARE(divider.insetEnd(), 24.0);

    // Insets shrink the paint, never the measured rectangle.
    divider.measure(md::MeasureSpec::exactly(200.0), md::MeasureSpec::exactly(1.0));
    QCOMPARE(divider.measuredSize().width(), 200.0);
    QCOMPARE(divider.measuredSize().height(), 1.0);
    QCOMPARE(divider.sizeHint().height(), 1);

    divider.resize(200, 1);
    const QImage image = renderWidget(divider);

    // 200 px wide, 16 dp in from the left and 24 dp in from the right: the
    // rule covers [16, 176), so the last painted pixel is x = 175.
    const PaintSpan span = paintedSpanX(image, 0.0);
    QCOMPARE(span.first, 16);
    QCOMPARE(span.last, 175);

    QCOMPARE(pixelAt(image, 15.0, 0.0).alpha(), 0);
    QCOMPARE(pixelAt(image, 100.0, 0.0).rgb(), lightColours().outlineVariant.rgb());
    QCOMPARE(pixelAt(image, 176.0, 0.0).alpha(), 0);
    QCOMPARE(pixelAt(image, 199.0, 0.0).alpha(), 0);
}

void TestDivider::verticalInsetsShortenThePaintedRule()
{
    md::Divider divider(md::Divider::Orientation::Vertical);
    divider.setInset(10.0, 20.0);
    divider.resize(1, 120);

    const QImage image = renderWidget(divider);

    // A vertical rule insets from the top and the bottom.
    const PaintSpan span = paintedSpanY(image, 0.0);
    QCOMPARE(span.first, 10);
    QCOMPARE(span.last, 99);

    QCOMPARE(pixelAt(image, 0.0, 9.0).alpha(), 0);
    QCOMPARE(pixelAt(image, 0.0, 60.0).rgb(), lightColours().outlineVariant.rgb());
    QCOMPARE(pixelAt(image, 0.0, 100.0).alpha(), 0);
}

void TestDivider::insetsMirrorInRightToLeft()
{
    md::Divider divider;
    divider.setLayoutDirection(Qt::RightToLeft);
    divider.setInset(16.0, 24.0);
    divider.resize(200, 1);

    const QImage image = renderWidget(divider);

    // The leading 16 dp inset is now on the right and the trailing 24 dp one
    // on the left: the rule covers [24, 184).
    const PaintSpan span = paintedSpanX(image, 0.0);
    QCOMPARE(span.first, 24);
    QCOMPARE(span.last, 183);
    QCOMPARE(pixelAt(image, 100.0, 0.0).rgb(), lightColours().outlineVariant.rgb());
}

void TestDivider::thicknessOverride()
{
    md::Divider divider;
    divider.setThickness(4.0);
    QCOMPARE(divider.thickness(), 4.0);
    QCOMPARE(divider.sizeHint().height(), 4);

    divider.resize(120, 4);
    QImage image = renderWidget(divider);
    for (int y = 0; y < 4; ++y) {
        QCOMPARE(pixelAt(image, 60.0, y).rgb(), lightColours().outlineVariant.rgb());
    }

    divider.setThickness(2.0);
    QCOMPARE(divider.thickness(), 2.0);
    QCOMPARE(divider.sizeHint().height(), 2);

    divider.resize(120, 2);
    image = renderWidget(divider);
    for (int y = 0; y < 2; ++y) {
        QCOMPARE(pixelAt(image, 60.0, y).rgb(), lightColours().outlineVariant.rgb());
    }

    // Zero (or any non-positive value) hides the rule without invalidating
    // the widget's geometry contract.
    divider.setThickness(0.0);
    QCOMPARE(divider.thickness(), 0.0);
    divider.resize(120, 1);
    image = renderWidget(divider);
    QCOMPARE(pixelAt(image, 60.0, 0.0).alpha(), 0);

    divider.setThickness(-3.0);
    QCOMPARE(divider.thickness(), 0.0);
}

void TestDivider::orientationCanBeChanged()
{
    md::Divider divider;
    divider.setOrientation(md::Divider::Orientation::Vertical);

    QCOMPARE(divider.orientation(), md::Divider::Orientation::Vertical);
    QCOMPARE(divider.sizeHint().width(), 1);
    QCOMPARE(divider.sizeHint().height(), 0);
    QCOMPARE(divider.accessibleName(), QStringLiteral("Separator"));
    QCOMPARE(divider.sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
    QCOMPARE(divider.sizePolicy().verticalPolicy(), QSizePolicy::Expanding);

    divider.setOrientation(md::Divider::Orientation::Horizontal);

    QCOMPARE(divider.orientation(), md::Divider::Orientation::Horizontal);
    QCOMPARE(divider.sizeHint().height(), 1);
    QCOMPARE(divider.accessibleName(), QStringLiteral("Divider"));
    QCOMPARE(divider.sizePolicy().horizontalPolicy(), QSizePolicy::Expanding);
    QCOMPARE(divider.sizePolicy().verticalPolicy(), QSizePolicy::Fixed);
}

QTEST_MAIN(TestDivider)
#include "tst_divider.moc"
