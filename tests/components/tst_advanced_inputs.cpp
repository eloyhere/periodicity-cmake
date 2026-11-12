// Unit tests for the date / cascade / mention components (framework.md §14).
//
// As in tests/components/tst_basic_widgets.cpp, pointer and key input is
// delivered straight to the widget with QApplication::sendEvent, so no window
// activation or virtual cursor is involved and the suite runs offscreen. Every
// test drives the public geometry helpers (dateRect, fieldRect, itemRect, ...)
// which are the same rectangles painting uses, so a hit-testing regression is
// caught together with the visual one.

#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QTextCursor>
#include <QTextEdit>
#include <QVariant>

#include <md/components/cascader.h>
#include <md/components/date_picker.h>
#include <md/components/datetime_picker.h>
#include <md/components/mention.h>

namespace {

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

void sendKey(QWidget& widget, int key, const QString& text = QString(),
             Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
    QApplication::sendEvent(&widget, &press);

    QKeyEvent release(QEvent::KeyRelease, key, modifiers, text);
    QApplication::sendEvent(&widget, &release);
}

QList<md::CascaderNode> cascaderLevels()
{
    return {
        {QStringLiteral("Zhejiang"), QStringLiteral("zj"),
         {
             {QStringLiteral("Hangzhou"), QStringLiteral("hz"),
              {
                  {QStringLiteral("Xihu"), QStringLiteral("xh"), {}},
              }},
             {QStringLiteral("Ningbo"), QStringLiteral("nb"), {}},
         }},
        {QStringLiteral("Jiangsu"), QStringLiteral("js"),
         {
             {QStringLiteral("Nanjing"), QStringLiteral("nj"), {}},
         }},
    };
}

} // namespace

class TestAdvancedInputs : public QObject
{
    Q_OBJECT

private slots:
    void calendarSixWeekGridAndMonthBoundaries();
    void calendarRangeAndPointer();
    void calendarKeyboardAndPages();

    void dateTimeFieldStepping();
    void dateTimeDigitEntryAndRange();

    void cascaderExpandsAndClearsChildren();
    void cascaderKeyboard();

    void mentionTriggersAndInserts();
    void mentionAtKeyIsReentrancySafe();
    void mentionNodesAreAtomic();
    void mentionPlainTextAcrossLines();
};

void TestAdvancedInputs::calendarSixWeekGridAndMonthBoundaries()
{
    md::DatePicker picker;
    picker.resize(360, 396);
    picker.setSelectedDate(QDate(2026, 1, 15));
    QCOMPARE(picker.displayedMonth(), QDate(2026, 1, 1));

    // January 2026 starts on a Thursday, so a Monday-first six-week grid runs
    // from Monday 29 December 2025 to Sunday 8 February 2026.
    QVERIFY(picker.dateRect(QDate(2025, 12, 29)).isValid());
    QVERIFY(picker.dateRect(QDate(2026, 2, 8)).isValid());
    QVERIFY(picker.dateRect(QDate(2025, 12, 28)).isEmpty());
    QVERIFY(picker.dateRect(QDate(2026, 2, 9)).isEmpty());

    // All 42 grid cells are reachable and live in one contiguous block.
    int cells = 0;
    for (int day = 0; day < 42; ++day) {
        if (picker.dateRect(QDate(2025, 12, 29).addDays(day)).isValid())
            ++cells;
    }
    QCOMPARE(cells, 42);

    QSignalSpy monthSpy(&picker, &md::DatePicker::displayedMonthChanged);
    picker.previousMonth();
    QCOMPARE(picker.displayedMonth(), QDate(2025, 12, 1));
    QCOMPARE(monthSpy.count(), 1);

    picker.nextMonth();
    QCOMPARE(picker.displayedMonth(), QDate(2026, 1, 1));
    picker.nextMonth();
    QCOMPARE(picker.displayedMonth(), QDate(2026, 2, 1));
    QCOMPARE(monthSpy.count(), 3);

    // The header arrows navigate the displayed month as well.
    picker.setDisplayedMonth(QDate(2026, 1, 1));
    clickAt(picker, picker.previousButtonRect().center());
    QCOMPARE(picker.displayedMonth(), QDate(2025, 12, 1));
    clickAt(picker, picker.nextButtonRect().center());
    QCOMPARE(picker.displayedMonth(), QDate(2026, 1, 1));
}

