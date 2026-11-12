// Unit tests for the table/video batch: Table, VirtualTable and Video.
//
// The suite follows the component tests' conventions: the application theme is
// pinned to baseline light, widgets are rendered offscreen into a transparent
// QImage, pointer/key input is delivered with QApplication::sendEvent, and
// nothing performs I/O. The virtual table uses a counting source, which is the
// observable proof that only the viewport (plus prefetch) is queried.
//
// Video is exercised without Qt Multimedia by pushing a solid QImage and by
// installing a provider lambda that records the positions it is asked for.

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QRegion>
#include <QSet>
#include <QSignalSpy>
#include <QSize>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QString>
#include <QTest>
#include <QVariant>
#include <QVector>
#include <QWheelEvent>
#include <QWidget>
#include <QtMath>

#include <md/components/table.h>
#include <md/components/video.h>
#include <md/components/virtual_table.h>
#include <md/structure/table.h>
#include <md/theme/theme.h>
#include <md/tokens/color_scheme.h>

#include <algorithm>
#include <limits>

namespace {

const md::ColorScheme& lightColours()
{
    return md::Theme::applicationTheme().colours();
}

QImage solidImage(const QSize& size, const QColor& colour)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(colour);
    return image;
}

// Same transparent render as tst_media_basics: QWidget::grab() leaves unset
// pixels uninitialised for a widget that does not fill its background, so every
// pixel assertion paints into a cleared image instead.
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

void sendMouse(QWidget& widget, QEvent::Type type, const QPointF& position,
               Qt::MouseButton button, Qt::MouseButtons buttons,
               Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    const QPoint global = widget.mapToGlobal(position.toPoint());
    QMouseEvent event(type, position, QPointF(global), button, buttons, modifiers);
    QApplication::sendEvent(&widget, &event);
}

void clickAt(QWidget& widget, const QPointF& position,
             Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    sendMouse(widget, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton,
              modifiers);
    sendMouse(widget, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton,
              modifiers);
}

void doubleClickAt(QWidget& widget, const QPointF& position)
{
    sendMouse(widget, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton);
    sendMouse(widget, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
    sendMouse(widget, QEvent::MouseButtonDblClick, position, Qt::LeftButton, Qt::LeftButton);
}

void sendKey(QWidget& widget, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QKeyEvent event(QEvent::KeyPress, key, modifiers);
    QApplication::sendEvent(&widget, &event);
}

void fillModel(QStandardItemModel& model)
{
    model.setHorizontalHeaderLabels(
        {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), QStringLiteral("D")});
    for (int row = 0; row < model.rowCount(); ++row) {
        for (int column = 0; column < model.columnCount(); ++column) {
            model.setItem(row, column,
                          new QStandardItem(QStringLiteral("r%1c%2").arg(row).arg(column)));
        }
    }
}

void prepareTable(md::Table& table, QStandardItemModel& model, int rows = 20,
                  int columns = 4)
{
    model.setRowCount(rows);
    model.setColumnCount(columns);
    fillModel(model);

    table.setModel(&model);
    table.setRowHeight(40.0);
    for (int column = 0; column < columns; ++column)
        table.setColumnWidth(column, 100.0);
    table.resize(420, 300);
}

// The counting source. `cell()` records how often and which rows were asked
// for; the table must never walk the whole 10 000-row space to paint one page.
class CountingTableSource final : public md::structure::VirtualTableSource
{
public:
    CountingTableSource(int rows, int columns)
        : rows_(rows)
        , columns_(columns)
    {
    }

    [[nodiscard]] int rowCount() const override
    {
        ++rowCountCalls;
        return rows_;
    }

    [[nodiscard]] int columnCount() const override
    {
        ++columnCountCalls;
        return columns_;
    }

    [[nodiscard]] QVariant cell(int row, int column) const override
    {
        ++cellCalls;
        rowsQueried.insert(row);
        minRow = std::min(minRow, row);
        maxRow = std::max(maxRow, row);
        return QStringLiteral("%1,%2").arg(row).arg(column);
    }

