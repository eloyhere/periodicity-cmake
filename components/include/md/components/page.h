// Periodicity — Page, a header / body / footer skeleton.
//
// A page is the frame an application screen is built in: a 64 dp app bar with a
// leading back affordance, a title and an optional subtitle, a trailing row of
// action Views, one body View and an optional footer View. The header and the
// footer carry their own painted content; the body is a real child inside an
// internal viewport, which is what makes scrolling clip: a child widget can only
// be clipped by an ancestor, so the body View lives in a viewport sized to
// bodyRect() and the viewport scrolls it.
//
// The title and the subtitle elide against the space the back button and the
// header actions leave, so a long title never runs under an action. The body
// scrolls with the wheel and with PageUp/PageDown/Home/End, and the offset is
// always clamped to what the content actually overflows.
//
// A theme change re-measures and re-arranges: every dimension on this page is a
// density-scaled token, so a density change invalidates all of them at once.

#pragma once

#include <md/view/view_group.h>

#include <QList>
#include <QString>

class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;
class QWheelEvent;

namespace md {

class PageViewport;

class Page : public ViewGroup
{
    Q_OBJECT

public:
    explicit Page(QWidget* parent = nullptr);
    ~Page() override;

    void setTitle(const QString& title);
    [[nodiscard]] QString title() const;

    void setSubtitle(const QString& subtitle);
    [[nodiscard]] QString subtitle() const;

    // Hiding the header gives the body the whole page.
    void setHeaderVisible(bool visible);
    [[nodiscard]] bool isHeaderVisible() const noexcept;

    // The leading back affordance. It is painted and hit-tested by the page; a
    // completed press emits backRequested().
    void setBackVisible(bool visible);
    [[nodiscard]] bool isBackVisible() const noexcept;

    // Appends a View to the trailing header row. The page reparents it and does
    // not own it: the caller must keep it alive while it is in the row.
    void addHeaderAction(View* action);
    void clearHeaderActions();
    [[nodiscard]] QList<View*> headerActions() const;

    // The body. `content` is reparented into the page's body viewport and shown;
    // a null pointer clears the body.
    void setContent(View* content);
    [[nodiscard]] View* content() const;

    // The footer is measured at its natural height and pinned to the bottom.
    void setFooter(View* footer);
    [[nodiscard]] View* footer() const;

    // Whether the body scrolls. A non-scrollable body pins the offset to zero.
    void setScrollable(bool scrollable);
    [[nodiscard]] bool isScrollable() const noexcept;

    void setScrollOffset(qreal offset);
    [[nodiscard]] qreal scrollOffset() const noexcept;

    // The natural height of the body content at the current body width. Zero
    // when there is no content.
    [[nodiscard]] qreal contentHeight() const;

    // Geometry in widget coordinates. The header and the footer are empty when
    // they are absent; the body is what remains between them. titleRect() is
    // the elided box the title is painted in, after the back button and the
    // header actions have taken their space.
    [[nodiscard]] QRectF headerRect() const;
    [[nodiscard]] QRectF bodyRect() const;
    [[nodiscard]] QRectF footerRect() const;
    [[nodiscard]] QRectF backButtonRect() const;
    [[nodiscard]] QRectF titleRect() const;

signals:
    void backRequested();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeChildren(const QRectF& content) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

    void onThemeChanged() override;
    [[nodiscard]] bool focusable() const override;

private:
    [[nodiscard]] qreal headerHeight() const;
    [[nodiscard]] qreal footerHeightForWidth(qreal width) const;
    [[nodiscard]] qreal headerActionsWidth() const;
    [[nodiscard]] QRectF subtitleRect() const;
    [[nodiscard]] qreal maxScrollOffset() const;

    void relayout();
    void syncLayout();
    void clampScroll();
    void drawBackArrow(QPainter& painter, const QRectF& button) const;

    QString title_;
    QString subtitle_;

    bool headerVisible_ = true;
    bool backVisible_ = false;
    bool scrollable_ = true;
    bool backPressed_ = false;
    bool backHovered_ = false;

    qreal scrollOffset_ = 0.0;

    PageViewport* viewport_ = nullptr;
    View* content_ = nullptr;
    View* footer_ = nullptr;
    QList<View*> actions_;
};

} // namespace md
