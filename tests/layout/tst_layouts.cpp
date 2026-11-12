// Unit tests for the layout containers (framework.md §6, docs/backlog.md
// batch 4), plus the container-based components of the same batch:
// StateLayout, ToolBar and Badge.
//
// Every container is driven through its public measure()/arrange() contract
// rather than through a QLayout, exactly as the layout bridge does. The
// children are leaf Views with a fixed natural size, so a failing assertion is
// about the container and never about a child's own layout.
//
// Right-to-left is tested by setting the widget's layout direction, which is
// what a ThemeScope or an application-level setting ends up doing.

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QList>
#include <QPainter>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRegion>
#include <QSignalSpy>
#include <QSizeF>
#include <QString>
#include <QTest>
#include <QWidget>

#include <md/components/badge.h>
#include <md/components/state.h>
#include <md/components/toolbar.h>
#include <md/layout/appbar.h>
#include <md/layout/collapse.h>
#include <md/layout/constraint.h>
#include <md/layout/coordinator.h>
#include <md/layout/frame.h>
#include <md/layout/linear.h>
#include <md/theme/theme.h>
#include <md/tokens/elevation.h>

#include <memory>
#include <limits>

namespace {

// A leaf view with a fixed natural size. The layouts under test must not be
// confused by a child that lays itself out.
class Leaf : public md::View
{
public:
    explicit Leaf(QSizeF natural, QWidget* parent = nullptr)
        : View(parent)
        , natural_(natural)
    {
    }

    void setNaturalSize(QSizeF size) { natural_ = size; }

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

// Renders a widget into an explicitly transparent image, so an unpainted pixel
// is deterministic rather than uninitialised.
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
            if (qAlpha(image.pixel(x, y)) != 0)
                ++count;
        }
    }
    return count;
}

// A coordinator behaviour that writes its calls to a shared log and records
// what the previous behaviours had already consumed when it ran.
class TestBehaviour : public md::CoordinatorBehaviour
{
public:
    TestBehaviour(QString name, QList<QString>* log, QList<qreal>* seen, qreal consume)
        : name_(std::move(name))
        , log_(log)
        , seen_(seen)
        , consume_(consume)
    {
    }

    bool onStartNestedScroll(Qt::Orientation axis) override
    {
        log_->append(name_ + QStringLiteral(".start"));
        return axis == Qt::Vertical;
    }

    void onNestedPreScroll(const QPointF& delta, QPointF* consumed) override
    {
        log_->append(name_ + QStringLiteral(".pre"));
        if (seen_ != nullptr && consumed != nullptr)
            seen_->append(consumed->y());
        if (consumed != nullptr)
            consumed->setY(consumed->y() + qMin(consume_, delta.y() > 0.0 ? delta.y() : 0.0));
    }

    void onNestedScroll(const QPointF&, const QPointF&) override
    {
        log_->append(name_ + QStringLiteral(".post"));
    }

    void onStopNestedScroll() override { log_->append(name_ + QStringLiteral(".stop")); }

private:
    QString name_;
    QList<QString>* log_;
    QList<qreal>* seen_;
    qreal consume_;
};

} // namespace

class TestLayouts : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void linearWeightsAndVerticalCoordinates();
    void linearRowMirrorsInRtl();
    void linearOverflowKeepsNaturalSizes();

    void frameStacksAndAligns();

    void constraintStretchCentreAndMirror();
    void constraintRejectsInvalidAnchors();
    void constraintConflictingAnchorsStayInside();

    void stateSwitchesVisibleChild();
    void stateDrawsPlaceholderWithoutWidget();

    void toolBarArrangesActions();

    void badgeCountTruncates();
    void badgeFollowsTarget();

    void coordinatorDispatchesInRegistrationOrder();
    void coordinatorDrivesAppBarAndCollapse();

    void appBarCollapsesWithScroll();
    void appBarSkipsHiddenActions();
    void appBarAnnouncesRangeClamp();
    void appBarHonoursZeroHeightsAndClearedNavigation();

    void collapseExpandsWithSignal();
    void collapseAcceptsZeroHeaderHeight();
};

void TestLayouts::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestLayouts::linearWeightsAndVerticalCoordinates()
{
    md::Linear column(md::Linear::Orientation::Vertical);
    Leaf first(QSizeF(20.0, 20.0));
    Leaf second(QSizeF(20.0, 30.0));
    Leaf third(QSizeF(20.0, 40.0));

    column.addView(&first);
    column.addView(&second);
    column.addView(&third);
    column.setSpacing(10.0);
    column.setWeight(&second, 1.0);

    QVERIFY(qFuzzyCompare(column.weight(&second), 1.0));

    column.measure(md::MeasureSpec::exactly(200.0), md::MeasureSpec::exactly(200.0));
    QCOMPARE(column.measuredSize(), QSizeF(200.0, 200.0));
    column.arrange(QRect(0, 0, 200, 200));

    // 20 + 30 + 40 of natural height plus two gaps is 110, so 90 pixels are
    // spare and the weighted child takes all of them.
    QCOMPARE(first.geometry(), QRect(0, 0, 20, 20));
    QCOMPARE(second.geometry(), QRect(0, 30, 20, 120));
    QCOMPARE(third.geometry(), QRect(0, 160, 20, 40));

    // The cross axis must come from the alignment, never from the main-axis
    // cursor: every child keeps x = 180 at this gravity.
    column.setGravity(Qt::AlignRight | Qt::AlignTop);
    QCOMPARE(first.geometry(), QRect(180, 0, 20, 20));
    QCOMPARE(second.geometry(), QRect(180, 30, 20, 120));
    QCOMPARE(third.geometry(), QRect(180, 160, 20, 40));

    // The cross axis mirrors in right-to-left — the trailing edge becomes the
    // left one — while the main axis never moves.
    column.setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(first.geometry(), QRect(0, 0, 20, 20));
    QCOMPARE(second.geometry(), QRect(0, 30, 20, 120));

    column.setGravity(Qt::AlignLeft | Qt::AlignTop);
    QCOMPARE(first.geometry(), QRect(180, 0, 20, 20));
}

