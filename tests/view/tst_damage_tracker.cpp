// Unit tests for damage tracking (L4): md::DamageTracker and the opt-in
// damage-aware repaint path on md::View.
//
// The tracker tests are pure value-type tests. The View tests drive a small
// subclass that records what paintContent() sees through currentDamage();
// every paint they assert on is triggered by grab(), so the assertions do not
// depend on how the offscreen platform schedules update events.

#include <QApplication>
#include <QImage>
#include <QList>
#include <QPainter>
#include <QRectF>
#include <QSizeF>
#include <QTest>
#include <QtGlobal>

#include <md/core/measure_spec.h>
#include <md/view/damage_tracker.h>
#include <md/view/view.h>

#include <limits>
#include <stdexcept>

namespace {

constexpr qreal kViewWidth = 200.0;
constexpr qreal kViewHeight = 100.0;

// A View that opts into damage tracking and records what paintContent() saw on
// the pass it is currently painting.
class TrackingView : public md::View
{
public:
    explicit TrackingView(bool tracking = true)
    {
        if (tracking)
            setDamageTrackingEnabled(true);
    }

    // The no-argument overload is protected on View; expose it for the test.
    void requestFullRepaint() { requestUpdate(); }

    void resetObservation()
    {
        sawDamage_ = false;
        lastPaintSawDamage_ = false;
        seenRectCount_ = 0;
        seenRect_ = QRectF();
    }

    [[nodiscard]] int paintCount() const noexcept { return paintCount_; }
    [[nodiscard]] bool sawDamage() const noexcept { return sawDamage_; }
    [[nodiscard]] bool lastPaintSawDamage() const noexcept
    {
        return lastPaintSawDamage_;
    }
    [[nodiscard]] int seenRectCount() const noexcept { return seenRectCount_; }
    [[nodiscard]] QRectF seenRect() const noexcept { return seenRect_; }

protected:
    void measureOverride(md::MeasureSpec width, md::MeasureSpec height) override
    {
        setMeasuredSize(QSizeF(width.resolve(kViewWidth),
                               height.resolve(kViewHeight)));
    }

    void paintContent(QPainter& painter, const QRectF& content) override
    {
        Q_UNUSED(painter);
        Q_UNUSED(content);

        ++paintCount_;

        const md::DamageTracker& current = currentDamage();
        lastPaintSawDamage_ = !current.isEmpty();

        if (!current.isEmpty()) {
            sawDamage_ = true;
            const QList<QRectF> rects = current.rects();
            seenRectCount_ = static_cast<int>(rects.size());
            seenRect_ = rects.first();
        }
    }

private:
    int paintCount_ = 0;
    bool sawDamage_ = false;
    bool lastPaintSawDamage_ = false;
    int seenRectCount_ = 0;
    QRectF seenRect_;
};

// A tracking view whose content hook can throw once. The base class catches
// the exception at its guarded paint entry point; the damage record must
// survive the failed pass.
class ThrowingTrackingView : public TrackingView
{
public:
    ThrowingTrackingView() = default;

    void throwOnNextPaint() { throws_ = true; }

protected:
    void paintContent(QPainter& painter, const QRectF& content) override
    {
        if (throws_) {
            throws_ = false;
            throw std::runtime_error("damage paint boom");
        }

        TrackingView::paintContent(painter, content);
    }

private:
    bool throws_ = false;
};

// Sizes the widget and lets the initial show/paint settle, then forces one
// deterministic full paint so the tracker starts empty.
void showAndSettle(QWidget& widget)
{
    widget.resize(static_cast<int>(kViewWidth), static_cast<int>(kViewHeight));
    widget.show();
    QTest::qWait(0);
    widget.grab();
}

} // namespace

class TestDamageTracker : public QObject
{
    Q_OBJECT

private slots:
    void singleRectIsReported();
    void overlappingRectsCoalesce();
    void touchingRectsCoalesce();
    void disjointRectsStaySeparate();
    void capCollapsesToBoundingRect();
    void areaThresholdCollapsesToFullRect();
    void addAllMarksTheFullRect();
    void clearEmptiesTheTracker();
    void emptyAndNonFiniteRectsAreIgnored();

