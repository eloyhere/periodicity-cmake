// Periodicity — StateLayout (framework.md §6.3).
//
// The container holds one child per role and shows exactly the one the state
// names. A role without a child is drawn as a placeholder, so a page is never
// blank: the state machine and the pixels agree at every moment.

#include <md/components/state.h>

#include <md/layout/alignment.h>
#include <md/theme/theme.h>

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>
#include <QRectF>

#include <algorithm>
#include <cmath>

namespace md {

namespace {

// The type style the placeholder text uses.
QFont placeholderFont(const Theme& theme)
{
    const TextStyle& style = theme.type().bodyMedium;
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(static_cast<QFont::Weight>(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
    return font;
}

QString defaultPlaceholder(StateLayout::State state)
{
    switch (state) {
    case StateLayout::State::Loading:
        return QStringLiteral("Loading");
    case StateLayout::State::Empty:
        return QStringLiteral("Nothing to show");
    case StateLayout::State::Error:
        return QStringLiteral("Something went wrong");
    case StateLayout::State::Offline:
        return QStringLiteral("You are offline");
    case StateLayout::State::Content:
        break;
    }
    return QString();
}

} // namespace

StateLayout::StateLayout(QWidget* parent)
    : ViewGroup(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setBackgroundColour(QColor(Qt::transparent));
}

StateLayout::StateLayout(State state, QWidget* parent)
    : StateLayout(parent)
{
    setState(state);
}

StateLayout::~StateLayout() = default;

void StateLayout::setState(State state)
{
    if (state_ == state)
        return;

    state_ = state;
    applyState();
    emit stateChanged(state_);
}

void StateLayout::setContentWidget(View* widget)
{
    installWidget(State::Content, widget);
}

void StateLayout::setLoadingWidget(View* widget)
{
    installWidget(State::Loading, widget);
}

void StateLayout::setEmptyWidget(View* widget)
{
    installWidget(State::Empty, widget);
}

void StateLayout::setErrorWidget(View* widget)
{
    installWidget(State::Error, widget);
}

void StateLayout::setOfflineWidget(View* widget)
{
    installWidget(State::Offline, widget);
}

View* StateLayout::contentWidget() const
{
    return widgetAt(State::Content);
}

View* StateLayout::loadingWidget() const
{
    return widgetAt(State::Loading);
}

View* StateLayout::emptyWidget() const
{
    return widgetAt(State::Empty);
}

View* StateLayout::errorWidget() const
{
    return widgetAt(State::Error);
}

View* StateLayout::offlineWidget() const
{
    return widgetAt(State::Offline);
}

void StateLayout::setPlaceholderText(State state, QString text)
{
    const int index = indexOf(state);
    if (placeholders_[index] == text)
        return;

    placeholders_[index] = std::move(text);
    if (state_ == state)
        requestUpdate();
}

QString StateLayout::placeholderText(State state) const
{
    return placeholderFor(state);
}

int StateLayout::indexOf(State state) noexcept
{
    const int index = static_cast<int>(state);
    return index >= 0 && index < 5 ? index : 0;
}

View* StateLayout::widgetAt(State state) const noexcept
{
    return widgets_[indexOf(state)];
}

QString StateLayout::placeholderFor(State state) const
{
    const QString& custom = placeholders_[indexOf(state)];
    return custom.isEmpty() ? defaultPlaceholder(state) : custom;
}

void StateLayout::installWidget(State state, View* widget)
{
    const int index = indexOf(state);

    if (widgets_[index] == widget) {
        applyState();
        return;
    }

    if (widgets_[index] != nullptr) {
        // The caller keeps ownership of the widget that is being replaced.
        removeView(widgets_[index]);
        widgets_[index] = nullptr;
    }

    if (widget != nullptr) {
        if (!views().contains(widget))
            addView(widget);

        widgets_[index] = widget;

        connect(widget, &QObject::destroyed, this, [this, widget] {
            for (View*& slot : widgets_) {
                if (slot == widget)
                    slot = nullptr;
            }
        });
    }

    applyState();
}

void StateLayout::applyState()
{
    for (int index = 0; index < 5; ++index) {
        if (widgets_[index] != nullptr)
            widgets_[index]->setVisible(index == indexOf(state_));
    }

    updateGeometry();
    requestUpdate();
    arrangeChildren(contentRect());
}

void StateLayout::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);

