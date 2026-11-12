// Periodicity — the HilbertChart component.
//
// The analytic signal is computed here, once per setSignal(), with the direct
// DFT/IDFT pair documented in the header. No FFT library and no mathematics
// module is involved: the transform is a local, self-contained routine.
//
// Painting resolves one theme snapshot per pass: surfaceContainerLow for the two
// panel backgrounds, outlineVariant/outline for the grid and frame, the series
// palette for the trajectory and envelope, tertiary for phase, and
// onSurfaceVariant for labels. No literal colour and every length through
// Density::dp().

#include <md/components/hilbert_chart.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QSizeF>

#include <algorithm>
#include <cmath>
#include <complex>
#include <exception>
#include <limits>
#include <utility>

namespace md {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

constexpr qreal kPreferredWidthDp = 480.0;
constexpr qreal kPreferredHeightDp = 340.0;

constexpr qreal kPanelGapDp = 8.0;
constexpr qreal kPanelInsetDp = 4.0;
constexpr qreal kCaptionHeightDp = 16.0;
constexpr qreal kTickLengthDp = 4.0;
constexpr qreal kLabelGapDp = 3.0;

constexpr qreal kHoverBoxAlpha = 0.92;
constexpr qreal kHoverCrosshairAlpha = 0.7;

constexpr qreal kMinimumPanelWidthDp = 90.0;
constexpr qreal kMinimumPanelHeightDp = 90.0;

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

[[nodiscard]] qreal clamp01(qreal value)
{
    return std::clamp(value, 0.0, 1.0);
}

[[nodiscard]] QColor mixColour(const QColor& from, const QColor& to, qreal fraction)
{
    const qreal t = clamp01(fraction);
    const auto channel = [t](int start, int end) {
        return static_cast<int>(qRound(start + (end - start) * t));
    };

    return QColor(channel(from.red(), to.red()),
                  channel(from.green(), to.green()),
                  channel(from.blue(), to.blue()));
}

[[nodiscard]] QColor rampColour(const QColor& low, const QColor& middle,
                                const QColor& high, qreal fraction)
{
    const qreal t = clamp01(fraction);
    if (t <= 0.5)
        return mixColour(low, middle, t * 2.0);
    return mixColour(middle, high, (t - 0.5) * 2.0);
}

[[nodiscard]] QString formatTick(double value)
{
    if (!std::isfinite(value))
        return QString();
    if (std::abs(value) < 1e-12)
        return QStringLiteral("0");

    const double magnitude = std::abs(value);
    if (magnitude >= 10000.0 || magnitude < 0.01)
        return QString::number(value, 'g', 3);

    QString text = QString::number(value, 'f', magnitude >= 100.0 ? 0 : 2);
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0')))
            text.chop(1);
        if (text.endsWith(QLatin1Char('.')))
            text.chop(1);
    }
    return text;
}

[[nodiscard]] QVector<double> niceTicks(double minimum, double maximum, int count)
{
    QVector<double> ticks;

    if (!std::isfinite(minimum) || !std::isfinite(maximum) || !(maximum > minimum))
        return ticks;

    const int target = std::clamp(count, 2, 12);
    const double rawStep = (maximum - minimum) / static_cast<double>(target - 1);
    if (!(rawStep > 0.0) || !std::isfinite(rawStep))
        return ticks;

    const double exponent = std::floor(std::log10(rawStep));
    const double base = std::pow(10.0, exponent);
    const double normalised = rawStep / base;

    double step = base;
    if (normalised > 5.0)
        step = base * 10.0;
    else if (normalised > 2.0)
        step = base * 5.0;
    else if (normalised > 1.0)
        step = base * 2.0;

    const double epsilon = step * 1e-9;
    double value = std::ceil((minimum - epsilon) / step) * step;

    for (int index = 0; index < 40 && value <= maximum + epsilon; ++index) {
        const double snapped = std::abs(value) < epsilon ? 0.0 : value;
        ticks.append(snapped);
        value += step;
    }

    return ticks;
}

} // namespace

HilbertChart::HilbertChart(QWidget* parent)
    : ChartBase(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setAxesVisible(false);
    setInteractionFeedbackEnabled(false);
    setLegendVisible(false);

    // The shared chart animation owns the reveal; both panels stage their
    // curves from its progress.
    animation().setDuration(MotionLevel::Level2);
}

HilbertChart::~HilbertChart() = default;

void HilbertChart::setSignal(std::vector<double> x)
{
    signal_ = std::move(x);
    computeAnalyticSignal();
    syncPresenceSeries();
    requestUpdate();
}

const std::vector<double>& HilbertChart::signal() const noexcept
{
    return signal_;
}

const std::vector<double>& HilbertChart::quadrature() const noexcept
{
    return quadrature_;
}

const std::vector<double>& HilbertChart::envelope() const noexcept
{
    return envelope_;
}

const std::vector<double>& HilbertChart::phase() const noexcept
{
    return phase_;
}

bool HilbertChart::hasAnalyticData() const noexcept
{
    return signal_.size() >= 4 && quadrature_.size() == signal_.size()
        && envelope_.size() == signal_.size() && phase_.size() == signal_.size();
}

bool HilbertChart::hasSignal() const noexcept
{
    return hasAnalyticData();
}

void HilbertChart::setSampleInterval(double interval)
{
    const double next = std::isfinite(interval) && interval > 0.0 ? interval : 1.0;
    if (sampleInterval_ == next)
        return;
    sampleInterval_ = next;
    requestUpdate();
}

