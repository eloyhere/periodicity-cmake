// Periodicity — the SpectrogramChart component.
//
// The paint pass resolves one theme snapshot per frame: the cell ramp, the
// grid, the axes and the colour bar all come from the same colour roles, and
// every length goes through Density::dp(). The matrix is painted cell by cell
// inside a clip to the data rectangle; the grid is stroked over the cells so a
// frequency or time reference stays visible through a hot region.
//
// The matrix is deliberately not routed through MathematicsCoordination: the
// optional logarithmic frequency axis and the shared frequency/colour-bar
// gutter are not a Cartesian frame, so the component owns its own axis layout.
// ChartBase still owns the title, the legend row, the empty state and the
// exception boundary around paintContent().
//
// Time and frequency label formatting is centralised in timeTickLabel() and
// frequencyTickLabel(); the paint pass and the public tick accessors therefore
// cannot disagree about a tick's text.

#include <md/components/spectrogram_chart.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QBrush>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPoint>
#include <QPointF>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>

namespace md {
namespace {

constexpr qreal kGapDp = 4.0;
constexpr qreal kAxisTitleGapDp = 3.0;
constexpr qreal kMinimumLeftGutterDp = 26.0;
constexpr qreal kMinimumBottomGutterDp = 26.0;
constexpr qreal kColourBarWidthDp = 10.0;
constexpr qreal kColourBarGapDp = 4.0;
constexpr qreal kTopInsetDp = 3.0;
constexpr qreal kGridAlpha = 0.70;
constexpr qreal kCellOverlap = 0.5;
constexpr qreal kCrosshairAlpha = 0.55;
constexpr qreal kTooltipAlpha = 0.96;
constexpr qreal kTooltipPaddingDp = 5.0;
constexpr int kMaximumLogTicks = 8;

[[nodiscard]] qreal emptyValue() noexcept
{
    return std::numeric_limits<qreal>::quiet_NaN();
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

// The five-stop ramp, low level first. Every stop is a theme role, so the ramp
// follows a theme switch and never needs a literal colour.
[[nodiscard]] QVector<QColor> rampStops(const ColorScheme& colours)
{
    return {colours.surfaceContainerHighest,
            colours.secondaryContainer,
            colours.tertiary,
            colours.primary,
            colours.error};
}

[[nodiscard]] QColor interpolate(const QColor& from, const QColor& to, qreal fraction)
{
    const qreal t = std::clamp(fraction, 0.0, 1.0);
    const auto channel = [t](int start, int end) {
        return static_cast<int>(qRound(start + (end - start) * t));
    };

    return QColor(channel(from.red(), to.red()),
                  channel(from.green(), to.green()),
                  channel(from.blue(), to.blue()));
}

[[nodiscard]] QColor rampColour(const QVector<QColor>& stops, qreal fraction)
{
    if (stops.isEmpty())
        return QColor();

    const qreal position = std::clamp(fraction, 0.0, 1.0) * (stops.size() - 1);
    const int lower = std::clamp(static_cast<int>(std::floor(position)), 0,
                                 static_cast<int>(stops.size()) - 1);
    const int upper = std::min(lower + 1, static_cast<int>(stops.size()) - 1);

    if (lower == upper)
        return stops.at(lower);

    return interpolate(stops.at(lower), stops.at(upper), position - lower);
}

[[nodiscard]] QString formatFrequencyReadout(qreal hertz)
{
    if (!std::isfinite(hertz))
        return QStringLiteral("—");

    if (std::abs(hertz) >= 1000.0) {
        return QStringLiteral("%1 kHz").arg(
            QString::number(hertz / 1000.0, 'f', 2));
    }

    return QStringLiteral("%1 Hz").arg(QString::number(hertz, 'f', 1));
}

[[nodiscard]] QString formatLevelReadout(qreal levelDb)
{
    if (!std::isfinite(levelDb))
        return QStringLiteral("—");

    return QStringLiteral("%1 dB").arg(QString::number(levelDb, 'f', 1));
}

} // namespace

SpectrogramChart::SpectrogramChart(QWidget* parent)
    : ChartBase(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);

    // The component paints its own time and frequency frame (the logarithmic
    // mode and the colour-bar gutter cannot be expressed by the shared
    // Cartesian frame), so the base frame and legend row are switched off. The
    // colour bar is the legend of a heat map.
    setAxesVisible(false);
    setLegendVisible(false);

