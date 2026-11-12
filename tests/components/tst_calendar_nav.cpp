// Unit tests for the time / navigation components (framework.md §14).
//
// As in tests/components/tst_advanced_inputs.cpp, pointer and key input is
// delivered straight to the widget with QApplication::sendEvent, so no window
// activation or virtual cursor is involved and the suite runs offscreen. Every
// test drives the public geometry helpers (cellRect, columnRect, pageButtonRect,
// itemRect, anchorRect, ...) — the same rectangles painting uses — so a
// hit-testing regression is caught together with the visual one.
//
// The timer-driven parts are the only ones that wait: the carousel's autoplay
// is observed with QTRY_VERIFY (and then switched off and checked for silence),
// and every explicit transition is ended with finishTransition() so the layout
// assertions do not depend on how fast the machine is.

#include <QApplication>
#include <QColor>
#include <QDate>
#include <QEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointF>
#include <QRegion>
#include <QSignalSpy>
#include <QTest>
#include <QTime>
#include <QWheelEvent>

#include <md/components/affix.h>
#include <md/components/anchor.h>
#include <md/components/button.h>
#include <md/components/calendar.h>
#include <md/components/carousel.h>
#include <md/components/pagination.h>
#include <md/components/timeline.h>
#include <md/components/time_picker.h>

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

} // namespace

class TestCalendarNav : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void calendarCellValue();
    void calendarGridGeometry();
    void calendarSelection();
    void calendarDataCells();
    void calendarKeyboard();
    void calendarPages();

    void timePickerWheelAndKeys();
    void timePickerDragSnap();
    void timePickerFormats();

    void carouselNavigation();
    void carouselIndicators();
    void carouselAutoPlay();

    void paginationModel();
    void paginationPointerAndKeys();

    void timelineItems();
    void timelineScrolling();

    void affixSticky();

    void anchorScrollAndClick();

    void paintSmoke();
};

void TestCalendarNav::initTestCase()
{
    // CalendarCell crosses the cellClicked() signal; QSignalSpy needs the
    // metatype to be known.
    qRegisterMetaType<md::CalendarCell>("md::CalendarCell");
}

void TestCalendarNav::calendarCellValue()
{
    md::CalendarCell first;
    first.date = QDate(2026, 3, 15);

    md::CalendarCell second;
    second.date = QDate(2026, 3, 15);

    QVERIFY(first.date.isValid());
    QVERIFY(first.enabled);
    QVERIFY(!first.selected);
    QVERIFY(!first.today);
    QVERIFY(first.label.isEmpty());
    QVERIFY(!first.marker.isValid());
    QVERIFY(first.currentMonth);
    QCOMPARE(first, second);

    second.label = QStringLiteral("Trip");
    QVERIFY(!(first == second));

    second.label.clear();
    second.marker = QColor(Qt::red);
    QVERIFY(!(first == second));

    second.marker = QColor();
    second.enabled = false;
    QVERIFY(!(first == second));
    QVERIFY(!second.enabled);
}

void TestCalendarNav::calendarGridGeometry()
{
    md::Calendar calendar;
    calendar.resize(340, 336);
    calendar.setSelectedDate(QDate(2026, 3, 15));

    // Monday-first: 1 March 2026 is a Sunday, so the grid opens on 23 February.
    QCOMPARE(calendar.gridStart(), QDate(2026, 2, 23));
    QCOMPARE(calendar.displayedMonth(), QDate(2026, 3, 1));
    QCOMPARE(calendar.selectedDate(), QDate(2026, 3, 15));

    QVERIFY(!calendar.cellRect(QDate(2026, 2, 23)).isEmpty());
    QVERIFY(!calendar.cellRect(QDate(2026, 4, 5)).isEmpty());
    // The sheet is exactly six weeks, so the next Monday is outside it.
    QVERIFY(calendar.cellRect(QDate(2026, 4, 6)).isEmpty());

    // One row is one week: the same top, seven columns apart.
    const QRectF monday = calendar.cellRect(QDate(2026, 2, 23));
    const QRectF tuesday = calendar.cellRect(QDate(2026, 2, 24));
    const QRectF nextMonday = calendar.cellRect(QDate(2026, 3, 2));
    QCOMPARE(monday.top(), tuesday.top());
    QVERIFY(tuesday.left() > monday.left());
    QVERIFY(nextMonday.top() > monday.top());
    QCOMPARE(nextMonday.left(), monday.left());

    // Every weekday column exists and the weekday row sits above the grid.
    for (int column = 0; column < 7; ++column)
        QVERIFY(!calendar.weekdayRect(column).isEmpty());
    QVERIFY(calendar.weekdayRect(7).isEmpty());
    QVERIFY(calendar.weekdayRect(0).bottom() <= monday.top());

    QVERIFY(calendar.cellForDate(QDate(2026, 2, 23)).currentMonth == false);
    QVERIFY(calendar.cellForDate(QDate(2026, 3, 15)).currentMonth);
    QVERIFY(calendar.cellForDate(QDate(2026, 3, 15)).selected);
}

