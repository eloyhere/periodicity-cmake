// Periodicity — DropdownTrigger, the visible row of a dropdown-style switch.
//
// A configuration switch needs a control that shows the committed value and
// opens a modal sheet, not a bare button. This View is that control: the current
// value as text, a trailing chevron that rotates while the sheet is open, a
// hover/pressed state layer and a click ripple across the whole row, and the
// keyboard opens it with Enter, Space or ArrowDown. It is deliberately additive
// to Dropdown: Dropdown remains the popup surface and this is the trigger that
// hosts place where the value should be shown.
//
// The chevron uses the same motion ladder as the sheet (Level2 in, Level1 out)
// and the same Animator, so the row and the sheet read as one gesture. A host
// that drives a deterministic clock can share it through
// setManualAnimationClock()/advanceAnimation().

#pragma once

#include <md/components/row_feedback.h>
#include <md/view/view.h>

#include <QString>
#include <QtGlobal>

#include <chrono>

class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;

namespace md {

namespace animation {
class Animator;
}

class DropdownTrigger final : public View
{
    Q_OBJECT

public:
    explicit DropdownTrigger(QWidget* parent = nullptr);

    // The committed value. The owner updates this when the sheet applies a
    // value; a cancelled sheet never changes it.
    void setLabel(const QString& label);
    [[nodiscard]] QString label() const;
    void setPlaceholder(const QString& placeholder);
    [[nodiscard]] QString placeholder() const;

    // Drives the chevron. True while the sheet is open: the glyph rotates from
    // down to up over Level2; false rotates it back over Level1.
    void setOpen(bool open);
    [[nodiscard]] bool isOpen() const noexcept;

    // 0 — pointing down (closed), 1 — pointing up (open).
    [[nodiscard]] qreal chevronProgress() const noexcept;

    // Deterministic clock for the chevron animation.
    void setManualAnimationClock(bool enabled);
    [[nodiscard]] bool isManualAnimationClock() const noexcept;
    void advanceAnimation(std::chrono::milliseconds delta);

    [[nodiscard]] RowFeedback* rowFeedback() noexcept;

signals:
    // The row was activated. `fromArrowDown` is true for ArrowDown, so the
    // sheet can move its highlight to the committed value.
    void activationRequested(bool fromArrowDown);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;
    void onThemeChanged() override;
    void onHidden() override;

private:
    void setChevronProgress(qreal progress);
    void syncAppearance();

    QString label_;
    QString placeholder_;
    bool open_ = false;
    qreal chevronProgress_ = 0.0;
    bool pressed_ = false;

    RowFeedback feedback_;
    animation::Animator* chevronAnimator_ = nullptr;
};

} // namespace md