    void viewRecordsDamageForPaintContent();
    void throwingPaintKeepsTheDamage();
    void disabledTrackingRetainsNothing();
    void noArgumentRequestUpdateMarksFullRect();
};

void TestDamageTracker::singleRectIsReported()
{
    md::DamageTracker tracker;
    tracker.setFullRect(QRectF(0.0, 0.0, kViewWidth, kViewHeight));

    QVERIFY(tracker.isEmpty());
    QVERIFY(!tracker.isFull());

    tracker.add(QRectF(10.0, 20.0, 30.0, 40.0));

    QVERIFY(!tracker.isEmpty());
    QCOMPARE(tracker.rects().size(), qsizetype(1));
    QCOMPARE(tracker.rects().at(0), QRectF(10.0, 20.0, 30.0, 40.0));
    QCOMPARE(tracker.bounds(), QRectF(10.0, 20.0, 30.0, 40.0));
    QCOMPARE(tracker.fullRect(), QRectF(0.0, 0.0, kViewWidth, kViewHeight));
    QVERIFY(!tracker.isFull());
}

void TestDamageTracker::overlappingRectsCoalesce()
{
    md::DamageTracker tracker;
    tracker.setFullRect(QRectF(0.0, 0.0, kViewWidth, kViewHeight));

    tracker.add(QRectF(0.0, 0.0, 20.0, 20.0));
    tracker.add(QRectF(10.0, 10.0, 20.0, 20.0));

    QCOMPARE(tracker.rects().size(), qsizetype(1));
    QCOMPARE(tracker.rects().at(0), QRectF(0.0, 0.0, 30.0, 30.0));
    QCOMPARE(tracker.bounds(), QRectF(0.0, 0.0, 30.0, 30.0));
}

void TestDamageTracker::touchingRectsCoalesce()
{
    md::DamageTracker tracker;
    tracker.setFullRect(QRectF(0.0, 0.0, kViewWidth, kViewHeight));

    // They share an edge but do not overlap: a caret and the glyph run beside
    // it are one damaged strip, not two.
    tracker.add(QRectF(0.0, 0.0, 10.0, 10.0));
    tracker.add(QRectF(10.0, 0.0, 10.0, 10.0));

    QCOMPARE(tracker.rects().size(), qsizetype(1));
    QCOMPARE(tracker.rects().at(0), QRectF(0.0, 0.0, 20.0, 10.0));
}

void TestDamageTracker::disjointRectsStaySeparate()
{
    md::DamageTracker tracker;
    tracker.setFullRect(QRectF(0.0, 0.0, kViewWidth, kViewHeight));

    tracker.add(QRectF(0.0, 0.0, 10.0, 10.0));
    tracker.add(QRectF(50.0, 50.0, 10.0, 10.0));

    QCOMPARE(tracker.rects().size(), qsizetype(2));
    QCOMPARE(tracker.rects().at(0), QRectF(0.0, 0.0, 10.0, 10.0));
    QCOMPARE(tracker.rects().at(1), QRectF(50.0, 50.0, 10.0, 10.0));
    QCOMPARE(tracker.bounds(), QRectF(0.0, 0.0, 60.0, 60.0));
}

void TestDamageTracker::capCollapsesToBoundingRect()
{
    // No fullRect: the area threshold is inert, so this isolates the cap.
    md::DamageTracker tracker;

    const int count = md::DamageTracker::kMaxRects + 1;
    for (int index = 0; index < count; ++index)
        tracker.add(QRectF(4.0 * index, 3.0 * index, 2.0, 2.0));

    const QRectF expected(0.0, 0.0, 4.0 * (count - 1) + 2.0,
                          3.0 * (count - 1) + 2.0);
    QCOMPARE(tracker.rects().size(), qsizetype(1));
    QCOMPARE(tracker.rects().at(0), expected);

    // More scattered updates still cannot grow the list past the cap.
    for (int index = 0; index < 100; ++index)
        tracker.add(QRectF(1000.0 + 10.0 * index, 1000.0, 4.0, 4.0));

    QVERIFY(tracker.rects().size() <= qsizetype(md::DamageTracker::kMaxRects));
    QVERIFY(!tracker.isEmpty());
}

