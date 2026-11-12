// Unit tests for md::Clock (framework.md §5.9, §14).
//
// The clock is exercised offscreen: the application theme is pinned to the
// baseline light scheme, every deterministic test freezes the instant in UTC,
// and the widget is rendered into a transparent QImage so unpainted pixels are
// deterministic. The three timing tests are the only ones that wait for the
// real one-second timer; everything else drives the face through
// setFrozenTime() so the assertions never depend on the wall clock.
//
// The frozen instant is an instant, not a wall-clock reading: changing the
// time zone re-expresses it in the new zone, which is why the zone tests can
// move the same frozen instant across a date boundary without touching the
// frozen value.

#include <QApplication>
#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QImage>
#include <QLocale>
#include <QPainter>
#include <QPoint>
#include <QRectF>
#include <QRegion>
#include <QSignalSpy>
#include <QSize>
#include <QSizeF>
#include <QTest>
#include <QTime>
#include <QTimeZone>
#include <QWidget>

#include <md/components/clock.h>
#include <md/core/density.h>
#include <md/core/measure_spec.h>
#include <md/theme/theme.h>
#include <md/tokens/color_scheme.h>

#include <stdexcept>

namespace {

// The theme every test paints from. initTestCase pins it, so no assertion
// depends on the platform's default application theme.
const md::ColorScheme& colours()
{
    return md::Theme::applicationTheme().colours();
}

// One deterministic instant. The QTimeZone overload keeps the value in UTC
// regardless of the machine's local zone.
QDateTime utc(int year, int month, int day, int hour, int minute, int second = 0)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute, second),
                     QTimeZone(QTimeZone::UTC));
}

// Stops the clock and pins both its zone and its instant, which is the state
// every pixel and text assertion uses.
void freeze(md::Clock& clock, const QDateTime& instant)
{
    clock.setTimeZone(QTimeZone(QTimeZone::UTC));
    clock.setRunning(false);
    clock.setFrozenTime(instant);
}

// Renders a widget into an explicitly transparent image. QWidget::render()
// would otherwise paint the palette's window background first, and unpainted
// pixels would be uninitialised, so every pixel assertion renders over a
// cleared image with DrawChildren only.
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

QImage renderClock(md::Clock& clock, int side)
{
    clock.resize(QSize(side, side));
    return renderWidget(clock);
}

QColor pixelAt(const QImage& image, const QPointF& logical)
{
    const qreal ratio = image.devicePixelRatio();
    const int x = qBound(0, qRound(logical.x() * ratio), image.width() - 1);
    const int y = qBound(0, qRound(logical.y() * ratio), image.height() - 1);
    return image.pixelColor(x, y);
}

// A clock whose paint hook throws before Clock::paintContent() runs. View's
// guard must catch it and leave the widget paintable.
class ThrowingPaintClock : public md::Clock
{
protected:
    void paintContent(QPainter&, const QRectF&) override
    {
        throw std::runtime_error("clock paint boom");
    }
};

// A clock whose measure hook throws; View::measure() must catch it as well.
class ThrowingMeasureClock : public md::Clock
{
protected:
    void measureOverride(md::MeasureSpec, md::MeasureSpec) override
    {
        throw std::runtime_error("clock measure boom");
    }
};

} // namespace

class TestClock : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void defaultsMatchTheSpecification();
    void measurementIsSquareAndFillsTheBox();
    void smallDialFallsBackToDigital();
    void frozenTimeIsDeterministic();
    void timeZoneReexpressesTheFrozenInstant();
    void digitalTextFollowsTheFormat();
    void setDisplayChangesPixels();
    void analogHandsDifferBetweenFrozenTimes();
    void stoppedClockDoesNotTick();
    void hiddenClockDoesNotTick();
    void runningClockTicksAfterOneSecond();
    void throwingOverrideDoesNotCrash();
    void throwingTickObserverDoesNotCrash();
};

void TestClock::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestClock::defaultsMatchTheSpecification()
{
    md::Clock clock;

    QVERIFY(clock.display() == md::Clock::Display::Analog);
    QVERIFY(clock.uses24Hour());
    QVERIFY(!clock.showsSeconds());
    QVERIFY(!clock.showsDate());
    QVERIFY(clock.isRunning());
    QVERIFY(clock.timeZone().isValid());
    QVERIFY(clock.time().isValid());
    QCOMPARE(clock.accessibleName(), QStringLiteral("Clock"));
    QVERIFY(!clock.accessibleDescription().isEmpty());

    // The display property round-trips.
    clock.setDisplay(md::Clock::Display::Both);
    QVERIFY(clock.display() == md::Clock::Display::Both);
    clock.setDisplay(md::Clock::Display::Digital);
    QVERIFY(clock.display() == md::Clock::Display::Digital);
}