void TestLayouts::linearRowMirrorsInRtl()
{
    md::Linear row(md::Linear::Orientation::Horizontal);
    Leaf first(QSizeF(40.0, 20.0));
    Leaf second(QSizeF(60.0, 20.0));

    row.addView(&first);
    row.addView(&second);
    row.setSpacing(10.0);

    row.measure(md::MeasureSpec::exactly(200.0), md::MeasureSpec::exactly(50.0));
    row.arrange(QRect(0, 0, 200, 50));

    QCOMPARE(first.geometry(), QRect(0, 0, 40, 20));
    QCOMPARE(second.geometry(), QRect(50, 0, 60, 20));

    row.setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(first.geometry(), QRect(160, 0, 40, 20));
    QCOMPARE(second.geometry(), QRect(90, 0, 60, 20));

    // A centred run mirrors onto itself, so the run start does not move.
    row.setLayoutDirection(Qt::LeftToRight);
    row.setGravity(Qt::AlignHCenter | Qt::AlignTop);
    QCOMPARE(first.geometry(), QRect(45, 0, 40, 20));
    QCOMPARE(second.geometry(), QRect(95, 0, 60, 20));
}

void TestLayouts::linearOverflowKeepsNaturalSizes()
{
    // A weight takes a share of *positive* spare space. When the natural
    // children overflow the axis there is no spare, so the weighted child keeps
    // its measurement and the run spills over instead of one child being
    // crushed to make the others fit.
    md::Linear row(md::Linear::Orientation::Horizontal);
    Leaf first(QSizeF(40.0, 20.0));
    Leaf second(QSizeF(40.0, 20.0));

    row.addView(&first);
    row.addView(&second);
    row.setWeight(&second, 1.0);

    row.measure(md::MeasureSpec::exactly(60.0), md::MeasureSpec::exactly(20.0));
    row.arrange(QRect(0, 0, 60, 20));
    QCOMPARE(first.geometry(), QRect(0, 0, 40, 20));
    QCOMPARE(second.geometry(), QRect(40, 0, 40, 20));

    // An axis of zero behaves the same way rather than collapsing the run.
    row.measure(md::MeasureSpec::exactly(0.0), md::MeasureSpec::exactly(20.0));
    row.arrange(QRect(0, 0, 0, 20));
    QCOMPARE(first.geometry(), QRect(0, 0, 40, 20));
    QCOMPARE(second.geometry(), QRect(40, 0, 40, 20));

    // A run that does fit still fills the axis: the weighted child takes the
    // whole 120-pixel share, so the fix only affects the overflowing case.
    row.measure(md::MeasureSpec::exactly(200.0), md::MeasureSpec::exactly(20.0));
    row.arrange(QRect(0, 0, 200, 20));
    QCOMPARE(first.geometry(), QRect(0, 0, 40, 20));
    QCOMPARE(second.geometry(), QRect(40, 0, 160, 20));
}

void TestLayouts::frameStacksAndAligns()
{
    md::Frame frame;
    Leaf under(QSizeF(40.0, 20.0));
    Leaf over(QSizeF(40.0, 20.0));

    frame.addView(&under);
    frame.addView(&over);

    frame.measure(md::MeasureSpec::exactly(200.0), md::MeasureSpec::exactly(100.0));
    frame.arrange(QRect(0, 0, 200, 100));

    // Without a gravity every child fills, so the paint order — which is the
    // child order — decides what is visible.
    QCOMPARE(under.geometry(), QRect(0, 0, 200, 100));
    QCOMPARE(over.geometry(), QRect(0, 0, 200, 100));
    QCOMPARE(frame.views().size(), 2);
    QVERIFY(frame.views().at(0) == &under);
    QVERIFY(frame.views().at(1) == &over);

    // A gravity gives the child its natural size in the nine-cell grid.
    frame.setGravity(&over, Qt::AlignRight | Qt::AlignBottom);
    QCOMPARE(over.geometry(), QRect(160, 80, 40, 20));
    QCOMPARE(under.geometry(), QRect(0, 0, 200, 100));

    frame.setGravity(&over, Qt::AlignHCenter | Qt::AlignVCenter);
    QCOMPARE(over.geometry(), QRect(80, 40, 40, 20));

    // A margin insets the grid, so the filling child no longer fills the frame.
    frame.setMargin(10.0);
    QCOMPARE(frame.margin(), 10.0);
    QCOMPARE(under.geometry(), QRect(10, 10, 180, 80));

    frame.setGravity(&over, Qt::AlignLeft | Qt::AlignTop);
    QCOMPARE(over.geometry(), QRect(10, 10, 40, 20));

    // Nine-grid leading/trailing mirror with the layout direction.
    frame.setGravity(&over, Qt::AlignTrailing | Qt::AlignTop);
    frame.setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(over.geometry(), QRect(10, 10, 40, 20));
    frame.setLayoutDirection(Qt::LeftToRight);
    QCOMPARE(over.geometry(), QRect(150, 10, 40, 20));
}

