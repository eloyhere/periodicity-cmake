// Regression tests for Coordinator dispatch lifetimes.
//
// A nested-scroll callback is allowed to mutate the coordinator it is being
// dispatched from: remove or add behaviours, or delete a child (which destroys
// that child's behaviour through the destroyed() connection). Dispatch must not
// hold a reference into entries_ across a callback, so every test here performs
// one of those mutations from inside a callback and then verifies that the
// remaining behaviours were still dispatched and the coordinator stayed usable.

#include <md/layout/coordinator.h>
#include <md/theme/theme.h>

#include <QList>
#include <QPainter>
#include <QPointF>
#include <QPointer>
#include <QSignalSpy>
#include <QSizeF>
#include <QString>
#include <QTest>

#include <functional>
#include <memory>
#include <utility>

namespace {

// A leaf view with a fixed natural size, so these tests are about dispatch and
// never about a child's own layout.
class Leaf final : public md::View
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

// A behaviour whose four callbacks are supplied by the test, so each test can
// perform its mutation exactly where the defect did.
class CallbackBehaviour final : public md::CoordinatorBehaviour
{
public:
    CallbackBehaviour(QString name, QList<QString>* log)
        : name_(std::move(name))
        , log_(log)
    {
    }

    bool onStartNestedScroll(Qt::Orientation) override
    {
        log_->append(name_ + QStringLiteral(".start"));
        return onStart ? onStart() : false;
    }

    void onNestedPreScroll(const QPointF&, QPointF*) override
    {
        log_->append(name_ + QStringLiteral(".pre"));
        if (onPre)
            onPre();
    }

    void onNestedScroll(const QPointF&, const QPointF&) override
    {
        log_->append(name_ + QStringLiteral(".post"));
        if (onPost)
            onPost();
    }

    void onStopNestedScroll() override
    {
        log_->append(name_ + QStringLiteral(".stop"));
        if (onStop)
            onStop();
    }

    std::function<bool()> onStart;
    std::function<void()> onPre;
    std::function<void()> onPost;
    std::function<void()> onStop;

private:
    QString name_;
    QList<QString>* log_;
};

} // namespace

class TestCoordinatorLifetime final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void preScrollSurvivesSelfRemoval();
    void scrollSurvivesChildDeletion();
    void startSurvivesBehaviourAddedDuringCallback();
    void stopSurvivesSelfRemoval();
};

void TestCoordinatorLifetime::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestCoordinatorLifetime::preScrollSurvivesSelfRemoval()
{
    md::Coordinator coordinator;
    Leaf first(QSizeF(10.0, 10.0));
    Leaf second(QSizeF(10.0, 10.0));
    coordinator.addView(&first);
    coordinator.addView(&second);

    QList<QString> log;

    auto firstBehaviour = std::make_unique<CallbackBehaviour>(QStringLiteral("a"), &log);
    firstBehaviour->onStart = [] { return true; };
    firstBehaviour->onPre = [&coordinator, &first] {
        coordinator.removeBehaviour(&first);
    };

    auto secondBehaviour = std::make_unique<CallbackBehaviour>(QStringLiteral("b"), &log);
    secondBehaviour->onStart = [] { return true; };
    CallbackBehaviour* secondRaw = secondBehaviour.get();

    QVERIFY(coordinator.addBehaviour(&first, std::move(firstBehaviour)) != nullptr);
    QVERIFY(coordinator.addBehaviour(&second, std::move(secondBehaviour)) != nullptr);

    QVERIFY(coordinator.startNestedScroll(Qt::Vertical));

    // "a" removes (and therefore destroys) itself while its own callback is
    // executing. The later "b" must still be dispatched.
    QPointF consumed;
    coordinator.dispatchNestedPreScroll(QPointF(0.0, 10.0), &consumed);

    QCOMPARE(consumed, QPointF());
    QVERIFY(coordinator.behaviour(&first) == nullptr);
    QVERIFY(coordinator.behaviour(&second) == secondRaw);
    QCOMPARE(log.join(QLatin1Char(',')),
             QStringLiteral("a.start,b.start,a.pre,b.pre"));
}

