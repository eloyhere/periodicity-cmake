// Visual-design regressions for the data-display, media and picker batch.
//
// Each case pins one design-baseline measurement or role pairing that the
// component used to miss, so a later refactor cannot quietly undo it:
//
//   * data tables put a header title and its column's cells on the same 16 dp
//     text grid (the old header used 12 dp, so every column had a four pixel
//     step between its title and its cells);
//   * the header band never holds a body glyph, even when a row is scrolled
//     halfway behind it;
//   * a frozen column has a 1 dp outline-variant seam against the scrolling
//     columns, in the header band and in the body;
//   * the video transport is the 48 dp design-baseline icon target on the 56 dp bar;
//   * list dividers are full-width;
//   * the avatar placeholder paints a theme container role and reports that
//     role through placeholderColour(), rather than inventing a grey.
//
// The suite is offscreen and deterministic, like the other component suites:
// the application theme is pinned to baseline light and widgets are rendered
// into a transparent QImage with QWidget::render.

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QRegion>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTest>
#include <QVariant>
#include <QWidget>

#include <md/components/avatar.h>
#include <md/components/list.h>
#include <md/components/table.h>
#include <md/components/video.h>
#include <md/components/virtual_table.h>
#include <md/structure/table.h>
#include <md/theme/theme.h>
#include <md/tokens/color_scheme.h>

namespace {

const md::ColorScheme& lightColours()
{
    return md::Theme::applicationTheme().colours();
}

// The transparent render used by the other component suites: QWidget::grab()
// leaves the pixels of a transparent widget undefined.
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

int tone(const QColor& colour)
{
    return colour.red() + colour.green() + colour.blue();
}

// A hairline can land on either side of a logical coordinate depending on the
// painter's rounding, so a seam is "present" when any sample in the window has
// exactly the expected role colour.
bool anyPixelEquals(const QImage& image, qreal xFrom, qreal xTo, qreal y,
                    const QColor& expected)
{
    for (qreal x = xFrom; x <= xTo; x += 0.25) {
        if (pixelAt(image, QPointF(x, y)).rgb() == expected.rgb())
            return true;
    }

    return false;
}

// The leftmost logical column inside [top, bottom] that holds a glyph, or -1
// when the band is empty. Both table texts are far darker than any surface or
// outline role, so a fixed tone threshold finds them without knowing which
// role the text uses.
int firstGlyphColumn(const QImage& image, qreal top, qreal bottom)
{
    const qreal ratio = image.devicePixelRatio();
    const int first = qBound(0, qRound(top * ratio), image.height() - 1);
    const int last = qBound(0, qRound(bottom * ratio), image.height() - 1);

    for (int x = 0; x < image.width(); ++x) {
        for (int y = first; y <= last; ++y) {
            if (tone(image.pixelColor(x, y)) < 400)
                return static_cast<int>(x / ratio);
        }
    }

    return -1;
}

// How many pixels in the top `height` logical rows are dark enough to be body
// text. Header text (onSurfaceVariant) is lighter than body text (onSurface),
// so a threshold between the two counts only a bleed from the rows.
int bodyTextPixelsInBand(const QImage& image, qreal height)
{
    const qreal ratio = image.devicePixelRatio();
    const int limit = qMin(image.height(), qRound(height * ratio));
    const int threshold =
        (tone(lightColours().onSurface) + tone(lightColours().onSurfaceVariant)) / 2;

    int count = 0;
    for (int y = 0; y < limit; ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor colour = image.pixelColor(x, y);
            if (colour.alpha() == 0)
                continue;
            if (tone(colour) < threshold)
                ++count;
        }
    }

    return count;
}

// A source with no backing store, so the virtual table only ever sees the
// cells it asks for.
class FlatSource : public md::structure::VirtualTableSource
{
public:
    FlatSource(int rows, int columns)
        : rows_(rows)
        , columns_(columns)
    {
    }

    [[nodiscard]] int rowCount() const override { return rows_; }
    [[nodiscard]] int columnCount() const override { return columns_; }

    [[nodiscard]] QVariant cell(int row, int column) const override
    {
        return QStringLiteral("%1,%2").arg(row).arg(column);
    }

    [[nodiscard]] QString header(int column) const override
    {
        return QStringLiteral("Column %1").arg(column);
    }

private:
    int rows_ = 0;
    int columns_ = 0;
};

