// Periodicity — the WaveformChart component.
//
// The paint pass is a single left-to-right traversal of the signal. A short
// signal keeps every sample in a QPainterPath; a long one is reduced to one
// min/max segment per pixel column so the result is dense without becoming
// expensive. The envelope band is painted behind the signal and the RMS curve
// on top of it, all inside one reveal clip.
//
// The transform is installed by ChartBase before paintPlot(), so math()->toPixel
// is the only mapping used here. The axis unit is decided from duration() and
// applied consistently to the data bounds, the labels and samplePoint().

#include <md/components/waveform_chart.h>

#include <md/theme/theme.h>

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace md {
namespace {

constexpr qreal kAmplitudePadding = 1.1;
constexpr qreal kEnvelopeFillAlpha = 0.18;
constexpr qreal kEnvelopeStrokeAlpha = 0.45;
constexpr qreal kZeroLineAlpha = 0.60;
constexpr qreal kSignalWidthDp = 1.4;
constexpr qreal kRmsWidthDp = 1.8;
constexpr int kMaximumHitSeriesPoints = 2048;

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

WaveformChart::WaveformChart(QWidget* parent)
    : ChartBase(parent)
{
    setFocusPolicy(Qt::NoFocus);

    // A waveform is a read-only data surface; the shared interaction layer
    // would tint the trace while the pointer rests on the chart.
    setInteractionFeedbackEnabled(false);

    math()->setYAxisLabel(localized(QStringLiteral("chart.waveform.amplitude"),
                                    QStringLiteral("Amplitude")));
    math()->setTickCount(6);

    updateViewport();
    rebuildSeries();
}

void WaveformChart::setSamples(std::vector<double> samples, double sampleRate)
{
    if (std::isfinite(sampleRate) && sampleRate > 0.0)
        sampleRate_ = sampleRate;

    samples_ = std::move(samples);
    recomputePeak();
    rebuildRms();
    rebuildSeries();
    updateViewport();
    requestUpdate();
}

const std::vector<double>& WaveformChart::samples() const noexcept
{
    return samples_;
}

double WaveformChart::sampleRate() const noexcept
{
    return sampleRate_;
}

double WaveformChart::duration() const noexcept
{
    if (!(sampleRate_ > 0.0) || samples_.empty())
        return 0.0;

    return static_cast<double>(samples_.size()) / sampleRate_;
}

double WaveformChart::peakAmplitude() const noexcept
{
    return peak_;
}

void WaveformChart::setEnvelope(std::vector<double> minima, std::vector<double> maxima)
{
    if (minima.empty() || maxima.empty()) {
        envelopeMinima_.clear();
        envelopeMaxima_.clear();
    } else {
        envelopeMinima_ = std::move(minima);
        envelopeMaxima_ = std::move(maxima);
    }

    recomputePeak();
    rebuildSeries();
    updateViewport();
    requestUpdate();
}

bool WaveformChart::hasEnvelope() const noexcept
{
    return !envelopeMinima_.empty() && !envelopeMaxima_.empty();
}

const std::vector<double>& WaveformChart::envelopeMinima() const noexcept
{
    return envelopeMinima_;
}

const std::vector<double>& WaveformChart::envelopeMaxima() const noexcept
{
    return envelopeMaxima_;
}

void WaveformChart::setShowRms(bool show)
{
    if (showRms_ == show)
        return;

    showRms_ = show;
    rebuildSeries();
    requestUpdate();
}

bool WaveformChart::showsRms() const noexcept
{
    return showRms_;
}

void WaveformChart::setRmsWindow(int samples)
{
    const int window = std::max(1, samples);
    if (rmsWindow_ == window)
        return;

    rmsWindow_ = window;
    rebuildRms();
    requestUpdate();
}

int WaveformChart::rmsWindow() const noexcept
{
    return rmsWindow_;
}

const std::vector<double>& WaveformChart::rmsValues() const noexcept
{
    return rms_;
}

bool WaveformChart::usesMilliseconds() const noexcept
{
    const double span = duration();
    return span > 0.0 && span < 1.0;
}

QVector<qreal> WaveformChart::timeTicks() const
{
    return math()->xTicks();
}

QVector<qreal> WaveformChart::amplitudeTicks() const
{
    return math()->yTicks();
}

QString WaveformChart::timeAxisLabel() const
{
    return math()->xAxisLabel();
}

void WaveformChart::recomputePeak()
{
    double peak = 0.0;

    for (const double value : samples_) {
        if (std::isfinite(value))
            peak = std::max(peak, std::abs(value));
    }

    for (const double value : envelopeMinima_) {
        if (std::isfinite(value))
            peak = std::max(peak, std::abs(value));
    }

    for (const double value : envelopeMaxima_) {
        if (std::isfinite(value))
            peak = std::max(peak, std::abs(value));
    }

    peak_ = peak;
}

void WaveformChart::rebuildRms()
{
    rms_.clear();

    if (samples_.empty() || rmsWindow_ < 1)
        return;

    const qsizetype count = static_cast<qsizetype>(samples_.size());
    rms_.reserve(static_cast<size_t>(
        (samples_.size() + static_cast<size_t>(rmsWindow_) - 1)
        / static_cast<size_t>(rmsWindow_)));

    for (qsizetype start = 0; start < count; start += rmsWindow_) {
        const qsizetype end = std::min<qsizetype>(start + rmsWindow_, count);
        double sum = 0.0;
        qsizetype finite = 0;

        for (qsizetype index = start; index < end; ++index) {
            const double value = samples_.at(static_cast<size_t>(index));
            if (!std::isfinite(value))
                continue;
            sum += value * value;
            ++finite;
        }

        rms_.push_back(finite > 0 ? std::sqrt(sum / static_cast<double>(finite)) : 0.0);
    }
}

void WaveformChart::rebuildSeries()
{
    const double scale = usesMilliseconds() ? 1000.0 : 1.0;

    QVector<ChartSeries> entries;

    if (!samples_.empty()) {
        ChartSeries signal;
        signal.name = QStringLiteral("Signal");

        const qsizetype count = static_cast<qsizetype>(samples_.size());
        const qsizetype stride = std::max<qsizetype>(
            1, count / static_cast<qsizetype>(kMaximumHitSeriesPoints));

        signal.points.reserve(static_cast<qsizetype>(
            (samples_.size() + static_cast<size_t>(stride) - 1)
            / static_cast<size_t>(stride)));

        for (qsizetype index = 0; index < count; index += stride) {
            const double value = samples_.at(static_cast<size_t>(index));
            if (!std::isfinite(value))
                continue;

            const double time = sampleRate_ > 0.0
                ? static_cast<double>(index) / sampleRate_
                : 0.0;
            signal.points.append(QPointF(time * scale, value));
        }

        // An all-NaN signal has no finite point; omitting the series then lets
        // the base show its empty state instead of an empty frame.
        if (!signal.points.isEmpty())
            entries.append(signal);
    }

    if (showRms_ && !rms_.empty()) {
        ChartSeries rms;
        rms.name = QStringLiteral("RMS");
        entries.append(rms);
    }

    if (hasEnvelope()) {
        ChartSeries envelope;
        envelope.name = QStringLiteral("Envelope");
        entries.append(envelope);
    }

    ChartBase::setSeries(std::move(entries));
}

void WaveformChart::updateViewport()
{
    const double span = duration();
    const double scale = usesMilliseconds() ? 1000.0 : 1.0;
    const double maximumX = span > 0.0 ? span * scale : 1.0;
    const double maximumY = peak_ > 0.0 ? peak_ * kAmplitudePadding : 1.0;

    math()->setAutoFit(false);
    math()->setDataBounds(QPointF(0.0, -maximumY), QPointF(maximumX, maximumY));
    math()->setVisibleBounds(QPointF(0.0, -maximumY), QPointF(maximumX, maximumY));

    math()->setXAxisLabel(usesMilliseconds()
        ? localized(QStringLiteral("chart.waveform.time.ms"), QStringLiteral("Time (ms)"))
        : localized(QStringLiteral("chart.waveform.time.s"), QStringLiteral("Time (s)")));
}

QPointF WaveformChart::samplePoint(int index) const
{
    if (index < 0 || index >= static_cast<int>(samples_.size()))
        return QPointF();

    const QRectF plot = plotRect();
    if (plot.isEmpty())
        return QPointF();

    const double time = sampleRate_ > 0.0
        ? static_cast<double>(index) / sampleRate_
        : 0.0;
    const double scale = usesMilliseconds() ? 1000.0 : 1.0;
    const double value = samples_.at(static_cast<size_t>(index));
    if (!std::isfinite(value))
        return QPointF();

    return math()->toPixel(QPointF(time * scale, value));
}

bool WaveformChart::sampleAt(QPointF position, double* time, double* amplitude) const
{
    if (!std::isfinite(position.x()) || !std::isfinite(position.y()))
        return false;
    if (samples_.empty() || !(sampleRate_ > 0.0))
        return false;

    const QRectF plot = plotRect();
    if (plot.isEmpty() || !plot.contains(position))
        return false;

    const double fraction = std::clamp(
        (position.x() - plot.left()) / plot.width(), 0.0, 1.0);
    const double seconds = fraction * duration();
    const int index = std::clamp(
        static_cast<int>(std::lround(seconds * sampleRate_)), 0,
        static_cast<int>(samples_.size()) - 1);

    if (time != nullptr)
        *time = static_cast<double>(index) / sampleRate_;
    if (amplitude != nullptr)
        *amplitude = samples_.at(static_cast<size_t>(index));

    return true;
}

qreal WaveformChart::revealProgress() const noexcept
{
    return animation().progress();
}

bool WaveformChart::isRevealing() const noexcept
{
    return animation().isRunning();
}

void WaveformChart::paintPlot(QPainter& painter, const QRectF& plot)
{
    paintPlot(painter, plot, 1.0);
}

void WaveformChart::paintPlot(QPainter& painter, const QRectF& plot, qreal progress)
{
    if (plot.isEmpty())
        return;

    const PainterStateGuard guard(painter);

    try {
        const Theme& theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const ColorScheme& colours = theme.colours();
        const QVector<QColor> palette = chartPalette();
        const double span = duration();
        const double scale = usesMilliseconds() ? 1000.0 : 1.0;
        const qreal zeroY = math()->toPixel(QPointF(0.0, 0.0)).y();

        // 1. The zero line is part of the frame, so it stays visible for the
        //    whole reveal.
        QColor zeroColour = colours.onSurfaceVariant;
        zeroColour.setAlphaF(static_cast<float>(kZeroLineAlpha));
        painter.setPen(QPen(zeroColour, std::max<qreal>(1.0, density.dp(1.0)),
                            Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(QPointF(plot.left(), zeroY), QPointF(plot.right(), zeroY));

        if (span <= 0.0 || samples_.empty())
            return;

        painter.save();
        const qreal revealed = std::clamp(progress, 0.0, 1.0);
        if (revealed < 1.0) {
            painter.setClipRect(QRectF(plot.left(), plot.top(),
                                       plot.width() * revealed, plot.height()),
                                Qt::IntersectClip);
        }
        painter.setRenderHint(QPainter::Antialiasing, true);

        // 2. Envelope band, behind the signal.
        if (hasEnvelope()) {
            const qsizetype count = std::min<qsizetype>(
                static_cast<qsizetype>(envelopeMinima_.size()),
                static_cast<qsizetype>(envelopeMaxima_.size()));

            if (count >= 2) {
                const QColor envelopeColour = seriesColour(2).isValid()
                    ? seriesColour(2)
                    : (palette.size() > 2 ? palette.at(2) : colours.tertiary);

                QPolygonF band;
                band.reserve(count * 2);

                for (qsizetype index = 0; index < count; ++index) {
                    const double maximum = envelopeMaxima_.at(static_cast<size_t>(index));
                    if (!std::isfinite(maximum))
                        continue;
                    const double time = span * static_cast<double>(index)
                        / static_cast<double>(count - 1);
                    band.append(math()->toPixel(QPointF(time * scale, maximum)));
                }

                for (qsizetype index = count; index-- > 0;) {
                    const double minimum = envelopeMinima_.at(static_cast<size_t>(index));
                    if (!std::isfinite(minimum))
                        continue;
                    const double time = span * static_cast<double>(index)
                        / static_cast<double>(count - 1);
                    band.append(math()->toPixel(QPointF(time * scale, minimum)));
                }

                if (band.size() >= 3) {
                    QColor fill = envelopeColour;
                    fill.setAlphaF(static_cast<float>(kEnvelopeFillAlpha));
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(fill);
                    painter.drawPolygon(band);

                    QColor edge = envelopeColour;
                    edge.setAlphaF(static_cast<float>(kEnvelopeStrokeAlpha));
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(QPen(edge, std::max<qreal>(1.0, density.dp(1.0))));
                    painter.drawPolygon(band);
                }
            }
        }

        // 3. The signal itself. A short signal keeps every sample; a long one
        //    is reduced to one min/max segment per pixel column.
        const QColor signalColour = seriesColour(0).isValid()
            ? seriesColour(0)
            : colours.primary;
        const qsizetype count = static_cast<qsizetype>(samples_.size());
        const int columns = std::max(1, static_cast<int>(std::floor(plot.width())));

        if (count <= static_cast<qsizetype>(std::max(2, columns * 2))) {
            QPainterPath path;
            bool connected = false;

            for (qsizetype index = 0; index < count; ++index) {
                const double value = samples_.at(static_cast<size_t>(index));
                if (!std::isfinite(value)) {
                    connected = false;
                    continue;
                }

                const double time = static_cast<double>(index) / sampleRate_;
                const QPointF point = math()->toPixel(QPointF(time * scale, value));

                if (connected)
                    path.lineTo(point);
                else
                    path.moveTo(point);
                connected = true;
            }

            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(signalColour,
                                std::max<qreal>(1.0, density.dp(kSignalWidthDp)),
                                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(path);
        } else {
            painter.setPen(QPen(signalColour,
                                std::max<qreal>(1.0, density.dp(kSignalWidthDp)),
                                Qt::SolidLine, Qt::RoundCap));

            for (int column = 0; column < columns; ++column) {
                const qsizetype start = static_cast<qsizetype>(
                    static_cast<double>(column) * static_cast<double>(count)
                    / static_cast<double>(columns));
                const qsizetype end = std::min<qsizetype>(
                    count,
                    static_cast<qsizetype>((static_cast<double>(column) + 1.0)
                                           * static_cast<double>(count)
                                           / static_cast<double>(columns)));

                if (end <= start)
                    continue;

                bool found = false;
                double minimum = 0.0;
                double maximum = 0.0;

                for (qsizetype index = start; index < end; ++index) {
                    const double value = samples_.at(static_cast<size_t>(index));
                    if (!std::isfinite(value))
                        continue;

                    if (!found) {
                        minimum = maximum = value;
                        found = true;
                    } else {
                        minimum = std::min(minimum, value);
                        maximum = std::max(maximum, value);
                    }
                }

                if (!found)
                    continue;

                const double time = (static_cast<double>(start) + 0.5) / sampleRate_;
                const qreal x = math()->toPixel(QPointF(time * scale, 0.0)).x();
                const qreal top = math()->toPixel(QPointF(time * scale, maximum)).y();
                const qreal bottom = math()->toPixel(QPointF(time * scale, minimum)).y();
                painter.drawLine(QPointF(x, top), QPointF(x, bottom));
            }
        }

        // 4. RMS curve, in the second palette slot.
        if (showRms_ && !rms_.empty() && sampleRate_ > 0.0) {
            const QColor rmsColour = seriesColour(1).isValid()
                ? seriesColour(1)
                : (palette.size() > 1 ? palette.at(1) : colours.secondary);

            QPainterPath path;
            for (size_t block = 0; block < rms_.size(); ++block) {
                const double time = (static_cast<double>(block) + 0.5)
                    * static_cast<double>(rmsWindow_) / sampleRate_;
                const QPointF point = math()->toPixel(
                    QPointF(time * scale, rms_.at(block)));
                if (block == 0)
                    path.moveTo(point);
                else
                    path.lineTo(point);
            }

            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(rmsColour,
                                std::max<qreal>(1.0, density.dp(kRmsWidthDp)),
                                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(path);
        }

        painter.restore();
    } catch (const std::exception& error) {
        qWarning("md::WaveformChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::WaveformChart::paintPlot failed with an unknown exception");
    }
}

} // namespace md
