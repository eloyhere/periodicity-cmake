// Periodicity — the Cascader component (framework.md §5.9).
//
// The hierarchy is stored once (levels_) and the selection is a list of
// indices into it, so "what is selected" and "which columns exist" cannot
// disagree: the visible column count is derived from the selection, and
// selectItem builds the new index list from the clicked column with
// indices_.mid-style truncation. That single rule is what clears the deeper
// choices when a parent changes.

#include <md/components/cascader.h>

#include <md/theme/theme.h>

#include <QFont>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace md {
namespace {

constexpr qreal kPaddingDp = 8.0;
constexpr qreal kColumnInsetDp = 2.0;
constexpr qreal kItemInsetDp = 4.0;
constexpr qreal kItemHeightDp = 44.0;

[[nodiscard]] QFont fontFor(const Theme& theme, const TextStyle& style)
{
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(QFont::Weight(style.weight));
    return font;
}

} // namespace

Cascader::Cascader(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setShape(Shape::medium());
    setAccessibleName(tr("Choose from a hierarchy"));
}

void Cascader::setLevels(const QList<CascaderNode>& levels)
{
    levels_ = levels;
    indices_.clear();
    activeColumn_ = 0;
    cursorIndex_ = 0;
    requestUpdate();
}

const QList<CascaderNode>* Cascader::nodesAt(int column) const
{
    if (column < 0)
        return nullptr;

    const QList<CascaderNode>* nodes = &levels_;
    for (int level = 0; level < column; ++level) {
        if (level >= static_cast<int>(indices_.size()))
            return nullptr;

        const int index = indices_.at(level);
        if (index < 0 || index >= static_cast<int>(nodes->size()))
            return nullptr;

        nodes = &nodes->at(index).children;
    }

    return nodes;
}

const CascaderNode* Cascader::nodeAt(int column, int index) const
{
    const QList<CascaderNode>* nodes = nodesAt(column);
    if (nodes == nullptr || index < 0 || index >= static_cast<int>(nodes->size()))
        return nullptr;

    return &nodes->at(index);
}

int Cascader::columnCount() const
{
    if (levels_.isEmpty() || indices_.isEmpty())
        return 1;

    const int lastColumn = static_cast<int>(indices_.size()) - 1;
    const CascaderNode* last = nodeAt(lastColumn, indices_.last());
    if (last == nullptr)
        return 1;

    return static_cast<int>(indices_.size()) + (last->children.isEmpty() ? 0 : 1);
}

int Cascader::itemCount(int column) const
{
    const QList<CascaderNode>* nodes = nodesAt(column);
    return nodes == nullptr ? 0 : static_cast<int>(nodes->size());
}

QRectF Cascader::contentArea() const
{
    const qreal pad = Theme::forWidget(this).density().dp(kPaddingDp);
    return QRectF(pad, pad, std::max<qreal>(0.0, qreal(width()) - 2.0 * pad),
                  std::max<qreal>(0.0, qreal(height()) - 2.0 * pad));
}

QRectF Cascader::columnRect(int column) const
{
    const int columns = std::max(1, columnCount());
    if (column < 0 || column >= columns)
        return QRectF();

    const QRectF area = contentArea();
    const qreal columnWidth = area.width() / columns;
    return QRectF(area.left() + column * columnWidth, area.top(),
                  std::max<qreal>(0.0, columnWidth), area.height());
}

QRectF Cascader::itemRect(int column, int index) const
{
    const QRectF area = columnRect(column);
    const int count = itemCount(column);
    if (area.isEmpty() || count <= 0 || index < 0 || index >= count)
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const qreal inset = density.dp(kItemInsetDp);
    const qreal available = std::max<qreal>(0.0, area.height() - 2.0 * inset);
    const qreal itemHeight = std::min(density.dp(kItemHeightDp), available / count);

    return QRectF(area.left() + inset, area.top() + inset + index * itemHeight,
                  std::max<qreal>(0.0, area.width() - 2.0 * inset),
                  std::max<qreal>(0.0, itemHeight));
}

