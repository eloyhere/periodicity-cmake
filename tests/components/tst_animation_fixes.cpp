// Unit tests for the animation-playback fixes (animation/timer batch).
//
// The cases pin the defects found while auditing the playback paths: timers
// that kept running while a widget was hidden or disabled, reduced-motion
// settings that did not stop a loop, a wheel snap that outlived its column
// layout, a loop transition that travelled the long way round, and a collapse
// reversal that rebuilt its height from the wrong start value. The delayed-run
// fix inside the animator is covered by tests/animation/tst_animation.cpp.
//
// The suite runs offscreen (CTest sets QT_QPA_PLATFORM=offscreen) and uses
// Theme::baselineLight() throughout; the reduced-motion cases install a
// ThemeScope on the widget so a single case can switch the setting.

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QRectF>
#include <QResizeEvent>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <md/components/button.h>
#include <md/components/carousel.h>
#include <md/components/marquee.h>
#include <md/components/progress.h>
#include <md/components/ring.h>
#include <md/components/skeleton.h>
#include <md/components/time_picker.h>
#include <md/components/video.h>
#include <md/layout/collapse.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>
#include <md/view/view.h>

#include <QtMath>

namespace {

md::Theme reducedMotionTheme()
{
    md::Theme theme = md::Theme::baselineLight();
    md::MotionScheme motion = theme.motion();
    motion.reducedMotion = true;
    return theme.withMotion(motion);
}

// A theme scope whose theme is switched *after* installation. The switch posts
// QEvent::ThemeChange to the host, which is the only way to exercise the
// "reduced motion was turned on while the widget was animating" path.
md::ThemeScope* switchToReducedMotion(QWidget* host)
{
    md::ThemeScope* scope = md::ThemeScope::install(host, md::Theme::baselineLight());
    scope->setTheme(reducedMotionTheme());
    return scope;
}

void sendMouse(QWidget& widget, QEvent::Type type, const QPointF& position,
               Qt::MouseButton button, Qt::MouseButtons buttons)
{
    const QPoint global = widget.mapToGlobal(position.toPoint());
    QMouseEvent event(type, position, QPointF(global), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

void dragAt(QWidget& widget, const QPointF& from, const QPointF& to)
{
    sendMouse(widget, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    sendMouse(widget, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
    sendMouse(widget, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
}

// A leaf view with a fixed natural size, used as a Collapse body: the collapse
// height is then a pure function of its expansion, which is what the geometry
// assertions rely on.
class FixedView : public md::View
{
public:
    explicit FixedView(QSizeF size, QWidget* parent = nullptr)
        : View(parent)
        , size_(size)
    {
    }

protected:
    void measureOverride(md::MeasureSpec width, md::MeasureSpec height) override
    {
        setMeasuredSize(QSizeF(width.resolve(size_.width()), height.resolve(size_.height())));
    }

    void paintContent(QPainter&, const QRectF&) override {}

    [[nodiscard]] bool focusable() const override { return false; }

private:
    QSizeF size_;
};

// Records the height of every resize a widget performs, which is how a
// transient height that belongs to neither end of a reversal becomes visible
// without screenshots.
class ResizeRecorder : public QObject
{
public:
    QList<int> heights;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Resize)
            heights.append(static_cast<QResizeEvent*>(event)->size().height());

        return QObject::eventFilter(watched, event);
    }
};

class LayoutRequestCounter : public QObject
{
public:
    int count = 0;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::LayoutRequest)
            ++count;

        return QObject::eventFilter(watched, event);
    }
};

QTimer* childTimer(const QObject& owner, const char* name)
{
    return owner.findChild<QTimer*>(QString::fromLatin1(name));
}

} // namespace

class TestAnimationFixes : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void progressHiddenStopsTheAnimator();
    void progressIndeterminatePhaseAdvances();
    void progressReducedMotionSnapsWithoutTimer();
    void progressStopsWhenReducedMotionTurnsOn();
    void progressAndRingClampTheirFractions();

