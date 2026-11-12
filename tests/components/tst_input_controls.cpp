// Unit tests for the input/model subsystem (framework.md §14).
//
// The controls are exercised offscreen: widgets are constructed directly, given
// an explicit geometry where pointer positions matter, and driven with QTest's
// synthetic key, mouse and wheel events. The tests cover the data path (Model
// and Adapter, including opaque user types), the text-field validation rules,
// the numeric range/step rules, the menu keyboard and pointer path, the
// completer's filtering/backfill cycle, the colour picker's HSV conversions,
// and the cyclic behaviour of the two pickers.

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QRegion>
#include <QRegularExpressionValidator>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTest>
#include <QValidator>
#include <QWheelEvent>

#include <md/components/input.h>
#include <md/components/model.h>
#include <md/theme/theme.h>

#include <memory>

namespace {

// An application type with no string conversion: it can only be displayed
// through an Adapter formatter, which is exactly the path under test.
struct TestEntry
{
    QString name;
    int id = 0;
};

// QSignalSpy stores a QVariant parameter as a nested QVariant; unwrap it.
QVariant unwrap(const QVariant& value)
{
    if (value.metaType().id() == QMetaType::QVariant)
        return value.value<QVariant>();

    return value;
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

} // namespace

Q_DECLARE_METATYPE(TestEntry)

class TestInputControls final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void modelValuesAndLabels();
    void modelArbitraryTypes();
    void adapterFormatsWithoutOwningSource();
    void controlBorrowsSourceModel();

    void textInputBasics();
    void textInputValidation();
    void textInputClearButton();

    void numberInputRangeAndSteps();

    void dropDownSelection();
    void dropDownMenuKeyboard();
    void selectControl();

    void completeFiltersAndBackfills();

    void colourPickerHsvAndHex();
    void colourPickerPointer();

    void numberPickerCycles();
    void ringPickerArbitraryTypes();
    void ringPickerPointerAndInertia();

    void paintSmokeTest();
};

void TestInputControls::initTestCase()
{
    // Pin the theme so the expected dp measurements do not depend on the
    // platform default.
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestInputControls::modelValuesAndLabels()
{
    md::Model model;
    QCOMPARE(model.rowCount(), 0);

    model.setValues(QVariantList{1, 2, 3}, QStringList{QStringLiteral("one"), QStringLiteral("two")});
    QCOMPARE(model.rowCount(), 3);

    // Explicit labels win, and a missing label falls back to the value's own
    // string conversion.
    QCOMPARE(model.data(model.index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("one"));
    QCOMPARE(model.data(model.index(1, 0), Qt::DisplayRole).toString(), QStringLiteral("two"));
    QCOMPARE(model.data(model.index(2, 0), Qt::DisplayRole).toString(), QStringLiteral("3"));

    // ValueRole always returns the stored value, never the label.
    QCOMPARE(model.data(model.index(1, 0), md::Model::ValueRole).toInt(), 2);

    model.append(42, QStringLiteral("answer"));
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.data(model.index(3, 0), Qt::DisplayRole).toString(), QStringLiteral("answer"));
    QCOMPARE(model.data(model.index(3, 0), md::Model::ValueRole).toInt(), 42);
    QCOMPARE(static_cast<int>(model.values().size()), 4);

    const QHash<int, QByteArray> roles = model.roleNames();
    QCOMPARE(roles.value(Qt::DisplayRole), QByteArrayLiteral("display"));
    QCOMPARE(roles.value(md::Model::ValueRole), QByteArrayLiteral("value"));

    // An invalid index is answered with an invalid variant, not a crash.
    QVERIFY(!model.data(QModelIndex(), Qt::DisplayRole).isValid());
    QVERIFY(!model.data(model.index(99, 0), Qt::DisplayRole).isValid());
}

void TestInputControls::modelArbitraryTypes()
{
    const TestEntry alpha{QStringLiteral("alpha"), 1};
    const TestEntry beta{QStringLiteral("beta"), 2};

    md::Model model;
    model.append(QVariant::fromValue(alpha), QStringLiteral("Alpha"));
    model.append(QVariant::fromValue(beta));

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), md::Model::ValueRole).value<TestEntry>().name,
             QStringLiteral("alpha"));
    QCOMPARE(model.data(model.index(1, 0), md::Model::ValueRole).value<TestEntry>().id, 2);

    // The explicit label is presentation; the value stays opaque.
    QCOMPARE(model.data(model.index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("Alpha"));
    // Without a label an opaque type has no sensible default text.
    QCOMPARE(model.data(model.index(1, 0), Qt::DisplayRole).toString(), QString());

    model.setValues(QVariantList{QVariant::fromValue(TestEntry{QStringLiteral("gamma"), 3})});
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), md::Model::ValueRole).value<TestEntry>().id, 3);
}

