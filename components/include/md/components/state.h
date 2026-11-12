// Periodicity — StateLayout (framework.md §6.3).
//
// A content container that swaps its visible child for the state the
// application is in: loading, empty, error, offline, or the real content. It
// is a layout, not a label: the state machine decides which child is visible,
// the active child fills the container, and a state without a widget of its own
// paints a placeholder instead.
//
// The five roles are fixed but the placeholder text is not: an application can
// localise it with setPlaceholderText() without replacing the widget.

#pragma once

#include <md/view/view_group.h>

#include <QString>
#include <Qt>

class QPainter;
class QRectF;

namespace md {

class StateLayout : public ViewGroup
{
    Q_OBJECT

public:
    // The roles the container can show. Content is the normal, "everything
    // worked" state; the other four are placeholders unless a widget is
    // installed for them.
    enum class State {
        Content,
        Loading,
        Empty,
        Error,
        Offline,
    };
    Q_ENUM(State)

    explicit StateLayout(QWidget* parent = nullptr);
    explicit StateLayout(State state, QWidget* parent = nullptr);
    ~StateLayout() override;

    // Switches the visible role. Emits stateChanged() only when the value
    // actually changes.
    void setState(State state);
    [[nodiscard]] State state() const noexcept { return state_; }

    // Installs the widget for a role. The container reparents it, takes
    // ownership of it, and shows it only when its role is the active state.
    // Passing a null view clears the role; the previous view is handed back
    // (parented to nothing) rather than deleted.
    void setContentWidget(View* widget);
    void setLoadingWidget(View* widget);
    void setEmptyWidget(View* widget);
    void setErrorWidget(View* widget);
    void setOfflineWidget(View* widget);

    [[nodiscard]] View* contentWidget() const;
    [[nodiscard]] View* loadingWidget() const;
    [[nodiscard]] View* emptyWidget() const;
    [[nodiscard]] View* errorWidget() const;
    [[nodiscard]] View* offlineWidget() const;

    // The text painted when a state has no widget. An empty string restores the
    // built-in default for that role.
    void setPlaceholderText(State state, QString text);
    [[nodiscard]] QString placeholderText(State state) const;

signals:
    void stateChanged(md::StateLayout::State state);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeChildren(const QRectF& content) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

private:
    [[nodiscard]] static int indexOf(State state) noexcept;
    [[nodiscard]] View* widgetAt(State state) const noexcept;
    void installWidget(State state, View* widget);
    void applyState();
    [[nodiscard]] QString placeholderFor(State state) const;
    void paintPlaceholder(QPainter& painter, const QRectF& content) const;

    State state_ = State::Content;
    View* widgets_[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    QString placeholders_[5];
};

// The component was called State before it became a container. The alias keeps
// the old spelling usable; new code should say StateLayout.
using State = StateLayout;

} // namespace md

Q_DECLARE_METATYPE(md::StateLayout::State)
