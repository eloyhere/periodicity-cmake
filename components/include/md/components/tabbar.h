// Periodicity — TabBar, the design baseline primary/secondary tab bar.
//
// The bar owns its tabs as values (see tab.h) and paints them itself. Two modes
// are supported: Fixed gives every tab an equal share of the width, Scrollable
// gives every tab its natural width and lets the set scroll horizontally.
//
// The selection indicator is a 3 dp bar under the current tab. When the
// selection changes it travels to the new tab over the theme's medium1 motion
// spec; reduced motion (or setIndicatorAnimationEnabled(false)) snaps instead.
// The animation is a QTimer driving requestUpdate(), so no framework animation
// object is required and a test can wait for it with QTest::qWait.

#pragma once

#include <md/components/tab.h>
#include <md/drawable/icon_drawable.h>
#include <md/view/view.h>

#include <QElapsedTimer>
#include <QList>
#include <QRectF>
#include <QTimer>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QResizeEvent;
class QWheelEvent;

namespace md {

class TabBar : public View
{
    Q_OBJECT

public:
    using Mode = TabLayout::Mode;

    explicit TabBar(QWidget* parent = nullptr);

    void setTabs(const QList<Tab>& tabs);
    [[nodiscard]] QList<Tab> tabs() const;
    [[nodiscard]] int count() const;
    [[nodiscard]] Tab tabAt(int index) const;

    void addTab(const Tab& tab);
    void insertTab(int index, const Tab& tab);
    void removeTab(int index);
    void setTabText(int index, const QString& text);
    void setTabEnabled(int index, bool enabled);
    void setTabBadge(int index, const QString& badge);

    void setMode(Mode mode);
    [[nodiscard]] Mode mode() const noexcept;

    // -1 clears the selection. Only enabled tabs can be selected through input;
    // setCurrentIndex() accepts a disabled index on purpose, because a caller
    // restoring persisted state should not have to re-check.
    void setCurrentIndex(int index);
    [[nodiscard]] int currentIndex() const noexcept;

    void setIndicatorAnimationEnabled(bool enabled);
    [[nodiscard]] bool isIndicatorAnimationEnabled() const noexcept;

    // The indicator rectangle in widget coordinates. It is the animated value
    // while a transition runs and the target rectangle otherwise.
    [[nodiscard]] QRectF indicatorRect() const;
    [[nodiscard]] QRectF tabRect(int index) const;

    void setScrollOffset(qreal offset);
    [[nodiscard]] qreal scrollOffset() const noexcept;
    [[nodiscard]] qreal maximumScrollOffset() const;

signals:
    void currentChanged(int index);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void onThemeChanged() override;

private:
    void invalidateLayout();
    [[nodiscard]] const QList<TabMetrics>& ensureLayout() const;
    [[nodiscard]] QRectF indicatorFor(int index) const;
    void snapIndicator();
    void startIndicatorAnimation(const QRectF& from, const QRectF& to);
    void tickIndicator();
    void ensureCurrentVisible();
    void clampScroll();
    [[nodiscard]] int indexAtPosition(const QPointF& position) const;

    QList<Tab> tabs_;
    int current_ = -1;
    int hovered_ = -1;
    Mode mode_ = Mode::Fixed;
    bool animateIndicator_ = true;
    qreal scrollOffset_ = 0.0;

    QRectF indicator_;
    QRectF indicatorFrom_;
    QRectF indicatorTo_;
    qreal indicatorProgress_ = 1.0;
    QTimer indicatorTimer_;
    QElapsedTimer indicatorClock_;

    // The layout is a pure function of tabs, mode, size and scroll offset, so
    // it is cached and invalidated by the setters that change any of them.
    mutable QList<TabMetrics> metricsCache_;
    mutable bool layoutDirty_ = true;

    drawable::IconDrawable iconDrawable_;
};

} // namespace md