void TestClock::measurementIsSquareAndFillsTheBox()
{
    md::Clock clock;
    const md::Density& density = md::Theme::applicationTheme().density();
    const qreal preferred = density.dp(200.0);

    // Unspecified on both axes: the component picks its own 200 dp square.
    clock.measure(md::MeasureSpec::unspecified(), md::MeasureSpec::unspecified());
    QCOMPARE(clock.measuredSize(), QSizeF(preferred, preferred));
    QCOMPARE(clock.sizeHint(), QSize(qRound(preferred), qRound(preferred)));

    // The shorter available axis wins, so an AtMost box stays square.
    clock.measure(md::MeasureSpec::atMost(120.0), md::MeasureSpec::unspecified());
    QCOMPARE(clock.measuredSize(), QSizeF(120.0, 120.0));

    // An Exactly axis stays authoritative even when the two axes disagree.
    clock.measure(md::MeasureSpec::exactly(300.0), md::MeasureSpec::atMost(500.0));
    QCOMPARE(clock.measuredSize(), QSizeF(300.0, 300.0));
}

void TestClock::smallDialFallsBackToDigital()
{
    // A dial inset by 8 dp on both sides cannot keep its 96 dp minimum in an
    // 80 dp box, so the analog face degrades to the digital read-out. The two
    // widgets must therefore paint identically.
    md::Clock analog;
    freeze(analog, utc(2024, 1, 1, 15, 30));
    analog.setDisplay(md::Clock::Display::Analog);
    analog.resize(80, 80);

    md::Clock digital;
    freeze(digital, utc(2024, 1, 1, 15, 30));
    digital.setDisplay(md::Clock::Display::Digital);
    digital.resize(80, 80);

    const QImage analogImage = renderWidget(analog);
    const QImage digitalImage = renderWidget(digital);
    QVERIFY(!analogImage.isNull());
    QVERIFY(analogImage == digitalImage);
}

void TestClock::frozenTimeIsDeterministic()
{
    md::Clock clock;
    freeze(clock, utc(2024, 6, 15, 10, 20, 30));

    QVERIFY(!clock.isRunning());
    QCOMPARE(clock.time(), QTime(10, 20, 30));
    QCOMPARE(clock.date(), QDate(2024, 6, 15));

    const QImage first = renderClock(clock, 240);

    // Waiting crosses no tick because the clock is stopped, and the face must
    // rasterise to exactly the same pixels again.
    QTest::qWait(20);
    QCOMPARE(clock.time(), QTime(10, 20, 30));
    QCOMPARE(clock.date(), QDate(2024, 6, 15));

    const QImage second = renderClock(clock, 240);
    QVERIFY(first == second);

    // setFrozenTime() replaces the instant even while the clock is stopped.
    clock.setFrozenTime(utc(2024, 6, 15, 23, 59, 59));
    QCOMPARE(clock.time(), QTime(23, 59, 59));
    QCOMPARE(clock.date(), QDate(2024, 6, 15));
}

void TestClock::timeZoneReexpressesTheFrozenInstant()
{
    md::Clock clock;
    clock.setRunning(false);
    clock.setTimeZone(QTimeZone(QTimeZone::UTC));
    clock.setFrozenTime(utc(2024, 1, 1, 0, 30));

    QCOMPARE(clock.time(), QTime(0, 30));
    QCOMPARE(clock.date(), QDate(2024, 1, 1));

    // The same instant in a +05:30 zone is still 2024-01-01, but at 06:00.
    clock.setTimeZone(QTimeZone(5 * 3600 + 30 * 60));
    QCOMPARE(clock.time(), QTime(6, 0));
    QCOMPARE(clock.date(), QDate(2024, 1, 1));

    // A negative offset crosses back over the date boundary.
    clock.setTimeZone(QTimeZone(-2 * 3600));
    QCOMPARE(clock.time(), QTime(22, 30));
    QCOMPARE(clock.date(), QDate(2023, 12, 31));

    // An invalid zone is replaced by a real one instead of being stored.
    clock.setTimeZone(QTimeZone());
    QVERIFY(clock.timeZone().isValid());
}

void TestClock::digitalTextFollowsTheFormat()
{
    md::Clock clock;
    freeze(clock, utc(2024, 1, 1, 15, 30, 7));

    // The accessible description is the current time text.
    QCOMPARE(clock.accessibleDescription(), QStringLiteral("15:30"));

    clock.setShowSeconds(true);
    QCOMPARE(clock.accessibleDescription(), QStringLiteral("15:30:07"));

    // 12-hour text drops the leading zero and appends the locale's meridiem.
    clock.setUse24Hour(false);
    QCOMPARE(clock.accessibleDescription(),
             QStringLiteral("3:30:07 ") + QLocale().pmText());

    clock.setShowSeconds(false);
    QCOMPARE(clock.accessibleDescription(),
             QStringLiteral("3:30 ") + QLocale().pmText());

    // Midnight and noon both read as 12, with the right meridiem.
    freeze(clock, utc(2024, 1, 1, 0, 5));
    QCOMPARE(clock.accessibleDescription(), QStringLiteral("12:05 ") + QLocale().amText());

    freeze(clock, utc(2024, 1, 1, 12, 5));
    QCOMPARE(clock.accessibleDescription(), QStringLiteral("12:05 ") + QLocale().pmText());

    clock.setUse24Hour(true);
    QCOMPARE(clock.accessibleDescription(), QStringLiteral("12:05"));

    freeze(clock, utc(2024, 1, 1, 0, 5));
    QCOMPARE(clock.accessibleDescription(), QStringLiteral("00:05"));
}