    void ringReducedMotionSnapsWithoutTimer();

    void skeletonDisabledStopsTheShimmer();
    void skeletonReducedMotionNeverStartsTheShimmer();

    void marqueeZeroSpeedDoesNotSpin();
    void marqueeReducedMotionDoesNotScroll();
    void marqueeTextChangeRestartsAndInvalidatesSize();
    void marqueeResizeRecomputesOverflow();

    void carouselAutoplayStopsWhileHidden();
    void carouselAutoplayStopsAtTheLastSlideWithoutLoop();
    void carouselWrapTransitionTakesOneStep();
    void carouselRapidNextKeepsThePairInPlace();
    void carouselHiddenTransitionIsSettled();
    void carouselReducedMotionSnapsTransition();

    void timePickerReducedMotionSnapsImmediately();
    void timePickerFormatSwitchDropsTheSnap();

    void videoHiddenStopsTheTicker();
    void videoSeekClampsAndPullsTheProvider();

    void collapseReversalStaysWithinItsHeights();
    void collapseReducedMotionSnaps();
    void collapseHiddenSettlesOnTheTarget();
};

void TestAnimationFixes::initTestCase()
{
    // Every case starts from the built-in light theme; the reduced-motion cases
    // replace it through a ThemeScope on the widget under test.
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
    QCOMPARE(md::Theme::applicationTheme(), md::Theme::baselineLight());
}

void TestAnimationFixes::progressHiddenStopsTheAnimator()
{
    md::Progress progress;
    progress.resize(progress.sizeHint());
    progress.setIndeterminate(true);

    // A hidden indicator is not animating, even though it was asked to.
    QVERIFY(!progress.isAnimating());

    progress.show();
    QTRY_VERIFY_WITH_TIMEOUT(progress.isAnimating(), 1000);

    progress.hide();
    QVERIFY(!progress.isAnimating());

    // ... and it stays stopped: a timer that survived hideEvent would restart
    // the loop and report true again.
    QTest::qWait(80);
    QVERIFY(!progress.isAnimating());
}

void TestAnimationFixes::progressIndeterminatePhaseAdvances()
{
    md::Progress progress;
    progress.resize(progress.sizeHint());
    progress.setIndeterminate(true);
    progress.show();
    QTRY_VERIFY_WITH_TIMEOUT(progress.isAnimating(), 1000);

    // The loop is restarted from finished(); if that link broke, the phase
    // would freeze at 0 and the two grabs would be identical.
    const QImage first = progress.grab().toImage();
    QTest::qWait(150);
    const QImage second = progress.grab().toImage();
    QVERIFY(progress.isAnimating());
    QVERIFY(first != second);
}

void TestAnimationFixes::progressReducedMotionSnapsWithoutTimer()
{
    md::Progress progress;
    progress.resize(progress.sizeHint());
    progress.setIndeterminate(true);
    switchToReducedMotion(&progress);

    progress.show();
    QTest::qWait(30);
    QVERIFY(!progress.isAnimating());

    // The static phase does not move: two grabs are identical.
    const QImage first = progress.grab().toImage();
    QTest::qWait(80);
    const QImage second = progress.grab().toImage();
    QCOMPARE(first, second);
}

void TestAnimationFixes::progressStopsWhenReducedMotionTurnsOn()
{
    md::Progress progress;
    progress.resize(progress.sizeHint());
    progress.setIndeterminate(true);
    progress.show();
    QTRY_VERIFY_WITH_TIMEOUT(progress.isAnimating(), 1000);

    // Restart the loop so a full motion token is ahead: then the assertion
    // below cannot pass by waiting for the current cycle to finish.
    progress.setIndeterminate(false);
    progress.setIndeterminate(true);
    QVERIFY(progress.isAnimating());

    // Turning the setting on mid-loop must stop the timer in the same turn as
    // the theme change, not just paint a static phase while the animation
    // keeps ticking until the cycle ends.
    switchToReducedMotion(&progress);
    QCoreApplication::processEvents();
    QVERIFY(!progress.isAnimating());
    QTest::qWait(60);
    QVERIFY(!progress.isAnimating());
}

