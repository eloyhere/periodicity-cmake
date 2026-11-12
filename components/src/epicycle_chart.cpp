// Periodicity — the EpicycleChart component.
//
// The transform is a plain DFT, not an approximation of one:
//
//   c_k = (1/N) * sum_n z_n * exp(-i 2*pi*k*n/N)
//
// with z_n = x_n + i*y_n. Both k and -k are kept, because a two-dimensional
// closed curve has no conjugate symmetry, and the chain therefore contains two
// vectors for each positive harmonic. The centroid c_0 is the chain's origin;
// the remaining vectors rotate at their signed frequency and their tip sums to
// the curve.
//
// The projection fits the drawing by the chain's worst-case reach, the sum of
// the vector radii, not by the target's bounding box. That guarantees every
// circle stays inside plotRect(). targetPoint() uses the same projection, so the
// hover hit test agrees with what was painted.
//
// The reveal is presentation only: the coefficients, the radii and the target
// mapping are settled immediately. Progressive tracing samples the same
// reconstruction at the current reveal time, so a frame is always a prefix of
// the final trace and never a different curve.
//
// Every entry point catches locally so a broken theme cannot unwind into Qt.

#include <md/components/epicycle_chart.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QBrush>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <complex>
#include <exception>
#include <limits>
#include <utility>

namespace md {
namespace {

constexpr qreal kPi = 3.14159265358979323846;
constexpr qreal kTwoPi = 2.0 * kPi;

constexpr qreal kPlotInsetDp = 10.0;
constexpr qreal kGridAlpha = 0.35;
constexpr qreal kGhostAlpha = 0.55;
constexpr qreal kCircleAlpha = 0.5;
constexpr qreal kArmAlpha = 0.6;
constexpr qreal kHoverRadiusDp = 6.0;
constexpr qreal kReadoutInsetDp = 4.0;
constexpr qreal kReadoutGapDp = 8.0;
constexpr qreal kMinimumPlotRadiusDp = 20.0;
constexpr int kMaximumHarmonics = 64;

[[nodiscard]] QString formatValue(qreal value)
{
    if (!std::isfinite(value))
        return QString();

    const qreal magnitude = std::abs(value);
    if (magnitude > 0.0 && (magnitude < 0.01 || magnitude >= 100000.0))
        return QString::number(value, 'g', 3);

    int decimals = 2;
    if (magnitude >= 100.0)
        decimals = 0;
    else if (magnitude >= 10.0)
        decimals = 1;

    QString text = QString::number(value, 'f', decimals);
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0')))
            text.chop(1);
        if (text.endsWith(QLatin1Char('.')))
            text.chop(1);
    }

    if (text == QStringLiteral("-0"))
        text = QStringLiteral("0");

    return text;
}

[[nodiscard]] qreal niceNumber(qreal value)
{
    if (!std::isfinite(value) || !(value > 0.0))
        return 1.0;

    const qreal exponent = std::floor(std::log10(value));
    const qreal base = std::pow(qreal(10.0), exponent);
    const qreal fraction = value / base;

    qreal nice = 10.0;
    if (fraction <= 1.0)
        nice = 1.0;
    else if (fraction <= 2.0)
        nice = 2.0;
    else if (fraction <= 5.0)
        nice = 5.0;

    return nice * base;
}

class PainterStateGuard
{
public:
    explicit PainterStateGuard(QPainter& painter) : painter_(painter) { painter_.save(); }
    ~PainterStateGuard() { painter_.restore(); }

    PainterStateGuard(const PainterStateGuard&) = delete;
    PainterStateGuard& operator=(const PainterStateGuard&) = delete;

private:
    QPainter& painter_;
};

} // namespace

EpicycleChart::EpicycleChart(QWidget* parent)
    : ChartBase(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setAxesVisible(false);

    // The trace and the circles are a data reading; the shared state layer
    // would tint them after paintPlot(), so the chart paints its own hover.
    setInteractionFeedbackEnabled(false);
    animation().setDuration(MotionLevel::Level2);
}