void TestInputControls::adapterFormatsWithoutOwningSource()
{
    md::Model source(QVariantList{10, 20, 30});
    md::Adapter adapter;
    adapter.setSourceModel(&source);

    QCOMPARE(adapter.rowCount(), 3);
    QCOMPARE(adapter.value(1).toInt(), 20);
    QCOMPARE(adapter.text(1), QStringLiteral("20"));

    adapter.setLabelFor([](const QVariant& value) {
        return QStringLiteral("Item %1").arg(value.toInt());
    });

    QCOMPARE(adapter.text(1), QStringLiteral("Item 20"));
    QCOMPARE(adapter.data(adapter.index(1, 0), Qt::DisplayRole).toString(),
             QStringLiteral("Item 20"));
    QCOMPARE(adapter.data(adapter.index(1, 0), Qt::EditRole).toString(),
             QStringLiteral("Item 20"));
    // Formatting changes presentation only.
    QCOMPARE(adapter.value(1).toInt(), 20);
    QCOMPARE(adapter.labelFor(QVariant(7)), QStringLiteral("Item 7"));

    adapter.setValueRole(Qt::DisplayRole);
    QCOMPARE(adapter.valueRole(), static_cast<int>(Qt::DisplayRole));
    QCOMPARE(adapter.value(2).toInt(), 30);

    adapter.setValueRole(md::Model::ValueRole);
    QCOMPARE(adapter.valueRole(), static_cast<int>(md::Model::ValueRole));

    // A source that stores values but no display text still produces readable
    // rows: the Adapter falls back to the value's own conversion.
    QStandardItemModel bare(1, 1);
    bare.setData(bare.index(0, 0), 42, Qt::UserRole);
    bare.setData(bare.index(0, 0), QString(), Qt::DisplayRole);
    md::Adapter bareAdapter;
    bareAdapter.setSourceModel(&bare);
    QCOMPARE(bareAdapter.text(0), QStringLiteral("42"));
    QCOMPARE(bareAdapter.value(0).toInt(), 42);

    // The adapter is a bridge, not an owner.
    auto* heapSource = new md::Model(QVariantList{1, 2});
    {
        auto scoped = std::make_unique<md::Adapter>();
        scoped->setSourceModel(heapSource);
        QCOMPARE(scoped->rowCount(), 2);
    }
    QCOMPARE(heapSource->rowCount(), 2);
    delete heapSource;
}

void TestInputControls::controlBorrowsSourceModel()
{
    auto* source = new md::Model(QVariantList{QStringLiteral("a"), QStringLiteral("b")});
    {
        md::DropDown dropdown;
        dropdown.setSourceModel(source);
        QCOMPARE(dropdown.sourceModel(), source);
        QCOMPARE(dropdown.count(), 2);
        QCOMPARE(dropdown.itemText(1), QStringLiteral("b"));
        QCOMPARE(dropdown.itemValue(0).toString(), QStringLiteral("a"));

        // Presentation only: the source text is upper-cased, the value is not.
        dropdown.setFormatter([](const QVariant& value) {
            return value.toString().toUpper();
        });
        QCOMPARE(dropdown.itemText(0), QStringLiteral("A"));
        QCOMPARE(dropdown.itemValue(0).toString(), QStringLiteral("a"));
        QCOMPARE(dropdown.currentText(), QStringLiteral("A"));
    }

    QCOMPARE(source->rowCount(), 2);
    delete source;
}

void TestInputControls::textInputBasics()
{
    md::TextInput input;
    input.setLabel(QStringLiteral("Name"));
    input.setPlaceholderText(QStringLiteral("Your name"));

    QCOMPARE(input.label(), QStringLiteral("Name"));
    QCOMPARE(input.placeholderText(), QStringLiteral("Your name"));
    // A floating label replaces the placeholder so the two never overlap.
    QCOMPARE(input.editor()->placeholderText(), QString());

    QSignalSpy textSpy(&input, &md::TextInput::textChanged);
    input.setText(QStringLiteral("Ada"));
    QCOMPARE(input.text(), QStringLiteral("Ada"));
    QCOMPARE(static_cast<int>(textSpy.count()), 1);
    QCOMPARE(textSpy.at(0).at(0).toString(), QStringLiteral("Ada"));

    input.setText(QStringLiteral("Grace"));
    QCOMPARE(static_cast<int>(textSpy.count()), 2);

    input.setHelperText(QStringLiteral("We only store initials"));
    QCOMPARE(input.helperText(), QStringLiteral("We only store initials"));
    input.measure(md::MeasureSpec::unspecified(), md::MeasureSpec::unspecified());
    QCOMPARE(input.measuredSize().height(), 76.0); // 56 dp field + 20 dp supporting line

    input.setPassword(true);
    QVERIFY(input.isPassword());
    QCOMPARE(input.editor()->echoMode(), QLineEdit::Password);
    input.setPassword(false);
    QCOMPARE(input.editor()->echoMode(), QLineEdit::Normal);

    input.setErrorText(QStringLiteral("Required"));
    QVERIFY(input.hasError());
    QCOMPARE(input.errorText(), QStringLiteral("Required"));
    QVERIFY(!input.hasAcceptableInput());
    input.measure(md::MeasureSpec::unspecified(), md::MeasureSpec::unspecified());
    QCOMPARE(input.measuredSize().height(), 76.0);

    input.setErrorText(QString());
    input.setHelperText(QString());
    QVERIFY(!input.hasError());
    input.measure(md::MeasureSpec::unspecified(), md::MeasureSpec::unspecified());
    QCOMPARE(input.measuredSize().height(), 56.0);

    // The QLineEdit is embedded by the layout: 16 dp inset, below the floating
    // label, above the underline.
    arrangeWidget(input, 280, 76);
    QCOMPARE(input.size(), QSize(280, 76));
    QCOMPARE(input.editor()->geometry(), QRect(16, 24, 248, 24));
}