void TestClock::setDisplayChangesPixels()
{
    md::Clock clock;
    freeze(clock, utc(2024, 1, 1, 15, 30));
    clock.resize(240, 240);

    clock.setDisplay(md::Clock::Display::Analog);
    const QImage analog = renderWidget(clock);

    clock.setDisplay(md::Clock::Display::Digital);
    const QImage digital = renderWidget(clock);

    clock.setDisplay(md::Clock::Display::Both);
    const QImage both = renderWidget(clock);

    // (60, 80) is inside the dial at every display mode, but away from the
    // hands and the cardinal digits at 15:30. Analog and Both paint the dial
    // face there; Digital leaves the background transparent.
    const QPointF sample(60.0, 80.0);
    QCOMPARE(pixelAt(analog, sample).rgb(), colours().surfaceContainerHighest.rgb());
    QCOMPARE(pixelAt(digital, sample).alpha(), 0);
    QCOMPARE(pixelAt(both, sample).rgb(), colours().surfaceContainerHighest.rgb());

    QVERIFY(analog != digital);
    QVERIFY(both != analog);
    QVERIFY(both != digital);
}

void TestClock::analogHandsDifferBetweenFrozenTimes()
{
    md::Clock clock;
    clock.setDisplay(md::Clock::Display::Analog);

    // At 03:00 the hour hand points right; at 09:00 it points left. (160, 120)
    // lies on the 3 o'clock hand and on the bare dial at 9 o'clock, because the
    // minute and second hands point up in both cases.
    freeze(clock, utc(2024, 1, 1, 3, 0));
    const QImage three = renderClock(clock, 240);

    freeze(clock, utc(2024, 1, 1, 9, 0));
    const QImage nine = renderClock(clock, 240);

    const QPointF sample(160.0, 120.0);
    QCOMPARE(pixelAt(three, sample).rgb(), colours().onSurface.rgb());
    QCOMPARE(pixelAt(nine, sample).rgb(), colours().surfaceContainerHighest.rgb());
    QVERIFY(three != nine);
}

void TestClock::stoppedClockDoesNotTick()
{
    md::Clock clock;
    clock.resize(200, 200);

    // The timer starts when the running clock is shown, then setRunning(false)
    // stops it again.
    clock.show();
    clock.setRunning(false);

    QSignalSpy spy(&clock, &md::Clock::tick);
    QTest::qWait(1100);
    QCOMPARE(spy.count(), 0);
    QVERIFY(!clock.isRunning());

    clock.hide();
}

void TestClock::hiddenClockDoesNotTick()
{
    md::Clock clock;
    clock.resize(200, 200);

    QSignalSpy spy(&clock, &md::Clock::tick);
    clock.show();
    clock.hide();

    QTest::qWait(1100);
    QCOMPARE(spy.count(), 0);
}

void TestClock::runningClockTicksAfterOneSecond()
{
    md::Clock clock;
    clock.resize(200, 200);
    QVERIFY(clock.isRunning());

    QSignalSpy spy(&clock, &md::Clock::tick);
    clock.show();

    // The timer interval is exactly one second.
    QTest::qWait(1100);
    QVERIFY(spy.count() >= 1);

    clock.hide();
}

void TestClock::throwingOverrideDoesNotCrash()
{
    // View guards paintContent(), so a throwing override must not escape into
    // Qt: the widget stays paintable and the failure is recorded.
    ThrowingPaintClock painter;
    painter.setRunning(false);
    painter.resize(240, 240);
    const QImage image = renderWidget(painter);
    QVERIFY(!image.isNull());
    QVERIFY(!painter.lastError().isEmpty());
    QVERIFY(painter.lastError().contains(QStringLiteral("clock paint boom")));

    // The same applies to measureOverride().
    ThrowingMeasureClock measurer;
    measurer.measure(md::MeasureSpec::unspecified(), md::MeasureSpec::unspecified());
    QVERIFY(!measurer.lastError().isEmpty());
    QVERIFY(measurer.lastError().contains(QStringLiteral("clock measure boom")));
}

void TestClock::throwingTickObserverDoesNotCrash()
{
    md::Clock clock;
    clock.resize(200, 200);

    bool called = false;
    QObject::connect(&clock, &md::Clock::tick, &clock, [&called] {
        called = true;
        throw std::runtime_error("tick observer boom");
    });

    // The observer throws from inside tick(); onTick()'s exception boundary has
    // to swallow it. The clock keeps running and the process keeps going.
    clock.show();
    QTest::qWait(1100);
    QTRY_VERIFY(called);
    QVERIFY(clock.lastError().isEmpty());

    clock.hide();
}

QTEST_MAIN(TestClock)
#include "tst_clock.moc"