void TestAnimationFixes::progressAndRingClampTheirFractions()
{
    md::Progress progress;
    progress.setValue(2.0);
    QCOMPARE(progress.value(), 1.0);
    progress.setValue(-0.5);
    QCOMPARE(progress.value(), 0.0);
    progress.setBuffer(3.0);
    QCOMPARE(progress.buffer(), 1.0);
    progress.setBuffer(-2.0);
    QCOMPARE(progress.buffer(), 0.0);

    md::Ring ring;
    ring.setValue(1.5);
    QCOMPARE(ring.value(), 1.0);
    ring.setValue(-1.0);
    QCOMPARE(ring.value(), 0.0);
}

void TestAnimationFixes::ringReducedMotionSnapsWithoutTimer()
{
    md::Ring ring;
    ring.resize(ring.sizeHint());
    ring.setIndeterminate(true);
    switchToReducedMotion(&ring);

    ring.show();
    QTest::qWait(30);
    QVERIFY(!ring.isAnimating());

    const QImage first = ring.grab().toImage();
    QTest::qWait(80);
    const QImage second = ring.grab().toImage();
    QCOMPARE(first, second);

    // Switching back to normal motion resumes the rotation.
    if (md::ThemeScope* scope = md::ThemeScope::find(&ring))
        scope->setTheme(md::Theme::baselineLight());
    QTRY_VERIFY_WITH_TIMEOUT(ring.isAnimating(), 1000);
}

void TestAnimationFixes::skeletonDisabledStopsTheShimmer()
{
    md::Skeleton skeleton;
    skeleton.setSkeletonWidth(120.0);
    skeleton.setSkeletonHeight(12.0);
    skeleton.resize(skeleton.sizeHint());
    skeleton.show();

    skeleton.setAnimating(true);
    QTRY_VERIFY_WITH_TIMEOUT(skeleton.isAnimating(), 1000);

    // A disabled placeholder is a flat fill; the documented contract says it
    // must not keep a 16 ms timer alive.
    skeleton.setEnabled(false);
    QVERIFY(!skeleton.isAnimating());
    QTest::qWait(60);
    QVERIFY(!skeleton.isAnimating());

    skeleton.setEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(skeleton.isAnimating(), 1000);
}

void TestAnimationFixes::skeletonReducedMotionNeverStartsTheShimmer()
{
    md::Skeleton skeleton;
    skeleton.setSkeletonWidth(120.0);
    skeleton.setSkeletonHeight(12.0);
    skeleton.resize(skeleton.sizeHint());
    switchToReducedMotion(&skeleton);
    skeleton.show();

    skeleton.setAnimating(true);
    QTest::qWait(50);
    QVERIFY(!skeleton.isAnimating());

    // And turning reduced motion on while the shimmer runs stops it.
    if (md::ThemeScope* scope = md::ThemeScope::find(&skeleton))
        scope->setTheme(md::Theme::baselineLight());
    QTRY_VERIFY_WITH_TIMEOUT(skeleton.isAnimating(), 1000);
    switchToReducedMotion(&skeleton);
    QTRY_VERIFY_WITH_TIMEOUT(!skeleton.isAnimating(), 1000);
}

void TestAnimationFixes::marqueeZeroSpeedDoesNotSpin()
{
    md::Marquee marquee;
    marquee.setText(QStringLiteral("A line long enough that it cannot fit the viewport"));
    marquee.setPauseOnHover(false);
    marquee.resize(120, 24);
    marquee.show();

    marquee.setSpeed(0.0);
    marquee.start();

    // No motion means no frame timer, not a timer that wakes up to add zero.
    QVERIFY(!marquee.isRunning());
    QTest::qWait(80);
    QCOMPARE(marquee.scrollOffset(), 0.0);
}