void TestCalendarNav::calendarSelection()
{
    md::Calendar calendar;
    calendar.resize(340, 336);
    calendar.setDateRange(QDate(2026, 3, 1), QDate(2026, 3, 31));
    calendar.setSelectedDate(QDate(2026, 3, 10));

    QSignalSpy dateSpy(&calendar, &md::Calendar::dateSelected);
    QSignalSpy cellSpy(&calendar, &md::Calendar::cellClicked);

    clickAt(calendar, calendar.cellRect(QDate(2026, 3, 17)).center());
    QCOMPARE(calendar.selectedDate(), QDate(2026, 3, 17));
    QCOMPARE(dateSpy.count(), 1);
    QCOMPARE(dateSpy.at(0).at(0).toDate(), QDate(2026, 3, 17));
    QCOMPARE(cellSpy.count(), 1);

    const md::CalendarCell clicked = cellSpy.at(0).at(0).value<md::CalendarCell>();
    QCOMPARE(clicked.date, QDate(2026, 3, 17));
    QVERIFY(clicked.selected);
    QVERIFY(clicked.currentMonth);

    // A date outside the range is painted but inert.
    clickAt(calendar, calendar.cellRect(QDate(2026, 4, 2)).center());
    QCOMPARE(calendar.selectedDate(), QDate(2026, 3, 17));
    QCOMPARE(dateSpy.count(), 1);

    // A cell disabled by the data overlay is inert as well.
    md::CalendarCell blocked;
    blocked.date = QDate(2026, 3, 20);
    blocked.enabled = false;
    calendar.setCell(blocked);
    QVERIFY(!calendar.isDateEnabled(QDate(2026, 3, 20)));

    clickAt(calendar, calendar.cellRect(QDate(2026, 3, 20)).center());
    QCOMPARE(calendar.selectedDate(), QDate(2026, 3, 17));
    QCOMPARE(dateSpy.count(), 1);
}

void TestCalendarNav::calendarDataCells()
{
    md::Calendar calendar;
    calendar.resize(340, 336);
    calendar.setSelectedDate(QDate(2026, 3, 15));

    md::CalendarCell marked;
    marked.date = QDate(2026, 3, 12);
    marked.label = QStringLiteral("Trip");
    marked.marker = QColor(Qt::red);
    calendar.setCell(marked);

    QCOMPARE(static_cast<int>(calendar.cells().size()), 1);

    const md::CalendarCell value = calendar.cellForDate(QDate(2026, 3, 12));
    QCOMPARE(value.label, QStringLiteral("Trip"));
    QCOMPARE(value.marker, QColor(Qt::red));
    QVERIFY(value.currentMonth);
    QVERIFY(value.enabled);
    QVERIFY(!value.selected);

    // setMarkedDates is the shorthand path and paints a valid marker.
    calendar.setMarkedDates({QDate(2026, 3, 2), QDate(2026, 3, 3)});
    QVERIFY(calendar.cellForDate(QDate(2026, 3, 2)).marker.isValid());
    QVERIFY(calendar.cellForDate(QDate(2026, 3, 3)).marker.isValid());
    QCOMPARE(static_cast<int>(calendar.cells().size()), 3);

    calendar.clearMarkedDates();
    // Every marker goes away; the caller's label and flags stay.
    QVERIFY(!calendar.cellForDate(QDate(2026, 3, 2)).marker.isValid());
    QVERIFY(!calendar.cellForDate(QDate(2026, 3, 12)).marker.isValid());
    QCOMPARE(calendar.cellForDate(QDate(2026, 3, 12)).label, QStringLiteral("Trip"));

    calendar.clearCells();
    QCOMPARE(static_cast<int>(calendar.cells().size()), 0);
    QVERIFY(calendar.cellForDate(QDate(2026, 3, 12)).label.isEmpty());
}

void TestCalendarNav::calendarKeyboard()
{
    md::Calendar calendar;
    calendar.resize(340, 336);
    calendar.setDateRange(QDate(2025, 1, 1), QDate(2027, 12, 31));
    calendar.setSelectedDate(QDate(2026, 3, 10));

    sendKey(calendar, Qt::Key_Right);
    QCOMPARE(calendar.selectedDate(), QDate(2026, 3, 11));

    sendKey(calendar, Qt::Key_Left);
    QCOMPARE(calendar.selectedDate(), QDate(2026, 3, 10));

    sendKey(calendar, Qt::Key_Down);
    QCOMPARE(calendar.selectedDate(), QDate(2026, 3, 17));

    sendKey(calendar, Qt::Key_Up);
    QCOMPARE(calendar.selectedDate(), QDate(2026, 3, 10));

    sendKey(calendar, Qt::Key_Home);
    QCOMPARE(calendar.selectedDate(), QDate(2026, 3, 1));

    sendKey(calendar, Qt::Key_End);
    QCOMPARE(calendar.selectedDate(), QDate(2026, 3, 31));

    QSignalSpy monthSpy(&calendar, &md::Calendar::monthChanged);
    sendKey(calendar, Qt::Key_PageDown);
    QCOMPARE(calendar.selectedDate(), QDate(2026, 4, 30));
    QCOMPARE(calendar.displayedMonth(), QDate(2026, 4, 1));
    QCOMPARE(monthSpy.count(), 1);
    QCOMPARE(monthSpy.at(0).at(0).toDate(), QDate(2026, 4, 1));

    sendKey(calendar, Qt::Key_PageUp);
    QCOMPARE(calendar.displayedMonth(), QDate(2026, 3, 1));

    QSignalSpy dateSpy(&calendar, &md::Calendar::dateSelected);
    sendKey(calendar, Qt::Key_Return);
    QCOMPARE(dateSpy.count(), 1);
    QCOMPARE(dateSpy.at(0).at(0).toDate(), calendar.selectedDate());

    // The range still bounds the keyboard.
    calendar.setSelectedDate(QDate(2026, 3, 31));
    sendKey(calendar, Qt::Key_Right);
    QCOMPARE(calendar.selectedDate(), QDate(2026, 4, 1));
}

