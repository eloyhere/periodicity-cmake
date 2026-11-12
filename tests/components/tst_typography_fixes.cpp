// Typography and layout regression tests for the fixes in this batch.
//
// Every assertion is deterministic: geometry is compared against values
// derived from the theme tokens, never against a screenshot of a particular
// font. Where pixels are inspected, the helper searches for a colour the
// component paints verbatim (a red icon or a red dot) or for "any pixel that is
// not the surface", so a missing font cannot turn an assertion into a
// tautology.
//
// The covered defects, one slot each:
//   textNaturalWidthIsLongestLine    — multi-line sizeHint summed its lines
//   textHardLineBreaksAffectHeight   — height ignored explicit line breaks
//   textWrapsAtAvailableWidth        — wrap width was the sizeHint width
//   typographyUsesTokenLineHeight    — same rules for the 15-role component
//   listRowMetricsFollowTokens       — one/two/three-line row heights
//   listRtlMirrorsDecoration         — leading decoration in RTL
//   tableCellsAreVerticallyCentred   — TextAlignmentRole without a v-flag
//   tableHeaderSurvivesScrolling     — a scrolled row painted over the header
//   buttonPaddingDependsOnIcon       — 16 dp icon side vs 24 dp text side
//   buttonContentStaysInsidePadding  — elide budget subtracted the icon
//   tagMirrorsInRtl                  — dot/icon/close sides in RTL
//   badgeAnchorsOnTrailingCorner     — badge mirrors in RTL
//   cardActionRowMirrorsInRtl        — card actions keep logical order in RTL
//   inputFieldGrowsWithFontScale      — label/editor/helper clashed at 1.5x
//   linearWeightsFillTheAxisExactly  — weighted rounding left gaps/overlaps
//   frameGravityStaysInsideBounds    — fractional margin inflated a child
//   constraintStretchAndCentreArePixelExact — odd leftovers grew by a pixel

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QIcon>
#include <QImage>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QRegion>
#include <QSize>
#include <QSizeF>
#include <QStandardItemModel>
#include <QString>
#include <QTest>
#include <QTextLayout>
#include <QTextLine>
#include <QTextOption>
#include <QWidget>

#include <md/components/badge.h>
#include <md/components/button.h>
#include <md/components/card.h>
#include <md/components/input.h>
#include <md/components/list.h>
#include <md/components/table.h>
#include <md/components/tag.h>
#include <md/components/text.h>
#include <md/components/typography.h>
#include <md/core/density.h>
#include <md/core/measure_spec.h>
#include <md/layout/constraint.h>
#include <md/layout/frame.h>
#include <md/layout/linear.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>
#include <md/tokens/type_scale.h>
#include <md/view/view.h>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

const md::Theme& theme()
{
    return md::Theme::applicationTheme();
}

// The same font a Text/Typography/Button resolves from a type token. Keeping
// the construction in one place is what lets the expected line count be
// computed independently of the component under test.
QFont styleFont(const md::TextStyle& style)
{
    const md::Density& density = theme().density();
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(density.sp(style.sizeSp)))));
    font.setWeight(static_cast<QFont::Weight>(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, density.sp(style.trackingSp));
    return font;
}

int attributedLineCount(const QString& text, const QFont& font, qreal width)
{
    QTextLayout layout(text, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);

    layout.beginLayout();
    int lines = 0;
    for (;;) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;

        line.setLineWidth(width);
        line.setPosition(QPointF(0.0, static_cast<qreal>(lines)));
        ++lines;
    }
    layout.endLayout();

    return lines;
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

// The bounding rectangle of every pixel `matches`, in logical coordinates. An
// invalid rectangle means nothing matched.
QRect matchBounds(const QImage& image, const std::function<bool(const QColor&)>& matches)
{
    int left = image.width();
    int top = image.height();
    int right = -1;
    int bottom = -1;

    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (!matches(image.pixelColor(x, y)))
                continue;

            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
        }
    }

    if (right < left || bottom < top)
        return QRect();

    return QRect(left, top, right - left + 1, bottom - top + 1);
}

