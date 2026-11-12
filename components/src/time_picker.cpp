// Periodicity — the TimePicker component (framework.md §5.9).
//
// Each column is one cyclic wheel in index space: columnStates_[i].offset is a
// continuous position where a whole number means "settled on that row". A
// wheel notch or a key moves the offset by one whole unit immediately; a drag
// moves it continuously and, on release, either snaps it to the row under the
// pointer or animates it to the nearest row. Painting samples the wheel around
// the offset, so the same three numbers — offset, row height and the band
// rectangle — describe the pixels and the hit targets.
//
// The committed value lives in a QTime and is written only when a column
// settles, which keeps timeChanged() from firing once per pixel of a drag.

#include <md/components/time_picker.h>

#include <md/theme/theme.h>

#include <QFont>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace md {
namespace {

constexpr qreal kPaddingDp = 8.0;
constexpr qreal kRowHeightDp = 44.0;
constexpr qreal kBandRadiusDp = 12.0;
constexpr qreal kMeridianWeight = 0.7;
constexpr qreal kDragThresholdDp = 4.0;
constexpr int kPageRows = 5;
constexpr int kAnimationIntervalMs = 16;
constexpr qreal kAnimationFraction = 0.35;

[[nodiscard]] QFont fontFor(const Theme& theme, const TextStyle& style)
{
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(QFont::Weight(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
    return font;
}

} // namespace

TimePicker::TimePicker(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    // The M3 time picker container is the extra-large shape (28 dp).
    setShape(Shape::extraLarge());
    setAccessibleName(tr("Choose time"));

    animator_ = new QTimer(this);
    animator_->setInterval(kAnimationIntervalMs);
    connect(animator_, &QTimer::timeout, this, [this] { tickAnimation(); });

    refreshColumns();
}

int TimePicker::valueCount(Field field) const
{
    switch (field) {
    case Field::Hour:
        return format_ == HourFormat::Hour12 ? 12 : 24;
    case Field::Meridian:
        return 2;
    case Field::Minute:
    case Field::Second:
        return 60;
    }

    return 0;
}

int TimePicker::valueForField(Field field) const
{
    switch (field) {
    case Field::Hour:
        return time_.hour();
    case Field::Meridian:
        return time_.hour() >= 12 ? 1 : 0;
    case Field::Minute:
        return time_.minute();
    case Field::Second:
        return time_.second();
    }

    return 0;
}

int TimePicker::indexOfValue(Field field, int value) const
{
    switch (field) {
    case Field::Hour:
        if (format_ == HourFormat::Hour12)
            return value % 12 == 0 ? 11 : (value % 12) - 1;
        return value % 24;
    case Field::Meridian:
        // The meridian value is already 0 (AM) or 1 (PM).
        return value == 1 ? 1 : 0;
    case Field::Minute:
    case Field::Second:
        return value % 60;
    }

    return 0;
}

int TimePicker::valueOfIndex(Field field, int index) const
{
    const int count = valueCount(field);
    if (count <= 0)
        return 0;

    int slot = index % count;
    if (slot < 0)
        slot += count;

    switch (field) {
    case Field::Hour:
        if (format_ == HourFormat::Hour12)
            return slot == 11 ? 12 : slot + 1;
        return slot;
    case Field::Meridian:
    case Field::Minute:
    case Field::Second:
        return slot;
    }

    return slot;
}

QString TimePicker::textOfIndex(Field field, int index) const
{
    if (field == Field::Meridian)
        return valueOfIndex(field, index) == 1
            ? localized(QStringLiteral("time.pm"), QStringLiteral("PM"))
            : localized(QStringLiteral("time.am"), QStringLiteral("AM"));

    return QStringLiteral("%1").arg(valueOfIndex(field, index), 2, 10, QLatin1Char('0'));
}

bool TimePicker::meridianVisible() const noexcept
{
    return format_ == HourFormat::Hour12;
}

int TimePicker::columnCount() const noexcept
{
    return columnCount_;
}

int TimePicker::fieldColumn(Field field) const
{
    for (int index = 0; index < columnCount_; ++index) {
        if (columns_[index] == field)
            return index;
    }

    return -1;
}

void TimePicker::refreshColumns()
{
    // A wheel that is mid-snap is described by the old column layout; keeping
    // the timer alive after the columns (or their ranges) changed would make it
    // animate stale numbers against a target that no longer exists.
    if (animator_ != nullptr)
        animator_->stop();

    std::array<Field, 4> columns{Field::Hour, Field::Minute, Field::Second, Field::Second};
    int count = 0;
    columns[count++] = Field::Hour;
    if (format_ == HourFormat::Hour12)
        columns[count++] = Field::Meridian;
    columns[count++] = Field::Minute;
    if (secondsVisible_)
        columns[count++] = Field::Second;

    columns_ = columns;
    columnCount_ = count;

    for (int index = 0; index < columnCount_; ++index) {
        const int row = indexOfValue(columns_[index], valueForField(columns_[index]));
        columnStates_[index] = ColumnState{qreal(row), qreal(row)};
    }

    if (currentColumn_ >= columnCount_)
        currentColumn_ = std::max(0, columnCount_ - 1);
}

void TimePicker::setTime(const QTime& time)
{
    if (!time.isValid() || time == time_)
        return;

    time_ = time;
    refreshColumns();
    requestUpdate();
    emit timeChanged(time_);
}

void TimePicker::setHourFormat(HourFormat format)
{
    if (format_ == format)
        return;

    format_ = format;
    refreshColumns();
    requestUpdate();
    emit hourFormatChanged(format_);
}

void TimePicker::setHourFormat(int format)
{
    if (format == 12)
        setHourFormat(HourFormat::Hour12);
    else if (format == 24)
        setHourFormat(HourFormat::Hour24);
}

void TimePicker::setSecondVisible(bool visible)
{
    if (secondsVisible_ == visible)
        return;

    secondsVisible_ = visible;
    refreshColumns();
    requestUpdate();
}

void TimePicker::setCurrentColumn(int column)
{
    const int next = std::clamp(column, 0, std::max(0, columnCount_ - 1));
    if (next == currentColumn_)
        return;

    currentColumn_ = next;
    requestUpdate();
    emit currentColumnChanged(currentColumn_);
}

qreal TimePicker::rowHeight() const
{
    return Theme::forWidget(this).density().dp(kRowHeightDp);
}

QRectF TimePicker::wheelRect() const
{
    const qreal pad = Theme::forWidget(this).density().dp(kPaddingDp);
    return QRectF(pad, pad, std::max<qreal>(0.0, qreal(width()) - 2.0 * pad),
                  std::max<qreal>(0.0, qreal(height()) - 2.0 * pad));
}

qreal TimePicker::columnWidth(int column) const
{
    if (column < 0 || column >= columnCount_)
        return 0.0;

    qreal total = 0.0;
    for (int index = 0; index < columnCount_; ++index)
        total += columns_[index] == Field::Meridian ? kMeridianWeight : 1.0;

    if (total <= 0.0)
        return 0.0;

    const qreal weight = columns_[column] == Field::Meridian ? kMeridianWeight : 1.0;
    return wheelRect().width() * weight / total;
}

QRectF TimePicker::columnRect(int column) const
{
    if (column < 0 || column >= columnCount_)
        return QRectF();

    const QRectF wheel = wheelRect();
    qreal left = wheel.left();
    for (int index = 0; index < column; ++index)
        left += columnWidth(index);

    return QRectF(left, wheel.top(), std::max<qreal>(0.0, columnWidth(column)),
                  std::max<qreal>(0.0, wheel.height()));
}

QRectF TimePicker::selectionBandRect() const
{
    const QRectF wheel = wheelRect();
    const qreal height = std::min(rowHeight(), wheel.height());
    return QRectF(wheel.left(), wheel.center().y() - height / 2.0,
                  std::max<qreal>(0.0, wheel.width()), std::max<qreal>(0.0, height));
}

QRectF TimePicker::rowRect(int column, int rowOffset) const
{
    const QRectF area = columnRect(column);
    if (area.isEmpty())
        return QRectF();

    const qreal height = rowHeight();
    const qreal centre = wheelRect().center().y() + rowOffset * height;
    return QRectF(area.left(), centre - height / 2.0, area.width(), height);
}

qreal TimePicker::normalized(qreal offset, int count) const
{
    if (count <= 0)
        return 0.0;

    qreal result = std::fmod(offset, qreal(count));
    if (result < 0.0)
        result += qreal(count);

    return result;
}

qreal TimePicker::columnOffset(int column) const
{
    if (column < 0 || column >= columnCount_)
        return 0.0;

    return columnStates_[column].offset;
}

QString TimePicker::columnText(int column) const
{
    if (column < 0 || column >= columnCount_)
        return QString();

    const int count = valueCount(columns_[column]);
    const qreal offset = columnStates_[column].offset;
    int row = static_cast<int>(std::lround(offset));
    row %= count;
    if (row < 0)
        row += count;

    return textOfIndex(columns_[column], row);
}

int TimePicker::columnValue(int column) const
{
    if (column < 0 || column >= columnCount_)
        return 0;

    const int count = valueCount(columns_[column]);
    int row = static_cast<int>(std::lround(columnStates_[column].offset));
    row %= count;
    if (row < 0)
        row += count;

    return valueOfIndex(columns_[column], row);
}

void TimePicker::commitColumn(int column, int index)
{
    if (column < 0 || column >= columnCount_)
        return;

    const Field field = columns_[column];
    const int value = valueOfIndex(field, index);

    int hour = time_.hour();
    int minute = time_.minute();
    int second = time_.second();

    switch (field) {
    case Field::Hour:
        if (format_ == HourFormat::Hour12) {
            const int base = value % 12;
            hour = time_.hour() >= 12 ? base + 12 : base;
        } else {
            hour = value;
        }
        break;
    case Field::Meridian:
        hour = (time_.hour() % 12) + (value == 1 ? 12 : 0);
        break;
    case Field::Minute:
        minute = value;
        break;
    case Field::Second:
        second = value;
        break;
    }

    const QTime next(hour, minute, second, time_.msec());
    if (!next.isValid() || next == time_)
        return;

    time_ = next;
    requestUpdate();
    emit timeChanged(time_);
}

void TimePicker::stepColumn(int column, int delta)
{
    if (!isEnabled() || column < 0 || column >= columnCount_ || delta == 0)
        return;

    const int count = valueCount(columns_[column]);
    if (count <= 0)
        return;

    int row = static_cast<int>(std::lround(columnStates_[column].offset)) + delta;
    row %= count;
    if (row < 0)
        row += count;

    columnStates_[column].offset = qreal(row);
    columnStates_[column].target = qreal(row);
    commitColumn(column, row);
    requestUpdate();
}

void TimePicker::stepField(Field field, int delta)
{
    stepColumn(fieldColumn(field), delta);
}

void TimePicker::step(int delta)
{
    stepColumn(currentColumn_, delta);
}

bool TimePicker::isAnimating() const
{
    return animator_ != nullptr && animator_->isActive();
}

void TimePicker::finishAnimation()
{
    if (animator_ != nullptr)
        animator_->stop();

    // Land on the target row, not on wherever the last tick happened to stop.
    for (int index = 0; index < columnCount_; ++index) {
        columnStates_[index].offset = normalized(columnStates_[index].target,
                                                 valueCount(columns_[index]));
        columnStates_[index].target = columnStates_[index].offset;
    }

    requestUpdate();
}

void TimePicker::animateTo(int column, qreal target)
{
    if (column < 0 || column >= columnCount_)
        return;

    // Take the short way round the cycle so a 23 -> 0 snap does not unwind the
    // whole wheel.
    const qreal count = qreal(valueCount(columns_[column]));
    const qreal offset = columnStates_[column].offset;
    qreal best = target;
    if (count > 0.0) {
        while (best - offset > count / 2.0)
            best -= count;
        while (offset - best > count / 2.0)
            best += count;
    }

    columnStates_[column].target = best;

    // Reduced motion: a snap that would be a 16 ms chase becomes an immediate
    // landing on the target row, and no timer is started at all.
    if (Theme::forWidget(this).motion().reducedMotion) {
        const qreal landed = normalized(best, valueCount(columns_[column]));
        columnStates_[column].offset = landed;
        columnStates_[column].target = landed;
        requestUpdate();
        return;
    }

    if (!animator_->isActive())
        animator_->start();
}

void TimePicker::tickAnimation()
{
    bool moving = false;
    for (int index = 0; index < columnCount_; ++index) {
        ColumnState& state = columnStates_[index];
        const qreal distance = state.target - state.offset;
        if (qAbs(distance) < 0.01) {
            state.offset = state.target;
            continue;
        }

        state.offset += distance * kAnimationFraction;
        moving = true;
    }

    if (!moving) {
        animator_->stop();
        for (int index = 0; index < columnCount_; ++index) {
            columnStates_[index].offset = normalized(columnStates_[index].offset,
                                                     valueCount(columns_[index]));
            columnStates_[index].target = columnStates_[index].offset;
        }
    }

    requestUpdate();
}

int TimePicker::columnAt(qreal x) const
{
    for (int index = 0; index < columnCount_; ++index) {
        if (columnRect(index).contains(QPointF(x, wheelRect().center().y())))
            return index;
    }

    return -1;
}

void TimePicker::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    setMeasuredSize(QSizeF(width.resolve(density.dp(256.0)), height.resolve(density.dp(200.0))));
}

void TimePicker::paintContent(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);

    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();
    const Density& density = theme.density();
    const qreal unit = density.dp(1.0);
    const qreal baseOpacity = isEnabled() ? 1.0 : 0.38;
    const QRectF wheel = wheelRect();
    if (wheel.isEmpty() || columnCount_ <= 0)
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setOpacity(baseOpacity);

