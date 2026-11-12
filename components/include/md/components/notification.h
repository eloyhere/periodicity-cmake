// Periodicity — Notification, an in-window snackbar.
//
// A Notification is a plain child widget, never a top-level window: it lives in
// the same backing store as the rest of the UI, so it needs no window flags and
// behaves the same under the offscreen platform as in a desktop session.
//
// Messages are queued. showMessage() either starts the message immediately or
// appends it behind the one on screen; when the visible message times out (or
// its action fires, or it is closed) the next one is shown. A non-positive
// duration makes a message sticky until the next message or close().
//
// Placement defaults to the bottom-right corner of the parent widget. Calling
// setAnchor() ties the surface to a particular widget instead: it appears under
// the anchor when there is room and above it otherwise, clamped to the parent.

#pragma once

#include <md/view/view.h>

#include <QList>
#include <QPointer>
#include <QString>
#include <QTimer>

class QEvent;
class QHideEvent;
class QMouseEvent;
class QPainter;
class QRectF;
class QWidget;

namespace md {

class Notification : public View
{
    Q_OBJECT

public:
    explicit Notification(QWidget* parent = nullptr);

    // The widget the surface is placed against. Not owned; destroying it
    // detaches the anchor and falls back to the parent's bottom-right corner.
    void setAnchor(QWidget* anchor);
    [[nodiscard]] QWidget* anchor() const;

    // `durationMs` <= 0 keeps the message on screen until it is replaced or
    // close() is called. The default is 4000 ms.
    void showMessage(const QString& text, const QString& actionText = QString(),
                     int durationMs = 4000);

    [[nodiscard]] QString currentText() const;
    [[nodiscard]] QString currentActionText() const;
    [[nodiscard]] int queuedCount() const;
    [[nodiscard]] bool isShowing() const noexcept;

    // Drops the visible message and every queued message. Emits closed() when
    // something was actually on screen.
    void close();

    // Whether the surface paints a close affordance. Off by default; close()
    // is available regardless.
    void setClosable(bool closable);
    [[nodiscard]] bool isClosable() const noexcept;

    // Hit rectangles in widget coordinates.
    [[nodiscard]] QRectF actionRect() const;
    [[nodiscard]] QRectF closeButtonRect() const;

signals:
    void actionTriggered();
    void closed();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void onThemeChanged() override;
    [[nodiscard]] bool focusable() const override;

private:
    struct Message
    {
        QString text;
        QString actionText;
        int durationMs = 4000;
    };

    void startNext();
    void dismissCurrent(bool notifyWhenEmpty);
    void updatePosition();
    void updateTimer();
    void installFilters();
    void removeFilters();
    [[nodiscard]] qreal surfaceWidth() const;

    QString text_;
    QString actionText_;
    int durationMs_ = 4000;
    QList<Message> queue_;
    QTimer timer_;
    QPointer<QWidget> anchor_;

    bool showing_ = false;
    bool closable_ = false;
    qreal actionWidth_ = 0.0;

    bool hoverAction_ = false;
    bool pressedAction_ = false;
    bool hoverClose_ = false;
    bool pressedClose_ = false;
};

} // namespace md
