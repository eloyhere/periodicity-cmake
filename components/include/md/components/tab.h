// Periodicity — Tab and TabLayout, the data and geometry of a tab bar.
//
// A Tab is a value: the label, the icon, the badge text and the enabled bit.
// It is not a widget. A tab bar owns its tabs and paints them, which is what
// lets the indicator and the bar measure the whole set in one pass; an
// application that needs a widget per tab composes a TabBar with its own
// content area instead.
//
// TabLayout is the pure geometry helper the bar and the tests share. It has no
// widget dependency: given the tabs, the viewport, the mode, the scroll offset
// and the label font, it returns one rectangle per tab. Keeping it out of
// TabBar is what makes a scrollable bar testable without a paint.

#pragma once

#include <QIcon>
#include <QList>
#include <QRectF>
#include <QString>
#include <Qt>

class QFont;

namespace md {

// One tab. A default-constructed Tab is an empty, enabled label.
struct Tab
{
    QString text;
    QIcon icon;
    QString badge;
    bool enabled = true;

    Tab() = default;
    explicit Tab(QString text, bool enabled = true);
    Tab(QString text, QIcon icon, QString badge = {});

    [[nodiscard]] friend bool operator==(const Tab& lhs, const Tab& rhs) = default;
};

// The geometry of one tab inside a bar. `visible` is false when scrolling moved
// the tab completely outside the viewport, which is what lets the paint pass
// skip it.
struct TabMetrics
{
    QRectF rect;
    bool visible = false;
};

class TabLayout
{
public:
    enum class Mode {
        Fixed,      // every tab gets an equal share of the width
        Scrollable, // every tab gets its natural width; the bar scrolls
    };

    TabLayout() = default;

    // Lays `tabs` out inside `viewport`. The returned list always has one entry
    // per tab, in tab order; `scrollOffset` is applied in Scrollable mode only.
    [[nodiscard]] static QList<TabMetrics> layout(const QList<Tab>& tabs,
                                                  const QRectF& viewport, Mode mode,
                                                  qreal scrollOffset, const QFont& font,
                                                  qreal horizontalPadding,
                                                  qreal minTabWidth, qreal maxTabWidth);

    // The width the tabs would like to occupy. In Fixed mode it equals the
    // viewport width; in Scrollable mode it is the sum of the natural widths.
    [[nodiscard]] static qreal contentWidth(const QList<Tab>& tabs, Mode mode,
                                            qreal viewportWidth, const QFont& font,
                                            qreal horizontalPadding, qreal minTabWidth,
                                            qreal maxTabWidth);
};

} // namespace md