double HilbertChart::sampleInterval() const noexcept
{
    return sampleInterval_;
}

void HilbertChart::setMode(HilbertMode mode)
{
    if (mode_ == mode)
        return;
    mode_ = mode;
    requestUpdate();
}

HilbertMode HilbertChart::mode() const noexcept
{
    return mode_;
}

void HilbertChart::computeAnalyticSignal()
{
    quadrature_.clear();
    envelope_.clear();
    phase_.clear();
    phaseMinimum_ = 0.0;
    phaseMaximum_ = 0.0;

    const int count = static_cast<int>(signal_.size());
    if (count < 4)
        return;

    const std::size_t n = static_cast<std::size_t>(count);

    // Forward DFT: X[k] = Σ x[i]·e^(-j2πki/N). The twiddle factor is advanced
    // by one complex multiplication per sample instead of a cos/sin pair, so
    // the O(N²) pass stays cheap for the intended signal sizes.
    std::vector<std::complex<double>> spectrum(n);
    for (std::size_t k = 0; k < n; ++k) {
        const double stepAngle = -kTwoPi * static_cast<double>(k)
            / static_cast<double>(n);
        const std::complex<double> step(std::cos(stepAngle), std::sin(stepAngle));
        std::complex<double> twiddle(1.0, 0.0);
        std::complex<double> sum(0.0, 0.0);
        for (std::size_t index = 0; index < n; ++index) {
            sum += signal_.at(index) * twiddle;
            twiddle *= step;
        }
        spectrum[k] = sum;
    }

    // Analytic spectrum: DC and Nyquist stay real; positive frequencies are
    // doubled; negative frequencies are removed.
    std::vector<std::complex<double>> analytic(n);
    for (std::size_t k = 0; k < n; ++k) {
        if (k == 0) {
            analytic[k] = spectrum[k];
        } else if (n % 2 == 0 && k == n / 2) {
            analytic[k] = spectrum[k];
        } else if (k < (n + 1) / 2) {
            analytic[k] = 2.0 * spectrum[k];
        } else {
            analytic[k] = std::complex<double>(0.0, 0.0);
        }
    }

    // Inverse DFT: z[i] = (1/N)·Σ Z[k]·e^(+j2πki/N).
    quadrature_.reserve(n);
    envelope_.reserve(n);
    for (std::size_t index = 0; index < n; ++index) {
        const double stepAngle = kTwoPi * static_cast<double>(index)
            / static_cast<double>(n);
        const std::complex<double> step(std::cos(stepAngle), std::sin(stepAngle));
        std::complex<double> twiddle(1.0, 0.0);
        std::complex<double> sum(0.0, 0.0);
        for (std::size_t k = 0; k < n; ++k) {
            sum += analytic[k] * twiddle;
            twiddle *= step;
        }

        const std::complex<double> z = sum / static_cast<double>(n);
        quadrature_.push_back(z.imag());
        envelope_.push_back(std::abs(z));
    }

    // Unwrap the principal phase so a rotating signal becomes a straight line
    // instead of a sawtooth.
    phase_.reserve(n);
    double previous = std::atan2(quadrature_.at(0), signal_.at(0));
    phase_.push_back(previous);

    for (std::size_t index = 1; index < n; ++index) {
        const double raw = std::atan2(quadrature_.at(index), signal_.at(index));
        double delta = raw - previous;
        while (delta > kPi)
            delta -= kTwoPi;
        while (delta <= -kPi)
            delta += kTwoPi;
        phase_.push_back(phase_.back() + delta);
        previous = raw;
    }

    const auto bounds = std::minmax_element(phase_.begin(), phase_.end());
    if (bounds.first != phase_.end()) {
        phaseMinimum_ = *bounds.first;
        phaseMaximum_ = *bounds.second;
    }
    if (!(phaseMaximum_ - phaseMinimum_ > 1e-9)) {
        phaseMinimum_ -= 0.5;
        phaseMaximum_ += 0.5;
    }
}

double HilbertChart::maximumEnvelope() const
{
    double maximum = 0.0;
    for (double value : envelope_)
        maximum = std::max(maximum, value);
    return maximum;
}

double HilbertChart::maximumQuadratureAmplitude() const
{
    double maximum = maximumEnvelope();

    for (double value : signal_)
        maximum = std::max(maximum, std::abs(value));
    for (double value : quadrature_)
        maximum = std::max(maximum, std::abs(value));

    return maximum > 0.0 ? maximum * 1.1 : 1.0;
}

double HilbertChart::phaseMinimum() const
{
    const double span = phaseMaximum_ - phaseMinimum_;
    return phaseMinimum_ - span * 0.06;
}

double HilbertChart::phaseMaximum() const
{
    const double span = phaseMaximum_ - phaseMinimum_;
    return phaseMaximum_ + span * 0.06;
}

QVector<double> HilbertChart::timeTicks(int count) const
{
    if (signal_.size() < 2)
        return {};
    const double last = static_cast<double>(signal_.size() - 1) * sampleInterval_;
    return niceTicks(0.0, last, count);
}

QVector<double> HilbertChart::amplitudeTicks(int count) const
{
    const double maximum = maximumEnvelope();
    const double top = maximum > 0.0 ? maximum * 1.15 : 1.0;
    return niceTicks(0.0, top, count);
}

QVector<double> HilbertChart::phaseTicks(int count) const
{
    if (!hasAnalyticData())
        return {};
    return niceTicks(phaseMinimum(), phaseMaximum(), count);
}