void TestAdvancedInputs::calendarRangeAndPointer()
{
    md::DatePicker picker;
    picker.resize(360, 396);
    picker.setDateRange(QDate(2026, 3, 10), QDate(2026, 3, 20));

    // A selection outside the range is clamped to the nearest bound.
    picker.setSelectedDate(QDate(2026, 3, 1));
    QCOMPARE(picker.selectedDate(), QDate(2026, 3, 10));
    picker.setSelectedDate(QDate(2026, 3, 31));
    QCOMPARE(picker.selectedDate(), QDate(2026, 3, 20));

    QVERIFY(picker.isDateEnabled(QDate(2026, 3, 10)));
    QVERIFY(picker.isDateEnabled(QDate(2026, 3, 20)));
    QVERIFY(!picker.isDateEnabled(QDate(2026, 3, 9)));
    QVERIFY(!picker.isDateEnabled(QDate(2026, 3, 21)));

    picker.setSelectedDate(QDate(2026, 3, 15));
    QSignalSpy selectedSpy(&picker, &md::DatePicker::selectedDateChanged);
    QSignalSpy activatedSpy(&picker, &md::DatePicker::dateActivated);

    // A click on a disabled day moves nothing.
    clickAt(picker, picker.dateRect(QDate(2026, 3, 9)).center());
    QCOMPARE(picker.selectedDate(), QDate(2026, 3, 15));
    QCOMPARE(selectedSpy.count(), 0);

    // A click on an enabled day selects and activates it.
    clickAt(picker, picker.dateRect(QDate(2026, 3, 18)).center());
    QCOMPARE(picker.selectedDate(), QDate(2026, 3, 18));
    QCOMPARE(selectedSpy.count(), 1);
    QCOMPARE(activatedSpy.count(), 1);
    QCOMPARE(activatedSpy.at(0).at(0).toDate(), QDate(2026, 3, 18));

    // The arrows are disabled when the whole range fits in one month.
    const QDate displayed = picker.displayedMonth();
    clickAt(picker, picker.nextButtonRect().center());
    QCOMPARE(picker.displayedMonth(), displayed);

    // The range also drives the coarser pages: only March is selectable.
    picker.setCalendarView(md::DatePicker::CalendarView::Months);
    QVERIFY(picker.isMonthEnabled(3));
    QVERIFY(!picker.isMonthEnabled(2));
    QVERIFY(!picker.isMonthEnabled(4));
}

void TestAdvancedInputs::calendarKeyboardAndPages()
{
    md::DatePicker picker;
    picker.resize(360, 396);
    picker.setSelectedDate(QDate(2026, 1, 31));

    sendKey(picker, Qt::Key_Right);
    QCOMPARE(picker.selectedDate(), QDate(2026, 2, 1));
    QCOMPARE(picker.displayedMonth(), QDate(2026, 2, 1));

    sendKey(picker, Qt::Key_Down);
    QCOMPARE(picker.selectedDate(), QDate(2026, 2, 8));

    sendKey(picker, Qt::Key_PageUp);
    QCOMPARE(picker.selectedDate(), QDate(2026, 1, 8));

    sendKey(picker, Qt::Key_PageDown);
    QCOMPARE(picker.selectedDate(), QDate(2026, 2, 8));

    sendKey(picker, Qt::Key_Home);
    QCOMPARE(picker.selectedDate(), QDate(2026, 2, 1));

    sendKey(picker, Qt::Key_End);
    QCOMPARE(picker.selectedDate(), QDate(2026, 2, 28));

    QSignalSpy activatedSpy(&picker, &md::DatePicker::dateActivated);
    sendKey(picker, Qt::Key_Return);
    QCOMPARE(activatedSpy.count(), 1);
    QCOMPARE(activatedSpy.at(0).at(0).toDate(), QDate(2026, 2, 28));

    // The title cycles Days -> Months -> Years -> Days.
    clickAt(picker, picker.titleRect().center());
    QVERIFY(picker.calendarView() == md::DatePicker::CalendarView::Months);
    clickAt(picker, picker.titleRect().center());
    QVERIFY(picker.calendarView() == md::DatePicker::CalendarView::Years);

    // A year cell moves to that year's month page.
    clickAt(picker, picker.yearRect(2025).center());
    QVERIFY(picker.calendarView() == md::DatePicker::CalendarView::Months);
    QCOMPARE(picker.displayedMonth(), QDate(2025, 2, 1));

    // A month cell returns to the day grid at the chosen month.
    clickAt(picker, picker.monthRect(3).center());
    QVERIFY(picker.calendarView() == md::DatePicker::CalendarView::Days);
    QCOMPARE(picker.displayedMonth(), QDate(2025, 3, 1));

    clickAt(picker, picker.titleRect().center());
    QVERIFY(picker.calendarView() == md::DatePicker::CalendarView::Months);
    clickAt(picker, picker.titleRect().center());
    QVERIFY(picker.calendarView() == md::DatePicker::CalendarView::Years);
    clickAt(picker, picker.titleRect().center());
    QVERIFY(picker.calendarView() == md::DatePicker::CalendarView::Days);
}