void TestAnimationFixes::marqueeReducedMotionDoesNotScroll()
{
    md::Marquee marquee;
    marquee.setText(QStringLiteral("A line long enough that it cannot fit the viewport"));
    marquee.setPauseOnHover(false);
    marquee.resize(120, 24);
    switchToReducedMotion(&marquee);
    marquee.show();
    QTest::qWait(20);

    marquee.start();
    QVERIFY(!marquee.isRunning());
    QTest::qWait(80);
    QCOMPARE(marquee.scrollOffset(), 0.0);
}

void TestAnimationFixes::marqueeTextChangeRestartsAndInvalidatesSize()
{
    // A short label in a horizontal layout: the marquee is given its preferred
    // width, so a text change must re-run the layout to a wider box. That is
    // the observable half of updateGeometry().
    QWidget host;
    auto* layout = new QHBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);

    md::Marquee marquee;
    marquee.setPauseOnHover(false);
    marquee.setText(QStringLiteral("Hi"));
    layout->addWidget(&marquee);
    layout->addStretch(1);
    host.resize(600, 40);
    host.show();
    QTest::qWait(30);

    const int shortWidth = marquee.width();
    QVERIFY(shortWidth > 0);

    // A layout only re-queries a child's size hint after the child told it that
    // its preferred size changed; the notification lands on the host widget.
    LayoutRequestCounter counter;
    host.installEventFilter(&counter);

    marquee.setText(QStringLiteral("Another line that is also far too wide for the cell"));

    QTRY_VERIFY_WITH_TIMEOUT(counter.count > 0, 1000);
    host.removeEventFilter(&counter);
    QVERIFY2(marquee.width() > shortWidth,
             qPrintable(QStringLiteral("width %1 after text change, was %2")
                            .arg(marquee.width())
                            .arg(shortWidth)));
    QCOMPARE(marquee.scrollOffset(), 0.0);

    // And a fixed-size marquee restarts from the leading edge on a text change.
    md::Marquee ticker;
    ticker.setText(QStringLiteral("A line long enough that it cannot fit the viewport"));
    ticker.setPauseOnHover(false);
    ticker.resize(120, 24);
    ticker.show();
    ticker.start();
    QTRY_VERIFY_WITH_TIMEOUT(ticker.scrollOffset() > 0.0, 1000);

    ticker.setText(QStringLiteral("A completely different line that is also too wide"));
    QCOMPARE(ticker.scrollOffset(), 0.0);
    QVERIFY(ticker.isRunning());
}

void TestAnimationFixes::marqueeResizeRecomputesOverflow()
{
    md::Marquee marquee;
    marquee.setText(QStringLiteral("A line long enough that it cannot fit the viewport"));
    marquee.setPauseOnHover(false);
    marquee.resize(120, 24);
    marquee.show();

    marquee.start();
    QTRY_VERIFY_WITH_TIMEOUT(marquee.scrollOffset() > 0.0, 1000);
    QVERIFY(marquee.isRunning());

    // Widen until the text fits: the timer stops.
    marquee.resize(qCeil(marquee.contentWidth()) + 24, 24);
    QVERIFY(!marquee.isOverflowing());
    QVERIFY(!marquee.isRunning());

    // Narrow it again: the overflow decision is re-made and the scroll position
    // restarts at the leading edge instead of resuming the old cycle.
    marquee.resize(120, 24);
    QVERIFY(marquee.isOverflowing());
    QVERIFY(marquee.isRunning());
    QCOMPARE(marquee.scrollOffset(), 0.0);
}

