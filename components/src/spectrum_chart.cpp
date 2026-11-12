// Periodicity — the SpectrumChart component.
//
// The frame is painted here rather than by MathematicsCoordination because the
// frequency axis can be logarithmic: the shared transform maps a linear data
// range, and stretching it would make the decade labels wrong. The static nice-
// tick helpers of MathematicsCoordination are still used, so the tick selection
// and number formatting match the rest of the chart family.
//
// Layout is derived from the theme's label metrics on every call, the same way
// ChartBase derives its own gutters. Nothing is cached on resize: a theme
// switch, a font-scale change or a resize is reflected the next time the chart
// is measured or painted, without an invalidation protocol.
//
// The reveal is a horizontal clip: the frame and its labels are always
// complete, while the data marks (fill, trace, peaks) grow in from the left.
// The clipped reveal never changes binAt() or pointForBin(): those describe the
// settled geometry, so a host tooltip cannot disagree with the final frame.

#include <md/components/spectrum_chart.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>

namespace md {
namespace {

constexpr qreal kPi = 3.141592653589793238462643383279502884;

constexpr qreal kAxisTickLengthDp = 4.0;
constexpr qreal kAxisLabelGapDp = 4.0;
constexpr qreal kMinimumYAxisGutterDp = 32.0;
constexpr qreal kTopInsetDp = 8.0;
constexpr qreal kRightInsetDp = 8.0;
constexpr qreal kGridWidthDp = 1.0;
constexpr qreal kAxisWidthDp = 1.0;
constexpr qreal kTraceWidthDp = 2.0;
constexpr qreal kPhaseWidthDp = 1.4;
constexpr qreal kPeakRadiusDp = 2.5;
constexpr qreal kCursorRadiusDp = 3.0;
constexpr qreal kReadoutPaddingDp = 5.0;
constexpr qreal kReadoutGapDp = 6.0;
constexpr qreal kMinimumPenWidth = 0.75;

constexpr double kDecibelsPerDecade = 20.0;
constexpr double kMinimumDb = -300.0;
constexpr double kMaximumDb = -1.0;
constexpr double kDefaultPeakFloorDb = -60.0;
constexpr double kDefaultDbFloor = -120.0;

constexpr qreal kGridAlpha = 0.65;
constexpr qreal kMinorGridAlpha = 0.32;
constexpr qreal kFillAlpha = 0.14;

// Saves and restores the painter on every exit path, including one taken by an
// exception. A manual save()/restore() pair around a try block would leak the
// clip and pen state into the next paint stage when a helper throws.
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

[[nodiscard]] QFont tickFont(const QWidget* context, const Theme& theme)
{
    return Font::resolve(context, theme.type().labelSmall, theme.density());
}

[[nodiscard]] QFont axisTitleFont(const QWidget* context, const Theme& theme)
{
    return Font::resolve(context, theme.type().labelMedium, theme.density());
}

// A frequency rendered for an axis tick or a peak label: compact, unit-
// suffixed and stable across a wide range. "1 kHz" rather than "1000.0 Hz"
// keeps a decade axis readable.
[[nodiscard]] QString formatFrequency(double hertz)
{
    if (!std::isfinite(hertz))
        return QStringLiteral("—");
    if (hertz < 0.0)
        hertz = 0.0;

    if (hertz >= 1.0e6) {
        const double value = hertz / 1.0e6;
        return QStringLiteral("%1 MHz").arg(QString::number(value, 'g', 4));
    }
    if (hertz >= 1.0e3) {
        const double value = hertz / 1.0e3;
        return QStringLiteral("%1 kHz").arg(QString::number(value, 'g', 4));
    }
    if (hertz >= 100.0)
        return QStringLiteral("%1 Hz").arg(QString::number(hertz, 'f', 0));
    if (hertz >= 1.0)
        return QStringLiteral("%1 Hz").arg(QString::number(hertz, 'f', 1));
    if (hertz > 0.0)
        return QStringLiteral("%1 Hz").arg(QString::number(hertz, 'f', 2));

    return QStringLiteral("0 Hz");
}

[[nodiscard]] QString formatDecibels(double value)
{
    if (!std::isfinite(value))
        return QStringLiteral("—");
    return QString::number(value, 'f', 0);
}

// The decade ticks inside [minimum, maximum]. Empty when the range contains
// fewer than two whole decades, which lets the caller fall back to a linear
// tick set instead of drawing a single unlabelled line.
[[nodiscard]] QVector<double> decadeTicks(double minimum, double maximum)
{
    QVector<double> ticks;
    if (!(minimum > 0.0) || !(maximum > minimum))
        return ticks;

    const int first = static_cast<int>(std::ceil(std::log10(minimum) - 1.0e-12));
    const int last = static_cast<int>(std::floor(std::log10(maximum) + 1.0e-12));

    for (int exponent = first; exponent <= last; ++exponent) {
        const double value = std::pow(10.0, exponent);
        if (std::isfinite(value) && value >= minimum * (1.0 - 1.0e-12)
            && value <= maximum * (1.0 + 1.0e-12)) {
            ticks.append(value);
        }
    }

    return ticks;
}

// −180..180, the interval the phase axis always shows.
[[nodiscard]] QVector<double> phaseTicks()
{
    return {-180.0, -90.0, 0.0, 90.0, 180.0};
}

[[nodiscard]] double wrappedPhase(double degrees)
{
    if (!std::isfinite(degrees))
        return 0.0;

    double wrapped = std::fmod(degrees + 180.0, 360.0);
    if (wrapped < 0.0)
        wrapped += 360.0;
    return wrapped - 180.0;
}

// The nice step for a set of ticks, or 1 when there are fewer than two.
[[nodiscard]] qreal tickStep(const QVector<double>& ticks)
{
    if (ticks.size() >= 2) {
        const double step = ticks.at(1) - ticks.at(0);
        if (step > 0.0 && std::isfinite(step))
            return static_cast<qreal>(step);
    }
    return 1.0;
}

} // namespace

SpectrumChart::SpectrumChart(QWidget* parent)
    : ChartBase(parent)
{
    // This chart paints its own two-axis frame: the base frame is Cartesian and
    // cannot express a logarithmic frequency axis, and it would reserve gutters
    // for the wrong labels.
    setAxesVisible(false);

    // Read-only data surface: no focus and no whole-surface hover tint. The
    // chart paints its own cursor crosshair and readout.
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setInteractionFeedbackEnabled(false);

    // The transform is not used for mapping (the custom frame owns the
    // mapping). Turning auto-fit off keeps a data change from starting a fit
    // animation that would repaint an invisible plot.
    math()->setAutoFit(false);

    // The sweep is the level-2 motion of the shared chart ladder.
    animation().setDuration(MotionLevel::Level2);
}

void SpectrumChart::setMagnitudes(std::vector<double> magnitudes, double sampleRate)
{
    if (!std::isfinite(sampleRate) || !(sampleRate > 0.0)) {
        qWarning("md::SpectrumChart::setMagnitudes rejected a non-positive sample rate");
        magnitudes_.clear();
        phase_.clear();
        sampleRate_ = 0.0;
    } else {
        magnitudes_ = std::move(magnitudes);
        sampleRate_ = sampleRate;
        phase_.clear(); // one phase value per bin; the old mapping is gone
    }

    cursorBin_ = -1;
    rebuildSeries();
    requestUpdate();
}

const std::vector<double>& SpectrumChart::magnitudes() const noexcept
{
    return magnitudes_;
}

double SpectrumChart::sampleRate() const noexcept
{
    return sampleRate_;
}

int SpectrumChart::binCount() const noexcept
{
    return static_cast<int>(magnitudes_.size());
}

double SpectrumChart::nyquistFrequency() const noexcept
{
    return sampleRate_ > 0.0 ? sampleRate_ * 0.5 : 0.0;
}

double SpectrumChart::frequencyAt(int bin) const noexcept
{
    if (bin < 0 || bin >= binCount() || !(sampleRate_ > 0.0))
        return 0.0;

    if (magnitudes_.size() <= 1)
        return 0.0;

    const double nyquist = sampleRate_ * 0.5;
    const double step = nyquist / static_cast<double>(magnitudes_.size() - 1);
    return static_cast<double>(bin) * step;
}

double SpectrumChart::magnitudeAt(int bin) const noexcept
{
    if (bin < 0 || bin >= binCount())
        return 0.0;

    const double value = magnitudes_.at(static_cast<std::size_t>(bin));
    return std::isfinite(value) ? value : 0.0;
}

double SpectrumChart::decibelsAt(int bin) const noexcept
{
    const double magnitude = magnitudeAt(bin);
    if (!(magnitude > 0.0))
        return dbFloor_;

    const double value = 20.0 * std::log10(magnitude);
    if (!std::isfinite(value))
        return dbFloor_;

    return std::max(dbFloor_, value);
}

void SpectrumChart::setScale(Scale scale)
{
    if (scale_ == scale)
        return;

    scale_ = scale;
    rebuildSeries();
    requestUpdate();
}

SpectrumChart::Scale SpectrumChart::scale() const noexcept
{
    return scale_;
}

void SpectrumChart::setDbFloor(double decibels)
{
    if (!std::isfinite(decibels))
        return;

    const double floor = std::clamp(decibels, kMinimumDb, kMaximumDb);
    if (dbFloor_ == floor)
        return;

    dbFloor_ = floor;
    rebuildSeries();
    requestUpdate();
}

double SpectrumChart::dbFloor() const noexcept
{
    return dbFloor_;
}

void SpectrumChart::setFrequencyAxis(FrequencyAxis axis)
{
    if (frequencyAxis_ == axis)
        return;

    frequencyAxis_ = axis;
    requestUpdate();
}

SpectrumChart::FrequencyAxis SpectrumChart::frequencyAxis() const noexcept
{
    return frequencyAxis_;
}

void SpectrumChart::setPhase(std::vector<double> degrees)
{
    phase_ = std::move(degrees);
    if (phase_.size() > magnitudes_.size())
        phase_.resize(magnitudes_.size());

    rebuildSeries();
    requestUpdate();
}

const std::vector<double>& SpectrumChart::phase() const noexcept
{
    return phase_;
}

bool SpectrumChart::hasPhase() const noexcept
{
    return !phase_.empty();
}

void SpectrumChart::clearPhase()
{
    if (phase_.empty())
        return;

    phase_.clear();
    rebuildSeries();
    requestUpdate();
}

void SpectrumChart::setShowPeaks(bool show)
{
    if (showPeaks_ == show)
        return;

    showPeaks_ = show;
    requestUpdate();
}

bool SpectrumChart::showsPeaks() const noexcept
{
    return showPeaks_;
}

void SpectrumChart::setPeakFloorDb(double decibels)
{
    if (!std::isfinite(decibels))
        return;

    const double floor = std::clamp(decibels, -300.0, 0.0);
    if (peakFloorDb_ == floor)
        return;

    peakFloorDb_ = floor;
    requestUpdate();
}

double SpectrumChart::peakFloorDb() const noexcept
{
    return peakFloorDb_;
}

void SpectrumChart::setMaxPeakLabels(int count)
{
    const int labels = std::clamp(count, 0, 12);
    if (maxPeakLabels_ == labels)
        return;

    maxPeakLabels_ = labels;
    requestUpdate();
}

int SpectrumChart::maxPeakLabels() const noexcept
{
    return maxPeakLabels_;
}

std::vector<int> SpectrumChart::peakIndices() const
{
    std::vector<int> peaks;
    const int count = binCount();
    if (count < 3)
        return peaks;

    double strongest = 0.0;
    for (int bin = 0; bin < count; ++bin) {
        if (std::isfinite(magnitudes_.at(static_cast<std::size_t>(bin))))
            strongest = std::max(strongest, magnitudeAt(bin));
    }

    if (!(strongest > 0.0))
        return peaks;

    const double floor = strongest * std::pow(10.0, peakFloorDb_ / 20.0);

    for (int bin = 1; bin + 1 < count; ++bin) {
        const double value = magnitudeAt(bin);
        if (!(value > 0.0) || value < floor)
            continue;

        const double previous = bin > 0 ? magnitudeAt(bin - 1) : 0.0;
        const double next = magnitudeAt(bin + 1);
        if (value > previous && value >= next)
            peaks.push_back(bin);
    }

    std::stable_sort(peaks.begin(), peaks.end(), [this](int left, int right) {
        return magnitudeAt(left) > magnitudeAt(right);
    });

    return peaks;
}

void SpectrumChart::setCursorFrequency(double hertz)
{
    if (!std::isfinite(hertz) || !hasMagnitudes()) {
        clearCursor();
        return;
    }

    const int bin = nearestBin(hertz);
    if (cursorBin_ == bin)
        return;

    cursorBin_ = bin;
    requestUpdate();
}

void SpectrumChart::clearCursor()
{
    if (cursorBin_ < 0)
        return;

    cursorBin_ = -1;
    requestUpdate();
}

bool SpectrumChart::hasCursor() const noexcept
{
    return cursorBin_ >= 0 && cursorBin_ < binCount();
}

int SpectrumChart::cursorBin() const noexcept
{
    return hasCursor() ? cursorBin_ : -1;
}

double SpectrumChart::cursorFrequency() const noexcept
{
    return hasCursor() ? frequencyAt(cursorBin_) : 0.0;
}

QString SpectrumChart::cursorReadout() const
{
    const int bin = cursorBin();
    if (bin < 0)
        return {};

    return QStringLiteral("%1  |  %2  |  %3 dB")
        .arg(formatFrequency(frequencyAt(bin)))
        .arg(QString::number(magnitudeAt(bin), 'f', 3))
        .arg(QString::number(decibelsAt(bin), 'f', 1));
}

QRectF SpectrumChart::dataRect() const
{
    return computeLayout(plotRect()).data;
}

int SpectrumChart::binAt(QPointF position) const
{
    const Layout layout = computeLayout(plotRect());
    if (!layout.valid || !hasMagnitudes())
        return -1;

    const qreal slop = std::max<qreal>(4.0, Theme::forWidget(this).density().dp(6.0));
    if (!layout.data.adjusted(-slop, -slop, slop, slop).contains(position))
        return -1;

    const Interval frequency = frequencyRange();
    const double hertz = unmapFrequency(position.x(), layout, frequency);
    if (!std::isfinite(hertz))
        return -1;

    return nearestBin(hertz);
}

QPointF SpectrumChart::pointForBin(int bin) const
{
    if (bin < 0 || bin >= binCount())
        return {};
    if (!std::isfinite(magnitudes_.at(static_cast<std::size_t>(bin))))
        return {};

    const Layout layout = computeLayout(plotRect());
    if (!layout.valid)
        return {};

    const double hertz = frequencyAt(bin);
    if (frequencyAxis_ == FrequencyAxis::Logarithmic && !(hertz > 0.0))
        return {};

    const Interval frequency = frequencyRange();
    const Interval value = valueRange();

    return QPointF(mapFrequency(hertz, layout, frequency),
                   mapValue(displayValueAt(bin), layout, value));
}

QVector<double> SpectrumChart::frequencyTicks() const
{
    if (!hasMagnitudes())
        return {};

    const Interval range = frequencyRange();
    if (!(range.maximum > range.minimum))
        return {};

    if (frequencyAxis_ == FrequencyAxis::Logarithmic) {
        const QVector<double> decades = decadeTicks(range.minimum, range.maximum);
        if (decades.size() >= 2)
            return decades;
    }

    QVector<double> ticks = MathematicsCoordination::niceTicks(
        static_cast<qreal>(range.minimum), static_cast<qreal>(range.maximum), 6);
    if (ticks.size() < 2) {
        ticks.clear();
        ticks.append(range.minimum);
        ticks.append(range.maximum);
    }

    return ticks;
}

QStringList SpectrumChart::frequencyTickLabels() const
{
    QStringList labels;
    const QVector<double> ticks = frequencyTicks();
    labels.reserve(ticks.size());
    for (double value : ticks)
        labels.append(formatFrequency(value));
    return labels;
}

QVector<double> SpectrumChart::magnitudeTicks() const
{
    if (!hasMagnitudes())
        return {};

    const Interval range = valueRange();

    if (scale_ == Scale::Db) {
        QVector<double> ticks;
        double step = 20.0;
        while ((range.maximum - range.minimum) / step > 8.0)
            step *= 2.0;

        for (double value = range.minimum; value <= range.maximum + 1.0e-9; value += step)
            ticks.append(value);

        if (ticks.size() < 2)
            ticks = {range.minimum, range.maximum};

        return ticks;
    }

    QVector<double> ticks = MathematicsCoordination::niceTicks(
        static_cast<qreal>(range.minimum), static_cast<qreal>(range.maximum), 5);
    if (ticks.size() < 2) {
        ticks.clear();
        ticks.append(range.minimum);
        ticks.append(range.maximum);
    }

    return ticks;
}

QStringList SpectrumChart::magnitudeTickLabels() const
{
    QStringList labels;
    const QVector<double> ticks = magnitudeTicks();
    labels.reserve(ticks.size());

    if (scale_ == Scale::Db) {
        for (double value : ticks)
            labels.append(formatDecibels(value));
        return labels;
    }

    const qreal step = tickStep(ticks);
    for (double value : ticks)
        labels.append(MathematicsCoordination::tickLabel(static_cast<qreal>(value), step));

    return labels;
}

QString SpectrumChart::xAxisTitle() const
{
    return localized(QStringLiteral("chart.spectrum.frequency"), QStringLiteral("Frequency (Hz)"));
}

QString SpectrumChart::yAxisTitle() const
{
    if (scale_ == Scale::Db)
        return localized(QStringLiteral("chart.spectrum.magnitudeDb"),
                         QStringLiteral("Magnitude (dB)"));

    return localized(QStringLiteral("chart.spectrum.magnitude"), QStringLiteral("Magnitude"));
}

QString SpectrumChart::phaseAxisTitle() const
{
    return localized(QStringLiteral("chart.spectrum.phase"), QStringLiteral("Phase (°)"));
}

qreal SpectrumChart::revealProgress() const noexcept
{
    return animation().progress();
}

bool SpectrumChart::isRevealing() const noexcept
{
    return animation().isRunning();
}

bool SpectrumChart::hasMagnitudes() const noexcept
{
    for (double value : magnitudes_) {
        if (std::isfinite(value))
            return true;
    }
    return false;
}

double SpectrumChart::displayValueAt(int bin) const
{
    if (scale_ == Scale::Db)
        return decibelsAt(bin);

    return std::max(0.0, magnitudeAt(bin));
}

SpectrumChart::Interval SpectrumChart::valueRange() const
{
    const int count = binCount();
    if (scale_ == Scale::Db) {
        double top = 0.0;
        for (int bin = 0; bin < count; ++bin) {
            const double magnitude = magnitudeAt(bin);
            if (!(magnitude > 0.0))
                continue;
            top = std::max(top, 20.0 * std::log10(magnitude));
        }

        // The next whole 20 dB step at or above the data, but never above the
        // floor: a spectrum of zeros still has a drawable span.
        top = std::ceil(top / 20.0) * 20.0;
        if (!(top > dbFloor_))
            top = dbFloor_ + 20.0;

        return {dbFloor_, top};
    }

    double top = 0.0;
    for (int bin = 0; bin < count; ++bin)
        top = std::max(top, magnitudeAt(bin));

    if (!(top > 0.0))
        top = 1.0;

    const QVector<double> ticks =
        MathematicsCoordination::niceTicks(0.0, static_cast<qreal>(top), 5);
    if (!ticks.isEmpty())
        top = std::max(top, static_cast<double>(ticks.last()));

    if (!(top > 0.0))
        top = 1.0;

    return {0.0, top};
}

SpectrumChart::Interval SpectrumChart::frequencyRange() const
{
    const double maximum = nyquistFrequency();
    if (!(maximum > 0.0))
        return {0.0, 1.0};

    if (frequencyAxis_ == FrequencyAxis::Logarithmic) {
        double minimum = binCount() > 1 ? frequencyAt(1) : maximum;
        if (!(minimum > 0.0) || minimum >= maximum)
            minimum = maximum / 1000.0;
        if (!(minimum > 0.0) || minimum >= maximum)
            minimum = maximum * 0.5;
        return {minimum, maximum};
    }

    return {0.0, maximum};
}

SpectrumChart::Layout SpectrumChart::computeLayout(const QRectF& plot) const
{
    Layout layout;
    if (plot.isEmpty() || plot.width() <= 0.0 || plot.height() <= 0.0)
        return layout;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFontMetricsF tickMetrics(tickFont(this, theme));
    const QFontMetricsF titleMetrics(axisTitleFont(this, theme));

    const qreal tickLength = density.dp(kAxisTickLengthDp);
    const qreal labelGap = density.dp(kAxisLabelGapDp);

    qreal widestY = 0.0;
    for (const QString& label : magnitudeTickLabels())
        widestY = std::max(widestY, tickMetrics.horizontalAdvance(label));

    qreal left = std::max<qreal>(density.dp(kMinimumYAxisGutterDp),
                                 widestY + tickLength + labelGap);
    left += titleMetrics.height() + labelGap;

    qreal right = density.dp(kRightInsetDp);
    if (hasPhase()) {
        qreal widestPhase = 0.0;
        for (double value : phaseTicks()) {
            widestPhase = std::max(widestPhase,
                                   tickMetrics.horizontalAdvance(formatDecibels(value)));
        }
        right = std::max<qreal>(right, widestPhase + tickLength + labelGap);
        right += titleMetrics.height() + labelGap;
    }

    const qreal bottom = tickMetrics.height() + tickLength + labelGap
        + titleMetrics.height() + labelGap;
    const qreal top = density.dp(kTopInsetDp);

    const QRectF area(plot.left() + left, plot.top() + top,
                      plot.width() - left - right, plot.height() - top - bottom);

    if (area.width() <= 0.0 || area.height() <= 0.0)
        return layout;

    layout.data = area;
    layout.leftGutter = left;
    layout.rightGutter = right;
    layout.bottomGutter = bottom;
    layout.valid = true;
    return layout;
}

qreal SpectrumChart::mapFrequency(double hertz, const Layout& layout,
                                  const Interval& range) const
{
    const qreal left = layout.data.left();
    const qreal width = layout.data.width();

    if (frequencyAxis_ == FrequencyAxis::Logarithmic) {
        if (!(range.minimum > 0.0) || !(range.maximum > range.minimum))
            return left + width * 0.5;

        const double clamped = std::clamp(hertz, range.minimum, range.maximum);
        const double fraction = (std::log10(clamped) - std::log10(range.minimum))
            / (std::log10(range.maximum) - std::log10(range.minimum));
        return left + static_cast<qreal>(fraction) * width;
    }

    if (!(range.maximum > range.minimum))
        return left + width * 0.5;

    const double clamped = std::clamp(hertz, range.minimum, range.maximum);
    const double fraction = (clamped - range.minimum) / (range.maximum - range.minimum);
    return left + static_cast<qreal>(fraction) * width;
}

qreal SpectrumChart::mapValue(double value, const Layout& layout,
                              const Interval& range) const
{
    if (!(range.maximum > range.minimum))
        return layout.data.center().y();

    const double clamped = std::clamp(value, range.minimum, range.maximum);
    const double fraction = (clamped - range.minimum) / (range.maximum - range.minimum);
    return layout.data.bottom() - static_cast<qreal>(fraction) * layout.data.height();
}

double SpectrumChart::unmapFrequency(qreal x, const Layout& layout,
                                     const Interval& range) const
{
    if (!layout.valid || !(layout.data.width() > 0.0))
        return 0.0;

    double fraction = static_cast<double>((x - layout.data.left()) / layout.data.width());
    fraction = std::clamp(fraction, 0.0, 1.0);

    if (frequencyAxis_ == FrequencyAxis::Logarithmic) {
        if (!(range.minimum > 0.0) || !(range.maximum > range.minimum))
            return range.minimum;

        const double low = std::log10(range.minimum);
        const double high = std::log10(range.maximum);
        return std::pow(10.0, low + fraction * (high - low));
    }

    if (!(range.maximum > range.minimum))
        return range.minimum;

    return range.minimum + fraction * (range.maximum - range.minimum);
}

int SpectrumChart::nearestBin(double hertz) const
{
    int best = -1;
    double bestDistance = std::numeric_limits<double>::infinity();

    for (int bin = 0; bin < binCount(); ++bin) {
        if (!std::isfinite(magnitudes_.at(static_cast<std::size_t>(bin))))
            continue;

        const double distance = std::abs(frequencyAt(bin) - hertz);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = bin;
        }
    }