    [[nodiscard]] QString header(int column) const override
    {
        ++headerCalls;
        return QStringLiteral("H%1").arg(column);
    }

    void resetCounters() const
    {
        cellCalls = 0;
        headerCalls = 0;
        rowCountCalls = 0;
        columnCountCalls = 0;
        rowsQueried.clear();
        minRow = std::numeric_limits<int>::max();
        maxRow = -1;
    }

    mutable int cellCalls = 0;
    mutable int headerCalls = 0;
    mutable int rowCountCalls = 0;
    mutable int columnCountCalls = 0;
    mutable int minRow = std::numeric_limits<int>::max();
    mutable int maxRow = -1;
    mutable QSet<int> rowsQueried;

private:
    int rows_ = 0;
    int columns_ = 0;
};

} // namespace

class TestTableVideo : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void tablePaintAndHeader();
    void tableKeyboardNavigation();
    void tableClickSelectionAndSignals();
    void tableScrolling();
    void tableModelChanges();

    void virtualTableVirtualisation();
    void virtualTableFrozenColumns();

    void videoFrameLetterboxAndPosition();
    void videoPlaybackAdvancesAndPauses();
    void videoEndAndLoop();
    void videoSeekMouseAndKeyboard();
    void videoVolumeMuteAndFullscreen();
    void videoFrameProvider();
    void videoPlaceholder();
};

void TestTableVideo::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestTableVideo::tablePaintAndHeader()
{
    QStandardItemModel model;
    md::Table table;
    prepareTable(table, model);

    QCOMPARE(table.rowCount(), 20);
    QCOMPARE(table.columnCount(), 4);
    QVERIFY(table.visibleRowCount() > 0);
    QVERIFY(table.visibleRowCount() < 20);
    QCOMPARE(table.firstVisibleRow(), 0);

    QCOMPARE(table.headerRect().height(), 48.0);

    const QImage rendered = renderWidget(table);
    QVERIFY(paintedPixels(rendered) > 1000);

    // The header uses surfaceContainer; the cell body is the plain surface.
    QCOMPARE(pixelAt(rendered, QPointF(5.0, 5.0)).rgb(),
             lightColours().surfaceContainer.rgb());
    const QRectF firstCell = table.cellRect(0, 0);
    QCOMPARE(pixelAt(rendered, QPointF(5.0, firstCell.top() + 8.0)).rgb(),
             lightColours().surface.rgb());

    table.setHeadersVisible(false);
    QCOMPARE(table.headerRect().height(), 0.0);
    QCOMPARE(table.cellRect(0, 0).top(), 0.0);

    table.setHeadersVisible(true);
    QCOMPARE(table.headerRect().height(), 48.0);
}

void TestTableVideo::tableKeyboardNavigation()
{
    QStandardItemModel model;
    md::Table table;
    prepareTable(table, model);

    table.setCurrentCell(0, 0);
    QCOMPARE(table.currentRow(), 0);
    QCOMPARE(table.currentColumn(), 0);

    sendKey(table, Qt::Key_Down);
    QCOMPARE(table.currentRow(), 1);
    QCOMPARE(table.currentColumn(), 0);

    sendKey(table, Qt::Key_Right);
    QCOMPARE(table.currentColumn(), 1);

    sendKey(table, Qt::Key_End);
    QCOMPARE(table.currentColumn(), 3);

    sendKey(table, Qt::Key_Home);
    QCOMPARE(table.currentRow(), 1);
    QCOMPARE(table.currentColumn(), 0);

    sendKey(table, Qt::Key_Home, Qt::ControlModifier);
    QCOMPARE(table.currentRow(), 0);
    QCOMPARE(table.currentColumn(), 0);

    const int visible = table.visibleRowCount();
    QVERIFY(visible > 1);
    sendKey(table, Qt::Key_PageDown);
    QCOMPARE(table.currentRow(), visible - 1);

    sendKey(table, Qt::Key_PageUp);
    QCOMPARE(table.currentRow(), 0);

    sendKey(table, Qt::Key_Up);
    QCOMPARE(table.currentRow(), 0);

    sendKey(table, Qt::Key_Left);
    QCOMPARE(table.currentColumn(), 0);

    sendKey(table, Qt::Key_End, Qt::ControlModifier);
    QCOMPARE(table.currentRow(), 19);
    QCOMPARE(table.currentColumn(), 3);
}