    // A spectrogram is a read-only data surface: the shared state layer would
    // wash the whole matrix while the pointer is over it, so the component
    // supplies only its own crosshair feedback.
    setInteractionFeedbackEnabled(false);
}

void SpectrogramChart::setFrames(std::vector<std::vector<double>> frames)
{
    frames_ = std::move(frames);
    magnitudeScale_ = false;
    syncPresenceSeries();
}

const std::vector<std::vector<double>>& SpectrogramChart::frames() const noexcept
{
    return frames_;
}

void SpectrogramChart::setMagnitude(const std::vector<std::vector<double>>& matrix,
                                    double sampleRate, double hopSize)
{
    if (std::isfinite(sampleRate) && sampleRate > 0.0)
        sampleRate_ = sampleRate;
    if (std::isfinite(hopSize) && hopSize > 0.0)
        hopSize_ = std::max(1.0, hopSize);

    frames_ = matrix;
    magnitudeScale_ = true;
    syncPresenceSeries();
}

bool SpectrogramChart::isMagnitudeScale() const noexcept
{
    return magnitudeScale_;
}

int SpectrogramChart::frequencyBins() const noexcept
{
    return static_cast<int>(frames_.size());
}

int SpectrogramChart::frameCount() const noexcept
{
    size_t count = 0;
    for (const std::vector<double>& row : frames_)
        count = std::max(count, row.size());

    return static_cast<int>(count);
}

void SpectrogramChart::setSampleRate(double sampleRate)
{
    if (!std::isfinite(sampleRate) || !(sampleRate > 0.0))
        return;

    if (sampleRate_ == sampleRate)
        return;

    sampleRate_ = sampleRate;
    requestUpdate();
}

double SpectrogramChart::sampleRate() const noexcept
{
    return sampleRate_;
}

void SpectrogramChart::setHopSize(double hopSize)
{
    if (!std::isfinite(hopSize) || !(hopSize > 0.0))
        return;

    const double clamped = std::max(1.0, hopSize);
    if (hopSize_ == clamped)
        return;

    hopSize_ = clamped;
    requestUpdate();
}

double SpectrogramChart::hopSize() const noexcept
{
    return hopSize_;
}

void SpectrogramChart::setDbRange(double minimumDb, double maximumDb)
{
    if (!std::isfinite(minimumDb) || !std::isfinite(maximumDb))
        return;

    double low = std::min(minimumDb, maximumDb);
    double high = std::max(minimumDb, maximumDb);

    if (!(high > low)) {
        low -= 0.5;
        high += 0.5;
    }

    if (dbMinimum_ == low && dbMaximum_ == high)
        return;

    dbMinimum_ = low;
    dbMaximum_ = high;
    requestUpdate();
}

double SpectrogramChart::minimumDb() const noexcept
{
    return dbMinimum_;
}

double SpectrogramChart::maximumDb() const noexcept
{
    return dbMaximum_;
}

void SpectrogramChart::setLogFrequency(bool logarithmic)
{
    if (logFrequency_ == logarithmic)
        return;

    logFrequency_ = logarithmic;
    requestUpdate();
}

bool SpectrogramChart::isLogFrequency() const noexcept
{
    return logFrequency_;
}

void SpectrogramChart::setColourBarVisible(bool visible)
{
    if (colourBarVisible_ == visible)
        return;

    colourBarVisible_ = visible;
    requestUpdate();
}

bool SpectrogramChart::isColourBarVisible() const noexcept
{
    return colourBarVisible_;
}

double SpectrogramChart::duration() const noexcept
{
    if (!(sampleRate_ > 0.0) || frameCount() <= 0)
        return 0.0;

    return static_cast<double>(frameCount()) * hopSize_ / sampleRate_;
}

double SpectrogramChart::nyquistFrequency() const noexcept
{
    if (!(sampleRate_ > 0.0))
        return 0.0;

    return sampleRate_ * 0.5;
}

double SpectrogramChart::binFrequency(int bin) const noexcept
{
    const int rows = frequencyBins();
    const double top = nyquistFrequency();

    if (rows <= 1 || !(top > 0.0))
        return 0.0;

    const int clamped = std::clamp(bin, 0, rows - 1);
    return static_cast<double>(clamped) * top / static_cast<double>(rows - 1);
}

double SpectrogramChart::frequencyFloor() const noexcept
{
    const int rows = frequencyBins();
    const double top = nyquistFrequency();

    if (!(top > 0.0))
        return 0.0;
    if (rows <= 1)
        return top / 1000.0;

    return top / static_cast<double>(rows - 1);
}

QVector<qreal> SpectrogramChart::timeTicks() const
{
    const double span = duration();

    if (!(span > 0.0) || !std::isfinite(span))
        return {0.0, 1.0};

    QVector<qreal> ticks = MathematicsCoordination::niceTicks(
        0.0, static_cast<qreal>(span), 6);

    if (ticks.size() < 2)
        ticks = {0.0, static_cast<qreal>(span)};

    return ticks;
}

QVector<qreal> SpectrogramChart::frequencyTicks() const
{
    const double top = nyquistFrequency();
    const int rows = frequencyBins();

    if (!(top > 0.0) || !std::isfinite(top))
        return {};

    if (!logFrequency_ || rows <= 2)
        return MathematicsCoordination::niceTicks(0.0, static_cast<qreal>(top), 6);

    const double floorHz = frequencyFloor();
    if (!(floorHz > 0.0) || !(floorHz < top))
        return MathematicsCoordination::niceTicks(0.0, static_cast<qreal>(top), 6);

    QVector<qreal> ticks;
    for (int decade = 0; decade < 8 && ticks.size() < kMaximumLogTicks; ++decade) {
        const double base = std::pow(10.0, decade);
        for (const double multiplier : {1.0, 2.0, 5.0}) {
            const double value = multiplier * base;
            if (value < floorHz)
                continue;
            if (value > top)
                break;
            ticks.append(static_cast<qreal>(value));
        }
        if (base > top)
            break;
    }

    // A wide range can exceed the label budget with the 1/2/5 ladder. Keep the
    // decade marks when possible, otherwise thin the list evenly.
    if (ticks.size() > kMaximumLogTicks) {
        QVector<qreal> decades;
        for (const qreal value : ticks) {
            const double exponent = std::log10(static_cast<double>(value));
            const double rounded = std::round(exponent);
            if (std::abs(exponent - rounded) < 1e-9)
                decades.append(value);
        }
        ticks = decades.size() >= 2 ? decades : ticks;
    }

    if (ticks.size() > kMaximumLogTicks) {
        QVector<qreal> thinned;
        const int stride = (ticks.size() + kMaximumLogTicks - 1) / kMaximumLogTicks;
        for (int index = 0; index < ticks.size(); index += stride)
            thinned.append(ticks.at(index));
        if (!thinned.isEmpty() && thinned.last() != ticks.last())
            thinned.append(ticks.last());
        ticks = thinned;
    }

    if (ticks.size() < 2)
        return {static_cast<qreal>(floorHz), static_cast<qreal>(top)};

    return ticks;
}

QString SpectrogramChart::timeTickLabel(qreal seconds) const
{
    const QVector<qreal> ticks = timeTicks();
    qreal step = 1.0;
    if (ticks.size() >= 2) {
        const qreal candidate = ticks.at(1) - ticks.at(0);
        if (candidate > 0.0)
            step = candidate;
    }

    return MathematicsCoordination::tickLabel(seconds, step);
}

QString SpectrogramChart::frequencyTickLabel(qreal hertz) const
{
    if (!std::isfinite(hertz))
        return QString();

    if (std::abs(hertz) >= 1000.0) {
        return QString::number(hertz / 1000.0, 'g', 3) + QLatin1Char('k');
    }

    return QString::number(hertz, 'g', 4);
}

qreal SpectrogramChart::frequencyFraction(double hertz) const
{
    const double top = nyquistFrequency();
    if (!(top > 0.0))
        return 0.0;

    if (!logFrequency_ || frequencyBins() <= 2) {
        return static_cast<qreal>(std::clamp(hertz / top, 0.0, 1.0));
    }

    const double floorHz = std::max(frequencyFloor(), 1e-9);
    const double high = std::max(top, floorHz * (1.0 + 1e-6));
    const double clamped = std::clamp(hertz, floorHz, high);
    const double denominator = std::log10(high) - std::log10(floorHz);
    if (!(denominator > 0.0))
        return 0.0;

    return static_cast<qreal>(std::clamp(
        (std::log10(clamped) - std::log10(floorHz)) / denominator, 0.0, 1.0));
}

SpectrogramChart::Layout SpectrogramChart::layoutFor(const QRectF& plot) const
{
    Layout layout;

    if (plot.isEmpty() || plot.width() <= 0.0 || plot.height() <= 0.0)
        return layout;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFont labelFont = Font::resolve(this, theme.type().labelSmall, density);
    const QFontMetricsF labelMetrics(labelFont);
    const QFont titleFont = Font::resolve(this, theme.type().labelMedium, density);
    const QFontMetricsF titleMetrics(titleFont);
    const qreal gap = density.dp(kGapDp);

    qreal widestFrequency = 0.0;
    for (const qreal value : frequencyTicks()) {
        widestFrequency = std::max(
            widestFrequency,
            labelMetrics.horizontalAdvance(frequencyTickLabel(value)));
    }
    widestFrequency = std::max(widestFrequency,
                               labelMetrics.horizontalAdvance(frequencyTickLabel(0.0)));

    const qreal leftGutter = std::max<qreal>(
        density.dp(kMinimumLeftGutterDp),
        titleMetrics.height() + density.dp(kAxisTitleGapDp) + widestFrequency + gap);

    const qreal bottomGutter = std::max<qreal>(
        density.dp(kMinimumBottomGutterDp),
        labelMetrics.height() + gap + titleMetrics.height() + density.dp(kAxisTitleGapDp));

    qreal rightGutter = gap;
    if (colourBarVisible_) {
        qreal widestLevel = 0.0;
        const QVector<qreal> ticks = MathematicsCoordination::niceTicks(
            static_cast<qreal>(dbMinimum_), static_cast<qreal>(dbMaximum_), 5);
        for (const qreal value : ticks) {
            widestLevel = std::max(
                widestLevel,
                labelMetrics.horizontalAdvance(MathematicsCoordination::tickLabel(
                    value, ticks.size() >= 2 ? ticks.at(1) - ticks.at(0) : 1.0)));
        }
        widestLevel += labelMetrics.horizontalAdvance(QStringLiteral(" dB"));
        rightGutter = density.dp(kColourBarGapDp) + density.dp(kColourBarWidthDp)
            + gap + widestLevel + gap;
    }

    const QRectF data = plot.adjusted(leftGutter, density.dp(kTopInsetDp),
                                      -rightGutter, -bottomGutter);
    if (data.width() <= 1.0 || data.height() <= 1.0)
        return layout;

    layout.data = data;

    if (colourBarVisible_) {
        const QRectF bar(data.right() + density.dp(kColourBarGapDp), data.top(),
                         density.dp(kColourBarWidthDp), data.height());
        if (bar.right() < plot.right())
            layout.colourBar = bar;
    }

    return layout;
}

QRectF SpectrogramChart::matrixRect() const
{
    return layoutFor(plotRect()).data;
}

QRectF SpectrogramChart::colourBarRect() const
{
    return layoutFor(plotRect()).colourBar;
}

QRectF SpectrogramChart::cellRect(int bin, int frame) const
{
    const int rows = frequencyBins();
    const int columns = frameCount();
    if (bin < 0 || bin >= rows || frame < 0 || frame >= columns)
        return QRectF();

    const Layout layout = layoutFor(plotRect());
    if (layout.data.isEmpty())
        return QRectF();

    const qreal cellWidth = layout.data.width() / static_cast<qreal>(columns);
    const double top = nyquistFrequency();
    const double bottom = frequencyFloor();
    const double df = rows > 1 ? top / static_cast<double>(rows - 1) : top;

    double lower = bin == 0 ? 0.0 : (static_cast<double>(bin) - 0.5) * df;
    double upper = bin == rows - 1 ? top : (static_cast<double>(bin) + 0.5) * df;
    lower = std::clamp(lower, 0.0, top);
    upper = std::clamp(upper, 0.0, top);

    if (logFrequency_ && rows > 2 && bin == 0)
        upper = std::max(upper, std::max(df, bottom));

    const qreal topFraction = frequencyFraction(upper);
    const qreal bottomFraction = frequencyFraction(lower);
    const qreal height = std::max<qreal>(
        0.5, (topFraction - bottomFraction) * layout.data.height());

    return QRectF(layout.data.left() + static_cast<qreal>(frame) * cellWidth,
                  layout.data.bottom() - topFraction * layout.data.height(),
                  cellWidth, height);
}

QPoint SpectrogramChart::cellAt(QPointF position) const
{
    const int rows = frequencyBins();
    const int columns = frameCount();
    if (rows <= 0 || columns <= 0 || !std::isfinite(position.x())
        || !std::isfinite(position.y())) {
        return QPoint(-1, -1);
    }

    const Layout layout = layoutFor(plotRect());
    const QRectF& data = layout.data;
    if (data.isEmpty() || !data.contains(position))
        return QPoint(-1, -1);

    const qreal u = (position.x() - data.left()) / data.width();
    const int frame = std::clamp(static_cast<int>(u * static_cast<qreal>(columns)), 0,
                                 columns - 1);

    int bin = -1;
    if (logFrequency_ && rows > 2) {
        const double top = nyquistFrequency();
        const double floorHz = std::max(frequencyFloor(), 1e-9);
        const qreal fraction = (data.bottom() - position.y()) / data.height();
        const double hertz = floorHz * std::pow(std::max(top, floorHz) / floorHz,
                                                static_cast<double>(fraction));
        bin = static_cast<int>(std::lround(
            hertz / std::max(top, 1e-9) * static_cast<double>(rows - 1)));
    } else {
        const qreal fraction = (data.bottom() - position.y()) / data.height();
        bin = static_cast<int>(std::floor(fraction * static_cast<qreal>(rows)));
    }

    return QPoint(frame, std::clamp(bin, 0, rows - 1));
}

bool SpectrogramChart::sampleAt(QPointF position, double* time, double* frequency,
                                double* level) const
{
    const QPoint cell = cellAt(position);
    if (cell.x() < 0 || cell.y() < 0)
        return false;

    const int frame = cell.x();
    const int bin = cell.y();

    if (time != nullptr) {
        *time = sampleRate_ > 0.0
            ? (static_cast<double>(frame) + 0.5) * hopSize_ / sampleRate_
            : 0.0;
    }
    if (frequency != nullptr)
        *frequency = binFrequency(bin);
    if (level != nullptr)
        *level = levelDb(bin, frame);

    return true;
}

bool SpectrogramChart::hasHover() const noexcept
{
    return hoveredBin_ >= 0 && hoveredFrame_ >= 0;
}

int SpectrogramChart::hoveredBin() const noexcept
{
    return hoveredBin_;
}

int SpectrogramChart::hoveredFrame() const noexcept
{
    return hoveredFrame_;
}

double SpectrogramChart::hoveredTime() const noexcept
{
    return hoveredTime_;
}

double SpectrogramChart::hoveredFrequency() const noexcept
{
    return hoveredFrequency_;
}

double SpectrogramChart::hoveredLevelDb() const noexcept
{
    return hoveredLevelDb_;
}

qreal SpectrogramChart::revealProgress() const noexcept
{
    return animation().progress();
}

bool SpectrogramChart::isRevealing() const noexcept
{
    return animation().isRunning();
}

qreal SpectrogramChart::levelDb(int bin, int frame) const
{
    if (bin < 0 || bin >= frequencyBins())
        return emptyValue();

    const std::vector<double>& row = frames_.at(static_cast<size_t>(bin));
    if (frame < 0 || static_cast<size_t>(frame) >= row.size())
        return emptyValue();

    const double value = row.at(static_cast<size_t>(frame));
    if (!std::isfinite(value))
        return emptyValue();

    if (!magnitudeScale_)
        return static_cast<qreal>(value);

    if (!(value > 0.0))
        return emptyValue();

    return static_cast<qreal>(20.0 * std::log10(value));
}

bool SpectrogramChart::hasSpectrogramValues() const noexcept
{
    for (const std::vector<double>& row : frames_) {
        for (const double value : row) {
            if (!std::isfinite(value))
                continue;
            if (!magnitudeScale_ || value > 0.0)
                return true;
        }
    }

    return false;
}

void SpectrogramChart::syncPresenceSeries()
{
    QVector<ChartSeries> presence;

    if (hasSpectrogramValues()) {
        ChartSeries marker;
        marker.points.append(QPointF(0.0, 0.0));
        presence.append(marker);
    }

    ChartBase::setSeries(std::move(presence));
}

void SpectrogramChart::setHoveredCell(int bin, int frame)
{
    if (hoveredBin_ == bin && hoveredFrame_ == frame)
        return;

    hoveredBin_ = bin;
    hoveredFrame_ = frame;

    if (bin >= 0 && frame >= 0) {
        hoveredTime_ = sampleRate_ > 0.0
            ? (static_cast<double>(frame) + 0.5) * hopSize_ / sampleRate_
            : 0.0;
        hoveredFrequency_ = binFrequency(bin);
        hoveredLevelDb_ = levelDb(bin, frame);
    } else {
        hoveredTime_ = 0.0;
        hoveredFrequency_ = 0.0;
        hoveredLevelDb_ = 0.0;
    }

    requestUpdate();
}

void SpectrogramChart::paintPlot(QPainter& painter, const QRectF& plot)
{
    paintScene(painter, plot, true, 1.0);
}

void SpectrogramChart::paintPlot(QPainter& painter, const QRectF& plot, qreal progress)
{
    paintScene(painter, plot, true, std::clamp(progress, 0.0, 1.0));
}

void SpectrogramChart::paintEmptyState(QPainter& painter, const QRectF& plot)
{
    // The frame is useful even without data: it shows the frequency and time
    // domains the next matrix will occupy.
    paintScene(painter, plot, false, 1.0);
    ChartBase::paintEmptyState(painter, plot);
}

void SpectrogramChart::paintScene(QPainter& painter, const QRectF& plot, bool drawData,
                                  qreal progress)
{
    if (plot.isEmpty())
        return;

    const PainterStateGuard guard(painter);

    try {
        const Theme& theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const ColorScheme& colours = theme.colours();
        const Layout layout = layoutFor(plot);
        const QRectF& data = layout.data;

        if (data.isEmpty())
            return;

        const int rows = frequencyBins();
        const int columns = frameCount();
        const qreal span = static_cast<qreal>(dbMaximum_ - dbMinimum_);
        const QVector<QColor> stops = rampStops(colours);

        // 1. Cells, clipped to the matrix. A partially revealed chart shows the
        //    first (oldest) columns only; column growth is the reveal order.
        if (drawData && rows > 0 && columns > 0) {
            const int visibleColumns = std::clamp(
                static_cast<int>(std::ceil(static_cast<qreal>(columns)
                                           * std::clamp(progress, 0.0, 1.0))),
                0, columns);

            if (visibleColumns > 0) {
                const qreal cellWidth = data.width() / static_cast<qreal>(columns);

                QVector<qreal> fractions(rows + 1, 0.0);
                const double top = nyquistFrequency();
                const double df = rows > 1 ? top / static_cast<double>(rows - 1) : top;

                QVector<double> edges(rows + 1, 0.0);
                edges[0] = 0.0;
                for (int bin = 1; bin < rows; ++bin)
                    edges[bin] = std::max(0.0, (static_cast<double>(bin) - 0.5) * df);
                edges[rows] = top;

                if (logFrequency_ && rows > 2) {
                    edges[1] = std::max(edges[1], df * 1.001);
                }

                for (int bin = 0; bin <= rows; ++bin)
                    fractions[bin] = frequencyFraction(edges.at(bin));

                painter.save();
                painter.setClipRect(data);
                painter.setRenderHint(QPainter::Antialiasing, false);
                painter.setPen(Qt::NoPen);

                for (int column = 0; column < visibleColumns; ++column) {
                    const qreal x = data.left() + static_cast<qreal>(column) * cellWidth;

                    for (int bin = 0; bin < rows; ++bin) {
                        const qreal level = levelDb(bin, column);
                        if (!std::isfinite(level))
                            continue;

                        const qreal fraction = span > 0.0
                            ? std::clamp((level - dbMinimum_) / span, 0.0, 1.0)
                            : 0.0;

                        const qreal bottom = data.bottom()
                            - fractions.at(bin) * data.height();
                        const qreal topEdge = data.bottom()
                            - fractions.at(bin + 1) * data.height();
                        const qreal height = std::max<qreal>(
                            1.0, bottom - topEdge + kCellOverlap);

                        painter.fillRect(QRectF(x, topEdge, cellWidth + kCellOverlap,
                                                height),
                                         rampColour(stops, fraction));
                    }
                }

                painter.restore();
            }
        }

        // 2. Frequency and time grid over the cells.
        QColor grid = colours.outlineVariant;
        grid.setAlphaF(static_cast<float>(kGridAlpha));
        painter.setPen(QPen(grid, std::max<qreal>(1.0, density.dp(1.0))));
        painter.setBrush(Qt::NoBrush);

        for (const qreal hertz : frequencyTicks()) {
            const qreal y = data.bottom()
                - frequencyFraction(hertz) * data.height();
            painter.drawLine(QPointF(data.left(), y), QPointF(data.right(), y));
        }

        const double spanSeconds = duration();
        if (spanSeconds > 0.0) {
            for (const qreal seconds : timeTicks()) {
                if (seconds < 0.0 || seconds > spanSeconds)
                    continue;
                const qreal x = data.left()
                    + static_cast<qreal>(seconds / spanSeconds) * data.width();
                painter.drawLine(QPointF(x, data.top()), QPointF(x, data.bottom()));
            }
        }

        // 3. Frame.
        painter.setPen(QPen(colours.outline, std::max<qreal>(1.0, density.dp(1.0))));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(data);

        // 4. Tick labels and axis titles.
        const QFont labelFont = Font::resolve(this, theme.type().labelSmall, density);
        const QFontMetricsF labelMetrics(labelFont);
        const QFont titleFont = Font::resolve(this, theme.type().labelMedium, density);
        const qreal gap = density.dp(kGapDp);
        const qreal labelHeight = labelMetrics.height();

        painter.setFont(labelFont);
        painter.setPen(colours.onSurfaceVariant);

        qreal widestFrequency = 0.0;
        for (const qreal hertz : frequencyTicks())
            widestFrequency = std::max(widestFrequency,
                                       labelMetrics.horizontalAdvance(frequencyTickLabel(hertz)));

        for (const qreal hertz : frequencyTicks()) {
            const qreal y = data.bottom() - frequencyFraction(hertz) * data.height();
            if (y < data.top() - labelHeight || y > data.bottom() + labelHeight)
                continue;

            const QString text = frequencyTickLabel(hertz);
            const QRectF labelRect(data.left() - gap - widestFrequency,
                                   y - labelHeight * 0.5, widestFrequency, labelHeight);
            painter.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, text);
        }

        if (spanSeconds > 0.0) {
            for (const qreal seconds : timeTicks()) {
                if (seconds < 0.0 || seconds > spanSeconds)
                    continue;

                const qreal x = data.left()
                    + static_cast<qreal>(seconds / spanSeconds) * data.width();
                const QRectF labelRect(x - 40.0, data.bottom() + gap, 80.0, labelHeight);
                painter.drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop,
                                 timeTickLabel(seconds));
            }
        }