void EpicycleChart::setTarget(std::vector<QPointF> points)
{
    target_.clear();
    target_.reserve(points.size());

    for (const QPointF& point : points) {
        if (std::isfinite(point.x()) && std::isfinite(point.y()))
            target_.push_back(point);
    }

    hoveredPoint_ = -1;
    rebuildHarmonics();
    rebuildDisplaySeries();
    requestUpdate();
}

const std::vector<QPointF>& EpicycleChart::target() const noexcept
{
    return target_;
}

int EpicycleChart::sampleCount() const noexcept
{
    return static_cast<int>(target_.size());
}

void EpicycleChart::rebuildDisplaySeries()
{
    QVector<ChartSeries> entries;

    if (!target_.empty()) {
        ChartSeries series;
        series.points.reserve(static_cast<qsizetype>(target_.size()));
        for (const QPointF& point : target_)
            series.points.append(point);
        entries.append(std::move(series));
    }

    ChartBase::setSeries(std::move(entries));
}

void EpicycleChart::rebuildHarmonics()
{
    terms_.clear();
    centroid_ = QPointF();
    extent_ = 0.0;

    const int count = static_cast<int>(target_.size());
    if (count <= 0)
        return;

    QPointF mean;
    for (const QPointF& point : target_)
        mean += point;
    mean /= static_cast<qreal>(count);
    centroid_ = mean;

    for (const QPointF& point : target_)
        extent_ = std::max(extent_, std::hypot(point.x() - mean.x(), point.y() - mean.y()));

    if (count < 3)
        return;

    const int maximumFrequency = (count - 1) / 2;
    const int requested = std::clamp(harmonics_, 1, kMaximumHarmonics);
    const int effective = std::min(requested, maximumFrequency);
    if (effective <= 0)
        return;

    QVector<Term> collected;
    collected.reserve(effective * 2);
    qreal maximumRadius = 0.0;

    for (int harmonic = 1; harmonic <= effective; ++harmonic) {
        std::complex<qreal> positive(0.0, 0.0);
        std::complex<qreal> negative(0.0, 0.0);

        for (int sample = 0; sample < count; ++sample) {
            const qreal angle = kTwoPi * harmonic * sample / count;
            const std::complex<qreal> value(target_.at(sample).x(),
                                            target_.at(sample).y());
            const std::complex<qreal> forward(std::cos(angle), -std::sin(angle));
            const std::complex<qreal> backward(std::cos(angle), std::sin(angle));
            positive += value * forward;
            negative += value * backward;
        }

        positive /= static_cast<qreal>(count);
        negative /= static_cast<qreal>(count);

        const qreal positiveRadius = std::abs(positive);
        const qreal negativeRadius = std::abs(negative);

        if (positiveRadius > 0.0) {
            Term term;
            term.frequency = harmonic;
            term.radius = positiveRadius;
            term.phase = std::arg(positive);
            collected.append(term);
            maximumRadius = std::max(maximumRadius, positiveRadius);
        }

        if (negativeRadius > 0.0) {
            Term term;
            term.frequency = -harmonic;
            term.radius = negativeRadius;
            term.phase = std::arg(negative);
            collected.append(term);
            maximumRadius = std::max(maximumRadius, negativeRadius);
        }
    }

    // Drop terms below the numerical floor: an exact circle has a conjugate
    // coefficient of about 1e-16, and painting it as a circle would add visual
    // noise without contributing to the curve.
    const qreal floor = maximumRadius * 1e-9;
    for (const Term& term : collected) {
        if (term.radius > floor)
            terms_.append(term);
    }

    // Largest vectors first: the chain reads as the dominant rotation followed
    // by the fine correction, which is the conventional epicycle diagram.
    std::sort(terms_.begin(), terms_.end(), [](const Term& left, const Term& right) {
        return left.radius > right.radius;
    });
}

