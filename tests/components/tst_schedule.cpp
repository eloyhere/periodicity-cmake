// Unit tests for the Schedule component (framework.md §14).
//
// Everything here is geometry and event delivery: the widget is driven through
// its public rectangles (dayColumnRect, hourLabelRect, eventRect, laneFor) —
// the same rectangles painting and hit-testing use — and mouse/key events are
// sent straight to the widget with QApplication::sendEvent, so no window
// activation or virtual cursor is involved and the suite runs offscreen.
//
// The drag test uses a deliberately unsnapped distance (18.75 minutes worth of
// pixels) and asserts the 15-minute result; the keyboard test then asserts that
// Alt+Right produces the exact same QDateTime as that drag, which is the
// equivalence WCAG 2.5.7 asks for. Every schedule is a bare stack object; the
// baseline theme resolves to density 1.0, so 48 dp per hour is 48 px.

#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QEvent>
#include <QImage>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QRegion>
#include <QSignalSpy>
#include <QTest>
#include <QTime>
#include <QVector>
#include <QWheelEvent>

#include <md/components/schedule.h>
#include <md/core/i18n.h>

#include <stdexcept>

namespace {

const QDate kDate = QDate(2026, 3, 9);
const QTime kRangeStart = QTime(8, 0);
const QTime kRangeEnd = QTime(18, 0);

[[nodiscard]] md::ScheduleEvent makeEvent(const QString& id, const QDate& date,
                                          const QTime& start, int durationMinutes)
{
    md::ScheduleEvent event;
    event.id = id;
    event.title = id;
    event.start = QDateTime(date, start);
    event.end = event.start.addSecs(qint64(durationMinutes) * 60);
    return event;
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

void dragAt(QWidget& widget, const QPointF& from, const QPointF& to)
{
    sendMouse(widget, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    sendMouse(widget, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
    sendMouse(widget, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
}

void sendKey(QWidget& widget, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QKeyEvent press(QEvent::KeyPress, key, modifiers);
    QApplication::sendEvent(&widget, &press);

    QKeyEvent release(QEvent::KeyRelease, key, modifiers);
    QApplication::sendEvent(&widget, &release);
}

void sendWheel(QWidget& widget, const QPointF& position, int angleY)
{
    const QPoint global = widget.mapToGlobal(position.toPoint());
    QWheelEvent event(position, QPointF(global), QPoint(), QPoint(0, angleY),
                      Qt::MouseButtons(Qt::NoButton), Qt::KeyboardModifiers(Qt::NoModifier),
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(&widget, &event);
}

// Renders the widget into a transparent image so the paint path runs without a
// visible window.
[[nodiscard]] QImage renderWidget(QWidget& widget)
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

[[nodiscard]] int paintedPixels(const QImage& image)
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

// The pixel height of one hour, measured from the public labels rather than
// assumed, so the test also follows a non-default density.
[[nodiscard]] qreal hourStep(const md::Schedule& schedule)
{
    return schedule.hourLabelRect(9).top() - schedule.hourLabelRect(8).top();
}

// A component whose content hook throws. View's paint entry point has to catch
// it, report it through lastError() and keep the process alive.
class ThrowingSchedule : public md::Schedule
{
public:
    explicit ThrowingSchedule(QWidget* parent = nullptr) : md::Schedule(parent) {}

protected:
    void paintContent(QPainter&, const QRectF&) override
    {
        throw std::runtime_error("schedule paint failure");
    }
};

} // namespace

class TestSchedule : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();

    void dayAndWeekGeometry();
    void hourEventGeometry();
    void overlappingEventsUseLanes();
    void clickActivatesAndSelects();
    void dragSnapsAndKeyboardMatches();
    void settersInvalidate();
    void emptyListPaints();
    void eventPaintingSmoke();
    void throwingOverrideDoesNotCrash();
    void localeFormattingAndTranslations();
};

void TestSchedule::cleanup()
{
    md::i18n::uninstall();
}

void TestSchedule::dayAndWeekGeometry()
{
    md::Schedule schedule;
    schedule.resize(700, 480);
    schedule.setDate(kDate);

    schedule.setMode(md::Schedule::Mode::Day);
    QCOMPARE(schedule.columnCount(), 1);
    QVERIFY(!schedule.dayColumnRect(0).isEmpty());
    QVERIFY(schedule.dayColumnRect(1).isEmpty());
    QCOMPARE(schedule.columnDate(0), kDate);
    QVERIFY(!schedule.columnDate(1).isValid());

    // The header sits exactly above the body, and the gutter is 56 dp wide.
    QCOMPARE(schedule.headerRect().bottom(), schedule.bodyRect().top());
    QCOMPARE(schedule.headerRect().height(), 32.0);
    QCOMPARE(schedule.gutterRect().width(), 56.0);
    QCOMPARE(schedule.gutterRect().height(), schedule.bodyRect().height());
    QVERIFY(schedule.gutterRect().right() <= schedule.dayColumnRect(0).left());

    const qreal dayWidth = schedule.dayColumnRect(0).width();
    QVERIFY(dayWidth > 0.0);

    schedule.setMode(md::Schedule::Mode::Week);
    QCOMPARE(schedule.columnCount(), 7);
    QCOMPARE(schedule.columnDate(0), kDate);
    QCOMPARE(schedule.columnDate(6), kDate.addDays(6));
    QVERIFY(!schedule.columnDate(7).isValid());

    for (int column = 0; column < 7; ++column) {
        const QRectF cell = schedule.dayColumnRect(column);
        QVERIFY(!cell.isEmpty());
        QCOMPARE(cell.top(), schedule.bodyRect().top());
        QCOMPARE(cell.height(), schedule.bodyRect().height());
        QCOMPARE(cell.width(), dayWidth / 7.0);

        const QRectF header = schedule.weekdayHeaderRect(column);
        QCOMPARE(header.left(), cell.left());
        QCOMPARE(header.width(), cell.width());
        QCOMPARE(header.bottom(), schedule.headerRect().bottom());
    }

    QVERIFY(schedule.dayColumnRect(7).isEmpty());
    QVERIFY(schedule.weekdayHeaderRect(7).isEmpty());
    QVERIFY(schedule.dayColumnRect(0).right() <= schedule.dayColumnRect(1).left());

    // RTL mirrors the reading order: column 0 is adjacent to the gutter on the
    // right and the gutter itself moves to the right edge.
    schedule.setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(schedule.gutterRect().right(), schedule.bodyRect().right());
    QCOMPARE(schedule.dayColumnRect(0).right(), schedule.gutterRect().left());
    QVERIFY(schedule.dayColumnRect(0).left() > schedule.dayColumnRect(1).left());
    QCOMPARE(schedule.weekdayHeaderRect(0).left(), schedule.dayColumnRect(0).left());
    schedule.setLayoutDirection(Qt::LeftToRight);
}

void TestSchedule::hourEventGeometry()
{
    md::Schedule schedule;
    schedule.resize(560, 400);
    schedule.setDate(kDate);
    schedule.setMode(md::Schedule::Mode::Day);
    schedule.setDayRange(kRangeStart, kRangeEnd);

    QVERIFY(schedule.hourLabelRect(7).isEmpty());
    QVERIFY(!schedule.hourLabelRect(8).isEmpty());
    QVERIFY(!schedule.hourLabelRect(12).isEmpty());
    QVERIFY(!schedule.hourLabelRect(18).isEmpty());
    QVERIFY(schedule.hourLabelRect(19).isEmpty());

    // 48 dp per hour, and the range shifts every block by the same amount.
    QCOMPARE(hourStep(schedule), 48.0);
    QCOMPARE(schedule.hourLabelRect(9).top() - schedule.hourLabelRect(8).top(), 48.0);
    QCOMPARE(schedule.contentHeight(), 10.0 * 48.0);

    schedule.setEvents(QVector<md::ScheduleEvent>{makeEvent(
        QStringLiteral("standup"), kDate, QTime(9, 0), 60)});
    QCOMPARE(schedule.events().size(), 1);

    const QRectF label8 = schedule.hourLabelRect(8);
    const QRectF block = schedule.eventRect(QStringLiteral("standup"));
    QVERIFY(!block.isEmpty());
    QVERIFY(schedule.eventRect(1).isEmpty());

    // The 09:00 block starts at the 09:00 line, which is below the 08:00 label.
    QCOMPARE(block.top(), schedule.hourLabelRect(9).top());
    QVERIFY(block.top() > label8.bottom());
    QVERIFY(block.bottom() < schedule.hourLabelRect(10).top());
    QVERIFY(block.left() >= schedule.gutterRect().right());
    QVERIFY(block.width() <= schedule.dayColumnRect(0).width());

    // Scrolling moves the block with the grid, not relative to the viewport.
    const qreal unscrolledTop = block.top();
    schedule.setScrollOffset(48.0);
    QCOMPARE(schedule.eventRect(QStringLiteral("standup")).top(), unscrolledTop - 48.0);
    schedule.setScrollOffset(0.0);

    // A time before the range start is clipped away, not drawn above 08:00.
    schedule.setEvents(QVector<md::ScheduleEvent>{makeEvent(
        QStringLiteral("early"), kDate, QTime(7, 0), 30)});
    QVERIFY(schedule.eventRect(QStringLiteral("early")).isEmpty());
    QVERIFY(schedule.eventAt(QPointF(0.0, 0.0)) < 0);
}

void TestSchedule::overlappingEventsUseLanes()
{
    md::Schedule schedule;
    schedule.resize(560, 800);
    schedule.setDate(kDate);
    schedule.setMode(md::Schedule::Mode::Day);
    schedule.setDayRange(kRangeStart, kRangeEnd);
    schedule.setEvents(QVector<md::ScheduleEvent>{
        makeEvent(QStringLiteral("a"), kDate, QTime(9, 0), 60),
        makeEvent(QStringLiteral("b"), kDate, QTime(9, 30), 60),
        makeEvent(QStringLiteral("c"), kDate, QTime(11, 0), 60),
    });

    QCOMPARE(schedule.laneFor(0), 0);
    QCOMPARE(schedule.laneFor(1), 1);
    QCOMPARE(schedule.laneFor(2), 0);
    QCOMPARE(schedule.laneCount(0), 2);
    QCOMPARE(schedule.laneCount(1), 2);
    QCOMPARE(schedule.laneCount(2), 1);

    const QRectF first = schedule.eventRect(0);
    const QRectF second = schedule.eventRect(1);
    const QRectF isolated = schedule.eventRect(2);
    const qreal columnWidth = schedule.dayColumnRect(0).width();

    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());
    QVERIFY(!isolated.isEmpty());
    QVERIFY(first.left() < second.left());
    QVERIFY(first.right() <= second.left());
    QVERIFY(first.width() < columnWidth * 0.6);
    QVERIFY(second.width() < columnWidth * 0.6);
    QVERIFY(isolated.width() > columnWidth * 0.9);
    QVERIFY(isolated.left() < second.left());

    QVERIFY(schedule.laneFor(-1) < 0);
    QVERIFY(schedule.laneFor(3) < 0);
    QCOMPARE(schedule.laneCount(3), 0);
}

void TestSchedule::clickActivatesAndSelects()
{
    md::Schedule schedule;
    schedule.resize(560, 640);
    schedule.setDate(kDate);
    schedule.setMode(md::Schedule::Mode::Day);
    schedule.setDayRange(kRangeStart, kRangeEnd);
    schedule.setEvents(QVector<md::ScheduleEvent>{makeEvent(
        QStringLiteral("a"), kDate, QTime(9, 0), 60)});

    QSignalSpy activateSpy(&schedule, &md::Schedule::eventActivated);
    const QRectF block = schedule.eventRect(QStringLiteral("a"));
    clickAt(schedule, block.center());

    QCOMPARE(schedule.selectedEvent(), QStringLiteral("a"));
    QCOMPARE(activateSpy.count(), 1);
    QCOMPARE(activateSpy.at(0).at(0).toString(), QStringLiteral("a"));

    // A click on empty grid clears the selection and does not activate.
    const QPointF empty(schedule.dayColumnRect(0).right() - 8.0,
                        schedule.bodyRect().bottom() - 8.0);
    QCOMPARE(schedule.eventAt(empty), -1);
    clickAt(schedule, empty);
    QVERIFY(schedule.selectedEvent().isEmpty());
    QCOMPARE(activateSpy.count(), 1);
}

void TestSchedule::dragSnapsAndKeyboardMatches()
{
    md::Schedule schedule;
    schedule.resize(560, 640);
    schedule.setDate(kDate);
    schedule.setMode(md::Schedule::Mode::Day);
    schedule.setDayRange(kRangeStart, kRangeEnd);
    schedule.setEditable(true);
    schedule.setEvents(QVector<md::ScheduleEvent>{makeEvent(
        QStringLiteral("a"), kDate, QTime(9, 0), 60)});

    QSignalSpy moveSpy(&schedule, &md::Schedule::eventMoved);
    QSignalSpy activateSpy(&schedule, &md::Schedule::eventActivated);

    const qreal quarter = hourStep(schedule) / 4.0;
    const QRectF block = schedule.eventRect(QStringLiteral("a"));
    const QPointF grab = block.center();

    // 1.25 slots of pointer travel is 18.75 minutes; the component must snap it
    // to one 15-minute step rather than reporting the raw distance.
    dragAt(schedule, grab, grab + QPointF(0.0, quarter * 1.25));

    QCOMPARE(schedule.selectedEvent(), QStringLiteral("a"));
    QCOMPARE(moveSpy.count(), 1);
    QCOMPARE(moveSpy.at(0).at(0).toString(), QStringLiteral("a"));

    const QDateTime draggedStart = moveSpy.at(0).at(1).toDateTime();
    QCOMPARE(draggedStart, QDateTime(kDate, QTime(9, 15)));

    // Controlled: the widget reported the move and left the model alone.
    QCOMPARE(schedule.events().at(0).start, QDateTime(kDate, QTime(9, 0)));
    QCOMPARE(activateSpy.count(), 0);

    // Alt+Right is the keyboard equivalent of the same one-slot move.
    moveSpy.clear();
    sendKey(schedule, Qt::Key_Right, Qt::AltModifier);
    QCOMPARE(moveSpy.count(), 1);
    QCOMPARE(moveSpy.at(0).at(1).toDateTime(), draggedStart);

    // Alt+Left / Alt+Down move by the same units in the other directions.
    moveSpy.clear();
    sendKey(schedule, Qt::Key_Left, Qt::AltModifier);
    QCOMPARE(moveSpy.count(), 1);
    QCOMPARE(moveSpy.at(0).at(1).toDateTime(), QDateTime(kDate, QTime(8, 45)));

    moveSpy.clear();
    sendKey(schedule, Qt::Key_Down, Qt::AltModifier);
    QCOMPARE(moveSpy.count(), 1);
    QCOMPARE(moveSpy.at(0).at(1).toDateTime(),
             QDateTime(kDate.addDays(1), QTime(9, 0)));

    // Enter activates the selected event, the pointer-free path to the click.
    sendKey(schedule, Qt::Key_Return);
    QCOMPARE(activateSpy.count(), 1);
    QCOMPARE(activateSpy.at(0).at(0).toString(), QStringLiteral("a"));

    // Movement is gated on editable; activation is not.
    schedule.setEditable(false);
    moveSpy.clear();
    sendKey(schedule, Qt::Key_Right, Qt::AltModifier);
    QCOMPARE(moveSpy.count(), 0);
}

void TestSchedule::settersInvalidate()
{
    md::Schedule schedule;
    QCOMPARE(schedule.mode(), md::Schedule::Mode::Week);
    QCOMPARE(schedule.columnCount(), 7);
    QCOMPARE(schedule.date(), QDate::currentDate());
    QCOMPARE(schedule.accessibleName(), QStringLiteral("Schedule"));
    QVERIFY(schedule.accessibleDescription().startsWith(QStringLiteral("Week of ")));
    QVERIFY(schedule.isNowLineVisible());
    QVERIFY(!schedule.isEditable());
    QVERIFY(schedule.events().isEmpty());

    schedule.resize(700, 480);
    schedule.setDate(kDate);
    QCOMPARE(schedule.date(), kDate);
    QCOMPARE(schedule.columnDate(0), kDate);
    QCOMPARE(schedule.columnDate(6), kDate.addDays(6));
    QVERIFY(schedule.accessibleDescription().contains(kDate.toString(Qt::ISODate)));

    // Mode changes the column count and the accessibility description.
    schedule.setMode(md::Schedule::Mode::Day);
    QCOMPARE(schedule.columnCount(), 1);
    QCOMPARE(schedule.accessibleDescription(), kDate.toString(Qt::ISODate));
    schedule.setMode(md::Schedule::Mode::Week);
    QCOMPARE(schedule.columnCount(), 7);
    QVERIFY(schedule.accessibleDescription().startsWith(QStringLiteral("Week of ")));

    // A range change moves the same event relative to the viewport.
    schedule.setMode(md::Schedule::Mode::Day);
    schedule.setDayRange(kRangeStart, kRangeEnd);
    schedule.setEvents(QVector<md::ScheduleEvent>{makeEvent(
        QStringLiteral("a"), kDate, QTime(12, 0), 60)});
    const qreal before = schedule.eventRect(QStringLiteral("a")).top();
    QVERIFY(before > 0.0);
    schedule.setDayRange(QTime(10, 0), kRangeEnd);
    const qreal after = schedule.eventRect(QStringLiteral("a")).top();
    QVERIFY(after < before);
    QCOMPARE(after, schedule.hourLabelRect(12).top());
    schedule.setDayRange(kRangeStart, kRangeEnd);

    // Week/day and date changes invalidate visibility.
    schedule.setMode(md::Schedule::Mode::Week);
    schedule.setEvents(QVector<md::ScheduleEvent>{makeEvent(
        QStringLiteral("later"), kDate.addDays(3), QTime(12, 0), 60)});
    QVERIFY(!schedule.eventRect(QStringLiteral("later")).isEmpty());
    schedule.setMode(md::Schedule::Mode::Day);
    QVERIFY(schedule.eventRect(QStringLiteral("later")).isEmpty());
    schedule.setMode(md::Schedule::Mode::Week);
    QVERIFY(!schedule.eventRect(QStringLiteral("later")).isEmpty());
    schedule.setDate(kDate.addDays(7));
    QVERIFY(schedule.eventRect(QStringLiteral("later")).isEmpty());
    schedule.setDate(kDate);
    QVERIFY(!schedule.eventRect(QStringLiteral("later")).isEmpty());

    // Selection follows the model: an unknown id is ignored, a removed event
    // clears the selection.
    schedule.setSelectedEvent(QStringLiteral("later"));
    QCOMPARE(schedule.selectedEvent(), QStringLiteral("later"));
    schedule.setSelectedEvent(QStringLiteral("missing"));
    QCOMPARE(schedule.selectedEvent(), QStringLiteral("later"));
    schedule.setEvents({});
    QVERIFY(schedule.selectedEvent().isEmpty());
    QVERIFY(schedule.eventRect(0).isEmpty());

    // Boolean setters.
    schedule.setNowLineVisible(false);
    QVERIFY(!schedule.isNowLineVisible());
    schedule.setNowLineVisible(true);
    QVERIFY(schedule.isNowLineVisible());
    schedule.setEditable(true);
    QVERIFY(schedule.isEditable());
    schedule.setEditable(false);
    QVERIFY(!schedule.isEditable());

    // Scroll setters clamp to the content, and the wheel moves by one hour.
    schedule.resize(560, 200);
    schedule.setDayRange(QTime(0, 0), QTime(0, 0)); // the full 00:00–24:00 day
    const qreal maximum = schedule.maximumScrollOffset();
    QCOMPARE(maximum, 24.0 * 48.0 - schedule.bodyRect().height());
    QVERIFY(maximum > 0.0);

    schedule.setScrollOffset(10000.0);
    QCOMPARE(schedule.scrollOffset(), maximum);
    schedule.setScrollOffset(-5.0);
    QCOMPARE(schedule.scrollOffset(), 0.0);

    sendWheel(schedule, QPointF(120.0, 120.0), -120);
    QCOMPARE(schedule.scrollOffset(), 48.0);
    sendWheel(schedule, QPointF(120.0, 120.0), 120);
    QCOMPARE(schedule.scrollOffset(), 0.0);
}

void TestSchedule::emptyListPaints()
{
    md::Schedule schedule;
    schedule.resize(560, 480);
    schedule.setDate(kDate);
    schedule.setMode(md::Schedule::Mode::Week);
    schedule.setDayRange(kRangeStart, kRangeEnd);

    QVERIFY(schedule.events().isEmpty());
    QCOMPARE(schedule.laneFor(0), -1);
    QCOMPARE(schedule.eventAt(schedule.dayColumnRect(0).center()), -1);

    QVERIFY(paintedPixels(renderWidget(schedule)) > 0);
    QVERIFY(schedule.lastError().isEmpty());

    schedule.setMode(md::Schedule::Mode::Day);
    QVERIFY(paintedPixels(renderWidget(schedule)) > 0);
    QVERIFY(schedule.lastError().isEmpty());
}

void TestSchedule::eventPaintingSmoke()
{
    md::Schedule schedule;
    schedule.resize(560, 480);
    schedule.setDate(kDate);
    schedule.setMode(md::Schedule::Mode::Week);
    schedule.setDayRange(kRangeStart, kRangeEnd);
    schedule.setEvents(QVector<md::ScheduleEvent>{
        makeEvent(QStringLiteral("a"), kDate, QTime(9, 0), 60),
        makeEvent(QStringLiteral("b"), kDate, QTime(9, 30), 90),
        makeEvent(QStringLiteral("otherDay"), kDate.addDays(2), QTime(14, 0), 60),
        makeEvent(QStringLiteral("outside"), kDate, QTime(7, 0), 30),
    });
    schedule.setSelectedEvent(QStringLiteral("a"));
    schedule.setEditable(true);

    // Exercises the fill + accent bar + title + selection border + lane split;
    // `outside` is clipped away and must not disturb the paint pass.
    QVERIFY(!schedule.eventRect(QStringLiteral("a")).isEmpty());
    QVERIFY(schedule.eventRect(QStringLiteral("outside")).isEmpty());
    QVERIFY(paintedPixels(renderWidget(schedule)) > 0);
    QVERIFY(schedule.lastError().isEmpty());

    schedule.setMode(md::Schedule::Mode::Day);
    QVERIFY(paintedPixels(renderWidget(schedule)) > 0);
    QVERIFY(schedule.lastError().isEmpty());
}

void TestSchedule::throwingOverrideDoesNotCrash()
{
    ThrowingSchedule schedule;
    schedule.resize(480, 320);

    const QImage image = renderWidget(schedule);
    QVERIFY(!image.isNull());
    QVERIFY(!schedule.lastError().isEmpty());
}

void TestSchedule::localeFormattingAndTranslations()
{
    QVERIFY(md::i18n::install(QStringLiteral("zh_CN")));
    const QLocale chinese(QStringLiteral("zh_CN"));

    md::Schedule schedule;
    schedule.resize(700, 480);
    schedule.setMode(md::Schedule::Mode::Week);
    schedule.setDate(QDate(2026, 3, 11)); // Wednesday

    // zh_CN starts its week on Monday, so the visible week starts two days
    // before the anchor date.
    QCOMPARE(schedule.weekStart(), QDate(2026, 3, 9));
    QCOMPARE(schedule.columnDate(0), QDate(2026, 3, 9));
    QCOMPARE(schedule.columnDate(6), QDate(2026, 3, 15));

    // The weekday label is built from QLocale::dayName(ShortFormat)/toString()
    // of the installed locale, not from an English weekday table.
    const QString expectedMonday =
        QStringLiteral("%1 %2")
            .arg(chinese.dayName(Qt::Monday, QLocale::ShortFormat),
                 chinese.toString(QDate(2026, 3, 9), QStringLiteral("d")));
    QCOMPARE(schedule.columnHeaderText(0), expectedMonday);
    QVERIFY(schedule.columnHeaderText(0).startsWith(
        chinese.dayName(Qt::Monday, QLocale::ShortFormat)));

    // With the translator installed, Schedule's own source strings resolve.
    QCOMPARE(schedule.tr("No events"), QStringLiteral("无事件"));
    QCOMPARE(schedule.tr("Week of %1").arg(QStringLiteral("x")),
             QStringLiteral("x 所在周"));
    QVERIFY(schedule.accessibleDescription().contains(QStringLiteral("所在周")));

    schedule.setMode(md::Schedule::Mode::Day);
    QCOMPARE(schedule.columnDate(0), QDate(2026, 3, 11));
    QVERIFY(schedule.columnHeaderText(0).startsWith(
        chinese.dayName(Qt::Wednesday, QLocale::LongFormat)));

    // The empty state is painted, not layered over any child widget.
    schedule.setEvents({});
    QVERIFY(paintedPixels(renderWidget(schedule)) > 0);
    QVERIFY(schedule.lastError().isEmpty());
}

QTEST_MAIN(TestSchedule)
#include "tst_schedule.moc"
