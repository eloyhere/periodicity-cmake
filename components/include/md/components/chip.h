// Periodicity — Chip (design baseline assist, filter, input and suggestion chips).
//
// One 32 dp, 8 dp-corner component with four semantic variants. An unselected
// chip is transparent with a 1 dp outline; a selected filter (or input) chip
// fills with the secondary container and draws its label in onSecondaryContainer.
// Assist and suggestion chips have no selected state at all: setSelected() on
// them is a no-op and no selectedChanged() is ever emitted.
//
// A filter chip shows a leading check mark while selected, in the same 18 dp
// slot as the optional leading icon, so switching the selection does not move
// the label. Input chips are the intended owners of the trailing close
// affordance, but setClosable() is honoured by every variant; the close region
// is a separate 24 dp hit target (WCAG 2.5.8) that emits closed() and never
// clicked(), while a body press emits clicked() and toggles the selection when
// the variant is selectable.
//
// Space / Return activate the body and Delete (or Backspace) closes a closable
// chip when it has keyboard focus. Every dimension is resolved through
// density().dp() and every colour through Theme::forWidget(this).

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

class Chip : public View
{
    Q_OBJECT

public:
    enum class Variant {
        Assist,
        Filter,
        Input,
        Suggestion,
    };

    explicit Chip(QWidget* parent = nullptr);
    explicit Chip(const QString& text, QWidget* parent = nullptr);

    void setText(const QString& text);
    [[nodiscard]] QString text() const;

    void setVariant(Variant variant);
    [[nodiscard]] Variant variant() const noexcept;

    // Assist and input chips use it as an action icon; a filter chip may carry
    // one too, but the check mark replaces it while the filter is selected.
    void setLeadingIcon(const QIcon& icon);
    [[nodiscard]] QIcon leadingIcon() const;

    // A no-op for Assist and Suggestion: the design baseline variants have no selected
    // state. Switching away from a selectable variant clears the flag and
    // reports selectedChanged(false).
    void setSelected(bool selected);
    [[nodiscard]] bool isSelected() const noexcept;

    void setClosable(bool closable);
    [[nodiscard]] bool isClosable() const noexcept;

signals:
    void clicked();
    void selectedChanged(bool selected);
    void closed();

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

private:
    struct ChipLayout
    {
        QRectF leading;
        QRectF text;
        QRectF close;
    };

    [[nodiscard]] ChipLayout layoutFor(const QRectF& content) const;

    // The 24 dp close hit target, which is larger than the 18 dp glyph it
    // surrounds.
    [[nodiscard]] QRectF closeRectFor(const QRectF& content) const;

    [[nodiscard]] bool hasLeadingIcon() const;
    [[nodiscard]] bool showsCheck() const noexcept;
    [[nodiscard]] bool showsClose() const noexcept;
    [[nodiscard]] bool isSelectable() const noexcept;

    [[nodiscard]] QColor containerColour() const;
    [[nodiscard]] QColor contentColour() const;
    [[nodiscard]] QColor outlineColour() const;

    void syncAppearance();
    void toggleSelected();
    void activate();

    QString text_;
    drawable::IconDrawable leadingDrawable_;
    Variant variant_ = Variant::Assist;

    bool closable_ = false;
    bool selected_ = false;

    bool pressedInside_ = false;
    bool closePressed_ = false;
    bool closeHovered_ = false;
};

} // namespace md
