// Periodicity — the Schedule component (framework.md §5.9).
//
// The widget is one painted surface: no child widgets, no Scrollbar. Every
// public rectangle (dayColumnRect, weekdayHeaderRect, hourLabelRect, eventRect)
// is computed from the same helpers painting uses, so a hit-testing or lane
// regression shows up in the geometry assertions of the test suite.
//
// Lane layout. Each visible column collects its events, sorted by start and
// then by end, and groups them into overlap clusters: a new cluster starts when
// no lane is still occupied at the event's start. Within a cluster a lane is
// the first one whose last end is <= the new start, so events that do not
// overlap share a lane. The width of every block in a cluster is the column
// width divided by that cluster's lane count; a cluster with one event keeps
// the whole column. A separate per-column global lane count was rejected: it
// would narrow an isolated event merely because another part of the day is
// busy.
//
// Simplifications, documented because the API cannot express them:
//   * an event is shown in the column of its start date only; an event that
//     spans midnight is clipped at the end of the day range instead of being
//     continued in the next column;
//   * events are clipped to [range start, range end]; times are minute
//     resolution (seconds are dropped);
//   * moving is controlled: eventMoved() reports the new start and the model is
//     not mutated, so the host decides whether to apply it;
//   * a drag or a key move is not clamped to the range; the host may clamp it
//     before calling setEvents().
//
// All colours and metrics come from Theme::forWidget(this). There is no hex
// literal and no Qt::GlobalColor in this file.

#include <md/components/schedule.h>

#include <md/theme/theme.h>

#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <exception>