QColor HilbertChart::colourForPhase(double phase) const
{
    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();

    if (!std::isfinite(phase))
        return colours.tertiary;

    const double minimum = phaseMinimum();
    const double maximum = phaseMaximum();
    const double span = maximum - minimum;

    qreal fraction = 0.5;
    if (span > 0.0)
        fraction = static_cast<qreal>((phase - minimum) / span);

    return rampColour(colours.primary, colours.tertiary, colours.secondary,
                      clamp01(fraction));
}

HilbertChart::Layout HilbertChart::layoutFor(const QRectF& plot) const
{
    Layout layout;
    if (plot.isEmpty() || plot.width() <= 0.0 || plot.height() <= 0.0)
        return layout;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const qreal gap = density.dp(kPanelGapDp);
    const qreal inset = density.dp(kPanelInsetDp);
    const qreal caption = density.dp(kCaptionHeightDp);
    const qreal tickLength = density.dp(kTickLengthDp);
    const qreal labelGap = density.dp(kLabelGapDp);

    const QFont tickFont = Font::resolve(this, theme.type().labelSmall, density);
    const QFontMetricsF tickMetrics(tickFont);
    const QFont axisFont = Font::resolve(this, theme.type().labelMedium, density);
    const QFontMetricsF axisMetrics(axisFont);

    // A phase-colour bar on the right repeats the portrait's colour mapping.
    QRectF content = plot;
    if (colourBarVisible_) {
        const qreal barWidth = density.dp(10.0);
        const qreal labelGutter = density.dp(32.0);
        const qreal reserve = barWidth + gap + labelGutter;
        if (content.width() > reserve + density.dp(240.0)) {
            layout.colourBar = QRectF(content.right() - labelGutter - barWidth,
                                      content.top() + caption,
                                      barWidth,
                                      std::max<qreal>(0.0, content.height() - caption));
            content.setRight(layout.colourBar.left() - gap - labelGutter);
        }
    }

    const qreal usableWidth = content.width() - gap;
    if (usableWidth <= 0.0 || content.height() <= caption + tickMetrics.height() + 20.0)
        return layout;

    // The emphasized panel (the mode's own view) takes the larger share; the
    // other panel is always painted as the supporting view.
    const qreal mainFraction = 0.58;
    const qreal mainWidth = usableWidth * mainFraction;
    const qreal sideWidth = usableWidth - mainWidth;

    QRectF mainBox(content.left(), content.top(), mainWidth, content.height());
    QRectF sideBox(content.left() + mainWidth + gap, content.top(), sideWidth, content.height());

    QRectF quadratureBox = mode_ == HilbertMode::Quadrature ? mainBox : sideBox;
    QRectF envelopeBox = mode_ == HilbertMode::Quadrature ? sideBox : mainBox;

    layout.quadratureBox = quadratureBox;
    layout.envelopeBox = envelopeBox;

    // Phase portrait: square data area, tick labels below and left.
    qreal widestY = 0.0;
    for (double value : niceTicks(-maximumQuadratureAmplitude(),
                                  maximumQuadratureAmplitude(), 5)) {
        widestY = std::max(widestY, tickMetrics.horizontalAdvance(formatTick(value)));
    }
    const qreal quadLeft = widestY + tickLength + labelGap + axisMetrics.height() + 2.0;
    const qreal quadBottom = tickMetrics.height() + tickLength + labelGap
        + axisMetrics.height() + 2.0;

    QRectF quadAvailable = quadratureBox.adjusted(quadLeft, caption, -inset, -quadBottom);
    if (quadAvailable.width() >= density.dp(kMinimumPanelWidthDp)
        && quadAvailable.height() >= density.dp(kMinimumPanelHeightDp)) {
        const qreal side = std::min(quadAvailable.width(), quadAvailable.height());
        layout.quadrature = QRectF(quadAvailable.left() + (quadAvailable.width() - side) / 2.0,
                                   quadAvailable.top() + (quadAvailable.height() - side) / 2.0,
                                   side, side);
    }

    // Envelope panel: amplitude labels on the left, phase labels on the right.
    qreal widestAmplitude = 0.0;
    for (double value : amplitudeTicks())
        widestAmplitude = std::max(widestAmplitude, tickMetrics.horizontalAdvance(formatTick(value)));

    const qreal envLeft = widestAmplitude + tickLength + labelGap + axisMetrics.height() + 2.0;
    const qreal envRight = axisMetrics.height() + tickLength + labelGap + 2.0;
    const qreal envBottom = tickMetrics.height() + tickLength + labelGap
        + axisMetrics.height() + 2.0;

    QRectF envAvailable = envelopeBox.adjusted(envLeft, caption, -envRight, -envBottom);
    if (envAvailable.width() >= density.dp(kMinimumPanelWidthDp)
        && envAvailable.height() >= density.dp(kMinimumPanelHeightDp))
        layout.envelope = envAvailable;

    if (layout.quadrature.width() < 20.0)
        layout.quadrature = QRectF();
    if (layout.envelope.width() < 20.0)
        layout.envelope = QRectF();

    return layout;
}

QRectF HilbertChart::quadratureRect() const
{
    return layoutFor(plotRect()).quadrature;
}

QRectF HilbertChart::envelopeRect() const
{
    return layoutFor(plotRect()).envelope;
}

QRectF HilbertChart::colourBarRect() const
{
    return layoutFor(plotRect()).colourBar;
}

void HilbertChart::setColourBarVisible(bool visible)
{
    if (colourBarVisible_ == visible)
        return;
    colourBarVisible_ = visible;
    requestUpdate();
}