    // The selection band: one tonal container behind the settled row.
    const QRectF band = selectionBandRect();
    painter.setPen(Qt::NoPen);
    painter.setBrush(colours.primaryContainer);
    painter.drawRoundedRect(band, density.dp(kBandRadiusDp), density.dp(kBandRadiusDp));

    // Column separators, between the band and the text. M3 dividers are a 1 dp
    // outline-variant line at full opacity; this one used to be dimmed to 60%,
    // which read as a smudge behind the numbers.
    painter.setPen(QPen(colours.outlineVariant, unit));
    for (int index = 1; index < columnCount_; ++index) {
        const qreal x = columnRect(index).left();
        painter.drawLine(QPointF(x, wheel.top() + density.dp(6.0)),
                         QPointF(x, wheel.bottom() - density.dp(6.0)));
    }

    painter.setClipRect(wheel);
    const qreal height = rowHeight();
    const qreal centreY = wheel.center().y();
    const QFont font = fontFor(theme, theme.type().titleMedium);

    for (int column = 0; column < columnCount_; ++column) {
        const QRectF area = columnRect(column);
        const Field field = columns_[column];
        const qreal offset = columnStates_[column].offset;
        const int first = static_cast<int>(std::floor(offset)) - 2;

        for (int row = first; row <= first + 5; ++row) {
            const qreal distance = qAbs(qreal(row) - offset);
            if (distance > 2.6)
                continue;

            const qreal top = centreY + (qreal(row) - offset) * height - height / 2.0;
            if (top + height < wheel.top() || top > wheel.bottom())
                continue;

            const qreal alpha = distance < 0.5 ? 1.0 : (distance < 1.5 ? 0.6 : 0.3);
            painter.setFont(font);
            painter.setOpacity(baseOpacity * alpha);
            painter.setPen(distance < 0.5 ? colours.onPrimaryContainer : colours.onSurfaceVariant);
            painter.drawText(QRectF(area.left(), top, area.width(), height), Qt::AlignCenter,
                             textOfIndex(field, row));
        }
    }