void TestCalendarNav::calendarPages()
{
    md::Calendar calendar;
    calendar.resize(340, 336);
    calendar.setSelectedDate(QDate(2026, 3, 15));

    QCOMPARE(calendar.page(), md::Calendar::Page::Days);
    QVERIFY(!calendar.monthButtonRect().isEmpty());
    QVERIFY(!calendar.yearButtonRect().isEmpty());

    clickAt(calendar, calendar.yearButtonRect().center());
    QCOMPARE(calendar.page(), md::Calendar::Page::Years);
    QVERIFY(calendar.yearButtonRect().isEmpty());
    QCOMPARE(calendar.yearPageStart(), 2016);
    QVERIFY(!calendar.yearRect(2026).isEmpty());
    QVERIFY(!calendar.yearRect(2027).isEmpty());
    QVERIFY(calendar.yearRect(2028).isEmpty());

    clickAt(calendar, calendar.yearRect(2027).center());
    QCOMPARE(calendar.page(), md::Calendar::Page::Months);
    QCOMPARE(calendar.displayedMonth().year(), 2027);

    clickAt(calendar, calendar.monthRect(5).center());
    QCOMPARE(calendar.page(), md::Calendar::Page::Days);
    QCOMPARE(calendar.displayedMonth(), QDate(2027, 5, 1));

    clickAt(calendar, calendar.monthButtonRect().center());
    QCOMPARE(calendar.page(), md::Calendar::Page::Months);

    clickAt(calendar, calendar.monthButtonRect().center());
    QCOMPARE(calendar.page(), md::Calendar::Page::Days);
}

void TestCalendarNav::timePickerWheelAndKeys()
{
    md::TimePicker picker;
    picker.resize(256, 200);
    picker.setTime(QTime(9, 30, 15));

    QCOMPARE(picker.time(), QTime(9, 30, 15));
    QCOMPARE(picker.hourFormat(), md::TimePicker::HourFormat::Hour24);
    QCOMPARE(picker.columnCount(), 3);

    const int hourColumn = picker.fieldColumn(md::TimePicker::Field::Hour);
    const int minuteColumn = picker.fieldColumn(md::TimePicker::Field::Minute);
    const int secondColumn = picker.fieldColumn(md::TimePicker::Field::Second);
    QCOMPARE(hourColumn, 0);
    QCOMPARE(minuteColumn, 1);
    QCOMPARE(secondColumn, 2);

    QCOMPARE(picker.columnText(hourColumn), QStringLiteral("09"));
    QCOMPARE(picker.columnText(minuteColumn), QStringLiteral("30"));
    QCOMPARE(picker.columnText(secondColumn), QStringLiteral("15"));

    QVERIFY(!picker.columnRect(hourColumn).isEmpty());
    QVERIFY(picker.columnRect(3).isEmpty());
    QVERIFY(!picker.rowRect(hourColumn, 0).isEmpty());
    QVERIFY(!picker.selectionBandRect().isEmpty());

    QSignalSpy timeSpy(&picker, &md::TimePicker::timeChanged);

    // One wheel notch is one row; a notch upward moves the wheel back.
    sendWheel(picker, picker.columnRect(hourColumn).center(), -120);
    QCOMPARE(picker.time(), QTime(10, 30, 15));
    QCOMPARE(timeSpy.count(), 1);

    sendWheel(picker, picker.columnRect(hourColumn).center(), 120);
    QCOMPARE(picker.time(), QTime(9, 30, 15));

    // Up / Down step the current column, Left / Right move between columns.
    sendKey(picker, Qt::Key_Down);
    QCOMPARE(picker.time(), QTime(10, 30, 15));

    sendKey(picker, Qt::Key_Up);
    QCOMPARE(picker.time(), QTime(9, 30, 15));

    sendKey(picker, Qt::Key_PageDown);
    QCOMPARE(picker.time(), QTime(14, 30, 15));

    sendKey(picker, Qt::Key_PageUp);
    QCOMPARE(picker.time(), QTime(9, 30, 15));

    sendKey(picker, Qt::Key_End);
    QCOMPARE(picker.time(), QTime(23, 30, 15));

    sendKey(picker, Qt::Key_Home);
    QCOMPARE(picker.time(), QTime(0, 30, 15));

    sendKey(picker, Qt::Key_Right);
    QCOMPARE(picker.currentColumn(), 1);

    sendKey(picker, Qt::Key_Up);
    QCOMPARE(picker.time(), QTime(0, 29, 15));

    // Home / End are column-local, and the step wraps cyclically.
    sendKey(picker, Qt::Key_End);
    QCOMPARE(picker.time(), QTime(0, 59, 15));

    sendKey(picker, Qt::Key_Down);
    QCOMPARE(picker.time(), QTime(0, 0, 15));

    sendKey(picker, Qt::Key_Left);
    QCOMPARE(picker.currentColumn(), 0);
}

