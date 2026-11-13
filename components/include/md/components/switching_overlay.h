// Periodicity — SwitchingOverlay, the busy surface of a configuration switch.
//
// A theme rebuild, a font reload and a retranslation can take long enough that
// the new value flashing in immediately reads as a glitch. This overlay covers
// the window after the picker's exit animation, shows an indeterminate progress
// indicator and a short "Switching…" label, runs the switch while it is up,
// holds for a minimum visible duration and then plays the mirrored exit.
//
// It is a Modal (the same scrim and card transform as the picker), but input
// dismissal is disabled: the switch is not cancellable. Escape and clicks are
// swallowed, and a second run while busy is ignored rather than queued.
//
// The window-level work — blocking input behind the surface, disabling the
// content tree with an exact restore, and returning focus — is md::Overlay's.
// The content is disabled late (setContentDisabled on the modal's Overlay once
// the scrim is fully opaque) so the content's disabled-token repaint happens
// behind an opaque frame, and Overlay restores the saved state when the modal
// pops.
//
// Failure is not a hang: the work callback is called inside a try/catch, a
// throwing switch still closes the overlay after the minimum hold, and
// lastError() carries the reason. The caller is responsible for restoring the
// previous value (the three configuration components do so in their work
// lambda).

#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

#include <chrono>
#include <functional>

class QWidget;

namespace md {

namespace animation {
class Animator;
}

class Modal;
class SwitchingCard;

class SwitchingOverlay final : public QObject
{
    Q_OBJECT

public:
    explicit SwitchingOverlay(QWidget* invoker, QObject* parent = nullptr);
    ~SwitchingOverlay() override;

    SwitchingOverlay(const SwitchingOverlay&) = delete;
    SwitchingOverlay& operator=(const SwitchingOverlay&) = delete;

    // Starts one switch. A call while busy is ignored. `work` runs once the
    // entrance has finished, with the overlay already covering the window.
    void run(std::function<void()> work);

    [[nodiscard]] bool isBusy() const noexcept;
    [[nodiscard]] bool isVisible() const;
    // The reason the last work callback failed, empty until one throws.
    [[nodiscard]] QString lastError() const;
    // The indeterminate indicator's 0..1 progress, for a test to observe.
    [[nodiscard]] qreal progressValue() const;

    // Deterministic clock, forwarded to the modal, the indicator and the
    // minimum-duration hold. Enable before run().
    void setManualAnimationClock(bool enabled);
    [[nodiscard]] bool isManualAnimationClock() const;
    void advanceAnimation(std::chrono::milliseconds delta);

    [[nodiscard]] Modal* modal() const;
    [[nodiscard]] int minimumVisibleMs() const;

signals:
    void finished();

private:
    void performWork();
    void maybeClose();
    void finishClose();
    void ensureSurface();

    QPointer<QWidget> invoker_;
    QPointer<Modal> modal_;
    QPointer<SwitchingCard> card_;
    animation::Animator* hold_ = nullptr;
    std::function<void()> work_;

    bool busy_ = false;
    bool workDone_ = false;
    bool holdDone_ = false;
    bool closing_ = false;
    bool manualClock_ = false;
    QString lastError_;
};

} // namespace md