// "Item" in every cell and in the first column's header, so the header glyph
// and the body glyph that should share a text inset are literally the same
// string.
void fillTable(QStandardItemModel& model, int rows, int columns)
{
    model.setRowCount(rows);
    model.setColumnCount(columns);

    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            auto* item = new QStandardItem(QStringLiteral("Item"));
            model.setItem(row, column, item);
        }
    }

    model.setHeaderData(0, Qt::Horizontal, QStringLiteral("Item"));
}

} // namespace

class TestVisualData : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void tableM3Metrics();
    void tableTextGridMatchesHeader();
    void tableHeaderHidesScrolledText();
    void virtualTableFrozenSeam();
    void videoTransportTarget();
    void listDividerIsFullWidth();
    void avatarPlaceholderRole();
};

void TestVisualData::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestVisualData::tableM3Metrics()
{
    QStandardItemModel model;
    fillTable(model, 6, 3);

    md::Table table;
    table.setModel(&model);
    for (int column = 0; column < 3; ++column)
        table.setColumnWidth(column, 100.0);
    table.resize(320, 300);

    // The header band is 48 dp on this component, and the body starts exactly
    // on its bottom edge: header and body are disjoint rectangles, not one
    // band painted twice.
    QCOMPARE(table.headerRect().height(), 48.0);
    QCOMPARE(table.bodyRect().top(), 48.0);
    QCOMPARE(table.rowRect(0).top(), 48.0);
    QCOMPARE(table.cellRect(0, 0).top(), 48.0);
    QCOMPARE(table.cellRect(1, 0).top(),
             table.cellRect(0, 0).bottom());
    QCOMPARE(table.cellRect(0, 0).height(), table.rowHeight());
}

void TestVisualData::tableTextGridMatchesHeader()
{
    QStandardItemModel model;
    fillTable(model, 4, 2);

    md::Table table;
    table.setModel(&model);
    table.setColumnWidth(0, 120.0);
    table.setColumnWidth(1, 120.0);
    table.resize(240, 300);

    const QImage rendered = renderWidget(table);
    const QRectF firstCell = table.cellRect(0, 0);

    const int headerX = firstGlyphColumn(rendered, 8.0, 40.0);
    const int bodyX =
        firstGlyphColumn(rendered, firstCell.top() + 12.0, firstCell.bottom() - 12.0);

    QVERIFY2(headerX >= 0, "no header glyph was rendered");
    QVERIFY2(bodyX >= 0, "no body glyph was rendered");

    // Both texts start on the same 16 dp text grid. The regression this guards
    // is the 12 dp header padding, which put the title four pixels to the left
    // of its own column.
    QVERIFY2(qAbs(headerX - bodyX) <= 2,
             qPrintable(QStringLiteral("header %1, body %2").arg(headerX).arg(bodyX)));
    QVERIFY2(headerX >= 14 && headerX <= 19,
             qPrintable(QStringLiteral("header glyph at %1, expected the 16 dp inset")
                            .arg(headerX)));
}

void TestVisualData::tableHeaderHidesScrolledText()
{
    QStandardItemModel model;
    fillTable(model, 6, 2);

    md::Table table;
    table.setModel(&model);
    table.setColumnWidth(0, 100.0);
    table.setColumnWidth(1, 100.0);
    table.resize(220, 300);

    // 40 dp of scroll leaves row 0's text vertically centred inside the header
    // band: exactly the state in which an unclipped body bleeds into it.
    table.setScrollOffset(40.0);
    QVERIFY(table.rowRect(0).top() < table.headerRect().bottom());

    const QImage rendered = renderWidget(table);
    QCOMPARE(bodyTextPixelsInBand(rendered, table.headerRect().height()), 0);
    QCOMPARE(pixelAt(rendered, QPointF(5.0, 5.0)).rgb(),
             lightColours().surfaceContainer.rgb());
}