void TestAdvancedInputs::dateTimeFieldStepping()
{
    md::DateTimePicker picker;
    picker.resize(360, 468);
    picker.setDateTime(QDateTime(QDate(2026, 5, 4), QTime(10, 30, 15)));
    QCOMPARE(picker.time(), QTime(10, 30, 15));
    QCOMPARE(picker.currentField(), 0);

    QSignalSpy spy(&picker, &md::DateTimePicker::dateTimeChanged);

    sendKey(picker, Qt::Key_Up);
    QCOMPARE(picker.time(), QTime(11, 30, 15));

    sendKey(picker, Qt::Key_Down);
    QCOMPARE(picker.time(), QTime(10, 30, 15));

    sendKey(picker, Qt::Key_Right);
    QCOMPARE(picker.currentField(), 1);
    sendKey(picker, Qt::Key_Plus);
    QCOMPARE(picker.time(), QTime(10, 31, 15));

    sendKey(picker, Qt::Key_Tab);
    QCOMPARE(picker.currentField(), 2);
    sendKey(picker, Qt::Key_Minus);
    QCOMPARE(picker.time(), QTime(10, 31, 14));

    sendKey(picker, Qt::Key_Left);
    QCOMPARE(picker.currentField(), 1);

    // A click on a segment gives it the keyboard focus.
    clickAt(picker, picker.fieldRect(0).center());
    QCOMPARE(picker.currentField(), 0);

    // Stepping wraps inside the time of day but keeps the date.
    picker.setTime(QTime(23, 59, 59));
    sendKey(picker, Qt::Key_Up);
    QCOMPARE(picker.time(), QTime(0, 59, 59));
    QCOMPARE(picker.dateTime().date(), QDate(2026, 5, 4));
    QVERIFY(spy.count() > 0);
}

void TestAdvancedInputs::dateTimeDigitEntryAndRange()
{
    md::DateTimePicker picker;
    picker.resize(360, 468);
    picker.setDateTime(QDateTime(QDate(2026, 5, 4), QTime(10, 30, 15)));

    picker.setCurrentField(0);
    sendKey(picker, Qt::Key_0, QStringLiteral("0"));
    sendKey(picker, Qt::Key_7, QStringLiteral("7"));
    QCOMPARE(picker.time().hour(), 7);
    QCOMPARE(picker.currentField(), 1);

    sendKey(picker, Qt::Key_1, QStringLiteral("1"));
    sendKey(picker, Qt::Key_4, QStringLiteral("4"));
    QCOMPARE(picker.time().minute(), 14);
    QCOMPARE(picker.currentField(), 2);

    // One digit that cannot start a valid value commits on its own.
    sendKey(picker, Qt::Key_9, QStringLiteral("9"));
    QCOMPARE(picker.time().second(), 9);
    QCOMPARE(picker.currentField(), 0);

    const QDateTime minimum(QDate(2026, 5, 4), QTime(9, 0));
    const QDateTime maximum(QDate(2026, 5, 4), QTime(17, 0));
    picker.setDateTimeRange(minimum, maximum);

    QCOMPARE(picker.datePicker()->minimumDate(), QDate(2026, 5, 4));
    QCOMPARE(picker.datePicker()->maximumDate(), QDate(2026, 5, 4));

    picker.setDateTime(QDateTime(QDate(2026, 5, 4), QTime(8, 0)));
    QCOMPARE(picker.dateTime(), minimum);

    picker.setDateTime(QDateTime(QDate(2026, 5, 4), QTime(18, 0)));
    QCOMPARE(picker.dateTime(), maximum);

    picker.setTime(QTime(12, 0));
    QCOMPARE(picker.time(), QTime(12, 0));
}