void TestDamageTracker::areaThresholdCollapsesToFullRect()
{
    md::DamageTracker tracker;
    tracker.setFullRect(QRectF(0.0, 0.0, 100.0, 100.0));

    // 50 x 50 is a quarter of the bounds: still described exactly.
    tracker.add(QRectF(0.0, 0.0, 50.0, 50.0));
    QCOMPARE(tracker.rects().size(), qsizetype(1));
    QCOMPARE(tracker.rects().at(0), QRectF(0.0, 0.0, 50.0, 50.0));
    QVERIFY(!tracker.isFull());

    // 80 x 80 is 64 %: above the 60 % threshold, so it collapses to the whole
    // reference rectangle instead of retaining the exact region.
    tracker.add(QRectF(0.0, 0.0, 80.0, 80.0));
    QCOMPARE(tracker.rects().size(), qsizetype(1));
    QCOMPARE(tracker.rects().at(0), QRectF(0.0, 0.0, 100.0, 100.0));
    QVERIFY(tracker.isFull());
}

void TestDamageTracker::addAllMarksTheFullRect()
{
    md::DamageTracker tracker;
    tracker.setFullRect(QRectF(0.0, 0.0, kViewWidth, kViewHeight));
    tracker.add(QRectF(10.0, 10.0, 20.0, 20.0));

    tracker.addAll();

    QVERIFY(tracker.isFull());
    QCOMPARE(tracker.rects().size(), qsizetype(1));
    QCOMPARE(tracker.rects().at(0), QRectF(0.0, 0.0, kViewWidth, kViewHeight));
    QCOMPARE(tracker.bounds(), QRectF(0.0, 0.0, kViewWidth, kViewHeight));

    // addAll() is idempotent.
    tracker.addAll();
    QCOMPARE(tracker.rects().size(), qsizetype(1));
}

void TestDamageTracker::clearEmptiesTheTracker()
{
    md::DamageTracker tracker;
    tracker.setFullRect(QRectF(0.0, 0.0, kViewWidth, kViewHeight));
    tracker.add(QRectF(10.0, 10.0, 20.0, 20.0));

    tracker.clear();

    QVERIFY(tracker.isEmpty());
    QVERIFY(tracker.rects().isEmpty());
    QVERIFY(!tracker.isFull());
    QCOMPARE(tracker.bounds(), QRectF());
    // The coordinate reference survives a clear.
    QCOMPARE(tracker.fullRect(), QRectF(0.0, 0.0, kViewWidth, kViewHeight));
}

void TestDamageTracker::emptyAndNonFiniteRectsAreIgnored()
{
    md::DamageTracker tracker;
    tracker.setFullRect(QRectF(0.0, 0.0, kViewWidth, kViewHeight));

    tracker.add(QRectF());
    tracker.add(QRectF(10.0, 10.0, 0.0, 5.0));
    tracker.add(QRectF(10.0, 10.0, 5.0, 0.0));
    tracker.add(QRectF(10.0, 10.0, -5.0, 5.0));
    tracker.add(QRectF(10.0, 10.0, 5.0, -5.0));

    const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
    const qreal inf = std::numeric_limits<qreal>::infinity();
    tracker.add(QRectF(nan, 0.0, 10.0, 10.0));
    tracker.add(QRectF(0.0, nan, 10.0, 10.0));
    tracker.add(QRectF(0.0, 0.0, inf, 10.0));
    tracker.add(QRectF(0.0, 0.0, 10.0, -inf));

    QVERIFY(tracker.isEmpty());
    QVERIFY(tracker.rects().isEmpty());
    QCOMPARE(tracker.bounds(), QRectF());

    // A usable rectangle after the rejected ones is still recorded normally.
    tracker.add(QRectF(1.0, 2.0, 3.0, 4.0));
    QCOMPARE(tracker.rects().size(), qsizetype(1));
    QCOMPARE(tracker.rects().at(0), QRectF(1.0, 2.0, 3.0, 4.0));
}