void TestLayouts::constraintStretchCentreAndMirror()
{
    md::Constraint root;
    Leaf stretched(QSizeF(30.0, 30.0));
    Leaf centred(QSizeF(40.0, 20.0));

    root.addView(&stretched);
    root.addView(&centred);

    // Two opposing anchors stretch the child between them.
    QVERIFY(root.setConstraints(&stretched, md::Constraint::Anchors{
                                                .start = 10.0,
                                                .end = 10.0,
                                                .top = 5.0,
                                                .bottom = 5.0,
                                            }));

    md::Constraint::Anchors centre;
    centre.centerHorizontal = true;
    centre.centerVertical = true;
    QVERIFY(root.setConstraints(&centred, centre));

    root.measure(md::MeasureSpec::exactly(200.0), md::MeasureSpec::exactly(100.0));
    root.arrange(QRect(0, 0, 200, 100));

    QCOMPARE(stretched.geometry(), QRect(10, 5, 180, 90));
    QCOMPARE(centred.geometry(), QRect(80, 40, 40, 20));

    // A single start anchor in right-to-left hangs from the right edge.
    Leaf anchored(QSizeF(30.0, 30.0));
    root.addView(&anchored);

    md::Constraint::Anchors startOnly;
    startOnly.start = 10.0;
    QVERIFY(root.setConstraints(&anchored, startOnly));

    root.arrange(QRect(0, 0, 200, 100));
    QCOMPARE(anchored.geometry(), QRect(10, 0, 30, 30));

    root.setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(anchored.geometry(), QRect(160, 0, 30, 30));

    // A symmetric stretch is unaffected by the mirror.
    QCOMPARE(stretched.geometry(), QRect(10, 5, 180, 90));
    QCOMPARE(centred.geometry(), QRect(80, 40, 40, 20));
}

void TestLayouts::constraintRejectsInvalidAnchors()
{
    md::Constraint root;
    Leaf child(QSizeF(30.0, 12.0));
    root.addView(&child);

    md::Constraint::Anchors valid;
    valid.start = 4.0;
    QVERIFY(root.setConstraints(&child, valid));
    QVERIFY(root.hasConstraints(&child));

    // A negative anchor is rejected and the previous value survives.
    QVERIFY(!root.setConstraints(&child, md::Constraint::Anchors{.start = -1.0}));
    QCOMPARE(root.constraints(&child).start.value(), 4.0);

    // A non-finite one is rejected as well.
    QVERIFY(!root.setConstraints(&child, md::Constraint::Anchors{.end = std::numeric_limits<qreal>::quiet_NaN()}));
    QVERIFY(!root.constraints(&child).end.has_value());

    // Constraints are only accepted for members.
    Leaf foreign(QSizeF(10.0, 10.0));
    QVERIFY(!root.setConstraints(&foreign, valid));

    // Without constraints a child keeps its natural size at the origin.
    md::Constraint loose;
    Leaf natural(QSizeF(30.0, 12.0));
    loose.addView(&natural);
    loose.measure(md::MeasureSpec::exactly(200.0), md::MeasureSpec::exactly(100.0));
    loose.arrange(QRect(0, 0, 200, 100));
    QCOMPARE(natural.geometry(), QRect(0, 0, 30, 12));
}

void TestLayouts::constraintConflictingAnchorsStayInside()
{
    md::Constraint root;
    Leaf child(QSizeF(30.0, 20.0));
    root.addView(&child);

    // start + end is 300 on a 100-wide axis. The insets are clamped so the
    // child stays inside the content; the overrun is split evenly, so the
    // zero-width child ends up in the middle of the axis instead of at x = 150.
    QVERIFY(root.setConstraints(&child, md::Constraint::Anchors{
                                                 .start = 150.0,
                                                 .end = 150.0,
                                                 .top = 5.0,
                                                 .bottom = 5.0,
                                             }));

    root.measure(md::MeasureSpec::exactly(100.0), md::MeasureSpec::exactly(100.0));
    QCOMPARE(root.measuredSize(), QSizeF(100.0, 100.0));
    root.arrange(QRect(0, 0, 100, 100));
    QCOMPARE(child.geometry(), QRect(50, 5, 0, 90));

    // A symmetric conflict is direction-independent: the mirror of the centre
    // is the centre.
    root.setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(child.geometry(), QRect(50, 5, 0, 90));
    root.setLayoutDirection(Qt::LeftToRight);

    // An asymmetric conflict keeps the ratio of the two sides: the 20-pixel
    // overrun comes off both anchors evenly.
    Leaf asymmetric(QSizeF(20.0, 20.0));
    root.addView(&asymmetric);
    QVERIFY(root.setConstraints(&asymmetric, md::Constraint::Anchors{
                                                  .start = 30.0,
                                                  .end = 90.0,
                                              }));
    root.arrange(QRect(0, 0, 100, 100));
    QCOMPARE(asymmetric.geometry(), QRect(20, 0, 0, 20));

    // The vertical axis follows the same rule.
    Leaf vertical(QSizeF(20.0, 20.0));
    root.addView(&vertical);
    QVERIFY(root.setConstraints(&vertical, md::Constraint::Anchors{
                                                   .top = 150.0,
                                                   .bottom = 150.0,
                                               }));
    root.arrange(QRect(0, 0, 100, 100));
    QCOMPARE(vertical.geometry(), QRect(0, 50, 20, 0));

    // A single anchor beyond the edge is clamped back inside as well, so a
    // child is never placed outside the content by its own anchors.
    Leaf beyond(QSizeF(20.0, 20.0));
    root.addView(&beyond);
    QVERIFY(root.setConstraints(&beyond, md::Constraint::Anchors{.start = 500.0}));
    root.arrange(QRect(0, 0, 100, 100));
    QCOMPARE(beyond.geometry(), QRect(100, 0, 0, 20));

    // Every child stayed inside the 100x100 content rectangle.
    for (md::View* view : root.views()) {
        QVERIFY(view->geometry().left() >= 0);
        QVERIFY(view->geometry().top() >= 0);
        QVERIFY(view->geometry().right() < 100);
        QVERIFY(view->geometry().bottom() < 100);
    }
}