void TestCalendarNav::timePickerDragSnap()
{
    md::TimePicker picker;
    picker.resize(256, 200);
    picker.setTime(QTime(9, 30, 15));

    const QPointF start = picker.columnRect(0).center();
    // A drag of one and a bit rows upward: the wheel follows continuously and
    // snaps to the nearest row on release.
    dragAt(picker, start, start - QPointF(0.0, 60.0));
    picker.finishAnimation();

    QCOMPARE(picker.time().hour(), 10);
    QCOMPARE(picker.time().minute(), 30);
    QVERIFY(!picker.isAnimating());

    // The snapped offset is a whole row.
    const qreal offset = picker.columnOffset(0);
    QCOMPARE(offset, qreal(qRound(offset)));
}

void TestCalendarNav::timePickerFormats()
{
    md::TimePicker picker;
    picker.resize(256, 200);
    picker.setTime(QTime(15, 30, 15));

    picker.setHourFormat(12);
    QCOMPARE(picker.hourFormat(), md::TimePicker::HourFormat::Hour12);
    QVERIFY(picker.meridianVisible());
    QCOMPARE(picker.columnCount(), 4);

    const int hourColumn = picker.fieldColumn(md::TimePicker::Field::Hour);
    const int meridianColumn = picker.fieldColumn(md::TimePicker::Field::Meridian);
    QCOMPARE(hourColumn, 0);
    QCOMPARE(meridianColumn, 1);
    QVERIFY(meridianColumn >= 0);

    QCOMPARE(picker.columnText(hourColumn), QStringLiteral("03"));
    QCOMPARE(picker.columnText(meridianColumn), QStringLiteral("PM"));

    // 24-hour values survive the format switch unchanged.
    QCOMPARE(picker.time(), QTime(15, 30, 15));

    picker.stepField(md::TimePicker::Field::Meridian, 1);
    QCOMPARE(picker.time(), QTime(3, 30, 15));
    QCOMPARE(picker.columnText(meridianColumn), QStringLiteral("AM"));

    picker.setHourFormat(24);
    QCOMPARE(picker.columnCount(), 3);
    QCOMPARE(picker.columnText(0), QStringLiteral("03"));

    picker.setSecondVisible(false);
    QCOMPARE(picker.columnCount(), 2);
    QCOMPARE(picker.fieldColumn(md::TimePicker::Field::Second), -1);

    picker.setSecondVisible(true);
    QCOMPARE(picker.columnCount(), 3);

    picker.setHourFormat(12);
    picker.setSecondVisible(false);
    QCOMPARE(picker.columnCount(), 3);
}

void TestCalendarNav::carouselNavigation()
{
    md::Carousel carousel;
    carousel.resize(360, 240);
    carousel.setDuration(0);

    auto* first = new md::Button(&carousel);
    auto* second = new md::Button(&carousel);
    auto* third = new md::Button(&carousel);

    carousel.setSlides({first, second, third});
    QCOMPARE(carousel.slideCount(), 3);
    QCOMPARE(carousel.currentIndex(), 0);
    QCOMPARE(carousel.slides().size(), 3);
    QCOMPARE(carousel.slideArea().toRect(), first->geometry());

    QSignalSpy spy(&carousel, &md::Carousel::currentChanged);

    carousel.next();
    carousel.finishTransition();
    QCOMPARE(carousel.currentIndex(), 1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 1);
    QCOMPARE(carousel.slideArea().toRect(), second->geometry());
    QVERIFY(first->isHidden());
    QVERIFY(!second->isHidden());

    carousel.previous();
    carousel.finishTransition();
    QCOMPARE(carousel.currentIndex(), 0);

    // Loop wraps in both directions.
    carousel.previous();
    carousel.finishTransition();
    QCOMPARE(carousel.currentIndex(), 2);

    carousel.next();
    carousel.finishTransition();
    QCOMPARE(carousel.currentIndex(), 0);

    // Without loop the ends are hard stops.
    carousel.setLoop(false);
    QVERIFY(!carousel.loop());
    carousel.setCurrentIndex(2);
    carousel.finishTransition();
    carousel.next();
    QCOMPARE(carousel.currentIndex(), 2);

    carousel.previous();
    carousel.finishTransition();
    QCOMPARE(carousel.currentIndex(), 1);

    // An explicit index out of range is clamped when loop is off.
    carousel.setCurrentIndex(99);
    QCOMPARE(carousel.currentIndex(), 2);
}