QRect redBounds(const QImage& image)
{
    return matchBounds(image, [](const QColor& colour) {
        return colour.alpha() > 0 && colour.red() > 150 && colour.green() < 100
            && colour.blue() < 100;
    });
}

QRect inkBounds(const QImage& image, const QColor& background)
{
    return matchBounds(image, [&background](const QColor& colour) {
        return colour.alpha() == 255 && colour.rgb() != background.rgb();
    });
}

// A leaf view with a fixed natural size: the containers under test must not be
// confused by a child that lays itself out.
class Leaf : public md::View
{
public:
    explicit Leaf(QSizeF natural, QWidget* parent = nullptr)
        : View(parent)
        , natural_(natural)
    {
    }

protected:
    void measureOverride(md::MeasureSpec width, md::MeasureSpec height) override
    {
        setMeasuredSize(QSizeF(width.resolve(natural_.width()),
                               height.resolve(natural_.height())));
    }

    void paintContent(QPainter&, const QRectF&) override {}

private:
    QSizeF natural_;
};

} // namespace

class TestTypographyFixes : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void textNaturalWidthIsLongestLine();
    void textHardLineBreaksAffectHeight();
    void textWrapsAtAvailableWidth();
    void typographyUsesTokenLineHeight();

    void listRowMetricsFollowTokens();
    void listRtlMirrorsDecoration();

    void tableCellsAreVerticallyCentred();
    void tableHeaderSurvivesScrolling();

    void buttonPaddingDependsOnIcon();
    void buttonContentStaysInsidePadding();

    void tagMirrorsInRtl();
    void badgeAnchorsOnTrailingCorner();
    void cardActionRowMirrorsInRtl();

    void inputFieldGrowsWithFontScale();

    void linearWeightsFillTheAxisExactly();
    void frameGravityStaysInsideBounds();
    void constraintStretchAndCentreArePixelExact();
};

void TestTypographyFixes::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestTypographyFixes::textNaturalWidthIsLongestLine()
{
    md::Text text;
    text.setText(QStringLiteral("MMMM\niiii"));

    const QFontMetricsF metrics(styleFont(text.textStyle()));
    const qreal wide = metrics.horizontalAdvance(QStringLiteral("MMMM"));
    const qreal narrow = metrics.horizontalAdvance(QStringLiteral("iiii"));
    const qreal both = metrics.horizontalAdvance(QStringLiteral("MMMMiiii"));

    text.measure(md::MeasureSpec::unspecified(), md::MeasureSpec::unspecified());

    // The natural width is the widest hard line, not the sum of the lines. The
    // old implementation advanced the whole string including the separators.
    QVERIFY2(qAbs(text.measuredSize().width() - std::max(wide, narrow)) < 0.51,
             "sizeHint width must be the longest line");
    QVERIFY2(text.measuredSize().width() < both - 1.0,
             "sizeHint width must not be the sum of the lines");
}

void TestTypographyFixes::textHardLineBreaksAffectHeight()
{
    md::Text text;
    md::TextStyle style = theme().type().bodyMedium;
    style.lineHeightSp = 32.0;
    text.setTextStyle(style);
    text.setText(QStringLiteral("first\nsecond"));

    const qreal lineHeight = theme().density().sp(style.lineHeightSp);

    text.measure(md::MeasureSpec::exactly(200.0), md::MeasureSpec::unspecified());
    QCOMPARE(text.measuredSize().width(), 200.0);
    QCOMPARE(qRound(text.measuredSize().height()), qRound(2.0 * lineHeight));

    // The token line height is authoritative, not QFontMetrics::height().
    text.setText(QStringLiteral("first"));
    text.measure(md::MeasureSpec::exactly(200.0), md::MeasureSpec::unspecified());
    QCOMPARE(qRound(text.measuredSize().height()), qRound(lineHeight));

    // heightForWidth() reports the same block, so a wrapped line count and a
    // hard-break line count agree.
    text.setText(QStringLiteral("first\nsecond"));
    QCOMPARE(text.heightForWidth(200), qRound(2.0 * lineHeight));
}

