// Periodicity — GuideView, a guided-tour overlay.
//
// The overlay covers its parent widget (normally the window), dims everything
// with the theme's scrim role, punches a hole around the current target and
// draws a bubble with a title, a description, a step counter and three
// actions: previous, next and skip.
//
// The hole is a path subtraction (full rectangle minus the target shape), not
// a composition-mode clear: clearing pixels in a child widget would depend on
// the parent's backing store being translucent, which it generally is not.
//
// Targets are followed, not sampled once: the overlay installs an event filter
// on every target and on the parent window, so a target that moves or resizes
// while the tour is running is re-highlighted and the bubble is re-positioned.
// A target destroyed mid-tour simply yields an empty highlight and a centred
// bubble.

#pragma once

#include <md/view/view_group.h>

#include <QList>
#include <QPointer>
#include <QRectF>
#include <QString>

class QEvent;
class QPainter;
class QPainterPath;
class QResizeEvent;
class QWidget;

namespace md {

class Button;

struct GuideStep
{
    enum class Highlight {
        RoundedRect,
        Circle,
    };

    QPointer<QWidget> target;
    QString title;
    QString description;
    Highlight highlight = Highlight::RoundedRect;

    // How far the highlight extends beyond the target, in dp.
    qreal paddingDp = 8.0;

    GuideStep() = default;
    GuideStep(QWidget* target, QString title, QString description);
};

class GuideView : public ViewGroup
{
    Q_OBJECT

public:
    explicit GuideView(QWidget* parent = nullptr);

    void setSteps(const QList<GuideStep>& steps);
    [[nodiscard]] QList<GuideStep> steps() const;
    [[nodiscard]] int stepCount() const;
    [[nodiscard]] int currentStep() const noexcept;

    // Moves to `index` (clamped) and emits stepChanged() when it changes.
    void setCurrentStep(int index);

    void setScrimOpacity(qreal opacity);
    [[nodiscard]] qreal scrimOpacity() const noexcept;

    void setSkipVisible(bool visible);
    [[nodiscard]] bool isSkipVisible() const;

    void setLabels(const QString& previous, const QString& next, const QString& skip);
    [[nodiscard]] QString previousLabel() const;
    [[nodiscard]] QString nextLabel() const;
    [[nodiscard]] QString skipLabel() const;

    void setBubbleWidth(qreal dp);
    [[nodiscard]] qreal bubbleWidth() const noexcept;

    // Shows the overlay over the parent widget and starts at the first step.
    void start();
    void next();
    void previous();
    void skip();
    void finish();
    [[nodiscard]] bool isActive() const noexcept;

    // Both rectangles are in the overlay's coordinates. An empty highlight
    // means the current step has no target (or it was destroyed).
    [[nodiscard]] QRectF highlightRect() const;
    [[nodiscard]] QRectF bubbleRect() const;

    [[nodiscard]] Button* previousButton() const;
    [[nodiscard]] Button* nextButton() const;
    [[nodiscard]] Button* skipButton() const;

    // Recomputes the highlight and the bubble from the current target. Called
    // automatically on target move/resize; public so a caller can force it.
    void reposition();

signals:
    void stepChanged(int index);
    void finished();
    void skipped();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeChildren(const QRectF& content) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void onThemeChanged() override;

private:
    void syncButtons();
    void installTargetFilters();
    void removeTargetFilters();
    void updateHighlight();
    void stopTour();
    [[nodiscard]] const GuideStep* currentStepData() const;
    [[nodiscard]] qreal descriptionHeight(qreal width) const;

    QList<GuideStep> steps_;
    int index_ = 0;
    bool active_ = false;

    qreal scrimOpacity_ = 0.6;
    qreal bubbleWidthDp_ = 320.0;
    bool skipVisible_ = true;

    QString previousLabel_ = QStringLiteral("Back");
    QString nextLabel_ = QStringLiteral("Next");
    QString skipLabel_ = QStringLiteral("Skip");

    QRectF highlight_;
    QRectF bubble_;

    Button* previousButton_ = nullptr;
    Button* nextButton_ = nullptr;
    Button* skipButton_ = nullptr;

    QList<QPointer<QWidget>> filteredTargets_;
};

} // namespace md