namespace md {
namespace {

constexpr qreal kGutterWidthDp = 56.0;
constexpr qreal kHeaderHeightDp = 32.0;
constexpr qreal kHourHeightDp = 48.0;
constexpr qreal kEventRadiusDp = 8.0;
constexpr qreal kAccentBarWidthDp = 2.0;
constexpr qreal kEventInsetDp = 2.0;
constexpr qreal kEventPaddingDp = 4.0;
constexpr qreal kEventBorderDp = 2.0;
constexpr qreal kMinimumEventHeightDp = 10.0;
constexpr qreal kHourLabelInsetDp = 4.0;
constexpr qreal kHourLabelTopDp = 2.0;
constexpr qreal kHeaderTextInsetDp = 6.0;
constexpr qreal kNowLineWidthDp = 2.0;
constexpr qreal kScrollIndicatorWidthDp = 3.0;
constexpr qreal kScrollIndicatorMarginDp = 2.0;
constexpr qreal kScrollThumbMinDp = 24.0;
constexpr qreal kDragThresholdDp = 3.0;
constexpr qreal kPreferredDayWidthDp = 360.0;
constexpr qreal kPreferredWeekWidthDp = 720.0;
constexpr qreal kPreferredHeightDp = 480.0;
constexpr int kSnapMinutes = 15;

// The label font of one text style, sized in sp through the density.
[[nodiscard]] QFont fontFor(const Theme& theme, const TextStyle& style)
{
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(QFont::Weight(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
    return font;
}

// "08:00" ... "24:00"; the gutter is a 24-hour scale even when the visible
// range starts later.
[[nodiscard]] QString hourText(int hour)
{
    if (hour >= 24)
        return QStringLiteral("24:00");
    return QStringLiteral("%1:00").arg(hour, 2, 10, QLatin1Char('0'));
}

} // namespace

Schedule::Schedule(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    // The agenda fills the whole surface: the header and gutter paints meet the
    // widget edge, so a rounded container would leave square corner fragments.
    setShape(Shape::none());
    setAccessibleName(tr("Schedule"));
    updateAccessibleDescription();
}

// ---------------------------------------------------------------------------
// Property setters
// ---------------------------------------------------------------------------

void Schedule::setMode(Mode mode)
{
    if (mode_ == mode)
        return;

    mode_ = mode;
    clampScroll();
    updateAccessibleDescription();
    requestUpdate();
}

void Schedule::setDate(const QDate& date)
{
    if (!date.isValid() || date_ == date)
        return;

    date_ = date;
    clampScroll();
    updateAccessibleDescription();
    requestUpdate();
}

void Schedule::setEvents(const QVector<ScheduleEvent>& events)
{
    events_ = events;

    // The model owns the ids; an event that disappeared cannot remain selected.
    if (!selectedId_.isEmpty() && indexOfEvent(selectedId_) < 0)
        selectedId_.clear();

    pressedEvent_ = -1;
    dragging_ = false;
    dragDeltaMinutes_ = 0;
    requestUpdate();
}

void Schedule::setDayRange(QTime start, QTime end)
{
    if (!start.isValid())
        start = QTime(0, 0);

    const int startMinutes = start.hour() * 60 + start.minute();

    // A QTime cannot represent 24:00, so both midnight and an invalid end mean
    // "the end of the day". This is what makes the default 00:00–24:00 range
    // expressible.
    int endMinutes = 0;
    if (!end.isValid()
        || (end.hour() == 0 && end.minute() == 0 && end.second() == 0)) {
        endMinutes = 24 * 60;
    } else {
        endMinutes = end.hour() * 60 + end.minute();
    }

    if (endMinutes <= startMinutes)
        return;

    if (rangeStart_ == start && rangeEndMinutes_ == endMinutes)
        return;

    rangeStart_ = start;
    rangeEndMinutes_ = endMinutes;
    clampScroll();
    requestUpdate();
}

void Schedule::setNowLineVisible(bool visible)
{
    if (nowLineVisible_ == visible)
        return;

    nowLineVisible_ = visible;
    requestUpdate();
}

void Schedule::setEditable(bool editable)
{
    if (editable_ == editable)
        return;

    editable_ = editable;
    pressedEvent_ = -1;
    dragging_ = false;
    dragDeltaMinutes_ = 0;
    requestUpdate();
}

void Schedule::setSelectedEvent(const QString& id)
{
    if (selectedId_ == id)
        return;

    if (!id.isEmpty() && indexOfEvent(id) < 0)
        return;

    selectedId_ = id;
    requestUpdate();
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

int Schedule::columnCount() const noexcept
{
    return mode_ == Mode::Day ? 1 : 7;
}

QDate Schedule::columnDate(int column) const
{
    if (column < 0 || column >= columnCount())
        return QDate();

    return date_.addDays(column);
}

bool Schedule::isRightToLeft() const noexcept
{
    return layoutDirection() == Qt::RightToLeft;
}

qreal Schedule::headerHeight() const
{
    return Theme::forWidget(this).density().dp(kHeaderHeightDp);
}

qreal Schedule::gutterWidth() const
{
    return Theme::forWidget(this).density().dp(kGutterWidthDp);
}

qreal Schedule::hourHeight() const
{
    return Theme::forWidget(this).density().dp(kHourHeightDp);
}

qreal Schedule::eventRadius() const
{
    return Theme::forWidget(this).density().dp(kEventRadiusDp);
}

int Schedule::rangeStartMinutes() const noexcept
{
    return rangeStart_.hour() * 60 + rangeStart_.minute();
}

int Schedule::rangeEndMinutes() const noexcept
{
    return rangeEndMinutes_;
}

QRectF Schedule::headerRect() const
{
    const QRectF area = contentRect();
    const qreal height = std::min(headerHeight(), std::max<qreal>(0.0, area.height()));
    return QRectF(area.left(), area.top(), std::max<qreal>(0.0, area.width()), height);
}

QRectF Schedule::bodyRect() const
{
    const QRectF area = contentRect();
    const qreal top = area.top() + headerRect().height();
    return QRectF(area.left(), top, std::max<qreal>(0.0, area.width()),
                  std::max<qreal>(0.0, area.bottom() - top));
}

QRectF Schedule::gutterRect() const
{
    const QRectF body = bodyRect();
    const qreal width = std::min(gutterWidth(), body.width());

    if (isRightToLeft())
        return QRectF(body.right() - width, body.top(), std::max<qreal>(0.0, width),
                      body.height());

    return QRectF(body.left(), body.top(), std::max<qreal>(0.0, width), body.height());
}

QRectF Schedule::gridRect() const
{
    const QRectF body = bodyRect();
    const QRectF gutter = gutterRect();

    if (isRightToLeft())
        return QRectF(body.left(), body.top(),
                      std::max<qreal>(0.0, body.width() - gutter.width()), body.height());

    return QRectF(body.left() + gutter.width(), body.top(),
                  std::max<qreal>(0.0, body.width() - gutter.width()), body.height());
}

QRectF Schedule::dayColumnRect(int column) const
{
    if (column < 0 || column >= columnCount())
        return QRectF();

    const QRectF grid = gridRect();
    const int columns = columnCount();
    const qreal cellWidth = grid.width() / qreal(columns);
    const int visual = isRightToLeft() ? columns - 1 - column : column;
    const qreal left = grid.left() + qreal(visual) * cellWidth;
    const qreal width = visual == columns - 1 ? std::max<qreal>(0.0, grid.right() - left)
                                              : cellWidth;

    return QRectF(left, grid.top(), std::max<qreal>(0.0, width), grid.height());
}

QRectF Schedule::weekdayHeaderRect(int column) const
{
    const QRectF cell = dayColumnRect(column);
    if (cell.isEmpty())
        return QRectF();

    return QRectF(cell.left(), headerRect().top(), cell.width(), headerRect().height());
}

qreal Schedule::yForMinutes(int minutes) const
{
    const qreal perMinute = hourHeight() / 60.0;
    return bodyRect().top()
           + qreal(minutes - rangeStartMinutes()) * perMinute
           - scrollOffset_;
}

QRectF Schedule::hourLabelRect(int hour) const
{
    if (hour < 0 || hour > 24)
        return QRectF();

    const int minutes = hour * 60;
    if (minutes < rangeStartMinutes() || minutes > rangeEndMinutes())
        return QRectF();

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFont labelFont = fontFor(theme, theme.type().labelSmall);
    const QFontMetricsF metrics(labelFont);
    const QRectF gutter = gutterRect();
    const qreal inset = density.dp(kHourLabelInsetDp);

    return QRectF(gutter.left() + inset, yForMinutes(minutes) + density.dp(kHourLabelTopDp),
                  std::max<qreal>(0.0, gutter.width() - 2.0 * inset),
                  std::max<qreal>(0.0, metrics.height()));
}

int Schedule::columnForDate(const QDate& date) const
{
    if (!date_.isValid() || !date.isValid())
        return -1;

    const qint64 offset = date_.daysTo(date);
    if (offset < 0 || offset >= columnCount())
        return -1;

    return static_cast<int>(offset);
}

QVector<Schedule::PlacedEvent> Schedule::layoutEvents() const
{
    QVector<PlacedEvent> placed;
    const int columns = columnCount();
    if (columns <= 0 || events_.isEmpty())
        return placed;

    struct RawEvent
    {
        int index = -1;
        int startMinutes = 0;
        int endMinutes = 0;
    };

    QVector<QVector<RawEvent>> buckets;
    buckets.resize(columns);

    const int dayStart = rangeStartMinutes();
    const int dayEnd = rangeEndMinutes();
    const int eventCount = static_cast<int>(events_.size());

    for (int index = 0; index < eventCount; ++index) {
        const ScheduleEvent& event = events_.at(index);
        if (!event.start.isValid() || !event.end.isValid() || event.end <= event.start)
            continue;

        const QDate day = event.start.date();
        const int column = columnForDate(day);
        if (column < 0)
            continue;

        const QTime startTime = event.start.time();
        int startMinutes = startTime.hour() * 60 + startTime.minute();

        int endMinutes = 0;
        if (event.end.date() > day)
            endMinutes = dayEnd;
        else
            endMinutes = event.end.time().hour() * 60 + event.end.time().minute();

        // Clip to the visible window before assigning lanes, so an event that
        // starts before the range still overlaps correctly.
        if (endMinutes <= dayStart || startMinutes >= dayEnd)
            continue;

        startMinutes = std::max(startMinutes, dayStart);
        endMinutes = std::min(endMinutes, dayEnd);
        if (endMinutes <= startMinutes)
            continue;

        buckets[column].append(RawEvent{index, startMinutes, endMinutes});
    }

    for (int column = 0; column < columns; ++column) {
        QVector<RawEvent> list = buckets.at(column);
        if (list.isEmpty())
            continue;

        std::sort(list.begin(), list.end(), [](const RawEvent& lhs, const RawEvent& rhs) {
            if (lhs.startMinutes != rhs.startMinutes)
                return lhs.startMinutes < rhs.startMinutes;
            if (lhs.endMinutes != rhs.endMinutes)
                return lhs.endMinutes < rhs.endMinutes;
            return lhs.index < rhs.index;
        });

        // laneEnds[l] is the end of the event currently occupying lane l in
        // this overlap cluster. An empty vector starts a cluster.
        QVector<int> laneEnds;
        QVector<int> clusterOf;
        QVector<int> clusterLaneCounts;
        const int base = static_cast<int>(placed.size());

        for (const RawEvent& raw : list) {
            const bool noActiveLane = std::all_of(
                laneEnds.cbegin(), laneEnds.cend(),
                [start = raw.startMinutes](int laneEnd) { return laneEnd <= start; });

            int cluster = clusterOf.isEmpty() ? -1 : clusterOf.last();
            if (cluster < 0 || noActiveLane) {
                laneEnds.clear();
                clusterLaneCounts.append(1);
                cluster = static_cast<int>(clusterLaneCounts.size()) - 1;
            }

            int lane = -1;
            const int laneTotal = static_cast<int>(laneEnds.size());
            for (int candidate = 0; candidate < laneTotal; ++candidate) {
                if (laneEnds.at(candidate) <= raw.startMinutes) {
                    lane = candidate;
                    break;
                }
            }

            if (lane < 0) {
                lane = laneTotal;
                laneEnds.append(raw.endMinutes);
            } else {
                laneEnds[lane] = raw.endMinutes;
            }

            if (laneEnds.size() > clusterLaneCounts.at(cluster))
                clusterLaneCounts[cluster] = static_cast<int>(laneEnds.size());

            clusterOf.append(cluster);

            PlacedEvent entry;
            entry.index = raw.index;
            entry.column = column;
            entry.lane = lane;
            entry.laneCount = 1;
            entry.startMinutes = raw.startMinutes;
            entry.endMinutes = raw.endMinutes;
            placed.append(entry);
        }

        // The cluster is only complete now; every member shares its lane count.
        for (int offset = 0; offset < static_cast<int>(list.size()); ++offset) {
            const int target = base + offset;
            placed[target].laneCount = clusterLaneCounts.at(clusterOf.at(offset));
        }
    }

    return placed;
}

QRectF Schedule::rectFor(const PlacedEvent& placed) const
{
    const QRectF column = dayColumnRect(placed.column);
    if (column.isEmpty())
        return QRectF();

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const int lanes = std::max(1, placed.laneCount);
    const qreal laneWidth = column.width() / qreal(lanes);
    const int visualLane = isRightToLeft() ? lanes - 1 - placed.lane : placed.lane;
    const qreal inset = density.dp(kEventInsetDp);

    const qreal left = column.left() + qreal(visualLane) * laneWidth + inset;
    const qreal width = std::max<qreal>(0.0, laneWidth - 2.0 * inset);

    qreal top = yForMinutes(placed.startMinutes) + inset;
    qreal bottom = yForMinutes(placed.endMinutes) - inset;
    if (bottom - top < density.dp(kMinimumEventHeightDp))
        bottom = top + density.dp(kMinimumEventHeightDp);

    // The dragged block follows the pointer before the release signal exists.
    if (dragging_ && placed.index == pressedEvent_) {
        const qreal shift = qreal(dragDeltaMinutes_) / 60.0 * hourHeight();
        top += shift;
        bottom += shift;
    }

    return QRectF(left, top, width, bottom - top);
}

QRectF Schedule::eventRect(int index) const
{
    if (index < 0 || index >= static_cast<int>(events_.size()))
        return QRectF();

    const QVector<PlacedEvent> placed = layoutEvents();
    for (const PlacedEvent& entry : placed) {
        if (entry.index == index)
            return rectFor(entry);
    }

    return QRectF();
}

QRectF Schedule::eventRect(const QString& id) const
{
    return eventRect(indexOfEvent(id));
}

int Schedule::eventAt(const QPointF& point) const
{
    const QVector<PlacedEvent> placed = layoutEvents();
    for (int position = static_cast<int>(placed.size()) - 1; position >= 0; --position) {
        const PlacedEvent& entry = placed.at(position);
        if (rectFor(entry).contains(point))
            return entry.index;
    }

    return -1;
}

int Schedule::laneFor(int index) const
{
    if (index < 0 || index >= static_cast<int>(events_.size()))
        return -1;

    const QVector<PlacedEvent> placed = layoutEvents();
    for (const PlacedEvent& entry : placed) {
        if (entry.index == index)
            return entry.lane;
    }

    return -1;
}

int Schedule::laneCount(int index) const
{
    if (index < 0 || index >= static_cast<int>(events_.size()))
        return 0;

    const QVector<PlacedEvent> placed = layoutEvents();
    for (const PlacedEvent& entry : placed) {
        if (entry.index == index)
            return entry.laneCount;
    }

    return 0;
}

int Schedule::indexOfEvent(const QString& id) const
{
    if (id.isEmpty())
        return -1;

    const int count = static_cast<int>(events_.size());
    for (int index = 0; index < count; ++index) {
        if (events_.at(index).id == id)
            return index;
    }

    return -1;
}

// ---------------------------------------------------------------------------
// Scrolling
// ---------------------------------------------------------------------------

qreal Schedule::contentHeight() const
{
    return qreal(rangeEndMinutes() - rangeStartMinutes()) / 60.0 * hourHeight();
}

qreal Schedule::maximumScrollOffset() const
{
    return std::max<qreal>(0.0, contentHeight() - bodyRect().height());
}

void Schedule::clampScroll()
{
    scrollOffset_ = std::clamp(scrollOffset_, qreal(0.0), maximumScrollOffset());
}

void Schedule::setScrollOffset(qreal offset)
{
    if (!std::isfinite(offset))
        return;

    const qreal next = std::clamp(offset, qreal(0.0), maximumScrollOffset());
    if (qFuzzyCompare(next + 1.0, scrollOffset_ + 1.0))
        return;

    scrollOffset_ = next;
    requestUpdate();
}

// ---------------------------------------------------------------------------
// Measurement and painting
// ---------------------------------------------------------------------------

void Schedule::measureOverride(MeasureSpec width, MeasureSpec height)
{
    try {
        const Density& density = Theme::forWidget(this).density();
        const qreal preferredWidth = density.dp(mode_ == Mode::Day ? kPreferredDayWidthDp
                                                                   : kPreferredWeekWidthDp);
        setMeasuredSize(QSizeF(width.resolve(preferredWidth),
                               height.resolve(density.dp(kPreferredHeightDp))));
    } catch (const std::exception& error) {
        qWarning("md::Schedule::measureOverride failed: %s", error.what());
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    } catch (...) {
        qWarning("md::Schedule::measureOverride failed with an unknown exception");
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    }
}

void Schedule::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    painter.save();
    try {
        const Theme& theme = Theme::forWidget(this);
        const ColorScheme& colours = theme.colours();
        const Density& density = theme.density();
        const qreal unit = density.dp(1.0);

        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setOpacity(isEnabled() ? 1.0 : 0.38);
        painter.setClipRect(content);

        const QRectF header = headerRect();
        const QRectF body = bodyRect();
        const QRectF gutter = gutterRect();
        const QRectF grid = gridRect();

        // Header and gutter sit one surface step above the body, so the time
        // strip reads as chrome rather than as a column of events.
        painter.fillRect(header, colours.surfaceContainerLow);
        painter.fillRect(gutter, colours.surfaceContainerLow);

        // Hour lines, column separators and the gutter edge. `outlineVariant`
        // at 60 % is the lightest role that still reads against both baselines.
        QColor gridColour = colours.outlineVariant;
        gridColour.setAlphaF(0.6f);

        painter.setPen(QPen(gridColour, unit, Qt::SolidLine));
        if (!body.isEmpty()) {
            const int firstHour = (rangeStartMinutes() + 59) / 60;
            const int lastHour = rangeEndMinutes() / 60;
            for (int hour = firstHour; hour <= lastHour; ++hour) {
                const qreal y = yForMinutes(hour * 60);
                if (y < body.top() - unit || y > body.bottom() + unit)
                    continue;
                painter.drawLine(QPointF(grid.left(), y), QPointF(grid.right(), y));
            }

            const qreal cellWidth = grid.width() / qreal(columnCount());
            for (int boundary = 1; boundary < columnCount(); ++boundary) {
                const qreal x = grid.left() + qreal(boundary) * cellWidth;
                painter.drawLine(QPointF(x, body.top()), QPointF(x, body.bottom()));
            }

            const qreal gutterEdge = isRightToLeft() ? gutter.left() : gutter.right();
            painter.drawLine(QPointF(gutterEdge, body.top()),
                             QPointF(gutterEdge, body.bottom()));
        }
        painter.drawLine(QPointF(header.left(), header.bottom()),
                         QPointF(header.right(), header.bottom()));

        // Hour labels, top-aligned with their line so a 09:00 event starts
        // below the 08:00 label rather than on top of it.
        const QFont labelFont = fontFor(theme, theme.type().labelSmall);
        const QFont headerFont = fontFor(theme, theme.type().labelMedium);
        const QFont eventFont = fontFor(theme, theme.type().labelMedium);

        painter.setFont(labelFont);
        painter.setPen(colours.onSurfaceVariant);
        const Qt::Alignment hourAlignment = isRightToLeft() ? Qt::AlignLeft : Qt::AlignRight;
        const int firstLabelHour = (rangeStartMinutes() + 59) / 60;
        const int lastLabelHour = rangeEndMinutes() / 60;
        for (int hour = firstLabelHour; hour <= lastLabelHour; ++hour) {
            const QRectF label = hourLabelRect(hour);
            if (label.isEmpty() || label.bottom() < body.top() || label.top() > body.bottom())
                continue;
            painter.drawText(label, hourAlignment | Qt::AlignTop, hourText(hour));
        }

        // Weekday header: weekday and day number in Week mode, the full date in
        // Day mode. Today is the only date tinted with the primary role.
        const QLocale locale;
        const QDate today = QDate::currentDate();
        painter.setFont(headerFont);
        const QFontMetricsF headerMetrics(headerFont);
        for (int column = 0; column < columnCount(); ++column) {
            const QRectF cell = weekdayHeaderRect(column);
            if (cell.isEmpty())
                continue;

            const QDate day = columnDate(column);
            const QString text = mode_ == Mode::Day
                ? locale.toString(day, QStringLiteral("dddd, d MMMM"))
                : locale.toString(day, QStringLiteral("ddd d"));

            painter.setPen(day == today ? colours.primary : colours.onSurface);
            const QRectF textRect = cell.adjusted(density.dp(kHeaderTextInsetDp), 0.0,
                                                  -density.dp(kHeaderTextInsetDp), 0.0);
            painter.drawText(textRect, Qt::AlignCenter,
                             headerMetrics.elidedText(text, Qt::ElideRight,
                                                      textRect.width()));
        }

        // Events. The clip is the grid, so a block dragged past the range or
        // over the gutter cannot paint outside its columns.
        const QVector<PlacedEvent> placed = layoutEvents();
        painter.save();
        painter.setClipRect(grid.intersected(content));
        const QFontMetricsF eventMetrics(eventFont);
        const qreal barWidth = density.dp(kAccentBarWidthDp);

        for (const PlacedEvent& entry : placed) {
            const QRectF rect = rectFor(entry);
            if (rect.isEmpty() || !rect.intersects(body))
                continue;

            const ScheduleEvent& event = events_.at(entry.index);
            const QColor accent = event.accent.isValid() ? event.accent : colours.primary;
            QColor fill = accent;
            fill.setAlphaF(0.20f);

            const qreal radius = eventRadius();
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
            painter.drawRoundedRect(rect, radius, radius);

            const QRectF bar = isRightToLeft()
                ? QRectF(rect.right() - barWidth, rect.top(), barWidth, rect.height())
                : QRectF(rect.left(), rect.top(), barWidth, rect.height());
            painter.setBrush(accent);
            painter.drawRect(bar);

            if (!selectedId_.isEmpty() && event.id == selectedId_) {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(colours.primary, density.dp(kEventBorderDp)));
                painter.drawRoundedRect(rect.adjusted(unit, unit, -unit, -unit), radius, radius);
            }

            if (event.title.isEmpty() || rect.height() < density.dp(kMinimumEventHeightDp))
                continue;

            painter.save();
            painter.setClipRect(rect.intersected(body));
            const qreal textLeft = isRightToLeft()
                ? rect.left() + density.dp(kEventPaddingDp)
                : rect.left() + barWidth + density.dp(kEventPaddingDp);
            const qreal textRight = isRightToLeft()
                ? rect.right() - barWidth - density.dp(kEventPaddingDp)
                : rect.right() - density.dp(kEventPaddingDp);
            const QRectF textRect(textLeft, rect.top(),
                                  std::max<qreal>(0.0, textRight - textLeft), rect.height());

            const Qt::Alignment textAlignment = isRightToLeft() ? Qt::AlignRight : Qt::AlignLeft;
            painter.setFont(eventFont);
            painter.setPen(colours.onSurface);
            painter.drawText(textRect, textAlignment | Qt::AlignVCenter,
                             eventMetrics.elidedText(event.title, Qt::ElideRight,
                                                     textRect.width()));
            painter.restore();
        }
        painter.restore();

        // The now line belongs to today's column only; drawing it across the
        // whole week would claim every day is at the same local time.
        if (nowLineVisible_ && !body.isEmpty()) {
            const QDateTime now = QDateTime::currentDateTime();
            const int column = columnForDate(now.date());
            const int minutes = now.time().hour() * 60 + now.time().minute();
            if (column >= 0 && minutes >= rangeStartMinutes()
                && minutes <= rangeEndMinutes()) {
                const qreal y = yForMinutes(minutes);
                if (y >= body.top() && y <= body.bottom()) {
                    const QRectF day = dayColumnRect(column);
                    painter.setPen(QPen(colours.primary, density.dp(kNowLineWidthDp),
                                        Qt::SolidLine, Qt::RoundCap));
                    painter.drawLine(QPointF(day.left() + unit, y),
                                     QPointF(day.right() - unit, y));
                }
            }
        }

        // A thin indicator instead of a Scrollbar child: the track shows how
        // much of the range is visible, the thumb follows scrollOffset().
        const qreal maximum = maximumScrollOffset();
        if (maximum > 0.0 && body.height() > 0.0) {
            const qreal indicatorWidth = density.dp(kScrollIndicatorWidthDp);
            const qreal margin = density.dp(kScrollIndicatorMarginDp);
            const qreal x = isRightToLeft() ? body.left() + margin
                                            : body.right() - margin - indicatorWidth;
            const QRectF track(x, body.top() + margin, indicatorWidth,
                               std::max<qreal>(0.0, body.height() - 2.0 * margin));

            if (!track.isEmpty()) {
                QColor trackColour = colours.outlineVariant;
                trackColour.setAlphaF(0.5f);
                painter.setPen(Qt::NoPen);
                painter.setBrush(trackColour);
                painter.drawRoundedRect(track, indicatorWidth / 2.0, indicatorWidth / 2.0);

                const qreal thumbHeight = std::min(
                    track.height(),
                    std::max(density.dp(kScrollThumbMinDp),
                             track.height() * (body.height() / contentHeight())));
                const qreal travel = std::max<qreal>(0.0, track.height() - thumbHeight);
                const qreal fraction = std::clamp(scrollOffset_ / maximum, qreal(0.0), qreal(1.0));
                const QRectF thumb(track.left(), track.top() + fraction * travel,
                                   track.width(), thumbHeight);
                painter.setBrush(colours.primary);
                painter.drawRoundedRect(thumb, indicatorWidth / 2.0, indicatorWidth / 2.0);
            }
        }
    } catch (const std::exception& error) {
        qWarning("md::Schedule::paintContent failed: %s", error.what());
    } catch (...) {
        qWarning("md::Schedule::paintContent failed with an unknown exception");
    }
    painter.restore();
}

// ---------------------------------------------------------------------------
// Pointer interaction
// ---------------------------------------------------------------------------

void Schedule::mousePressEvent(QMouseEvent* event)
{
    try {
        if (!isEnabled()) {
            View::mousePressEvent(event);
            return;
        }

        pressedPosition_ = event->position();
        pressedEvent_ = -1;
        dragDeltaMinutes_ = 0;
        dragging_ = false;

        if (event->button() == Qt::LeftButton) {
            pressedEvent_ = eventAt(pressedPosition_);
            if (pressedEvent_ >= 0)
                setSelectedEvent(events_.at(pressedEvent_).id);
            else
                setSelectedEvent(QString());
        }

        View::mousePressEvent(event);
    } catch (const std::exception& error) {
        qWarning("md::Schedule::mousePressEvent failed: %s", error.what());
    } catch (...) {
        qWarning("md::Schedule::mousePressEvent failed with an unknown exception");
    }
}

void Schedule::mouseMoveEvent(QMouseEvent* event)
{
    try {
        if (!isEnabled()) {
            View::mouseMoveEvent(event);
            return;
        }

        if (pressedEvent_ >= 0 && editable_ && event->buttons().testFlag(Qt::LeftButton)) {
            const qreal deltaY = event->position().y() - pressedPosition_.y();
            if (!dragging_ && qAbs(deltaY) >= Theme::forWidget(this).density().dp(kDragThresholdDp))
                dragging_ = true;

            if (dragging_) {
                const int snapped = snappedDeltaMinutes(deltaY);
                if (snapped != dragDeltaMinutes_) {
                    dragDeltaMinutes_ = snapped;
                    requestUpdate();
                }
            }
        }

        View::mouseMoveEvent(event);
    } catch (const std::exception& error) {
        qWarning("md::Schedule::mouseMoveEvent failed: %s", error.what());
    } catch (...) {
        qWarning("md::Schedule::mouseMoveEvent failed with an unknown exception");
    }
}

void Schedule::mouseReleaseEvent(QMouseEvent* event)
{
    try {
        const int pressed = pressedEvent_;
        const bool wasDragging = dragging_;
        const int delta = dragDeltaMinutes_;

        if (!isEnabled()) {
            pressedEvent_ = -1;
            dragging_ = false;
            dragDeltaMinutes_ = 0;
            pressedPosition_ = QPointF(-1.0, -1.0);
            View::mouseReleaseEvent(event);
            return;
        }

        pressedEvent_ = -1;
        dragging_ = false;
        dragDeltaMinutes_ = 0;
        pressedPosition_ = QPointF(-1.0, -1.0);

        if (event->button() == Qt::LeftButton && pressed >= 0
            && pressed < static_cast<int>(events_.size())) {
            const QString id = events_.at(pressed).id;

            if (editable_ && wasDragging) {
                if (delta != 0) {
                    const QDateTime newStart = events_.at(pressed).start.addSecs(
                        qint64(delta) * 60);
                    emit eventMoved(id, newStart);
                }
            } else if (!wasDragging && eventAt(event->position()) == pressed) {
                emit eventActivated(id);
            }
        }

        requestUpdate();
        View::mouseReleaseEvent(event);
    } catch (const std::exception& error) {
        qWarning("md::Schedule::mouseReleaseEvent failed: %s", error.what());
    } catch (...) {
        qWarning("md::Schedule::mouseReleaseEvent failed with an unknown exception");
    }
}

// The keyboard equivalent of the pointer gestures (WCAG 2.5.7). Movement is
// only available while editable; Enter always activates the selection.
void Schedule::keyPressEvent(QKeyEvent* keyEvent)
{
    try {
        if (!isEnabled()) {
            View::keyPressEvent(keyEvent);
            return;
        }

        const int index = indexOfEvent(selectedId_);
        if (index >= 0) {
            const bool alt = keyEvent->modifiers().testFlag(Qt::AltModifier);

            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                const QString id = events_.at(index).id;
                emit eventActivated(id);
                keyEvent->accept();
                return;
            }

            if (alt && editable_) {
                int minutes = 0;
                int days = 0;
                switch (keyEvent->key()) {
                case Qt::Key_Left:
                    minutes = -kSnapMinutes;
                    break;
                case Qt::Key_Right:
                    minutes = kSnapMinutes;
                    break;
                case Qt::Key_Up:
                    days = -1;
                    break;
                case Qt::Key_Down:
                    days = 1;
                    break;
                default:
                    break;
                }

                if (minutes != 0 || days != 0) {
                    const ScheduleEvent& event = events_.at(index);
                    const QString id = event.id;
                    const QDateTime newStart = days != 0 ? event.start.addDays(days)
                                                        : event.start.addSecs(qint64(minutes) * 60);
                    if (newStart.isValid() && newStart != event.start)
                        emit eventMoved(id, newStart);
                    keyEvent->accept();
                    return;
                }
            }
        }

        View::keyPressEvent(keyEvent);
    } catch (const std::exception& error) {
        qWarning("md::Schedule::keyPressEvent failed: %s", error.what());
    } catch (...) {
        qWarning("md::Schedule::keyPressEvent failed with an unknown exception");
    }
}

void Schedule::wheelEvent(QWheelEvent* wheel)
{
    try {
        if (!isEnabled()) {
            wheel->ignore();
            return;
        }

        qreal delta = 0.0;
        const QPoint angle = wheel->angleDelta();
        if (!angle.isNull())
            delta = -qreal(angle.y()) / 120.0 * hourHeight();
        else
            delta = -wheel->pixelDelta().y();

        if (!qFuzzyIsNull(delta))
            setScrollOffset(scrollOffset_ + delta);

        wheel->accept();
    } catch (const std::exception& error) {
        qWarning("md::Schedule::wheelEvent failed: %s", error.what());
        wheel->ignore();
    } catch (...) {
        qWarning("md::Schedule::wheelEvent failed with an unknown exception");
        wheel->ignore();
    }
}

void Schedule::resizeEvent(QResizeEvent* event)
{
    try {
        View::resizeEvent(event);
        clampScroll();
    } catch (const std::exception& error) {
        qWarning("md::Schedule::resizeEvent failed: %s", error.what());
    } catch (...) {
        qWarning("md::Schedule::resizeEvent failed with an unknown exception");
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

int Schedule::snappedDeltaMinutes(qreal deltaPixels) const
{
    const qreal hour = hourHeight();
    if (!(hour > 0.0))
        return 0;

    const qreal minutes = deltaPixels / hour * 60.0;
    return static_cast<int>(qRound(minutes / qreal(kSnapMinutes))) * kSnapMinutes;
}

void Schedule::updateAccessibleDescription()
{
    const QString day = date_.toString(Qt::ISODate);
    if (mode_ == Mode::Week)
        setAccessibleDescription(tr("Week of %1").arg(day));
    else
        setAccessibleDescription(day);
}

} // namespace md
