// Periodicity — dialogs.
//
// A Dialog is an in-window overlay, not a top-level window: open() creates no
// platform window and covers the window's surface, paints the scrim and the
// centred card, and follows the window's resize. That keeps the whole family
// testable under the offscreen platform and lets a dialog composite inside the
// same backing store as its host.
//
// The window-level part — finding the window, stacking the dialog above what
// is already up, blocking input below it, disabling/restoring the content root
// and restoring focus — is md::Overlay (md/view/overlay.h). open() is a push
// and close()/accept()/reject() are a pop plus a hide. Dialog keeps only its
// own contract: the title, the text, the action row, the body hooks and the
// accepted/rejected protocol. It no longer installs a parent event filter and
// it does not decide whether it is top-most; when it is not, Overlay never
// delivers the input to it.
//
// The base class owns the surface, the title, the body slot, the action row and
// the accept/reject protocol. Subclasses supply the body: plain text for the
// base Dialog, a radio list for SingleChoiceDialog, a checkbox list for
// MultipleChoiceDialog, and an arbitrary View for CustomDialog. Action buttons
// are real Button children, so they inherit the framework's state layers,
// ripples and focus handling.
//
// Escape rejects. A press outside the surface rejects when
// scrimDismissible() is true. close() hides without emitting; accept() and
// reject() hide and emit.
#pragma once

#include <md/view/overlay.h>
#include <md/view/view_group.h>

#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;
class QWidget;

namespace md {

class Button;
class View;

class Dialog : public ViewGroup
{
    Q_OBJECT

public:
    enum class ActionRole {
        Accept,
        Reject,
        Custom,
    };
    Q_ENUM(ActionRole)

    explicit Dialog(QWidget* parent = nullptr);

    void setTitle(const QString& title);
    [[nodiscard]] QString title() const;

    // The plain-text body of the base dialog. Subclasses that paint their own
    // body ignore it.
    void setText(const QString& text);
    [[nodiscard]] QString text() const;

    void setScrimDismissible(bool dismissible);
    [[nodiscard]] bool isScrimDismissible() const noexcept;

    // open() covers the window and takes focus; close() only hides.
    void open();
    void close();
    [[nodiscard]] bool isOpen() const noexcept;

    // Accept/reject hide the dialog and emit the matching signal. They are
    // no-ops when the dialog is already closed.
    void accept();
    void reject();

    [[nodiscard]] int actionCount() const;
    [[nodiscard]] Button* actionButton(int index) const;

    // The visible surface and the body slot, in dialog coordinates.
    [[nodiscard]] QRectF surfaceRect() const;
    [[nodiscard]] QRectF bodyRect() const;

signals:
    void accepted();
    void rejected();
    void actionTriggered(int index);

protected:
    // Appends an action button and returns its index. The last action is the
    // right-most (the primary action in a design baseline dialog).
    int addActionButton(const QString& text, ActionRole role);

    // Re-measures and re-lays out the dialog after the body changed.
    void relayout();

    // The body contract. The default paints text(); a subclass overrides the
    // four hooks to supply its own content.
    [[nodiscard]] virtual qreal bodyHeightForWidth(qreal width) const;
    virtual void paintBody(QPainter& painter, const QRectF& body) const;
    virtual bool handleBodyClick(const QPointF& position);
    virtual bool handleBodyKey(QKeyEvent* event);
    virtual void arrangeBody(const QRectF& body);

    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeChildren(const QRectF& content) override;
    // Action buttons are child widgets composed by Qt; the ViewGroup manual
    // pass would paint a second, offset copy of every action.
    void paintChildren(QPainter& painter) override { Q_UNUSED(painter); }
    void paintContent(QPainter& painter, const QRectF& content) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void onThemeChanged() override;
    [[nodiscard]] bool focusable() const override;

private:
    struct ActionEntry
    {
        Button* button = nullptr;
        ActionRole role = ActionRole::Custom;
    };

    void syncAppearance();
    void relayoutSurface();
    void handleWindowHidden();
    void handleAction(int index);

    [[nodiscard]] qreal surfaceWidth() const;
    [[nodiscard]] qreal titleLineHeight() const;
    [[nodiscard]] qreal surfaceHeightForWidth(qreal width) const;
    [[nodiscard]] QRectF actionRowRect(const QRectF& surface) const;
    [[nodiscard]] qreal bodyTop(const QRectF& surface) const;

    QList<ActionEntry> actions_;
    QString title_;
    QString text_;

    // The host this dialog is pushed onto while open. A QPointer because the
    // window may die while the dialog survives (only when it was reparented).
    QPointer<Overlay> overlay_;
    bool open_ = false;
    bool scrimDismissible_ = true;
    bool pressedScrim_ = false;
    bool pressedBody_ = false;
};

// Confirm / cancel. The accept button is the primary action on the right.
class ConfirmDialog : public Dialog
{
    Q_OBJECT

public:
    explicit ConfirmDialog(QWidget* parent = nullptr);
    ConfirmDialog(const QString& title, const QString& text, QWidget* parent = nullptr);