void EpicycleChart::setHarmonics(int count)
{
    const int next = std::clamp(count, 1, kMaximumHarmonics);
    if (harmonics_ == next)
        return;

    harmonics_ = next;
    rebuildHarmonics();
    requestUpdate();
}

int EpicycleChart::harmonics() const noexcept
{
    return harmonics_;
}

void EpicycleChart::setShowCircles(bool show)
{
    if (showCircles_ == show)
        return;

    showCircles_ = show;
    requestUpdate();
}

bool EpicycleChart::showsCircles() const noexcept
{
    return showCircles_;
}

void EpicycleChart::setSpeed(double speed)
{
    if (!std::isfinite(speed))
        return;

    const double next = std::clamp(speed, 0.25, 8.0);
    if (speed_ == next)
        return;

    speed_ = next;
    requestUpdate();
}

double EpicycleChart::speed() const noexcept
{
    return speed_;
}

void EpicycleChart::setUnit(const QString& unit)
{
    if (unit_ == unit)
        return;

    unit_ = unit;
    requestUpdate();
}

QString EpicycleChart::unit() const
{
    return unit_;
}

QVector<qreal> EpicycleChart::epicycleRadii() const
{
    QVector<qreal> radii;
    radii.reserve(terms_.size());
    for (const Term& term : terms_)
        radii.append(term.radius);
    return radii;
}

qreal EpicycleChart::epicycleRadiusSum() const
{
    qreal sum = 0.0;
    for (const Term& term : terms_)
        sum += term.radius;
    return sum;
}

qreal EpicycleChart::radiusBound() const
{
    return std::max({extent_, epicycleRadiusSum(), qreal(1e-9)});
}

QPointF EpicycleChart::centroid() const noexcept
{
    return centroid_;
}

QPointF EpicycleChart::sampleAt(qreal sampleTime) const
{
    if (target_.empty())
        return QPointF();

    const qreal count = static_cast<qreal>(target_.size());
    QPointF result = centroid_;

    for (const Term& term : terms_) {
        const qreal angle = term.phase + kTwoPi * term.frequency * sampleTime / count;
        result += QPointF(term.radius * std::cos(angle), term.radius * std::sin(angle));
    }

    return result;
}

EpicycleChart::Projection EpicycleChart::projectionFor(const QRectF& plot) const
{
    Projection projection;

    if (plot.isEmpty() || target_.empty())
        return projection;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    const qreal half = std::min(plot.width(), plot.height()) / 2.0
        - density.dp(kPlotInsetDp);
    if (!(half > density.dp(kMinimumPlotRadiusDp)))
        return projection;

    projection.valid = true;
    projection.centre = plot.center();
    projection.gridRadius = half;
    projection.scale = half / radiusBound();
    return projection;
}

QPointF EpicycleChart::toPixel(const Projection& projection, const QPointF& point) const
{
    return projection.centre
        + QPointF((point.x() - centroid_.x()) * projection.scale,
                  -(point.y() - centroid_.y()) * projection.scale);
}

QPolygonF EpicycleChart::buildTrace(const Projection& projection, qreal progress) const
{
    QPolygonF path;

    const qreal clamped = std::clamp(progress, 0.0, 1.0);
    if (!projection.valid || clamped <= 0.0005 || target_.size() < 2)
        return path;

    const qreal count = static_cast<qreal>(target_.size());
    const qreal loops = std::max(1.0, speed_);
    const qreal endTime = count * loops * clamped;

    const int perLoop = std::clamp(static_cast<int>(target_.size()) * 4, 96, 1200);
    int samples = static_cast<int>(std::ceil(perLoop * loops * clamped));
    samples = std::clamp(samples, 8, 8192);

    path.reserve(samples + 1);
    for (int index = 0; index <= samples; ++index) {
        const qreal fraction = static_cast<qreal>(index) / static_cast<qreal>(samples);
        path.append(toPixel(projection, sampleAt(endTime * fraction)));
    }

    return path;
}

QPolygonF EpicycleChart::tracePath() const
{
    const Projection projection = projectionFor(plotRect());
    return buildTrace(projection, animation().progress());
}

