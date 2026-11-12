// Periodicity — the Button component (framework.md §5.9).
//
// One class, five variants (framework.md §5.9): Filled, FilledTonal, Elevated,
// Outlined and Text. The widget occupies a 48 dp touch target while the
// painted container is the 40 dp visual height M3 specifies, centred inside
// it, so the extra area is transparent padding rather than layout size
// (framework.md §8).
//
// The container is drawn from the theme's full (pill) shape token, the label
// from the labelLarge type token and every colour from the colour scheme. The
// interaction overlay, ripple and focus ring come from View's pipeline; the
// component only reacts to state() by choosing the container and content
// colours, and emits clicked() when a press started and ended inside the
// widget (or on Space / Return / Enter).

#pragma once

#include <md/drawable/icon_drawable.h>
#include <md/view/view.h>

#include <QColor>
#include <QIcon>
#include <QString>

class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;

namespace md {

class Button : public View
{
    Q_OBJECT

public:
    enum class ButtonVariant {
        Filled,
        FilledTonal,
        Elevated,
        Outlined,
        Text,
    };

    explicit Button(QWidget* parent = nullptr);
    explicit Button(const QString& text, QWidget* parent = nullptr);

    void setText(const QString& text);
    [[nodiscard]] QString text() const;

    void setVariant(ButtonVariant variant);
    [[nodiscard]] ButtonVariant variant() const noexcept;

    void setIcon(const QIcon& icon);
    [[nodiscard]] QIcon icon() const;

    // The icon box, in dp. M3's standard button icon is 20 dp.
    void setIconSize(qreal dp);
    [[nodiscard]] qreal iconSize() const noexcept;

signals:
    void clicked();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    // Re-resolves the theme-dependent shape, elevation and content colour.
    // Called from the constructor and whenever the theme or the enabled state
    // changes.
    void syncAppearance();
    [[nodiscard]] bool hasIcon() const;

    QString text_;
    ButtonVariant variant_ = ButtonVariant::Filled;
    drawable::IconDrawable iconDrawable_;
    qreal iconSizeDp_ = 20.0;
    bool pressedInside_ = false;
};

} // namespace md
