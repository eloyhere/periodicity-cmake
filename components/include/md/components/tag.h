// Periodicity — Tag (media/content batch).
//
// A design baseline chip in its three container treatments — Filled, Tonal and
// Outlined — with an optional leading colour dot, an optional tinted icon, an
// optional selected state and an optional close affordance. The component is
// the 32 dp, 8 dp-corner chip painted from the theme's small shape token; it
// owns no background layer and no child widgets, so measurement is one font
// pass and one set of dp constants.
//
// The trailing close region is a real hit target: a press and release inside it
// emits closeClicked() and never toggles the selection, while a body press
// emits clicked() and, when selectable, flips selected() and reports it through
// selectedChanged(). Space / Return activate the body and Delete closes it when
// the widget has keyboard focus.

#pragma once

#include <md/drawable/icon_drawable.h>
#include <md/view/view.h>

#include <QColor>
#include <QIcon>
#include <QString>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;

namespace md {

class Tag : public View
{
    Q_OBJECT

public:
    enum class TagVariant {
        Filled,
        Tonal,
        Outlined,
    };

    explicit Tag(QWidget* parent = nullptr);
    explicit Tag(const QString& text, QWidget* parent = nullptr);

    void setText(const QString& text);
    [[nodiscard]] QString text() const;

    void setIcon(const QIcon& icon);
    [[nodiscard]] QIcon icon() const;

    void setVariant(TagVariant variant);
    [[nodiscard]] TagVariant variant() const noexcept;

    // The close affordance and its closeClicked() signal.
    void setClosable(bool closable);
    [[nodiscard]] bool isClosable() const noexcept;

    // When false a body click still emits clicked() but never changes the
    // selected state. True is the default.
    void setSelectable(bool selectable);
    [[nodiscard]] bool isSelectable() const noexcept;

    void setSelected(bool selected);
    [[nodiscard]] bool isSelected() const noexcept;

    // A leading dot. Its colour defaults to the theme's primary role.
    void setColourDot(bool visible);
    [[nodiscard]] bool hasColourDot() const noexcept;
    void setDotColour(const QColor& colour);
    [[nodiscard]] QColor dotColour() const;

signals:
    void clicked();
    void closeClicked();
    void selectedChanged(bool selected);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void onThemeChanged() override;
    void onEnabledChanged(bool enabled) override;
    [[nodiscard]] bool focusable() const override;

private:
    struct TagLayout
    {
        QRectF dot;
        QRectF icon;
        QRectF text;
        QRectF close;
    };

    [[nodiscard]] TagLayout layoutFor(const QRectF& content) const;
    [[nodiscard]] QRectF closeRectFor(const QRectF& content) const;
    [[nodiscard]] bool hasIcon() const;
    [[nodiscard]] QColor containerColour() const;
    [[nodiscard]] QColor contentColour() const;
    void syncAppearance();
    void toggleSelected();

    QString text_;
    drawable::IconDrawable iconDrawable_;
    TagVariant variant_ = TagVariant::Filled;

    bool closable_ = false;
    bool selectable_ = true;
    bool selected_ = false;
    bool colourDot_ = false;
    QColor dotColour_;

    bool pressedInside_ = false;
    bool closePressed_ = false;
    bool closeHovered_ = false;
};

} // namespace md