    void setConfirmText(const QString& text);
    [[nodiscard]] QString confirmText() const;

    void setCancelText(const QString& text);
    [[nodiscard]] QString cancelText() const;
};

// One acknowledgement button ("知道了" by default).
class MessageDialog : public Dialog
{
    Q_OBJECT

public:
    explicit MessageDialog(QWidget* parent = nullptr);
    MessageDialog(const QString& title, const QString& message, QWidget* parent = nullptr);

    void setMessage(const QString& message);
    [[nodiscard]] QString message() const;

    void setButtonText(const QString& text);
    [[nodiscard]] QString buttonText() const;
};

// A radio list. The dialog accepts only after a row has been selected.
class SingleChoiceDialog : public Dialog
{
    Q_OBJECT

public:
    explicit SingleChoiceDialog(QWidget* parent = nullptr);
    SingleChoiceDialog(const QString& title, const QStringList& items,
                       QWidget* parent = nullptr);

    void setItems(const QStringList& items);
    [[nodiscard]] QStringList items() const;

    // -1 means "nothing selected". Setting an out-of-range index clears the
    // selection.
    void setSelectedIndex(int index);
    [[nodiscard]] int selectedIndex() const noexcept;
    [[nodiscard]] QString selectedValue() const;

    void setConfirmText(const QString& text);
    [[nodiscard]] QString confirmText() const;
    void setCancelText(const QString& text);
    [[nodiscard]] QString cancelText() const;

    // The row rectangle in dialog coordinates.
    [[nodiscard]] QRectF itemRect(int index) const;

signals:
    void selectionChanged(int index);

protected:
    [[nodiscard]] qreal bodyHeightForWidth(qreal width) const override;
    void paintBody(QPainter& painter, const QRectF& body) const override;
    bool handleBodyClick(const QPointF& position) override;
    bool handleBodyKey(QKeyEvent* event) override;

private:
    void applySelection(int index);

    QStringList items_;
    QString confirmText_ = QStringLiteral("确定");
    QString cancelText_ = QStringLiteral("取消");
    int selected_ = -1;
    int highlighted_ = 0;
};

// A checkbox list. Any number of rows may be selected, including none.
class MultipleChoiceDialog : public Dialog
{
    Q_OBJECT

public:
    explicit MultipleChoiceDialog(QWidget* parent = nullptr);
    MultipleChoiceDialog(const QString& title, const QStringList& items,
                         QWidget* parent = nullptr);

    void setItems(const QStringList& items);
    [[nodiscard]] QStringList items() const;

    void setItemChecked(int index, bool checked);
    [[nodiscard]] bool isItemChecked(int index) const;
    void setCheckedIndices(const QList<int>& indices);

    // Ascending item order.
    [[nodiscard]] QList<int> selectedIndices() const;
    [[nodiscard]] QStringList selectedValues() const;

    void setConfirmText(const QString& text);
    [[nodiscard]] QString confirmText() const;
    void setCancelText(const QString& text);
    [[nodiscard]] QString cancelText() const;

    [[nodiscard]] QRectF itemRect(int index) const;

signals:
    void selectionChanged();

protected:
    [[nodiscard]] qreal bodyHeightForWidth(qreal width) const override;
    void paintBody(QPainter& painter, const QRectF& body) const override;
    bool handleBodyClick(const QPointF& position) override;
    bool handleBodyKey(QKeyEvent* event) override;

private:
    void toggle(int index);

    QStringList items_;
    QList<bool> checked_;
    QString confirmText_ = QStringLiteral("确定");
    QString cancelText_ = QStringLiteral("取消");
    int highlighted_ = 0;
};

// An arbitrary content View plus a caller-defined set of action buttons.
class CustomDialog : public Dialog
{
    Q_OBJECT

public:
    explicit CustomDialog(QWidget* parent = nullptr);
    ~CustomDialog() override;

    // Not owned by the dialog; it is reparented into the body while set and
    // detached again when the dialog is destroyed.
    void setContent(View* content);
    [[nodiscard]] View* content() const;

    // Appends an action and returns its index. Custom actions keep the dialog
    // open; Accept/Reject actions also close it.
    int addAction(const QString& text, ActionRole role = ActionRole::Custom);

protected:
    [[nodiscard]] qreal bodyHeightForWidth(qreal width) const override;
    void paintBody(QPainter& painter, const QRectF& body) const override;
    void arrangeBody(const QRectF& body) override;

private:
    QPointer<View> content_;
};

} // namespace md