void TestCalendarNav::carouselIndicators()
{
    md::Carousel carousel;
    carousel.resize(360, 240);
    carousel.setDuration(0);

    carousel.addSlide(new md::Button(&carousel));
    carousel.addSlide(new md::Button(&carousel));
    carousel.addSlide(new md::Button(&carousel));

    QVERIFY(!carousel.indicatorBarRect().isEmpty());
    for (int index = 0; index < 3; ++index)
        QVERIFY(!carousel.indicatorRect(index).isEmpty());
    QVERIFY(carousel.indicatorRect(3).isEmpty());

    // The indicator band is below the slides, never behind them.
    QVERIFY(carousel.indicatorBarRect().top() >= carousel.slideArea().bottom());

    clickAt(carousel, carousel.indicatorRect(2).center());
    QCOMPARE(carousel.currentIndex(), 2);
    carousel.finishTransition();

    clickAt(carousel, carousel.indicatorRect(0).center());
    QCOMPARE(carousel.currentIndex(), 0);

    // A transition reports progress between 0 and 1 and settles at 1.
    carousel.setDuration(300);
    carousel.setCurrentIndex(1);
    QVERIFY(carousel.transitionProgress() >= 0.0);
    QVERIFY(carousel.transitionProgress() < 1.0);
    carousel.finishTransition();
    QCOMPARE(carousel.transitionProgress(), 1.0);
    QVERIFY(!carousel.isAnimating());
}

void TestCalendarNav::carouselAutoPlay()
{
    md::Carousel carousel;
    carousel.resize(300, 200);
    carousel.setDuration(0);
    carousel.addSlide(new md::Button(&carousel));
    carousel.addSlide(new md::Button(&carousel));
    carousel.addSlide(new md::Button(&carousel));

    QSignalSpy spy(&carousel, &md::Carousel::currentChanged);

    carousel.setInterval(40);
    QCOMPARE(carousel.interval(), 40);

    carousel.setAutoPlay(true);
    QVERIFY(carousel.autoPlay());
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 2000);

    carousel.setAutoPlay(false);
    QVERIFY(!carousel.autoPlay());

    const int settled = spy.count();
    QTest::qWait(150);
    QCOMPARE(spy.count(), settled);

    // A single slide has nothing to advance to, so autoplay stays quiet.
    md::Carousel single;
    single.resize(200, 150);
    single.addSlide(new md::Button(&single));
    single.setInterval(20);
    QSignalSpy singleSpy(&single, &md::Carousel::currentChanged);
    single.setAutoPlay(true);
    QTest::qWait(100);
    QCOMPARE(singleSpy.count(), 0);
}

void TestCalendarNav::paginationModel()
{
    md::Pagination pager;
    pager.resize(360, 48);

    QCOMPARE(pager.pageCount(), 1);
    QCOMPARE(pager.currentPage(), 1);

    QSignalSpy spy(&pager, &md::Pagination::pageChanged);

    pager.setPageCount(10);
    QCOMPARE(pager.pageCount(), 10);
    QCOMPARE(pager.currentPage(), 1);
    QCOMPARE(spy.count(), 0);

    pager.setCurrentPage(5);
    QCOMPARE(pager.currentPage(), 5);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 5);

    // Setting the same page again is silent.
    pager.setCurrentPage(5);
    QCOMPARE(spy.count(), 1);

    // The run is compressed: first ... window ... last.
    const QList<int> pages = pager.visiblePages();
    QCOMPARE(static_cast<int>(pages.size()), 7);
    QCOMPARE(pages.first(), 1);
    QCOMPARE(pages.last(), 10);
    QCOMPARE(pages.contains(-1), true);
    QCOMPARE(pages.contains(5), true);
    QVERIFY(!pager.pageButtonRect(5).isEmpty());
    QVERIFY(pager.pageButtonRect(2).isEmpty());

    // Fewer pages than slots shows every page and no ellipsis.
    pager.setPageCount(3);
    QCOMPARE(pager.currentPage(), 3);
    const QList<int> small = pager.visiblePages();
    QCOMPARE(static_cast<int>(small.size()), 3);
    QCOMPARE(small.at(0), 1);
    QCOMPARE(small.at(1), 2);
    QCOMPARE(small.at(2), 3);

    // The page count is clamped to at least one page.
    pager.setPageCount(0);
    QCOMPARE(pager.pageCount(), 1);
    QCOMPARE(pager.currentPage(), 1);

    pager.setCurrentPage(-4);
    QCOMPARE(pager.currentPage(), 1);
}

