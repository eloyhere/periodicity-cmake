// Periodicity — the Clock component.
//
// Clock paints an analog dial, a digital read-out, or both, and shows one
// instant in one explicit time zone. While running it samples
// QDateTime::currentDateTime() once per second; a frozen instant turns the
// widget into a deterministic snapshot for tests and documentation. The timer
// is owned by setRunning(), and showEvent()/hideEvent() resume and stop it, so
// a clock hidden by its owner costs nothing.
//
// The analog face is drawn from single-line elements: 24 radial ticks (the four
// cardinals are longer and heavier), three needle hands and, on a dial of at
// least 120 dp, the 12/3/6/9 labels. The hour hand advances continuously with
// the minute (and the minute with the second), while the second hand steps once
// per second. A dial smaller than 96 dp falls back to the digital read-out,
// which keeps the component legible at icon sizes.
//
// A QDateTime is an instant, not a wall clock. setFrozenTime() therefore fixes
// the instant, and setTimeZone() re-expresses that same instant in the new
// zone; time() and date() always report the configured zone.

#pragma once

#include <md/view/view.h>

#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QTimeZone>
#include <QTimer>

class QPainter;
class QRectF;

namespace md {

class Clock : public View
{
    Q_OBJECT

public:
    enum class Display {
        Analog,  // the dial only
        Digital, // the text read-out only
        Both,    // the dial plus a digital read-out
    };

    explicit Clock(QWidget* parent = nullptr);

    void setDisplay(Display display);
    [[nodiscard]] Display display() const noexcept;

    // The zone the wall clock is shown in. An invalid zone is replaced by the
    // system zone (or UTC when the system zone is unavailable), so time() and
    // date() always describe a real wall clock.
    void setTimeZone(const QTimeZone& zone);
    [[nodiscard]] QTimeZone timeZone() const;

    void setUse24Hour(bool use24Hour);
    [[nodiscard]] bool uses24Hour() const noexcept;

    void setShowSeconds(bool showSeconds);
    [[nodiscard]] bool showsSeconds() const noexcept;

    void setShowDate(bool showDate);
    [[nodiscard]] bool showsDate() const noexcept;

    // While running the clock follows the current instant; while stopped it
    // shows the frozen instant. Changing the flag starts or stops the one-
    // second timer; showEvent()/hideEvent() provide the lifecycle half of the
    // same contract, so a clock hidden by its owner is idle.
    void setRunning(bool running);
    [[nodiscard]] bool isRunning() const noexcept;

    // The wall-clock time and date at the shown instant, in timeZone().
    [[nodiscard]] QTime time() const;
    [[nodiscard]] QDate date() const;

    // Fixes the shown instant (tests and snapshots). Because the value is an
    // instant, a later setTimeZone() re-expresses it in the new zone. An
    // invalid QDateTime restores the current instant.
    void setFrozenTime(const QDateTime& instant);

signals:
    void tick();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onTick();

private:
    [[nodiscard]] QDateTime instant() const;
    [[nodiscard]] QString timeText() const;
    void updateAccessibleDescription();
    void syncTimer();

    Display display_ = Display::Analog;
    QTimeZone zone_;
    bool use24Hour_ = true;
    bool showSeconds_ = false;
    bool showDate_ = false;
    bool running_ = true;

    // The instant shown while stopped. It is captured in the constructor and
    // never advances while the clock is stopped.
    QDateTime frozen_;

    // One timer for the whole component; it is started only while running and
    // visible.
    QTimer timer_;
};

} // namespace md
