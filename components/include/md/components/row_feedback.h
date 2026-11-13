// Periodicity — RowFeedback, the shared interaction feedback of a list row.
//
// A row of a menu, a picker or a select popup has the same three feedback
// states in every surface: a hover/pressed state layer composited from the
// content colour, a keyboard highlight drawn with the same state-layer value so
// mouse and keyboard look identical, and a click ripple that expands from the
// pointer (or the row centre for a keyboard activation) and is clipped to the
// row's rounded rectangle.
//
// The helper owns no geometry. The host already knows its rows, so it passes
// the row index, the row rectangle and the pointer position at the moment of
// the event. The ripple stores a copy of that rectangle, not a pointer to a row
// object: removing a row, resetting a model or closing the list can therefore
// drop the feedback without racing a freed row.
//
// The state-layer opacities come from the tokens (hover/pressed), never from a
// hand-picked alpha, and the ripple is driven by md::animation::Animator with a
// MotionLevel spec. setManualAnimationClock()/advanceAnimation() let a host
// that renders on its own cadence — and a test — step that animator without
// sleeping.

#pragma once

#include <QColor>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QPointer>
#include <QtGlobal>

#include <chrono>

class QPainter;
class QWidget;

namespace md {

namespace animation {
class Animator;
}

class RowFeedback final : public QObject
{
    Q_OBJECT

public:
    // `context` is the row surface; it supplies the theme (density, motion,
    // content colour) and is observed through a QPointer, so the helper can
    // outlive a destroyed host safely.
    explicit RowFeedback(QWidget* context, QObject* parent = nullptr);
    ~RowFeedback() override;

    RowFeedback(const RowFeedback&) = delete;
    RowFeedback& operator=(const RowFeedback&) = delete;

    // Pointer feedback. `row` is the host's current row index or -1 outside a
    // row. pointerPressed() starts a ripple at `origin` clipped to `rowRect`.
    void pointerMoved(int row);
    void pointerPressed(int row, const QPointF& origin, const QRectF& rowRect);
    void pointerReleased();
    void pointerLeft();

    // Starts a ripple for an activation that has no pointer position, such as
    // Enter on the keyboard, at `origin` (normally the row centre).
    void triggerRipple(int row, const QPointF& origin, const QRectF& rowRect);

    // Drops every hover/pressed state and every ripple. A host calls this when
    // the list hides, closes or resets, so no animator keeps ticking against a
    // surface that is no longer painted.
    void clear();

    [[nodiscard]] int hoveredRow() const noexcept;
    [[nodiscard]] int pressedRow() const noexcept;
    [[nodiscard]] int rippleCount() const noexcept;
    // The largest progress of the ripples in flight, in 0..1; 0 when none.
    [[nodiscard]] qreal rippleProgress() const noexcept;

    // Deterministic clock for the ripple animators.
    void setManualAnimationClock(bool enabled);
    [[nodiscard]] bool isManualAnimationClock() const noexcept;
    void advanceAnimation(std::chrono::milliseconds delta);

    // Paints the state layer and the ripples of one row, clipped to its rounded
    // rectangle. The host paints the row container, text and icons; call this
    // after the container fill and before the text. `highlighted` is the
    // keyboard highlight; it uses the hover token so both input paths match.
    void paintRow(QPainter& painter, int row, const QRectF& rowRect,
                  bool highlighted, qreal cornerRadius,
                  const QColor& contentColour) const;

signals:
    // The painted frame changed: schedule update(). Coalescing is the host's
    // business; the signal is cheap and each animation step emits it once.
    void repaintRequested();

private:
    struct Ripple;

    void startRipple(int row, const QPointF& origin, const QRectF& rowRect);
    void retireRipple(Ripple* ripple);
    void resetRipples();

    QPointer<QWidget> context_;
    int hovered_ = -1;
    int pressed_ = -1;
    QList<Ripple*> ripples_;
    bool manualClock_ = false;
};

} // namespace md