void TestAdvancedInputs::cascaderExpandsAndClearsChildren()
{
    md::Cascader cascader;
    cascader.setLevels(cascaderLevels());
    cascader.resize(480, 280);

    QCOMPARE(cascader.columnCount(), 1);
    QCOMPARE(cascader.currentPath(), QStringList());
    QVERIFY(!cascader.hasCompletePath());

    QSignalSpy pathSpy(&cascader, &md::Cascader::pathChanged);
    QSignalSpy leafSpy(&cascader, &md::Cascader::leafActivated);

    clickAt(cascader, cascader.itemRect(0, 0).center());
    QCOMPARE(cascader.currentPath(), QStringList{QStringLiteral("Zhejiang")});
    QCOMPARE(cascader.columnCount(), 2);
    QCOMPARE(pathSpy.count(), 1);

    clickAt(cascader, cascader.itemRect(1, 0).center());
    QCOMPARE(cascader.currentPath(),
             (QStringList{QStringLiteral("Zhejiang"), QStringLiteral("Hangzhou")}));
    QCOMPARE(cascader.columnCount(), 3);

    clickAt(cascader, cascader.itemRect(2, 0).center());
    QCOMPARE(cascader.currentPath(),
             (QStringList{QStringLiteral("Zhejiang"), QStringLiteral("Hangzhou"),
                          QStringLiteral("Xihu")}));
    QVERIFY(cascader.hasCompletePath());
    QCOMPARE(cascader.currentValue().toString(), QStringLiteral("xh"));
    QCOMPARE(leafSpy.count(), 1);

    // Choosing a sibling at an earlier level drops Xihu from the path.
    clickAt(cascader, cascader.itemRect(1, 1).center());
    QCOMPARE(cascader.currentPath(),
             (QStringList{QStringLiteral("Zhejiang"), QStringLiteral("Ningbo")}));
    QCOMPARE(cascader.columnCount(), 2);
    QVERIFY(cascader.hasCompletePath());
    QCOMPARE(cascader.currentValue().toString(), QStringLiteral("nb"));
    QCOMPARE(leafSpy.count(), 2);

    // Changing the root clears the whole branch.
    clickAt(cascader, cascader.itemRect(0, 1).center());
    QCOMPARE(cascader.currentPath(), QStringList{QStringLiteral("Jiangsu")});
    QCOMPARE(cascader.columnCount(), 2);
    QVERIFY(!cascader.hasCompletePath());
    QCOMPARE(cascader.currentValue().toString(), QStringLiteral("js"));

    clickAt(cascader, cascader.itemRect(1, 0).center());
    QCOMPARE(cascader.currentPath(),
             (QStringList{QStringLiteral("Jiangsu"), QStringLiteral("Nanjing")}));
    QVERIFY(cascader.hasCompletePath());
    QCOMPARE(cascader.currentValue().toString(), QStringLiteral("nj"));
}

void TestAdvancedInputs::cascaderKeyboard()
{
    md::Cascader cascader;
    cascader.setLevels(cascaderLevels());
    cascader.resize(480, 280);
    cascader.setFocus();

    sendKey(cascader, Qt::Key_Down);
    QCOMPARE(cascader.cursorIndex(), 1);

    sendKey(cascader, Qt::Key_Return);
    QCOMPARE(cascader.currentPath(), QStringList{QStringLiteral("Jiangsu")});
    QCOMPARE(cascader.activeColumn(), 1);

    sendKey(cascader, Qt::Key_Return);
    QCOMPARE(cascader.currentPath(),
             (QStringList{QStringLiteral("Jiangsu"), QStringLiteral("Nanjing")}));
    QCOMPARE(cascader.currentValue().toString(), QStringLiteral("nj"));

    sendKey(cascader, Qt::Key_Left);
    QCOMPARE(cascader.activeColumn(), 0);
}

void TestAdvancedInputs::mentionTriggersAndInserts()
{
    md::Mention mention;
    mention.resize(360, 140);
    mention.setCandidates({QStringLiteral("alice"), QStringLiteral("bob"),
                           QStringLiteral("carol")});
    QCOMPARE(mention.candidates().size(), 3);
    QCOMPARE(mention.plainText(), QString());

    QSignalSpy insertedSpy(&mention, &md::Mention::mentionInserted);

    sendKey(mention, Qt::Key_At, QStringLiteral("@"));
    QVERIFY(mention.isCandidatePopupVisible());
    QCOMPARE(mention.visibleCandidates(),
             (QStringList{QStringLiteral("alice"), QStringLiteral("bob"),
                          QStringLiteral("carol")}));

    sendKey(mention, Qt::Key_A, QStringLiteral("a"));
    sendKey(mention, Qt::Key_L, QStringLiteral("l"));
    QCOMPARE(mention.visibleCandidates(), QStringList{QStringLiteral("alice")});

    sendKey(mention, Qt::Key_Return);
    QVERIFY(!mention.isCandidatePopupVisible());
    QCOMPARE(mention.plainText(), QStringLiteral("@alice"));
    QCOMPARE(mention.mentionCount(), 1);
    QCOMPARE(insertedSpy.count(), 1);
    QCOMPARE(insertedSpy.at(0).at(0).toString(), QStringLiteral("alice"));
    QCOMPARE(insertedSpy.at(0).at(1).toString(), QStringLiteral("alice"));

    const md::MentionSpan span = mention.mentions().first();
    QCOMPARE(span.start, 0);
    QCOMPARE(span.length, 6); // "@alice"
    QCOMPARE(span.text, QStringLiteral("@alice"));
    QCOMPARE(span.value.toString(), QStringLiteral("alice"));
}