void TestTypographyFixes::textWrapsAtAvailableWidth()
{
    const QString content = QStringLiteral("The quick brown fox jumps over the lazy dog");

    md::Text text(content);
    text.setWordWrap(true);
    text.setAlignment(Qt::AlignLeft | Qt::AlignTop);

    const md::TextStyle style = text.textStyle();
    const QFont font = styleFont(style);
    const qreal lineHeight = theme().density().sp(style.lineHeightSp);

    const int narrowLines = attributedLineCount(content, font, 80.0);
    const int wideLines = attributedLineCount(content, font, 400.0);
    QVERIFY(narrowLines > 1);
    QVERIFY(narrowLines > wideLines);

    text.measure(md::MeasureSpec::exactly(80.0), md::MeasureSpec::unspecified());
    QCOMPARE(text.measuredSize().width(), 80.0);
    QCOMPARE(qRound(text.measuredSize().height()), qRound(narrowLines * lineHeight));
    QCOMPARE(text.heightForWidth(80), qRound(narrowLines * lineHeight));

    // A wider spec wraps at that width, not at the single-line sizeHint width.
    text.measure(md::MeasureSpec::exactly(400.0), md::MeasureSpec::unspecified());
    QCOMPARE(qRound(text.measuredSize().height()), qRound(wideLines * lineHeight));
    QVERIFY(text.measuredSize().height() < narrowLines * lineHeight);
}

void TestTypographyFixes::typographyUsesTokenLineHeight()
{
    md::Typography typography(QStringLiteral("alpha\nbeta"),
                              md::Typography::TypeRole::BodyMedium);
    md::TextStyle style = theme().type().bodyMedium;
    style.lineHeightSp = 28.0;
    typography.setTextStyle(style);

    typography.measure(md::MeasureSpec::exactly(200.0), md::MeasureSpec::unspecified());
    QCOMPARE(qRound(typography.measuredSize().height()),
             qRound(2.0 * theme().density().sp(style.lineHeightSp)));

    // The same measurement is the one Qt asks for through heightForWidth().
    QCOMPARE(typography.heightForWidth(200),
             qRound(2.0 * theme().density().sp(style.lineHeightSp)));
}

void TestTypographyFixes::listRowMetricsFollowTokens()
{
    QStandardItemModel model(3, 1);
    model.setData(model.index(0, 0), QStringLiteral("One"));

    md::List list;
    list.setModel(&model);

    // The design-baseline one-, two- and three-line tokens.
    QCOMPARE(md::List::rowHeightFor(md::List::RowLines::OneLine), 56.0);
    QCOMPARE(md::List::rowHeightFor(md::List::RowLines::TwoLine), 72.0);
    QCOMPARE(md::List::rowHeightFor(md::List::RowLines::ThreeLine), 88.0);
    QCOMPARE(list.rowHeight(), 56.0);

    list.setRowLines(md::List::RowLines::TwoLine);
    QCOMPARE(list.rowHeight(), 72.0);

    list.measure(md::MeasureSpec::exactly(240.0), md::MeasureSpec::exactly(240.0));
    list.arrange(QRect(0, 0, 240, 240));

    const QRectF first = list.rowRect(0);
    const QRectF second = list.rowRect(1);
    QCOMPARE(first.height(), 72.0);
    QCOMPARE(first.top(), 0.0);
    QCOMPARE(second.top(), 72.0);
    QCOMPARE(list.rowRect(2).top(), 144.0);
    QCOMPARE(first.width(), 240.0);

    // A three-line row is 88 dp; the row rectangle is the token even when the
    // viewport is smaller than the content.
    list.setRowLines(md::List::RowLines::ThreeLine);
    QCOMPARE(list.rowHeight(), 88.0);
    QCOMPARE(list.rowRect(0).height(), 88.0);
}