void TestInputControls::textInputValidation()
{
    md::EmailInput email;
    QVERIFY(!email.hasError()); // an empty field is not an error

    email.setText(QStringLiteral("person@example.org"));
    QVERIFY(email.hasAcceptableInput());
    QVERIFY(!email.hasError());

    email.setText(QStringLiteral("not-an-email"));
    QVERIFY(!email.hasAcceptableInput());
    QVERIFY(email.hasError());
    QVERIFY(!email.errorText().isEmpty());

    md::TelephoneInput phone;
    phone.setText(QStringLiteral("+1 555 0100"));
    QVERIFY(phone.hasAcceptableInput());
    phone.setText(QStringLiteral("abc"));
    QVERIFY(!phone.hasAcceptableInput());
    QVERIFY(phone.hasError());

    md::RegexInput regexInput;
    regexInput.setPattern(QRegularExpression(QStringLiteral("[A-Z]{2}")));
    QCOMPARE(regexInput.pattern().pattern(), QStringLiteral("[A-Z]{2}"));

    regexInput.setText(QStringLiteral("AB"));
    QVERIFY(regexInput.hasAcceptableInput());
    regexInput.setText(QStringLiteral("A1"));
    QVERIFY(!regexInput.hasAcceptableInput());
    QVERIFY(regexInput.hasError());

    md::TextInput plain;
    plain.setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]+")),
                                                      &plain));
    plain.setText(QStringLiteral("123"));
    QVERIFY(plain.hasAcceptableInput());
    plain.setText(QStringLiteral("12a"));
    QVERIFY(!plain.hasAcceptableInput());
}

void TestInputControls::textInputClearButton()
{
    md::TextInput input;
    arrangeWidget(input, 280, 56);
    QVERIFY(input.isClearButtonEnabled() == false);

    input.setClearButtonEnabled(true);
    QVERIFY(input.isClearButtonEnabled());
    input.setText(QStringLiteral("hello"));

    QSignalSpy clearSpy(&input, &md::TextInput::clearClicked);
    const QPoint centre = input.clearButtonRect().center().toPoint();
    QVERIFY(input.rect().contains(centre));

    QTest::mouseClick(&input, Qt::LeftButton, Qt::NoModifier, centre);
    QCOMPARE(static_cast<int>(clearSpy.count()), 1);
    QVERIFY(input.text().isEmpty());
}

void TestInputControls::numberInputRangeAndSteps()
{
    md::NumberInput number;
    number.setRange(0.0, 10.0);
    number.setStep(1.0);

    QCOMPARE(number.minimum(), 0.0);
    QCOMPARE(number.maximum(), 10.0);
    QCOMPARE(number.step(), 1.0);

    number.setValue(12.0);
    QCOMPARE(number.value(), 10.0);
    QCOMPARE(number.text(), QStringLiteral("10"));

    number.setValue(-3.0);
    QCOMPARE(number.value(), 0.0);

    QSignalSpy valueSpy(&number, &md::NumberInput::valueChanged);
    number.setValue(5.0);
    QCOMPARE(static_cast<int>(valueSpy.count()), 1);
    QCOMPARE(number.value(), 5.0);

    // Out-of-range text is presented as an error, but the committed value is
    // still the clamped numeric reading.
    number.setText(QStringLiteral("99"));
    QVERIFY(number.hasError());
    QVERIFY(!number.hasAcceptableInput());
    QCOMPARE(number.value(), 10.0);

    number.setText(QStringLiteral("abc"));
    QVERIFY(number.hasError());
    QCOMPARE(number.value(), 5.0); // last committed value survives invalid text

    number.setText(QStringLiteral("5"));
    QVERIFY(!number.hasError());
    QCOMPARE(number.value(), 5.0);

    // Arrow keys step the committed value without leaving the editor.
    QTest::keyClick(number.editor(), Qt::Key_Up);
    QCOMPARE(number.value(), 6.0);
    QCOMPARE(static_cast<int>(valueSpy.count()), 2);
    QTest::keyClick(number.editor(), Qt::Key_Down);
    QCOMPARE(number.value(), 5.0);

    number.setStep(0.5);
    QTest::keyClick(number.editor(), Qt::Key_Up);
    QCOMPARE(number.value(), 5.5);

    // The step is clamped by the range.
    number.setValue(9.8);
    QTest::keyClick(number.editor(), Qt::Key_Up);
    QCOMPARE(number.value(), 10.0);
}

