// Periodicity — the Clock component.
//
// The paint path deliberately resolves everything from Theme::forWidget(this):
// the dial face, ticks, hands and text are token colours, and every dimension
// is a dp or sp value converted by the current density. Two layout decisions
// are made from the measured box: an inset dial smaller than 96 dp falls back
// to the digital read-out, and "Both" stacks the read-out under the dial below
// 240 dp when the dial can still keep its 96 dp minimum; otherwise the
// read-out is centred on the dial with a tonal backing plate so the hands do
// not run through the digits.
//
// Time is one instant sampled per paint. `frozen_` is used while stopped and
// QDateTime::currentDateTime() while running; either value is converted to
// zone_ before time()/date() or the face is painted, so changing the zone
// re-expresses the same instant instead of changing it.
//
// Every protected override and the timer slot has its own exception boundary:
// a throwing observer (for example a tick() slot connected by the host) is
// reported with qWarning and never unwinds into Qt.

#include <md/components/clock.h>

#include <md/theme/theme.h>

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QHideEvent>
#include <QLocale>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QShowEvent>
#include <QString>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <exception>

namespace md {

namespace {

constexpr qreal kPreferredSizeDp = 200.0;
constexpr qreal kDialInsetDp = 8.0;
constexpr qreal kMinimumDialDp = 96.0;
constexpr qreal kCardinalDigitsDp = 120.0;
constexpr qreal kCompactReadoutDp = 240.0;
constexpr int kTimerIntervalMs = 1000;

constexpr qreal kMajorTickLengthDp = 6.0;
constexpr qreal kMinorTickLengthDp = 3.0;
constexpr qreal kMajorTickWidthDp = 2.0;
constexpr qreal kMinorTickWidthDp = 1.0;

constexpr qreal kHourHandLengthFactor = 0.50;
constexpr qreal kMinuteHandLengthFactor = 0.72;
constexpr qreal kSecondHandLengthFactor = 0.84;
constexpr qreal kHourHandWidthDp = 5.0;
constexpr qreal kMinuteHandWidthDp = 3.5;
constexpr qreal kSecondHandWidthDp = 1.5;

constexpr qreal kPi = 3.14159265358979323846;

[[nodiscard]] QFont fontFor(const Theme& theme, const TextStyle& style)
{
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(QFont::Weight(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
    return font;
}

[[nodiscard]] QString twoDigits(int value)
{
    return QStringLiteral("%1").arg(value, 2, 10, QLatin1Char('0'));
}

// The main time line, without the meridiem. 24-hour text always has a leading
// zero; 12-hour text never does, which is the design baseline clock convention.
[[nodiscard]] QString formatTime(const QTime& time, bool use24Hour, bool showSeconds)
{
    if (!time.isValid())
        return QString();

    const int hour = use24Hour ? time.hour()
                               : (time.hour() % 12 == 0 ? 12 : time.hour() % 12);

    QString text = use24Hour ? twoDigits(hour) : QString::number(hour);
    text += QLatin1Char(':');
    text += twoDigits(time.minute());

    if (showSeconds) {
        text += QLatin1Char(':');
        text += twoDigits(time.second());
    }

    return text;
}

[[nodiscard]] QString meridiemText(const QTime& time)
{
    if (!time.isValid())
        return QString();

    const QLocale locale;
    return time.hour() < 12 ? locale.amText() : locale.pmText();
}

[[nodiscard]] QString dateText(const QDate& date)
{
    if (!date.isValid())
        return QString();

    return QLocale().toString(date, QLocale::ShortFormat);
}

[[nodiscard]] QRectF squareIn(const QRectF& box)
{
    const qreal side = std::min(box.width(), box.height());
    if (side <= 0.0)
        return QRectF();

    return QRectF(box.center().x() - side / 2.0, box.center().y() - side / 2.0,
                  side, side);
}

[[nodiscard]] QPointF pointOnCircle(const QPointF& centre, qreal radius, qreal degrees)
{
    const qreal radians = degrees * kPi / 180.0;
    return QPointF(centre.x() + std::cos(radians) * radius,
                   centre.y() + std::sin(radians) * radius);
}

// The measured text of one read-out. Building it once lets the caller decide
// the layout (centred over the dial, or in a band under it) and then paint the
// same strings and metrics.
struct Readout
{
    Readout(const Theme& theme, const TextStyle& timeStyle, const QDateTime& instant,
            bool use24Hour, bool showSeconds, bool showDate)
        : timeFont(fontFor(theme, timeStyle)),
          metaFont(fontFor(theme, theme.type().labelMedium)),
          timeMetrics(timeFont),
          metaMetrics(metaFont)
    {
        time = formatTime(instant.time(), use24Hour, showSeconds);
        if (!use24Hour)
            meridiem = meridiemText(instant.time());
        if (showDate)
            date = dateText(instant.date());

        const Density& density = theme.density();
        meridiemGap = meridiem.isEmpty() ? 0.0 : density.dp(4.0);
        timeWidth = timeMetrics.horizontalAdvance(time);
        meridiemWidth = meridiem.isEmpty() ? 0.0 : metaMetrics.horizontalAdvance(meridiem);
        lineWidth = timeWidth + meridiemGap + meridiemWidth;
        lineHeight = std::max(timeMetrics.height(), metaMetrics.height());
        dateGap = date.isEmpty() ? 0.0 : density.dp(2.0);
        dateHeight = date.isEmpty() ? 0.0 : metaMetrics.height();
        height = lineHeight + dateGap + dateHeight;
    }

    QFont timeFont;
    QFont metaFont;
    QFontMetricsF timeMetrics;
    QFontMetricsF metaMetrics;

    QString time;
    QString meridiem;
    QString date;

    qreal meridiemGap = 0.0;
    qreal timeWidth = 0.0;
    qreal meridiemWidth = 0.0;
    qreal lineWidth = 0.0;
    qreal lineHeight = 0.0;
    qreal dateGap = 0.0;
    qreal dateHeight = 0.0;
    qreal height = 0.0;
};

void paintReadout(QPainter& painter, const QRectF& area, const Theme& theme,
                  const Readout& readout, bool plate)
{
    if (area.isEmpty() || readout.time.isEmpty())
        return;

    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    const qreal top = area.center().y() - readout.height / 2.0;
    const qreal left = area.center().x() - readout.lineWidth / 2.0;

    if (plate) {
        // A tonal plate keeps the numerals readable where the hands pass behind
        // them. It is the same role as the dial, so only the silhouette of the
        // hands disappears; the read-out does not introduce a new surface tone.
        const qreal padX = density.dp(10.0);
        const qreal padY = density.dp(4.0);
        const qreal dateWidth = readout.date.isEmpty()
            ? 0.0
            : readout.metaMetrics.horizontalAdvance(readout.date);
        const qreal blockWidth = std::max(readout.lineWidth, dateWidth);

        const QRectF block(area.center().x() - blockWidth / 2.0 - padX,
                           top - padY,
                           blockWidth + 2.0 * padX,
                           readout.height + 2.0 * padY);

        painter.setPen(Qt::NoPen);
        painter.setBrush(colours.surfaceContainerHighest);
        painter.drawRoundedRect(block, density.dp(8.0), density.dp(8.0));
    }

    painter.setFont(readout.timeFont);
    painter.setPen(colours.onSurface);
    painter.drawText(QRectF(left, top, readout.timeWidth, readout.lineHeight),
                     static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), readout.time);

    if (!readout.meridiem.isEmpty()) {
        painter.setFont(readout.metaFont);
        painter.setPen(colours.onSurfaceVariant);
        painter.drawText(QRectF(left + readout.timeWidth + readout.meridiemGap, top,
                                readout.meridiemWidth, readout.lineHeight),
                         static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                         readout.meridiem);
    }

    if (!readout.date.isEmpty()) {
        painter.setFont(readout.metaFont);
        painter.setPen(colours.onSurfaceVariant);
        painter.drawText(QRectF(area.left(), top + readout.lineHeight + readout.dateGap,
                                area.width(), readout.dateHeight),
                         static_cast<int>(Qt::AlignHCenter | Qt::AlignTop), readout.date);
    }
}

void paintDial(QPainter& painter, const QRectF& dial, const QTime& time, const Theme& theme)
{
    if (dial.isEmpty() || !time.isValid())
        return;

    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const QPointF centre = dial.center();
    const qreal radius = std::min(dial.width(), dial.height()) / 2.0;
    if (radius <= 0.0)
        return;

    painter.setPen(Qt::NoPen);
    painter.setBrush(colours.surfaceContainerHighest);
    painter.drawEllipse(dial);

    // 24 single-line ticks, every sixth one at a cardinal and drawn longer.
    const qreal tickOuter = radius - density.dp(2.0);
    for (int tick = 0; tick < 24; ++tick) {
        const bool cardinal = tick % 6 == 0;
        const qreal length = density.dp(cardinal ? kMajorTickLengthDp : kMinorTickLengthDp);
        const qreal width = density.dp(cardinal ? kMajorTickWidthDp : kMinorTickWidthDp);
        if (tickOuter <= 0.0 || length <= 0.0 || width <= 0.0)
            continue;

        const qreal angle = -90.0 + tick * 15.0;
        const QPointF outer = pointOnCircle(centre, tickOuter, angle);
        const QPointF base = pointOnCircle(centre, std::max(qreal(0.0), tickOuter - length),
                                          angle);

        painter.setPen(QPen(colours.onSurfaceVariant, width, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(base, outer);
    }

    // Cardinal digits only when the dial is large enough to keep them clear of
    // the ticks and the hands.
    if (radius * 2.0 >= density.dp(kCardinalDigitsDp)) {
        painter.setFont(fontFor(theme, theme.type().labelMedium));
        painter.setPen(colours.onSurfaceVariant);

        const qreal digitRadius = std::max(qreal(0.0), radius - density.dp(22.0));
        const qreal box = density.dp(20.0);
        const char* const labels[4] = {"12", "3", "6", "9"};

        for (int index = 0; index < 4; ++index) {
            const qreal angle = -90.0 + index * 90.0;
            const QPointF anchor = pointOnCircle(centre, digitRadius, angle);
            const QRectF digitRect(anchor.x() - box / 2.0, anchor.y() - box / 2.0, box, box);
            painter.drawText(digitRect, static_cast<int>(Qt::AlignCenter),
                             QString::fromLatin1(labels[index]));
        }
    }

    // The hour hand advances continuously with the minute (and the minute hand
    // with the second), while the second hand is a whole-second step because
    // QTime has no sub-second component here.
    const qreal hourAngle = -90.0
        + (static_cast<qreal>(time.hour() % 12) + time.minute() / 60.0
           + time.second() / 3600.0) * 30.0;
    const qreal minuteAngle = -90.0
        + (static_cast<qreal>(time.minute()) + time.second() / 60.0) * 6.0;
    const qreal secondAngle = -90.0 + static_cast<qreal>(time.second()) * 6.0;

    const auto drawHand = [&painter, &centre](qreal angle, qreal length, qreal width,
                                              const QColor& colour) {
        if (length <= 0.0 || width <= 0.0)
            return;

        painter.setPen(QPen(colour, width, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(centre, pointOnCircle(centre, length, angle));
    };

    drawHand(hourAngle, radius * kHourHandLengthFactor, density.dp(kHourHandWidthDp),
             colours.onSurface);
    drawHand(minuteAngle, radius * kMinuteHandLengthFactor, density.dp(kMinuteHandWidthDp),
             colours.onSurface);
    drawHand(secondAngle, radius * kSecondHandLengthFactor, density.dp(kSecondHandWidthDp),
             colours.primary);

    painter.setPen(Qt::NoPen);
    painter.setBrush(colours.primary);
    const qreal pin = density.dp(2.5);
    painter.drawEllipse(centre, pin, pin);
}

} // namespace

Clock::Clock(QWidget* parent)
    : View(parent),
      frozen_(QDateTime::currentDateTime())
{
    zone_ = QTimeZone::systemTimeZone();
    if (!zone_.isValid())
        zone_ = QTimeZone(QTimeZone::UTC);

    setBackgroundColour(QColor(Qt::transparent));
    setFocusPolicy(Qt::NoFocus);
    setAccessibleName(tr("Clock"));

    timer_.setInterval(kTimerIntervalMs);
    // A precise timer keeps the second hand stepping on the second rather than
    // up to 5% late, which is also what the one-second tick contract promises.
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &Clock::onTick);

    updateAccessibleDescription();
}

void Clock::setDisplay(Display display)
{
    if (display_ == display)
        return;

    display_ = display;
    requestUpdate();
}

Clock::Display Clock::display() const noexcept
{
    return display_;
}

void Clock::setTimeZone(const QTimeZone& zone)
{
    QTimeZone normalized = zone;
    if (!normalized.isValid())
        normalized = QTimeZone::systemTimeZone();
    if (!normalized.isValid())
        normalized = QTimeZone(QTimeZone::UTC);

    if (zone_ == normalized)
        return;

    zone_ = normalized;
    updateAccessibleDescription();
    requestUpdate();
}

QTimeZone Clock::timeZone() const
{
    return zone_;
}

void Clock::setUse24Hour(bool use24Hour)
{
    if (use24Hour_ == use24Hour)
        return;

    use24Hour_ = use24Hour;
    updateAccessibleDescription();
    requestUpdate();
}

bool Clock::uses24Hour() const noexcept
{
    return use24Hour_;
}

void Clock::setShowSeconds(bool showSeconds)
{
    if (showSeconds_ == showSeconds)
        return;

    showSeconds_ = showSeconds;
    updateAccessibleDescription();
    requestUpdate();
}

bool Clock::showsSeconds() const noexcept
{
    return showSeconds_;
}

void Clock::setShowDate(bool showDate)
{
    if (showDate_ == showDate)
        return;

    showDate_ = showDate;
    requestUpdate();
}

bool Clock::showsDate() const noexcept
{
    return showDate_;
}

void Clock::setRunning(bool running)
{
    if (running_ != running) {
        running_ = running;
        updateAccessibleDescription();
        requestUpdate();
    }

    syncTimer();
}

bool Clock::isRunning() const noexcept
{
    return running_;
}

QTime Clock::time() const
{
    return instant().time();
}

QDate Clock::date() const
{
    return instant().date();
}

void Clock::setFrozenTime(const QDateTime& instant)
{
    frozen_ = instant.isValid() ? instant : QDateTime::currentDateTime();
    updateAccessibleDescription();
    requestUpdate();
}

QDateTime Clock::instant() const
{
    QDateTime value = running_ ? QDateTime::currentDateTime() : frozen_;
    if (!value.isValid())
        value = QDateTime::currentDateTime();

    if (zone_.isValid())
        value = value.toTimeZone(zone_);

    return value;
}

QString Clock::timeText() const
{
    const QDateTime shown = instant();
    const QString time = formatTime(shown.time(), use24Hour_, showSeconds_);
    if (time.isEmpty())
        return QString();

    if (use24Hour_)
        return time;

    return time + QLatin1Char(' ') + meridiemText(shown.time());
}

void Clock::updateAccessibleDescription()
{
    setAccessibleDescription(timeText());
}

void Clock::syncTimer()
{
    // setRunning() is authoritative; showEvent()/hideEvent() provide the
    // lifecycle half of the same contract (a clock hidden by its owner is idle,
    // and showing it again resumes the tick). Starting an already-active timer
    // would reset its phase, so the active check keeps show() idempotent.
    if (running_) {
        if (!timer_.isActive())
            timer_.start();
    } else {
        timer_.stop();
    }
}

void Clock::onTick()
{
    try {
        requestUpdate();
        updateAccessibleDescription();
        emit tick();
    } catch (const std::exception& error) {
        qWarning("md::Clock::onTick failed: %s", error.what());
    } catch (...) {
        qWarning("md::Clock::onTick failed with an unknown exception");
    }
}

void Clock::measureOverride(MeasureSpec width, MeasureSpec height)
{
    try {
        const qreal preferred = Theme::forWidget(this).density().dp(kPreferredSizeDp);
        const qreal widthCandidate = width.mode() == MeasureSpec::Mode::Unspecified
            ? preferred
            : std::max(qreal(0.0), width.size());
        const qreal heightCandidate = height.mode() == MeasureSpec::Mode::Unspecified
            ? preferred
            : std::max(qreal(0.0), height.size());

        // The component is square: the shorter available side wins, and the
        // specs are then applied to that side so an Exactly axis stays
        // authoritative (which can leave a rectangular box only when the two
        // Exactly axes disagree).
        const qreal side = std::min(widthCandidate, heightCandidate);
        setMeasuredSize(QSizeF(width.resolve(side), height.resolve(side)));
    } catch (const std::exception& error) {
        qWarning("md::Clock::measureOverride failed: %s", error.what());
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    } catch (...) {
        qWarning("md::Clock::measureOverride failed with an unknown exception");
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    }
}

void Clock::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    painter.save();
    try {
        const Theme& theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const QDateTime shown = instant();

        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);

        const qreal inset = density.dp(kDialInsetDp);
        const QRectF inner = content.adjusted(inset, inset, -inset, -inset);
        const qreal dialSide = std::min(inner.width(), inner.height());
        const bool dialFits = dialSide >= density.dp(kMinimumDialDp);
        const Display effective = (display_ == Display::Digital || dialFits)
            ? display_
            : Display::Digital;

        if (effective == Display::Digital) {
            const Readout readout(theme, theme.type().displaySmall, shown, use24Hour_,
                                  showSeconds_, showDate_);
            paintReadout(painter, content, theme, readout, false);
        } else if (effective == Display::Analog) {
            paintDial(painter, squareIn(inner), shown.time(), theme);
        } else {
            const Readout compact(theme, theme.type().titleLarge, shown, use24Hour_,
                                  showSeconds_, showDate_);
            const qreal gap = density.dp(4.0);
            const qreal available = std::min(inner.width(), inner.height());
            const bool canStack = available
                >= density.dp(kMinimumDialDp) + compact.height + gap;
            const bool stack = available < density.dp(kCompactReadoutDp) && canStack;

            if (stack) {
                const qreal dialLength = std::min(inner.width(),
                                                  available - compact.height - gap);
                const QRectF dial(inner.center().x() - dialLength / 2.0, inner.top(),
                                  dialLength, dialLength);
                paintDial(painter, dial, shown.time(), theme);

                const QRectF readoutArea(inner.left(), dial.bottom() + gap, inner.width(),
                                         compact.height);
                paintReadout(painter, readoutArea, theme, compact, false);
            } else {
                const QRectF dial = squareIn(inner);
                paintDial(painter, dial, shown.time(), theme);

                const Readout overlay(theme, theme.type().displaySmall, shown, use24Hour_,
                                      showSeconds_, showDate_);
                paintReadout(painter, dial, theme, overlay, true);
            }
        }
    } catch (const std::exception& error) {
        qWarning("md::Clock::paintContent failed: %s", error.what());
    } catch (...) {
        qWarning("md::Clock::paintContent failed with an unknown exception");
    }
    painter.restore();
}

void Clock::showEvent(QShowEvent* event)
{
    try {
        View::showEvent(event);
        syncTimer();
    } catch (const std::exception& error) {
        qWarning("md::Clock::showEvent failed: %s", error.what());
    } catch (...) {
        qWarning("md::Clock::showEvent failed with an unknown exception");
    }
}

void Clock::hideEvent(QHideEvent* event)
{
    try {
        timer_.stop();
        View::hideEvent(event);
    } catch (const std::exception& error) {
        qWarning("md::Clock::hideEvent failed: %s", error.what());
    } catch (...) {
        qWarning("md::Clock::hideEvent failed with an unknown exception");
    }
}

} // namespace md