QStringList Cascader::currentPath() const
{
    QStringList path;
    const QList<CascaderNode>* nodes = &levels_;

    for (int level = 0; level < static_cast<int>(indices_.size()); ++level) {
        const int index = indices_.at(level);
        if (index < 0 || index >= static_cast<int>(nodes->size()))
            break;

        path.append(nodes->at(index).label);
        nodes = &nodes->at(index).children;
    }

    return path;
}

QVariantList Cascader::currentValues() const
{
    QVariantList values;
    const QList<CascaderNode>* nodes = &levels_;

    for (int level = 0; level < static_cast<int>(indices_.size()); ++level) {
        const int index = indices_.at(level);
        if (index < 0 || index >= static_cast<int>(nodes->size()))
            break;

        values.append(nodes->at(index).value);
        nodes = &nodes->at(index).children;
    }

    return values;
}

QVariant Cascader::currentValue() const
{
    const QList<CascaderNode>* nodes = &levels_;
    const CascaderNode* last = nullptr;

    for (int level = 0; level < static_cast<int>(indices_.size()); ++level) {
        const int index = indices_.at(level);
        if (index < 0 || index >= static_cast<int>(nodes->size())) {
            last = nullptr;
            break;
        }

        last = &nodes->at(index);
        nodes = &last->children;
    }

    return last == nullptr ? QVariant() : last->value;
}

bool Cascader::hasCompletePath() const
{
    if (indices_.isEmpty())
        return false;

    const int column = static_cast<int>(indices_.size()) - 1;
    const CascaderNode* node = nodeAt(column, indices_.last());
    return node != nullptr && node->children.isEmpty();
}

bool Cascader::selectItem(int column, int index)
{
    if (column < 0 || column >= columnCount())
        return false;

    const CascaderNode* node = nodeAt(column, index);
    if (node == nullptr)
        return false;

    QList<int> next;
    for (int level = 0; level < column; ++level)
        next.append(indices_.value(level));
    next.append(index);

    const bool changed = next != indices_;
    if (changed) {
        indices_ = next;
        emit pathChanged(currentPath());

        if (node->children.isEmpty())
            emit leafActivated(node->value);
    }

    if (node->children.isEmpty()) {
        activeColumn_ = column;
        cursorIndex_ = index;
    } else {
        activeColumn_ = std::min(column + 1, std::max(0, columnCount() - 1));
        cursorIndex_ = 0;
    }

    requestUpdate();
    return true;
}

void Cascader::clearSelection()
{
    if (indices_.isEmpty()) {
        requestUpdate();
        return;
    }

    indices_.clear();
    activeColumn_ = 0;
    cursorIndex_ = 0;
    requestUpdate();
    emit pathChanged(QStringList());
}

void Cascader::moveActiveColumn(int delta)
{
    const int columns = std::max(1, columnCount());
    const int target = std::clamp(activeColumn_ + delta, 0, columns - 1);
    if (target == activeColumn_)
        return;

    activeColumn_ = target;
    const int selected = indices_.value(target, -1);
    cursorIndex_ = selected >= 0 ? selected : 0;

    const int count = itemCount(target);
    cursorIndex_ = count > 0 ? std::clamp(cursorIndex_, 0, count - 1) : 0;
    requestUpdate();
}

void Cascader::moveCursor(int delta)
{
    const int count = itemCount(activeColumn_);
    if (count <= 0)
        return;

    cursorIndex_ = (cursorIndex_ + delta + count) % count;
    requestUpdate();
}

void Cascader::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    setMeasuredSize(QSizeF(width.resolve(density.dp(360.0)),
                           height.resolve(density.dp(240.0))));
}