    return best;
}

void SpectrumChart::rebuildSeries()
{
    QVector<ChartSeries> list;

    if (hasMagnitudes()) {
        ChartSeries magnitude;
        magnitude.name = localized(QStringLiteral("chart.spectrum.magnitude"),
                                   QStringLiteral("Magnitude"));
        magnitude.points.reserve(binCount());

        for (int bin = 0; bin < binCount(); ++bin) {
            if (!std::isfinite(magnitudes_.at(static_cast<std::size_t>(bin))))
                continue;
            magnitude.points.append(QPointF(frequencyAt(bin), displayValueAt(bin)));
        }

        if (!magnitude.points.isEmpty())
            list.append(std::move(magnitude));

        if (hasPhase()) {
            ChartSeries phase;
            phase.name = localized(QStringLiteral("chart.spectrum.phase"),
                                   QStringLiteral("Phase"));
            phase.points.reserve(phase_.size());

            const int count = std::min(binCount(), static_cast<int>(phase_.size()));
            for (int bin = 0; bin < count; ++bin) {
                if (!std::isfinite(magnitudes_.at(static_cast<std::size_t>(bin)))
                    || !std::isfinite(phase_.at(static_cast<std::size_t>(bin)))) {
                    continue;
                }
                phase.points.append(QPointF(frequencyAt(bin), wrappedPhase(phase_.at(bin))));
            }

            if (!phase.points.isEmpty())
                list.append(std::move(phase));
        }
    }

    ChartBase::setSeries(std::move(list));
}