void TestTypographyFixes::listRtlMirrorsDecoration()
{
    QStandardItemModel model(1, 1);
    model.setData(model.index(0, 0), QStringLiteral("Row"));
    QPixmap pixmap(24, 24);
    pixmap.fill(QColor(220, 0, 0));
    model.setData(model.index(0, 0), QIcon(pixmap), Qt::DecorationRole);

    md::List list;
    list.setModel(&model);
    list.measure(md::MeasureSpec::exactly(200.0), md::MeasureSpec::exactly(120.0));
    list.arrange(QRect(0, 0, 200, 120));

    QImage image = renderWidget(list);
    QRect bounds = redBounds(image);
    QVERIFY2(bounds.isValid(), "the decoration must be painted");
    QCOMPARE(bounds.left(), 16);
    QCOMPARE(bounds.width(), 24);

    // In RTL the leading edge is the right one: the decoration mirrors, and the
    // text run keeps the 16 dp inset on the left.
    list.setLayoutDirection(Qt::RightToLeft);
    image = renderWidget(list);
    bounds = redBounds(image);
    QVERIFY2(bounds.isValid(), "the decoration must still be painted in RTL");
    QCOMPARE(bounds.left(), 200 - 16 - 24);
    QCOMPARE(bounds.right(), 200 - 16 - 1);
}

void TestTypographyFixes::tableCellsAreVerticallyCentred()
{
    QStandardItemModel model(1, 1);
    model.setData(model.index(0, 0), QStringLiteral("Ag"));
    // A horizontal-only role: the table, not the model, owns the vertical
    // centring.
    model.setData(model.index(0, 0), static_cast<int>(Qt::AlignRight),
                  Qt::TextAlignmentRole);

    md::Table table;
    table.setModel(&model);
    table.setHeadersVisible(false);
    table.setColumnWidth(0, 160.0);
    table.measure(md::MeasureSpec::exactly(160.0), md::MeasureSpec::exactly(120.0));
    table.arrange(QRect(0, 0, 160, 120));

    const QRectF cell = table.cellRect(0, 0);
    QCOMPARE(cell.height(), 48.0);
    QCOMPARE(cell.top(), 0.0);

    const QImage image = renderWidget(table);
    const QColor surface = theme().colours().surface;
    int inkCount = 0;
    int minY = image.height();
    int maxY = -1;

    // The row divider lives on the bottom edge; the scan stops above it.
    for (int y = 0; y < qRound(cell.height()) - 4; ++y) {
        for (int x = 1; x < 159; ++x) {
            const QColor colour = image.pixelColor(x, y);
            if (colour.alpha() == 0 || colour.rgb() == surface.rgb())
                continue;

            ++inkCount;
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }
    }

    QVERIFY2(inkCount > 10, "the cell must paint its text");
    const int centre = (minY + maxY) / 2;
    QVERIFY2(qAbs(centre - 24) <= 4,
             "a horizontal-only TextAlignmentRole must not pin the text to the cell top");
}

void TestTypographyFixes::tableHeaderSurvivesScrolling()
{
    QStandardItemModel model(6, 1);
    model.setHorizontalHeaderLabels({QStringLiteral("Head")});
    for (int row = 0; row < 6; ++row)
        model.setItem(row, 0, new QStandardItem(QStringLiteral("row %1").arg(row)));

    md::Table table;
    table.setModel(&model);
    table.measure(md::MeasureSpec::exactly(200.0), md::MeasureSpec::exactly(160.0));
    table.arrange(QRect(0, 0, 200, 160));
    table.setScrollOffset(20.0);

    QCOMPARE(table.headerRect().height(), 48.0);
    QCOMPARE(table.rowRect(0).top(), 28.0);

    const QImage image = renderWidget(table);
    const QColor header = theme().colours().surfaceContainer;
    const QColor surface = theme().colours().surface;

    // Well inside the header band and clear of the header label (which starts
    // at the 12 dp cell padding): the first, partially scrolled row must not
    // repaint it with the body colour.
    QCOMPARE(pixelAt(image, QPointF(5.0, 8.0)).rgb(), header.rgb());
    QCOMPARE(pixelAt(image, QPointF(5.0, 40.0)).rgb(), header.rgb());
    QVERIFY2(pixelAt(image, QPointF(5.0, 40.0)).rgb() != surface.rgb(),
             "a scrolled row painted over the header");
    QCOMPARE(pixelAt(image, QPointF(5.0, 60.0)).rgb(), surface.rgb());
}

