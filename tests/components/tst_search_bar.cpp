// Unit tests for SearchBar (framework.md §14).
//
// The bar is exercised offscreen: the widget is measured and arranged directly,
// the keyboard is driven through the embedded QLineEdit, and the suggestion
// popup is inspected through its public introspection helpers. The focus test
// uses a real host window because Escape is defined in terms of field focus.

#include <QColor>
#include <QImage>
#include <QLineEdit>
#include <QPainter>
#include <QRegion>
#include <QSignalSpy>
#include <QTest>
#include <QWidget>

#include <md/components/popup.h>
#include <md/components/search_bar.h>
#include <md/theme/theme.h>

#include <algorithm>
#include <cmath>

namespace {

// Sizes a widget through the framework's measurement and arrangement entry
// points. A never-shown QWidget defers QEvent::Resize, so resize() alone would
// not place embedded children; arrange() is the documented layout hook.
template<class Widget>
void arrangeWidget(Widget& widget, int width, int height)
{
    widget.measure(md::MeasureSpec::exactly(static_cast<qreal>(width)),
                   md::MeasureSpec::exactly(static_cast<qreal>(height)));
    widget.arrange(QRect(0, 0, width, height));
}

// Renders a widget (and its QWidget children) into a transparent image so the
// paint path can be exercised without a visible window.
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

int paintedPixels(const QImage& image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y).alpha() > 0)
                ++count;
        }
    }
    return count;
}

QColor colourAt(const QImage& image, const QPoint& logical)
{
    const qreal ratio = image.devicePixelRatio();
    const int x = std::clamp(static_cast<int>(std::lround(logical.x() * ratio)),
                             0, image.width() - 1);
    const int y = std::clamp(static_cast<int>(std::lround(logical.y() * ratio)),
                             0, image.height() - 1);
    return image.pixelColor(x, y);
}

bool nearColour(const QColor& lhs, const QColor& rhs, int tolerance = 12)
{
    return qAbs(lhs.red() - rhs.red()) <= tolerance
        && qAbs(lhs.green() - rhs.green()) <= tolerance
        && qAbs(lhs.blue() - rhs.blue()) <= tolerance;
}

bool anyNearColour(const QImage& image, const QPoint& centre, int radius,
                   const QColor& colour)
{
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (nearColour(colourAt(image, centre + QPoint(dx, dy)), colour))
                return true;
        }
    }
    return false;
}

void showHost(QWidget& host)
{
    host.show();
    host.activateWindow();
    // The test only needs a mapped window; offscreen platforms always expose.
    (void)QTest::qWaitForWindowExposed(&host);
}

} // namespace

class TestSearchBar final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void placeholderAndTextRoundTrip();
    void textChangedFromSetTextAndTyping();
    void clearEmptiesAndEmitsTextChanged();
    void clearButtonAppearsOnlyWithText();
    void suggestionsFilterCaseInsensitively();
    void suggestionListIsCappedAtSixRows();
    void returnSubmitsAndClosesPopup();
    void suggestionActivationFillsText();
    void clickingSuggestionActivatesIt();
    void escapeClosesPopupBeforeFocusLoss();
    void accessibleNameAndPillSurface();
};