void SpectrumChart::drawFrame(QPainter& painter, const Layout& layout, const Theme& theme)
{
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const QRectF area = layout.data;
    if (area.isEmpty())
        return;

    const Interval frequency = frequencyRange();
    const Interval value = valueRange();

    const qreal gridWidth = std::max(kMinimumPenWidth, density.dp(kGridWidthDp));
    const qreal axisWidth = std::max(kMinimumPenWidth, density.dp(kAxisWidthDp));
    const qreal tickLength = density.dp(kAxisTickLengthDp);
    const qreal labelGap = density.dp(kAxisLabelGapDp);

    const QFont labels = tickFont(this, theme);
    const QFontMetricsF labelMetrics(labels);
    const QFont titles = axisTitleFont(this, theme);

    // 1. Grid, clipped to the data rectangle. The horizontal pass is the
    //    magnitude axis; the vertical pass is the frequency axis.
    {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setClipRect(area.adjusted(-1.0, -1.0, 1.0, 1.0));

        QColor gridColour = colours.outlineVariant;
        gridColour.setAlphaF(static_cast<float>(kGridAlpha));
        painter.setPen(QPen(gridColour, gridWidth));

        for (double hertz : frequencyTicks()) {
            const qreal x = mapFrequency(hertz, layout, frequency);
            painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        }

        if (frequencyAxis_ == FrequencyAxis::Logarithmic
            && frequency.minimum > 0.0) {
            QColor minorColour = colours.outlineVariant;
            minorColour.setAlphaF(static_cast<float>(kMinorGridAlpha));
            painter.setPen(QPen(minorColour, gridWidth));

            const int first =
                static_cast<int>(std::floor(std::log10(frequency.minimum)));
            const int last =
                static_cast<int>(std::ceil(std::log10(frequency.maximum)));

            for (int exponent = first; exponent <= last; ++exponent) {
                for (double multiplier : {2.0, 5.0}) {
                    const double hertz = multiplier * std::pow(10.0, exponent);
                    if (hertz <= frequency.minimum || hertz >= frequency.maximum)
                        continue;
                    const qreal x = mapFrequency(hertz, layout, frequency);
                    painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
                }
            }
        }

        painter.setPen(QPen(gridColour, gridWidth));
        for (double level : magnitudeTicks()) {
            const qreal y = mapValue(level, layout, value);
            painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        }

        painter.restore();
    }

    // 2. Axes and their tick marks.
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(colours.outline, axisWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(area.topLeft(), area.bottomLeft());
    painter.drawLine(area.bottomLeft(), area.bottomRight());
    if (hasPhase())
        painter.drawLine(area.topRight(), area.bottomRight());

    // 3. Tick marks and labels: right-aligned for the left axis, centred for
    //    the bottom axis, left-aligned for the optional right axis.
    painter.setFont(labels);

    const QVector<double> yTicks = magnitudeTicks();
    const QStringList yLabels = magnitudeTickLabels();
    const qreal yLabelX = area.left() - tickLength - labelGap;

    for (qsizetype i = 0; i < yTicks.size() && i < yLabels.size(); ++i) {
        const qreal y = mapValue(yTicks.at(i), layout, value);
        painter.setPen(QPen(colours.outline, axisWidth));
        painter.drawLine(QPointF(area.left() - tickLength, y), QPointF(area.left(), y));

        painter.setPen(colours.onSurfaceVariant);
        const qreal width = labelMetrics.horizontalAdvance(yLabels.at(i));
        const QRectF box(yLabelX - width, y - labelMetrics.height() * 0.5,
                         width, labelMetrics.height());
        painter.drawText(box, Qt::AlignRight | Qt::AlignVCenter, yLabels.at(i));
    }

    const QVector<double> xTicks = frequencyTicks();
    const QStringList xLabels = frequencyTickLabels();
    const qreal xLabelTop = area.bottom() + tickLength + labelGap;

    for (qsizetype i = 0; i < xTicks.size() && i < xLabels.size(); ++i) {
        const qreal x = mapFrequency(xTicks.at(i), layout, frequency);
        painter.setPen(QPen(colours.outline, axisWidth));
        painter.drawLine(QPointF(x, area.bottom()), QPointF(x, area.bottom() + tickLength));

        painter.setPen(colours.onSurfaceVariant);
        const qreal width = labelMetrics.horizontalAdvance(xLabels.at(i));
        const QRectF box(x - width * 0.5, xLabelTop, width, labelMetrics.height());
        painter.drawText(box, Qt::AlignHCenter | Qt::AlignTop, xLabels.at(i));
    }

    if (hasPhase()) {
        const Interval phaseInterval{-180.0, 180.0};
        const qreal phaseLabelX = area.right() + tickLength + labelGap;

        for (double degrees : phaseTicks()) {
            const qreal y = mapValue(degrees, layout, phaseInterval);
            painter.setPen(QPen(colours.outline, axisWidth));
            painter.drawLine(QPointF(area.right(), y),
                             QPointF(area.right() + tickLength, y));

            painter.setPen(colours.onSurfaceVariant);
            const QString text = formatDecibels(degrees);
            const qreal width = labelMetrics.horizontalAdvance(text);
            const QRectF box(phaseLabelX, y - labelMetrics.height() * 0.5,
                             width, labelMetrics.height());
            painter.drawText(box, Qt::AlignLeft | Qt::AlignVCenter, text);
        }
    }

    // 4. Axis titles. The two vertical titles are rotated so they read
    //    bottom-to-top, which is the convention for a numeric axis.
    painter.setFont(titles);
    painter.setPen(colours.onSurfaceVariant);

    const qreal titleHeight = QFontMetricsF(titles).height();

    painter.save();
    painter.translate(area.left() - layout.leftGutter + titleHeight * 0.5, area.center().y());
    painter.rotate(-90.0);
    painter.drawText(QRectF(-area.height() * 0.5, -titleHeight * 0.5,
                            area.height(), titleHeight),
                     Qt::AlignCenter, yAxisTitle());
    painter.restore();

    if (hasPhase()) {
        painter.save();
        painter.translate(area.right() + layout.rightGutter - titleHeight * 0.5,
                          area.center().y());
        painter.rotate(90.0);
        painter.drawText(QRectF(-area.height() * 0.5, -titleHeight * 0.5,
                                area.height(), titleHeight),
                         Qt::AlignCenter, phaseAxisTitle());
        painter.restore();
    }

    const QString xTitle = xAxisTitle();
    const qreal xTitleWidth = std::min(
        area.width(), QFontMetricsF(titles).horizontalAdvance(xTitle));
    const QRectF xTitleBox(area.center().x() - xTitleWidth * 0.5,
                           area.bottom() + tickLength + labelGap
                               + labelMetrics.height() + labelGap,
                           xTitleWidth, titleHeight);
    painter.drawText(xTitleBox, Qt::AlignCenter, xTitle);
}

void SpectrumChart::paintPlot(QPainter& painter, const QRectF& plot, qreal progress)
{
    paintPlotAt(painter, plot, progress);
}

void SpectrumChart::paintPlotAt(QPainter& painter, const QRectF& plot, qreal progress)
{
    if (plot.isEmpty())
        return;

    [[maybe_unused]] const PainterStateGuard guard(painter);
    try {
        const Theme theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const Layout layout = computeLayout(plot);
        if (!layout.valid)
            return;

        drawFrame(painter, layout, theme);

        if (!hasMagnitudes())
            return;

        const QRectF area = layout.data;
        const Interval frequency = frequencyRange();
        const Interval value = valueRange();
        const qreal reveal = std::clamp(progress, 0.0, 1.0);
        const qreal revealedRight = area.left() + area.width() * reveal;

        const QColor traceColour = seriesColour(0);
        const QColor phaseColour = seriesColour(1);
        const qreal traceWidth = std::max<qreal>(kMinimumPenWidth,
                                                 density.dp(kTraceWidthDp));

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);

        // The data marks are clipped to the data rectangle and, while the
        // reveal runs, to its revealed part. The frame above is deliberately
        // outside this clip.
        painter.setClipRect(area.adjusted(-2.0, -6.0, 2.0, 2.0), Qt::IntersectClip);
        if (reveal < 1.0) {
            painter.setClipRect(QRectF(area.left() - 2.0, area.top() - 6.0,
                                       revealedRight - area.left() + 4.0,
                                       area.height() + 8.0),
                                Qt::IntersectClip);
        }

        // 1. Phase trace, on the right-hand axis, behind the magnitude trace.
        if (hasPhase() && phaseColour.isValid()) {
            const Interval phaseInterval{-180.0, 180.0};
            QPainterPath phasePath;
            bool started = false;
            double previousPhase = 0.0;

            const int count = std::min(binCount(), static_cast<int>(phase_.size()));
            for (int bin = 0; bin < count; ++bin) {
                const double magnitude =
                    magnitudes_.at(static_cast<std::size_t>(bin));
                const double rawPhase = phase_.at(static_cast<std::size_t>(bin));
                if (!std::isfinite(magnitude) || !std::isfinite(rawPhase)) {
                    started = false;
                    continue;
                }

                const double hertz = frequencyAt(bin);
                if (frequencyAxis_ == FrequencyAxis::Logarithmic && !(hertz > 0.0))
                    continue;

                const double degrees = wrappedPhase(rawPhase);
                const QPointF point(mapFrequency(hertz, layout, frequency),
                                    mapValue(degrees, layout, phaseInterval));

                if (!started) {
                    phasePath.moveTo(point);
                    started = true;
                } else if (std::abs(degrees - previousPhase) > 180.0) {
                    // A wrap is a discontinuity, not a vertical segment.
                    phasePath.moveTo(point);
                } else {
                    phasePath.lineTo(point);
                }

                previousPhase = degrees;
            }

            QPen phasePen(phaseColour, std::max<qreal>(kMinimumPenWidth,
                                                       density.dp(kPhaseWidthDp)));
            phasePen.setStyle(Qt::DashLine);
            phasePen.setCapStyle(Qt::RoundCap);
            painter.setPen(phasePen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(phasePath);
        }

        // 2. Magnitude area fill and trace.
        QVector<QPointF> samples;
        samples.reserve(binCount());
        for (int bin = 0; bin < binCount(); ++bin) {
            if (!std::isfinite(magnitudes_.at(static_cast<std::size_t>(bin))))
                continue;
            const double hertz = frequencyAt(bin);
            if (frequencyAxis_ == FrequencyAxis::Logarithmic && !(hertz > 0.0))
                continue;
            samples.append(QPointF(mapFrequency(hertz, layout, frequency),
                                   mapValue(displayValueAt(bin), layout, value)));
        }

        if (traceColour.isValid() && samples.size() >= 2) {
            QPainterPath path;
            path.moveTo(samples.first());
            for (qsizetype i = 1; i < samples.size(); ++i)
                path.lineTo(samples.at(i));

            QColor fill = traceColour;
            fill.setAlphaF(static_cast<float>(kFillAlpha));
            QPainterPath fillPath = path;
            fillPath.lineTo(samples.last().x(), area.bottom());
            fillPath.lineTo(samples.first().x(), area.bottom());
            fillPath.closeSubpath();
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
            painter.drawPath(fillPath);

            QPen tracePen(traceColour, traceWidth);
            tracePen.setCapStyle(Qt::RoundCap);
            tracePen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(tracePen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
        } else if (traceColour.isValid() && samples.size() == 1) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(traceColour);
            painter.drawEllipse(samples.first(), traceWidth, traceWidth);
        }

        // 3. Peak markers and labels, strongest first. A label is skipped once
        //    the frame budget is exhausted; the marker still marks the peak.
        if (showPeaks_ && maxPeakLabels_ > 0 && traceColour.isValid()) {
            const std::vector<int> peaks = peakIndices();
            const int shown = std::min<int>(maxPeakLabels_,
                                            static_cast<int>(peaks.size()));
            const qreal radius = std::max<qreal>(1.5, density.dp(kPeakRadiusDp));
            const QFont peakFont = tickFont(this, theme);
            const QFontMetricsF peakMetrics(peakFont);

            painter.setFont(peakFont);

            for (int i = 0; i < shown; ++i) {
                const int bin = peaks.at(static_cast<std::size_t>(i));
                const QPointF point = pointForBin(bin);
                if (point.isNull() || point.x() > revealedRight + 0.5)
                    continue;

                painter.setPen(QPen(theme.colours().surface, 1.0));
                painter.setBrush(traceColour);
                painter.drawEllipse(point, radius, radius);

                const QString text = formatFrequency(frequencyAt(bin));
                const qreal textWidth = peakMetrics.horizontalAdvance(text);
                const qreal textHeight = peakMetrics.height();
                QRectF box(point.x() - textWidth * 0.5 - 3.0,
                           point.y() - textHeight - radius - 3.0,
                           textWidth + 6.0, textHeight);

                if (box.left() < area.left())
                    box.moveLeft(area.left());
                if (box.right() > area.right())
                    box.moveRight(area.right());
                if (box.top() < area.top())
                    box.moveTop(point.y() + radius + 3.0);

                QColor background = theme.colours().surfaceContainerHigh;
                background.setAlpha(225);
                painter.setPen(QPen(theme.colours().outlineVariant, 1.0));
                painter.setBrush(background);
                painter.drawRoundedRect(box, 3.0, 3.0);
                painter.setPen(theme.colours().onSurface);
                painter.drawText(box, Qt::AlignCenter, text);
            }
        }

        painter.restore();

        // 4. Cursor crosshair and readout, drawn after the clipped marks so the
        //    readout is never cut by the reveal clip.
        const int cursor = cursorBin();
        if (cursor >= 0 && traceColour.isValid()) {
            const QPointF point = pointForBin(cursor);
            if (!point.isNull()) {
                QColor cursorColour = traceColour;
                QPen crosshair(cursorColour, std::max<qreal>(kMinimumPenWidth,
                                                             density.dp(1.0)),
                               Qt::DashLine);
                painter.setPen(crosshair);
                painter.drawLine(QPointF(point.x(), area.top()),
                                 QPointF(point.x(), area.bottom()));

                painter.setPen(QPen(theme.colours().surface, 1.5));
                painter.setBrush(cursorColour);
                painter.drawEllipse(point, density.dp(kCursorRadiusDp),
                                    density.dp(kCursorRadiusDp));

                const QString readout = cursorReadout();
                if (!readout.isEmpty()) {
                    const QFont readoutFont = tickFont(this, theme);
                    const QFontMetricsF readoutMetrics(readoutFont);
                    const qreal padding = density.dp(kReadoutPaddingDp);
                    const qreal gap = density.dp(kReadoutGapDp);
                    const qreal maximum = std::max<qreal>(0.0, area.width() - gap * 2.0);
                    const QString text =
                        readoutMetrics.elidedText(readout, Qt::ElideRight, maximum);
                    const qreal boxWidth =
                        std::min(maximum, readoutMetrics.horizontalAdvance(text) + padding * 2.0);
                    const QRectF box(area.right() - gap - boxWidth,
                                     area.top() + gap,
                                     boxWidth,
                                     readoutMetrics.height() + padding);
                    QColor background = theme.colours().surfaceContainerHigh;
                    background.setAlpha(235);

                    painter.setPen(QPen(theme.colours().outlineVariant, 1.0));
                    painter.setBrush(background);
                    painter.drawRoundedRect(box, 4.0, 4.0);
                    painter.setFont(readoutFont);
                    painter.setPen(theme.colours().onSurface);
                    painter.drawText(box, Qt::AlignCenter, text);
                }
            }
        }
    } catch (const std::exception& error) {
        qWarning("md::SpectrumChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::SpectrumChart::paintPlot failed with an unknown exception");
    }
}

void SpectrumChart::paintEmptyState(QPainter& painter, const QRectF& plot)
{
    if (plot.isEmpty())
        return;

    painter.save();
    try {
        const Theme& theme = Theme::forWidget(this);
        painter.setFont(Font::resolve(this, theme.type().labelMedium, theme.density()));
        painter.setPen(theme.colours().onSurfaceVariant);
        painter.drawText(plot, Qt::AlignCenter,
                         localized(QStringLiteral("chart.spectrum.noData"),
                                   QStringLiteral("No spectrum area")));
    } catch (const std::exception& error) {
        qWarning("md::SpectrumChart::paintEmptyState failed: %s", error.what());
    } catch (...) {
        qWarning("md::SpectrumChart::paintEmptyState failed with an unknown exception");
    }
    painter.restore();
}

void SpectrumChart::mouseMoveEvent(QMouseEvent* event)
{
    try {
        View::mouseMoveEvent(event);
        const int bin = binAt(event->position());
        if (bin != cursorBin_) {
            cursorBin_ = bin;
            requestUpdate();
        }
    } catch (const std::exception& error) {
        qWarning("md::SpectrumChart::mouseMoveEvent failed: %s", error.what());
    } catch (...) {
        qWarning("md::SpectrumChart::mouseMoveEvent failed with an unknown exception");
    }
}

void SpectrumChart::leaveEvent(QEvent* event)
{
    try {
        View::leaveEvent(event);
        clearCursor();
    } catch (const std::exception& error) {
        qWarning("md::SpectrumChart::leaveEvent failed: %s", error.what());
    } catch (...) {
        qWarning("md::SpectrumChart::leaveEvent failed with an unknown exception");
    }
}

} // namespace md