void TestDamageTracker::viewRecordsDamageForPaintContent()
{
    TrackingView view;
    showAndSettle(view);

    QVERIFY(view.isDamageTrackingEnabled());
    QVERIFY(view.damage().isEmpty());

    view.resetObservation();
    view.requestUpdate(QRectF(10.0, 20.0, 30.0, 40.0));

    // The request is recorded before Qt gets a chance to paint.
    QVERIFY(!view.damage().isEmpty());
    QCOMPARE(view.damage().rects().size(), qsizetype(1));
    QCOMPARE(view.damage().rects().at(0), QRectF(10.0, 20.0, 30.0, 40.0));

    const QImage image = view.grab().toImage();
    QVERIFY(!image.isNull());

    // paintContent() saw exactly the recorded rectangle through
    // currentDamage(), and a completed pass consumed it.
    QVERIFY(view.sawDamage());
    QCOMPARE(view.seenRectCount(), 1);
    QCOMPARE(view.seenRect(), QRectF(10.0, 20.0, 30.0, 40.0));
    QVERIFY(view.damage().isEmpty());
}

void TestDamageTracker::throwingPaintKeepsTheDamage()
{
    ThrowingTrackingView view;
    showAndSettle(view);

    view.requestUpdate(QRectF(5.0, 5.0, 10.0, 10.0));
    QVERIFY(!view.damage().isEmpty());

    view.throwOnNextPaint();
    const QImage failed = view.grab().toImage();
    QVERIFY(!failed.isNull());
    QVERIFY(!view.lastError().isEmpty());

    // A failed pass is not a successful paint: the damage is still there.
    QVERIFY(!view.damage().isEmpty());
    QCOMPARE(view.damage().rects().size(), qsizetype(1));
    QCOMPARE(view.damage().rects().at(0), QRectF(5.0, 5.0, 10.0, 10.0));

    // The next complete pass sees it and consumes it.
    view.resetObservation();
    const QImage recovered = view.grab().toImage();
    QVERIFY(!recovered.isNull());
    QCOMPARE(view.seenRect(), QRectF(5.0, 5.0, 10.0, 10.0));
    QVERIFY(view.damage().isEmpty());
}

void TestDamageTracker::disabledTrackingRetainsNothing()
{
    TrackingView view(false);
    showAndSettle(view);

    QVERIFY(!view.isDamageTrackingEnabled());
    QVERIFY(view.damage().isEmpty());

    view.resetObservation();
    const int paintsBefore = view.paintCount();

    // The overload still requests a repaint, exactly like a plain update(),
    // but an opted-out view retains no damage state.
    view.requestUpdate(QRectF(10.0, 20.0, 30.0, 40.0));
    QVERIFY(view.damage().isEmpty());

    QTRY_VERIFY(view.paintCount() > paintsBefore);
    QVERIFY(!view.lastPaintSawDamage());
    QVERIFY(view.damage().isEmpty());
}

void TestDamageTracker::noArgumentRequestUpdateMarksFullRect()
{
    TrackingView view;
    showAndSettle(view);
    QVERIFY(view.damage().isEmpty());

    view.requestFullRepaint();

    QVERIFY(!view.damage().isEmpty());
    QVERIFY(view.damage().isFull());
    QCOMPARE(view.damage().rects().size(), qsizetype(1));
    QCOMPARE(view.damage().rects().at(0), QRectF(view.rect()));
    QCOMPARE(view.damage().fullRect(), QRectF(view.rect()));

    // And the next successful pass consumes it, like any other damage.
    view.grab();
    QVERIFY(view.damage().isEmpty());
}

QTEST_MAIN(TestDamageTracker)
#include "tst_damage_tracker.moc"