void TestAnimationFixes::carouselAutoplayStopsWhileHidden()
{
    md::Carousel carousel;
    carousel.resize(300, 200);
    carousel.setDuration(0);
    carousel.show();

    carousel.addSlide(new md::Button(&carousel));
    carousel.addSlide(new md::Button(&carousel));
    carousel.addSlide(new md::Button(&carousel));

    carousel.setInterval(40);
    QSignalSpy changedSpy(&carousel, &md::Carousel::currentChanged);
    carousel.setAutoPlay(true);

    QTimer* autoTimer = childTimer(carousel, "md.components.Carousel.autoPlay");
    QVERIFY(autoTimer != nullptr);
    QVERIFY(autoTimer->isActive());
    QTRY_VERIFY_WITH_TIMEOUT(changedSpy.count() >= 1, 2000);

    carousel.hide();
    QVERIFY(!autoTimer->isActive());

    const int settled = changedSpy.count();
    QTest::qWait(150);
    QCOMPARE(changedSpy.count(), settled);

    // Showing it again resumes auto play.
    carousel.show();
    QTRY_VERIFY_WITH_TIMEOUT(autoTimer->isActive(), 1000);
    carousel.setAutoPlay(false);
}

void TestAnimationFixes::carouselAutoplayStopsAtTheLastSlideWithoutLoop()
{
    md::Carousel carousel;
    carousel.resize(300, 200);
    carousel.setDuration(0);
    carousel.setLoop(false);
    carousel.show();

    carousel.addSlide(new md::Button(&carousel));
    carousel.addSlide(new md::Button(&carousel));
    carousel.addSlide(new md::Button(&carousel));

    carousel.setInterval(30);
    QTimer* autoTimer = childTimer(carousel, "md.components.Carousel.autoPlay");
    QVERIFY(autoTimer != nullptr);

    carousel.setAutoPlay(true);
    QVERIFY(autoTimer->isActive());

    // Auto play has nowhere to go once it reaches the last slide of a
    // non-looping carousel, so the timer stops instead of firing forever.
    QTRY_COMPARE_WITH_TIMEOUT(carousel.currentIndex(), 2, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!autoTimer->isActive(), 1000);

    // Re-enabling loop gives it somewhere to go again.
    carousel.setLoop(true);
    QVERIFY(autoTimer->isActive());
    carousel.setAutoPlay(false);
}

void TestAnimationFixes::carouselWrapTransitionTakesOneStep()
{
    md::Carousel carousel;
    carousel.resize(300, 300);
    carousel.setDuration(300);
    carousel.addSlide(new md::Button(&carousel));
    carousel.addSlide(new md::Button(&carousel));
    carousel.addSlide(new md::Button(&carousel));

    carousel.setCurrentIndex(2);
    carousel.finishTransition();
    QCOMPARE(carousel.currentIndex(), 2);

    const QRectF area = carousel.slideArea();
    const int width = qRound(area.width());
    const QList<md::View*> slides = carousel.slides();
    QCOMPARE(slides.at(2)->geometry().left(), qRound(area.left()));

    // 2 -> 0 wraps forward by one slide, so the incoming slide starts exactly
    // one viewport to the right of the outgoing one. It used to start two
    // viewports to the left, sweeping the whole set in 300 ms.
    carousel.next();
    QCOMPARE(carousel.currentIndex(), 0);
    QCOMPARE(carousel.transitionProgress(), 0.0);
    QCOMPARE(slides.at(0)->geometry().left(), qRound(area.left()) + width);
    QCOMPARE(slides.at(2)->geometry().left(), qRound(area.left()));

    carousel.finishTransition();
    QCOMPARE(slides.at(0)->geometry().left(), qRound(area.left()));
    QCOMPARE(carousel.transitionProgress(), 1.0);
}

