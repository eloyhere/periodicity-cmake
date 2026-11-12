// Periodicity — PopUp, an anchored floating surface.
//
// A PopUp is a child of the anchor's window rather than a separate top-level
// window: it composites in the same backing store as the rest of the UI, it
// follows the window while the window moves, and it needs no window flags that
// a headless test would have to emulate.
//
// The placement is a request, not a promise. The surface is laid out below,
// above, right or left of the anchor; when that side does not fit inside the
// screen's available geometry the opposite side is used instead, and the
// result is clamped to the screen. The arrow follows the final side and points
// at the anchor's centre within the limits of the body.
//
// Dismissal is both explicit (close()) and implicit: a press outside the
// surface or Escape closes it. The outside-press rule is implemented with an
// application event filter installed only while the popup is open, so a closed
// popup costs nothing.

#pragma once

#include <md/view/view_group.h>

#include <QPointer>

class QEvent;
class QHideEvent;
class QPainter;
class QPainterPath;
class QRect;
class QRectF;
class QSizeF;
class QWidget;

namespace md {

class View;

class PopUp : public ViewGroup
{
    Q_OBJECT

public:
    enum class Placement {
        Below, // the surface sits under the anchor; the arrow points up
        Above, // the surface sits over the anchor; the arrow points down
        Right, // the surface sits right of the anchor; the arrow points left
        Left,  // the surface sits left of the anchor; the arrow points right
    };
    Q_ENUM(Placement)

    explicit PopUp(QWidget* parent = nullptr);

    // The widget the surface is attached to. It is not owned. Setting a new
    // anchor while open repositions; destroying the anchor closes the popup.
    void setAnchor(QWidget* anchor);
    [[nodiscard]] QWidget* anchor() const;

    void setPlacement(Placement placement);
    [[nodiscard]] Placement placement() const noexcept;

    // The side the surface actually ended up on after the fit test. Equal to
    // placement() unless a flip happened.
    [[nodiscard]] Placement effectivePlacement() const noexcept;

    // The custom content. Not owned by the popup; the popup only reparents it
    // into the surface. Passing nullptr removes it.
    void setContent(View* content);
    [[nodiscard]] View* content() const;

    void setArrowVisible(bool visible);
    [[nodiscard]] bool isArrowVisible() const noexcept;

    void setContentPadding(qreal dp);
    [[nodiscard]] qreal contentPadding() const noexcept;

    void setArrowSize(qreal dp);
    [[nodiscard]] qreal arrowSize() const noexcept;

    void open();
    void close();
    [[nodiscard]] bool isOpen() const noexcept;

    // Geometry in widget coordinates. bodyRect() excludes the arrow band;
    // contentArea() is bodyRect() minus the content padding.
    [[nodiscard]] QRectF bodyRect() const;
    [[nodiscard]] QRectF contentArea() const;
    [[nodiscard]] QRectF arrowRect() const;

signals:
    void opened();
    void closed();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeChildren(const QRectF& content) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void onThemeChanged() override;

private:
    void syncAppearance();
    void reposition();
    void detachFilters();
    [[nodiscard]] QSizeF desiredSize();
    [[nodiscard]] QRectF anchorRectGlobal() const;
    [[nodiscard]] QRectF bodyFor(const QRectF& full) const;
    [[nodiscard]] QRectF geometryFor(Placement placement, const QRectF& anchorRect,
                                     const QSizeF& size) const;
    [[nodiscard]] Placement choosePlacement(Placement requested, const QRectF& anchorRect,
                                            const QRect& available,
                                            const QSizeF& size) const;
    [[nodiscard]] QPainterPath arrowPath(const QRectF& full) const;

    QPointer<QWidget> anchor_;
    QPointer<View> content_;
    Placement placement_ = Placement::Below;
    Placement effective_ = Placement::Below;
    bool open_ = false;
    bool arrowVisible_ = true;
    qreal contentPaddingDp_ = 12.0;
    qreal arrowSizeDp_ = 12.0;
};

} // namespace md
