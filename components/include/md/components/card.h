// Periodicity — Card (media/content batch).
//
// An M3 surface that groups a title, an optional subtitle and body, an optional
// media Image and an optional action row. The card owns the background: the
// fill, the shape (medium, 12 dp) and the elevation come from the theme and are
// painted by View's own layers, so the tonal step and the shadow are the same
// ones every other material surface uses. Outlined adds a one-dp contour.
//
// The media Image and the actions are real child Views (the card is a
// ViewGroup), arranged inside the theme's 16 dp content padding. Actions are
// measured against their own size hints and right-aligned in a trailing row.
// Clicking the card surface emits clicked() only while interactive() is true;
// a child action keeps its own clicks and does not bubble them into the card.

#pragma once

#include <md/view/view_group.h>

#include <QList>
#include <QString>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;

namespace md {

class Image;

class Card : public ViewGroup
{
    Q_OBJECT

public:
    enum class CardVariant {
        Elevated, // surfaceContainerLow + level 1
        Filled,   // surfaceContainerHighest
        Outlined, // surface + one-dp contour
    };

    explicit Card(QWidget* parent = nullptr);

    void setVariant(CardVariant variant);
    [[nodiscard]] CardVariant variant() const noexcept;

    void setTitle(const QString& title);
    [[nodiscard]] QString title() const;

    void setSubtitle(const QString& subtitle);
    [[nodiscard]] QString subtitle() const;

    void setBody(const QString& body);
    [[nodiscard]] QString body() const;

    // The media is reparented to the card and shown above the header. The card
    // does not delete it; passing a new media, clearing it, or destroying the
    // card only detaches it from the arrangement.
    void setMedia(Image* media);
    [[nodiscard]] Image* media() const;

    // Reparents `action` to the card and appends it to the action row. The
    // caller keeps ownership and must keep the pointer alive while it is in
    // the row; destroying the card does not destroy a stack-owned action that
    // is destroyed first.
    void addAction(View* action);
    [[nodiscard]] QList<View*> actions() const;
    void clearActions();

    void setInteractive(bool interactive);
    [[nodiscard]] bool isInteractive() const noexcept;

signals:
    void clicked();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeChildren(const QRectF& content) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    // The media and action children are real widgets and Qt composites them;
    // the ViewGroup manual pass would paint a second, offset copy.
    void paintChildren(QPainter& painter) override { Q_UNUSED(painter); }
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void onThemeChanged() override;
    [[nodiscard]] bool focusable() const override;

private:
    // One resolved arrangement. `desiredHeight` is what measureOverride()
    // reports; the rectangles are what arrangeChildren() applies.
    struct CardLayout
    {
        QRectF media;
        QRectF title;
        QRectF subtitle;
        QRectF body;
        QRectF actions;
        qreal desiredHeight = 0.0;
        bool hasMedia = false;
        bool hasActions = false;
    };

    [[nodiscard]] CardLayout computeLayout(const QRectF& frame) const;
    [[nodiscard]] qreal paddingPx() const;
    [[nodiscard]] qreal actionRowHeight(qreal maxWidth) const;
    [[nodiscard]] qreal wrappedBodyHeight(qreal width) const;
    void syncAppearance();
    void relayoutNow();

    CardVariant variant_ = CardVariant::Filled;
    QString title_;
    QString subtitle_;
    QString body_;

    Image* media_ = nullptr;
    QList<View*> actions_;

    bool interactive_ = false;
    bool pressedInside_ = false;
};

} // namespace md
