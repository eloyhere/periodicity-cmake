// Periodicity — the Anchor component (framework.md §5.9).
//
// The list is painted as one rounded row per entry: a tonal pill plus a primary
// bar on the leading edge for the active one, the label tinted to match. Both
// the pill and the bar are derived from anchorRect(), the same rectangle the
// pointer path uses, so the highlight and the hit target cannot drift.
//
// Scrolling and activation are kept apart on purpose. setScrollOffset() walks
// the entries and moves the highlight to the last one whose offset the page has
// reached — that is a report, not a command — while a click or Enter emits
// anchorActivated() so a scroll view can jump. A caller that connects both
// directions therefore converges instead of echoing.

#include <md/components/anchor.h>

#include <md/theme/theme.h>

#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>

#include <algorithm>

namespace md {
namespace {

constexpr qreal kRowHeightDp = 40.0;
constexpr qreal kPaddingDp = 8.0;
constexpr qreal kTextIndentDp = 16.0;
constexpr qreal kBarWidthDp = 3.0;
constexpr qreal kBarInsetDp = 8.0;
constexpr qreal kRowInsetDp = 2.0;

[[nodiscard]] QFont fontFor(const Theme& theme, const TextStyle& style)
{
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(QFont::Weight(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
    return font;
}

} // namespace

Anchor::Anchor(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setShape(Shape::large());
    setAccessibleName(tr("Anchors"));
}

int Anchor::anchorCount() const
{
    return static_cast<int>(anchors_.size());
}

qreal Anchor::rowHeight() const
{
    return Theme::forWidget(this).density().dp(kRowHeightDp);
}

int Anchor::indexOfAnchor(const QString& key) const
{
    for (int index = 0; index < anchorCount(); ++index) {
        if (anchors_.at(index).key == key)
            return index;
    }

    return -1;
}

AnchorItem Anchor::anchorAt(int index) const
{
    if (index < 0 || index >= anchorCount())
        return AnchorItem{};

    return anchors_.at(index);
}

QString Anchor::activeAnchor() const
{
    if (active_ < 0 || active_ >= anchorCount())
        return QString();

    return anchors_.at(active_).key;
}

QRectF Anchor::anchorRect(int index) const
{
    if (index < 0 || index >= anchorCount())
        return QRectF();

    const QRectF area = contentRect();
    const Density& density = Theme::forWidget(this).density();
    const qreal top = area.top() + density.dp(kPaddingDp) + qreal(index) * rowHeight();

    return QRectF(area.left(), top, std::max<qreal>(0.0, area.width()),
                  std::max<qreal>(0.0, rowHeight()));
}

QRectF Anchor::indicatorRect() const
{
    const QRectF row = anchorRect(active_);
    if (row.isEmpty())
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const qreal inset = density.dp(kBarInsetDp);
    const qreal height = std::max<qreal>(0.0, row.height() - 2.0 * inset);

    return QRectF(row.left() + density.dp(kRowInsetDp), row.top() + inset,
                  density.dp(kBarWidthDp), height);
}

int Anchor::anchorIndexAt(const QPointF& point) const
{
    for (int index = 0; index < anchorCount(); ++index) {
        if (anchorRect(index).contains(point))
            return index;
    }

    return -1;
}

int Anchor::indexForOffset(qreal offset) const
{
    if (anchors_.isEmpty())
        return -1;

    int best = 0;
    for (int index = 0; index < anchorCount(); ++index) {
        if (anchors_.at(index).offset <= offset + qreal(0.001))
            best = index;
    }

    return best;
}

void Anchor::setActiveIndex(int index)
{
    if (anchors_.isEmpty())
        return;

    const int next = std::clamp(index, 0, anchorCount() - 1);
    if (next == active_)
        return;

    active_ = next;
    requestUpdate();
    emit activeAnchorChanged(activeAnchor());
}

void Anchor::activate(int index)
{
    if (index < 0 || index >= anchorCount())
        return;

    setActiveIndex(index);
    const AnchorItem item = anchors_.at(index);
    emit anchorActivated(item.key, item.offset);
}

void Anchor::clampActive()
{
    const int previous = active_;

    if (anchors_.isEmpty())
        active_ = -1;
    else
        active_ = std::clamp(active_ < 0 ? 0 : active_, 0, anchorCount() - 1);

    requestUpdate();

    if (active_ == previous)
        return;

    emit activeAnchorChanged(active_ >= 0 ? activeAnchor() : QString());
}

void Anchor::setAnchors(const QList<AnchorItem>& anchors)
{
    anchors_ = anchors;
    clampActive();
}

void Anchor::addAnchor(const AnchorItem& anchor)
{
    addAnchor(anchor.key, anchor.label, anchor.offset);
}

void Anchor::addAnchor(const QString& key, const QString& label, qreal offset)
{
    if (key.isEmpty())
        return;

    const int existing = indexOfAnchor(key);
    if (existing >= 0) {
        anchors_[existing].label = label;
        anchors_[existing].offset = offset;
        requestUpdate();
        return;
    }

    anchors_.append(AnchorItem{key, label, offset});

    if (active_ < 0) {
        active_ = 0;
        requestUpdate();
        emit activeAnchorChanged(activeAnchor());
        return;
    }

    requestUpdate();
}

void Anchor::insertAnchor(int index, const AnchorItem& anchor)
{
    if (anchor.key.isEmpty())
        return;

    const int existing = indexOfAnchor(anchor.key);
    if (existing >= 0) {
        anchors_[existing] = anchor;
        requestUpdate();
        return;
    }

    const int position = std::clamp(index, 0, anchorCount());
    anchors_.insert(position, anchor);

    if (active_ >= position)
        ++active_;

    clampActive();
}

void Anchor::removeAnchor(const QString& key)
{
    const int index = indexOfAnchor(key);
    if (index < 0)
        return;

    anchors_.removeAt(index);

    if (active_ > index)
        --active_;
    else if (active_ == index)
        active_ = std::max(0, index - 1);

    clampActive();
}

void Anchor::clearAnchors()
{
    if (anchors_.isEmpty())
        return;

    anchors_.clear();
    active_ = -1;
    requestUpdate();
    emit activeAnchorChanged(QString());
}

void Anchor::setActiveAnchor(const QString& key)
{
    const int index = indexOfAnchor(key);
    if (index < 0)
        return;

    setActiveIndex(index);
}

void Anchor::setScrollOffset(qreal offset)
{
    if (qFuzzyCompare(offset + 1.0, scrollOffset_ + 1.0))
        return;

    scrollOffset_ = offset;
    if (anchors_.isEmpty())
        return;

    const int next = indexForOffset(scrollOffset_);
    if (next == active_ || next < 0)
        return;

    active_ = next;
    requestUpdate();
    emit activeAnchorChanged(activeAnchor());
}

void Anchor::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    const qreal rows = qreal(anchorCount()) * rowHeight() + 2.0 * density.dp(kPaddingDp);

    setMeasuredSize(QSizeF(width.resolve(density.dp(224.0)), height.resolve(rows)));
}

void Anchor::paintContent(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);