void TestCoordinatorLifetime::scrollSurvivesChildDeletion()
{
    md::Coordinator coordinator;
    Leaf first(QSizeF(10.0, 10.0));
    auto* second = new Leaf(QSizeF(10.0, 10.0));
    Leaf third(QSizeF(10.0, 10.0));
    coordinator.addView(&first);
    coordinator.addView(second);
    coordinator.addView(&third);

    QPointer<Leaf> secondGuard(second);
    QList<QString> log;

    auto firstBehaviour = std::make_unique<CallbackBehaviour>(QStringLiteral("a"), &log);
    firstBehaviour->onStart = [] { return true; };
    firstBehaviour->onPost = [second] { delete second; };

    auto secondBehaviour = std::make_unique<CallbackBehaviour>(QStringLiteral("b"), &log);
    auto thirdBehaviour = std::make_unique<CallbackBehaviour>(QStringLiteral("c"), &log);
    thirdBehaviour->onStart = [] { return true; };

    QVERIFY(coordinator.addBehaviour(&first, std::move(firstBehaviour)) != nullptr);
    QVERIFY(coordinator.addBehaviour(second, std::move(secondBehaviour)) != nullptr);
    QVERIFY(coordinator.addBehaviour(&third, std::move(thirdBehaviour)) != nullptr);

    QVERIFY(coordinator.startNestedScroll(Qt::Vertical));

    // "a" deletes a later child while its callback is executing. The destroyed
    // connection erases that child's entry, so "b" is gone and "c" still runs.
    coordinator.dispatchNestedScroll(QPointF(0.0, 5.0), QPointF(0.0, 5.0));

    QVERIFY(secondGuard.isNull());
    QCOMPARE(coordinator.behaviours().size(), 2);
    QCOMPARE(log.join(QLatin1Char(',')),
             QStringLiteral("a.start,b.start,c.start,a.post,c.post"));
}

void TestCoordinatorLifetime::startSurvivesBehaviourAddedDuringCallback()
{
    md::Coordinator coordinator;
    Leaf first(QSizeF(10.0, 10.0));
    Leaf second(QSizeF(10.0, 10.0));
    coordinator.addView(&first);
    coordinator.addView(&second);

    QList<Leaf*> extras;
    for (int i = 0; i < 8; ++i) {
        auto* extra = new Leaf(QSizeF(10.0, 10.0));
        coordinator.addView(extra);
        extras.append(extra);
    }

    QList<QString> log;

    auto firstBehaviour = std::make_unique<CallbackBehaviour>(QStringLiteral("a"), &log);
    firstBehaviour->onStart = [&coordinator, &extras, &log] {
        // Growing entries_ from inside a callback moves every Entry. With two
        // initial behaviours the old dispatcher would then read the moved-from
        // second entry out of the freed buffer.
        for (Leaf* extra : extras) {
            coordinator.addBehaviour(
                extra, std::make_unique<CallbackBehaviour>(QStringLiteral("extra"), &log));
        }
        return true;
    };

    auto secondBehaviour = std::make_unique<CallbackBehaviour>(QStringLiteral("b"), &log);
    secondBehaviour->onStart = [] { return true; };

    QVERIFY(coordinator.addBehaviour(&first, std::move(firstBehaviour)) != nullptr);
    QVERIFY(coordinator.addBehaviour(&second, std::move(secondBehaviour)) != nullptr);

    QVERIFY(coordinator.startNestedScroll(Qt::Vertical));

    // The behaviours added by "a" join the registry, but they are not part of
    // this dispatch round's snapshot; "b" must still be started.
    QCOMPARE(coordinator.behaviours().size(), 10);
    QCOMPARE(log.join(QLatin1Char(',')), QStringLiteral("a.start,b.start"));
}

void TestCoordinatorLifetime::stopSurvivesSelfRemoval()
{
    md::Coordinator coordinator;
    Leaf first(QSizeF(10.0, 10.0));
    Leaf second(QSizeF(10.0, 10.0));
    coordinator.addView(&first);
    coordinator.addView(&second);

    QList<QString> log;

    auto firstBehaviour = std::make_unique<CallbackBehaviour>(QStringLiteral("a"), &log);
    firstBehaviour->onStart = [] { return true; };
    firstBehaviour->onStop = [&coordinator, &first] {
        coordinator.removeBehaviour(&first);
    };

    auto secondBehaviour = std::make_unique<CallbackBehaviour>(QStringLiteral("b"), &log);
    secondBehaviour->onStart = [] { return true; };

    QVERIFY(coordinator.addBehaviour(&first, std::move(firstBehaviour)) != nullptr);
    QVERIFY(coordinator.addBehaviour(&second, std::move(secondBehaviour)) != nullptr);

    QSignalSpy stopped(&coordinator, &md::Coordinator::nestedScrollStopped);
    QVERIFY(coordinator.startNestedScroll(Qt::Vertical));

    // "a" destroys itself from its own onStopNestedScroll() callback; "b" must
    // still be told and the gesture must still report that it stopped.
    coordinator.stopNestedScroll();

    QCOMPARE(stopped.count(), 1);
    QVERIFY(!coordinator.isNestedScrollInProgress());
    QVERIFY(coordinator.behaviour(&first) == nullptr);
    QCOMPARE(log.join(QLatin1Char(',')),
             QStringLiteral("a.start,b.start,a.stop,b.stop"));
}

QTEST_MAIN(TestCoordinatorLifetime)
#include "tst_coordinator.moc"