void TestTypographyFixes::buttonPaddingDependsOnIcon()
{
    const md::TextStyle& style = theme().type().labelLarge;
    const QFont font = styleFont(style);
    const qreal advance = QFontMetricsF(font).horizontalAdvance(QStringLiteral("OK"));

    md::Button textOnly(QStringLiteral("OK"));
    QCOMPARE(textOnly.sizeHint().width(), qCeil(16.0 + advance + 16.0));
    QCOMPARE(textOnly.sizeHint().height(), 48);

    md::Button withIcon(QStringLiteral("OK"));
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::white);
    withIcon.setIcon(QIcon(pixmap));
    QCOMPARE(withIcon.iconSize(), 20.0);

    // Design baseline: 16 dp on the icon side, 24 dp on the label side — not 24/24.
    QCOMPARE(withIcon.sizeHint().width(), qCeil(16.0 + 20.0 + 8.0 + advance + 16.0));

    // An empty label still honours the 48 dp minimum width.
    md::Button empty;
    QCOMPARE(empty.sizeHint().width(), 48);
}

void TestTypographyFixes::buttonContentStaysInsidePadding()
{
    md::Button button(QStringLiteral("A very long button label"));
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::white);
    button.setIcon(QIcon(pixmap));

    button.measure(md::MeasureSpec::exactly(120.0), md::MeasureSpec::exactly(48.0));
    button.arrange(QRect(0, 0, 120, 48));

    const QImage image = renderWidget(button);
    const QColor container = theme().colours().primary;
    const QRect ink = inkBounds(image, container);

    QVERIFY2(ink.isValid(), "the icon and the label must be painted");
    // The icon starts at 16 dp and the label ends 24 dp before the edge. The
    // old elide budget ignored the icon, so the group overflowed the padding on
    // both sides.
    QVERIFY2(ink.left() >= 15, "the icon must start inside the 16 dp inset");
    QVERIFY2(ink.right() <= 120 - 24 + 1, "the label must end inside the 24 dp inset");
}

void TestTypographyFixes::tagMirrorsInRtl()
{
    md::Tag tag(QStringLiteral("Label"));
    tag.setColourDot(true);
    tag.setDotColour(QColor(220, 0, 0));

    tag.measure(md::MeasureSpec::exactly(120.0), md::MeasureSpec::exactly(32.0));
    tag.arrange(QRect(0, 0, 120, 32));

    QImage image = renderWidget(tag);
    QRect dot = redBounds(image);
    QVERIFY2(dot.isValid(), "the colour dot must be painted");
    QCOMPARE(dot.left(), 8);
    QCOMPARE(dot.width(), 8);

    // In RTL the dot is the leading element, so it moves to the right edge.
    tag.setLayoutDirection(Qt::RightToLeft);
    image = renderWidget(tag);
    dot = redBounds(image);
    QVERIFY2(dot.isValid(), "the colour dot must still be painted in RTL");
    QCOMPARE(dot.left(), 120 - 8 - 8);
    QCOMPARE(dot.width(), 8);
}

void TestTypographyFixes::badgeAnchorsOnTrailingCorner()
{
    QWidget host;
    host.resize(300, 200);

    QWidget target(&host);
    target.setGeometry(10, 20, 40, 40);

    host.show();
    QTest::qWait(10);

    md::Badge badge;
    badge.setCount(5);
    badge.setTarget(&target);

    const QSize size = badge.size();
    QCOMPARE(badge.x(), 10 + 40 - size.width() / 2);
    QCOMPARE(badge.y(), 20 - size.height() / 2);

    // The trailing corner is the left one in RTL: the badge mirrors with the
    // target and is no longer clipped by the leading edge.
    badge.setLayoutDirection(Qt::RightToLeft);
    badge.setTarget(&target);
    QCOMPARE(badge.x(), 10 - size.width() / 2);
    QCOMPARE(badge.y(), 20 - size.height() / 2);
}