QPointF EpicycleChart::targetPoint(int index) const
{
    const Projection projection = projectionFor(plotRect());
    if (!projection.valid || index < 0 || index >= static_cast<int>(target_.size()))
        return QPointF();

    return toPixel(projection, target_.at(index));
}

int EpicycleChart::targetPointAt(QPointF position, qreal radius) const
{
    const Projection projection = projectionFor(plotRect());
    if (!projection.valid || target_.empty())
        return -1;
    if (!std::isfinite(position.x()) || !std::isfinite(position.y()))
        return -1;

    const Density& density = Theme::forWidget(this).density();
    const qreal slop = radius > 0.0 ? radius
                                    : std::max<qreal>(density.dp(9.0), 4.0);
    const qreal slopSquared = slop * slop;

    int nearest = -1;
    qreal best = slopSquared;

    for (int index = 0; index < static_cast<int>(target_.size()); ++index) {
        const QPointF pixel = toPixel(projection, target_.at(index));
        const qreal dx = pixel.x() - position.x();
        const qreal dy = pixel.y() - position.y();
        const qreal distance = dx * dx + dy * dy;

        if (distance <= best) {
            best = distance;
            nearest = index;
        }
    }

    return nearest;
}

int EpicycleChart::hoveredTargetPoint() const noexcept
{
    return hoveredPoint_;
}

qreal EpicycleChart::revealProgress() const noexcept
{
    return animation().progress();
}

bool EpicycleChart::isRevealing() const noexcept
{
    return animation().isRunning();
}

void EpicycleChart::setHoveredTargetPoint(int index)
{
    if (hoveredPoint_ == index)
        return;

    hoveredPoint_ = index;
    requestUpdate();
}


void EpicycleChart::paintPlot(QPainter& painter, const QRectF& plot)
{
    paintPlot(painter, plot, 1.0);
}