void TestLayouts::stateSwitchesVisibleChild()
{
    md::StateLayout page;
    Leaf content(QSizeF(100.0, 50.0));
    Leaf loading(QSizeF(60.0, 20.0));
    Leaf error(QSizeF(80.0, 30.0));

    page.setContentWidget(&content);
    page.setLoadingWidget(&loading);
    page.setErrorWidget(&error);

    QVERIFY(page.state() == md::StateLayout::State::Content);
    QVERIFY(!content.isHidden());
    QVERIFY(loading.isHidden());
    QVERIFY(error.isHidden());
    QVERIFY(page.contentWidget() == &content);
    QVERIFY(page.loadingWidget() == &loading);
    QVERIFY(page.errorWidget() == &error);

    QSignalSpy spy(&page, &md::StateLayout::stateChanged);

    page.setState(md::StateLayout::State::Loading);
    QCOMPARE(spy.count(), 1);
    QVERIFY(!loading.isHidden());
    QVERIFY(content.isHidden());
    QVERIFY(error.isHidden());

    page.measure(md::MeasureSpec::exactly(200.0), md::MeasureSpec::exactly(100.0));
    page.arrange(QRect(0, 0, 200, 100));

    // The active child fills the container.
    QCOMPARE(loading.geometry(), QRect(0, 0, 200, 100));

    page.setState(md::StateLayout::State::Error);
    QCOMPARE(spy.count(), 2);
    QVERIFY(!error.isHidden());
    QVERIFY(loading.isHidden());
    QCOMPARE(error.geometry(), QRect(0, 0, 200, 100));

    // Re-selecting the current state is not a transition.
    page.setState(md::StateLayout::State::Error);
    QCOMPARE(spy.count(), 2);
}

void TestLayouts::stateDrawsPlaceholderWithoutWidget()
{
    md::StateLayout page(md::StateLayout::State::Error);
    QVERIFY(page.state() == md::StateLayout::State::Error);
    QCOMPARE(page.placeholderText(md::StateLayout::State::Error),
             QStringLiteral("Something went wrong"));

    page.resize(200, 120);
    const QImage image = renderWidget(page);
    QVERIFY(paintedPixels(image) > 0);

    page.setPlaceholderText(md::StateLayout::State::Error, QStringLiteral("Try again"));
    QCOMPARE(page.placeholderText(md::StateLayout::State::Error), QStringLiteral("Try again"));

    // The content state without a widget paints no placeholder: only the
    // framework's own outline is left, which is strictly less than a page that
    // carries the indicator ring and the message.
    md::StateLayout empty;
    empty.resize(200, 120);
    QVERIFY(empty.views().isEmpty());
    QVERIFY(paintedPixels(renderWidget(empty)) < paintedPixels(image));
}

void TestLayouts::toolBarArrangesActions()
{
    md::ToolBar bar;
    bar.setTitle(QStringLiteral("Inbox"));

    Leaf nav(QSizeF(48.0, 48.0));
    Leaf first(QSizeF(48.0, 48.0));
    Leaf second(QSizeF(48.0, 48.0));

    bar.setNavigationIcon(&nav);
    bar.addAction(&first);
    bar.addAction(&second);

    QCOMPARE(bar.title(), QStringLiteral("Inbox"));
    QCOMPARE(bar.actions().size(), 2);
    QCOMPARE(bar.navigationIcon(), &nav);
    QCOMPARE(bar.barHeight(), 64.0);

    bar.measure(md::MeasureSpec::exactly(320.0), md::MeasureSpec::exactly(64.0));
    QCOMPARE(bar.measuredSize(), QSizeF(320.0, 64.0));
    bar.arrange(QRect(0, 0, 320, 64));

    // Navigation icon at the leading edge, vertically centred.
    QCOMPARE(nav.geometry().left(), 4);
    QCOMPARE(nav.geometry().top(), 8);

    // The action run is flush with the trailing edge and keeps its order.
    QCOMPARE(second.x() + second.width(), 320 - 4);
    QCOMPARE(first.x() + first.width() + 4, second.x());

    // The title is squeezed between the icon and the actions.
    QVERIFY(bar.titleRect().width() > 0.0);
    QVERIFY(bar.titleRect().left() >= nav.geometry().right());
    QVERIFY(bar.titleRect().right() <= first.x());

    // A left-aligned action run starts right after the navigation icon.
    bar.setActionsAlignment(Qt::AlignLeft);
    QCOMPARE(first.geometry().left(), nav.geometry().left() + nav.width() + 4);

    // Mirroring moves the icon to the right and the actions to the left.
    bar.setActionsAlignment(Qt::AlignRight);
    bar.setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(nav.geometry().left(), 320 - 4 - 48);
    QCOMPARE(first.geometry().left(), 4);
    QCOMPARE(second.x(), 4 + 48 + 4);

    // Scrolling lifts the bar without changing its height.
    QVERIFY(bar.elevation() == md::Elevation::Level0);
    bar.setScrollFraction(0.5);
    QVERIFY(bar.elevation() == md::Elevation::Level2);
    QCOMPARE(bar.scrollFraction(), 0.5);
    bar.setScrollFraction(0.0);
    QVERIFY(bar.elevation() == md::Elevation::Level0);
    bar.setElevated(true);
    QVERIFY(bar.isElevated());
    QVERIFY(bar.elevation() == md::Elevation::Level2);
}

