// Periodicity — implementation of the 1D strip context.
//
// The strip is painted into an inset of the content rectangle so dots at the
// minimum and maximum values are fully inside the clip. Ticks and the zero
// baseline paint below the data, the polyline above them and the dots last.

#include <md/components/context1d.h>

#include <md/core/density.h>
#include <md/theme/theme.h>

#include <QBrush>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QScopeGuard>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace md {
namespace {

template <class Fn>
void guardContext1D(const char* where, Fn&& fn)
{
    try {
        std::forward<Fn>(fn)();
    } catch (const std::exception& error) {
        qWarning("md::Context1D: %s failed: %s", where, error.what());
    } catch (...) {
        qWarning("md::Context1D: %s failed: unknown exception", where);
    }
}

constexpr int kMaximumTicks = 1000;

} // namespace

Context1D::Context1D(QWidget* host)
    : host_(host)
{
    const Theme& theme = Theme::forWidget(host);
    lineColour_ = theme.colours().primary;
    dotColour_ = theme.colours().primary;
    baselineColour_ = theme.colours().outlineVariant;
    tickColour_ = theme.colours().outlineVariant;
}

Context1D::~Context1D() = default;

void Context1D::setRange(qreal minimum, qreal maximum)
{
    guardContext1D("Context1D::setRange", [this, minimum, maximum] {
        qreal low = std::isfinite(minimum) ? minimum : 0.0;
        qreal high = std::isfinite(maximum) ? maximum : low + 1.0;
        if (low > high)
            std::swap(low, high);

        if (minimum_ == low && maximum_ == high)
            return;

        minimum_ = low;
        maximum_ = high;
        clampStoredValues();
    });
}

qreal Context1D::minimum() const noexcept
{
    return minimum_;
}

qreal Context1D::maximum() const noexcept
{
    return maximum_;
}

void Context1D::setValues(QVector<qreal> values)
{
    guardContext1D("Context1D::setValues", [this, &values] {
        QVector<qreal> clamped;
        clamped.reserve(values.size());
        for (const qreal value : values)
            clamped.push_back(clampValue(value));

        values_ = std::move(clamped);
    });
}

const QVector<qreal>& Context1D::values() const noexcept
{
    return values_;
}

void Context1D::setTicks(int count)
{
    guardContext1D("Context1D::setTicks", [this, count] {
        ticks_ = std::clamp(count, 0, kMaximumTicks);
    });
}

int Context1D::ticks() const noexcept
{
    return ticks_;
}

void Context1D::setBaselineVisible(bool visible)
{
    guardContext1D("Context1D::setBaselineVisible",
                   [this, visible] { baselineVisible_ = visible; });
}

bool Context1D::baselineVisible() const noexcept
{
    return baselineVisible_;
}

void Context1D::setPolylineVisible(bool visible)
{
    guardContext1D("Context1D::setPolylineVisible",
                   [this, visible] { polylineVisible_ = visible; });
}

bool Context1D::polylineVisible() const noexcept
{
    return polylineVisible_;
}

void Context1D::setDotsVisible(bool visible)
{
    guardContext1D("Context1D::setDotsVisible",
                   [this, visible] { dotsVisible_ = visible; });
}

bool Context1D::dotsVisible() const noexcept
{
    return dotsVisible_;
}

void Context1D::setLineColour(const QColor& colour)
{
    guardContext1D("Context1D::setLineColour", [this, &colour] {
        lineColour_ = colour.isValid() ? colour
                                       : Theme::forWidget(host_.data()).colours().primary;
    });
}

QColor Context1D::lineColour() const
{
    return lineColour_;
}

void Context1D::setDotColour(const QColor& colour)
{
    guardContext1D("Context1D::setDotColour", [this, &colour] {
        dotColour_ = colour.isValid() ? colour
                                      : Theme::forWidget(host_.data()).colours().primary;
    });
}

QColor Context1D::dotColour() const
{
    return dotColour_;
}

void Context1D::setBaselineColour(const QColor& colour)
{
    guardContext1D("Context1D::setBaselineColour", [this, &colour] {
        baselineColour_ = colour.isValid()
            ? colour
            : Theme::forWidget(host_.data()).colours().outlineVariant;
    });
}

QColor Context1D::baselineColour() const
{
    return baselineColour_;
}

void Context1D::setTickColour(const QColor& colour)
{
    guardContext1D("Context1D::setTickColour", [this, &colour] {
        tickColour_ = colour.isValid()
            ? colour
            : Theme::forWidget(host_.data()).colours().outlineVariant;
    });
}

QColor Context1D::tickColour() const
{
    return tickColour_;
}

void Context1D::setLineWidth(qreal widthDp)
{
    guardContext1D("Context1D::setLineWidth", [this, widthDp] {
        lineWidthDp_ = std::max<qreal>(0.0, std::isfinite(widthDp) ? widthDp : 2.0);
    });
}

