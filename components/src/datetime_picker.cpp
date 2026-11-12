// Periodicity — the DateTimePicker component (framework.md §5.9).
//
// The composite keeps one QDateTime and derives both the calendar selection and
// the three time segments from it, so date and time can never disagree. Every
// mutation funnels through applyValue(), which clamps to the datetime range and
// re-synchronises the child calendar under an `updating_` guard to break the
// signal loop between "calendar selected a date" and "value pushed a date".

#include <md/components/datetime_picker.h>

#include <md/theme/theme.h>

#include <QFont>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>

namespace md {
namespace {

constexpr qreal kTimeHeightDp = 72.0;
constexpr qreal kTimePaddingDp = 12.0;
constexpr qreal kFieldGapDp = 12.0;

[[nodiscard]] QFont fontFor(const Theme& theme, const TextStyle& style)
{
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(QFont::Weight(style.weight));
    return font;
}

} // namespace

DateTimePicker::DateTimePicker(QWidget* parent)
    : View(parent)
    , calendar_(new DatePicker(this))
{
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("Choose date and time"));

    // The composite paints its own surface; the calendar child only paints its
    // content so the two areas read as one sheet.
    calendar_->setBackgroundColour(QColor(Qt::transparent));
    calendar_->setDateRange(minimum_.date(), maximum_.date());
    calendar_->setSelectedDate(value_.date());
    calendar_->setDisplayedMonth(value_.date());

    connect(calendar_, &DatePicker::selectedDateChanged, this, [this](QDate date) {
        if (updating_ || !date.isValid())
            return;
        applyValue(QDateTime(date, value_.time()));
    });
}

int DateTimePicker::fieldCount() const
{
    return secondsVisible_ ? 3 : 2;
}

int DateTimePicker::fieldMaximum(int field) const
{
    switch (field) {
    case 0:
        return 23;
    case 1:
    case 2:
        return 59;
    default:
        return 0;
    }
}

int DateTimePicker::fieldValue(int field) const
{
    const QTime current = value_.time();
    switch (field) {
    case 0:
        return current.hour();
    case 1:
        return current.minute();
    case 2:
        return current.second();
    default:
        return 0;
    }
}

void DateTimePicker::setFieldValue(int field, int value)
{
    QTime next = value_.time();
    switch (field) {
    case 0:
        next.setHMS(std::clamp(value, 0, 23), next.minute(), next.second(), next.msec());
        break;
    case 1:
        next.setHMS(next.hour(), std::clamp(value, 0, 59), next.second(), next.msec());
        break;
    case 2:
        next.setHMS(next.hour(), next.minute(), std::clamp(value, 0, 59), next.msec());
        break;
    default:
        return;
    }

    applyValue(QDateTime(value_.date(), next));
}

void DateTimePicker::commitDigits()
{
    if (digits_.isEmpty())
        return;

    const int value = std::clamp(digits_.toInt(), 0, fieldMaximum(field_));
    digits_.clear();
    setFieldValue(field_, value);
}

void DateTimePicker::typeDigit(int digit)
{
    if (digits_.size() >= 2)
        digits_.clear();

    digits_ += QString::number(digit);

    int candidate = digits_.toInt();
    const int maximum = fieldMaximum(field_);
    if (candidate > maximum) {
        digits_ = QString::number(digit);
        candidate = digit;
    }

    setFieldValue(field_, candidate);

    // A second digit completes the entry; a single digit that cannot be the
    // first half of any valid two-digit value completes it as well (hour 9,
    // minute 7, ...).
    const bool complete = digits_.size() >= 2 || candidate * 10 > maximum;
    if (complete) {
        digits_.clear();
        field_ = (field_ + 1) % fieldCount();
    }

    requestUpdate();
}

void DateTimePicker::step(int amount)
{
    if (amount == 0)
        return;

    int seconds = 3600;
    if (field_ == 1)
        seconds = 60;
    else if (field_ == 2)
        seconds = 1;

    const QTime next = value_.time().addSecs(seconds * amount);
    applyValue(QDateTime(value_.date(), next));
}

void DateTimePicker::moveField(int delta)
{
    setCurrentField(field_ + delta);
}

void DateTimePicker::setCurrentField(int field)
{
    const int count = fieldCount();
    if (count <= 0)
        return;

    commitDigits();
    const int next = std::clamp(field, 0, count - 1);
    if (next == field_)
        return;

    field_ = next;
    requestUpdate();
}

