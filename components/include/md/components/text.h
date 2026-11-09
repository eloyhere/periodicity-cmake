// Periodicity — the Text component (framework.md §5.9, §7.5).
//
// Text is the type-scale primitive: it resolves one TextStyle from the theme
// (either a named role or an explicit style), lays the string out with
// QTextLayout and paints it. Wrapping is a measure-time concern, so
// heightForWidth() — which View implements by measuring with an exactly(width)
// spec — returns the real wrapped height rather than a single-line estimate.
//
// The colour defaults to onSurface for body and title roles and to
// onSurfaceVariant for caption and label roles; a caller may override it.
// Text owns no interaction and is never focusable.

#pragma once

#include <md/tokens/type_scale.h>
#include <md/view/view.h>

#include <QColor>
#include <QString>
#include <Qt>

class QPainter;
class QRectF;

namespace md {

class Text : public View
{
    Q_OBJECT

public:
    // The named type-scale roles this component exposes.
    enum class TextRole {
        Caption, // bodySmall
        Label,   // labelLarge
        Body,    // bodyMedium
        Title,   // titleMedium
    };

    explicit Text(QWidget* parent = nullptr);
    explicit Text(const QString& text, QWidget* parent = nullptr);

    void setText(const QString& text);
    [[nodiscard]] QString text() const;

    void setRole(TextRole role);
    [[nodiscard]] TextRole role() const noexcept;

    // An explicit style. It overrides the role until setRole() is called
    // again, which restores the theme's style for that role.
    void setTextStyle(const TextStyle& style);
    [[nodiscard]] TextStyle textStyle() const;

    // Horizontal alignment is applied to every line; the vertical component
    // (AlignTop / AlignVCenter / AlignBottom) positions the block inside the
    // content rectangle.
    void setAlignment(Qt::Alignment alignment);
    [[nodiscard]] Qt::Alignment alignment() const noexcept;

    void setWordWrap(bool wrap);
    [[nodiscard]] bool wordWrap() const noexcept;

    // The colour. The default (an invalid colour) resolves from the role.
    void setColour(const QColor& colour);
    [[nodiscard]] QColor colour() const;

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    [[nodiscard]] bool focusable() const override;

private:
    [[nodiscard]] TextStyle resolvedStyle() const;
    [[nodiscard]] QColor resolvedColour(const TextStyle& style) const;

    QString text_;
    TextRole role_ = TextRole::Body;
    TextStyle explicitStyle_;
    bool customStyle_ = false;
    Qt::Alignment alignment_ = Qt::AlignLeft | Qt::AlignVCenter;
    bool wordWrap_ = false;
    QColor colour_;
};

} // namespace md
