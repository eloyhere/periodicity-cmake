// Periodicity — Typography, the full design baseline type-scale display component.
//
// Text exposes four named roles (Caption, Label, Body, Title) because that is
// what a form or a button needs. Typography is the presentation counterpart: it
// exposes all fifteen styles of the design baseline type scale, so a gallery or a
// documentation page can show the scale itself rather than a re-labelled
// subset.
//
// The two components share the same measurement and painting rules: the string
// is laid out with QTextLayout at the token line height (never at
// QFontMetrics::height()), tracking is applied through
// QFont::setLetterSpacing() in sp, and wrapping is a measure-time concern, so
// heightForWidth() reports the real wrapped height.
//
// The colour defaults to onSurface for display, headline, title and body roles
// and to onSurfaceVariant for the label roles, matching Text. Typography owns
// no interaction and is never focusable.

#pragma once

#include <md/components/text.h>
#include <md/tokens/type_scale.h>
#include <md/view/view.h>

#include <QColor>
#include <QString>
#include <Qt>

class QPainter;
class QRectF;

namespace md {

class Typography : public View
{
    Q_OBJECT

public:
    // The fifteen design baseline type styles, in the order the specification lists them:
    // display, headline, title, body and label, each large / medium / small.
    enum class TypeRole {
        DisplayLarge,
        DisplayMedium,
        DisplaySmall,
        HeadlineLarge,
        HeadlineMedium,
        HeadlineSmall,
        TitleLarge,
        TitleMedium,
        TitleSmall,
        BodyLarge,
        BodyMedium,
        BodySmall,
        LabelLarge,
        LabelMedium,
        LabelSmall,
    };
    Q_ENUM(TypeRole)

    explicit Typography(QWidget* parent = nullptr);
    explicit Typography(const QString& text, QWidget* parent = nullptr);
    Typography(const QString& text, TypeRole role, QWidget* parent = nullptr);

    void setText(const QString& text);
    [[nodiscard]] QString text() const;

    void setRole(TypeRole role);
    [[nodiscard]] TypeRole role() const noexcept;

    // Compatibility with Text's four named roles. The mapped role is the
    // closest style of the same family, so a caller can hand a Text role to
    // Typography without translating it by hand:
    //   Caption -> BodySmall, Label -> LabelLarge, Body -> BodyMedium,
    //   Title -> TitleMedium.
    void setRole(Text::TextRole role);
    [[nodiscard]] Text::TextRole textRole() const;

    // The style `role` resolves to in `scale`. This is a pure lookup, so a
    // caller can compare two roles without a widget or a theme.
    [[nodiscard]] static TextStyle styleFor(TypeRole role, const TypeScale& scale);
    [[nodiscard]] static TypeRole typeRoleFor(Text::TextRole role);

    // An explicit style. It overrides the role until setRole() is called again,
    // which restores the theme's style for that role.
    void setTextStyle(const TextStyle& style);
    [[nodiscard]] TextStyle textStyle() const;

    // Horizontal alignment applies to every line; the vertical component
    // (AlignTop / AlignVCenter / AlignBottom) positions the block inside the
    // content rectangle.
    void setAlignment(Qt::Alignment alignment);
    [[nodiscard]] Qt::Alignment alignment() const noexcept;

    void setWordWrap(bool wrap);
    [[nodiscard]] bool wordWrap() const noexcept;

    // The colour. An invalid colour (the default) resolves from the role.
    void setColour(const QColor& colour);
    [[nodiscard]] QColor colour() const;

protected:
    void onStoreChanged(Store* store, const QString& key, const QVariant& value) override;
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    [[nodiscard]] bool focusable() const override;

private:
    [[nodiscard]] TextStyle resolvedStyle() const;
    [[nodiscard]] QColor resolvedColour() const;

    QString text_;
    TypeRole role_ = TypeRole::BodyMedium;
    TextStyle explicitStyle_;
    bool customStyle_ = false;
    Qt::Alignment alignment_ = Qt::AlignLeft | Qt::AlignTop;
    bool wordWrap_ = false;
    QColor colour_;
};

} // namespace md