void TestAnimationFixes::carouselRapidNextKeepsThePairInPlace()
{
    md::Carousel carousel;
    carousel.resize(300, 300);
    carousel.setDuration(300);
    carousel.addSlide(new md::Button(&carousel));
    carousel.addSlide(new md::Button(&carousel));
    carousel.addSlide(new md::Button(&carousel));

    const QRectF area = carousel.slideArea();
    const int width = qRound(area.width());
    const QList<md::View*> slides = carousel.slides();

    carousel.setCurrentIndex(1);
    QTest::qWait(80);
    QVERIFY(carousel.transitionProgress() > 0.0);
    QVERIFY(carousel.transitionProgress() < 1.0);

    // A second request during the transition retargets; the visible pair has to
    // stay one step apart, not span the whole set.
    carousel.next();
    QCOMPARE(carousel.currentIndex(), 2);
    QVERIFY(carousel.transitionProgress() >= 0.0);
    QVERIFY(carousel.transitionProgress() <= 1.0);
    QCOMPARE(slides.at(1)->geometry().left(), qRound(area.left()));
    QCOMPARE(slides.at(2)->geometry().left(), qRound(area.left()) + width);

    carousel.previous();
    QCOMPARE(carousel.currentIndex(), 1);
    QVERIFY(carousel.transitionProgress() >= 0.0);
    QVERIFY(carousel.transitionProgress() <= 1.0);
    QCOMPARE(slides.at(2)->geometry().left(), qRound(area.left()));
    QCOMPARE(slides.at(1)->geometry().left(), qRound(area.left()) - width);

    carousel.finishTransition();
    QCOMPARE(carousel.currentIndex(), 1);
    QVERIFY(!carousel.isAnimating());
    QCOMPARE(carousel.transitionProgress(), 1.0);
    QCOMPARE(slides.at(1)->geometry().left(), qRound(area.left()));
}

void TestAnimationFixes::carouselHiddenTransitionIsSettled()
{
    md::Carousel carousel;
    carousel.resize(300, 300);
    carousel.setDuration(300);
    carousel.addSlide(new md::Button(&carousel));
    carousel.addSlide(new md::Button(&carousel));
    carousel.show();
    QTest::qWait(20);

    carousel.setCurrentIndex(1);
    QVERIFY(carousel.isAnimating());

    // Hiding the carousel settles the transition: no timer keeps arranging
    // off-screen slides after the widget is gone.
    carousel.hide();
    QVERIFY(!carousel.isAnimating());
    QCOMPARE(carousel.transitionProgress(), 1.0);

    QTest::qWait(120);
    QVERIFY(!carousel.isAnimating());
    QCOMPARE(carousel.currentIndex(), 1);
}

void TestAnimationFixes::carouselReducedMotionSnapsTransition()
{
    md::Carousel carousel;
    carousel.resize(300, 300);
    carousel.setDuration(500);
    carousel.addSlide(new md::Button(&carousel));
    carousel.addSlide(new md::Button(&carousel));
    carousel.addSlide(new md::Button(&carousel));
    switchToReducedMotion(&carousel);
    carousel.show();
    QTest::qWait(20);

    // A reduced-motion theme resolves the transition to zero duration, so the
    // new slide is in place before setCurrentIndex() returns.
    carousel.setCurrentIndex(1);
    QCOMPARE(carousel.currentIndex(), 1);
    QCOMPARE(carousel.transitionProgress(), 1.0);
    QVERIFY(!carousel.isAnimating());
}

void TestAnimationFixes::timePickerReducedMotionSnapsImmediately()
{
    md::TimePicker picker;
    picker.resize(256, 200);
    picker.setTime(QTime(9, 30, 15));
    switchToReducedMotion(&picker);
    picker.show();
    QTest::qWait(20);

    const int hourColumn = picker.fieldColumn(md::TimePicker::Field::Hour);
    const QPointF start = picker.columnRect(hourColumn).center();
    dragAt(picker, start, start - QPointF(0.0, 60.0));

    // The wheel lands on the snapped row at once: no chase timer, and the
    // offset is a whole row rather than a fraction of one.
    QVERIFY(!picker.isAnimating());
    const qreal offset = picker.columnOffset(hourColumn);
    QCOMPARE(offset, qreal(qRound(offset)));
    QCOMPARE(picker.columnText(hourColumn), QStringLiteral("10"));
    QCOMPARE(picker.time(), QTime(10, 30, 15));
}