void Cascader::paintContent(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);

    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();
    const qreal unit = theme.density().dp(1.0);
    const qreal baseOpacity = isEnabled() ? 1.0 : 0.38;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setOpacity(baseOpacity);

    const int columns = columnCount();
    const QFont itemFont = fontFor(theme, theme.type().bodyMedium);
    const QFont arrowFont = fontFor(theme, theme.type().labelSmall);

    for (int column = 0; column < columns; ++column) {
        const QRectF area = columnRect(column);
        if (area.isEmpty())
            continue;

        const QRectF card = area.adjusted(unit * kColumnInsetDp, unit * kColumnInsetDp,
                                          -unit * kColumnInsetDp, -unit * kColumnInsetDp);
        if (card.isEmpty())
            continue;

        painter.setPen(Qt::NoPen);
        painter.setBrush(colours.surfaceContainerLow);
        painter.drawRoundedRect(card, unit * 8.0, unit * 8.0);

        if (column > 0) {
            painter.setPen(QPen(colours.outlineVariant, unit));
            painter.drawLine(QPointF(area.left(), card.top() + unit * 8.0),
                             QPointF(area.left(), card.bottom() - unit * 8.0));
        }

        const int count = itemCount(column);
        for (int index = 0; index < count; ++index) {
            const CascaderNode* node = nodeAt(column, index);
            const QRectF item = itemRect(column, index);
            if (node == nullptr || item.isEmpty())
                continue;

            const bool selected = indices_.value(column, -1) == index;
            const bool cursor = hasFocus() && column == activeColumn_ && index == cursorIndex_;

            if (selected) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(colours.primaryContainer);
                painter.drawRoundedRect(item, unit * 8.0, unit * 8.0);
            } else if (cursor) {
                painter.setPen(QPen(colours.primary, unit * 1.5));
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(item.adjusted(unit, unit, -unit, -unit),
                                        unit * 7.0, unit * 7.0);
            }

            painter.setFont(itemFont);
            painter.setPen(selected ? colours.onPrimaryContainer : colours.onSurface);
            const qreal trailing = node->children.isEmpty() ? unit * 12.0 : unit * 30.0;
            painter.drawText(item.adjusted(unit * 12.0, 0.0, -trailing, 0.0),
                             Qt::AlignLeft | Qt::AlignVCenter, node->label);

            if (!node->children.isEmpty()) {
                painter.setFont(arrowFont);
                painter.setPen(QPen(selected ? colours.onPrimaryContainer
                                             : colours.onSurfaceVariant,
                                    unit * 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                const QPointF centre(item.right() - unit * 14.0, item.center().y());
                QPolygonF chevron;
                chevron << QPointF(centre.x() - unit * 2.0, centre.y() - unit * 4.0)
                        << QPointF(centre.x() + unit * 2.0, centre.y())
                        << QPointF(centre.x() - unit * 2.0, centre.y() + unit * 4.0);
                painter.drawPolyline(chevron);
            }
        }
    }

    painter.restore();
}

void Cascader::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        pressed_ = event->position();

    View::mousePressEvent(event);
}

void Cascader::mouseReleaseEvent(QMouseEvent* event)
{
    const QPointF press = pressed_;
    pressed_ = QPointF(-1.0, -1.0);
    View::mouseReleaseEvent(event);

    if (!isEnabled() || event->button() != Qt::LeftButton || press.x() < 0.0)
        return;

    const QPointF release = event->position();
    const int columns = columnCount();
    for (int column = 0; column < columns; ++column) {
        const int count = itemCount(column);
        for (int index = 0; index < count; ++index) {
            const QRectF rect = itemRect(column, index);
            if (!rect.isEmpty() && rect.contains(press) && rect.contains(release)) {
                selectItem(column, index);
                return;
            }
        }
    }
}

void Cascader::keyPressEvent(QKeyEvent* event)
{
    if (!isEnabled()) {
        View::keyPressEvent(event);
        return;
    }

    switch (event->key()) {
    case Qt::Key_Left:
        moveActiveColumn(-1);
        event->accept();
        return;
    case Qt::Key_Right:
        moveActiveColumn(1);
        event->accept();
        return;
    case Qt::Key_Up:
        moveCursor(-1);
        event->accept();
        return;
    case Qt::Key_Down:
        moveCursor(1);
        event->accept();
        return;
    case Qt::Key_Home:
        if (itemCount(activeColumn_) > 0) {
            cursorIndex_ = 0;
            requestUpdate();
        }
        event->accept();
        return;
    case Qt::Key_End:
        if (const int count = itemCount(activeColumn_); count > 0) {
            cursorIndex_ = count - 1;
            requestUpdate();
        }
        event->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        selectItem(activeColumn_, cursorIndex_);
        event->accept();
        return;
    default:
        View::keyPressEvent(event);
        return;
    }
}

} // namespace md