void TestInputControls::dropDownSelection()
{
    md::DropDown dropdown;
    dropdown.addItem(QStringLiteral("One"), 1);
    dropdown.addItem(QStringLiteral("Two"), 2);
    dropdown.addItem(QStringLiteral("Three"), 3);

    QCOMPARE(dropdown.count(), 3);
    QCOMPARE(dropdown.currentIndex(), 0);
    QCOMPARE(dropdown.currentText(), QStringLiteral("One"));
    QCOMPARE(dropdown.currentValue().toInt(), 1);
    QCOMPARE(dropdown.itemText(2), QStringLiteral("Three"));
    QCOMPARE(dropdown.itemValue(2).toInt(), 3);

    QSignalSpy indexSpy(&dropdown, &md::DropDown::currentIndexChanged);
    QSignalSpy valueSpy(&dropdown, &md::DropDown::currentValueChanged);
    dropdown.setCurrentIndex(1);
    QCOMPARE(static_cast<int>(indexSpy.count()), 1);
    QCOMPARE(static_cast<int>(valueSpy.count()), 1);
    QCOMPARE(dropdown.currentText(), QStringLiteral("Two"));
    QCOMPARE(dropdown.currentValue().toInt(), 2);

    dropdown.setCurrentIndex(1); // no change, no signal
    QCOMPARE(static_cast<int>(indexSpy.count()), 1);

    // Select by value.
    dropdown.setCurrentValue(3);
    QCOMPARE(dropdown.currentIndex(), 2);
    QCOMPARE(dropdown.currentText(), QStringLiteral("Three"));

    // Out-of-range indices are clamped, not accepted.
    dropdown.setCurrentIndex(99);
    QCOMPARE(dropdown.currentIndex(), 2);
    dropdown.setCurrentIndex(-4);
    QCOMPARE(dropdown.currentIndex(), 0);

    // An unknown value leaves the selection alone.
    dropdown.setCurrentValue(QVariant(1234));
    QCOMPARE(dropdown.currentIndex(), 0);

    // addItems() is the bulk form of addItem().
    md::DropDown bulk;
    bulk.addItems({QStringLiteral("a"), QStringLiteral("b")});
    QCOMPARE(bulk.count(), 2);
    QCOMPARE(bulk.currentText(), QStringLiteral("a"));
    QCOMPARE(bulk.itemValue(1).toString(), QStringLiteral("b"));
}

