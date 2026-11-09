// Periodicity — the Progress component (framework.md §5.9).
//
// A linear or circular progress indicator. The linear form is a 4 dp rounded
// track in surfaceContainerHighest with a primary indicator whose width is the
// value (plus an optional buffer band); the circular form is a stroked ring
// around the same two roles.
//
// Indeterminate mode animates through md::animation::Animator with the
// Level3 motion token. The animation is restarted from its finished() signal,
// and only while the widget is visible: showEvent starts it, hideEvent stops
// it, so a hidden indicator never burns a timer. Reduced-motion themes snap to
// a static phase instead of looping.

#pragma once

#include <md/animation/animator.h>
#include <md/view/view.h>

class QHideEvent;
class QPainter;
class QRectF;
class QShowEvent;

namespace md {

class Progress : public View
{
    Q_OBJECT

public:
    enum class ProgressType {
        Linear,
        Circular,
    };

    explicit Progress(QWidget* parent = nullptr);
    explicit Progress(ProgressType type, QWidget* parent = nullptr);

    void setType(ProgressType type);
    [[nodiscard]] ProgressType type() const noexcept;

    // The determinate fraction, clamped to 0..1.
    void setValue(qreal value);
    [[nodiscard]] qreal value() const noexcept;

    void setIndeterminate(bool indeterminate);
    [[nodiscard]] bool isIndeterminate() const noexcept;

    // The buffered fraction of the linear form, clamped to 0..1.
    void setBuffer(qreal buffer);
    [[nodiscard]] qreal buffer() const noexcept;

    // True while the indeterminate animation is running. A determinate
    // indicator, a hidden one and a reduced-motion theme all report false.
    [[nodiscard]] bool isAnimating() const noexcept;

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    [[nodiscard]] bool focusable() const override;

private:
    void startAnimation();
    void stopAnimation();
    void paintLinear(QPainter& painter, const QRectF& content);
    void paintCircular(QPainter& painter, const QRectF& content);

    ProgressType type_ = ProgressType::Linear;
    qreal value_ = 0.0;
    qreal buffer_ = 0.0;
    bool indeterminate_ = false;
    qreal phase_ = 0.0;
    animation::Animator animator_;
};

} // namespace md