void TestAdvancedInputs::mentionAtKeyIsReentrancySafe()
{
    md::Mention mention;
    mention.resize(360, 140);
    mention.setCandidates({QStringLiteral("alice"), QStringLiteral("bob")});

    // An input method can commit '@' without any key event; the trigger is
    // derived from the document, so this opens the list as well.
    QInputMethodEvent ime;
    ime.setCommitString(QStringLiteral("@"));
    QApplication::sendEvent(mention.editor(), &ime);
    QVERIFY2(mention.isCandidatePopupVisible(),
             qPrintable(QStringLiteral("after IME text='%1'").arg(mention.plainText())));
    QCOMPARE(mention.plainText(), QStringLiteral("@"));
    QCOMPARE(mention.visibleCandidates().size(), 2);

    // Deleting the trigger closes the list.
    QTest::keyClick(mention.editor(), Qt::Key_Backspace);
    QVERIFY(!mention.isCandidatePopupVisible());

    // The ordinary key path still works, and a key the editor ignores is
    // forwarded once instead of bouncing between the widget and its editor.
    mention.clear();
    QTest::keyClick(&mention, Qt::Key_F1);
    QTest::keyClicks(&mention, QStringLiteral("@"));
    QVERIFY(mention.isCandidatePopupVisible());

    QTest::keyClicks(&mention, QStringLiteral("al"));
    QCOMPARE(mention.visibleCandidates(), QStringList{QStringLiteral("alice")});
    QTest::keyClick(&mention, Qt::Key_Escape);
    QVERIFY(!mention.isCandidatePopupVisible());
}

void TestAdvancedInputs::mentionNodesAreAtomic()
{
    md::Mention mention;
    mention.resize(360, 140);
    mention.insertMention(QStringLiteral("carol"), QStringLiteral("c-42"));
    sendKey(mention, Qt::Key_Space, QStringLiteral(" "));
    sendKey(mention, Qt::Key_H, QStringLiteral("h"));
    sendKey(mention, Qt::Key_I, QStringLiteral("i"));

    QCOMPARE(mention.plainText(), QStringLiteral("@carol hi"));
    QCOMPARE(mention.mentionCount(), 1);

    const md::MentionSpan span = mention.mentions().first();
    QCOMPARE(span.start, 0);
    QCOMPARE(span.length, 6); // "@carol"
    QCOMPARE(span.value.toString(), QStringLiteral("c-42"));

    // Left/Right step over the node as one position; Backspace removes the
    // whole node, never half of it.
    sendKey(mention, Qt::Key_Home);
    sendKey(mention, Qt::Key_Right);
    sendKey(mention, Qt::Key_Backspace);
    QCOMPARE(mention.plainText(), QStringLiteral(" hi"));
    QCOMPARE(mention.mentionCount(), 0);

    // Programmatic plain text does not fabricate nodes: only insertMention()
    // (or the '@' flow) creates an atomic mention.
    mention.clear();
    mention.setText(QStringLiteral("@alice"));
    QCOMPARE(mention.plainText(), QStringLiteral("@alice"));
    QCOMPARE(mention.mentionCount(), 0);
}

void TestAdvancedInputs::mentionPlainTextAcrossLines()
{
    md::Mention mention;
    mention.resize(360, 160);
    mention.setText(QStringLiteral("first line\nsecond "));
    {
        QTextCursor cursor = mention.editor()->textCursor();
        cursor.movePosition(QTextCursor::End);
        mention.editor()->setTextCursor(cursor);
    }
    mention.insertMention(QStringLiteral("dave"), QStringLiteral("d-1"));

    QCOMPARE(mention.plainText(), QStringLiteral("first line\nsecond @dave"));
    QCOMPARE(mention.mentionCount(), 1);

    // The range is an offset into plainText(), so the newline counts as one
    // character and the node expands to "@dave".
    const md::MentionSpan span = mention.mentions().first();
    QCOMPARE(span.start, 18);
    QCOMPARE(span.length, 5);
    QCOMPARE(span.text, QStringLiteral("@dave"));
    QCOMPARE(span.value.toString(), QStringLiteral("d-1"));
}

QTEST_MAIN(TestAdvancedInputs)
#include "tst_advanced_inputs.moc"