QDateTime DateTimePicker::clampDateTime(const QDateTime& value) const
{
    if (value < minimum_)
        return minimum_;
    if (value > maximum_)
        return maximum_;
    return value;
}

void DateTimePicker::applyValue(const QDateTime& candidate)
{
    if (!candidate.isValid())
        return;

    const QDateTime next = clampDateTime(candidate);
    const bool changed = next != value_;
    value_ = next;
    syncCalendar();
    requestUpdate();

    if (changed)
        emit dateTimeChanged(value_);
}

void DateTimePicker::syncCalendar()
{
    if (calendar_ == nullptr)
        return;

    const bool previous = updating_;
    updating_ = true;
    calendar_->setDateRange(minimum_.date(), maximum_.date());
    calendar_->setSelectedDate(value_.date());
    calendar_->setDisplayedMonth(value_.date());
    updating_ = previous;
}

void DateTimePicker::setDateTime(const QDateTime& value)
{
    applyValue(value);
}

void DateTimePicker::setTime(QTime time)
{
    if (!time.isValid())
        return;

    applyValue(QDateTime(value_.date(), time));
}

void DateTimePicker::setDateTimeRange(const QDateTime& minimum, const QDateTime& maximum)
{
    if (!minimum.isValid() || !maximum.isValid() || minimum > maximum)
        return;

    minimum_ = minimum;
    maximum_ = maximum;
    applyValue(value_);
}

void DateTimePicker::setSecondsVisible(bool visible)
{
    if (secondsVisible_ == visible)
        return;

    secondsVisible_ = visible;
    if (!secondsVisible_ && field_ > 1)
        field_ = 1;
    digits_.clear();
    requestUpdate();
}

QRectF DateTimePicker::timeRect() const
{
    const qreal heightPx = Theme::forWidget(this).density().dp(kTimeHeightDp);
    const qreal top = std::max<qreal>(0.0, qreal(height()) - heightPx);
    return QRectF(0.0, top, qreal(width()), std::min<qreal>(heightPx, qreal(height())));
}

QRectF DateTimePicker::fieldRect(int field) const
{
    const int count = fieldCount();
    if (field < 0 || field >= count)
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const QRectF area = timeRect().adjusted(density.dp(kTimePaddingDp), density.dp(8.0),
                                            -density.dp(kTimePaddingDp), -density.dp(8.0));
    if (area.width() <= 0.0 || area.height() <= 0.0)
        return QRectF();

    const qreal gap = density.dp(kFieldGapDp);
    const qreal cellWidth = (area.width() - gap * (count - 1)) / count;
    return QRectF(area.left() + field * (cellWidth + gap), area.top(),
                  std::max<qreal>(0.0, cellWidth), area.height());
}

void DateTimePicker::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    setMeasuredSize(QSizeF(width.resolve(density.dp(360.0)),
                           height.resolve(density.dp(396.0) + density.dp(kTimeHeightDp))));
}

void DateTimePicker::arrangeOverride(const QRectF& content)
{
    if (calendar_ == nullptr)
        return;

    const qreal timeHeight = Theme::forWidget(this).density().dp(kTimeHeightDp);
    const qreal calendarHeight = std::max<qreal>(0.0, content.height() - timeHeight);
    calendar_->arrange(QRectF(content.left(), content.top(), content.width(), calendarHeight)
                           .toRect());
}

void DateTimePicker::resizeEvent(QResizeEvent* event)
{
    View::resizeEvent(event);
    arrangeOverride(contentRect());
}

bool DateTimePicker::event(QEvent* event)
{
    // QWidget::event() consumes Tab for focus traversal before keyPressEvent()
    // runs. In this composite Tab switches time segments, so the key has to be
    // claimed one level higher.
    if (event->type() == QEvent::KeyPress && isEnabled()) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
            const int count = fieldCount();
            if (count > 0) {
                commitDigits();
                const int direction = key->key() == Qt::Key_Tab ? 1 : -1;
                field_ = (field_ + direction + count) % count;
                requestUpdate();
                key->accept();
                return true;
            }
        }
    }

    return View::event(event);
}