    qreal naturalWidth = 0.0;
    qreal naturalHeight = 0.0;

    // Every role is measured, not only the visible one, so switching states
    // does not resize the page under the user's finger.
    for (View* view : widgets_) {
        if (view == nullptr)
            continue;

        view->measure(layout_detail::capTo(width), layout_detail::capTo(height));
        naturalWidth = qMax(naturalWidth, view->measuredSize().width());
        naturalHeight = qMax(naturalHeight, view->measuredSize().height());
    }

    // A page with no widget at all still has a usable placeholder size.
    naturalWidth = qMax(naturalWidth, theme.density().dp(160.0));
    naturalHeight = qMax(naturalHeight, theme.density().dp(96.0));

    setMeasuredSize(QSizeF(width.resolve(naturalWidth), height.resolve(naturalHeight)));
}

void StateLayout::arrangeChildren(const QRectF& content)
{
    const qreal width = qMax(qreal(0.0), content.width());
    const qreal height = qMax(qreal(0.0), content.height());

    // Only the active role is visible, so "fill the container" needs no second
    // check against state_.
    for (View* view : views()) {
        if (view == nullptr || view->isHidden())
            continue;

        view->measure(MeasureSpec::exactly(width), MeasureSpec::exactly(height));
        view->arrange(QRectF(content.left(), content.top(), width, height).toAlignedRect());
    }
}

void StateLayout::paintContent(QPainter& painter, const QRectF& content)
{
    // The active child paints itself; a role without a widget is drawn here.
    paintPlaceholder(painter, content);
}

void StateLayout::paintPlaceholder(QPainter& painter, const QRectF& content) const
{
    if (state_ == State::Content || widgetAt(state_) != nullptr)
        return;

    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();

    QColor accent = colours.outline;
    switch (state_) {
    case State::Loading:
        accent = colours.primary;
        break;
    case State::Error:
        accent = colours.error;
        break;
    case State::Offline:
        accent = colours.tertiary;
        break;
    case State::Empty:
    case State::Content:
        break;
    }

    const qreal iconSize = theme.density().dp(32.0);
    const qreal gap = theme.density().dp(12.0);
    const qreal margin = theme.density().dp(8.0);

    const QFont font = placeholderFont(theme);
    const QFontMetricsF metrics(font);
    const QString text = placeholderFor(state_);

    const QRectF textBounds =
        metrics.boundingRect(QRectF(0.0, 0.0, qMax(qreal(0.0), content.width() - 2.0 * margin),
                                    1.0e6),
                             Qt::AlignHCenter | Qt::TextWordWrap, text);
    const qreal blockHeight = iconSize + gap + textBounds.height();
    const qreal top = content.top() + qMax(qreal(0.0), (content.height() - blockHeight) / 2.0);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    // A ring stands in for the state's indicator; it is deliberately not an
    // animation, so a state page never spins a timer it cannot stop.
    const QPointF centre(content.center().x(), top + iconSize / 2.0);
    const qreal radius = qMax(qreal(1.0), iconSize / 2.0 - 1.5);
    painter.setPen(QPen(accent, 3.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(centre, radius, radius);

    painter.setFont(font);
    painter.setPen(colours.onSurfaceVariant);
    painter.drawText(QRectF(content.left() + margin, top + iconSize + gap,
                            qMax(qreal(0.0), content.width() - 2.0 * margin),
                            textBounds.height() + 2.0),
                     Qt::AlignHCenter | Qt::AlignTop, text);

    painter.restore();
}

} // namespace md