bool HilbertChart::colourBarVisible() const noexcept
{
    return colourBarVisible_;
}

QPointF HilbertChart::toQuadraturePixel(double x, double quadrature) const
{
    const QRectF rect = quadratureRect();
    if (rect.isEmpty() || !std::isfinite(x) || !std::isfinite(quadrature))
        return QPointF();

    const double amplitude = maximumQuadratureAmplitude();
    if (!(amplitude > 0.0))
        return QPointF();

    const qreal scale = rect.width() / (2.0 * static_cast<qreal>(amplitude));
    return QPointF(rect.center().x() + static_cast<qreal>(x) * scale,
                   rect.center().y() - static_cast<qreal>(quadrature) * scale);
}

QPointF HilbertChart::toEnvelopePixel(double time, double envelope) const
{
    const QRectF rect = envelopeRect();
    if (rect.isEmpty() || !std::isfinite(time) || !std::isfinite(envelope))
        return QPointF();
    if (signal_.size() < 2)
        return QPointF();

    const double lastTime = static_cast<double>(signal_.size() - 1) * sampleInterval_;
    if (!(lastTime > 0.0))
        return QPointF();

    const double top = maximumEnvelope() * 1.15;
    if (!(top > 0.0))
        return QPointF();

    const qreal xFraction = static_cast<qreal>(time / lastTime);
    const qreal yFraction = static_cast<qreal>(envelope / top);
    return QPointF(rect.left() + xFraction * rect.width(),
                   rect.bottom() - yFraction * rect.height());
}

HilbertSample HilbertChart::sampleAt(QPointF position) const
{
    HilbertSample sample;
    if (!hasAnalyticData())
        return sample;

    const Layout layout = layoutFor(plotRect());
    const int count = static_cast<int>(signal_.size());

    const auto fill = [&](int index) {
        if (index < 0 || index >= count)
            return;
        sample.valid = true;
        sample.index = index;
        sample.time = static_cast<double>(index) * sampleInterval_;
        sample.value = signal_.at(static_cast<std::size_t>(index));
        sample.quadrature = quadrature_.at(static_cast<std::size_t>(index));
        sample.envelope = envelope_.at(static_cast<std::size_t>(index));
        sample.phase = phase_.at(static_cast<std::size_t>(index));
    };

    if (!layout.quadrature.isEmpty() && layout.quadrature.contains(position)) {
        const double amplitude = maximumQuadratureAmplitude();
        if (!(amplitude > 0.0))
            return sample;

        const qreal scale = layout.quadrature.width() / (2.0 * static_cast<qreal>(amplitude));
        const qreal centreX = layout.quadrature.center().x();
        const qreal centreY = layout.quadrature.center().y();

        int nearest = -1;
        qreal best = std::numeric_limits<qreal>::max();
        for (int index = 0; index < count; ++index) {
            const qreal x = centreX + static_cast<qreal>(signal_.at(static_cast<std::size_t>(index))) * scale;
            const qreal y = centreY - static_cast<qreal>(quadrature_.at(static_cast<std::size_t>(index))) * scale;
            const qreal dx = x - position.x();
            const qreal dy = y - position.y();
            const qreal distance = dx * dx + dy * dy;
            if (distance < best) {
                best = distance;
                nearest = index;
            }
        }
        fill(nearest);
        return sample;
    }

    if (!layout.envelope.isEmpty() && layout.envelope.contains(position)) {
        const qreal fraction = (position.x() - layout.envelope.left())
            / layout.envelope.width();
        const int index = std::clamp(
            static_cast<int>(std::lround(static_cast<double>(fraction)
                                         * static_cast<double>(count - 1))),
            0, count - 1);
        fill(index);
        return sample;
    }

    return sample;
}

void HilbertChart::syncPresenceSeries()
{
    // Two unnamed series keep seriesColour(0) and seriesColour(1) valid for the
    // trajectory and envelope colours; neither point is ever painted.
    QVector<ChartSeries> presence;
    if (hasAnalyticData()) {
        ChartSeries first;
        first.points.append(QPointF(0.0, 0.0));
        presence.append(first);

        ChartSeries second;
        second.points.append(QPointF(0.0, 1.0));
        presence.append(second);
    }
    ChartBase::setSeries(std::move(presence));
}

void HilbertChart::measureOverride(MeasureSpec width, MeasureSpec height)
{
    try {
        const Density& density = Theme::forWidget(this).density();
        setMeasuredSize(QSizeF(width.resolve(density.dp(kPreferredWidthDp)),
                               height.resolve(density.dp(kPreferredHeightDp))));
    } catch (const std::exception& error) {
        qWarning("md::HilbertChart::measureOverride failed: %s", error.what());
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    } catch (...) {
        qWarning("md::HilbertChart::measureOverride failed with an unknown exception");
        setMeasuredSize(QSizeF(width.resolve(0.0), height.resolve(0.0)));
    }
}

