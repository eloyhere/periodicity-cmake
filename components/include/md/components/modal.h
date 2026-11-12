// Periodicity — Modal, the shared scrim and layer manager.
//
// Every floating surface in the library needs the same two things: a scrim
// that dims and blocks the content behind it, and a place in a deterministic
// stacking order. Dialog, Drawer, Menu, Dropdown and Notification each used to
// invent their own scrim and rely on raise() calls scattered through the call
// sites, which is why a dropdown could end up above a dialog or behind a
// drawer.
//
// Modal is that shared piece. It is a full-window scrim widget, and it also
// owns a tiny per-window layer registry: a widget registers with a Level, and
// the registry restacks the registered widgets in ascending level order so the
// ordering is stable regardless of which widget was shown last.
//
// Levels are values, not enums only, so a host can place its own surface
// between two of ours without editing the library.

#pragma once

#include <md/view/view.h>

#include <QHash>
#include <QRectF>
#include <QString>

class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QWidget;

namespace md {

class Modal final : public View
{
    Q_OBJECT

public:
    enum class Level {
        Content = 0,
        Dropdown = 100,
        Drawer = 200,
        Dialog = 300,
        Notification = 400,
        Tooltip = 500,
    };

    explicit Modal(QWidget* parent = nullptr);
    ~Modal() override;

    // The scrim's place in the stack. Registering happens on open(); changing
    // the level while open restacks immediately.
    void setLevel(Level level);
    void setLevelValue(int level);
    [[nodiscard]] int levelValue() const;

    // 0 is transparent, 1 is the theme's scrim colour at full strength. The
    // default is 0.32, the Material modal scrim.
    void setDim(qreal dim);
    [[nodiscard]] qreal dim() const noexcept;

    void setScrimColour(const QColor& colour);
    [[nodiscard]] QColor scrimColour() const;

    void setDismissOnClick(bool dismiss);
    [[nodiscard]] bool dismissesOnClick() const noexcept;
    void setDismissOnEscape(bool dismiss);
    [[nodiscard]] bool dismissesOnEscape() const noexcept;

    void open();
    void close();
    [[nodiscard]] bool isOpen() const noexcept;

    [[nodiscard]] QRectF scrimRect() const;

    // Layer registry -----------------------------------------------------
    // Registers a window-level widget. The widget must be a child of the
    // window; registerLayer() restacks the window after storing the level.
    static void registerLayer(QWidget* layer, Level level);
    static void registerLayer(QWidget* layer, int level);
    static void unregisterLayer(QWidget* layer);
    [[nodiscard]] static int layerOf(const QWidget* layer);
    // Raises `layer` within its own level without letting it jump above a
    // higher level (a dropdown cannot cover a dialog that opened after it).
    static void raiseWithinLayer(QWidget* layer);
    [[nodiscard]] static QWidget* topLayer(QWidget* window);
    static void restack(QWidget* window);

signals:
    void clicked();
    void opened();
    void closed();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyWindowGeometry();
    void installFilters();
    void removeFilters();

    QWidget* host_ = nullptr;
    int level_ = static_cast<int>(Level::Dialog);
    qreal dim_ = 0.32;
    QColor scrimColour_;
    bool dismissOnClick_ = true;
    bool dismissOnEscape_ = true;
    bool open_ = false;
};

} // namespace md