void TestSearchBar::initTestCase()
{
    // Pin the theme so the expected dp measurements do not depend on the
    // platform default.
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestSearchBar::placeholderAndTextRoundTrip()
{
    md::SearchBar bar;
    QVERIFY(bar.placeholder().isEmpty());
    QVERIFY(bar.text().isEmpty());

    bar.setPlaceholder(QStringLiteral("Search components"));
    QCOMPARE(bar.placeholder(), QStringLiteral("Search components"));
    QCOMPARE(bar.editor()->placeholderText(), QStringLiteral("Search components"));

    bar.setText(QStringLiteral("button"));
    QCOMPARE(bar.text(), QStringLiteral("button"));
    QCOMPARE(bar.editor()->text(), QStringLiteral("button"));

    // The placeholder may change while text is present; it is shown again once
    // the text is cleared.
    bar.setPlaceholder(QStringLiteral("Find anything"));
    QCOMPARE(bar.placeholder(), QStringLiteral("Find anything"));
    QCOMPARE(bar.editor()->placeholderText(), QStringLiteral("Find anything"));

    bar.clear();
    QVERIFY(bar.text().isEmpty());

    bar.measure(md::MeasureSpec::unspecified(), md::MeasureSpec::unspecified());
    QCOMPARE(bar.measuredSize().width(), 360.0);  // the 360 dp default width
    QCOMPARE(bar.measuredSize().height(), 56.0);  // the M3 search-bar height
}

void TestSearchBar::textChangedFromSetTextAndTyping()
{
    md::SearchBar bar;
    QSignalSpy textSpy(&bar, &md::SearchBar::textChanged);

    bar.setText(QStringLiteral("alpha"));
    QCOMPARE(static_cast<int>(textSpy.count()), 1);
    QCOMPARE(textSpy.at(0).at(0).toString(), QStringLiteral("alpha"));

    // Re-setting the same value is not a change.
    bar.setText(QStringLiteral("alpha"));
    QCOMPARE(static_cast<int>(textSpy.count()), 1);

    bar.clear();
    QCOMPARE(static_cast<int>(textSpy.count()), 2);
    QCOMPARE(textSpy.at(1).at(0).toString(), QString());

    textSpy.clear();
    QTest::keyClicks(bar.editor(), QStringLiteral("beta"));
    QCOMPARE(bar.text(), QStringLiteral("beta"));
    QCOMPARE(static_cast<int>(textSpy.count()), 4);
    QCOMPARE(textSpy.at(3).at(0).toString(), QStringLiteral("beta"));
}

void TestSearchBar::clearEmptiesAndEmitsTextChanged()
{
    md::SearchBar bar;
    arrangeWidget(bar, 360, 56);

    bar.setSuggestions({QStringLiteral("Alpha"), QStringLiteral("Beta")});
    bar.setText(QStringLiteral("al"));
    bar.showSuggestions();
    QVERIFY(bar.isPopupVisible());

    QSignalSpy textSpy(&bar, &md::SearchBar::textChanged);

    // The trailing "x" is a 48 dp target; a click on it clears the field and
    // dismisses the suggestion list.
    QTest::mouseClick(&bar, Qt::LeftButton, Qt::NoModifier,
                      bar.clearButtonRect().center().toPoint());
    QVERIFY(bar.text().isEmpty());
    QCOMPARE(static_cast<int>(textSpy.count()), 1);
    QVERIFY(!bar.isPopupVisible());

    // Clearing an already empty field is a no-op.
    bar.clear();
    QCOMPARE(static_cast<int>(textSpy.count()), 1);

    bar.setText(QStringLiteral("query"));
    QCOMPARE(static_cast<int>(textSpy.count()), 2);
    bar.clear();
    QVERIFY(bar.text().isEmpty());
    QCOMPARE(static_cast<int>(textSpy.count()), 3);
}

void TestSearchBar::clearButtonAppearsOnlyWithText()
{
    md::SearchBar bar;
    arrangeWidget(bar, 360, 56);
    QVERIFY(paintedPixels(renderWidget(bar)) > 500);

    const QPoint centre = bar.clearButtonRect().center().toPoint();
    const QColor iconColour = md::Theme::baselineLight().colours().onSurfaceVariant;
    const QImage empty = renderWidget(bar);
    QVERIFY(!anyNearColour(empty, centre, 4, iconColour));

    bar.setText(QStringLiteral("query"));
    const QImage filled = renderWidget(bar);
    QVERIFY(anyNearColour(filled, centre, 4, iconColour));
}

void TestSearchBar::suggestionsFilterCaseInsensitively()
{
    md::SearchBar bar;
    const QStringList all{QStringLiteral("Alpha"), QStringLiteral("Alpine"),
                          QStringLiteral("beta"), QStringLiteral("GAMMA")};
    bar.setSuggestions(all);
    QCOMPARE(bar.suggestions(), all);

    // An upper-case query still matches the lower-case entries.
    bar.setText(QStringLiteral("AL"));
    QCOMPARE(bar.visibleSuggestionCount(), 2);
    bar.showSuggestions();
    QVERIFY(bar.isPopupVisible());
    QCOMPARE(bar.visibleSuggestionCount(), 2);
    QCOMPARE(bar.highlightedSuggestion(), -1);

    // A single letter matches every entry in the set.
    bar.setText(QStringLiteral("a"));
    QCOMPARE(bar.visibleSuggestionCount(), 4);
    QVERIFY(!bar.isPopupVisible()); // changing an unfocused field hides the list

    // No match means no list at all.
    bar.setText(QStringLiteral("zzz"));
    QCOMPARE(bar.visibleSuggestionCount(), 0);
    bar.showSuggestions();
    QVERIFY(!bar.isPopupVisible());
}

void TestSearchBar::suggestionListIsCappedAtSixRows()
{
    QStringList many;
    for (int index = 0; index < 9; ++index)
        many.append(QStringLiteral("Item %1").arg(index));

    md::SearchBar bar;
    bar.setSuggestions(many);
    bar.setText(QStringLiteral("item"));
    QCOMPARE(bar.visibleSuggestionCount(), 6);

    bar.showSuggestions();
    QVERIFY(bar.isPopupVisible());

    // Down walks the six visible rows and wraps around; Enter then activates
    // the highlighted row.
    QTest::keyClick(bar.editor(), Qt::Key_Down);
    QCOMPARE(bar.highlightedSuggestion(), 0);
    for (int step = 0; step < 5; ++step)
        QTest::keyClick(bar.editor(), Qt::Key_Down);
    QCOMPARE(bar.highlightedSuggestion(), 5);
    QTest::keyClick(bar.editor(), Qt::Key_Down);
    QCOMPARE(bar.highlightedSuggestion(), 0);
    QTest::keyClick(bar.editor(), Qt::Key_Down);
    QCOMPARE(bar.highlightedSuggestion(), 1);
}

void TestSearchBar::returnSubmitsAndClosesPopup()
{
    md::SearchBar bar;
    bar.setSuggestions({QStringLiteral("Alpha"), QStringLiteral("Alpine")});

    QSignalSpy submittedSpy(&bar, &md::SearchBar::submitted);
    QSignalSpy activatedSpy(&bar, &md::SearchBar::suggestionActivated);

    bar.setText(QStringLiteral("al"));
    bar.showSuggestions();
    QVERIFY(bar.isPopupVisible());
    QCOMPARE(bar.highlightedSuggestion(), -1);

    // With no highlighted row, Return submits the typed query rather than
    // accepting the first suggestion.
    QTest::keyClick(bar.editor(), Qt::Key_Return);
    QCOMPARE(static_cast<int>(submittedSpy.count()), 1);
    QCOMPARE(submittedSpy.at(0).at(0).toString(), QStringLiteral("al"));
    QVERIFY(!bar.isPopupVisible());
    QCOMPARE(static_cast<int>(activatedSpy.count()), 0);

    // Return in the closed field submits as well.
    bar.setText(QStringLiteral("service"));
    QTest::keyClick(bar.editor(), Qt::Key_Enter);
    QCOMPARE(static_cast<int>(submittedSpy.count()), 2);
    QCOMPARE(submittedSpy.at(1).at(0).toString(), QStringLiteral("service"));
}

void TestSearchBar::suggestionActivationFillsText()
{
    md::SearchBar bar;
    bar.setSuggestions({QStringLiteral("Alpha"), QStringLiteral("Alpine"),
                        QStringLiteral("Beta")});

    QSignalSpy activatedSpy(&bar, &md::SearchBar::suggestionActivated);
    QSignalSpy submittedSpy(&bar, &md::SearchBar::submitted);
    QSignalSpy textSpy(&bar, &md::SearchBar::textChanged);

    bar.setText(QStringLiteral("al"));
    QCOMPARE(static_cast<int>(textSpy.count()), 1);

    bar.showSuggestions();
    QTest::keyClick(bar.editor(), Qt::Key_Down);
    QCOMPARE(bar.highlightedSuggestion(), 0);
    QTest::keyClick(bar.editor(), Qt::Key_Down);
    QCOMPARE(bar.highlightedSuggestion(), 1);

    QTest::keyClick(bar.editor(), Qt::Key_Return);
    QCOMPARE(bar.text(), QStringLiteral("Alpine"));
    QCOMPARE(static_cast<int>(activatedSpy.count()), 1);
    QCOMPARE(activatedSpy.at(0).at(0).toString(), QStringLiteral("Alpine"));
    QVERIFY(!bar.isPopupVisible());
    QCOMPARE(bar.highlightedSuggestion(), -1);
    QCOMPARE(static_cast<int>(submittedSpy.count()), 0);
    // Typing and the accepted row are two distinct text changes.
    QCOMPARE(static_cast<int>(textSpy.count()), 2);
}

void TestSearchBar::clickingSuggestionActivatesIt()
{
    QWidget host;
    host.resize(520, 320);

    auto* bar = new md::SearchBar(&host);
    bar->setGeometry(20, 20, 360, 56);
    bar->setSuggestions({QStringLiteral("Alpha"), QStringLiteral("Alpine"),
                         QStringLiteral("Beta")});
    showHost(host);

    bar->setText(QStringLiteral("al"));
    bar->showSuggestions();
    QVERIFY(bar->isPopupVisible());

    auto* popup = qobject_cast<md::PopUp*>(bar->popupWidget());
    QVERIFY(popup != nullptr);
    md::View* list = popup->content();
    QVERIFY(list != nullptr);

    QSignalSpy activatedSpy(bar, &md::SearchBar::suggestionActivated);

    // Row 1 is "Alpine": 4 dp list padding, 48 dp rows, so its centre is at
    // y = 4 + 48 + 24.
    QTest::mouseClick(list, Qt::LeftButton, Qt::NoModifier, QPoint(24, 4 + 48 + 24));
    QCOMPARE(bar->text(), QStringLiteral("Alpine"));
    QCOMPARE(static_cast<int>(activatedSpy.count()), 1);
    QCOMPARE(activatedSpy.at(0).at(0).toString(), QStringLiteral("Alpine"));
    QVERIFY(!bar->isPopupVisible());
}

void TestSearchBar::escapeClosesPopupBeforeFocusLoss()
{
    QWidget host;
    host.resize(520, 320);

    auto* bar = new md::SearchBar(&host);
    bar->setGeometry(20, 20, 360, 56);
    bar->setSuggestions({QStringLiteral("Alpha"), QStringLiteral("Alpine"),
                         QStringLiteral("Beta")});
    showHost(host);

    bar->editor()->setFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(bar->editor()->hasFocus());

    // Focused typing opens the filtered list.
    QTest::keyClicks(bar->editor(), QStringLiteral("Al"));
    QCOMPARE(bar->text(), QStringLiteral("Al"));
    QTRY_VERIFY(bar->isPopupVisible());
    QVERIFY(bar->editor()->hasFocus());

    // The first Escape only dismisses the list; the field keeps the keyboard.
    QTest::keyClick(bar->editor(), Qt::Key_Escape);
    QVERIFY(!bar->isPopupVisible());
    QVERIFY(bar->editor()->hasFocus());

    // The second Escape clears focus.
    QTest::keyClick(bar->editor(), Qt::Key_Escape);
    QTRY_VERIFY(!bar->editor()->hasFocus());
}

void TestSearchBar::accessibleNameAndPillSurface()
{
    md::SearchBar bar;
    QCOMPARE(bar.accessibleName(), QStringLiteral("Search"));
    QCOMPARE(bar.editor()->accessibleName(), QStringLiteral("Search"));
    QVERIFY(bar.shape().isPill());
    QCOMPARE(static_cast<int>(bar.focusPolicy()), static_cast<int>(Qt::StrongFocus));
}

QTEST_MAIN(TestSearchBar)
#include "tst_search_bar.moc"