        painter.setFont(titleFont);
        const QFontMetricsF titleMetrics(titleFont);
        const QRectF timeTitleRect(data.left(), data.bottom() + gap + labelHeight
                                                       + density.dp(kAxisTitleGapDp),
                                   data.width(), titleMetrics.height());
        painter.drawText(timeTitleRect, Qt::AlignHCenter | Qt::AlignTop,
                         localized(QStringLiteral("chart.spectrogram.time"),
                                   QStringLiteral("Time (s)")));

        // Rotated frequency title in the left gutter.
        painter.save();
        painter.translate(data.left() - gap - widestFrequency - density.dp(kGapDp),
                          data.center().y());
        painter.rotate(-90.0);
        const QRectF frequencyTitleRect(-data.height() * 0.5,
                                        -titleMetrics.height() * 0.5,
                                        data.height(), titleMetrics.height());
        painter.drawText(frequencyTitleRect, Qt::AlignCenter,
                         localized(QStringLiteral("chart.spectrogram.frequency"),
                                   QStringLiteral("Frequency (Hz)")));
        painter.restore();

        // 5. Colour bar.
        if (!layout.colourBar.isEmpty()) {
            const QRectF bar = layout.colourBar;
            QLinearGradient gradient(bar.topLeft(), bar.bottomLeft());
            for (int index = 0; index < stops.size(); ++index) {
                const qreal position = stops.size() > 1
                    ? static_cast<qreal>(index) / static_cast<qreal>(stops.size() - 1)
                    : 0.0;
                gradient.setColorAt(position, stops.at(stops.size() - 1 - index));
            }

            painter.fillRect(bar, gradient);
            painter.setPen(QPen(colours.outline, std::max<qreal>(1.0, density.dp(1.0))));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(bar);

            const QVector<qreal> levelTicks = MathematicsCoordination::niceTicks(
                static_cast<qreal>(dbMinimum_), static_cast<qreal>(dbMaximum_), 5);
            const qreal step = levelTicks.size() >= 2
                ? levelTicks.at(1) - levelTicks.at(0)
                : 1.0;

            painter.setFont(labelFont);
            painter.setPen(colours.onSurfaceVariant);

            for (int index = 0; index < levelTicks.size(); ++index) {
                const qreal value = levelTicks.at(index);
                const qreal fraction = span > 0.0
                    ? std::clamp((value - dbMinimum_) / span, 0.0, 1.0)
                    : 0.0;
                const qreal y = bar.bottom() - fraction * bar.height();
                if (y < bar.top() - labelHeight || y > bar.bottom() + labelHeight)
                    continue;

                QString text = MathematicsCoordination::tickLabel(value, step);
                if (index == levelTicks.size() - 1)
                    text += QStringLiteral(" dB");

                painter.drawText(QRectF(bar.right() + gap, y - labelHeight * 0.5,
                                        80.0, labelHeight),
                                 Qt::AlignLeft | Qt::AlignVCenter, text);
            }
        }