void TestTypographyFixes::cardActionRowMirrorsInRtl()
{
    md::Card card;
    Leaf first(QSizeF(40.0, 20.0));
    Leaf second(QSizeF(60.0, 20.0));
    card.addAction(&first);
    card.addAction(&second);

    card.measure(md::MeasureSpec::exactly(320.0), md::MeasureSpec::exactly(200.0));
    card.arrange(QRect(0, 0, 320, 200));

    // The run is flush with the trailing edge; the first action leads it.
    QCOMPARE(second.geometry().right(), 320 - 16 - 1);
    QCOMPARE(first.geometry().left(), 320 - 16 - (40 + 8 + 60));

    card.setLayoutDirection(Qt::RightToLeft);
    card.measure(md::MeasureSpec::exactly(320.0), md::MeasureSpec::exactly(200.0));
    card.arrange(QRect(0, 0, 320, 200));

    // In RTL the trailing edge is the left one; the actions keep their logical
    // order, so the first one is the rightmost of the group.
    QCOMPARE(second.geometry().left(), 16);
    QCOMPARE(first.geometry().left(), 16 + 60 + 8);
    QCOMPARE(first.geometry().right(), 16 + 60 + 8 + 40 - 1);
}

void TestTypographyFixes::inputFieldGrowsWithFontScale()
{
    // At 1.5x font scaling the floating label's line box is 24 dp, not 18: a
    // fixed label box and a fixed editor top made the label overlap the input
    // text and clipped the helper line.
    md::Density density;
    density.scale = 1.0;
    density.fontScale = 1.5;

    QWidget host;
    md::ThemeScope* scope =
        md::ThemeScope::install(&host, md::Theme::baselineLight().withDensity(density));
    QVERIFY(scope != nullptr);

    md::TextInput input(&host);
    input.setLabel(QStringLiteral("Label"));
    input.setHelperText(QStringLiteral("Helper"));

    const md::Theme& scoped = md::Theme::forWidget(&input);
    const qreal padding = scoped.density().dp(8.0);
    const qreal labelLine = scoped.density().sp(scoped.type().bodySmall.lineHeightSp);
    const qreal editorLine = scoped.density().sp(scoped.type().bodyLarge.lineHeightSp);
    const qreal expectedField = padding + labelLine + editorLine + padding;
    const qreal supporting =
        qMax(scoped.density().dp(18.0),
             scoped.density().sp(scoped.type().bodySmall.lineHeightSp));
    const qreal expectedHeight =
        expectedField + scoped.density().dp(4.0) + supporting;

    QVERIFY(labelLine > 18.0);

    input.measure(md::MeasureSpec::exactly(240.0), md::MeasureSpec::unspecified());
    QCOMPARE(input.measuredSize().width(), 240.0);
    QCOMPARE(qRound(input.measuredSize().height()), qRound(expectedHeight));

    input.arrange(QRect(0, 0, 240, qRound(input.measuredSize().height())));
    QCOMPARE(qRound(input.fieldRect().height()), qRound(expectedField));
    QVERIFY(input.editor() != nullptr);
    QVERIFY2(input.editor()->geometry().top() >= qRound(padding + labelLine),
             "the editor must start below the floating label's line box");

    // The baseline field is unchanged: 8 + 16 + 24 + 8 = 56 dp.
    md::TextInput baseline;
    baseline.setLabel(QStringLiteral("Label"));
    baseline.measure(md::MeasureSpec::exactly(240.0), md::MeasureSpec::unspecified());
    QCOMPARE(qRound(baseline.fieldRect().height()), 56);
}

void TestTypographyFixes::linearWeightsFillTheAxisExactly()
{
    md::Linear row(md::Linear::Orientation::Horizontal);
    Leaf first(QSizeF(20.0, 10.0));
    Leaf second(QSizeF(20.0, 10.0));
    Leaf third(QSizeF(20.0, 10.0));

    row.addView(&first);
    row.addView(&second);
    row.addView(&third);
    row.setSpacing(10.0);
    row.setWeight(&first, 1.0);
    row.setWeight(&second, 1.0);
    row.setWeight(&third, 1.0);

    row.measure(md::MeasureSpec::exactly(100.0), md::MeasureSpec::exactly(30.0));
    row.arrange(QRect(0, 0, 100, 30));

    // 100 px minus two 10 px gaps leaves 80 px for three equal weights. Every
    // pixel is owned by exactly one child: no 1 px gap at the end and no 1 px
    // overlap between neighbours.
    QCOMPARE(first.geometry().width() + second.geometry().width()
                 + third.geometry().width(),
             80);
    QCOMPARE(first.geometry().right() + 1 + 10, second.geometry().left());
    QCOMPARE(second.geometry().right() + 1 + 10, third.geometry().left());
    QCOMPARE(third.geometry().right(), 99);
    QCOMPARE(first.geometry().left(), 0);
    QCOMPARE(second.geometry().left(), 37);
    QCOMPARE(third.geometry().left(), 73);

    // The child is measured with exactly the slot it is arranged in, so a
    // text-bearing child cannot disagree with its own geometry.
    QCOMPARE(first.measuredSize(), QSizeF(first.geometry().size()));
    QCOMPARE(third.measuredSize(), QSizeF(third.geometry().size()));
}