void TestLayouts::badgeCountTruncates()
{
    md::Badge badge;

    badge.setCount(150);
    QCOMPARE(badge.text(), QStringLiteral("99+"));
    QCOMPARE(badge.count(), 150);

    badge.setMaxCount(9);
    QCOMPARE(badge.maxCount(), 9);
    badge.setCount(12);
    QCOMPARE(badge.text(), QStringLiteral("9+"));

    badge.setCount(7);
    QCOMPARE(badge.text(), QStringLiteral("7"));

    const QSize textSize = badge.sizeHint();
    // A single digit is the circular form of a badge, so the width is never
    // narrower than the height and only grows past it when the glyph is wide.
    // The metric must not depend on which platform plugin supplies the font.
    QVERIFY(textSize.width() >= textSize.height());
    QVERIFY(textSize.height() >= 16);

    // A dot is square and has no content.
    badge.setDot(true);
    QVERIFY(badge.isDot());
    QVERIFY(badge.text().isEmpty());
    const QSize dotSize = badge.sizeHint();
    QCOMPARE(dotSize.width(), dotSize.height());

    // Explicit text leaves dot mode.
    badge.setText(QStringLiteral("NEW"));
    QVERIFY(!badge.isDot());
    QCOMPARE(badge.text(), QStringLiteral("NEW"));
    QVERIFY(badge.sizeHint().width() > badge.sizeHint().height());

    // A custom fill is kept verbatim; the default stays the theme's error red.
    badge.setColour(QColor(0, 128, 255));
    QCOMPARE(badge.colour(), QColor(0, 128, 255));
}

void TestLayouts::badgeFollowsTarget()
{
    QWidget host;
    host.resize(300, 200);

    QWidget target(&host);
    target.setGeometry(10, 20, 40, 40);

    // Qt defers move and resize events for a hidden widget, so the scenario the
    // badge is built for needs a shown window. The offscreen platform makes it
    // free.
    host.show();
    QTest::qWait(10);

    md::Badge badge;
    badge.setCount(5);
    badge.setTarget(&target);

    QVERIFY(badge.target() == &target);
    QVERIFY(badge.parentWidget() == &host);

    // The badge is centred on the target's top-trailing corner.
    const QSize size = badge.size();
    QCOMPARE(badge.x(), 10 + 40 - size.width() / 2);
    QCOMPARE(badge.y(), 20 - size.height() / 2);

    // A resize of the target is followed through the event filter.
    target.resize(120, 40);
    QCOMPARE(badge.x(), 10 + 120 - size.width() / 2);
    QCOMPARE(badge.y(), 20 - size.height() / 2);

    // So is a move.
    target.move(30, 50);
    QCOMPARE(badge.x(), 30 + 120 - size.width() / 2);
    QCOMPARE(badge.y(), 50 - size.height() / 2);

    // Detaching leaves an ordinary widget behind.
    badge.setTarget(nullptr);
    QVERIFY(badge.target() == nullptr);
    const QPoint keep = badge.pos();
    target.move(0, 0);
    QCOMPARE(badge.pos(), keep);

    // A zero count hides the badge, and a later count brings it back without
    // the owner having to show it again.
    QVERIFY(!badge.isHidden());
    badge.setCount(0);
    QVERIFY(badge.isHidden());
    badge.setCount(4);
    QVERIFY(!badge.isHidden());
    QCOMPARE(badge.text(), QStringLiteral("4"));

    // A standalone badge is painted through the normal View pipeline.
    md::Badge standalone;
    standalone.setText(QStringLiteral("3"));
    standalone.resize(standalone.sizeHint());
    QVERIFY(paintedPixels(renderWidget(standalone)) > 0);
}

void TestLayouts::coordinatorDispatchesInRegistrationOrder()
{
    md::Coordinator coordinator;
    Leaf first(QSizeF(10.0, 10.0));
    Leaf second(QSizeF(10.0, 10.0));

    coordinator.addView(&first);
    coordinator.addView(&second);

    QList<QString> log;
    QList<qreal> seen;
    md::CoordinatorBehaviour* a =
        coordinator.addBehaviour(&first, std::make_unique<TestBehaviour>(QStringLiteral("a"), &log,
                                                                        &seen, 10.0));
    md::CoordinatorBehaviour* b =
        coordinator.addBehaviour(&second, std::make_unique<TestBehaviour>(QStringLiteral("b"), &log,
                                                                         &seen, 5.0));

    QVERIFY(a != nullptr);
    QVERIFY(b != nullptr);
    QCOMPARE(coordinator.behaviours().size(), 2);
    QVERIFY(coordinator.behaviour(&first) == a);

    // A behaviour is only accepted for a member of this coordinator.
    Leaf foreign(QSizeF(1.0, 1.0));
    QVERIFY(coordinator.addBehaviour(&foreign, std::make_unique<TestBehaviour>(
                                                  QStringLiteral("x"), &log, &seen, 0.0))
            == nullptr);

    QSignalSpy started(&coordinator, &md::Coordinator::nestedScrollStarted);
    QSignalSpy stopped(&coordinator, &md::Coordinator::nestedScrollStopped);

    QVERIFY(coordinator.startNestedScroll(Qt::Vertical));
    QCOMPARE(started.count(), 1);
    QVERIFY(coordinator.isNestedScrollInProgress());

    QPointF consumed;
    coordinator.dispatchNestedPreScroll(QPointF(0.0, 100.0), &consumed);
    QCOMPARE(consumed.y(), 15.0);

    // Registration order is dispatch order: "b" saw the 10 pixels "a" took.
    QCOMPARE(seen.size(), 2);
    QCOMPARE(seen.at(0), 0.0);
    QCOMPARE(seen.at(1), 10.0);

    coordinator.dispatchNestedScroll(QPointF(0.0, 15.0), QPointF(0.0, 85.0));
    coordinator.stopNestedScroll();

    QCOMPARE(stopped.count(), 1);
    QVERIFY(!coordinator.isNestedScrollInProgress());
    QCOMPARE(log.join(QLatin1Char(',')),
             QStringLiteral("a.start,b.start,a.pre,b.pre,a.post,b.post,a.stop,b.stop"));

    // Nothing is dispatched while no gesture is in progress.
    coordinator.dispatchNestedPreScroll(QPointF(0.0, 10.0), &consumed);
    QCOMPARE(consumed, QPointF());

    // Removing a behaviour stops the dispatch.
    QVERIFY(coordinator.removeBehaviour(&first));
    QCOMPARE(coordinator.behaviours().size(), 1);
    QVERIFY(coordinator.behaviour(&first) == nullptr);
}

