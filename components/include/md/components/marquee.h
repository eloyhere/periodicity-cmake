// Periodicity — Marquee, a text ticker.
//
// The label scrolls only when it actually overflows the viewport: a short text
// is painted once and the timer never starts, which is what makes a marquee
// usable in a table cell without a "should it move?" branch at every call site.
//
// Scrolling is driven by a QTimer and QElapsedTimer: the offset advances by
// speed × elapsed seconds, so the perceived speed does not depend on the timer
// interval, and a paused frame does not jump. The paint pass draws two copies
// of the text separated by the gap, which is what makes the loop seamless.

#pragma once

#include <md/tokens/type_scale.h>
#include <md/view/view.h>

#include <QColor>
#include <QElapsedTimer>
#include <QString>
#include <QTimer>
#include <Qt>

class QEnterEvent;
class QEvent;
class QFont;
class QPainter;
class QRectF;
class QResizeEvent;

namespace md {

class Marquee : public View
{
    Q_OBJECT

public:
    enum class Direction {
        Left,  // the text moves towards the leading edge
        Right, // the text moves towards the trailing edge
    };
    Q_ENUM(Direction)

    explicit Marquee(QWidget* parent = nullptr);
    explicit Marquee(const QString& text, QWidget* parent = nullptr);

    void setText(const QString& text);
    [[nodiscard]] QString text() const;

    // The default style is the theme's labelLarge.
    void setTextStyle(const TextStyle& style);
    [[nodiscard]] TextStyle textStyle() const;

    void setColour(const QColor& colour);
    [[nodiscard]] QColor colour() const;

    // Used only while the text fits; an overflowing text always starts at the
    // leading edge.
    void setAlignment(Qt::Alignment alignment);
    [[nodiscard]] Qt::Alignment alignment() const noexcept;

    // Scrolling speed in dp per second.
    void setSpeed(qreal dpPerSecond);
    [[nodiscard]] qreal speed() const noexcept;

    void setDirection(Direction direction);
    [[nodiscard]] Direction direction() const noexcept;

    // Distance between the trailing and the leading copy, in dp.
    void setGap(qreal dp);
    [[nodiscard]] qreal gap() const noexcept;

    void setPauseOnHover(bool pause);
    [[nodiscard]] bool pauseOnHover() const noexcept;

    // An explicit pause that keeps the started/stopped state.
    void setPaused(bool paused);
    [[nodiscard]] bool isPaused() const noexcept;

    void start();
    void stop();

    // True only while the text actually moves: started, not paused, not
    // hover-paused and overflowing. A short label reports false even after
    // start().
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] bool isOverflowing() const;

    [[nodiscard]] qreal scrollOffset() const noexcept;

    // The width of one copy of the text, in device-independent pixels.
    [[nodiscard]] qreal contentWidth() const;

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

    void onShown() override;
    void onHidden() override;
    void onThemeChanged() override;

    [[nodiscard]] bool focusable() const override;

private:
    void updateTimer();
    void tick();
    [[nodiscard]] TextStyle resolvedStyle() const;
    [[nodiscard]] QFont textFont() const;

    QString text_;
    TextStyle explicitStyle_;
    bool customStyle_ = false;
    QColor colour_;
    Qt::Alignment alignment_ = Qt::AlignLeft | Qt::AlignVCenter;

    qreal speedDp_ = 40.0;
    qreal gapDp_ = 32.0;
    Direction direction_ = Direction::Left;
    bool pauseOnHover_ = true;
    bool hovering_ = false;
    bool running_ = false;
    bool paused_ = false;
    qreal offset_ = 0.0;

    QTimer timer_;
    QElapsedTimer clock_;
};

} // namespace md