void TestCalendarNav::paginationPointerAndKeys()
{
    md::Pagination pager;
    pager.resize(360, 48);
    pager.setPageCount(10);
    pager.setCurrentPage(5);

    QSignalSpy spy(&pager, &md::Pagination::pageChanged);

    clickAt(pager, pager.pageButtonRect(4).center());
    QCOMPARE(pager.currentPage(), 4);
    QCOMPARE(spy.count(), 1);

    clickAt(pager, pager.firstButtonRect().center());
    QCOMPARE(pager.currentPage(), 1);

    clickAt(pager, pager.lastButtonRect().center());
    QCOMPARE(pager.currentPage(), 10);

    clickAt(pager, pager.previousButtonRect().center());
    QCOMPARE(pager.currentPage(), 9);

    clickAt(pager, pager.nextButtonRect().center());
    QCOMPARE(pager.currentPage(), 10);

    sendKey(pager, Qt::Key_Home);
    QCOMPARE(pager.currentPage(), 1);

    sendKey(pager, Qt::Key_Right);
    QCOMPARE(pager.currentPage(), 2);

    sendKey(pager, Qt::Key_Left);
    QCOMPARE(pager.currentPage(), 1);

    sendKey(pager, Qt::Key_End);
    QCOMPARE(pager.currentPage(), 10);

    sendKey(pager, Qt::Key_PageUp);
    QCOMPARE(pager.currentPage(), 9);

    sendKey(pager, Qt::Key_PageDown);
    QCOMPARE(pager.currentPage(), 10);
}

void TestCalendarNav::timelineItems()
{
    md::Timeline timeline;
    timeline.resize(320, 200);

    QCOMPARE(timeline.itemCount(), 0);
    QCOMPARE(timeline.contentHeight(), qreal(16.0));

    md::TimelineItem item;
    item.time = QStringLiteral("09:00");
    item.title = QStringLiteral("Kickoff");
    item.description = QStringLiteral("Welcome and agenda");
    item.color = QColor(Qt::blue);
    timeline.addItem(item);

    item.time = QStringLiteral("10:30");
    item.title = QStringLiteral("Design");
    item.description = QStringLiteral("Review the flows");
    item.color = QColor();
    timeline.addItem(item);

    item.time = QStringLiteral("12:00");
    item.title = QStringLiteral("Lunch");
    item.description.clear();
    item.icon = QStringLiteral("\u25cf");
    timeline.addItem(item);

    item.time = QStringLiteral("14:00");
    item.title = QStringLiteral("Build");
    item.description = QStringLiteral("Ship the demo");
    item.icon.clear();
    timeline.addItem(item);

    QCOMPARE(timeline.itemCount(), 4);
    QCOMPARE(static_cast<int>(timeline.items().size()), 4);
    QCOMPARE(timeline.items().at(1).title, QStringLiteral("Design"));
    QCOMPARE(timeline.items().at(2).icon, QStringLiteral("\u25cf"));

    // Four 72 dp rows plus the two 8 dp insets.
    QCOMPARE(timeline.contentHeight(), qreal(304.0));
    QVERIFY(timeline.maximumScrollOffset() > 0.0);

    // Rows are stacked and the marker sits on the same row as its text.
    QCOMPARE(timeline.itemRect(0).left(), timeline.itemRect(1).left());
    QVERIFY(timeline.itemRect(1).top() > timeline.itemRect(0).top());
    QCOMPARE(timeline.itemRect(0).height(), timeline.itemHeight());
    QVERIFY(!timeline.markerRect(0).isEmpty());
    QVERIFY(timeline.itemRect(4).isEmpty());
    QVERIFY(timeline.markerRect(4).isEmpty());

    QSignalSpy spy(&timeline, &md::Timeline::itemClicked);
    clickAt(timeline, timeline.itemRect(0).center());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 0);

    clickAt(timeline, timeline.itemRect(1).center());
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toInt(), 1);

    // A click outside any row does nothing.
    clickAt(timeline, QPointF(2.0, 2.0));
    QCOMPARE(spy.count(), 2);
}

void TestCalendarNav::timelineScrolling()
{
    md::Timeline timeline;
    timeline.resize(320, 200);

    for (int index = 0; index < 4; ++index) {
        md::TimelineItem item;
        item.time = QStringLiteral("0%1:00").arg(index + 9);
        item.title = QStringLiteral("Entry %1").arg(index);
        timeline.addItem(item);
    }

    const qreal maximum = timeline.maximumScrollOffset();
    QCOMPARE(maximum, qreal(104.0));

    timeline.setScrollOffset(1000);
    QCOMPARE(timeline.scrollOffset(), maximum);

    timeline.setScrollOffset(-40);
    QCOMPARE(timeline.scrollOffset(), 0.0);

    // The rows move with the offset.
    const qreal top = timeline.itemRect(0).top();
    timeline.setScrollOffset(40);
    QCOMPARE(timeline.itemRect(0).top(), top - 40.0);
    QVERIFY(timeline.itemRect(0).top() < 0.0);

    // At the maximum offset the last row is fully inside the viewport.
    timeline.setScrollOffset(maximum);
    QVERIFY(timeline.itemRect(3).bottom() <= timeline.height() + 1.0);

    QSignalSpy spy(&timeline, &md::Timeline::itemClicked);
    clickAt(timeline, timeline.itemRect(3).center());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 3);

    sendKey(timeline, Qt::Key_Home);
    QCOMPARE(timeline.scrollOffset(), 0.0);

    sendKey(timeline, Qt::Key_End);
    QCOMPARE(timeline.scrollOffset(), maximum);

    sendKey(timeline, Qt::Key_Up);
    QCOMPARE(timeline.scrollOffset(), maximum - timeline.itemHeight());
}