void TestLayouts::coordinatorDrivesAppBarAndCollapse()
{
    md::Coordinator coordinator;
    md::AppBar appBar;
    md::Collapse section;

    coordinator.addView(&appBar);
    coordinator.addView(&section);

    appBar.setTitle(QStringLiteral("Inbox"));
    appBar.setScrollRange(100.0);
    QCOMPARE(appBar.expandedHeight(), 112.0);
    QCOMPARE(appBar.collapsedHeight(), 64.0);

    Leaf body(QSizeF(200.0, 60.0));
    section.setContentWidget(&body);
    section.setAnimated(false);

    md::AppBarBehavior* barBehaviour = appBar.attachTo(&coordinator);
    QVERIFY(barBehaviour != nullptr);
    QVERIFY(coordinator.behaviour(&appBar) == barBehaviour);
    QVERIFY(barBehaviour->appBar() == &appBar);

    // A second attach must not replace the behaviour.
    QVERIFY(coordinator.behaviour(&appBar) == barBehaviour);

    // A plain behaviour that opens the section as the page scrolls: this is the
    // AppBar/Collapse collaboration the coordinator is there for.
    struct OpenOnScroll : md::CoordinatorBehaviour
    {
        explicit OpenOnScroll(md::Collapse* section)
            : section(section)
        {
        }

        bool onStartNestedScroll(Qt::Orientation axis) override
        {
            return axis == Qt::Vertical;
        }

        void onNestedPreScroll(const QPointF& delta, QPointF* consumed) override
        {
            Q_UNUSED(consumed);
            if (delta.y() > 0.0)
                section->setExpanded(true);
        }

        md::Collapse* section;
    };

    QVERIFY(coordinator.addBehaviour(&section, std::make_unique<OpenOnScroll>(&section))
            != nullptr);

    coordinator.measure(md::MeasureSpec::exactly(320.0), md::MeasureSpec::exactly(600.0));
    coordinator.arrange(QRect(0, 0, 320, 600));

    // The AppBarBehaviour owns the bar's rectangle: it stays a top strip
    // instead of being stretched over the whole coordinator.
    QCOMPARE(appBar.height(), 112);

    QVERIFY(coordinator.startNestedScroll(Qt::Vertical));

    QPointF consumed;
    coordinator.dispatchNestedPreScroll(QPointF(0.0, 60.0), &consumed);

    // The bar collapsed by the whole delta, and the content has nothing left to
    // scroll for it.
    QCOMPARE(appBar.scrollOffset(), 60.0);
    QCOMPARE(consumed.y(), 60.0);
    QVERIFY(appBar.elevation() == md::Elevation::Level2);

    // The bar asked to be laid out again when its height changed, and the
    // coordinator honoured the request straight away.
    QVERIFY(appBar.height() < 112);
    QVERIFY(appBar.height() >= 64);

    // The section opened through its own behaviour, driven by the same delta.
    QVERIFY(section.isExpanded());
    QCOMPARE(section.expansion(), 1.0);

    // The bar height follows the collapse fraction.
    appBar.measure(md::MeasureSpec::exactly(320.0), md::MeasureSpec::unspecified());
    QVERIFY(appBar.measuredSize().height() < appBar.expandedHeight());
    QVERIFY(appBar.measuredSize().height() > appBar.collapsedHeight());

    // An overscroll at the top expands the bar again.
    coordinator.dispatchNestedScroll(QPointF(0.0, 60.0), QPointF(0.0, -100.0));
    QCOMPARE(appBar.scrollOffset(), 0.0);
    QVERIFY(appBar.elevation() == md::Elevation::Level0);

    QTest::qWait(1);
    QCOMPARE(appBar.height(), 112);
    coordinator.stopNestedScroll();
    QVERIFY(!coordinator.isNestedScrollInProgress());
}

void TestLayouts::appBarCollapsesWithScroll()
{
    md::AppBar bar;
    bar.setTitle(QStringLiteral("Inbox"));

    Leaf nav(QSizeF(48.0, 48.0));
    Leaf action(QSizeF(48.0, 48.0));
    bar.setNavigationButton(&nav);
    bar.addAction(&action);

    bar.setScrollRange(100.0);

    bar.measure(md::MeasureSpec::exactly(300.0), md::MeasureSpec::exactly(112.0));
    QCOMPARE(bar.measuredSize(), QSizeF(300.0, 112.0));
    bar.arrange(QRect(0, 0, 300, 112));

    QCOMPARE(nav.geometry().left(), 4);
    QCOMPARE(action.x() + action.width(), 300 - 4);

    QSignalSpy offsetSpy(&bar, &md::AppBar::offsetChanged);
    QSignalSpy collapsedSpy(&bar, &md::AppBar::collapsedChanged);

    QVERIFY(bar.elevation() == md::Elevation::Level0);

    bar.setScrollOffset(50.0);
    QCOMPARE(bar.scrollOffset(), 50.0);
    QCOMPARE(bar.collapseFraction(), 0.5);
    QCOMPARE(offsetSpy.count(), 1);
    QVERIFY(bar.elevation() == md::Elevation::Level2);

    // The bar is half collapsed, so it is neither fully open nor fully closed.
    QVERIFY(bar.titleRect().width() >= 0.0);

    // Out-of-range offsets clamp instead of growing the bar.
    bar.setScrollOffset(500.0);
    QCOMPARE(bar.scrollOffset(), 100.0);
    QCOMPARE(bar.collapseFraction(), 1.0);
    QCOMPARE(collapsedSpy.count(), 1);
    QCOMPARE(collapsedSpy.at(0).at(0).toBool(), true);

    bar.setScrollOffset(-20.0);
    QCOMPARE(bar.scrollOffset(), 0.0);
    QCOMPARE(collapsedSpy.count(), 2);
    QCOMPARE(collapsedSpy.at(1).at(0).toBool(), false);

    // A zero range disables the collapse entirely.
    bar.setScrollRange(0.0);
    bar.setScrollOffset(50.0);
    QCOMPARE(bar.scrollOffset(), 0.0);
    QCOMPARE(bar.collapseFraction(), 0.0);
}