    painter.setClipping(false);
    painter.setOpacity(baseOpacity);

    // A short hint under the active column so the keyboard target is visible
    // even when the widget is not focused.
    if (currentColumn_ >= 0 && currentColumn_ < columnCount_) {
        const QRectF area = columnRect(currentColumn_);
        const qreal width = std::min(area.width() * 0.4, density.dp(16.0));
        const QRectF hint(area.center().x() - width / 2.0, band.bottom() + density.dp(8.0),
                          width, unit * 2.0);
        if (hint.bottom() < wheel.bottom()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(hasFocus() ? colours.primary : colours.outlineVariant);
            painter.drawRoundedRect(hint, unit, unit);
        }
    }

    painter.restore();
}

void TimePicker::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && isEnabled()) {
        pressed_ = event->position();
        const int column = columnAt(pressed_.x());
        if (column >= 0) {
            setCurrentColumn(column);
            dragging_ = true;
            dragColumn_ = column;
            dragStart_ = pressed_;
            dragStartOffset_ = columnStates_[column].offset;
        }
    }

    View::mousePressEvent(event);
}

void TimePicker::mouseMoveEvent(QMouseEvent* event)
{
    if (dragging_ && dragColumn_ >= 0 && dragColumn_ < columnCount_
        && (event->buttons() & Qt::LeftButton) != 0) {
        const qreal dy = event->position().y() - dragStart_.y();
        const int count = valueCount(columns_[dragColumn_]);
        columnStates_[dragColumn_].offset =
            normalized(dragStartOffset_ - dy / rowHeight(), count);
        columnStates_[dragColumn_].target = columnStates_[dragColumn_].offset;
        requestUpdate();
    }

    View::mouseMoveEvent(event);
}

