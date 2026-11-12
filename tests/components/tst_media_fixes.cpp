// Regression tests for the "Carousel drag + Table overlap + Video controls"
// batch.
//
// The suite is offscreen and deterministic, like the other component suites:
// the application theme is pinned to baseline light, widgets are rendered into
// a transparent QImage with QWidget::render (QWidget::grab() leaves the pixels
// of a transparent widget undefined), and pointer input is delivered with
// QApplication::sendEvent.
//
// The table cases assert the overlap fix directly: a row may be scrolled
// behind the header, but the header band must contain no pixel darker than the
// header text, which is what a body glyph bleeding into it would produce. The
// geometry assertions (bodyRect(), rowRect(), cellRect()) are the stable half
// of the same contract.
//
// The video cases drive the documented transport API and the new scrub/hover
// state: position advances while playing, freezes while paused and while the
// progress thumb is held, and the control bar lays the track out between the
// time label and the volume button on a 56 dp bar.

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QRegion>
#include <QSignalSpy>
#include <QSize>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTest>
#include <QVariant>
#include <QWidget>

#include <md/components/carousel.h>
#include <md/components/table.h>
#include <md/components/video.h>
#include <md/components/virtual_table.h>
#include <md/structure/table.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>
#include <md/tokens/color_scheme.h>
#include <md/tokens/motion.h>
#include <md/view/view.h>

#include <algorithm>
#include <cmath>

namespace {

const md::ColorScheme& lightColours()
{
    return md::Theme::applicationTheme().colours();
}

// The transparent render used by the other component suites.
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

QColor pixelAt(const QImage& image, const QPointF& logical)
{
    const qreal ratio = image.devicePixelRatio();
    const int x = qBound(0, qRound(logical.x() * ratio), image.width() - 1);
    const int y = qBound(0, qRound(logical.y() * ratio), image.height() - 1);
    return image.pixelColor(x, y);
}

int tone(const QColor& colour)
{
    return colour.red() + colour.green() + colour.blue();
}

// How many pixels in the top `height` logical rows are dark enough to be body
// text. Header text (onSurfaceVariant) is lighter than body text (onSurface),
// so a threshold between the two counts only a bleed from the rows.
int bodyTextPixelsInBand(const QImage& image, qreal height)
{
    const qreal ratio = image.devicePixelRatio();
    const int limit = qMin(image.height(), qRound(height * ratio));
    const int threshold =
        (tone(lightColours().onSurface) + tone(lightColours().onSurfaceVariant)) / 2;

    int count = 0;
    for (int y = 0; y < limit; ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor colour = image.pixelColor(x, y);
            if (colour.alpha() == 0)
                continue;
            if (tone(colour) < threshold)
                ++count;
        }
    }

    return count;
}

void sendMouseAtGlobal(QWidget& widget, QEvent::Type type, const QPointF& global,
                       Qt::MouseButton button, Qt::MouseButtons buttons)
{
    const QPointF local = widget.mapFromGlobal(global);
    QMouseEvent event(type, local, global, button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

void sendMouse(QWidget& widget, QEvent::Type type, const QPointF& position,
               Qt::MouseButton button, Qt::MouseButtons buttons)
{
    sendMouseAtGlobal(widget, type, widget.mapToGlobal(position), button, buttons);
}

void clickAt(QWidget& widget, const QPointF& position)
{
    sendMouse(widget, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton);
    sendMouse(widget, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
}

void sendEnter(QWidget& widget)
{
    const QPointF point = widget.rect().center();
    const QPointF global = widget.mapToGlobal(point);
    QEnterEvent event(point, global, global);
    QApplication::sendEvent(&widget, &event);
}

void sendLeave(QWidget& widget)
{
    QEvent event(QEvent::Leave);
    QApplication::sendEvent(&widget, &event);
}

// A slide that counts the clicks it receives, so "a small movement is still a
// click on the slide" can be asserted instead of assumed.
class ClickableSlide : public md::View
{
public:
    explicit ClickableSlide(const QColor& colour = QColor(0xE8, 0xDE, 0xF8),
                            QWidget* parent = nullptr)
        : md::View(parent)
        , colour_(colour)
    {
        setBackgroundColour(QColor(Qt::transparent));
    }

    int presses = 0;
    int releases = 0;

protected:
    void measureOverride(md::MeasureSpec width, md::MeasureSpec height) override
    {
        setMeasuredSize(QSizeF(width.resolve(120.0), height.resolve(80.0)));
    }

    void paintContent(QPainter& painter, const QRectF& content) override
    {
        painter.fillRect(content, colour_);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        ++presses;
        md::View::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        ++releases;
        md::View::mouseReleaseEvent(event);
    }

private:
    QColor colour_;
};

// A drag from `fromX` to `toX` in the slide's local coordinates, delivered the
// way Qt delivers a real gesture: the global pointer path is fixed and the
// slide-local point is recomputed as the slide moves with the finger.
void dragSlide(QWidget& slide, qreal fromX, qreal toX, qreal y)
{
    const QPointF startGlobal = slide.mapToGlobal(QPointF(fromX, y));
    const QPointF endGlobal = startGlobal + QPointF(toX - fromX, 0.0);

    sendMouseAtGlobal(slide, QEvent::MouseButtonPress, startGlobal, Qt::LeftButton,
                      Qt::LeftButton);
    sendMouseAtGlobal(slide, QEvent::MouseMove, endGlobal, Qt::NoButton, Qt::LeftButton);
    sendMouseAtGlobal(slide, QEvent::MouseButtonRelease, endGlobal, Qt::LeftButton,
                      Qt::NoButton);
}

class CountingSource final : public md::structure::VirtualTableSource
{
public:
    [[nodiscard]] int rowCount() const override { return 1000; }
    [[nodiscard]] int columnCount() const override { return 5; }

    [[nodiscard]] QVariant cell(int row, int column) const override
    {
        return QStringLiteral("cell %1/%2").arg(row).arg(column);
    }

    [[nodiscard]] QString header(int column) const override
    {
        return column == 0 ? QStringLiteral("#") : QStringLiteral("Column %1").arg(column);
    }
};

void fillModel(QStandardItemModel& model, int rows = 20, int columns = 4)
{
    model.setRowCount(rows);
    model.setColumnCount(columns);
    model.setHorizontalHeaderLabels(
        {QStringLiteral("Header A"), QStringLiteral("Header B"),
         QStringLiteral("Header C"), QStringLiteral("Header D")});

    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            model.setItem(row, column,
                          new QStandardItem(QStringLiteral("row %1 col %2")
                                                .arg(row)
                                                .arg(column)));
        }
    }
}

} // namespace