void TestLayouts::appBarSkipsHiddenActions()
{
    md::AppBar bar;
    bar.setTitle(QStringLiteral("Inbox"));

    Leaf nav(QSizeF(48.0, 48.0));
    Leaf first(QSizeF(48.0, 48.0));
    Leaf second(QSizeF(48.0, 48.0));
    bar.setNavigationButton(&nav);
    bar.addAction(&first);
    bar.addAction(&second);

    bar.measure(md::MeasureSpec::exactly(320.0), md::MeasureSpec::exactly(112.0));
    bar.arrange(QRect(0, 0, 320, 112));

    QCOMPARE(bar.actions().size(), 2);
    const qreal twoActionTitle = bar.titleRect().width();
    const int trailingEdge = 320 - 4;

    // Hiding an action removes it from the run: the remaining action is still
    // flush with the trailing edge (no gap where the hidden one used to be) and
    // the title takes the space back.
    second.hide();
    QCOMPARE(bar.actions().size(), 1);
    QVERIFY(bar.actions().at(0) == &first);

    bar.measure(md::MeasureSpec::exactly(320.0), md::MeasureSpec::exactly(112.0));
    bar.arrange(QRect(0, 0, 320, 112));
    QCOMPARE(first.x() + first.width(), trailingEdge);
    QVERIFY(bar.titleRect().width() > twoActionTitle);

    // Hiding the last action leaves an empty run, and the title keeps the span
    // between the navigation button and the trailing inset.
    first.hide();
    QVERIFY(bar.actions().isEmpty());

    bar.measure(md::MeasureSpec::exactly(320.0), md::MeasureSpec::exactly(112.0));
    bar.arrange(QRect(0, 0, 320, 112));
    QVERIFY(bar.titleRect().width() > twoActionTitle);
    QVERIFY(bar.titleRect().right() <= 320.0);

    // The whole bar still fits the axis it was given.
    QCOMPARE(bar.measuredSize(), QSizeF(320.0, 112.0));
}

void TestLayouts::appBarAnnouncesRangeClamp()
{
    md::AppBar bar;
    Leaf action(QSizeF(48.0, 48.0));
    bar.addAction(&action);

    bar.setScrollRange(100.0);
    bar.setScrollOffset(100.0);
    QCOMPARE(bar.collapseFraction(), 1.0);
    QCOMPARE(bar.currentHeight(), bar.collapsedHeight());

    QSignalSpy offsetSpy(&bar, &md::AppBar::offsetChanged);
    QSignalSpy collapsedSpy(&bar, &md::AppBar::collapsedChanged);

    // Shrinking the range below the offset repairs the offset through the
    // offset setter, so the clamp is announced and the collapse flag follows
    // the new fraction instead of contradicting it.
    bar.setScrollRange(0.0);
    QCOMPARE(bar.scrollOffset(), 0.0);
    QCOMPARE(bar.collapseFraction(), 0.0);
    QCOMPARE(bar.currentHeight(), bar.expandedHeight());
    QCOMPARE(offsetSpy.count(), 1);
    QCOMPARE(offsetSpy.at(0).at(0).toReal(), 0.0);
    QCOMPARE(collapsedSpy.count(), 1);
    QCOMPARE(collapsedSpy.at(0).at(0).toBool(), false);

    // Growing the range moves the fraction without moving the offset, so the
    // collapsed flag is re-derived even though the offset did not change.
    bar.setScrollRange(100.0);
    bar.setScrollOffset(100.0);
    QCOMPARE(collapsedSpy.count(), 2);
    QCOMPARE(collapsedSpy.at(1).at(0).toBool(), true);
    QCOMPARE(offsetSpy.count(), 2);

    bar.setScrollRange(200.0);
    QCOMPARE(bar.scrollOffset(), 100.0);
    QCOMPARE(bar.collapseFraction(), 0.5);
    QCOMPARE(collapsedSpy.count(), 3);
    QCOMPARE(collapsedSpy.at(2).at(0).toBool(), false);
    // The repair must not invent an offset change that did not happen.
    QCOMPARE(offsetSpy.count(), 2);

    QVERIFY(bar.currentHeight() > bar.collapsedHeight());
    QVERIFY(bar.currentHeight() < bar.expandedHeight());
}