void TestInputControls::dropDownMenuKeyboard()
{
    md::DropDown dropdown;
    dropdown.addItem(QStringLiteral("One"));
    dropdown.addItem(QStringLiteral("Two"));
    dropdown.addItem(QStringLiteral("Three"));

    QSignalSpy activatedSpy(&dropdown, &md::DropDown::activated);

    dropdown.showPopup();
    QVERIFY(dropdown.isPopupVisible());
    QVERIFY(dropdown.popupWidget() != nullptr);

    // Down, down, Return: highlight the third row and activate it.
    QTest::keyClick(dropdown.popupWidget(), Qt::Key_Down);
    QTest::keyClick(dropdown.popupWidget(), Qt::Key_Down);
    QTest::keyClick(dropdown.popupWidget(), Qt::Key_Return);

    QVERIFY(!dropdown.isPopupVisible());
    QCOMPARE(dropdown.currentIndex(), 2);
    QCOMPARE(static_cast<int>(activatedSpy.count()), 1);

    // Escape closes the menu without changing the selection.
    dropdown.setCurrentIndex(0);
    dropdown.showPopup();
    QVERIFY(dropdown.isPopupVisible());
    QTest::keyClick(dropdown.popupWidget(), Qt::Key_Escape);
    QVERIFY(!dropdown.isPopupVisible());
    QCOMPARE(dropdown.currentIndex(), 0);

    // A click on a row selects it and closes the menu.
    dropdown.showPopup();
    QVERIFY(dropdown.isPopupVisible());
    const int rowHeight = 48; // kMenuItemHeightDp at the baseline density
    const int padding = 4;    // kMenuPaddingDp
    QTest::mouseClick(dropdown.popupWidget(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(20, padding + rowHeight + rowHeight / 2));
    QVERIFY(!dropdown.isPopupVisible());
    QCOMPARE(dropdown.currentIndex(), 1);

    // Keyboard on the closed control moves the selection directly.
    QTest::keyClick(&dropdown, Qt::Key_Down);
    QCOMPARE(dropdown.currentIndex(), 2);
    QTest::keyClick(&dropdown, Qt::Key_Up);
    QCOMPARE(dropdown.currentIndex(), 1);
    QTest::keyClick(&dropdown, Qt::Key_Home);
    QCOMPARE(dropdown.currentIndex(), 0);
    QTest::keyClick(&dropdown, Qt::Key_End);
    QCOMPARE(dropdown.currentIndex(), 2);
}

void TestInputControls::selectControl()
{
    md::Select select;
    select.addItem(QStringLiteral("Small"), 1);
    select.addItem(QStringLiteral("Large"), 2);

    QCOMPARE(select.value().toInt(), 1);
    select.setValue(2);
    QCOMPARE(select.value().toInt(), 2);
    QCOMPARE(select.currentText(), QStringLiteral("Large"));

    select.showPopup();
    QVERIFY(select.isPopupVisible());
    select.hidePopup();
    QVERIFY(!select.isPopupVisible());
}

void TestInputControls::completeFiltersAndBackfills()
{
    md::Complete complete;
    complete.setSuggestions({QStringLiteral("Alpha"), QStringLiteral("Alpine"),
                             QStringLiteral("Beta"), QStringLiteral("Gamma")});

    QCOMPARE(static_cast<int>(complete.suggestions().size()), 4);
    QCOMPARE(complete.adapter()->rowCount(), 4);
    QVERIFY(!complete.isPopupVisible());

    QSignalSpy activatedSpy(&complete, &md::Complete::suggestionActivated);

    complete.showSuggestions();
    QVERIFY(complete.isPopupVisible());
    QCOMPARE(complete.visibleSuggestionCount(), 4);

    // Typing filters the list while the popup stays open.
    QTest::keyClicks(complete.editor(), QStringLiteral("Al"));
    QCOMPARE(complete.text(), QStringLiteral("Al"));
    QCOMPARE(complete.visibleSuggestionCount(), 2);
    QVERIFY(complete.isPopupVisible());

    // Down highlights the first match, Return copies it into the editor.
    QCOMPARE(complete.highlightedSuggestion(), -1);
    QTest::keyClick(complete.editor(), Qt::Key_Down);
    QCOMPARE(complete.highlightedSuggestion(), 0);
    QTest::keyClick(complete.editor(), Qt::Key_Return);

    QCOMPARE(complete.text(), QStringLiteral("Alpha"));
    QVERIFY(!complete.isPopupVisible());
    QCOMPARE(static_cast<int>(activatedSpy.count()), 1);
    QCOMPARE(activatedSpy.at(0).at(0).toInt(), 0);
    QCOMPARE(unwrap(activatedSpy.at(0).at(1)).toString(), QStringLiteral("Alpha"));

    // A click on a suggestion accepts it as well.
    complete.setText(QStringLiteral("Al"));
    complete.showSuggestions();
    QCOMPARE(complete.visibleSuggestionCount(), 2);
    QTest::mouseClick(complete.popupWidget(), Qt::LeftButton, Qt::NoModifier, QPoint(20, 28));
    QCOMPARE(complete.text(), QStringLiteral("Alpha"));
    QVERIFY(!complete.isPopupVisible());
    QCOMPARE(static_cast<int>(activatedSpy.count()), 2);

    // A fresh query filters from the beginning again, case-insensitively.
    complete.setText(QStringLiteral("ga"));
    complete.showSuggestions();
    QCOMPARE(complete.visibleSuggestionCount(), 1);
    QTest::keyClick(complete.editor(), Qt::Key_Escape);
    QVERIFY(!complete.isPopupVisible());

    // A query with no match does not leave an empty popup behind.
    complete.setText(QStringLiteral("zzz"));
    complete.showSuggestions();
    QVERIFY(!complete.isPopupVisible());
    QCOMPARE(complete.visibleSuggestionCount(), 0);
}

void TestInputControls::colourPickerHsvAndHex()
{
    md::ColourPicker picker;
    arrangeWidget(picker, 300, 260);

    picker.setColour(QColor::fromHsv(120, 255, 255));
    QCOMPARE(picker.hue(), 120);
    QCOMPARE(picker.saturation(), 255);
    QCOMPARE(picker.value(), 255);
    QCOMPARE(picker.colour().red(), 0);
    QCOMPARE(picker.colour().green(), 255);
    QCOMPARE(picker.colour().blue(), 0);

    QSignalSpy colourSpy(&picker, &md::ColourPicker::colourChanged);
    picker.setColour(QColor(255, 0, 0, 128));
    QCOMPARE(picker.alpha(), 128);
    QCOMPARE(picker.colour().alpha(), 128);
    QVERIFY(colourSpy.count() >= 1);

    // The alpha channel survives an HSV change.
    QTest::keyClick(&picker, Qt::Key_Right);
    QCOMPARE(picker.colour().alpha(), 128);

    // Hex input, six digits: the current alpha is preserved.
    picker.setColour(QColor(255, 255, 255, 255));
    picker.setHexText(QStringLiteral("#0000FF"));
    QCOMPARE(picker.colour().red(), 0);
    QCOMPARE(picker.colour().green(), 0);
    QCOMPARE(picker.colour().blue(), 255);
    QCOMPARE(picker.colour().alpha(), 255);

    picker.setAlpha(90);
    picker.setHexText(QStringLiteral("#00FF00"));
    QCOMPARE(picker.colour().green(), 255);
    QCOMPARE(picker.colour().alpha(), 90);

    // Eight digits carry their own alpha (#AARRGGBB).
    picker.setHexText(QStringLiteral("#800000FF"));
    QCOMPARE(picker.colour().alpha(), 128);
    QCOMPARE(picker.colour().blue(), 255);

    // Invalid hex leaves the colour untouched and is reported back to the UI.
    const QColor before = picker.colour();
    picker.setHexText(QStringLiteral("#12"));
    QCOMPARE(picker.colour(), before);

    QCOMPARE(picker.hueBarRect().isEmpty(), false);
    QCOMPARE(picker.alphaBarRect().isEmpty(), false);
    QCOMPARE(picker.saturationValueRect().isEmpty(), false);
    QCOMPARE(picker.hexRect().isEmpty(), false);
}

void TestInputControls::colourPickerPointer()
{
    md::ColourPicker picker;
    arrangeWidget(picker, 300, 260);
    picker.setColour(QColor::fromHsv(0, 0, 255)); // white, hue 0

    const QRectF panel = picker.saturationValueRect();
    QVERIFY(!panel.isEmpty());

    // Top-right of the panel is full saturation and full value: pure hue.
    QTest::mouseClick(&picker, Qt::LeftButton, Qt::NoModifier,
                      QPoint(static_cast<int>(panel.right()) - 2,
                             static_cast<int>(panel.top()) + 2));
    const QColor picked = picker.colour();
    QVERIFY(picked.red() > 240);
    QVERIFY(picked.green() < 20);
    QVERIFY(picked.blue() < 20);

    // Dragging to the bottom of the panel drives the value to black.
    const QPoint start(static_cast<int>(panel.center().x()),
                       static_cast<int>(panel.center().y()));
    const QPoint end(static_cast<int>(panel.right()) - 4,
                     static_cast<int>(panel.bottom()) - 4);
    QTest::mousePress(&picker, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(&picker, end);
    QTest::mouseRelease(&picker, Qt::LeftButton, Qt::NoModifier, end);

    QVERIFY(picker.saturation() > 240);
    QVERIFY(picker.value() < 20);

    // The hue bar maps its horizontal position to hue.
    picker.setColour(QColor::fromHsv(0, 255, 255));
    const QRectF hueBar = picker.hueBarRect();
    QTest::mouseClick(&picker, Qt::LeftButton, Qt::NoModifier,
                      QPoint(static_cast<int>(hueBar.left()
                                              + hueBar.width() * 0.5),
                             static_cast<int>(hueBar.center().y())));
    QVERIFY(picker.hue() >= 175);
    QVERIFY(picker.hue() <= 185);

    // The alpha bar maps its horizontal position to alpha.
    picker.setAlpha(255);
    const QRectF alphaBar = picker.alphaBarRect();
    QTest::mouseClick(&picker, Qt::LeftButton, Qt::NoModifier,
                      QPoint(static_cast<int>(alphaBar.left() + alphaBar.width() * 0.5),
                             static_cast<int>(alphaBar.center().y())));
    QVERIFY(picker.alpha() >= 120);
    QVERIFY(picker.alpha() <= 135);
}

void TestInputControls::numberPickerCycles()
{
    md::NumberPicker picker;
    picker.setRange(0.0, 2.0);

    QCOMPARE(picker.count(), 3);
    QCOMPARE(picker.currentIndex(), 0);
    QCOMPARE(picker.value(), 0.0);
    QVERIFY(picker.isCyclic());

    picker.setCurrentIndex(2);
    QCOMPARE(picker.value(), 2.0);

    QSignalSpy valueSpy(&picker, &md::NumberPicker::valueChanged);

    // The last item wraps to the first, forwards and backwards.
    QTest::keyClick(&picker, Qt::Key_Down);
    QCOMPARE(picker.currentIndex(), 0);
    QCOMPARE(picker.value(), 0.0);
    QCOMPARE(static_cast<int>(valueSpy.count()), 1);

    QTest::keyClick(&picker, Qt::Key_Up);
    QCOMPARE(picker.currentIndex(), 2);
    QCOMPARE(picker.value(), 2.0);

    // One wheel notch moves exactly one row.
    QWheelEvent wheelDown(QPointF(20.0, 20.0), QPointF(20.0, 20.0), QPoint(), QPoint(0, -120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    (void)QCoreApplication::sendEvent(&picker, &wheelDown);
    QCOMPARE(picker.currentIndex(), 0);
    QCOMPARE(picker.value(), 0.0);

    // Home and End jump without wrapping.
    QTest::keyClick(&picker, Qt::Key_End);
    QCOMPARE(picker.currentIndex(), 2);
    QTest::keyClick(&picker, Qt::Key_Home);
    QCOMPARE(picker.currentIndex(), 0);

    // setValue snaps to the nearest entry in the explicit set.
    picker.setValue(1.4);
    QCOMPARE(picker.currentIndex(), 1);
    QCOMPARE(picker.value(), 1.0);

    // An explicit set overrides the generated range.
    md::NumberPicker explicitPicker;
    explicitPicker.setValues(QVariantList{0.0, 2.5, 5.0});
    QCOMPARE(explicitPicker.count(), 3);
    QCOMPARE(explicitPicker.step(), 2.5);
    explicitPicker.setCurrentIndex(2);
    QCOMPARE(explicitPicker.value(), 5.0);
    QTest::keyClick(&explicitPicker, Qt::Key_Down);
    QCOMPARE(explicitPicker.currentIndex(), 0);
    QCOMPARE(explicitPicker.value(), 0.0);
}

void TestInputControls::ringPickerArbitraryTypes()
{
    // 1. QString values.
    md::RingPicker textPicker;
    textPicker.setValues(QVariantList{QStringLiteral("alpha"), QStringLiteral("beta"),
                                      QStringLiteral("gamma")});
    QCOMPARE(textPicker.count(), 3);
    QCOMPARE(textPicker.currentIndex(), 0);
    QCOMPARE(textPicker.currentText(), QStringLiteral("alpha"));
    QCOMPARE(textPicker.currentValue().toString(), QStringLiteral("alpha"));

    QSignalSpy indexSpy(&textPicker, &md::PickerBase::currentIndexChanged);
    QSignalSpy valueSpy(&textPicker, &md::PickerBase::currentValueChanged);
    textPicker.setCurrentIndex(2);
    QCOMPARE(textPicker.currentText(), QStringLiteral("gamma"));
    QCOMPARE(textPicker.currentValue().toString(), QStringLiteral("gamma"));
    QCOMPARE(static_cast<int>(indexSpy.count()), 1);
    QCOMPARE(static_cast<int>(valueSpy.count()), 1);

    // Cyclic keyboard navigation.
    QTest::keyClick(&textPicker, Qt::Key_Down);
    QCOMPARE(textPicker.currentIndex(), 0);
    QCOMPARE(textPicker.currentValue().toString(), QStringLiteral("alpha"));
    QTest::keyClick(&textPicker, Qt::Key_End);
    QCOMPARE(textPicker.currentIndex(), 2);
    QTest::keyClick(&textPicker, Qt::Key_Home);
    QCOMPARE(textPicker.currentIndex(), 0);

    // 2. int values.
    md::RingPicker numberPicker;
    numberPicker.setValues(QVariantList{10, 20, 30});
    QCOMPARE(numberPicker.count(), 3);
    QCOMPARE(numberPicker.currentValue().toInt(), 10);
    numberPicker.setCurrentIndex(1);
    QCOMPARE(numberPicker.currentValue().toInt(), 20);
    QCOMPARE(numberPicker.currentText(), QStringLiteral("20"));

    // 3. An opaque application type, displayed through a formatter.
    md::RingPicker structPicker;
    structPicker.setFormatter([](const QVariant& value) {
        if (value.metaType().id() == qMetaTypeId<TestEntry>())
            return value.value<TestEntry>().name;
        return value.toString();
    });
    structPicker.setValues(QVariantList{
        QVariant::fromValue(TestEntry{QStringLiteral("first"), 1}),
        QVariant::fromValue(TestEntry{QStringLiteral("second"), 2}),
        QVariant::fromValue(TestEntry{QStringLiteral("third"), 3})});

    QCOMPARE(structPicker.count(), 3);
    QCOMPARE(structPicker.currentText(), QStringLiteral("first"));
    QCOMPARE(structPicker.currentValue().value<TestEntry>().id, 1);

    structPicker.setCurrentIndex(2);
    QCOMPARE(structPicker.currentText(), QStringLiteral("third"));
    QCOMPARE(structPicker.currentValue().value<TestEntry>().name, QStringLiteral("third"));

    QTest::keyClick(&structPicker, Qt::Key_Down);
    QCOMPARE(structPicker.currentIndex(), 0);
    QCOMPARE(structPicker.currentValue().value<TestEntry>().id, 1);

    // 4. A borrowed source model: the picker reads through its Adapter and
    //    never owns the data.
    md::Model external;
    external.setValues(QVariantList{100, 200},
                       QStringList{QStringLiteral("hundred"), QStringLiteral("two hundred")});
    {
        md::RingPicker borrowed;
        borrowed.setSourceModel(&external);
        QCOMPARE(borrowed.count(), 2);
        QCOMPARE(borrowed.currentText(), QStringLiteral("hundred"));
        QCOMPARE(borrowed.currentValue().toInt(), 100);
        QCOMPARE(borrowed.adapter()->sourceModel(), &external);
        QTest::keyClick(&borrowed, Qt::Key_Down);
        QCOMPARE(borrowed.currentText(), QStringLiteral("two hundred"));
    }
    QCOMPARE(external.rowCount(), 2);

    // Non-cyclic mode clamps instead of wrapping.
    structPicker.setCyclic(false);
    structPicker.setCurrentIndex(2);
    QTest::keyClick(&structPicker, Qt::Key_Down);
    QCOMPARE(structPicker.currentIndex(), 2);
    QTest::keyClick(&structPicker, Qt::Key_Up);
    QCOMPARE(structPicker.currentIndex(), 1);
}

void TestInputControls::ringPickerPointerAndInertia()
{
    md::RingPicker picker;
    picker.setValues(QVariantList{QStringLiteral("A"), QStringLiteral("B"),
                                  QStringLiteral("C"), QStringLiteral("D"), QStringLiteral("E")});
    arrangeWidget(picker, 200, 240);

    const qreal rowHeight = picker.itemHeight();
    QCOMPARE(rowHeight, 40.0);

    // A click one row below the centre selects the next entry.
    QTest::mouseClick(&picker, Qt::LeftButton, Qt::NoModifier,
                      QPoint(100, 120 + static_cast<int>(rowHeight)));
    QCOMPARE(picker.currentIndex(), 1);
    QCOMPARE(picker.currentText(), QStringLiteral("B"));

    // Dragging up two rows advances the wheel by two entries.
    QTest::mousePress(&picker, Qt::LeftButton, Qt::NoModifier,
                      QPoint(100, 120 + static_cast<int>(rowHeight)));
    QTest::mouseMove(&picker, QPoint(100, 120 - static_cast<int>(rowHeight)));
    QTest::mouseRelease(&picker, Qt::LeftButton, Qt::NoModifier,
                        QPoint(100, 120 - static_cast<int>(rowHeight)));

    QCOMPARE(picker.currentIndex(), 3);

    // The inertia animation settles and snaps the wheel to a whole row.
    QTRY_VERIFY_WITH_TIMEOUT(!picker.isSettling(), 3000);
    QVERIFY(picker.currentIndex() >= 0);
    QVERIFY(picker.currentIndex() < picker.count());
    QVERIFY(!picker.currentText().isEmpty());
}

void TestInputControls::paintSmokeTest()
{
    // Every component paints its own surface; rendering them exercises the
    // geometry and painting code without a visible window.
    md::TextInput input;
    input.setLabel(QStringLiteral("Label"));
    input.setText(QStringLiteral("Value"));
    input.setHelperText(QStringLiteral("Helper"));
    arrangeWidget(input, 280, 76);
    QVERIFY(paintedPixels(renderWidget(input)) > 1000);

    md::NumberInput number;
    number.setRange(0.0, 10.0);
    arrangeWidget(number, 280, 56);
    QVERIFY(paintedPixels(renderWidget(number)) > 1000);

    md::DropDown dropdown;
    dropdown.addItem(QStringLiteral("One"), 1);
    dropdown.addItem(QStringLiteral("Two"), 2);
    dropdown.setCurrentIndex(1);
    arrangeWidget(dropdown, 240, 56);
    QVERIFY(paintedPixels(renderWidget(dropdown)) > 1000);

    md::Complete complete;
    complete.setSuggestions({QStringLiteral("Alpha"), QStringLiteral("Beta")});
    arrangeWidget(complete, 280, 56);
    QVERIFY(paintedPixels(renderWidget(complete)) > 1000);

    md::ColourPicker picker;
    arrangeWidget(picker, 300, 260);
    QVERIFY(paintedPixels(renderWidget(picker)) > 5000);

    md::NumberPicker numberPicker;
    numberPicker.setRange(0.0, 4.0);
    arrangeWidget(numberPicker, 200, 240);
    QVERIFY(paintedPixels(renderWidget(numberPicker)) > 1000);

    md::RingPicker ringPicker;
    ringPicker.setValues(QVariantList{QStringLiteral("A"), QStringLiteral("B"),
                                      QStringLiteral("C")});
    arrangeWidget(ringPicker, 200, 240);
    QVERIFY(paintedPixels(renderWidget(ringPicker)) > 500);

    // The menu popup is a window of its own; render it once it is open.
    dropdown.showPopup();
    QVERIFY(dropdown.popupWidget() != nullptr);
    QVERIFY(paintedPixels(renderWidget(*dropdown.popupWidget())) > 1000);
    dropdown.hidePopup();
}

QTEST_MAIN(TestInputControls)
#include "tst_input_controls.moc"