void TestAnimationFixes::timePickerFormatSwitchDropsTheSnap()
{
    md::TimePicker picker;
    picker.resize(256, 200);
    picker.setTime(QTime(9, 30, 15));
    picker.show();
    QTest::qWait(20);

    const int hourColumn = picker.fieldColumn(md::TimePicker::Field::Hour);
    const QPointF start = picker.columnRect(hourColumn).center();
    dragAt(picker, start, start - QPointF(0.0, 60.0));
    QVERIFY(picker.isAnimating());

    // The 12-hour layout has a different column count and hour range; the
    // in-flight snap belongs to the old layout and must be dropped, not left
    // chasing a target in the replaced column state.
    picker.setHourFormat(12);
    QVERIFY(!picker.isAnimating());

    const int meridianColumn = picker.fieldColumn(md::TimePicker::Field::Meridian);
    QVERIFY(meridianColumn >= 0);
    QCOMPARE(picker.columnText(hourColumn), QStringLiteral("10"));
    QCOMPARE(picker.columnText(meridianColumn), QStringLiteral("AM"));
    QCOMPARE(picker.time(), QTime(10, 30, 15));

    for (int column = 0; column < picker.columnCount(); ++column) {
        const qreal offset = picker.columnOffset(column);
        QCOMPARE(offset, qreal(qRound(offset)));
    }
}

void TestAnimationFixes::videoHiddenStopsTheTicker()
{
    md::Video video;
    video.resize(320, 180);
    video.setDuration(60000);
    video.show();
    QTest::qWait(20);

    QTimer* ticker = childTimer(video, "md.components.Video.ticker");
    QVERIFY(ticker != nullptr);

    video.play();
    QTRY_VERIFY_WITH_TIMEOUT(video.position() > 0, 2000);
    QVERIFY(ticker->isActive());

    video.hide();
    QVERIFY(!ticker->isActive());

    const qint64 frozen = video.position();
    QTest::qWait(120);
    QCOMPARE(video.position(), frozen);

    // The playback state did not change while hidden, so showing the surface
    // resumes from where it stopped.
    video.show();
    QTRY_VERIFY_WITH_TIMEOUT(video.position() > frozen, 2000);
    video.pause();
    QVERIFY(!ticker->isActive());
}

void TestAnimationFixes::videoSeekClampsAndPullsTheProvider()
{
    md::Video video;
    video.resize(320, 180);
    video.setDuration(1000);

    QList<qint64> pulls;
    video.setFrameProvider([&pulls](qint64 position) {
        pulls.append(position);
        return QImage(1, 1, QImage::Format_ARGB32);
    });
    QVERIFY(!pulls.isEmpty());

    // A seek beyond the media is clamped, and the provider is asked for the
    // frame at the *clamped* position, not at the requested one.
    pulls.clear();
    video.setPosition(1500);
    QCOMPARE(video.position(), 1000);
    QVERIFY(!pulls.isEmpty());
    QCOMPARE(pulls.last(), qint64(1000));

    pulls.clear();
    video.setPosition(-10);
    QCOMPARE(video.position(), 0);
    QVERIFY(!pulls.isEmpty());
    QCOMPARE(pulls.last(), qint64(0));
}