    if (anchors_.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();
    const Density& density = theme.density();
    const qreal unit = density.dp(1.0);
    const qreal baseOpacity = isEnabled() ? 1.0 : 0.38;
    const QFont font = fontFor(theme, theme.type().labelLarge);
    const QFontMetricsF metrics(font);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setOpacity(baseOpacity);
    painter.setFont(font);

    for (int index = 0; index < anchorCount(); ++index) {
        const QRectF row = anchorRect(index);
        if (row.isEmpty())
            continue;

        const bool active = index == active_;
        const QRectF pill = row.adjusted(density.dp(kRowInsetDp), density.dp(kRowInsetDp),
                                         -density.dp(kRowInsetDp), -density.dp(kRowInsetDp));
        const qreal radius = density.dp(8.0);

        if (active) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colours.secondaryContainer);
            painter.drawRoundedRect(pill, radius, radius);
        } else if (index % 2 == 1) {
            QColor stripe = colours.surfaceContainerLow;
            stripe.setAlphaF(0.5f);
            painter.setPen(Qt::NoPen);
            painter.setBrush(stripe);
            painter.drawRoundedRect(pill, radius, radius);
        }

        painter.setPen(active ? colours.onSecondaryContainer : colours.onSurfaceVariant);
        const QRectF text = pill.adjusted(density.dp(kTextIndentDp), 0.0, -density.dp(8.0), 0.0);
        painter.drawText(text, Qt::AlignLeft | Qt::AlignVCenter,
                         metrics.elidedText(anchors_.at(index).label, Qt::ElideRight,
                                            text.width()));
    }

    const QRectF bar = indicatorRect();
    if (!bar.isEmpty()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colours.primary);
        painter.drawRoundedRect(bar, unit * kBarWidthDp / 2.0, unit * kBarWidthDp / 2.0);
    }

    painter.restore();
}

void Anchor::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        pressed_ = event->position();

    View::mousePressEvent(event);
}

void Anchor::mouseReleaseEvent(QMouseEvent* event)
{
    const QPointF press = pressed_;
    pressed_ = QPointF(-1.0, -1.0);
    View::mouseReleaseEvent(event);

    if (!isEnabled() || event->button() != Qt::LeftButton || press.x() < 0.0)
        return;

    const int pressedIndex = anchorIndexAt(press);
    if (pressedIndex < 0)
        return;

    if (anchorIndexAt(event->position()) == pressedIndex)
        activate(pressedIndex);
}

void Anchor::keyPressEvent(QKeyEvent* event)
{
    if (!isEnabled()) {
        View::keyPressEvent(event);
        return;
    }

    switch (event->key()) {
    case Qt::Key_Up:
        setActiveIndex(active_ < 0 ? 0 : active_ - 1);
        break;
    case Qt::Key_Down:
        setActiveIndex(active_ < 0 ? 0 : active_ + 1);
        break;
    case Qt::Key_Home:
        setActiveIndex(0);
        break;
    case Qt::Key_End:
        setActiveIndex(anchorCount() - 1);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        activate(active_);
        break;
    default:
        View::keyPressEvent(event);
        return;
    }

    event->accept();
}

} // namespace md