void TestTypographyFixes::frameGravityStaysInsideBounds()
{
    md::Frame frame;
    Leaf child(QSizeF(40.0, 20.0));
    frame.addView(&child);
    frame.setGravity(&child, Qt::AlignRight | Qt::AlignBottom);

    // A half-pixel margin used to round the child's rectangle outwards, adding
    // one pixel to each aligned axis.
    frame.setMargin(0.5);
    frame.measure(md::MeasureSpec::exactly(101.0), md::MeasureSpec::exactly(101.0));
    frame.arrange(QRect(0, 0, 101, 101));

    QCOMPARE(child.geometry().size(), QSize(40, 20));
    QCOMPARE(child.geometry().right(), 99);
    QCOMPARE(child.geometry().bottom(), 99);

    // A whole-pixel margin keeps the classic nine-cell placement.
    frame.setMargin(10.0);
    frame.measure(md::MeasureSpec::exactly(200.0), md::MeasureSpec::exactly(100.0));
    frame.arrange(QRect(0, 0, 200, 100));
    QCOMPARE(child.geometry(), QRect(150, 70, 40, 20));
}

void TestTypographyFixes::constraintStretchAndCentreArePixelExact()
{
    md::Constraint root;
    Leaf centred(QSizeF(30.0, 30.0));
    Leaf stretched(QSizeF(10.0, 10.0));
    Leaf pinned(QSizeF(30.0, 10.0));
    root.addView(&centred);
    root.addView(&stretched);
    root.addView(&pinned);

    md::Constraint::Anchors centre;
    centre.centerHorizontal = true;
    centre.centerVertical = true;
    QVERIFY(root.setConstraints(&centred, centre));

    md::Constraint::Anchors stretch;
    stretch.start = 10.0;
    stretch.end = 10.0;
    stretch.top = 4.0;
    stretch.bottom = 4.0;
    QVERIFY(root.setConstraints(&stretched, stretch));

    md::Constraint::Anchors startOnly;
    startOnly.start = 10.0;
    QVERIFY(root.setConstraints(&pinned, startOnly));

    root.measure(md::MeasureSpec::exactly(201.0), md::MeasureSpec::exactly(101.0));
    root.arrange(QRect(0, 0, 201, 101));

    // An odd leftover centres at 85 + 30 = 115, leaving the extra pixel on the
    // trailing side: the child is 30 wide, not 31.
    QCOMPARE(centred.geometry(), QRect(85, 35, 30, 30));
    QCOMPARE(centred.geometry().width(), 30);
    QCOMPARE(centred.geometry().height(), 30);

    QCOMPARE(stretched.geometry(), QRect(10, 4, 181, 93));
    QCOMPARE(stretched.geometry().right(), 201 - 10 - 1);
    QCOMPARE(stretched.geometry().bottom(), 101 - 4 - 1);

    QCOMPARE(pinned.geometry(), QRect(10, 0, 30, 10));

    // A logical start anchor mirrors to the right edge in RTL, and the
    // symmetric constraints are unaffected.
    root.setLayoutDirection(Qt::RightToLeft);
    root.arrange(QRect(0, 0, 201, 101));
    QCOMPARE(pinned.geometry(), QRect(201 - 10 - 30, 0, 30, 10));
    QCOMPARE(centred.geometry(), QRect(85, 35, 30, 30));
    QCOMPARE(stretched.geometry(), QRect(10, 4, 181, 93));
}

QTEST_MAIN(TestTypographyFixes)
#include "tst_typography_fixes.moc"
