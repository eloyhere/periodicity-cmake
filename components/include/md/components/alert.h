// Periodicity — Alert, an inline severity banner.
//
// An Alert is a filled container in the tone that matches its severity, with a
// leading severity glyph, a title, an optional description, an optional text
// action and a close affordance. The container is the View background layer
// (so it is clipped by the medium shape), while the glyph, the text and the
// close "X" are painted by the component. The action is a real Button child, so
// it inherits the framework's state layer, ripple and focus handling.
//
// Severity chooses the design baseline role pair: Info uses the high surface container,
// Success the secondary container, Warning the tertiary container and Error the
// error container. The component resolves the pair from the active theme; it
// never hard-codes a colour.

#pragma once

#include <md/view/view_group.h>

#include <QColor>
#include <QString>

class QEvent;
class QMouseEvent;
class QPainter;
class QRectF;

namespace md {

class Button;

class Alert : public ViewGroup
{
    Q_OBJECT

public:
    enum class Severity {
        Info,
        Success,
        Warning,
        Error,
    };
    Q_ENUM(Severity)

    explicit Alert(QWidget* parent = nullptr);

    void setSeverity(Severity severity);
    [[nodiscard]] Severity severity() const noexcept;

    void setTitle(const QString& title);
    [[nodiscard]] QString title() const;

    void setDescription(const QString& description);
    [[nodiscard]] QString description() const;

    // Empty text hides the action button.
    void setActionText(const QString& text);
    [[nodiscard]] QString actionText() const;

    void setClosable(bool closable);
    [[nodiscard]] bool isClosable() const noexcept;

    // The resolved container and content colours. They are exposed so a host
    // (and a test) can assert the severity mapping without reading pixels.
    [[nodiscard]] QColor containerColour() const;
    [[nodiscard]] QColor contentColour() const;

    // Hides the alert and emits closeClicked(). Emitting is guarded, so calling
    // close() twice reports one close.
    void close();

    [[nodiscard]] Button* actionButton() const;
    [[nodiscard]] QRectF closeButtonRect() const;

signals:
    void actionClicked();
    void closeClicked();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeChildren(const QRectF& content) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void onShown() override;
    void onThemeChanged() override;
    [[nodiscard]] bool focusable() const override;

private:
    void syncAppearance();
    void syncAction();
    [[nodiscard]] qreal contentTextHeight(qreal textWidth) const;
    [[nodiscard]] qreal actionAreaWidth() const;

    Severity severity_ = Severity::Info;
    QString title_;
    QString description_;
    QString actionText_;
    bool closable_ = true;
    bool closedEmitted_ = false;
    bool hoverClose_ = false;
    bool pressedClose_ = false;
    qreal actionWidth_ = 0.0;

    Button* action_ = nullptr;
};

} // namespace md