qreal Context1D::lineWidth() const noexcept
{
    return lineWidthDp_;
}

void Context1D::setDotRadius(qreal radiusDp)
{
    guardContext1D("Context1D::setDotRadius", [this, radiusDp] {
        dotRadiusDp_ = std::max<qreal>(0.5, std::isfinite(radiusDp) ? radiusDp : 2.5);
    });
}

qreal Context1D::dotRadius() const noexcept
{
    return dotRadiusDp_;
}

qreal Context1D::clampValue(qreal value) const noexcept
{
    if (!std::isfinite(value))
        return minimum_;

    if (maximum_ <= minimum_)
        return minimum_;

    return std::clamp(value, minimum_, maximum_);
}

void Context1D::clampStoredValues() noexcept
{
    for (qreal& value : values_)
        value = clampValue(value);
}

qreal Context1D::valueToY(qreal value, const QRectF& area) const noexcept
{
    if (!std::isfinite(area.height()) || area.height() <= 0.0)
        return area.center().y();

    qreal fraction = 0.5;
    if (maximum_ > minimum_)
        fraction = (clampValue(value) - minimum_) / (maximum_ - minimum_);

    return area.bottom() - fraction * area.height();
}

QPointF Context1D::pointAt(int index, const QRectF& area) const noexcept
{
    if (values_.isEmpty())
        return area.center();

    const int bounded = std::clamp(index, 0, static_cast<int>(values_.size()) - 1);
    qreal x = area.center().x();
    if (values_.size() > 1) {
        const qreal fraction = static_cast<qreal>(bounded)
            / static_cast<qreal>(values_.size() - 1);
        x = area.left() + fraction * area.width();
    }

    return QPointF(x, valueToY(values_.at(bounded), area));
}

void Context1D::paint(QPainter& painter, const QRectF& content)
{
    guardContext1D("Context1D::paint", [this, &painter, &content] {
        if (!painter.isActive() || content.isEmpty())
            return;

        const Theme& theme = Theme::forWidget(host_.data());
        const Density& density = theme.density();
        const qreal radius = std::max<qreal>(0.5, density.dp(dotRadiusDp_));
        const qreal stroke = std::max<qreal>(0.0, density.dp(lineWidthDp_));
        const qreal inset = radius + stroke * 0.5 + 1.0;

        QRectF plot = content.adjusted(inset, inset, -inset, -inset);
        if (plot.width() < 1.0 || plot.height() < 1.0)
            plot = content;

        painter.save();
        const auto restorePainter = qScopeGuard([&painter] { painter.restore(); });
        painter.setClipRect(content, Qt::IntersectClip);
        painter.setRenderHint(QPainter::Antialiasing, true);

        // Grid lines first, so the baseline and data sit above them.
        if (ticks_ > 0) {
            QColor grid = tickColour_;
            grid.setAlphaF(grid.alphaF() * 0.55f);
            QPen gridPen(grid);
            gridPen.setWidthF(std::max<qreal>(0.5, density.dp(0.75)));
            painter.setPen(gridPen);
            painter.setBrush(Qt::NoBrush);

            for (int index = 0; index < ticks_; ++index) {
                const qreal fraction = ticks_ == 1
                    ? 0.5
                    : static_cast<qreal>(index) / static_cast<qreal>(ticks_ - 1);
                const qreal value = minimum_ + fraction * (maximum_ - minimum_);
                const qreal y = valueToY(value, plot);
                painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            }
        }

        // A zero baseline is meaningful only when zero lies inside the range.
        if (baselineVisible_ && minimum_ <= 0.0 && maximum_ >= 0.0) {
            QPen baselinePen(baselineColour_);
            baselinePen.setWidthF(std::max<qreal>(0.5, density.dp(1.0)));
            painter.setPen(baselinePen);
            painter.setBrush(Qt::NoBrush);
            const qreal y = valueToY(0.0, plot);
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }

        if (polylineVisible_ && values_.size() >= 2) {
            QPolygonF line;
            for (int index = 0; index < values_.size(); ++index)
                line.append(pointAt(index, plot));

            QPen linePen(lineColour_);
            linePen.setWidthF(stroke);
            linePen.setCapStyle(Qt::RoundCap);
            linePen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(linePen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPolyline(line);
        }

        if (dotsVisible_ && !values_.isEmpty()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(dotColour_);
            for (int index = 0; index < values_.size(); ++index)
                painter.drawEllipse(pointAt(index, plot), radius, radius);
        }
    });
}

void Context1D::clear()
{
    guardContext1D("Context1D::clear", [this] { values_.clear(); });
}

void Context1D::requestDraw()
{
    guardContext1D("Context1D::requestDraw", [this] {
        if (host_)
            host_->update();
    });
}

} // namespace md