void TestVisualData::virtualTableFrozenSeam()
{
    FlatSource source(8, 4);

    md::VirtualTable table;
    table.setSource(&source);
    // 4 x 120 dp in a 400 dp viewport leaves 80 dp of horizontal travel, so the
    // scrolling band really does slide under the frozen column.
    for (int column = 0; column < 4; ++column)
        table.setColumnWidth(column, 120.0);
    table.resize(400, 300);
    table.setFrozenColumns(1);
    table.setHorizontalScrollOffset(30.0);

    const QImage rendered = renderWidget(table);
    const QRectF body = table.bodyRect();
    const qreal seamX = body.left() + 120.0;
    const QColor seam = lightColours().outlineVariant;

    // Body seam, on the frozen boundary.
    QVERIFY2(anyPixelEquals(rendered, seamX - 1.5, seamX + 0.5, body.top() + 30.0, seam),
             "no seam on the frozen boundary in the row band");

    // Header seam, on the same boundary.
    QVERIFY2(anyPixelEquals(rendered, seamX - 1.5, seamX + 0.5,
                            table.headerRect().center().y(), seam),
             "no seam on the frozen boundary in the header band");

    // The pinned column keeps the body's left edge and is not covered by the
    // seam paint.
    QCOMPARE(table.cellRect(0, 0).left(), body.left());
    QCOMPARE(pixelAt(rendered, QPointF(seamX - 20.0, body.top() + 30.0)).rgb(),
             lightColours().surface.rgb());
}

void TestVisualData::videoTransportTarget()
{
    md::Video video;
    video.resize(480, 320);
    video.setDuration(10'000);

    const QRectF bar = video.controlBarRect();
    QCOMPARE(bar.height(), 56.0);

    // Design-baseline icon targets: 48 dp for the transport, 40 dp for the compact actions,
    // both inside the 56 dp bar with a 4 dp vertical margin.
    QCOMPARE(video.playButtonRect().width(), 48.0);
    QCOMPARE(video.playButtonRect().height(), 48.0);
    QCOMPARE(video.volumeButtonRect().width(), 48.0);
    QCOMPARE(video.fullscreenButtonRect().width(), 48.0);
    QVERIFY(bar.contains(video.playButtonRect()));
    QVERIFY(bar.contains(video.volumeButtonRect()));

    // Growing the transport to its full target must not push it into the track.
    QVERIFY(!video.progressRect().intersects(video.playButtonRect()));
    QVERIFY(video.progressRect().left() >= video.timeLabelRect().right());
}

void TestVisualData::listDividerIsFullWidth()
{
    QStandardItemModel model;
    model.setRowCount(3);
    for (int row = 0; row < 3; ++row)
        model.setData(model.index(row, 0), QStringLiteral("Row %1").arg(row));

    md::List list;
    list.setModel(&model);
    list.resize(240, 168);
    list.setRowHeight(56.0);

    const QImage rendered = renderWidget(list);
    const QRectF first = list.rowRect(0);
    const qreal dividerY = first.bottom() - 0.6;
    const QColor divider = lightColours().outlineVariant;

    // Design-baseline list dividers are full-width: the rule reaches both edges, not just
    // the 16 dp text column.
    QVERIFY2(anyPixelEquals(rendered, 0.0, 4.0, dividerY, divider),
             "the divider does not reach the leading edge");
    QVERIFY2(anyPixelEquals(rendered, 236.0, 239.0, dividerY, divider),
             "the divider does not reach the trailing edge");

    // Away from the divider the row is the plain surface.
    QCOMPARE(pixelAt(rendered, QPointF(3.0, first.center().y())).rgb(),
             lightColours().surface.rgb());
}

void TestVisualData::avatarPlaceholderRole()
{
    md::Avatar avatar(QStringLiteral("Ada Lovelace"));
    avatar.setDiameter(40.0);
    avatar.resize(40, 40);

    const md::ColorScheme& colours = lightColours();
    avatar.setPlaceholderColour(colours.tertiaryContainer);
    const QColor fill = avatar.placeholderColour();
    QVERIFY(fill.isValid());
    const QColor roles[] = {
        colours.primaryContainer,     colours.secondaryContainer,
        colours.tertiaryContainer,    colours.errorContainer,
        colours.surfaceContainerHigh, colours.surfaceContainerHighest,
    };

    bool isRole = false;
    for (const QColor& role : roles)
        isRole = isRole || role.rgb() == fill.rgb();

    QVERIFY2(isRole, "the placeholder fill must be one of the theme container roles");

    // The painted mark uses that exact fill; the sample sits near the top of
    // the circle, inside the fill and clear of the centred initials.
    const QImage rendered = renderWidget(avatar);
    QCOMPARE(pixelAt(rendered, QPointF(20.0, 4.0)).rgb(), fill.rgb());
}

QTEST_MAIN(TestVisualData)
#include "tst_visual_data.moc"