void TestAnimationFixes::collapseReversalStaysWithinItsHeights()
{
    md::Collapse section;
    section.setTitle(QStringLiteral("Details"));
    section.setHeaderHeight(48.0);

    FixedView body(QSizeF(120.0, 100.0));
    section.setContentWidget(&body);
    section.resize(200, 48);
    section.measure(md::MeasureSpec::exactly(200.0), md::MeasureSpec::exactly(48.0));
    section.arrange(QRect(0, 0, 200, 48));
    QCOMPARE(section.headerRect().height(), 48.0);

    // The resize events that make a transient height observable only arrive
    // while the widget is visible.
    section.show();
    QTest::qWait(20);

    section.setExpanded(true);
    QTest::qWait(80);

    const qreal before = section.expansion();
    QVERIFY2(before > 0.05 && before < 0.95,
             qPrintable(QStringLiteral("expansion at reversal: %1").arg(before)));
    const int heightBefore = section.height();
    QVERIFY(heightBefore > 48);

    ResizeRecorder recorder;
    section.installEventFilter(&recorder);
    section.setExpanded(false);
    QTRY_COMPARE_WITH_TIMEOUT(section.expansion(), 0.0, 2000);
    section.removeEventFilter(&recorder);

    QVERIFY(!recorder.heights.isEmpty());
    for (int index = 0; index < recorder.heights.size(); ++index) {
        const int height = recorder.heights.at(index);
        // No intermediate height may lie outside the segment between the two
        // endpoints: that is what a reversal rebuilt from the wrong start value
        // used to do (it shrank to the target, then jumped back up).
        QVERIFY2(height >= 48 && height <= heightBefore,
                 qPrintable(QStringLiteral("resize %1 at index %2 outside [48, %3]")
                                .arg(height)
                                .arg(index)
                                .arg(heightBefore)));
        if (index > 0) {
            QVERIFY2(recorder.heights.at(index) <= recorder.heights.at(index - 1),
                     qPrintable(QStringLiteral("height went back up at index %1").arg(index)));
        }
    }

    QCOMPARE(section.height(), 48);
    QCOMPARE(section.preferredHeight(), 48.0);
    QVERIFY(!section.isExpanded());
    QVERIFY(body.isHidden());
}

void TestAnimationFixes::collapseReducedMotionSnaps()
{
    md::Collapse section;
    section.setTitle(QStringLiteral("Details"));
    section.setHeaderHeight(48.0);

    FixedView body(QSizeF(120.0, 100.0));
    section.setContentWidget(&body);
    section.resize(200, 48);
    section.measure(md::MeasureSpec::exactly(200.0), md::MeasureSpec::exactly(48.0));
    section.arrange(QRect(0, 0, 200, 48));

    switchToReducedMotion(&section);

    QSignalSpy finishedSpy(&section, &md::Collapse::animationFinished);
    section.setExpanded(true);

    // Zero-duration spec: the target is reached inside the call.
    QCOMPARE(section.expansion(), 1.0);
    QCOMPARE(section.preferredHeight(), 148.0);
    QCOMPARE(finishedSpy.count(), 1);

    section.setExpanded(false);
    QCOMPARE(section.expansion(), 0.0);
    QCOMPARE(section.preferredHeight(), 48.0);
    QCOMPARE(finishedSpy.count(), 2);
}

void TestAnimationFixes::collapseHiddenSettlesOnTheTarget()
{
    md::Collapse section;
    section.setTitle(QStringLiteral("Details"));
    section.setHeaderHeight(48.0);

    FixedView body(QSizeF(120.0, 100.0));
    section.setContentWidget(&body);
    section.resize(200, 48);
    section.show();
    QTest::qWait(20);

    section.setExpanded(true);
    QTest::qWait(60);
    QVERIFY(section.expansion() > 0.0);
    QVERIFY(section.expansion() < 1.0);

    // Hiding mid-animation settles on the target instead of leaving the curve
    // running against an invisible widget.
    section.hide();
    QCOMPARE(section.expansion(), 1.0);
    QTest::qWait(120);
    QCOMPARE(section.expansion(), 1.0);
    QVERIFY(section.isExpanded());
}

QTEST_MAIN(TestAnimationFixes)
#include "tst_animation_fixes.moc"
