// Periodicity — the IconButton component (framework.md §5.9).
//
// A single-icon action: a 24 dp icon centred in the 40 dp circular container
// M3 specifies, itself centred in a 48 dp touch target so the extra area is
// transparent padding rather than layout size (framework.md §8), exactly as
// Button does.
//
// Four variants: Standard (transparent container, onSurfaceVariant icon),
// Filled (primary container, onPrimary icon), Tonal (secondaryContainer,
// onSecondaryContainer icon) and Outlined (transparent container with a 1 dp
// outline, onSurfaceVariant icon). A checkable button paints the selected
// colours once checked: Standard becomes primaryContainer with an
// onPrimaryContainer icon, and Filled / Tonal / Outlined become primary with
// an onPrimary icon. A disabled button takes the theme's disabled opacities;
// the selected colours do not apply, because a disabled button cannot be
// interacted with.
//
// Interaction feedback — the state layer, the ripple and the keyboard focus
// ring — comes from View's paint pipeline; the component only chooses the
// container, outline and icon colours from the theme. A left-button press and
// release inside the widget activates it, and Space / Return / Enter do the
// same when the widget has focus. A checkable button toggles on activation and
// reports the new state through toggled(bool); clicked() is emitted after the
// checked state has been updated, so a handler observes the new state.
//
// The class has no text: an icon-only control has no visible label, so a host
// should give it an accessible name (setAccessibleName()) for screen readers.

#pragma once

#include <md/drawable/icon_drawable.h>
#include <md/view/view.h>

#include <QIcon>

class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;

namespace md {

class IconButton : public View
{
    Q_OBJECT

public:
    enum class Variant {
        Standard,
        Filled,
        Tonal,
        Outlined,
    };

    explicit IconButton(QWidget* parent = nullptr);
    explicit IconButton(const QIcon& icon, QWidget* parent = nullptr);

    void setIcon(const QIcon& icon);
    [[nodiscard]] QIcon icon() const;

    // The icon box, in dp. M3's icon button uses a 24 dp icon.
    void setIconSize(qreal dp);
    [[nodiscard]] qreal iconSize() const noexcept;

    void setVariant(Variant variant);
    [[nodiscard]] Variant variant() const noexcept;

    // Turns the button into a toggle. Clearing the flag also clears a checked
    // state, because a non-checkable button cannot stay checked.
    void setCheckable(bool checkable);
    [[nodiscard]] bool isCheckable() const noexcept;

    // Sets the checked state in code. It is ignored while the button is not
    // checkable, and it emits toggled() only when the state actually changes.
    void setChecked(bool checked);
    [[nodiscard]] bool isChecked() const noexcept;

signals:
    void clicked();
    void toggled(bool checked);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    // Re-resolves the theme-dependent shape, elevation and content colour.
    // Called from the constructor and whenever the theme, the enabled state,
    // the variant or the checked state changes.
    void syncAppearance();

    // Emits clicked() and, when checkable, flips the checked state first.
    void activate();
    void applyChecked(bool checked);

    // The checked state that currently has a visual effect.
    [[nodiscard]] bool isSelected() const noexcept;

    drawable::IconDrawable iconDrawable_;
    qreal iconSizeDp_ = 24.0;
    Variant variant_ = Variant::Standard;
    bool checkable_ = false;
    bool checked_ = false;
    bool pressedInside_ = false;
};

} // namespace md
