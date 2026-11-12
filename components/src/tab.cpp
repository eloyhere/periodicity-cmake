// Periodicity — Tab and TabLayout.
//
// The geometry is deliberately split from the widget: a bar asks TabLayout for
// rectangles, paints them and places the indicator; a test asks the same
// function and asserts on rectangles. No QWidget is created per tab.

#include <md/components/tab.h>

#include <QFont>
#include <QFontMetricsF>

#include <algorithm>
#include <utility>

namespace md {

Tab::Tab(QString textValue, bool enabledValue)
    : text(std::move(textValue))
    , enabled(enabledValue)
{
}

Tab::Tab(QString textValue, QIcon iconValue, QString badgeValue)
    : text(std::move(textValue))
    , icon(std::move(iconValue))
    , badge(std::move(badgeValue))
{
}

namespace {

// The width one tab asks for in Scrollable mode: its label plus, when there is
// one, its icon, surrounded by the horizontal padding.
qreal naturalWidth(const Tab& tab, const QFontMetricsF& metrics, qreal horizontalPadding)
{
    qreal width = metrics.horizontalAdvance(tab.text) + horizontalPadding * 2.0;
    if (!tab.icon.isNull())
        width += metrics.height() + horizontalPadding;

    return width;
}

} // namespace

QList<TabMetrics> TabLayout::layout(const QList<Tab>& tabs, const QRectF& viewport, Mode mode,
                                    qreal scrollOffset, const QFont& font,
                                    qreal horizontalPadding, qreal minTabWidth,
                                    qreal maxTabWidth)
{
    QList<TabMetrics> result;
    const int count = static_cast<int>(tabs.size());
    if (count == 0 || viewport.isEmpty())
        return result;

    result.reserve(count);
    const QFontMetricsF metrics(font);

    qreal cursor = 0.0;
    for (int index = 0; index < count; ++index) {
        const Tab& tab = tabs.at(index);

        qreal width = 0.0;
        if (mode == Mode::Fixed) {
            width = viewport.width() / static_cast<qreal>(count);
        } else {
            qreal natural = naturalWidth(tab, metrics, horizontalPadding);
            if (maxTabWidth > minTabWidth)
                natural = std::clamp(natural, minTabWidth, maxTabWidth);
            width = natural;
        }

        const qreal offset = mode == Mode::Scrollable ? scrollOffset : 0.0;
        const QRectF rect(viewport.left() + cursor - offset, viewport.top(), width,
                          viewport.height());

        TabMetrics item;
        item.rect = rect;
        item.visible = rect.right() > viewport.left() && rect.left() < viewport.right();
        result.append(item);

        cursor += width;
    }

    return result;
}

qreal TabLayout::contentWidth(const QList<Tab>& tabs, Mode mode, qreal viewportWidth,
                              const QFont& font, qreal horizontalPadding, qreal minTabWidth,
                              qreal maxTabWidth)
{
    const int count = static_cast<int>(tabs.size());
    if (count == 0)
        return 0.0;

    if (mode == Mode::Fixed)
        return viewportWidth;

    const QFontMetricsF metrics(font);
    qreal total = 0.0;
    for (const Tab& tab : tabs) {
        qreal width = naturalWidth(tab, metrics, horizontalPadding);
        if (maxTabWidth > minTabWidth)
            width = std::clamp(width, minTabWidth, maxTabWidth);
        total += width;
    }

    return total;
}

} // namespace md
