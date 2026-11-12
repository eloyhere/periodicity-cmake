// Periodicity — Skeleton (media/content batch).
//
// A placeholder silhouette for content that has not loaded yet: a rectangle, a
// circle or a text line, with a dp width, height and corner radius. Painting is
// a single path fill in the theme's surfaceContainerHighest, plus — while the
// shimmer runs — a horizontal highlighted band swept across the same path.
//
// The shimmer is driven by one QTimer inside the component (16 ms, roughly one
// frame) and only while the widget is visible and setAnimating(true); the
// timeout advances a phase and calls requestUpdate(), so a burst of frames
// still collapses into the shared task queue. A hidden, disabled or
// non-animating skeleton paints a flat fill and burns no timer.

#pragma once

#include <md/view/view.h>

#include <QColor>
#include <QTimer>

class QPainter;
class QRectF;

namespace md {

class Skeleton : public View
{
    Q_OBJECT

public:
    enum class SkeletonShape {
        Rect,
        Circle,
        Text, // a single text line: a rounded rectangle with a capped radius
    };

    explicit Skeleton(QWidget* parent = nullptr);
    explicit Skeleton(SkeletonShape shape, QWidget* parent = nullptr);

    void setSkeletonShape(SkeletonShape shape);
    [[nodiscard]] SkeletonShape skeletonShape() const noexcept;

    // The requested size in dp. A circle resolves both axes to the smaller of
    // the two, so it stays round.
    void setSkeletonWidth(qreal dp);
    [[nodiscard]] qreal skeletonWidth() const noexcept;

    void setSkeletonHeight(qreal dp);
    [[nodiscard]] qreal skeletonHeight() const noexcept;

    // The corner radius of the Rect and Text forms, in dp. A circle ignores it.
    void setCornerRadius(qreal dp);
    [[nodiscard]] qreal cornerRadius() const noexcept;

    // Requests the shimmer. It actually runs only while the widget is visible;
    // isAnimating() reports the running timer, not the request.
    void setAnimating(bool animating);
    [[nodiscard]] bool isAnimating() const noexcept;

    // The flat fill and the highlight band. Invalid colours (the default)
    // resolve to surfaceContainerHighest and surfaceContainerLow.
    void setBaseColour(const QColor& colour);
    [[nodiscard]] QColor baseColour() const;

    void setHighlightColour(const QColor& colour);
    [[nodiscard]] QColor highlightColour() const;

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void onShown() override;
    void onHidden() override;
    void onThemeChanged() override;
    void onEnabledChanged(bool enabled) override;
    [[nodiscard]] bool focusable() const override;

private:
    [[nodiscard]] bool shimmerAllowed() const;
    void startTimer();
    void stopTimer();
    void advanceFrame();

    SkeletonShape shape_ = SkeletonShape::Rect;
    qreal widthDp_ = 160.0;
    qreal heightDp_ = 16.0;
    qreal radiusDp_ = 8.0;
    // The defaults follow the shape, but an explicit setter always wins.
    bool widthSet_ = false;
    bool heightSet_ = false;
    bool radiusSet_ = false;

    bool animating_ = false;
    qreal phase_ = 0.0;
    QColor baseColour_;
    QColor highlightColour_;
    QTimer timer_;
};

} // namespace md