void TestCalendarNav::affixSticky()
{
    md::Affix affix;
    affix.resize(320, 56);
    affix.setContent(new md::Button(&affix));

    QVERIFY(affix.content() != nullptr);
    QCOMPARE(affix.offset(), 0.0);
    QVERIFY(!affix.isStuck());

    affix.setThreshold(100);
    QCOMPARE(affix.threshold(), 100.0);

    QSignalSpy spy(&affix, &md::Affix::stuckChanged);

    affix.setScrollOffset(60);
    QVERIFY(!affix.isStuck());
    QCOMPARE(affix.offset(), -60.0);
    QCOMPARE(affix.contentViewRect().top(), -60.0);

    affix.setScrollOffset(150);
    QVERIFY(affix.isStuck());
    QCOMPARE(affix.offset(), -100.0);
    QCOMPARE(affix.contentViewRect().top(), -100.0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);

    // Scrolling further does not move the stuck bar.
    affix.setScrollOffset(400);
    QCOMPARE(affix.offset(), -100.0);
    QCOMPARE(affix.contentViewRect().top(), -100.0);

    affix.setScrollOffset(10);
    QVERIFY(!affix.isStuck());
    QCOMPARE(affix.offset(), -10.0);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toBool(), false);

    // targetOffset shifts the boundary in document coordinates.
    affix.setTargetOffset(50);
    QCOMPARE(affix.targetOffset(), 50.0);
    QCOMPARE(affix.stickPoint(), 150.0);

    affix.setScrollOffset(140);
    QVERIFY(!affix.isStuck());
    QCOMPARE(affix.offset(), -90.0);

    affix.setScrollOffset(160);
    QVERIFY(affix.isStuck());
    QCOMPARE(affix.offset(), -100.0);

    // Raising the threshold above the local scroll releases the bar again.
    affix.setThreshold(200);
    QVERIFY(!affix.isStuck());
    QCOMPARE(affix.offset(), -110.0);
}

void TestCalendarNav::anchorScrollAndClick()
{
    md::Anchor anchor;
    anchor.resize(224, 200);

    anchor.addAnchor(QStringLiteral("intro"), QStringLiteral("Introduction"), 0.0);
    anchor.addAnchor(QStringLiteral("usage"), QStringLiteral("Usage"), 400.0);
    anchor.addAnchor(QStringLiteral("api"), QStringLiteral("API"), 900.0);

    QCOMPARE(anchor.anchorCount(), 3);
    QCOMPARE(anchor.indexOfAnchor(QStringLiteral("usage")), 1);
    QCOMPARE(anchor.indexOfAnchor(QStringLiteral("missing")), -1);
    QCOMPARE(anchor.activeAnchor(), QStringLiteral("intro"));
    QCOMPARE(anchor.activeIndex(), 0);
    QCOMPARE(anchor.anchorAt(2).label, QStringLiteral("API"));

    QVERIFY(!anchor.anchorRect(0).isEmpty());
    QVERIFY(anchor.anchorRect(3).isEmpty());
    QVERIFY(anchor.anchorRect(1).top() > anchor.anchorRect(0).top());
    QVERIFY(anchor.anchorRect(0).contains(anchor.anchorRect(0).left() + 4.0,
                                          anchor.anchorRect(0).center().y()));

    QSignalSpy activeSpy(&anchor, &md::Anchor::activeAnchorChanged);
    QSignalSpy activatedSpy(&anchor, &md::Anchor::anchorActivated);

    // Following the page only reports the new highlight.
    anchor.setScrollOffset(500);
    QCOMPARE(anchor.activeAnchor(), QStringLiteral("usage"));
    QCOMPARE(anchor.activeIndex(), 1);
    QCOMPARE(activeSpy.count(), 1);
    QCOMPARE(activeSpy.at(0).at(0).toString(), QStringLiteral("usage"));
    QCOMPARE(activatedSpy.count(), 0);

    anchor.setScrollOffset(2000);
    QCOMPARE(anchor.activeAnchor(), QStringLiteral("api"));
    QCOMPARE(activeSpy.count(), 2);

    // Scrolling back above an offset walks the highlight back.
    anchor.setScrollOffset(10);
    QCOMPARE(anchor.activeAnchor(), QStringLiteral("intro"));

    // setActiveAnchor is programmatic: it highlights but does not navigate.
    anchor.setActiveAnchor(QStringLiteral("usage"));
    QCOMPARE(anchor.activeAnchor(), QStringLiteral("usage"));
    QCOMPARE(activatedSpy.count(), 0);

    // A click activates, and always emits even when the entry is current.
    clickAt(anchor, anchor.anchorRect(1).center());
    QCOMPARE(activatedSpy.count(), 1);
    QCOMPARE(activatedSpy.at(0).at(0).toString(), QStringLiteral("usage"));
    QCOMPARE(activatedSpy.at(0).at(1).toDouble(), 400.0);

    clickAt(anchor, anchor.anchorRect(2).center());
    QCOMPARE(anchor.activeAnchor(), QStringLiteral("api"));
    QCOMPARE(activatedSpy.count(), 2);
    QCOMPARE(activatedSpy.at(1).at(0).toString(), QStringLiteral("api"));
    QCOMPARE(activatedSpy.at(1).at(1).toDouble(), 900.0);

    // Keyboard: Down moves the highlight, Enter activates it.
    sendKey(anchor, Qt::Key_Home);
    QCOMPARE(anchor.activeAnchor(), QStringLiteral("intro"));

    sendKey(anchor, Qt::Key_Down);
    QCOMPARE(anchor.activeAnchor(), QStringLiteral("usage"));
    QCOMPARE(activatedSpy.count(), 2);

    sendKey(anchor, Qt::Key_Return);
    QCOMPARE(activatedSpy.count(), 3);
    QCOMPARE(activatedSpy.at(2).at(0).toString(), QStringLiteral("usage"));

    // Removing the active entry keeps a valid highlight.
    anchor.removeAnchor(QStringLiteral("usage"));
    QCOMPARE(anchor.anchorCount(), 2);
    QVERIFY(anchor.activeIndex() >= 0);
    QVERIFY(!anchor.activeAnchor().isEmpty());

    anchor.clearAnchors();
    QCOMPARE(anchor.anchorCount(), 0);
    QCOMPARE(anchor.activeAnchor(), QString());
    QCOMPARE(anchor.activeIndex(), -1);
}

