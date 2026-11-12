// Periodicity — Dropdown, the anchored popup layer.
//
// Dropdown is the surface underneath anything that opens next to a widget: a
// select list, a picker, a command palette or a custom panel. It owns the three
// things every popup otherwise reimplements badly:
//
//   * placement — below the anchor by default, flipped above when the screen
//     has no room, side placements for rails, and every edge clamped to the
//     screen's available geometry;
//   * the layer — it registers with Modal at Level::Dropdown, so it sits above
//     page content but never above a dialog or a notification;
//   * dismissal — a press outside the panel or its anchor, Escape, the anchor
//     hiding or being destroyed.
//
// The content is a View supplied by the host and is not deleted by the
// dropdown. The widget rectangle includes a shadow margin; the visible panel is
// panelRect(), and the content is arranged inside contentRect().

#pragma once

#include <md/view/view.h>

#include <QColor>
#include <QPointF>
#include <QPointer>
#include <QRectF>

class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QWidget;

namespace md {

class Dropdown final : public View
{
    Q_OBJECT

public:
    enum class Placement {
        Below,
        Above,
        Left,
        Right,
        Auto,
    };
    Q_ENUM(Placement)

    explicit Dropdown(QWidget* parent = nullptr);
    ~Dropdown() override;

    // Anchor -------------------------------------------------------------
    // The widget the panel is attached to. Not owned; hiding or destroying it
    // closes the dropdown according to closeOnAnchorHidden().
    void setAnchor(QWidget* anchor);
    [[nodiscard]] QWidget* anchor() const;

    // Content ------------------------------------------------------------
    void setContent(View* content);
    [[nodiscard]] View* content() const;

    // Placement ----------------------------------------------------------
    void setPlacement(Placement placement);
    [[nodiscard]] Placement placement() const noexcept;
    // The side actually used by the last layout.
    [[nodiscard]] Placement effectivePlacement() const noexcept;

    // A gap added to the chosen side, in pixels.
    void setOffset(const QPointF& offset);
    [[nodiscard]] QPointF offset() const noexcept;

    // The panel width follows the anchor unless the content is wider.
    void setMatchAnchorWidth(bool match);
    [[nodiscard]] bool matchesAnchorWidth() const noexcept;

    void setMinimumWidthDp(qreal widthDp);
    [[nodiscard]] qreal minimumWidthDp() const noexcept;

    // Taller content is clipped to this height; the content can scroll itself.
    void setMaximumHeightDp(qreal heightDp);
    [[nodiscard]] qreal maximumHeightDp() const noexcept;

    // Dismissal ----------------------------------------------------------
    void setDismissOnOutsideClick(bool dismiss);
    [[nodiscard]] bool dismissesOnOutsideClick() const noexcept;
    void setCloseOnAnchorHidden(bool close);
    [[nodiscard]] bool closesOnAnchorHidden() const noexcept;

    // Opening ------------------------------------------------------------
    void open();
    void close();
    [[nodiscard]] bool isOpen() const noexcept;
    void reposition();

    // Geometry -----------------------------------------------------------
    // The visible panel in widget coordinates (the widget itself is larger by
    // the shadow margin).
    [[nodiscard]] QRectF panelRect() const;
    [[nodiscard]] QRectF popupGlobalRect() const;
    [[nodiscard]] QRectF contentRect() const;
    [[nodiscard]] qreal shadowMargin() const;

signals:
    void opened();
    void closed();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyGeometry();
    void installFilters();
    void removeFilters();
    [[nodiscard]] bool containsGlobal(const QPoint& global) const;
    [[nodiscard]] QSizeF contentHint() const;

    QPointer<QWidget> anchor_;
    View* content_ = nullptr;
    QWidget* host_ = nullptr;
    Placement placement_ = Placement::Auto;
    Placement effective_ = Placement::Below;
    QPointF offset_;
    bool matchAnchorWidth_ = true;
    qreal minimumWidthDp_ = 0.0;
    qreal maximumHeightDp_ = 360.0;
    bool dismissOnOutsideClick_ = true;
    bool closeOnAnchorHidden_ = true;
    bool open_ = false;
    QRectF panel_;
};

} // namespace md