void DateTimePicker::paintContent(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);

    const QRectF area = timeRect();
    if (area.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();
    const Density& density = theme.density();
    const qreal unit = density.dp(1.0);
    const qreal baseOpacity = isEnabled() ? 1.0 : 0.38;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setOpacity(baseOpacity);

    painter.setPen(QPen(colours.outlineVariant, unit));
    painter.drawLine(QPointF(area.left() + density.dp(kTimePaddingDp), area.top()),
                     QPointF(area.right() - density.dp(kTimePaddingDp), area.top()));

    const QFont valueFont = fontFor(theme, theme.type().titleLarge);
    const QFont labelFont = fontFor(theme, theme.type().labelSmall);
    const int count = fieldCount();

    for (int field = 0; field < count; ++field) {
        const QRectF rect = fieldRect(field);
        if (rect.isEmpty())
            continue;

        const bool active = field == field_;
        painter.setPen(active ? QPen(colours.primary, unit * 2.0)
                              : QPen(colours.outlineVariant, unit));
        painter.setBrush(active ? colours.primaryContainer : colours.surfaceContainerHighest);
        painter.drawRoundedRect(rect, unit * 8.0, unit * 8.0);

        const QString valueText =
            QStringLiteral("%1").arg(fieldValue(field), 2, 10, QLatin1Char('0'));
        painter.setFont(valueFont);
        painter.setPen(active ? colours.onPrimaryContainer : colours.onSurface);
        painter.drawText(rect.adjusted(0.0, 0.0, 0.0, -unit * 12.0), Qt::AlignCenter, valueText);

        const QString label = field == 0 ? tr("Hour")
                              : field == 1 ? tr("Minute")
                                           : tr("Second");
        painter.setFont(labelFont);
        painter.setPen(active ? colours.onPrimaryContainer : colours.onSurfaceVariant);
        painter.drawText(rect.adjusted(0.0, rect.height() - unit * 18.0, 0.0, -unit * 2.0),
                         Qt::AlignCenter, label);
    }

    painter.setFont(fontFor(theme, theme.type().titleMedium));
    painter.setPen(colours.onSurfaceVariant);
    const qreal fieldGap = density.dp(kFieldGapDp);
    for (int field = 0; field + 1 < count; ++field) {
        const QRectF left = fieldRect(field);
        const QRectF right = fieldRect(field + 1);
        if (left.isEmpty() || right.isEmpty())
            continue;

        const qreal x = (left.right() + right.left()) / 2.0;
        const qreal y = left.center().y();
        painter.drawText(QRectF(x - fieldGap / 2.0, y - unit * 12.0, fieldGap, unit * 24.0),
                         Qt::AlignCenter, QStringLiteral(":"));
    }

    painter.restore();
}

void DateTimePicker::mousePressEvent(QMouseEvent* event)
{
    if (isEnabled() && event->button() == Qt::LeftButton) {
        for (int field = 0; field < fieldCount(); ++field) {
            if (fieldRect(field).contains(event->position())) {
                setCurrentField(field);
                digits_.clear();
                requestUpdate();
                event->accept();
                return;
            }
        }

        if (timeRect().contains(event->position())) {
            event->accept();
            return;
        }
    }

    View::mousePressEvent(event);
}

void DateTimePicker::keyPressEvent(QKeyEvent* event)
{
    if (!isEnabled()) {
        View::keyPressEvent(event);
        return;
    }

    const int key = event->key();
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        typeDigit(key - Qt::Key_0);
        event->accept();
        return;
    }

    if (key == Qt::Key_Backspace) {
        if (!digits_.isEmpty())
            digits_.chop(1);
        event->accept();
        return;
    }

    commitDigits();

    switch (key) {
    case Qt::Key_Left:
        moveField(-1);
        event->accept();
        return;
    case Qt::Key_Right:
        moveField(1);
        event->accept();
        return;
    case Qt::Key_Tab:
        setCurrentField((field_ + 1) % fieldCount());
        event->accept();
        return;
    case Qt::Key_Backtab:
        setCurrentField((field_ - 1 + fieldCount()) % fieldCount());
        event->accept();
        return;
    case Qt::Key_Up:
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        step(1);
        event->accept();
        return;
    case Qt::Key_Down:
    case Qt::Key_Minus:
    case Qt::Key_Underscore:
        step(-1);
        event->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        event->accept();
        return;
    default:
        View::keyPressEvent(event);
        return;
    }
}

} // namespace md
