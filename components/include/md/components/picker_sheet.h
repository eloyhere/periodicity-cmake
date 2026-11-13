// Periodicity — PickerSheet, the one modal sheet behind the configuration
// switches.
//
// Font, Theme and Language all need the same presentation: a scrim, a card
// that scales and rises in and out, a title, a scrollable list of options with
// a check on the committed value, and Cancel/Confirm affordances that apply a
// choice only when the user confirms it. This class is that presentation. The
// three switches supply only their option list and their current value; they
// never create a Modal, a card or an animation of their own.
//
// The sheet owns a Modal (whose scrim fade and card transform are the shared
// motion path) and a private card View that paints the list. Rows use the
// shared RowFeedback helper for hover/pressed state layers, the keyboard
// highlight and click ripples, so the picker rows behave exactly like the rows
// of the select popups.
//
// Lifetime: the sheet is parented to the invoking widget's window, not to the
// invoking widget itself, so deleting the chip does not destroy a sheet that is
// already on screen. Every callback captures a QPointer and the modal, card and
// invoker are all QPointers; a deleted invoker therefore leaves the sheet safe
// (nothing is applied) rather than touching freed state. A one-shot sheet
// deletes itself after the exit animation.

#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <chrono>

class QWidget;

namespace md {

class Modal;
class PickerSheetCard;
class RowFeedback;

class PickerSheet final : public QObject
{
    Q_OBJECT

public:
    struct Option
    {
        QString id;
        QString label;
    };

    // `invoker` is the widget the sheet belongs to: it is the placement anchor
    // when no explicit anchor is set, the focus-return target, and the widget
    // whose window owns the sheet. It may be destroyed while the sheet is open.
    explicit PickerSheet(QWidget* invoker, QObject* parent = nullptr);
    ~PickerSheet() override;

    PickerSheet(const PickerSheet&) = delete;
    PickerSheet& operator=(const PickerSheet&) = delete;

    // Content ------------------------------------------------------------
    void setTitle(const QString& title);
    [[nodiscard]] QString title() const;

    // Replaces the option list. `currentId` is the committed value; it is the
    // row that carries the check, not the keyboard highlight. An unknown id
    // leaves the check off and highlights the first option.
    void setOptions(const QList<Option>& options, const QString& currentId);
    [[nodiscard]] QList<Option> options() const;
    [[nodiscard]] QString currentId() const;

    // The pending selection. Moving it does not apply anything; only
    // confirm() (or Enter on the list) emits confirmed().
    [[nodiscard]] QString highlightedId() const;
    [[nodiscard]] int highlightedRow() const;
    void setHighlightedId(const QString& id);

    // Placement and geometry ---------------------------------------------
    void setAnchor(QWidget* anchor);
    [[nodiscard]] QWidget* invoker() const;
    [[nodiscard]] QWidget* card() const;
    [[nodiscard]] Modal* modal() const;
    [[nodiscard]] RowFeedback* rowFeedback() const;

    // Presentation -------------------------------------------------------
    void open();
    // Cancels: emits cancelled() once, leaves the committed value alone and
    // plays the mirrored exit animation.
    void close();
    void cancel();
    // Applies the highlighted option: emits confirmed() once and exits.
    void confirm();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] bool isAnimating() const;

    // Deterministic animation clock, forwarded to the modal and the rows.
    // Enable it before open(); advanceAnimation() then steps the entrance,
    // the exit and any ripple without waiting for wall-clock time.
    void setManualAnimationClock(bool enabled);
    [[nodiscard]] bool isManualAnimationClock() const;
    void advanceAnimation(std::chrono::milliseconds delta);

    // The first row currently painted in the scrollable list.
    [[nodiscard]] int firstVisibleRow() const;

signals:
    void confirmed(const QString& id);
    void cancelled();
    // The exit animation finished and the surface is hidden. The sender is
    // still alive; a heap-owning caller connects this to deleteLater().
    void finished();

private:
    enum class Outcome
    {
        None,
        Confirmed,
        Cancelled,
    };

    void confirmWith(const QString& id);
    void handleModalClosed();
    [[nodiscard]] int indexOf(const QString& id) const;

    QPointer<QWidget> invoker_;
    QPointer<QWidget> anchor_;
    QPointer<Modal> modal_;
    QPointer<PickerSheetCard> card_;

    QList<Option> options_;
    QString title_;
    QString currentId_;
    QString highlightedId_;
    Outcome outcome_ = Outcome::None;
    bool cancelledEmitted_ = false;
    bool confirmedEmitted_ = false;
    bool manualClock_ = false;
};

} // namespace md
