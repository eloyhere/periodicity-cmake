// Tooltip: anchored label, delay, flipping and dismissal.

#include <md/components/tooltip.h>

#include <QApplication>
#include <QEvent>
#include <QTest>
#include <QWidget>

class TestTooltip : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void opensAboveTheAnchor();
    void flipsBelowNearTheTop();
    void hoverRespectsTheDelay();
    void leaveHidesTheTooltip();
    void showTextReusesTheWindowTooltip();
};

namespace {

QWidget* host = nullptr;

} // namespace

void TestTooltip::init()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
    host = new QWidget;
    host->resize(800, 600);
    host->show();
    QTest::qWait(10);
}

void TestTooltip::cleanup()
{
    delete host;
    host = nullptr;
}

void TestTooltip::opensAboveTheAnchor()
{
    QWidget anchor(host);
    anchor.setGeometry(300, 300, 160, 40);
    anchor.show();

    md::Tooltip tooltip;
    tooltip.setAnchor(&anchor);
    tooltip.setText(QStringLiteral("A tooltip"));
    tooltip.setDelay(0);
    tooltip.show();

    QVERIFY(tooltip.isOpen());
    QCOMPARE(tooltip.effectivePlacement(), md::Tooltip::Placement::Above);

    const QRectF panel = tooltip.panelGlobalRect();
    const QPoint anchorGlobal = anchor.mapToGlobal(QPoint(0, 0));
    QVERIFY(panel.bottom() <= anchorGlobal.y());
    QCOMPARE(qRound(panel.center().x()), anchorGlobal.x() + anchor.width() / 2);
    QVERIFY(panel.width() > 0.0);
    QVERIFY(panel.height() > 0.0);

    tooltip.hide();
    QVERIFY(!tooltip.isOpen());
}

void TestTooltip::flipsBelowNearTheTop()
{
    QWidget anchor(host);
    anchor.setGeometry(300, 0, 160, 40);
    anchor.show();

    md::Tooltip tooltip;
    tooltip.setAnchor(&anchor);
    tooltip.setText(QStringLiteral("Below"));
    tooltip.setPlacement(md::Tooltip::Placement::Above);
    tooltip.show();

    QCOMPARE(tooltip.effectivePlacement(), md::Tooltip::Placement::Below);
    QVERIFY(tooltip.panelGlobalRect().top() >= anchor.mapToGlobal(QPoint(0, 40)).y());
}

void TestTooltip::hoverRespectsTheDelay()
{
    QWidget anchor(host);
    anchor.setGeometry(300, 300, 160, 40);
    anchor.show();

    md::Tooltip tooltip;
    tooltip.setAnchor(&anchor);
    tooltip.setText(QStringLiteral("Delayed"));
    tooltip.setDelay(60);
    tooltip.hover();

    QVERIFY(!tooltip.isOpen());
    QTRY_VERIFY_WITH_TIMEOUT(tooltip.isOpen(), 1000);
}

void TestTooltip::leaveHidesTheTooltip()
{
    QWidget anchor(host);
    anchor.setGeometry(300, 300, 160, 40);
    anchor.show();

    md::Tooltip tooltip;
    tooltip.setAnchor(&anchor);
    tooltip.setText(QStringLiteral("Leave"));
    tooltip.setDelay(0);
    tooltip.hover();
    QVERIFY(tooltip.isOpen());

    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&anchor, &leave);
    QVERIFY(!tooltip.isOpen());
}

void TestTooltip::showTextReusesTheWindowTooltip()
{
    QWidget anchor(host);
    anchor.setGeometry(300, 300, 160, 40);
    anchor.show();

    md::Tooltip* first = md::Tooltip::showText(&anchor, QStringLiteral("One"), 0);
    QVERIFY(first != nullptr);
    QVERIFY(first->isOpen());

    md::Tooltip* second = md::Tooltip::showText(&anchor, QStringLiteral("Two"), 0);
    QCOMPARE(second, first);
    QCOMPARE(second->text(), QStringLiteral("Two"));
}

QTEST_MAIN(TestTooltip)
#include "tst_tooltip.moc"