void TestLayouts::appBarHonoursZeroHeightsAndClearedNavigation()
{
    md::AppBar bar;

    // Zero is a height, not a missing value: the old fallback stored 56, which
    // could make an explicitly shorter bar grow while it collapsed.
    bar.setExpandedHeight(40.0);
    bar.setCollapsedHeight(0.0);
    QCOMPARE(bar.expandedHeight(), 40.0);
    QCOMPARE(bar.collapsedHeight(), 0.0);
    QCOMPARE(bar.currentHeight(), 40.0);

    bar.setScrollRange(100.0);
    bar.setScrollOffset(100.0);
    QCOMPARE(bar.currentHeight(), 0.0);

    // Negative and non-finite heights are ignored instead of replaced by a
    // number the caller never asked for.
    bar.setExpandedHeight(-1.0);
    bar.setCollapsedHeight(std::numeric_limits<qreal>::quiet_NaN());
    QCOMPARE(bar.expandedHeight(), 40.0);
    QCOMPARE(bar.collapsedHeight(), 0.0);

    // Clearing the navigation role hides the button and keeps it out of the
    // action run; the former button never becomes a trailing action.
    md::AppBar navBar;
    Leaf nav(QSizeF(48.0, 48.0));
    Leaf action(QSizeF(48.0, 48.0));
    navBar.setNavigationButton(&nav);
    navBar.addAction(&action);

    navBar.measure(md::MeasureSpec::exactly(300.0), md::MeasureSpec::exactly(112.0));
    navBar.arrange(QRect(0, 0, 300, 112));
    QCOMPARE(navBar.actions().size(), 1);
    QVERIFY(!nav.isHidden());
    QCOMPARE(nav.geometry().left(), 4);

    navBar.setNavigationButton(nullptr);
    QVERIFY(navBar.navigationButton() == nullptr);
    QVERIFY(nav.isHidden());
    QCOMPARE(navBar.actions().size(), 1);
    QVERIFY(navBar.actions().at(0) == &action);

    navBar.measure(md::MeasureSpec::exactly(300.0), md::MeasureSpec::exactly(112.0));
    navBar.arrange(QRect(0, 0, 300, 112));
    QCOMPARE(action.x() + action.width(), 300 - 4);

    // Assigning the role again shows the button and places it at the leading
    // edge once more.
    navBar.setNavigationButton(&nav);
    QVERIFY(!nav.isHidden());
    navBar.measure(md::MeasureSpec::exactly(300.0), md::MeasureSpec::exactly(112.0));
    navBar.arrange(QRect(0, 0, 300, 112));
    QCOMPARE(nav.geometry().left(), 4);
    QCOMPARE(navBar.actions().size(), 1);
}

void TestLayouts::collapseExpandsWithSignal()
{
    md::Collapse section;
    section.setTitle(QStringLiteral("Details"));
    section.setHeaderHeight(48.0);

    Leaf body(QSizeF(120.0, 100.0));
    section.setContentWidget(&body);
    QVERIFY(section.contentWidget() == &body);
    section.resize(200, 48);
    section.measure(md::MeasureSpec::exactly(200.0), md::MeasureSpec::exactly(48.0));
    section.arrange(QRect(0, 0, 200, 48));

    QVERIFY(!section.isExpanded());
    QCOMPARE(section.expansion(), 0.0);
    QCOMPARE(section.headerRect().height(), 48.0);
    QVERIFY(body.isHidden());

    QSignalSpy expandedSpy(&section, &md::Collapse::expandedChanged);
    QSignalSpy finishedSpy(&section, &md::Collapse::animationFinished);

    section.setExpanded(true);
    QCOMPARE(expandedSpy.count(), 1);
    QCOMPARE(expandedSpy.at(0).at(0).toBool(), true);
    QVERIFY(section.isExpanded());
    QVERIFY(!body.isHidden());

    QTRY_VERIFY_WITH_TIMEOUT(section.expansion() > 0.99, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 2000);
    QCOMPARE(section.expansion(), 1.0);
    QCOMPARE(section.preferredHeight(), 148.0);

    // A standalone section owns its height, so the animation really grows it.
    QCOMPARE(section.height(), 148);
    QCOMPARE(body.geometry(), QRect(0, 48, 200, 100));

    section.setExpanded(false);
    QCOMPARE(expandedSpy.count(), 2);
    QCOMPARE(expandedSpy.at(1).at(0).toBool(), false);

    QTRY_VERIFY_WITH_TIMEOUT(section.expansion() < 0.01, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 2000);
    QVERIFY(body.isHidden());
    QCOMPARE(section.height(), 48);

    // A section that is already collapsed does not emit again.
    section.setExpanded(false);
    QCOMPARE(expandedSpy.count(), 2);

    // Without animation the transition snaps and still reports both signals.
    section.setAnimated(false);
    section.setContentWidget(&body);
    section.setExpanded(true);
    QCOMPARE(section.expansion(), 1.0);
    QCOMPARE(expandedSpy.count(), 3);
    QCOMPARE(finishedSpy.count(), 3);
}

void TestLayouts::collapseAcceptsZeroHeaderHeight()
{
    md::Collapse section;

    // Zero is a header height, not a missing value: the old fallback stored 48,
    // so a section asked for no header still reserved one.
    section.setHeaderHeight(0.0);
    QCOMPARE(section.headerHeight(), 0.0);
    QCOMPARE(section.preferredHeight(), 0.0);

    // Negative and non-finite heights are ignored, so the accepted zero is not
    // replaced by a fallback afterwards.
    section.setHeaderHeight(-8.0);
    section.setHeaderHeight(std::numeric_limits<qreal>::quiet_NaN());
    QCOMPARE(section.headerHeight(), 0.0);

    // The header row follows the stored zero instead of a 48-pixel default.
    section.measure(md::MeasureSpec::exactly(200.0), md::MeasureSpec::exactly(0.0));
    QCOMPARE(section.measuredSize(), QSizeF(200.0, 0.0));
    section.arrange(QRect(0, 0, 200, 0));
    QCOMPARE(section.headerRect(), QRectF(0.0, 0.0, 200.0, 0.0));
}

QTEST_MAIN(TestLayouts)
#include "tst_layouts.moc"