void TestCalendarNav::paintSmoke()
{
    // Every component paints itself; rendering one offscreen is the cheapest
    // way to prove the painter runs on the geometry it computed.
    md::Calendar calendar;
    calendar.resize(340, 336);
    calendar.setSelectedDate(QDate(2026, 3, 15));
    md::CalendarCell marked;
    marked.date = QDate(2026, 3, 12);
    marked.label = QStringLiteral("Trip");
    marked.marker = QColor(Qt::red);
    calendar.setCell(marked);
    QVERIFY(paintedPixels(renderWidget(calendar)) > 0);

    md::TimePicker picker;
    picker.resize(256, 200);
    picker.setTime(QTime(9, 30, 15));
    QVERIFY(paintedPixels(renderWidget(picker)) > 0);
    picker.setHourFormat(12);
    QVERIFY(paintedPixels(renderWidget(picker)) > 0);

    md::Carousel carousel;
    carousel.resize(360, 240);
    carousel.setDuration(0);
    carousel.addSlide(new md::Button(QStringLiteral("One"), &carousel));
    carousel.addSlide(new md::Button(QStringLiteral("Two"), &carousel));
    QVERIFY(paintedPixels(renderWidget(carousel)) > 0);

    md::Pagination pager;
    pager.resize(360, 48);
    pager.setPageCount(10);
    pager.setCurrentPage(5);
    QVERIFY(paintedPixels(renderWidget(pager)) > 0);

    md::Timeline timeline;
    timeline.resize(320, 200);
    for (int index = 0; index < 3; ++index) {
        md::TimelineItem item;
        item.time = QStringLiteral("09:0%1").arg(index);
        item.title = QStringLiteral("Entry %1").arg(index);
        item.description = QStringLiteral("Details");
        timeline.addItem(item);
    }
    QVERIFY(paintedPixels(renderWidget(timeline)) > 0);
    timeline.setScrollOffset(timeline.maximumScrollOffset());
    QVERIFY(paintedPixels(renderWidget(timeline)) > 0);

    // A stuck bar paints its own background and shadow, so it is the state
    // worth rendering: the loose one is deliberately transparent.
    md::Affix affix;
    affix.resize(320, 56);
    affix.setContent(new md::Button(QStringLiteral("Sticky"), &affix));
    affix.setThreshold(4);
    affix.setScrollOffset(20);
    QVERIFY(affix.isStuck());
    QVERIFY(paintedPixels(renderWidget(affix)) > 0);

    md::Anchor anchor;
    anchor.resize(224, 200);
    anchor.addAnchor(QStringLiteral("intro"), QStringLiteral("Introduction"), 0.0);
    anchor.addAnchor(QStringLiteral("api"), QStringLiteral("API"), 400.0);
    QVERIFY(paintedPixels(renderWidget(anchor)) > 0);
}

QTEST_MAIN(TestCalendarNav)
#include "tst_calendar_nav.moc"
