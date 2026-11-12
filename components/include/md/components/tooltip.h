// Periodicity — Tooltip, a non-interactive label anchored to a widget.
//
// The tooltip is a window-level surface in the Tooltip layer: it never takes
// focus, never takes the mouse (Qt::WA_TransparentForMouseEvents), and it
// positions itself above the anchor, flipping below when the screen has no
// room. Hovering the anchor starts a delay timer; leaving it, pressing it or
// hiding it closes the tooltip. showText() is the one-line convenience for the
// common case.

#pragma once

#include <md/view/view.h>

#include <QPointer>
#include <QRectF>
#include <QString>

class QEvent;
class QPainter;
class QTimer;
class QWidget;

namespace md {

class Tooltip final : public View
{
    Q_OBJECT

public:
    enum class Placement {
        Above,
        Below,
        Left,
        Right,
        Auto,
    };
    Q_ENUM(Placement)

    explicit Tooltip(QWidget* parent = nullptr);
    ~Tooltip() override;

    // The widget the label is attached to. Not owned; hiding or destroying it
    // closes the tooltip.
    void setAnchor(QWidget* anchor);
    [[nodiscard]] QWidget* anchor() const;

    void setText(const QString& text);
    [[nodiscard]] QString text() const;

    void setPlacement(Placement placement);
    [[nodiscard]] Placement placement() const noexcept;
    [[nodiscard]] Placement effectivePlacement() const noexcept;

    // Milliseconds the pointer must rest on the anchor before opening. Zero
    // opens on the next hover event.
    void setDelay(int milliseconds);
    [[nodiscard]] int delay() const noexcept;

    void setOffset(const QPointF& offset);
    [[nodiscard]] QPointF offset() const noexcept;

    void setMaximumWidthDp(qreal widthDp);
    [[nodiscard]] qreal maximumWidthDp() const noexcept;

    // Opening and closing. show() is immediate; hover() starts the delay.
    void show();
    void hide();
    [[nodiscard]] bool isOpen() const noexcept;
    void hover();
    void reposition();

    [[nodiscard]] QRectF panelRect() const;
    [[nodiscard]] QRectF panelGlobalRect() const;

    // The one-shot convenience: creates (or reuses) the tooltip of the
    // anchor's window, sets the text and shows it after `delayMs`.
    static Tooltip* showText(QWidget* anchor, const QString& text, int delayMs = 500);

signals:
    void opened();
    void closed();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void applyGeometry();
    void installFilters();
    void removeFilters();
    [[nodiscard]] QFont textFont() const;
    [[nodiscard]] QSizeF preferredSize() const;

    QPointer<QWidget> anchor_;
    QWidget* host_ = nullptr;
    QTimer* delayTimer_ = nullptr;
    QString text_;
    Placement placement_ = Placement::Above;
    Placement effective_ = Placement::Above;
    QPointF offset_;
    qreal maximumWidthDp_ = 240.0;
    int delayMs_ = 500;
    bool open_ = false;
    QRectF panel_;
};

} // namespace md