void TestTableVideo::tableClickSelectionAndSignals()
{
    QStandardItemModel model;
    md::Table table;
    prepareTable(table, model);

    QSignalSpy clicked(&table, &md::Table::cellClicked);
    QSignalSpy currentChanged(&table, &md::Table::currentCellChanged);

    const QRectF target = table.cellRect(3, 2);
    clickAt(table, target.center());

    QCOMPARE(clicked.count(), 1);
    QCOMPARE(clicked.at(0).at(0).toInt(), 3);
    QCOMPARE(clicked.at(0).at(1).toInt(), 2);
    QVERIFY(currentChanged.count() >= 1);
    QCOMPARE(table.currentRow(), 3);
    QCOMPARE(table.currentColumn(), 2);

    QImage rendered = renderWidget(table);
    QCOMPARE(pixelAt(rendered,
                     QPointF(target.left() + 4.0, target.center().y())).rgb(),
             lightColours().secondaryContainer.rgb());

    // SingleCell paints only the current cell.
    const QRectF neighbour = table.cellRect(3, 1);
    QCOMPARE(pixelAt(rendered,
                     QPointF(neighbour.left() + 4.0, neighbour.center().y())).rgb(),
             lightColours().surface.rgb());

    // SingleRow paints the whole row.
    table.setSelectionMode(md::Table::SelectionMode::SingleRow);
    table.setCurrentCell(2, 1);
    rendered = renderWidget(table);
    QCOMPARE(pixelAt(rendered, QPointF(table.cellRect(2, 0).left() + 4.0,
                                       table.cellRect(2, 0).center().y())).rgb(),
             lightColours().secondaryContainer.rgb());

    // NoSelection keeps the current cell but paints no layer.
    table.setSelectionMode(md::Table::SelectionMode::NoSelection);
    table.setCurrentCell(4, 0);
    rendered = renderWidget(table);
    QCOMPARE(pixelAt(rendered, QPointF(table.cellRect(4, 0).left() + 4.0,
                                       table.cellRect(4, 0).center().y())).rgb(),
             lightColours().surface.rgb());

    // Double-click reports its own signal and moves the current cell.
    table.setSelectionMode(md::Table::SelectionMode::SingleCell);
    QSignalSpy doubleClicked(&table, &md::Table::doubleClicked);
    const QPointF point = table.cellRect(5, 1).center();
    doubleClickAt(table, point);
    QCOMPARE(doubleClicked.count(), 1);
    QCOMPARE(doubleClicked.at(0).at(0).toInt(), 5);
    QCOMPARE(doubleClicked.at(0).at(1).toInt(), 1);
    QCOMPARE(table.currentRow(), 5);
    QCOMPARE(table.currentColumn(), 1);
}

