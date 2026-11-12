// Periodicity — the Ring component (framework.md §5.9).
//
// A declarative-style ring progress: a stroked circle with an indicator arc in
// the primary role. In determinate mode the arc starts at twelve o'clock and
// sweeps value * 360 degrees. In indeterminate mode a trimmed arc rotates
// continuously, driven by md::animation::Animator with the Level3 motion
// token; the animator is restarted from finished() and runs only while the
// widget is visible.

#pragma once

#include <md/animation/animator.h>
#include <md/view/view.h>

class QHideEvent;
class QPainter;
class QRectF;
class QShowEvent;

namespace md {

class Ring : public View
{
    Q_OBJECT

public:
    explicit Ring(QWidget* parent = nullptr);

    // The determinate fraction, clamped to 0..1.
    void setValue(qreal value);
    [[nodiscard]] qreal value() const noexcept;

    void setIndeterminate(bool indeterminate);
    [[nodiscard]] bool isIndeterminate() const noexcept;

    // The indicator width, in dp. The default is 4 dp.
    void setStrokeWidthDp(qreal dp);
    [[nodiscard]] qreal strokeWidthDp() const noexcept;

    // True while the indeterminate rotation is running.
    [[nodiscard]] bool isAnimating() const noexcept;

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void onThemeChanged() override;
    [[nodiscard]] bool focusable() const override;

private:
    void startAnimation();
    void stopAnimation();

    qreal value_ = 0.0;
    bool indeterminate_ = false;
    qreal strokeWidthDp_ = 4.0;
    qreal phase_ = 0.0;
    animation::Animator animator_;
};

} // namespace md
