// Periodicity — Banner, a top-of-surface message with actions.
//
// A banner carries an icon, a title, an optional description, a primary and a
// secondary action and a close affordance. The two actions are real Button
// children (text variants), so they inherit the framework's state layers,
// ripples and focus handling; the close affordance is painted by the banner
// itself, because a 40 dp "X" is a glyph rather than a label and giving it a
// button text would force the caller to find an icon asset.
//
// Show and hide are animated: the whole surface (container, text and children)
// slides up out of the widget rectangle while fading, over the theme's medium2
// motion spec. A widget that reveals to zero hides itself, so an animated hide
// ends in the same state as a plain hide().

#pragma once

#include <md/drawable/icon_drawable.h>
#include <md/view/view_group.h>

#include <QColor>
#include <QElapsedTimer>
#include <QIcon>
#include <QString>
#include <QTimer>

class QEvent;
class QMouseEvent;
class QPainter;
class QRectF;

namespace md {

class Button;

class Banner : public ViewGroup
{
    Q_OBJECT

public:
    enum class Action {
        None = 0,
        Primary = 1,
        Secondary = 2,
    };
    Q_ENUM(Action)

    explicit Banner(QWidget* parent = nullptr);

    void setIcon(const QIcon& icon);
    [[nodiscard]] QIcon icon() const;

    void setTitle(const QString& title);
    [[nodiscard]] QString title() const;

    void setDescription(const QString& description);
    [[nodiscard]] QString description() const;

    // Empty text hides the corresponding action.
    void setPrimaryAction(const QString& text);
    [[nodiscard]] QString primaryAction() const;

    void setSecondaryAction(const QString& text);
    [[nodiscard]] QString secondaryAction() const;

    void setCloseButtonVisible(bool visible);
    [[nodiscard]] bool isCloseButtonVisible() const;

    // The action widgets, owned by the banner. Exposed so a caller can adjust
    // their variant or enabled state and so a test can click them.
    [[nodiscard]] Button* primaryButton() const;
    [[nodiscard]] Button* secondaryButton() const;

    // The close affordance in widget coordinates (already offset by the reveal
    // animation, so it is the rectangle that actually responds to a press).
    [[nodiscard]] QRectF closeButtonRect() const;

    void showAnimated();
    void hideAnimated();

    // Hides the banner with the animation and emits closed(). Emitting is
    // immediate; only the visual hide is deferred.
    void close();

    [[nodiscard]] qreal revealProgress() const noexcept;
    [[nodiscard]] bool isRevealed() const noexcept;

signals:
    void actionClicked(Banner::Action action);
    void closed();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeChildren(const QRectF& content) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

    void onThemeChanged() override;

private:
    void syncAppearance();
    void syncButtons();
    void startReveal(qreal target);
    void tickReveal();
    void finishReveal();
    void applyRevealLayout();

    [[nodiscard]] qreal revealOffset() const;
    [[nodiscard]] qreal barHeightPx() const;
    [[nodiscard]] QRectF closeButtonArea() const;

    drawable::IconDrawable iconDrawable_;
    QString title_;
    QString description_;
    QString primaryText_;
    QString secondaryText_;
    bool closeVisible_ = true;
    bool closedEmitted_ = false;
    qreal actionsWidth_ = 0.0;

    Button* primary_ = nullptr;
    Button* secondary_ = nullptr;

    QTimer revealTimer_;
    QElapsedTimer revealClock_;
    qreal reveal_ = 1.0;
    qreal revealFrom_ = 1.0;
    qreal revealTarget_ = 1.0;
    bool hoverClose_ = false;
    bool pressedClose_ = false;
};

} // namespace md