void HilbertChart::paintPlot(QPainter& painter, const QRectF& plot, qreal progress)
{
    if (plot.isEmpty())
        return;

    const qreal reveal = clamp01(progress);
    if (reveal <= 0.0)
        return;

    const PainterStateGuard guard(painter);

    try {
        const Layout layout = layoutFor(plot);
        if (layout.quadrature.isEmpty() && layout.envelope.isEmpty())
            return;

        const Theme& theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const ColorScheme& colours = theme.colours();

        // Panel backgrounds first, so the grid and curves sit on a container
        // tone rather than directly on the chart surface.
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(colours.outlineVariant, std::max<qreal>(1.0, density.dp(1.0))));
        painter.setBrush(colours.surfaceContainerLow);

        if (!layout.quadratureBox.isEmpty())
            painter.drawRoundedRect(layout.quadratureBox, 4.0, 4.0);
        if (!layout.envelopeBox.isEmpty())
            painter.drawRoundedRect(layout.envelopeBox, 4.0, 4.0);

        paintQuadrature(painter, layout.quadrature, reveal);
        paintEnvelope(painter, layout.envelope, reveal);

        paintPanelCaption(painter, layout.quadratureBox,
                          localized(QStringLiteral("chart.hilbert.quadratureCaption"),
                                    QStringLiteral("q(t) vs x(t)")));
        paintPanelCaption(painter, layout.envelopeBox,
                          localized(QStringLiteral("chart.hilbert.envelopeCaption"),
                                    QStringLiteral("|z(t)| and φ(t)")));

        if (!layout.colourBar.isEmpty())
            paintColourBar(painter, layout.colourBar, reveal);

        paintHover(painter, layout);
    } catch (const std::exception& error) {
        qWarning("md::HilbertChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::HilbertChart::paintPlot failed with an unknown exception");
    }
}

