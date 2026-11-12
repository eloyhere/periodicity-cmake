// Periodicity — the Icon component (framework.md §5.9, §7.6).
//
// Icon is the tier-0 primitive every other component composes: a single tinted
// glyph in a requested dp box. The source may be a QIcon or a QPixmap, and the
// tint is applied as a SourceIn mask by drawable::IconDrawable, so the icon
// keeps the alpha channel of the source and takes exactly the colour the
// component asks for.
//
// The component itself owns no interaction: it is not focusable, paints no
// container and contributes only its requested size to measurement. RTL
// mirroring is opt-in per icon, because most icons in the icon set are
// direction-agnostic while arrows and chevrons are not (framework.md §7.6).

#pragma once

#include <md/drawable/icon_drawable.h>
#include <md/view/view.h>

#include <QColor>
#include <QIcon>
#include <QPixmap>

class QPainter;
class QRectF;

namespace md {

class Icon : public View
{
    Q_OBJECT

public:
    explicit Icon(QWidget* parent = nullptr);
    explicit Icon(const QIcon& icon, QWidget* parent = nullptr);

    // The source. Setting one clears the other, so the last call wins.
    void setIcon(const QIcon& icon);
    [[nodiscard]] QIcon icon() const;

    void setPixmap(const QPixmap& pixmap);
    [[nodiscard]] QPixmap pixmap() const;

    // The requested icon box, in dp. sizeHint() reports exactly this size.
    void setSize(qreal dp);
    [[nodiscard]] qreal size() const noexcept;

    // The tint. The default (an invalid colour) resolves to the theme's
    // onSurface role at paint time.
    void setColour(const QColor& colour);
    [[nodiscard]] QColor colour() const;

    // Mirror horizontally when the application layout direction is RTL.
    void setMirrored(bool mirrored);
    [[nodiscard]] bool isMirrored() const noexcept;

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    [[nodiscard]] bool focusable() const override;

private:
    drawable::IconDrawable drawable_;
    qreal sizeDp_ = 24.0;
    QColor colour_;
};

} // namespace md
