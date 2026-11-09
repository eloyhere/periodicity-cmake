// Periodicity — the Radio component (framework.md §5.9).
//
// A 20 dp circle with a 2 dp contour and a 10 dp inner dot inside a 48 dp
// touch target. Radio is exclusive by group rather than by Qt's
// QAbstractButton auto-exclusive machinery, because the framework has no
// QButtonGroup: setGroupId() names a group and checking one radio unchecks
// every sibling discovered through parentWidget()->children() that shares the
// group id. A radio is never unchecked by its own click, matching M3.

#pragma once

#include <md/view/view.h>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;

namespace md {

class Radio : public View
{
    Q_OBJECT

public:
    explicit Radio(QWidget* parent = nullptr);

    // Checking a radio unchecks its sibling group; unchecking is a no-op for a
    // radio that is already unchecked.
    void setChecked(bool checked);
    [[nodiscard]] bool isChecked() const noexcept;

    // Radios with the same group id, under the same parent widget, are
    // mutually exclusive. The default group id is 0.
    void setGroupId(int id);
    [[nodiscard]] int groupId() const noexcept;

signals:
    void toggled(bool checked);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void check();
    void uncheckSiblings();

    bool checked_ = false;
    int groupId_ = 0;
    bool pressedInside_ = false;
};

} // namespace md
