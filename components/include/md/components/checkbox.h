// Periodicity — the CheckBox component (framework.md §5.9).
//
// An 18 dp box with a 2 dp contour inside a 48 dp touch target. The visual
// follows the design baseline three-state contract: unchecked (outlineVariant contour),
// checked (primary fill plus a stroked check mark) and indeterminate (primary
// fill plus a dash). The check mark is a QPainterPath stroked with round joins,
// not a font glyph, so it scales with density.
//
// Clicking the touch target toggles the state; Space, Return and Enter do the
// same when the widget has focus. toggled(bool) reports the new checked state.

#pragma once

#include <md/view/view.h>

#include <Qt>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;

namespace md {

class CheckBox : public View
{
    Q_OBJECT

public:
    explicit CheckBox(QWidget* parent = nullptr);

    void setCheckState(Qt::CheckState state);
    [[nodiscard]] Qt::CheckState checkState() const noexcept;

    void setChecked(bool checked);
    [[nodiscard]] bool isChecked() const noexcept;

    // When tristate is on, a click cycles Unchecked -> PartiallyChecked ->
    // Checked -> Unchecked. Off by default, which cycles Unchecked <-> Checked.
    void setTristate(bool tristate);
    [[nodiscard]] bool isTristate() const noexcept;

signals:
    void toggled(bool checked);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void cycle();
    void applyState(Qt::CheckState state);

    Qt::CheckState checkState_ = Qt::Unchecked;
    bool tristate_ = false;
    bool pressedInside_ = false;
};

} // namespace md