class TestMediaFixes : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void carouselDragToNextPage();
    void carouselDragSnapsBack();
    void carouselClickIsNotADrag();
    void carouselDragPausesAutoPlay();
    void carouselDragReducedMotion();
    void carouselIndicatorFollowsTheDrag();

    void tableHeaderNeverShowsBodyText();
    void tableScrollKeepsTheHeader();
    void virtualTableHeaderAndFrozenColumn();

    void videoPlaybackAndPause();
    void videoProgressClickSeeks();
    void videoScrubPausesTheTicker();
    void videoControlsHoverAndLayout();

    // The icon-set rewrite: geometry, ink, states and the compact overflow.
    void videoControlGeometryKeepsTouchTargets();
    void videoEveryControlPaintsItsIcon();
    void videoPlayIconIsOpticallyCentred();
    void videoHoverPaintsAStateLayer();
    void videoDisabledControlsPaintFaintInk();
    void videoHiddenControlsPaintNoInk();
    void videoCompactBarOverflowsSecondaryControls();
    void videoFullscreenIconFollowsState();
    void videoControlNamesAndTooltips();
};

void TestMediaFixes::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestMediaFixes::init()
{
    // A reduced-motion test installs a scoped theme; every test starts from
    // the baseline one so ordering cannot leak it.
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

// ---------------------------------------------------------------------------
// Carousel
// ---------------------------------------------------------------------------

void TestMediaFixes::carouselDragToNextPage()
{
    md::Carousel carousel;
    ClickableSlide first;
    ClickableSlide second;
    ClickableSlide third;

    carousel.arrange(QRect(0, 0, 400, 240));
    carousel.show();
    carousel.setLoop(false);
    carousel.setSlides({&first, &second, &third});

    QCOMPARE(carousel.currentIndex(), 0);
    QVERIFY(carousel.slideArea().width() > 0.0);
    QVERIFY(second.isHidden());
    QVERIFY(third.isHidden());

    QSignalSpy changedSpy(&carousel, &md::Carousel::currentChanged);
    QSignalSpy dragSpy(&carousel, &md::Carousel::dragStateChanged);

    const qreal width = carousel.slideArea().width();
    const qreal y = carousel.slideArea().center().y();
    const QPointF startGlobal = first.mapToGlobal(QPointF(width * 0.85, y));
    const QPointF endGlobal = startGlobal - QPointF(width * 0.6, 0.0);

    sendMouseAtGlobal(first, QEvent::MouseButtonPress, startGlobal, Qt::LeftButton,
                      Qt::LeftButton);
    sendMouseAtGlobal(first, QEvent::MouseMove, endGlobal, Qt::NoButton, Qt::LeftButton);

    QVERIFY(carousel.isDragging());
    QCOMPARE(dragSpy.count(), 1);
    QCOMPARE(dragSpy.at(0).at(0).toBool(), true);
    QVERIFY(carousel.dragOffset() < -width * 0.5);
    QVERIFY(qAbs(first.pos().x() - qRound(carousel.dragOffset())) <= 1);

    // Both pages that are entering the viewport are on screen; the third one,
    // which the gesture never exposes, stays hidden.
    QVERIFY(!first.isHidden());
    QVERIFY(!second.isHidden());
    QVERIFY(third.isHidden());
    QCOMPARE(changedSpy.count(), 0);
    QCOMPARE(carousel.currentIndex(), 0);

    sendMouseAtGlobal(first, QEvent::MouseButtonRelease, endGlobal, Qt::LeftButton,
                      Qt::NoButton);

    QVERIFY(!carousel.isDragging());
    QCOMPARE(dragSpy.count(), 2);
    QCOMPARE(dragSpy.at(1).at(0).toBool(), false);
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(changedSpy.at(0).at(0).toInt(), 1);
    QCOMPARE(carousel.currentIndex(), 1);

    // The commit is animating; finishing it settles the arrangement.
    carousel.finishAnimation();

    QCOMPARE(carousel.currentIndex(), 1);
    QCOMPARE(carousel.dragOffset(), 0.0);
    QVERIFY(!carousel.isAnimating());
    QCOMPARE(second.geometry(), carousel.slideArea().toRect());
    QVERIFY(!second.isHidden());
    QVERIFY(first.isHidden());
    QVERIFY(third.isHidden());

    // The click that started the gesture never reached the slide.
    QCOMPARE(first.releases, 0);
    QCOMPARE(first.presses, 1);
}

void TestMediaFixes::carouselDragSnapsBack()
{
    md::Carousel carousel;
    ClickableSlide first;
    ClickableSlide second;

    carousel.arrange(QRect(0, 0, 400, 240));
    carousel.show();
    carousel.setLoop(false);
    carousel.setSlides({&first, &second});

    QSignalSpy changedSpy(&carousel, &md::Carousel::currentChanged);
    QSignalSpy dragSpy(&carousel, &md::Carousel::dragStateChanged);

    const qreal width = carousel.slideArea().width();
    const qreal y = carousel.slideArea().center().y();

    const QPointF startGlobal = first.mapToGlobal(QPointF(width * 0.5, y));
    const QPointF endGlobal = startGlobal - QPointF(width * 0.1, 0.0);

    sendMouseAtGlobal(first, QEvent::MouseButtonPress, startGlobal, Qt::LeftButton,
                      Qt::LeftButton);
    sendMouseAtGlobal(first, QEvent::MouseMove, endGlobal, Qt::NoButton, Qt::LeftButton);

    QVERIFY(carousel.isDragging());
    QVERIFY(carousel.dragOffset() < 0.0);
    QVERIFY(carousel.dragOffset() > -width * 0.35);

    sendMouseAtGlobal(first, QEvent::MouseButtonRelease, endGlobal, Qt::LeftButton,
                      Qt::NoButton);

    QVERIFY(!carousel.isDragging());
    QCOMPARE(dragSpy.count(), 2);
    QCOMPARE(changedSpy.count(), 0);
    QCOMPARE(carousel.currentIndex(), 0);

    carousel.finishAnimation();

    QCOMPARE(carousel.dragOffset(), 0.0);
    QCOMPARE(carousel.currentIndex(), 0);
    QCOMPARE(first.geometry(), carousel.slideArea().toRect());
    QVERIFY(!first.isHidden());
    QVERIFY(second.isHidden());

    // A backwards drag on the first slide is elastic: it moves, but can never
    // expose a slide before the first one.
    const QPointF backStart = first.mapToGlobal(QPointF(width * 0.2, y));
    const QPointF backEnd = backStart + QPointF(width, 0.0);

    sendMouseAtGlobal(first, QEvent::MouseButtonPress, backStart, Qt::LeftButton,
                      Qt::LeftButton);
    sendMouseAtGlobal(first, QEvent::MouseMove, backEnd, Qt::NoButton, Qt::LeftButton);

    QVERIFY(carousel.isDragging());
    QVERIFY(carousel.dragOffset() > 0.0);
    QVERIFY(carousel.dragOffset() < width * 0.5);

    sendMouseAtGlobal(first, QEvent::MouseButtonRelease, backEnd, Qt::LeftButton,
                      Qt::NoButton);
    carousel.finishAnimation();

    QCOMPARE(carousel.currentIndex(), 0);
    QCOMPARE(carousel.dragOffset(), 0.0);
}

void TestMediaFixes::carouselClickIsNotADrag()
{
    md::Carousel carousel;
    ClickableSlide first;
    ClickableSlide second;

    carousel.arrange(QRect(0, 0, 400, 240));
    carousel.show();
    carousel.setLoop(false);
    carousel.setSlides({&first, &second});

    QSignalSpy dragSpy(&carousel, &md::Carousel::dragStateChanged);
    QSignalSpy changedSpy(&carousel, &md::Carousel::currentChanged);

    const QPointF point(180.0, 100.0);
    clickAt(first, point);

    QCOMPARE(first.presses, 1);
    QCOMPARE(first.releases, 1);
    QCOMPARE(dragSpy.count(), 0);
    QCOMPARE(changedSpy.count(), 0);
    QVERIFY(!carousel.isDragging());
    QCOMPARE(carousel.currentIndex(), 0);

    // A wobble below the touch slop stays a click as well.
    const QPointF startGlobal = first.mapToGlobal(point);
    sendMouseAtGlobal(first, QEvent::MouseButtonPress, startGlobal, Qt::LeftButton,
                      Qt::LeftButton);
    sendMouseAtGlobal(first, QEvent::MouseMove, startGlobal + QPointF(2.0, 1.0),
                      Qt::NoButton, Qt::LeftButton);
    sendMouseAtGlobal(first, QEvent::MouseButtonRelease, startGlobal + QPointF(2.0, 1.0),
                      Qt::LeftButton, Qt::NoButton);

    QCOMPARE(first.presses, 2);
    QCOMPARE(first.releases, 2);
    QCOMPARE(dragSpy.count(), 0);
    QCOMPARE(changedSpy.count(), 0);
    QVERIFY(!carousel.isDragging());
}

void TestMediaFixes::carouselDragPausesAutoPlay()
{
    md::Carousel carousel;
    ClickableSlide first;
    ClickableSlide second;
    ClickableSlide third;

    carousel.arrange(QRect(0, 0, 400, 240));
    carousel.show();
    carousel.setLoop(true);
    carousel.setSlides({&first, &second, &third});

    // Instant transitions keep the auto-play observation independent of the
    // 250 ms snap timer.
    carousel.setDuration(0);
    carousel.setInterval(60);
    carousel.setAutoPlay(true);

    QTRY_VERIFY_WITH_TIMEOUT(carousel.currentIndex() != 0, 2000);
    carousel.finishAnimation();

    const int beforeDrag = carousel.currentIndex();
    auto* slide = static_cast<ClickableSlide*>(
        carousel.slides().value(carousel.currentIndex()));
    QVERIFY(slide != nullptr);

    const qreal width = carousel.slideArea().width();
    const qreal y = carousel.slideArea().center().y();
    const QPointF startGlobal = slide->mapToGlobal(QPointF(width * 0.5, y));
    const QPointF holdGlobal = startGlobal - QPointF(width * 0.2, 0.0);

    sendMouseAtGlobal(*slide, QEvent::MouseButtonPress, startGlobal, Qt::LeftButton,
                      Qt::LeftButton);
    sendMouseAtGlobal(*slide, QEvent::MouseMove, holdGlobal, Qt::NoButton, Qt::LeftButton);
    QVERIFY(carousel.isDragging());

    // The hold is longer than several auto-play periods; the index must not
    // move while the gesture owns the slides.
    QTest::qWait(320);
    QCOMPARE(carousel.currentIndex(), beforeDrag);
    QVERIFY(carousel.isDragging());

    // Releasing below the threshold springs back, and the carousel resumes
    // advancing on its own.
    sendMouseAtGlobal(*slide, QEvent::MouseButtonRelease, holdGlobal, Qt::LeftButton,
                      Qt::NoButton);
    QVERIFY(!carousel.isDragging());
    carousel.finishAnimation();

    QTRY_VERIFY_WITH_TIMEOUT(carousel.currentIndex() != beforeDrag, 2000);

    carousel.setAutoPlay(false);
}

void TestMediaFixes::carouselDragReducedMotion()
{
    md::Carousel carousel;
    ClickableSlide first;
    ClickableSlide second;
    ClickableSlide third;

    carousel.arrange(QRect(0, 0, 400, 240));
    carousel.show();
    carousel.setLoop(false);
    carousel.setSlides({&first, &second, &third});

    md::MotionScheme reduced;
    reduced.reducedMotion = true;
    md::ThemeScope* scope =
        md::ThemeScope::install(&carousel, md::Theme::baselineLight().withMotion(reduced));
    QVERIFY(scope != nullptr);
    QVERIFY(md::Theme::forWidget(&carousel).motion().reducedMotion);

    QSignalSpy changedSpy(&carousel, &md::Carousel::currentChanged);

    const qreal width = carousel.slideArea().width();
    const qreal y = carousel.slideArea().center().y();

    dragSlide(first, width * 0.85, width * 0.25, y);

    // No timer is left running: the release settled in one step.
    QVERIFY(!carousel.isAnimating());
    QCOMPARE(carousel.currentIndex(), 1);
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(carousel.dragOffset(), 0.0);
    QCOMPARE(carousel.slideArea(), second.geometry());
    QVERIFY(first.isHidden());
    QVERIFY(!second.isHidden());
}

void TestMediaFixes::carouselIndicatorFollowsTheDrag()
{
    md::Carousel carousel;
    ClickableSlide first;
    ClickableSlide second;
    ClickableSlide third;

    carousel.arrange(QRect(0, 0, 400, 240));
    carousel.show();
    carousel.setLoop(false);
    carousel.setSlides({&first, &second, &third});

    const qreal width = carousel.slideArea().width();
    const qreal y = carousel.slideArea().center().y();

    dragSlide(first, width * 0.85, width * 0.15, y);
    carousel.finishAnimation();

    QCOMPARE(carousel.currentIndex(), 1);

    const QImage rendered = renderWidget(carousel);
    const QColor active = pixelAt(rendered, carousel.indicatorRect(1).center());
    const QColor idle = pixelAt(rendered, carousel.indicatorRect(0).center());

    QCOMPARE(active.rgb(), lightColours().primary.rgb());
    QCOMPARE(idle.rgb(), lightColours().outlineVariant.rgb());
}

// ---------------------------------------------------------------------------
// Table / VirtualTable
// ---------------------------------------------------------------------------

void TestMediaFixes::tableHeaderNeverShowsBodyText()
{
    QStandardItemModel model;
    fillModel(model);

    md::Table table;
    table.setModel(&model);
    table.setRowHeight(40.0);
    for (int column = 0; column < 4; ++column)
        table.setColumnWidth(column, 100.0);
    table.resize(420, 300);

    QCOMPARE(table.headerRect().height(), 48.0);
    QCOMPARE(table.headerRect().top(), 0.0);
    QCOMPARE(table.bodyRect().top(), 48.0);
    QCOMPARE(table.bodyRect().top(), table.headerRect().bottom());
    QVERIFY(table.rowRect(0).top() >= table.headerRect().bottom());
    QCOMPARE(table.rowRect(0).top(), 48.0);
    QCOMPARE(table.cellRect(0, 0).top(), 48.0);

    const QImage rendered = renderWidget(table);
    QCOMPARE(bodyTextPixelsInBand(rendered, table.headerRect().height()), 0);
    QCOMPARE(pixelAt(rendered, QPointF(5.0, 5.0)).rgb(),
             lightColours().surfaceContainer.rgb());

    // The header is drawn and the body is not empty, so the band would see a
    // body glyph if the clip were missing.
    QCOMPARE(pixelAt(rendered, QPointF(5.0, table.rowRect(0).top() + 8.0)).rgb(),
             lightColours().surface.rgb());
}

void TestMediaFixes::tableScrollKeepsTheHeader()
{
    QStandardItemModel model;
    fillModel(model);

    md::Table table;
    table.setModel(&model);
    table.setRowHeight(40.0);
    for (int column = 0; column < 4; ++column)
        table.setColumnWidth(column, 100.0);
    table.resize(420, 300);

    table.setScrollOffset(60.0);

    // Row 1 starts above the header's bottom edge: it is the row the clip has
    // to hide.
    QCOMPARE(table.firstVisibleRow(), 1);
    QVERIFY(table.rowRect(1).top() < table.headerRect().bottom());

    const QImage rendered = renderWidget(table);
    QCOMPARE(bodyTextPixelsInBand(rendered, table.headerRect().height()), 0);
    QCOMPARE(pixelAt(rendered, QPointF(5.0, 5.0)).rgb(),
             lightColours().surfaceContainer.rgb());

    // The header does not move with the rows.
    QCOMPARE(table.headerRect().top(), 0.0);
    QCOMPARE(table.headerRect().height(), 48.0);

    // Neither does it disappear at the end of the scroll.
    table.setScrollOffset(100'000.0);
    QVERIFY(table.scrollOffset() > 0.0);
    const QImage scrolled = renderWidget(table);
    QCOMPARE(bodyTextPixelsInBand(scrolled, table.headerRect().height()), 0);
    QCOMPARE(pixelAt(scrolled, QPointF(5.0, 5.0)).rgb(),
             lightColours().surfaceContainer.rgb());
}

void TestMediaFixes::virtualTableHeaderAndFrozenColumn()
{
    CountingSource source;
    md::VirtualTable table;
    table.setSource(&source);
    table.setRowHeight(40.0);
    for (int column = 0; column < 5; ++column)
        table.setColumnWidth(column, 100.0);
    table.resize(420, 300);
    table.setFrozenColumns(1);

    QCOMPARE(table.headerRect().height(), 48.0);
    QCOMPARE(table.bodyRect().top(), 48.0);
    QCOMPARE(table.rowRect(0).top(), 48.0);
    QCOMPARE(table.cellRect(0, 0).top(), 48.0);

    // The frozen column keeps the body's left edge; only the scrolling columns
    // move under it.
    const QRectF body = table.bodyRect();
    QCOMPARE(table.cellRect(0, 0).left(), body.left());
    QCOMPARE(table.headerRect().left(), body.left());

    table.setHorizontalScrollOffset(50.0);
    QCOMPARE(table.cellRect(0, 0).left(), body.left());
    QCOMPARE(table.cellRect(0, 1).left(), body.left() + 100.0 - 50.0);
    QCOMPARE(table.columnRect(0).top(), body.top());

    const QImage rendered = renderWidget(table);
    QCOMPARE(bodyTextPixelsInBand(rendered, table.headerRect().height()), 0);
    QCOMPARE(pixelAt(rendered, QPointF(5.0, 5.0)).rgb(),
             lightColours().surfaceContainer.rgb());

    // Scrolling the rows puts a row behind the header; the header band still
    // contains no body text.
    table.setScrollOffset(60.0);
    QCOMPARE(table.firstVisibleRow(), 1);
    QVERIFY(table.rowRect(1).top() < table.headerRect().bottom());
    const QImage scrolledRows = renderWidget(table);
    QCOMPARE(bodyTextPixelsInBand(scrolledRows, table.headerRect().height()), 0);
    QCOMPARE(pixelAt(scrolledRows, QPointF(5.0, 5.0)).rgb(),
             lightColours().surfaceContainer.rgb());
}

// ---------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------

void TestMediaFixes::videoPlaybackAndPause()
{
    md::Video video;
    video.resize(400, 300);
    video.setDuration(60'000);

    video.play();
    QVERIFY(video.state() == md::Video::PlaybackState::Playing);
    QTRY_VERIFY_WITH_TIMEOUT(video.position() > 0, 1500);

    video.pause();
    QVERIFY(video.state() == md::Video::PlaybackState::Paused);

    const qint64 pausedAt = video.position();
    QVERIFY(pausedAt > 0);
    QTest::qWait(90);
    QCOMPARE(video.position(), pausedAt);

    video.play();
    QTRY_VERIFY_WITH_TIMEOUT(video.position() > pausedAt, 1500);
    video.stop();
    QCOMPARE(video.position(), 0);
}

void TestMediaFixes::videoProgressClickSeeks()
{
    md::Video video;
    video.resize(400, 300);
    video.setDuration(10'000);
    video.setBuffered(6'000);
    QCOMPARE(video.buffered(), 6'000);

    const QRectF track = video.progressRect();
    QVERIFY(!track.isEmpty());
    QVERIFY(track.width() > 40.0);

    // The track sits inside the control bar, to the right of the time label
    // and to the left of the volume button.
    const QRectF bar = video.controlBarRect();
    QCOMPARE(bar.height(), 56.0);
    QVERIFY(bar.top() <= track.top());
    QVERIFY(bar.bottom() >= track.bottom());
    QVERIFY(track.left() >= video.timeLabelRect().right());
    QVERIFY(track.right() <= video.volumeButtonRect().left());
    QVERIFY(!track.intersects(video.playButtonRect()));
    QVERIFY(bar.contains(video.playButtonRect().center()));
    QVERIFY(bar.contains(video.volumeButtonRect().center()));
    QVERIFY(bar.contains(video.fullscreenButtonRect().center()));
    QVERIFY(!video.timeLabelRect().intersects(video.progressRect()));

    // A click on the track seeks to that fraction, within one 16 ms frame.
    clickAt(video, QPointF(track.left() + track.width() * 0.4, track.center().y()));
    QVERIFY2(std::abs(video.position() - 4'000) <= 16,
             qPrintable(QStringLiteral("position %1").arg(video.position())));

    // And so does a drag: press, move along the track, release.
    const QPointF start(track.left() + track.width() * 0.2, track.center().y());
    const QPointF finish(track.left() + track.width() * 0.75, track.center().y());
    sendMouse(video, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    QVERIFY(video.isScrubbing());
    sendMouse(video, QEvent::MouseMove, finish, Qt::NoButton, Qt::LeftButton);
    QVERIFY2(std::abs(video.position() - 7'500) <= 16,
             qPrintable(QStringLiteral("position %1").arg(video.position())));
    sendMouse(video, QEvent::MouseButtonRelease, finish, Qt::LeftButton, Qt::NoButton);
    QVERIFY(!video.isScrubbing());
    QVERIFY(std::abs(video.position() - 7'500) <= 16);
}

void TestMediaFixes::videoScrubPausesTheTicker()
{
    md::Video video;
    video.resize(400, 300);
    video.setDuration(60'000);

    video.play();
    QTRY_VERIFY_WITH_TIMEOUT(video.position() > 0, 1500);

    const QRectF track = video.progressRect();
    const QPointF grab(track.left() + track.width() * 0.3, track.center().y());

    sendMouse(video, QEvent::MouseButtonPress, grab, Qt::LeftButton, Qt::LeftButton);
    QVERIFY(video.isScrubbing());
    QVERIFY(video.state() == md::Video::PlaybackState::Playing);

    const qint64 held = video.position();
    QTest::qWait(120);

    // Playback is still "playing", but the ticker is suspended for the seek.
    QCOMPARE(video.position(), held);

    const QPointF target(track.left() + track.width() * 0.6, track.center().y());
    sendMouse(video, QEvent::MouseMove, target, Qt::NoButton, Qt::LeftButton);
    QVERIFY(std::abs(video.position() - 36'000) <= 16);

    sendMouse(video, QEvent::MouseButtonRelease, target, Qt::LeftButton, Qt::NoButton);
    QVERIFY(!video.isScrubbing());

    // Releasing resumes the ticker.
    QTRY_VERIFY_WITH_TIMEOUT(video.position() > 36'000, 1500);
    video.pause();
}

void TestMediaFixes::videoControlsHoverAndLayout()
{
    md::Video video;
    video.resize(480, 320);
    video.setDuration(10'000);

    // A stopped surface always shows its transport.
    QVERIFY(video.controlsVisible());

    const QRectF bar = video.controlBarRect();
    QCOMPARE(bar.height(), 56.0);
    QCOMPARE(bar.bottom(), qreal(video.height()));

    // The control bar grows a scrim above it rather than a hard black strip:
    // the bar's own top edge is inside the widget.
    QVERIFY(bar.top() > 0.0);

    // Playing with the pointer elsewhere fades them out.
    video.play();
    QTRY_VERIFY_WITH_TIMEOUT(!video.controlsVisible(), 1500);
    QVERIFY(!video.controlsVisible());

    // Hovering fades them back in.
    sendEnter(video);
    QTRY_VERIFY_WITH_TIMEOUT(video.controlsVisible(), 1500);

    sendLeave(video);
    QTRY_VERIFY_WITH_TIMEOUT(!video.controlsVisible(), 1500);

    // Pausing keeps them on screen, even after the pointer leaves.
    video.pause();
    QTRY_VERIFY_WITH_TIMEOUT(video.controlsVisible(), 1500);
    sendLeave(video);
    QTest::qWait(150);
    QVERIFY(video.controlsVisible());

    video.stop();
}

// ---------------------------------------------------------------------------
// Video: the icon set
// ---------------------------------------------------------------------------
//
// The bar paints the light on-scrim ink over the black letterbox in the
// baseline light theme, so "ink" is a brightness test and not a colour
// comparison. Each case below reads the rendered pixels back, because the
// complaint this batch answers was visual: the geometry can be right while the
// glyphs are not.

namespace {

int inkPixelsInRect(const QImage& image, const QRectF& rect, int minimumTone = 400)
{
    const qreal ratio = image.devicePixelRatio();
    const int left =
        qBound(0, static_cast<int>(std::floor(rect.left() * ratio)), image.width() - 1);
    const int right =
        qBound(left, static_cast<int>(std::ceil(rect.right() * ratio)), image.width());
    const int top =
        qBound(0, static_cast<int>(std::floor(rect.top() * ratio)), image.height() - 1);
    const int bottom =
        qBound(top, static_cast<int>(std::ceil(rect.bottom() * ratio)), image.height());

    int count = 0;
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            const QColor colour = image.pixelColor(x, y);
            if (colour.alpha() > 0 && tone(colour) >= minimumTone)
                ++count;
        }
    }
    return count;
}

qreal inkCentroidX(const QImage& image, const QRectF& rect, int minimumTone = 400)
{
    const qreal ratio = image.devicePixelRatio();
    const int left =
        qBound(0, static_cast<int>(std::floor(rect.left() * ratio)), image.width() - 1);
    const int right =
        qBound(left, static_cast<int>(std::ceil(rect.right() * ratio)), image.width());
    const int top =
        qBound(0, static_cast<int>(std::floor(rect.top() * ratio)), image.height() - 1);
    const int bottom =
        qBound(top, static_cast<int>(std::ceil(rect.bottom() * ratio)), image.height());

    qreal sum = 0.0;
    int count = 0;
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            const QColor colour = image.pixelColor(x, y);
            if (colour.alpha() == 0 || tone(colour) < minimumTone)
                continue;
            sum += (static_cast<qreal>(x) + 0.5) / ratio;
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<qreal>(count) : -1.0;
}

int peakToneInRect(const QImage& image, const QRectF& rect)
{
    const qreal ratio = image.devicePixelRatio();
    const int left =
        qBound(0, static_cast<int>(std::floor(rect.left() * ratio)), image.width() - 1);
    const int right =
        qBound(left, static_cast<int>(std::ceil(rect.right() * ratio)), image.width());
    const int top =
        qBound(0, static_cast<int>(std::floor(rect.top() * ratio)), image.height() - 1);
    const int bottom =
        qBound(top, static_cast<int>(std::ceil(rect.bottom() * ratio)), image.height());

    int peak = 0;
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            const QColor colour = image.pixelColor(x, y);
            if (colour.alpha() > 0)
                peak = std::max(peak, tone(colour));
        }
    }
    return peak;
}

int differingPixelsInRect(const QImage& lhs, const QImage& rhs, const QRectF& rect,
                          int tolerance = 60)
{
    const qreal ratio = lhs.devicePixelRatio();
    const int left =
        qBound(0, static_cast<int>(std::floor(rect.left() * ratio)), lhs.width() - 1);
    const int right =
        qBound(left, static_cast<int>(std::ceil(rect.right() * ratio)), lhs.width());
    const int top =
        qBound(0, static_cast<int>(std::floor(rect.top() * ratio)), lhs.height() - 1);
    const int bottom =
        qBound(top, static_cast<int>(std::ceil(rect.bottom() * ratio)), lhs.height());

    int count = 0;
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            const int difference =
                std::abs(tone(lhs.pixelColor(x, y)) - tone(rhs.pixelColor(x, y)));
            if (difference > tolerance)
                ++count;
        }
    }
    return count;
}

} // namespace

void TestMediaFixes::videoControlGeometryKeepsTouchTargets()
{
    md::Video video;
    video.resize(800, 450);
    video.setDuration(60'000);

    // The wide bar carries the full transport, left to right.
    const md::Video::Control order[] = {
        md::Video::Control::PlayPause,   md::Video::Control::Replay,
        md::Video::Control::SkipBack,    md::Video::Control::SkipForward,
        md::Video::Control::Volume,      md::Video::Control::Settings,
        md::Video::Control::Fullscreen,
    };
    const int expected = static_cast<int>(sizeof(order) / sizeof(order[0]));
    QCOMPARE(video.controlCount(), expected);

    const QRectF bar = video.controlBarRect();
    QCOMPARE(bar.height(), 56.0);

    for (int i = 0; i < expected; ++i) {
        const QRectF rect = video.controlRect(i);
        QVERIFY2(!rect.isEmpty(), qPrintable(QStringLiteral("control %1").arg(i)));
        QCOMPARE(rect.width(), 48.0);
        QCOMPARE(rect.height(), 48.0);
        QVERIFY(bar.contains(rect));
        QCOMPARE(video.controlRect(order[i]), rect);
        QCOMPARE(video.controlAt(rect.center()), order[i]);
    }

    // The three accessors the older tests use are the new geometry, not a copy.
    QCOMPARE(video.controlRect(md::Video::Control::PlayPause), video.playButtonRect());
    QCOMPARE(video.controlRect(md::Video::Control::Volume), video.volumeButtonRect());
    QCOMPARE(video.controlRect(md::Video::Control::Fullscreen), video.fullscreenButtonRect());

    // 12 dp to either edge and 8 dp between neighbours.
    QCOMPARE(video.playButtonRect().left(), bar.left() + 12.0);
    QCOMPARE(video.fullscreenButtonRect().right(), bar.right() - 12.0);
    QCOMPARE(video.controlRect(md::Video::Control::Replay).left(),
             video.playButtonRect().right() + 8.0);
    QCOMPARE(video.controlRect(md::Video::Control::SkipBack).left(),
             video.controlRect(md::Video::Control::Replay).right() + 8.0);
    QCOMPARE(video.controlRect(md::Video::Control::SkipForward).left(),
             video.controlRect(md::Video::Control::SkipBack).right() + 8.0);
    QCOMPARE(video.controlRect(md::Video::Control::Settings).left(),
             video.controlRect(md::Video::Control::Volume).right() + 8.0);
    QCOMPARE(video.controlRect(md::Video::Control::Fullscreen).left(),
             video.controlRect(md::Video::Control::Settings).right() + 8.0);

    // The track lives between the time label and the volume target.
    QVERIFY(!video.progressRect().isEmpty());
    QVERIFY(video.progressRect().left() >= video.timeLabelRect().right());
    QVERIFY(video.progressRect().right() <= video.volumeButtonRect().left());
}

void TestMediaFixes::videoEveryControlPaintsItsIcon()
{
    md::Video video;
    video.resize(800, 450);
    video.setDuration(10'000);

    const QImage image = renderWidget(video);
    const md::Video::Control controls[] = {
        md::Video::Control::PlayPause,   md::Video::Control::Replay,
        md::Video::Control::SkipBack,    md::Video::Control::SkipForward,
        md::Video::Control::Volume,      md::Video::Control::Settings,
        md::Video::Control::Fullscreen,
    };

    for (md::Video::Control control : controls) {
        const QRectF rect = video.controlRect(control);
        QVERIFY(!rect.isEmpty());
        QVERIFY2(inkPixelsInRect(image, rect) > 30,
                 qPrintable(QStringLiteral("control %1 has no ink")
                                .arg(static_cast<int>(control))));

        // 24 dp glyph inside a 48 dp target: the outer 12 dp ring stays empty.
        QCOMPARE(inkPixelsInRect(image,
                                 QRectF(rect.left(), rect.top(), rect.width(), 10.0)),
                 0);
        QCOMPARE(inkPixelsInRect(image,
                                 QRectF(rect.left(), rect.bottom() - 10.0, rect.width(),
                                        10.0)),
                 0);
    }
}

void TestMediaFixes::videoPlayIconIsOpticallyCentred()
{
    md::Video video;
    video.resize(800, 450);
    video.setDuration(10'000);

    const QRectF play = video.playButtonRect();
    QVERIFY(!play.isEmpty());

    // A filled triangle reads as centred when its ink centre of mass sits on the
    // target centre, not when its bounding box does.
    const qreal triangle = inkCentroidX(renderWidget(video), play);
    QVERIFY(triangle > 0.0);
    QVERIFY2(std::abs(triangle - play.center().x()) <= 1.6,
             qPrintable(QStringLiteral("play centroid %1, centre %2")
                            .arg(triangle)
                            .arg(play.center().x())));

    // The pause bars are symmetric, so they are centred exactly.
    video.play();
    const qreal bars = inkCentroidX(renderWidget(video), play);
    QVERIFY(bars > 0.0);
    QVERIFY2(std::abs(bars - play.center().x()) <= 0.75,
             qPrintable(QStringLiteral("pause centroid %1, centre %2")
                            .arg(bars)
                            .arg(play.center().x())));
    video.pause();
}

void TestMediaFixes::videoHoverPaintsAStateLayer()
{
    md::Video video;
    video.resize(800, 450);
    video.setDuration(60'000);

    const QRectF play = video.playButtonRect();
    // Inside the 40 dp state layer, outside the 24 dp glyph.
    const QPointF probe = play.center() + QPointF(12.0, 12.0);

    const QColor resting = pixelAt(renderWidget(video), probe);
    QCOMPARE(resting.alpha(), 255);
    QVERIFY(tone(resting) < 120);

    sendMouse(video, QEvent::MouseMove, probe, Qt::NoButton, Qt::NoButton);
    QCOMPARE(video.hoveredControl(), md::Video::Control::PlayPause);

    const QColor hovered = pixelAt(renderWidget(video), probe);
    QVERIFY2(tone(hovered) > tone(resting) + 60,
             qPrintable(QStringLiteral("resting %1, hovered %2")
                            .arg(tone(resting))
                            .arg(tone(hovered))));

    // Leaving the surface drops both the layer and the hovered action.
    sendLeave(video);
    QCOMPARE(video.hoveredControl(), md::Video::Control::None);
    QCOMPARE(pixelAt(renderWidget(video), probe), resting);
}

void TestMediaFixes::videoDisabledControlsPaintFaintInk()
{
    md::Video video;
    video.resize(800, 450);
    video.setDuration(10'000);

    const QRectF play = video.playButtonRect();
    const QImage enabled = renderWidget(video);
    QVERIFY(inkPixelsInRect(enabled, play) > 30);
    const int enabledPeak = peakToneInRect(enabled, play);
    QVERIFY(enabledPeak > 600);

    video.setEnabled(false);
    const QImage disabled = renderWidget(video);

    // The glyph stays, at the 38% disabled emphasis ...
    QVERIFY2(inkPixelsInRect(disabled, play, 120) > 0,
             "a disabled action still paints its glyph");
    const int disabledPeak = peakToneInRect(disabled, play);
    QVERIFY(disabledPeak > 0);
    QVERIFY2(disabledPeak <= enabledPeak * 45 / 100,
             qPrintable(QStringLiteral("disabled peak %1 vs enabled %2")
                            .arg(disabledPeak)
                            .arg(enabledPeak)));

    // ... and the hover state layer is suppressed: a disabled control never
    // paints the interactive ink.
    sendMouse(video, QEvent::MouseMove, play.center(), Qt::NoButton, Qt::NoButton);
    QCOMPARE(video.hoveredControl(), md::Video::Control::None);
    QCOMPARE(renderWidget(video), disabled);
}

void TestMediaFixes::videoHiddenControlsPaintNoInk()
{
    md::Video video;
    video.resize(480, 320);
    video.setDuration(60'000);
    video.play();

    // Playing with the pointer elsewhere fades the whole bar out; a hidden
    // control must paint nothing, not its ink at zero opacity.
    QTRY_VERIFY_WITH_TIMEOUT(!video.controlsVisible(), 1500);
    QVERIFY(!video.controlsVisible());

    const QImage image = renderWidget(video);
    QCOMPARE(inkPixelsInRect(image, video.playButtonRect()), 0);
    QCOMPARE(inkPixelsInRect(image, video.controlRect(md::Video::Control::Volume)), 0);
    QCOMPARE(inkPixelsInRect(image, video.controlRect(md::Video::Control::Fullscreen)), 0);
}

void TestMediaFixes::videoCompactBarOverflowsSecondaryControls()
{
    md::Video video;
    video.resize(400, 300);
    video.setDuration(60'000);
    video.setPosition(30'000);

    // The bar keeps the primary transport and drops replay and the seeks into
    // the settings panel; the targets that remain are still a full 48 dp.
    QCOMPARE(video.controlCount(), 4);
    QVERIFY(video.controlRect(md::Video::Control::Replay).isEmpty());
    QVERIFY(video.controlRect(md::Video::Control::SkipBack).isEmpty());
    QVERIFY(video.controlRect(md::Video::Control::SkipForward).isEmpty());

    for (int i = 0; i < video.controlCount(); ++i) {
        const QRectF rect = video.controlRect(i);
        QCOMPARE(rect.width(), 48.0);
        QCOMPARE(rect.height(), 48.0);
        QVERIFY(video.controlBarRect().contains(rect));
    }

    const QRectF settings = video.controlRect(md::Video::Control::Settings);
    QVERIFY(!settings.isEmpty());
    clickAt(video, settings.center());
    QVERIFY(video.overflowOpen());
    QCOMPARE(video.overflowItemCount(), 4);

    for (int i = 0; i < video.overflowItemCount(); ++i) {
        const QRectF item = video.overflowItemRect(i);
        QVERIFY(!item.isEmpty());
        QVERIFY(item.top() >= 0.0);
        QVERIFY(item.bottom() <= video.height());
        QVERIFY(item.left() >= 0.0);
        QVERIFY(item.right() <= video.width());
    }

    // The replay row restarts the clip.
    clickAt(video, video.overflowItemRect(0).center());
    QVERIFY(!video.overflowOpen());
    QVERIFY2(video.position() < 100, qPrintable(QStringLiteral("position %1")
                                                    .arg(video.position())));
    QVERIFY(video.state() == md::Video::PlaybackState::Playing);
    video.pause();

    // The last row toggles loop.
    clickAt(video, settings.center());
    QVERIFY(video.overflowOpen());
    clickAt(video, video.overflowItemRect(3).center());
    QVERIFY(!video.overflowOpen());
    QVERIFY(video.loop());

    // A click that dismisses the panel is not also an action on the bar.
    clickAt(video, settings.center());
    QVERIFY(video.overflowOpen());
    const qint64 held = video.position();
    clickAt(video, QPointF(10.0, 10.0));
    QVERIFY(!video.overflowOpen());
    QCOMPARE(video.position(), held);
}

void TestMediaFixes::videoFullscreenIconFollowsState()
{
    md::Video video;
    video.resize(800, 450);
    const QRectF fullscreen = video.controlRect(md::Video::Control::Fullscreen);
    QVERIFY(!fullscreen.isEmpty());

    const QImage enter = renderWidget(video);
    QVERIFY(inkPixelsInRect(enter, fullscreen) > 20);

    video.setFullscreen(true);
    QVERIFY(video.isFullscreen());
    const QImage exit = renderWidget(video);
    QVERIFY(inkPixelsInRect(exit, fullscreen) > 20);

    // Enter and exit are different glyphs, not the same one recoloured.
    QVERIFY2(differingPixelsInRect(enter, exit, fullscreen) > 30,
             "the full-screen icon did not change with the state");
    QVERIFY(video.controlAccessibleName(md::Video::Control::Fullscreen)
                .contains(QStringLiteral("Exit")));
}

void TestMediaFixes::videoControlNamesAndTooltips()
{
    md::Video video;

    const md::Video::Control controls[] = {
        md::Video::Control::PlayPause,   md::Video::Control::Replay,
        md::Video::Control::SkipBack,    md::Video::Control::SkipForward,
        md::Video::Control::Volume,      md::Video::Control::Settings,
        md::Video::Control::Fullscreen,
    };

    for (md::Video::Control control : controls) {
        QVERIFY2(!video.controlToolTip(control).isEmpty(),
                 qPrintable(QStringLiteral("tooltip %1").arg(static_cast<int>(control))));
        QVERIFY2(!video.controlAccessibleName(control).isEmpty(),
                 qPrintable(QStringLiteral("name %1").arg(static_cast<int>(control))));
    }

    QVERIFY(video.controlToolTip(md::Video::Control::None).isEmpty());
    QVERIFY(!video.accessibleName().isEmpty());
    QVERIFY(!video.accessibleDescription().isEmpty());

    QVERIFY(video.controlToolTip(md::Video::Control::PlayPause)
                .startsWith(QStringLiteral("Play")));
    video.play();
    QVERIFY(video.controlToolTip(md::Video::Control::PlayPause)
                .startsWith(QStringLiteral("Pause")));
    video.pause();

    video.setMuted(true);
    QVERIFY(video.controlToolTip(md::Video::Control::Volume)
                .startsWith(QStringLiteral("Unmute")));
    video.setMuted(false);
    QVERIFY(video.controlToolTip(md::Video::Control::Volume)
                .startsWith(QStringLiteral("Mute")));
}

QTEST_MAIN(TestMediaFixes)
#include "tst_media_fixes.moc"