void TimePicker::mouseReleaseEvent(QMouseEvent* event)
{
    const bool wasDragging = dragging_;
    const int column = dragColumn_;
    const qreal startY = dragStart_.y();
    dragging_ = false;
    dragColumn_ = -1;
    pressed_ = QPointF(-1.0, -1.0);

    View::mouseReleaseEvent(event);

    if (!isEnabled() || event->button() != Qt::LeftButton || !wasDragging || column < 0
        || column >= columnCount_) {
        return;
    }

    const qreal thresholdPx = Theme::forWidget(this).density().dp(kDragThresholdDp);
    if (qAbs(event->position().y() - startY) <= thresholdPx) {
        // A click, not a drag: the row under the pointer wins.
        const qreal rows = (event->position().y() - wheelRect().center().y()) / rowHeight();
        const int row = static_cast<int>(std::lround(rows));
        if (row != 0) {
            stepColumn(column, row);
            return;
        }

        animateTo(column, qreal(std::lround(columnStates_[column].offset)));
        return;
    }

    const int count = valueCount(columns_[column]);
    int row = static_cast<int>(std::lround(columnStates_[column].offset));
    row %= count;
    if (row < 0)
        row += count;

    commitColumn(column, row);
    animateTo(column, qreal(row));
}

void TimePicker::keyPressEvent(QKeyEvent* event)
{
    if (!isEnabled()) {
        View::keyPressEvent(event);
        return;
    }

    switch (event->key()) {
    case Qt::Key_Up:
        step(-1);
        break;
    case Qt::Key_Down:
        step(1);
        break;
    case Qt::Key_PageUp:
        step(-kPageRows);
        break;
    case Qt::Key_PageDown:
        step(kPageRows);
        break;
    case Qt::Key_Home: {
        if (columnCount_ <= 0)
            return;
        const int count = valueCount(columns_[currentColumn_]);
        const int row = static_cast<int>(std::lround(columnStates_[currentColumn_].offset)) % count;
        stepColumn(currentColumn_, -row);
        break;
    }
    case Qt::Key_End: {
        if (columnCount_ <= 0)
            return;
        const int count = valueCount(columns_[currentColumn_]);
        int row = static_cast<int>(std::lround(columnStates_[currentColumn_].offset)) % count;
        if (row < 0)
            row += count;
        stepColumn(currentColumn_, count - 1 - row);
        break;
    }
    case Qt::Key_Left:
        setCurrentColumn(currentColumn_ - 1);
        break;
    case Qt::Key_Right:
        setCurrentColumn(currentColumn_ + 1);
        break;
    default:
        View::keyPressEvent(event);
        return;
    }

    event->accept();
}

void TimePicker::wheelEvent(QWheelEvent* event)
{
    if (!isEnabled()) {
        event->ignore();
        return;
    }

    const int column = columnAt(event->position().x());
    if (column < 0) {
        event->ignore();
        return;
    }

    int steps = 0;
    const QPoint angle = event->angleDelta();
    if (!angle.isNull()) {
        steps = angle.y() / 120;
    } else {
        const qreal pixels = event->pixelDelta().y();
        if (!qFuzzyIsNull(pixels))
            steps = pixels > 0.0 ? 1 : -1;
    }

    if (steps != 0) {
        // A notch upward moves the wheel back one row, as a physical wheel does.
        stepColumn(column, -steps);
    }

    event->accept();
}

void TimePicker::onHidden()
{
    finishAnimation();
}

void TimePicker::onThemeChanged()
{
    // If the new theme asks for reduced motion, a running snap has to land on
    // its target now instead of continuing to chase it frame by frame.
    if (isAnimating() && Theme::forWidget(this).motion().reducedMotion)
        finishAnimation();

    requestUpdate();
}

} // namespace md
