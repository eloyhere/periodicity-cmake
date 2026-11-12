// Periodicity — the Fab component (design baseline floating action button).
//
// Three container sizes — small (40 dp), regular (56 dp) and large (96 dp) —
// plus an extended form: a 56 dp tall pill carrying the icon and a label. The
// container is primaryContainer with onPrimaryContainer content, the corner
// radius is 16 dp for the small and regular forms, 28 dp for the large one and
// the full (pill) shape when extended, and the elevation is level 3.
//
// The surface is painted by View's background and elevation layers, exactly as
// Card paints its container; paintContent() only adds the icon and the optional
// label. The interaction overlay, the ripple, the focus ring and the keyboard
// activation contract come from View, so a Fab behaves like a Button from the
// user's point of view: Space / Return / Enter activate, a press that starts and
// ends inside the container clicks, and a disabled Fab emits nothing.

#pragma once

#include <md/drawable/icon_drawable.h>
#include <md/view/view.h>

#include <QIcon>
#include <QString>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;

namespace md {

class Fab : public View
{
    Q_OBJECT

public:
    enum class Size {
        Small,
        Regular,
        Large,
    };

    explicit Fab(QWidget* parent = nullptr);
    explicit Fab(const QIcon& value, QWidget* parent = nullptr);

    void setIcon(const QIcon& value);
    [[nodiscard]] QIcon icon() const;

    // The icon box in dp. The default follows the size — 24 dp for the small
    // and regular forms, 36 dp for the large one — until an explicit value is
    // set, after which the explicit value survives a size change.
    void setIconSize(qreal dp);
    [[nodiscard]] qreal iconSize() const noexcept;

    // The container size. When extended, the height is fixed at 56 dp and this
    // value only drives the icon-size default; the width follows the label.
    // Note that this hides QWidget::size(); use width()/height() for geometry.
    void setSize(Size value);
    [[nodiscard]] Size size() const noexcept;

    void setExtended(bool value);
    [[nodiscard]] bool isExtended() const noexcept;

    // The label. It is measured and painted only when the FAB is extended.
    void setText(const QString& value);
    [[nodiscard]] QString text() const;

signals:
    void clicked();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

    void onThemeChanged() override;
    void onEnabledChanged(bool enabled) override;

private:
    // Re-resolves shape, container/content colours and elevation from the
    // current theme and enabled state.
    void syncAppearance();
    [[nodiscard]] bool hasIcon() const;

    drawable::IconDrawable iconDrawable_;
    QString text_;
    qreal iconSizeDp_ = 24.0;
    bool iconSizeExplicit_ = false;
    Size size_ = Size::Regular;
    bool extended_ = false;
    bool pressedInside_ = false;
};

} // namespace md