void HilbertChart::paintPanelCaption(QPainter& painter, const QRectF& box, const QString& text)
{
    if (box.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFont font = Font::resolve(this, theme.type().labelSmall, density);
    const QFontMetricsF metrics(font);

    painter.save();
    painter.setFont(font);
    painter.setPen(theme.colours().onSurfaceVariant);
    painter.drawText(QRectF(box.left() + density.dp(4.0), box.top() + density.dp(3.0),
                            box.width() - density.dp(8.0), metrics.height()),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     metrics.elidedText(text, Qt::ElideRight, box.width() - density.dp(8.0)));
    painter.restore();
}

void HilbertChart::paintQuadrature(QPainter& painter, const QRectF& rect, qreal progress)
{
    if (rect.isEmpty() || !hasAnalyticData())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const QFont tickFont = Font::resolve(this, theme.type().labelSmall, density);
    const QFontMetricsF tickMetrics(tickFont);

    const double amplitude = maximumQuadratureAmplitude();
    if (!(amplitude > 0.0))
        return;

    const qreal scale = rect.width() / (2.0 * static_cast<qreal>(amplitude));
    const QPointF centre = rect.center();
    const auto pixelFor = [&](int index) {
        return QPointF(centre.x() + static_cast<qreal>(signal_.at(static_cast<std::size_t>(index))) * scale,
                       centre.y() - static_cast<qreal>(quadrature_.at(static_cast<std::size_t>(index))) * scale);
    };

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(rect.adjusted(-1.0, -1.0, 1.0, 1.0));

    // Grid and zero axes.
    QColor grid = colours.outlineVariant;
    grid.setAlphaF(0.55f);
    painter.setPen(QPen(grid, std::max<qreal>(0.75, density.dp(1.0)), Qt::DotLine));
    const QVector<double> ticks = niceTicks(-amplitude, amplitude, 5);
    for (double value : ticks) {
        const qreal x = centre.x() + static_cast<qreal>(value) * scale;
        const qreal y = centre.y() - static_cast<qreal>(value) * scale;
        painter.drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
        painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
    }

    QColor axis = colours.outline;
    axis.setAlphaF(0.85f);
    painter.setPen(QPen(axis, std::max<qreal>(1.0, density.dp(1.0))));
    painter.drawLine(QPointF(centre.x(), rect.top()), QPointF(centre.x(), rect.bottom()));
    painter.drawLine(QPointF(rect.left(), centre.y()), QPointF(rect.right(), centre.y()));

    // The phase portrait, coloured by the instantaneous phase. The reveal
    // budget maps to the number of samples already traced.
    const int count = static_cast<int>(signal_.size());
    const int visible = std::max(2, static_cast<int>(
        std::ceil(static_cast<double>(count) * static_cast<double>(clamp01(progress)))));

    painter.setBrush(Qt::NoBrush);
    for (int index = 0; index + 1 < visible; ++index) {
        QColor colour = colourForPhase(phase_.at(static_cast<std::size_t>(index)));
        colour.setAlphaF(0.9f);
        painter.setPen(QPen(colour, std::max<qreal>(1.4, density.dp(1.4)),
                            Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(pixelFor(index), pixelFor(index + 1));
    }

    // The newest sample of the traced part.
    if (visible >= 2) {
        QColor head = seriesColour(1);
        if (!head.isValid())
            head = colours.tertiary;
        painter.setPen(Qt::NoPen);
        painter.setBrush(head);
        painter.drawEllipse(pixelFor(visible - 1), density.dp(3.0), density.dp(3.0));
    }

    painter.setClipRect(rect);

    // Frame and tick labels.
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(colours.outlineVariant, std::max<qreal>(1.0, density.dp(1.0))));
    painter.drawRect(rect);

    painter.setFont(tickFont);
    painter.setPen(colours.onSurfaceVariant);

    for (double value : ticks) {
        if (std::abs(value) < 1e-12)
            continue;
        const qreal x = centre.x() + static_cast<qreal>(value) * scale;
        const qreal y = centre.y() - static_cast<qreal>(value) * scale;
        painter.drawLine(QPointF(x, rect.bottom()),
                         QPointF(x, rect.bottom() + density.dp(kTickLengthDp)));
        painter.drawText(QRectF(x - 20.0, rect.bottom() + density.dp(kTickLengthDp) + 1.0,
                                40.0, tickMetrics.height()),
                         Qt::AlignHCenter | Qt::AlignTop, formatTick(value));
        painter.drawLine(QPointF(rect.left() - density.dp(kTickLengthDp), y),
                         QPointF(rect.left(), y));
        painter.drawText(QRectF(rect.left() - density.dp(kTickLengthDp) - density.dp(kLabelGapDp) - 44.0,
                                y - tickMetrics.height() / 2.0, 44.0, tickMetrics.height()),
                         Qt::AlignRight | Qt::AlignVCenter, formatTick(value));
    }

    // Axis captions, rotated against the left edge and centred below.
    const QFont axisFont = Font::resolve(this, theme.type().labelMedium, density);
    painter.setFont(axisFont);
    painter.setPen(colours.onSurfaceVariant);
    painter.drawText(QRectF(rect.left(), rect.bottom() + density.dp(kTickLengthDp)
                                + tickMetrics.height() + 1.0,
                            rect.width(), density.dp(14.0)),
                     Qt::AlignHCenter | Qt::AlignTop,
                     localized(QStringLiteral("chart.hilbert.xAxis"), QStringLiteral("x(t)")));

    painter.save();
    painter.translate(rect.left() - density.dp(6.0), rect.center().y());
    painter.rotate(-90.0);
    painter.drawText(QRectF(-40.0, -density.dp(14.0) / 2.0, 80.0, density.dp(14.0)),
                     Qt::AlignCenter,
                     localized(QStringLiteral("chart.hilbert.qAxis"), QStringLiteral("q(t)")));
    painter.restore();

    painter.restore();
}

void HilbertChart::paintEnvelope(QPainter& painter, const QRectF& rect, qreal progress)
{
    if (rect.isEmpty() || !hasAnalyticData())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const QFont tickFont = Font::resolve(this, theme.type().labelSmall, density);
    const QFontMetricsF tickMetrics(tickFont);

    const int count = static_cast<int>(signal_.size());
    const double lastTime = static_cast<double>(count - 1) * sampleInterval_;
    const double top = maximumEnvelope() * 1.15;
    if (!(lastTime > 0.0) || !(top > 0.0))
        return;

    const auto xForIndex = [&](int index) {
        return rect.left() + static_cast<qreal>(static_cast<double>(index)
                                               / static_cast<double>(count - 1))
            * rect.width();
    };
    const auto yForEnvelope = [&](double value) {
        return rect.bottom() - static_cast<qreal>(value / top) * rect.height();
    };
    const auto yForPhase = [&](double value) {
        const double minimum = phaseMinimum();
        const double maximum = phaseMaximum();
        const double span = maximum - minimum;
        const double fraction = span > 0.0 ? (value - minimum) / span : 0.5;
        return rect.bottom() - static_cast<qreal>(fraction) * rect.height();
    };

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(rect);

    // Grid: amplitude ticks horizontal, time ticks vertical.
    QColor grid = colours.outlineVariant;
    grid.setAlphaF(0.55f);
    painter.setPen(QPen(grid, std::max<qreal>(0.75, density.dp(1.0)), Qt::DotLine));
    for (double value : amplitudeTicks()) {
        const qreal y = rect.bottom() - static_cast<qreal>(value / top) * rect.height();
        painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
    }
    for (double value : timeTicks()) {
        const qreal x = rect.left() + static_cast<qreal>(value / lastTime) * rect.width();
        painter.drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
    }

    const int visible = std::max(2, static_cast<int>(
        std::ceil(static_cast<double>(count) * static_cast<double>(clamp01(progress)))));

    // The envelope area and its outline.
    QColor fill = seriesColour(0);
    if (!fill.isValid())
        fill = colours.primary;
    fill.setAlphaF(0.20f);

    QPainterPath envelopePath;
    envelopePath.moveTo(xForIndex(0), rect.bottom());
    for (int index = 0; index < visible; ++index) {
        envelopePath.lineTo(xForIndex(index),
                            yForEnvelope(envelope_.at(static_cast<std::size_t>(index))));
    }
    envelopePath.lineTo(xForIndex(visible - 1), rect.bottom());
    envelopePath.closeSubpath();

    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawPath(envelopePath);

    QColor line = seriesColour(0);
    if (!line.isValid())
        line = colours.primary;
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(line, std::max<qreal>(1.5, density.dp(1.5))));

    QPolygonF outlinePoints;
    for (int index = 0; index < visible; ++index) {
        outlinePoints.append(QPointF(xForIndex(index),
                                     yForEnvelope(envelope_.at(static_cast<std::size_t>(index)))));
    }
    painter.drawPolyline(outlinePoints);

    // The unwrapped phase on the secondary axis.
    QColor phaseColour = colours.secondary;
    painter.setPen(QPen(phaseColour, std::max<qreal>(1.2, density.dp(1.2)),
                        Qt::SolidLine, Qt::RoundCap));
    QPolygonF phasePoints;
    for (int index = 0; index < visible; ++index) {
        phasePoints.append(QPointF(xForIndex(index),
                                   yForPhase(phase_.at(static_cast<std::size_t>(index)))));
    }
    painter.drawPolyline(phasePoints);

    // Frame and ticks.
    painter.setClipRect(rect.adjusted(-1.0, -1.0, 1.0, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(colours.outlineVariant, std::max<qreal>(1.0, density.dp(1.0))));
    painter.drawRect(rect);

    painter.setFont(tickFont);
    painter.setPen(colours.onSurfaceVariant);

    for (double value : amplitudeTicks()) {
        const qreal y = rect.bottom() - static_cast<qreal>(value / top) * rect.height();
        painter.drawLine(QPointF(rect.left() - density.dp(kTickLengthDp), y),
                         QPointF(rect.left(), y));
        painter.drawText(QRectF(rect.left() - density.dp(kTickLengthDp) - density.dp(kLabelGapDp) - 44.0,
                                y - tickMetrics.height() / 2.0, 44.0, tickMetrics.height()),
                         Qt::AlignRight | Qt::AlignVCenter, formatTick(value));
    }

    for (double value : timeTicks()) {
        const qreal x = rect.left() + static_cast<qreal>(value / lastTime) * rect.width();
        painter.drawLine(QPointF(x, rect.bottom()),
                         QPointF(x, rect.bottom() + density.dp(kTickLengthDp)));
        painter.drawText(QRectF(x - 24.0, rect.bottom() + density.dp(kTickLengthDp) + 1.0,
                                48.0, tickMetrics.height()),
                         Qt::AlignHCenter | Qt::AlignTop, formatTick(value));
    }

    painter.setPen(phaseColour);
    for (double value : phaseTicks()) {
        const qreal y = yForPhase(value);
        painter.drawLine(QPointF(rect.right(), y),
                         QPointF(rect.right() + density.dp(kTickLengthDp), y));
        painter.drawText(QRectF(rect.right() + density.dp(kTickLengthDp) + density.dp(kLabelGapDp),
                                y - tickMetrics.height() / 2.0, 36.0, tickMetrics.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, formatTick(value));
    }

    // Captions.
    const QFont axisFont = Font::resolve(this, theme.type().labelMedium, density);
    painter.setFont(axisFont);
    painter.setPen(colours.onSurfaceVariant);
    const qreal captionTop = rect.bottom() + density.dp(kTickLengthDp)
        + tickMetrics.height() + 1.0;

    painter.drawText(QRectF(rect.left(), captionTop, rect.width(), density.dp(14.0)),
                     Qt::AlignHCenter | Qt::AlignTop,
                     localized(QStringLiteral("chart.hilbert.timeAxis"), QStringLiteral("t")));

    painter.save();
    painter.translate(rect.left() - density.dp(6.0), rect.top() + rect.height() * 0.3);
    painter.rotate(-90.0);
    painter.drawText(QRectF(-50.0, -density.dp(14.0) / 2.0, 100.0, density.dp(14.0)),
                     Qt::AlignCenter,
                     localized(QStringLiteral("chart.hilbert.envelopeAxis"),
                               QStringLiteral("|z|")));
    painter.restore();

    painter.save();
    painter.setFont(axisFont);
    painter.setPen(phaseColour);
    painter.translate(rect.right() + density.dp(26.0), rect.top() + rect.height() * 0.5);
    painter.rotate(90.0);
    painter.drawText(QRectF(-40.0, -density.dp(14.0) / 2.0, 80.0, density.dp(14.0)),
                     Qt::AlignCenter,
                     localized(QStringLiteral("chart.hilbert.phaseAxis"),
                               QStringLiteral("φ")));
    painter.restore();

    painter.restore();
}

void HilbertChart::paintColourBar(QPainter& painter, const QRectF& bar, qreal progress)
{
    if (bar.isEmpty() || !hasAnalyticData())
        return;

    const qreal reveal = clamp01(progress);
    if (reveal <= 0.0)
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    const double minimum = phaseMinimum();
    const double maximum = phaseMaximum();
    const double span = maximum - minimum;

    QLinearGradient gradient(bar.topLeft(), bar.bottomLeft());
    const int stops = 5;
    for (int index = 0; index < stops; ++index) {
        const qreal fraction = static_cast<qreal>(index) / static_cast<qreal>(stops - 1);
        gradient.setColorAt(fraction,
                            colourForPhase(maximum - span * static_cast<double>(fraction)));
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF visible(bar.left(), bar.bottom() - bar.height() * reveal,
                         bar.width(), bar.height() * reveal);
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRect(visible);

    painter.setPen(QPen(colours.outlineVariant, std::max<qreal>(1.0, density.dp(1.0))));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(bar);

    painter.setFont(Font::resolve(this, theme.type().labelSmall, density));
    painter.setPen(colours.onSurfaceVariant);

    for (int index = 0; index < stops; ++index) {
        const qreal fraction = static_cast<qreal>(index) / static_cast<qreal>(stops - 1);
        const qreal y = bar.top() + fraction * bar.height();
        const double value = maximum - span * static_cast<double>(fraction);
        const QRectF label(bar.right() + density.dp(4.0), y - 8.0,
                           density.dp(30.0), 16.0);
        painter.drawText(label, Qt::AlignLeft | Qt::AlignVCenter, formatTick(value));
    }

    painter.drawText(QRectF(bar.left(), bar.top() - density.dp(14.0),
                            bar.width() + density.dp(34.0), density.dp(14.0)),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     localized(QStringLiteral("chart.hilbert.phaseLabel"),
                               QStringLiteral("φ")));

    painter.restore();
}
void HilbertChart::paintHover(QPainter& painter, const Layout& layout)
{
    if (!hovering_)
        return;

    const QRectF panel = layout.quadrature.contains(hoverPosition_)
        ? layout.quadrature
        : (layout.envelope.contains(hoverPosition_) ? layout.envelope : QRectF());
    if (panel.isEmpty())
        return;

    const HilbertSample sample = sampleAt(hoverPosition_);
    if (!sample.valid)
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    const QRectF quadrature = layout.quadrature;
    const QRectF envelope = layout.envelope;
    const QPointF marker = !quadrature.isEmpty() && quadrature.contains(hoverPosition_)
        ? toQuadraturePixel(sample.value, sample.quadrature)
        : toEnvelopePixel(sample.time, sample.envelope);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(panel);

    QColor crosshair = colours.onSurface;
    crosshair.setAlphaF(static_cast<float>(kHoverCrosshairAlpha));
    QPen pen(crosshair, std::max<qreal>(1.0, density.dp(1.0)), Qt::DashLine);
    pen.setDashPattern({3.0, 3.0});
    painter.setPen(pen);
    painter.drawLine(QPointF(marker.x(), panel.top()), QPointF(marker.x(), panel.bottom()));
    painter.drawLine(QPointF(panel.left(), marker.y()), QPointF(panel.right(), marker.y()));

    painter.setPen(QPen(colours.onSurface, std::max<qreal>(1.0, density.dp(1.0))));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(marker, density.dp(3.0), density.dp(3.0));

    const QFont font = Font::resolve(this, theme.type().labelSmall, density);
    const QFontMetricsF metrics(font);
    const QString lineOne = QStringLiteral("t %1   x %2")
        .arg(formatTick(sample.time), formatTick(sample.value));
    const QString lineTwo = QStringLiteral("q %1   |z| %2")
        .arg(formatTick(sample.quadrature), formatTick(sample.envelope));
    const QString lineThree = QStringLiteral("φ %1 rad").arg(formatTick(sample.phase));

    const qreal padding = density.dp(5.0);
    const qreal width = std::max({metrics.horizontalAdvance(lineOne),
                                  metrics.horizontalAdvance(lineTwo),
                                  metrics.horizontalAdvance(lineThree)})
        + padding * 2.0;
    const qreal height = metrics.height() * 3.0 + padding * 2.0;

    QPointF anchor = marker + QPointF(density.dp(12.0), -height - density.dp(8.0));
    if (anchor.x() + width > panel.right())
        anchor.setX(marker.x() - width - density.dp(12.0));
    anchor.setX(std::max(panel.left() + 1.0, anchor.x()));
    anchor.setY(std::clamp(anchor.y(), panel.top() + 1.0,
                           std::max(panel.top() + 1.0, panel.bottom() - height - 1.0)));

    const QRectF box(anchor, QSizeF(width, height));
    QColor background = colours.surfaceContainerHigh;
    background.setAlphaF(static_cast<float>(kHoverBoxAlpha));
    painter.setPen(QPen(colours.outlineVariant, std::max<qreal>(1.0, density.dp(1.0))));
    painter.setBrush(background);
    painter.drawRoundedRect(box, 4.0, 4.0);

    painter.setFont(font);
    painter.setPen(colours.onSurface);
    painter.drawText(box.adjusted(padding, padding, -padding, -padding),
                     Qt::AlignLeft | Qt::AlignTop, lineOne);
    painter.setPen(colours.onSurfaceVariant);
    painter.drawText(box.adjusted(padding, padding + metrics.height(), -padding, -padding),
                     Qt::AlignLeft | Qt::AlignTop, lineTwo);
    painter.drawText(box.adjusted(padding, padding + metrics.height() * 2.0, -padding, -padding),
                     Qt::AlignLeft | Qt::AlignTop, lineThree);

    painter.restore();
}

void HilbertChart::paintNoData(QPainter& painter, const QRectF& plot) const
{
    if (plot.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFont font = Font::resolve(this, theme.type().labelMedium, density);

    painter.save();
    painter.setFont(font);
    painter.setPen(theme.colours().onSurfaceVariant);
    painter.drawText(plot, Qt::AlignCenter,
                     localized(QStringLiteral("chart.hilbert.noData"),
                               QStringLiteral("No analytic signal")));
    painter.restore();
}

void HilbertChart::paintEmptyState(QPainter& painter, const QRectF& plot)
{
    const PainterStateGuard guard(painter);
    try {
        paintNoData(painter, plot);
    } catch (const std::exception& error) {
        qWarning("md::HilbertChart::paintEmptyState failed: %s", error.what());
    } catch (...) {
        qWarning("md::HilbertChart::paintEmptyState failed with an unknown exception");
    }
}

void HilbertChart::mouseMoveEvent(QMouseEvent* event)
{
    try {
        View::mouseMoveEvent(event);
        hoverPosition_ = event->position();
        const Layout layout = layoutFor(plotRect());
        hovering_ = layout.quadrature.contains(hoverPosition_)
            || layout.envelope.contains(hoverPosition_);
        requestUpdate();
    } catch (const std::exception& error) {
        qWarning("md::HilbertChart::mouseMoveEvent failed: %s", error.what());
        hovering_ = false;
    } catch (...) {
        qWarning("md::HilbertChart::mouseMoveEvent failed with an unknown exception");
        hovering_ = false;
    }
}

void HilbertChart::leaveEvent(QEvent* event)
{
    try {
        View::leaveEvent(event);
        hovering_ = false;
        requestUpdate();
    } catch (const std::exception& error) {
        qWarning("md::HilbertChart::leaveEvent failed: %s", error.what());
    } catch (...) {
        qWarning("md::HilbertChart::leaveEvent failed with an unknown exception");
    }
}

qreal HilbertChart::revealProgress() const noexcept
{
    return animation().progress();
}

bool HilbertChart::isRevealing() const noexcept
{
    return animation().isRunning();
}

} // namespace md