void EpicycleChart::paintPlot(QPainter& painter, const QRectF& plot, qreal progress)
{
    if (plot.isEmpty() || target_.empty())
        return;

    const PainterStateGuard guard(painter);

    try {
        const qreal stage = std::clamp(progress, 0.0, 1.0);
        const Theme& theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const ColorScheme& colours = theme.colours();
        const Projection projection = projectionFor(plot);
        if (!projection.valid)
            return;

        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setBrush(Qt::NoBrush);

        const QFont labelFont = Font::resolve(this, theme.type().labelSmall, density);
        const QFontMetricsF labelMetrics(labelFont);

        // 1. Radial grid and centre axes. These are deliberately quiet: the
        //    trace is the foreground.
        QColor guide = colours.outlineVariant;
        guide.setAlphaF(static_cast<float>(kGridAlpha));
        painter.setPen(QPen(guide, std::max<qreal>(1.0, density.dp(0.75)), Qt::DashLine));
        for (int ring = 1; ring <= 4; ++ring) {
            const qreal radius = projection.gridRadius * ring / 4.0;
            painter.drawEllipse(projection.centre, radius, radius);
        }
        painter.drawLine(QPointF(projection.centre.x() - projection.gridRadius,
                                 projection.centre.y()),
                         QPointF(projection.centre.x() + projection.gridRadius,
                                 projection.centre.y()));
        painter.drawLine(QPointF(projection.centre.x(),
                                 projection.centre.y() - projection.gridRadius),
                         QPointF(projection.centre.x(),
                                 projection.centre.y() + projection.gridRadius));

        // 2. Scale bar, chosen from a nice data length close to half the grid
        //    radius. It is the chart's explicit spatial reference.
        const qreal dataSpan = projection.gridRadius * 0.55 / projection.scale;
        const qreal barLengthData = niceNumber(dataSpan);
        const qreal barLength = barLengthData * projection.scale;
        if (barLength > density.dp(20.0) && barLength < projection.gridRadius) {
            const qreal left = plot.left() + density.dp(8.0);
            const qreal bottom = plot.bottom() - density.dp(10.0);
            const qreal top = bottom - density.dp(4.0);

            painter.setPen(QPen(colours.onSurfaceVariant, 1.0));
            painter.drawLine(QPointF(left, bottom), QPointF(left + barLength, bottom));
            painter.drawLine(QPointF(left, top), QPointF(left, bottom));
            painter.drawLine(QPointF(left + barLength, top),
                             QPointF(left + barLength, bottom));

            painter.setFont(labelFont);
            const QString text = unit_.isEmpty()
                ? formatValue(barLengthData)
                : QStringLiteral("%1 %2").arg(formatValue(barLengthData), unit_);
            painter.drawText(QRectF(left, bottom - density.dp(4.0) - labelMetrics.height(),
                                    std::max<qreal>(barLength, labelMetrics.horizontalAdvance(text)),
                                    labelMetrics.height()),
                             Qt::AlignLeft | Qt::AlignBottom,
                             labelMetrics.elidedText(text, Qt::ElideRight,
                                                     std::max<qreal>(barLength,
                                                                     labelMetrics.horizontalAdvance(text))));
        }

        // 3. The target curve as a faint dashed guide.
        QPolygonF ghost;
        ghost.reserve(static_cast<qsizetype>(target_.size()));
        for (const QPointF& point : target_)
            ghost.append(toPixel(projection, point));

        QColor ghostColour = colours.outlineVariant;
        ghostColour.setAlphaF(static_cast<float>(kGhostAlpha));
        painter.setPen(QPen(ghostColour, std::max<qreal>(1.0, density.dp(0.75)),
                            Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        if (ghost.size() >= 2)
            painter.drawPolygon(ghost);
        else if (ghost.size() == 1)
            painter.drawEllipse(ghost.first(), 2.5, 2.5);

        // 4. The rotating epicycle chain. Its phase follows the reveal, so the
        //    circles turn and the arms sweep while the trace grows.
        const qreal loops = std::max(1.0, speed_);
        const qreal sampleTime = static_cast<qreal>(target_.size()) * loops * stage;

        if (showCircles_ && stage > 0.001) {
            const QVector<QColor> palette = chartPalette();
            QPointF current = centroid_;

            for (int index = 0; index < terms_.size(); ++index) {
                const Term& term = terms_.at(index);
                const qreal angle = term.phase
                    + kTwoPi * term.frequency * sampleTime / static_cast<qreal>(target_.size());

                const QPointF next = current
                    + QPointF(term.radius * std::cos(angle),
                              term.radius * std::sin(angle));

                QColor colour = palette.isEmpty()
                    ? colours.primary
                    : palette.at(index % palette.size());
                colour.setAlphaF(static_cast<float>(kCircleAlpha));

                const QPointF centrePixel = toPixel(projection, current);
                const qreal radiusPixel = term.radius * projection.scale;
                if (radiusPixel >= 1.0) {
                    painter.setPen(QPen(colour, 1.0));
                    painter.setBrush(Qt::NoBrush);
                    painter.drawEllipse(centrePixel, radiusPixel, radiusPixel);
                }

                QColor arm = colours.onSurfaceVariant;
                arm.setAlphaF(static_cast<float>(kArmAlpha));
                painter.setPen(QPen(arm, 1.0));
                painter.drawLine(centrePixel, toPixel(projection, next));

                painter.setPen(Qt::NoPen);
                painter.setBrush(arm);
                painter.drawEllipse(centrePixel, 1.4, 1.4);

                current = next;
            }
        }

        // 5. The traced path up to the current phase.
        const QPolygonF trace = buildTrace(projection, stage);
        if (trace.size() >= 2) {
            painter.setPen(QPen(colours.primary, std::max<qreal>(1.5, density.dp(2.0)),
                                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawPolyline(trace);
        } else if (trace.size() == 1) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colours.primary);
            painter.drawEllipse(trace.first(), 2.0, 2.0);
        }

        // 6. Hover marker and readout for a target sample.
        if (hoveredPoint_ >= 0 && hoveredPoint_ < static_cast<int>(target_.size())) {
            const QPointF point = toPixel(projection, target_.at(hoveredPoint_));
            painter.setPen(QPen(colours.primary, std::max<qreal>(1.5, density.dp(2.0))));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(point, density.dp(kHoverRadiusDp),
                                density.dp(kHoverRadiusDp));

            const QPointF sample = target_.at(hoveredPoint_);
            const QString text = QStringLiteral("P%1  (%2, %3)%4")
                .arg(hoveredPoint_)
                .arg(formatValue(sample.x()), formatValue(sample.y()))
                .arg(unit_.isEmpty() ? QString() : QStringLiteral("  %1").arg(unit_));

            const qreal inset = density.dp(kReadoutInsetDp);
            const qreal gap = density.dp(kReadoutGapDp);
            const qreal maximumWidth = std::max<qreal>(0.0, plot.width() - 2.0 * gap);
            const QString shown = labelMetrics.elidedText(text, Qt::ElideRight, maximumWidth);
            const qreal boxWidth = std::min(maximumWidth,
                                            labelMetrics.horizontalAdvance(shown) + 2.0 * inset);
            const qreal boxHeight = labelMetrics.height() + 2.0 * inset;

            qreal left = point.x() + gap;
            if (left + boxWidth > plot.right())
                left = point.x() - gap - boxWidth;
            left = std::clamp(left, plot.left(), plot.right() - boxWidth);

            qreal top = point.y() - boxHeight / 2.0;
            top = std::clamp(top, plot.top(), plot.bottom() - boxHeight);

            const QRectF box(left, top, boxWidth, boxHeight);
            painter.setPen(QPen(colours.outlineVariant, 1.0));
            painter.setBrush(colours.surfaceContainerHigh);
            painter.drawRoundedRect(box, inset, inset);

            painter.setFont(labelFont);
            painter.setPen(colours.onSurface);
            painter.drawText(box.adjusted(inset, 0.0, -inset, 0.0),
                             Qt::AlignLeft | Qt::AlignVCenter, shown);
        }

        // 7. Sample/mode caption, bottom-right.
        painter.setFont(labelFont);
        painter.setPen(colours.onSurfaceVariant);
        const QString caption = QStringLiteral("%1 samples  ·  %2 modes")
                                    .arg(sampleCount())
                                    .arg(terms_.size());
        painter.drawText(QRectF(plot.left(), plot.bottom() - labelMetrics.height(),
                                plot.width(), labelMetrics.height()),
                         Qt::AlignRight | Qt::AlignBottom,
                         labelMetrics.elidedText(caption, Qt::ElideRight, plot.width()));
    } catch (const std::exception& error) {
        qWarning("md::EpicycleChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::EpicycleChart::paintPlot failed with an unknown exception");
    }
}

void EpicycleChart::mouseMoveEvent(QMouseEvent* event)
{
    try {
        View::mouseMoveEvent(event);
        hoverPosition_ = event->position();
        setHoveredTargetPoint(targetPointAt(hoverPosition_));
        requestUpdate();
    } catch (const std::exception& error) {
        qWarning("md::EpicycleChart::mouseMoveEvent failed: %s", error.what());
        setHoveredTargetPoint(-1);
    } catch (...) {
        qWarning("md::EpicycleChart::mouseMoveEvent failed with an unknown exception");
        setHoveredTargetPoint(-1);
    }
}

void EpicycleChart::leaveEvent(QEvent* event)
{
    try {
        View::leaveEvent(event);
        setHoveredTargetPoint(-1);
    } catch (const std::exception& error) {
        qWarning("md::EpicycleChart::leaveEvent failed: %s", error.what());
        setHoveredTargetPoint(-1);
    } catch (...) {
        qWarning("md::EpicycleChart::leaveEvent failed with an unknown exception");
        setHoveredTargetPoint(-1);
    }
}

} // namespace md