void TestTableVideo::tableScrolling()
{
    QStandardItemModel model;
    md::Table table;
    prepareTable(table, model);

    QCOMPARE(table.firstVisibleRow(), 0);
    QWheelEvent wheel(QPointF(60.0, 120.0), QPointF(table.mapToGlobal(QPoint(60, 120))),
                      QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(&table, &wheel);

    QVERIFY(table.scrollOffset() > 0.0);
    QCOMPARE(table.firstVisibleRow(), 3);
    QVERIFY(table.firstVisibleRow() != 0);

    table.setScrollOffset(0.0);
    QCOMPARE(table.firstVisibleRow(), 0);

    table.setScrollOffset(1'000'000.0);
    QVERIFY(table.firstVisibleRow() > 0);
    QVERIFY(table.firstVisibleRow() < table.rowCount());

    // A keyboard move below the viewport scrolls the table as well.
    table.setCurrentCell(0, 0);
    table.setScrollOffset(0.0);
    sendKey(table, Qt::Key_End);
    QCOMPARE(table.currentRow(), 0);
    for (int step = 0; step < table.visibleRowCount(); ++step)
        sendKey(table, Qt::Key_Down);
    QVERIFY(table.currentRow() >= table.visibleRowCount());
    QVERIFY(table.scrollOffset() > 0.0);
}

void TestTableVideo::tableModelChanges()
{
    QStandardItemModel model;
    md::Table table;
    prepareTable(table, model);

    table.setCurrentCell(5, 1);
    model.insertRow(2);
    QCOMPARE(table.rowCount(), 21);
    QCOMPARE(table.currentRow(), 6);
    QCOMPARE(table.currentColumn(), 1);

    model.removeRow(2);
    QCOMPARE(table.rowCount(), 20);
    QCOMPARE(table.currentRow(), 5);

    QVERIFY(model.setData(model.index(0, 0), QStringLiteral("changed"), Qt::DisplayRole));
    QVERIFY(paintedPixels(renderWidget(table)) > 0);

    model.insertRow(20);
    QCOMPARE(table.rowCount(), 21);
    QCOMPARE(model.rowCount(), 21);

    model.removeRow(0);
    QCOMPARE(table.rowCount(), 20);
    QCOMPARE(table.currentRow(), 4);

    model.clear();
    QCOMPARE(table.rowCount(), 0);
    QCOMPARE(table.currentRow(), -1);
    QCOMPARE(table.currentColumn(), -1);
    QVERIFY(paintedPixels(renderWidget(table)) > 0);
}

void TestTableVideo::virtualTableVirtualisation()
{
    CountingTableSource source(10000, 4);
    md::VirtualTable table;
    table.setSource(&source);
    QCOMPARE(table.rowCount(), 10000);
    QCOMPARE(table.columnCount(), 4);

    table.setRowHeight(40.0);
    for (int column = 0; column < 4; ++column)
        table.setColumnWidth(column, 100.0);
    table.resize(420, 300);

    QVERIFY(table.visibleRowCount() > 0);
    QVERIFY(table.visibleRowCount() <= 12);
    QVERIFY(table.visibleColumnCount() > 0);
    QVERIFY(table.visibleColumnCount() <= 4);
    QCOMPARE(table.cellText(123, 2), QStringLiteral("123,2"));
    QVERIFY(table.cellText(-1, 0).isEmpty());
    QVERIFY(table.cellText(10000, 0).isEmpty());

    QVERIFY(paintedPixels(renderWidget(table)) > 0);

    source.resetCounters();
    QVERIFY(paintedPixels(renderWidget(table)) > 0);

    QVERIFY(source.cellCalls > 0);
    QVERIFY(source.cellCalls
            <= 4 * (table.visibleRowCount() + 2 * table.prefetchRows() + 2));
    const int firstWindow = std::max(0, table.firstVisibleRow() - table.prefetchRows());
    const int lastWindow = std::min(
        9999, table.firstVisibleRow() + table.visibleRowCount() + table.prefetchRows());
    QVERIFY(source.minRow >= firstWindow);
    QVERIFY(source.maxRow <= lastWindow);
    QVERIFY(source.rowsQueried.contains(0));
    QVERIFY(!source.rowsQueried.contains(9999));

    // Scrolling replaces the query window; the table never walks the rows in
    // between and never touches the far end of the source.
    table.setScrollOffset(5000.0 * 40.0);
    QCOMPARE(table.firstVisibleRow(), 5000);
    QVERIFY(table.visibleRowCount() <= 12);

    source.resetCounters();
    QVERIFY(paintedPixels(renderWidget(table)) > 0);
    QVERIFY(source.cellCalls > 0);
    QVERIFY(source.cellCalls
            <= 4 * (table.visibleRowCount() + 2 * table.prefetchRows() + 2));
    QVERIFY(source.minRow >= 5000 - table.prefetchRows());
    QVERIFY(source.maxRow <= 5000 + table.visibleRowCount() + table.prefetchRows());
    QVERIFY(!source.rowsQueried.contains(0));
    QVERIFY(!source.rowsQueried.contains(9999));

    QCOMPARE(table.cellText(5002, 3), QStringLiteral("5002,3"));

    table.setCurrentCell(5000, 0);
    sendKey(table, Qt::Key_Down);
    QCOMPARE(table.currentRow(), 5001);
    sendKey(table, Qt::Key_Right);
    QCOMPARE(table.currentColumn(), 1);

    const int stride = std::max(1, table.visibleRowCount() - 1);
    sendKey(table, Qt::Key_PageDown);
    QCOMPARE(table.currentRow(), 5001 + stride);
    QVERIFY(table.currentRow() < 10000);

    sendKey(table, Qt::Key_End);
    QCOMPARE(table.currentColumn(), 3);
    sendKey(table, Qt::Key_Home);
    QCOMPARE(table.currentColumn(), 0);
}

void TestTableVideo::virtualTableFrozenColumns()
{
    CountingTableSource source(10000, 6);
    md::VirtualTable table;
    table.setSource(&source);
    table.setRowHeight(40.0);
    for (int column = 0; column < 6; ++column)
        table.setColumnWidth(column, 100.0);
    table.resize(420, 300);

    table.setFrozenColumns(1);
    QCOMPARE(table.frozenColumns(), 1);
    QCOMPARE(table.columnRect(0).left(), 0.0);
    QCOMPARE(table.columnRect(1).left(), 100.0);

    table.setHorizontalScrollOffset(50.0);
    QCOMPARE(table.horizontalScrollOffset(), 50.0);
    QCOMPARE(table.columnRect(0).left(), 0.0);
    QCOMPARE(table.columnRect(1).left(), 50.0);
    QVERIFY(table.visibleColumnCount() > 0);
    QVERIFY(table.visibleColumnCount() <= 6);

    source.resetCounters();
    QVERIFY(paintedPixels(renderWidget(table)) > 0);
    QVERIFY(source.cellCalls > 0);
    QVERIFY(source.cellCalls <= 6 * (table.visibleRowCount() + 2 * table.prefetchRows() + 2));

    QCOMPARE(table.cellText(9, 5), QStringLiteral("9,5"));
}

void TestTableVideo::videoFrameLetterboxAndPosition()
{
    const QImage red = solidImage(QSize(200, 100), QColor(255, 0, 0));

    md::Video video;
    QVERIFY(!video.hasFrame());
    QVERIFY(video.state() == md::Video::PlaybackState::Stopped);

    QSignalSpy durationSpy(&video, &md::Video::durationChanged);
    video.setDuration(10000);
    QCOMPARE(video.duration(), 10000);
    QCOMPARE(durationSpy.count(), 1);

    QSignalSpy positionSpy(&video, &md::Video::positionChanged);
    video.setPosition(2000);
    QCOMPARE(video.position(), 2000);
    QCOMPARE(positionSpy.count(), 1);

    video.setFrame(red);
    QVERIFY(video.hasFrame());
    QCOMPARE(video.frame().size(), QSize(200, 100));

    video.resize(400, 300);
    const QImage rendered = renderWidget(video);
    QVERIFY(paintedPixels(rendered) > 0);

    // AspectFit: a 200x100 frame in 400x300 paints 400x200 centred, so the top
    // band is the black letterbox and the middle is the frame.
    QCOMPARE(pixelAt(rendered, QPointF(200.0, 100.0)).rgb(), QColor(255, 0, 0).rgb());
    QCOMPARE(pixelAt(rendered, QPointF(200.0, 20.0)).rgb(), QColor(Qt::black).rgb());
    QCOMPARE(pixelAt(rendered, QPointF(2.0, 2.0)).rgb(), QColor(Qt::black).rgb());

    video.setPosition(50000);
    QCOMPARE(video.position(), 10000);
    video.setPosition(-10);
    QCOMPARE(video.position(), 0);

    video.clearFrame();
    QVERIFY(!video.hasFrame());
}

void TestTableVideo::videoPlaybackAdvancesAndPauses()
{
    const QImage red = solidImage(QSize(64, 36), QColor(255, 0, 0));
    md::Video video;
    video.setDuration(60000);
    video.setFrame(red);
    video.resize(320, 180);

    QSignalSpy stateSpy(&video, &md::Video::playbackStateChanged);
    QSignalSpy positionSpy(&video, &md::Video::positionChanged);

    video.play();
    QVERIFY(video.state() == md::Video::PlaybackState::Playing);
    QCOMPARE(stateSpy.count(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(video.position() > 0, 1500);
    QVERIFY(positionSpy.count() > 0);

    video.pause();
    QVERIFY(video.state() == md::Video::PlaybackState::Paused);
    QCOMPARE(stateSpy.count(), 2);

    const qint64 pausedAt = video.position();
    QVERIFY(pausedAt > 0);
    QTest::qWait(100);
    QCOMPARE(video.position(), pausedAt);

    video.play();
    QTRY_VERIFY_WITH_TIMEOUT(video.position() > pausedAt, 1500);

    video.stop();
    QVERIFY(video.state() == md::Video::PlaybackState::Stopped);
    QCOMPARE(video.position(), 0);
    QCOMPARE(stateSpy.count(), 4);
}

void TestTableVideo::videoEndAndLoop()
{
    md::Video ending;
    ending.setDuration(40);
    ending.play();
    QTRY_VERIFY_WITH_TIMEOUT(ending.state() == md::Video::PlaybackState::Stopped, 2000);
    QCOMPARE(ending.position(), 40);

    md::Video looping;
    looping.setDuration(100);
    looping.setLoop(true);
    QVERIFY(looping.loop());
    looping.play();
    QTest::qWait(260);
    QVERIFY(looping.state() == md::Video::PlaybackState::Playing);
    QVERIFY(looping.position() >= 0);
    QVERIFY(looping.position() < 100);
    looping.pause();
}

void TestTableVideo::videoSeekMouseAndKeyboard()
{
    const QImage red = solidImage(QSize(80, 45), QColor(255, 0, 0));
    md::Video video;
    video.setDuration(10000);
    video.setFrame(red);
    video.resize(400, 300);

    const QRectF track = video.progressRect();
    QVERIFY(!track.isEmpty());

    QSignalSpy positionSpy(&video, &md::Video::positionChanged);
    clickAt(video, QPointF(track.left() + track.width() * 0.25, track.center().y()));
    QCOMPARE(video.position(), 2500);
    QVERIFY(positionSpy.count() >= 1);

    // Press, drag and release: each move seeks continuously.
    const QPointF start(track.left() + track.width() * 0.2, track.center().y());
    const QPointF finish(track.left() + track.width() * 0.6, track.center().y());
    sendMouse(video, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    sendMouse(video, QEvent::MouseMove, finish, Qt::NoButton, Qt::LeftButton);
    sendMouse(video, QEvent::MouseButtonRelease, finish, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(video.position(), 6000);

    video.setPosition(6000);
    sendKey(video, Qt::Key_Right);
    QCOMPARE(video.position(), 10000);
    sendKey(video, Qt::Key_Left);
    QCOMPARE(video.position(), 5000);
    sendKey(video, Qt::Key_Home);
    QCOMPARE(video.position(), 0);
    sendKey(video, Qt::Key_End);
    QCOMPARE(video.position(), 10000);

    video.setPosition(5000);
    sendKey(video, Qt::Key_Space);
    QVERIFY(video.state() == md::Video::PlaybackState::Playing);
    sendKey(video, Qt::Key_Space);
    QVERIFY(video.state() == md::Video::PlaybackState::Paused);
    QCOMPARE(video.position(), 5000);
}

void TestTableVideo::videoVolumeMuteAndFullscreen()
{
    md::Video video;
    video.resize(400, 300);
    video.setDuration(10000);

    QSignalSpy volumeSpy(&video, &md::Video::volumeChanged);
    video.setVolume(0.5);
    QVERIFY(qAbs(video.volume() - 0.5) < 1e-9);
    QCOMPARE(volumeSpy.count(), 1);

    video.setVolume(2.0);
    QCOMPARE(video.volume(), 1.0);
    video.setVolume(-1.0);
    QCOMPARE(video.volume(), 0.0);
    video.setVolume(0.5);
    QCOMPARE(volumeSpy.count(), 4);

    QSignalSpy mutedSpy(&video, &md::Video::mutedChanged);
    video.setMuted(true);
    QVERIFY(video.isMuted());
    QCOMPARE(mutedSpy.count(), 1);
    video.setMuted(false);
    QVERIFY(!video.isMuted());
    QCOMPARE(mutedSpy.count(), 2);

    sendKey(video, Qt::Key_Up);
    QVERIFY(qAbs(video.volume() - 0.55) < 1e-6);
    sendKey(video, Qt::Key_Down);
    QVERIFY(qAbs(video.volume() - 0.5) < 1e-6);

    video.setMuted(true);
    sendKey(video, Qt::Key_M);
    QVERIFY(!video.isMuted());

    const QRectF volumeButton = video.volumeButtonRect();
    QVERIFY(!volumeButton.isEmpty());
    const bool mutedBefore = video.isMuted();
    clickAt(video, volumeButton.center());
    QCOMPARE(video.isMuted(), !mutedBefore);

    QSignalSpy fullscreenSpy(&video, &md::Video::fullscreenRequested);
    const QRectF fullscreenButton = video.fullscreenButtonRect();
    QVERIFY(!fullscreenButton.isEmpty());
    clickAt(video, fullscreenButton.center());
    QCOMPARE(fullscreenSpy.count(), 1);

    sendKey(video, Qt::Key_F);
    QCOMPARE(fullscreenSpy.count(), 2);
}

void TestTableVideo::videoFrameProvider()
{
    const QImage green = solidImage(QSize(160, 90), QColor(0, 180, 0));

    QVector<qint64> pulls;
    md::Video video;
    video.setDuration(1000);
    video.setFrameProvider([&green, &pulls](qint64 position) {
        pulls.append(position);
        return green;
    });

    QVERIFY(video.hasFrameProvider());
    QVERIFY(video.hasFrame());
    QVERIFY(!pulls.isEmpty());
    QCOMPARE(pulls.last(), qint64(0));

    video.setPosition(300);
    QCOMPARE(video.position(), 300);
    QCOMPARE(pulls.last(), qint64(300));

    const qsizetype before = pulls.size();
    video.play();
    QTRY_VERIFY_WITH_TIMEOUT(video.position() > 300, 1500);
    QVERIFY(pulls.size() > before);
    video.pause();
    QVERIFY(pulls.last() >= 300);

    video.resize(400, 300);
    const QImage rendered = renderWidget(video);
    QCOMPARE(pixelAt(rendered, QPointF(200.0, 150.0)).rgb(), QColor(0, 180, 0).rgb());
}

void TestTableVideo::videoPlaceholder()
{
    md::Video video;
    video.resize(400, 300);
    QVERIFY(!video.hasFrame());
    QVERIFY(!video.hasFrameProvider());

    const QImage placeholder = renderWidget(video);
    QVERIFY(paintedPixels(placeholder) > 0);

    const QColor background(16, 16, 16);
    QCOMPARE(pixelAt(placeholder, QPointF(2.0, 2.0)).rgb(), background.rgb());

    int differing = 0;
    for (int y = 100; y < 200; y += 2) {
        for (int x = 150; x < 250; x += 2) {
            if (pixelAt(placeholder, QPointF(x, y)).rgb() != background.rgb())
                ++differing;
        }
    }
    QVERIFY(differing > 20);

    const QImage red = solidImage(QSize(200, 100), QColor(255, 0, 0));
    video.setFrame(red);
    const QImage framed = renderWidget(video);
    QVERIFY(framed != placeholder);
    QCOMPARE(pixelAt(framed, QPointF(200.0, 100.0)).rgb(), QColor(255, 0, 0).rgb());
}

QTEST_MAIN(TestTableVideo)
#include "tst_table_video.moc"