        // 6. Hover crosshair and readout.
        if (drawData && hasHover() && hoveredFrame_ < columns && hoveredBin_ < rows) {
            const QRectF cell = cellRect(hoveredBin_, hoveredFrame_);
            if (!cell.isEmpty()) {
                QColor crosshair = colours.onSurface;
                crosshair.setAlphaF(static_cast<float>(kCrosshairAlpha));
                painter.setPen(QPen(crosshair, std::max<qreal>(1.0, density.dp(1.0)),
                                    Qt::DashLine));
                painter.drawLine(QPointF(cell.center().x(), data.top()),
                                 QPointF(cell.center().x(), data.bottom()));
                painter.drawLine(QPointF(data.left(), cell.center().y()),
                                 QPointF(data.right(), cell.center().y()));
            }

            const QString text = QStringLiteral("%1 s   %2   %3")
                .arg(QString::number(hoveredTime_, 'f', 3),
                     formatFrequencyReadout(hoveredFrequency_),
                     formatLevelReadout(hoveredLevelDb_));

            const QFontMetricsF tooltipMetrics(labelFont);
            const qreal padding = density.dp(kTooltipPaddingDp);
            const qreal textWidth = tooltipMetrics.horizontalAdvance(text);
            const QRectF tooltip(data.left() + gap, data.top() + gap,
                                 textWidth + padding * 2.0,
                                 tooltipMetrics.height() + padding * 2.0);

            QColor fill = colours.surfaceContainerHigh;
            fill.setAlphaF(static_cast<float>(kTooltipAlpha));

            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(QPen(colours.outlineVariant, 1.0));
            painter.setBrush(fill);
            painter.drawRoundedRect(tooltip, density.dp(3.0), density.dp(3.0));

            painter.setFont(labelFont);
            painter.setPen(colours.onSurface);
            painter.drawText(tooltip.adjusted(padding, 0.0, -padding, 0.0),
                             Qt::AlignLeft | Qt::AlignVCenter, text);
        }
    } catch (const std::exception& error) {
        qWarning("md::SpectrogramChart::paintScene failed: %s", error.what());
    } catch (...) {
        qWarning("md::SpectrogramChart::paintScene failed with an unknown exception");
    }
}

void SpectrogramChart::mouseMoveEvent(QMouseEvent* event)
{
    try {
        View::mouseMoveEvent(event);

        if (sampleAt(event->position())) {
            const QPoint cell = cellAt(event->position());
            setHoveredCell(cell.y(), cell.x());
        } else {
            setHoveredCell(-1, -1);
        }
    } catch (const std::exception& error) {
        qWarning("md::SpectrogramChart::mouseMoveEvent failed: %s", error.what());
        setHoveredCell(-1, -1);
    } catch (...) {
        qWarning("md::SpectrogramChart::mouseMoveEvent failed with an unknown exception");
        setHoveredCell(-1, -1);
    }
}

void SpectrogramChart::leaveEvent(QEvent* event)
{
    try {
        View::leaveEvent(event);
        setHoveredCell(-1, -1);
    } catch (const std::exception& error) {
        qWarning("md::SpectrogramChart::leaveEvent failed: %s", error.what());
        setHoveredCell(-1, -1);
    } catch (...) {
        qWarning("md::SpectrogramChart::leaveEvent failed with an unknown exception");
        setHoveredCell(-1, -1);
    }
}

} // namespace md
