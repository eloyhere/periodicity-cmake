// Periodicity — Link, a hyperlink as text.
//
// A Link is a single line of text that reports one action: activation emits
// clicked(href). It paints itself, so it participates in a layout like any
// other view, and it uses the type scale rather than an ad-hoc font: setRole()
// picks one of Text's four named roles, setTextStyle() overrides the style
// outright.
//
// Colour is the link's state: an unvisited link is primary, a visited one is
// tertiary, and hovering or pressing deepens the resolved colour with the
// theme's interaction overlay (a state layer of onSurface, which darkens in a
// light scheme and lightens in a dark one — the same rule every other Material
// control follows). The colour is resolved per paint, so a theme change needs
// no cached state.
//
// Long text never widens the widget past its allocated rectangle: the paint
// pass and elidedText() use the same QFontMetricsF elision, with the elide
// edge mirrored for a right-to-left layout. The text is focusable, so Enter
// and Space activate it and the kernel's focus ring marks it.

#pragma once

#include <md/components/text.h>
#include <md/tokens/type_scale.h>
#include <md/view/view.h>

#include <QColor>
#include <QFont>
#include <QString>
#include <Qt>

class QEvent;
class QFontMetricsF;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;

namespace md {

class Link final : public View
{
    Q_OBJECT

public:
    explicit Link(QWidget* parent = nullptr);
    Link(const QString& text, const QString& href, QWidget* parent = nullptr);

    // The visible label and the target reported on activation. The href is
    // opaque to the component; the host decides what to do with it.
    void setText(const QString& text);
    [[nodiscard]] QString text() const { return text_; }
    void setHref(const QString& href);
    [[nodiscard]] QString href() const { return href_; }

    // Visited switches the role colour from primary to tertiary.
    void setVisited(bool visited);
    [[nodiscard]] bool isVisited() const noexcept { return visited_; }

    // An underline under the painted text. Independent of visited.
    void setUnderline(bool underline);
    [[nodiscard]] bool isUnderline() const noexcept { return underline_; }

    // The type role. setTextStyle() overrides it until setRole() is called
    // again, exactly like Text.
    void setRole(Text::TextRole role);
    [[nodiscard]] Text::TextRole role() const noexcept { return role_; }
    void setTextStyle(const TextStyle& style);
    [[nodiscard]] TextStyle textStyle() const;

    // An explicit colour overrides the role colour (primary/tertiary) but the
    // hover and pressed states still apply. An invalid colour restores the
    // role colour.
    void setColour(const QColor& colour);
    [[nodiscard]] QColor colour() const;

    // The rectangle the label occupies, in widget coordinates. Empty until the
    // first arrangement.
    [[nodiscard]] QRectF linkRect() const;

    // The string as the paint pass draws it at the current width: elided when
    // it does not fit. Useful to a test and to a host that measures the link.
    [[nodiscard]] QString elidedText() const;

signals:
    // A press released inside the text, or Enter/Space while focused.
    void clicked(QString href);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;
    void onThemeChanged() override;
    [[nodiscard]] bool focusable() const override;

private:
    [[nodiscard]] TextStyle resolvedStyle() const;
    [[nodiscard]] QFont resolvedFont() const;
    [[nodiscard]] QRectF resolvedLinkRect() const;
    [[nodiscard]] QString resolveElided(const QFontMetricsF& metrics) const;
    [[nodiscard]] QColor roleColour() const;
    void syncCursor();

    QString text_;
    QString href_;
    Text::TextRole role_ = Text::TextRole::Body;
    TextStyle explicitStyle_;
    bool customStyle_ = false;
    QColor explicitColour_;
    bool visited_ = false;
    bool underline_ = false;
    bool pressedInside_ = false;
};

} // namespace md
