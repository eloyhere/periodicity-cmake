// Periodicity — the TopographyChart component.
//
// Marching squares runs on the lattice of sample values, not on a mutable
// picture. Each lattice cell contributes zero, one or two segments; the
// segments are stitched by matching their quantized end points, which turns the
// per-cell output into continuous iso-lines that can be drawn partially for the
// reveal. The ambiguous cases (5 and 10) use the cell-centre average, so a
// saddle is resolved consistently with the bilinear interpretation that
// fieldAt() uses.
//
// The filled bands are a rasterized bilinear interpolation of the same field,
// sampled at a bounded resolution and cached. Bands and lines therefore agree
// about where a level is, and the colour bar uses the same sequential ramp.
//
// Both caches are keyed by a geometry generation and the field rectangle. A
// theme switch invalidates the band raster (its colours came from the old
// theme); contour geometry does not depend on colours and survives.
//
// Every entry point catches locally so a broken theme cannot unwind into Qt.

#include <md/components/topography_chart.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QBrush>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>

namespace md {
namespace {

constexpr qreal kPi = 3.14159265358979323846;
constexpr qreal kDegreesPerRadian = 180.0 / kPi;

constexpr qreal kLabelGapDp = 4.0;
constexpr qreal kBarWidthDp = 9.0;
constexpr qreal kBarLabelGapDp = 3.0;
constexpr qreal kGridWidthDp = 0.75;
constexpr qreal kGridAlpha = 0.32;
constexpr qreal kContourAlpha = 0.85;
constexpr qreal kLabelInsetDp = 2.5;
constexpr qreal kHoverRadiusDp = 5.0;
constexpr qreal kReadoutInsetDp = 4.0;
constexpr qreal kReadoutGapDp = 8.0;
constexpr qreal kMinimumGridSpacingDp = 8.0;
constexpr int kMaximumBandPixels = 480;

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

[[nodiscard]] QColor interpolateColour(const QColor& from, const QColor& to, qreal fraction)
{
    const qreal t = std::clamp(fraction, 0.0, 1.0);
    const auto channel = [t](int start, int end) {
        return static_cast<int>(qRound(start + (end - start) * t));
    };

    return QColor(channel(from.red(), to.red()),
                  channel(from.green(), to.green()),
                  channel(from.blue(), to.blue()));
}

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

[[nodiscard]] qreal distanceBetween(const QPointF& first, const QPointF& second)
{
    return std::hypot(second.x() - first.x(), second.y() - first.y());
}

[[nodiscard]] qreal polylineLength(const QPolygonF& line)
{
    qreal length = 0.0;
    for (qsizetype index = 1; index < line.size(); ++index)
        length += distanceBetween(line.at(index - 1), line.at(index));
    return length;
}

// Samples a polyline at `fraction` of its length and reports the local tangent.
[[nodiscard]] bool polylineSample(const QPolygonF& line, qreal fraction,
                                  QPointF& point, QPointF& tangent)
{
    if (line.size() < 2)
        return false;

    const qreal total = polylineLength(line);
    if (!(total > 0.0))
        return false;

    const qreal target = std::clamp(fraction, 0.0, 1.0) * total;
    qreal travelled = 0.0;

    for (qsizetype index = 1; index < line.size(); ++index) {
        const QPointF first = line.at(index - 1);
        const QPointF second = line.at(index);
        const qreal length = distanceBetween(first, second);
        if (!(length > 0.0))
            continue;

        if (travelled + length >= target) {
            const qreal local = (target - travelled) / length;
            point = first + (second - first) * local;
            tangent = (second - first) / length;
            return true;
        }
        travelled += length;
    }

    point = line.last();
    tangent = line.last() - line.at(line.size() - 2);
    const qreal length = std::hypot(tangent.x(), tangent.y());
    if (length > 0.0)
        tangent /= length;
    return true;
}

// Appends the first `fraction` of a polyline to a painter path. A fraction of
// one appends the whole line; zero appends nothing.
void appendPartialPolyline(QPainterPath& path, const QPolygonF& line, qreal fraction)
{
    if (line.size() < 2 || !(fraction > 0.0))
        return;

    if (fraction >= 1.0) {
        path.moveTo(line.first());
        for (qsizetype index = 1; index < line.size(); ++index)
            path.lineTo(line.at(index));
        return;
    }

    const qreal total = polylineLength(line);
    if (!(total > 0.0))
        return;

    const qreal wanted = total * std::clamp(fraction, 0.0, 1.0);
    qreal travelled = 0.0;

    path.moveTo(line.first());
    for (qsizetype index = 1; index < line.size(); ++index) {
        const QPointF first = line.at(index - 1);
        const QPointF second = line.at(index);
        const qreal length = distanceBetween(first, second);
        if (!(length > 0.0))
            continue;

        if (travelled + length >= wanted) {
            const qreal local = (wanted - travelled) / length;
            path.lineTo(first + (second - first) * local);
            return;
        }

        path.lineTo(second);
        travelled += length;
    }
}

} // namespace

TopographyChart::TopographyChart(QWidget* parent)
    : ChartBase(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setAxesVisible(false);

    // The field is a colour reading; the shared whole-surface state layer would
    // tint every band. The chart paints its own hover dot and readout instead.
    setInteractionFeedbackEnabled(false);
    animation().setDuration(MotionLevel::Level2);
}

void TopographyChart::setField(int rows, int columns, QVector<qreal> values)
{
    rows_ = std::clamp(rows, 0, 2048);
    columns_ = std::clamp(columns, 0, 2048);

    if (rows_ == 0 || columns_ == 0) {
        rows_ = 0;
        columns_ = 0;
        values_.clear();
    } else {
        const qsizetype expected = static_cast<qsizetype>(rows_) * columns_;
        QVector<qreal> normalized(expected, emptyValue());
        const qsizetype count = std::min<qsizetype>(expected, values.size());
        for (qsizetype index = 0; index < count; ++index) {
            const qreal sample = values.at(index);
            if (std::isfinite(sample))
                normalized[index] = sample;
        }
        values_ = std::move(normalized);
    }

    ++geometryGeneration_;
    contourCacheGeneration_ = 0;
    bandCacheGeneration_ = 0;
    clearHoveredCell();

    syncPresenceSeries();
    requestUpdate();
}

int TopographyChart::rows() const noexcept
{
    return rows_;
}

int TopographyChart::columns() const noexcept
{
    return columns_;
}

const QVector<qreal>& TopographyChart::values() const noexcept
{
    return values_;
}

qreal TopographyChart::value(int row, int column) const
{
    if (row < 0 || row >= rows_ || column < 0 || column >= columns_)
        return emptyValue();
    if (row * columns_ + column >= values_.size())
        return emptyValue();

    return values_.at(row * columns_ + column);
}

void TopographyChart::setLevels(int count)
{
    const int next = std::clamp(count, 1, 24);
    if (levelCount_ == next)
        return;

    levelCount_ = next;
    ++geometryGeneration_;
    contourCacheGeneration_ = 0;
    requestUpdate();
}

int TopographyChart::levels() const noexcept
{
    return levelCount_;
}

void TopographyChart::setFilledBands(bool filled)
{
    if (filled_ == filled)
        return;

    filled_ = filled;
    requestUpdate();
}

bool TopographyChart::hasFilledBands() const noexcept
{
    return filled_;
}

void TopographyChart::setUnit(const QString& unit)
{
    if (unit_ == unit)
        return;

    unit_ = unit;
    requestUpdate();
}

QString TopographyChart::unit() const
{
    return unit_;
}

bool TopographyChart::finiteRange(qreal& minimum, qreal& maximum) const
{
    bool found = false;
    qreal low = 0.0;
    qreal high = 0.0;

    for (qreal sample : values_) {
        if (!std::isfinite(sample))
            continue;

        if (!found) {
            low = high = sample;
            found = true;
        } else {
            low = std::min(low, sample);
            high = std::max(high, sample);
        }
    }

    minimum = low;
    maximum = high;
    return found;
}

QVector<qreal> TopographyChart::contourLevels() const
{
    qreal minimum = 0.0;
    qreal maximum = 0.0;
    if (!finiteRange(minimum, maximum) || maximum - minimum <= 0.0)
        return {};

    const int count = std::clamp(levelCount_, 1, 24);
    QVector<qreal> levels;
    levels.reserve(count);
    const qreal span = maximum - minimum;

    for (int index = 1; index <= count; ++index)
        levels.append(minimum + span * index / (count + 1));

    return levels;
}

QColor TopographyChart::rampColour(qreal fraction) const
{
    const ColorScheme& colours = Theme::forWidget(this).colours();

    QColor low = colours.surfaceContainerHighest;
    QColor middle = colours.secondaryContainer;
    QColor high = colours.primary;

    if (!low.isValid())
        low = colours.surface;
    if (!middle.isValid())
        middle = colours.secondary.isValid() ? colours.secondary : colours.primary;
    if (!high.isValid())
        high = colours.onSurface;

    const qreal t = std::clamp(fraction, 0.0, 1.0);
    if (t < 0.5)
        return interpolateColour(low, middle, t * 2.0);
    return interpolateColour(middle, high, (t - 0.5) * 2.0);
}

TopographyChart::Layout TopographyChart::layoutFor(const QRectF& plot) const
{
    Layout layout;
    if (plot.isEmpty() || rows_ <= 0 || columns_ <= 0)
        return layout;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFontMetricsF metrics(Font::resolve(this, theme.type().labelSmall, density));

    const qreal gap = density.dp(kLabelGapDp);
    const qreal barWidth = density.dp(kBarWidthDp);
    const qreal barLabelGap = density.dp(kBarLabelGapDp);

    qreal minimum = 0.0;
    qreal maximum = 0.0;
    if (!finiteRange(minimum, maximum)) {
        minimum = 0.0;
        maximum = 1.0;
    }

    qreal labelWidth = std::max(metrics.horizontalAdvance(formatValue(minimum)),
                                metrics.horizontalAdvance(formatValue(maximum)));
    labelWidth = std::max<qreal>(labelWidth, density.dp(10.0));

    layout.labelGap = gap;
    layout.labelHeight = metrics.height();
    layout.barLabelWidth = labelWidth;

    const qreal reserve = gap + barWidth + barLabelGap + labelWidth;
    QRectF field = plot;
    field.setTop(plot.top() + metrics.height() + gap);
    field.setRight(plot.right() - reserve);

    if (field.width() <= 0.0 || field.height() <= 0.0)
        return layout;

    layout.field = field;
    layout.bar = QRectF(field.right() + gap, field.top(), barWidth, field.height());
    layout.valid = true;
    return layout;
}

QRectF TopographyChart::fieldRect() const
{
    return layoutFor(plotRect()).field;
}

QPointF TopographyChart::samplePoint(const QRectF& field, int row, int column) const
{
    const qreal x = columns_ > 1
        ? field.left() + field.width() * column / static_cast<qreal>(columns_ - 1)
        : field.center().x();
    const qreal y = rows_ > 1
        ? field.top() + field.height() * row / static_cast<qreal>(rows_ - 1)
        : field.center().y();
    return QPointF(x, y);
}

qreal TopographyChart::sampleNormalised(qreal u, qreal v) const
{
    if (rows_ <= 0 || columns_ <= 0 || values_.isEmpty())
        return emptyValue();

    const qreal sampleX = std::clamp(u, 0.0, 1.0) * (columns_ - 1);
    const qreal sampleY = std::clamp(v, 0.0, 1.0) * (rows_ - 1);

    const int column0 = std::clamp(static_cast<int>(std::floor(sampleX)), 0, columns_ - 1);
    const int row0 = std::clamp(static_cast<int>(std::floor(sampleY)), 0, rows_ - 1);
    const int column1 = std::min(column0 + 1, columns_ - 1);
    const int row1 = std::min(row0 + 1, rows_ - 1);

    const qreal first = values_.at(row0 * columns_ + column0);
    const qreal topRight = values_.at(row0 * columns_ + column1);
    const qreal bottomLeft = values_.at(row1 * columns_ + column0);
    const qreal bottomRight = values_.at(row1 * columns_ + column1);

    if (!std::isfinite(first) || !std::isfinite(topRight)
        || !std::isfinite(bottomLeft) || !std::isfinite(bottomRight)) {
        return emptyValue();
    }

    const qreal fx = sampleX - column0;
    const qreal fy = sampleY - row0;

    const qreal top = first * (1.0 - fx) + topRight * fx;
    const qreal bottom = bottomLeft * (1.0 - fx) + bottomRight * fx;
    return top * (1.0 - fy) + bottom * fy;
}

QPoint TopographyChart::cellAt(QPointF position) const
{
    const Layout layout = layoutFor(plotRect());
    if (!layout.valid)
        return QPoint(-1, -1);
    if (!std::isfinite(position.x()) || !std::isfinite(position.y()))
        return QPoint(-1, -1);
    if (!layout.field.contains(position))
        return QPoint(-1, -1);

    const qreal u = (position.x() - layout.field.left()) / layout.field.width();
    const qreal v = (position.y() - layout.field.top()) / layout.field.height();

    const int column = columns_ > 1
        ? std::clamp(qRound(u * (columns_ - 1)), 0, columns_ - 1)
        : 0;
    const int row = rows_ > 1
        ? std::clamp(qRound(v * (rows_ - 1)), 0, rows_ - 1)
        : 0;

    return QPoint(column, row);
}

qreal TopographyChart::fieldAt(QPointF position) const
{
    const Layout layout = layoutFor(plotRect());
    if (!layout.valid || !layout.field.contains(position))
        return emptyValue();

    const qreal u = (position.x() - layout.field.left()) / layout.field.width();
    const qreal v = (position.y() - layout.field.top()) / layout.field.height();
    return sampleNormalised(u, v);
}

QPoint TopographyChart::hoveredCell() const noexcept
{
    if (hoveredRow_ < 0 || hoveredColumn_ < 0)
        return QPoint(-1, -1);

    return QPoint(hoveredColumn_, hoveredRow_);
}

qreal TopographyChart::hoveredValue() const
{
    if (hoveredRow_ < 0 || hoveredColumn_ < 0)
        return emptyValue();
    return hoveredValue_;
}

QVector<TopographyChart::Segment> TopographyChart::buildSegments(qreal level,
                                                                 const QRectF& field) const
{
    QVector<Segment> segments;

    if (rows_ < 2 || columns_ < 2 || !std::isfinite(level) || field.isEmpty())
        return segments;

    const auto sample = [this](int row, int column) {
        return values_.at(row * columns_ + column);
    };

    const auto edgePoint = [level](const QPointF& first, const QPointF& second,
                                   qreal firstValue, qreal secondValue) {
        const qreal denominator = secondValue - firstValue;
        qreal fraction = std::abs(denominator) > 1e-15
            ? (level - firstValue) / denominator
            : 0.5;
        fraction = std::clamp(fraction, 0.0, 1.0);
        return first + (second - first) * fraction;
    };

    for (int row = 0; row < rows_ - 1; ++row) {
        for (int column = 0; column < columns_ - 1; ++column) {
            const qreal topLeft = sample(row, column);
            const qreal topRightValue = sample(row, column + 1);
            const qreal bottomRight = sample(row + 1, column + 1);
            const qreal bottomLeft = sample(row + 1, column);

            if (!std::isfinite(topLeft) || !std::isfinite(topRightValue)
                || !std::isfinite(bottomRight) || !std::isfinite(bottomLeft)) {
                continue;
            }

            const QPointF topLeftPoint = samplePoint(field, row, column);
            const QPointF topRightPoint = samplePoint(field, row, column + 1);
            const QPointF bottomRightPoint = samplePoint(field, row + 1, column + 1);
            const QPointF bottomLeftPoint = samplePoint(field, row + 1, column);

            const QPointF top = edgePoint(topLeftPoint, topRightPoint,
                                          topLeft, topRightValue);
            const QPointF right = edgePoint(topRightPoint, bottomRightPoint,
                                            topRightValue, bottomRight);
            const QPointF bottom = edgePoint(bottomLeftPoint, bottomRightPoint,
                                             bottomLeft, bottomRight);
            const QPointF left = edgePoint(topLeftPoint, bottomLeftPoint,
                                           topLeft, bottomLeft);

            int index = 0;
            if (topLeft > level)
                index |= 1;
            if (topRightValue > level)
                index |= 2;
            if (bottomRight > level)
                index |= 4;
            if (bottomLeft > level)
                index |= 8;

            const qreal centre = (topLeft + topRightValue + bottomRight + bottomLeft) / 4.0;
            const auto add = [&segments](const QPointF& first, const QPointF& second) {
                if (distanceBetween(first, second) > 1e-9)
                    segments.append(Segment{first, second});
            };

            switch (index) {
            case 0:
            case 15:
                break;
            case 1:
                add(left, top);
                break;
            case 2:
                add(top, right);
                break;
            case 3:
                add(left, right);
                break;
            case 4:
                add(right, bottom);
                break;
            case 5:
                if (centre > level) {
                    add(top, right);
                    add(bottom, left);
                } else {
                    add(left, top);
                    add(right, bottom);
                }
                break;
            case 6:
                add(top, bottom);
                break;
            case 7:
                add(left, bottom);
                break;
            case 8:
                add(bottom, left);
                break;
            case 9:
                add(top, bottom);
                break;
            case 10:
                if (centre > level) {
                    add(left, top);
                    add(right, bottom);
                } else {
                    add(top, right);
                    add(bottom, left);
                }
                break;
            case 11:
                add(right, bottom);
                break;
            case 12:
                add(left, right);
                break;
            case 13:
                add(top, right);
                break;
            case 14:
                add(left, top);
                break;
            default:
                break;
            }
        }
    }

    return segments;
}

QVector<QPolygonF> TopographyChart::buildPolylines(qreal level, const QRectF& field) const
{
    const QVector<Segment> segments = buildSegments(level, field);
    QVector<QPolygonF> polylines;

    if (segments.isEmpty())
        return polylines;

    const auto keyOf = [](const QPointF& point) -> quint64 {
        const qint64 x = qRound64(point.x() * 8.0);
        const qint64 y = qRound64(point.y() * 8.0);
        return (static_cast<quint64>(static_cast<quint32>(x)) << 32)
            | static_cast<quint32>(y);
    };

    QHash<quint64, QVector<int>> adjacency;
    adjacency.reserve(segments.size() * 2);
    for (int index = 0; index < segments.size(); ++index) {
        adjacency[keyOf(segments.at(index).first)].append(index);
        adjacency[keyOf(segments.at(index).second)].append(index);
    }

    QVector<bool> used(segments.size(), false);

    for (int start = 0; start < segments.size(); ++start) {
        if (used.at(start))
            continue;

        used[start] = true;
        QVector<QPointF> points;
        points.append(segments.at(start).first);
        points.append(segments.at(start).second);

        const auto extend = [&](QPointF from, bool front) {
            for (;;) {
                const QVector<int> candidates = adjacency.value(keyOf(from));
                int next = -1;
                for (int candidate : candidates) {
                    if (!used.at(candidate)) {
                        next = candidate;
                        break;
                    }
                }

                if (next < 0)
                    return;

                used[next] = true;
                const Segment& segment = segments.at(next);
                const QPointF other = keyOf(segment.first) == keyOf(from)
                    ? segment.second
                    : segment.first;

                if (front)
                    points.prepend(other);
                else
                    points.append(other);

                from = other;
            }
        };

        extend(points.last(), false);
        extend(points.first(), true);

        QPolygonF polyline;
        polyline.reserve(points.size());
        for (const QPointF& point : points)
            polyline.append(point);
        polylines.append(std::move(polyline));
    }

    return polylines;
}

QVector<QLineF> TopographyChart::contourLines(qreal level) const
{
    const Layout layout = layoutFor(plotRect());
    if (!layout.valid)
        return {};

    const QVector<Segment> segments = buildSegments(level, layout.field);
    QVector<QLineF> lines;
    lines.reserve(segments.size());
    for (const Segment& segment : segments)
        lines.append(QLineF(segment.first, segment.second));
    return lines;
}

int TopographyChart::contourSegmentCount(qreal level) const
{
    return static_cast<int>(contourLines(level).size());
}

int TopographyChart::totalContourSegments() const
{
    int total = 0;
    for (qreal level : contourLevels())
        total += contourSegmentCount(level);
    return total;
}

void TopographyChart::ensureContours(const QRectF& field)
{
    if (contourCacheGeneration_ == geometryGeneration_ && contourCacheArea_ == field)
        return;

    contourCache_.clear();
    const QVector<qreal> levels = contourLevels();
    contourCache_.reserve(levels.size());
    for (qreal level : levels)
        contourCache_.append(buildPolylines(level, field));

    contourCacheArea_ = field;
    contourCacheGeneration_ = geometryGeneration_;
}

void TopographyChart::ensureBands(const QRectF& field, qreal minimum, qreal maximum)
{
    if (bandCacheGeneration_ == geometryGeneration_
        && bandCacheArea_.size() == field.size() && !bandCache_.isNull()) {
        return;
    }

    bandCache_ = QImage();
    bandCacheArea_ = field;

    if (field.width() <= 0.0 || field.height() <= 0.0 || !(maximum - minimum > 0.0))
        return;

    const int width = std::clamp(static_cast<int>(std::ceil(field.width())),
                                 32, kMaximumBandPixels);
    const int height = std::clamp(static_cast<int>(std::ceil(field.height())),
                                  32, kMaximumBandPixels);

    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    const QVector<qreal> levels = contourLevels();
    const int bandCount = levels.size();

    QVector<QRgb> bandColours;
    bandColours.reserve(bandCount + 1);
    for (int band = 0; band <= bandCount; ++band) {
        const qreal fraction = static_cast<qreal>(band + 0.5)
            / static_cast<qreal>(bandCount + 1);
        bandColours.append(rampColour(fraction).rgb());
    }

    for (int y = 0; y < height; ++y) {
        const qreal v = (y + 0.5) / static_cast<qreal>(height);
        for (int x = 0; x < width; ++x) {
            const qreal u = (x + 0.5) / static_cast<qreal>(width);
            const qreal sample = sampleNormalised(u, v);
            if (!std::isfinite(sample))
                continue;

            const auto position = std::upper_bound(levels.cbegin(), levels.cend(), sample);
            const int band = std::clamp(static_cast<int>(position - levels.cbegin()),
                                        0, bandCount);
            image.setPixel(x, y, bandColours.at(band));
        }
    }

    bandCache_ = std::move(image);
    bandCacheGeneration_ = geometryGeneration_;
}

bool TopographyChart::hasFieldValues() const noexcept
{
    for (qreal sample : values_) {
        if (std::isfinite(sample))
            return true;
    }
    return false;
}

void TopographyChart::syncPresenceSeries()
{
    QVector<ChartSeries> presence;

    if (hasFieldValues()) {
        ChartSeries marker;
        marker.points.append(QPointF(0.0, 0.0));
        presence.append(marker);
    }

    ChartBase::setSeries(std::move(presence));
}

void TopographyChart::setHoveredFieldCell(int row, int column, qreal value,
                                          QPointF position)
{
    hoveredRow_ = row;
    hoveredColumn_ = column;
    hoveredValue_ = value;
    hoverPosition_ = position;
    requestUpdate();
}

void TopographyChart::clearHoveredCell()
{
    if (hoveredRow_ < 0 && hoveredColumn_ < 0)
        return;

    hoveredRow_ = -1;
    hoveredColumn_ = -1;
    hoveredValue_ = emptyValue();
    requestUpdate();
}

qreal TopographyChart::revealProgress() const noexcept
{
    return animation().progress();
}

bool TopographyChart::isRevealing() const noexcept
{
    return animation().isRunning();
}

void TopographyChart::onThemeChanged()
{
    try {
        ChartBase::onThemeChanged();

        // The band raster baked the previous theme's colours.
        bandCache_ = QImage();
        bandCacheGeneration_ = 0;
        requestUpdate();
    } catch (const std::exception& error) {
        qWarning("md::TopographyChart::onThemeChanged failed: %s", error.what());
    } catch (...) {
        qWarning("md::TopographyChart::onThemeChanged failed with an unknown exception");
    }
}

void TopographyChart::paintPlot(QPainter& painter, const QRectF& plot)
{
    paintPlot(painter, plot, 1.0);
}

void TopographyChart::paintPlot(QPainter& painter, const QRectF& plot, qreal progress)
{
    if (plot.isEmpty() || values_.isEmpty() || rows_ <= 0 || columns_ <= 0)
        return;

    const PainterStateGuard guard(painter);

    try {
        const qreal stage = std::clamp(progress, 0.0, 1.0);
        const Theme& theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const ColorScheme& colours = theme.colours();
        const Layout layout = layoutFor(plot);
        if (!layout.valid)
            return;

        qreal minimum = 0.0;
        qreal maximum = 0.0;
        if (!finiteRange(minimum, maximum))
            return;
        const qreal span = maximum - minimum;

        const QFont labelFont = Font::resolve(this, theme.type().labelSmall, density);
        const QFontMetricsF labelMetrics(labelFont);

        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setBrush(Qt::NoBrush);

        // 1. Filled bands. The raster is cached and only its opacity follows the
        //    reveal, so a frame is cheap after the first one.
        if (filled_) {
            if (span > 0.0) {
                ensureBands(layout.field, minimum, maximum);
                if (!bandCache_.isNull()) {
                    painter.save();
                    painter.setOpacity(stage);
                    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                    painter.drawImage(layout.field, bandCache_);
                    painter.restore();
                }
            } else {
                QColor flat = rampColour(0.5);
                flat.setAlphaF(static_cast<float>(stage));
                painter.fillRect(layout.field, flat);
            }
        }

        // 2. Sample grid. It is skipped when the lattice would turn into a moiré
        //    field; the border and colour bar still give the frame.
        if (rows_ > 1 || columns_ > 1) {
            const qreal columnSpacing = columns_ > 1
                ? layout.field.width() / (columns_ - 1)
                : layout.field.width();
            const qreal rowSpacing = rows_ > 1
                ? layout.field.height() / (rows_ - 1)
                : layout.field.height();

            if (columnSpacing >= density.dp(kMinimumGridSpacingDp)
                && rowSpacing >= density.dp(kMinimumGridSpacingDp)) {
                QColor grid = colours.outlineVariant;
                grid.setAlphaF(static_cast<float>(kGridAlpha));
                painter.setPen(QPen(grid, std::max<qreal>(1.0, density.dp(kGridWidthDp))));

                for (int column = 0; column < columns_; ++column) {
                    const QPointF point = samplePoint(layout.field, 0, column);
                    painter.drawLine(QPointF(point.x(), layout.field.top()),
                                     QPointF(point.x(), layout.field.bottom()));
                }
                for (int row = 0; row < rows_; ++row) {
                    const QPointF point = samplePoint(layout.field, row, 0);
                    painter.drawLine(QPointF(layout.field.left(), point.y()),
                                     QPointF(layout.field.right(), point.y()));
                }
            }
        }

        // 3. Contours, drawn as a prefix of their stitched polylines.
        const QVector<qreal> levels = contourLevels();
        if (stage > 0.001 && !levels.isEmpty()) {
            ensureContours(layout.field);

            QPainterPath path;
            for (const QVector<QPolygonF>& levelLines : contourCache_) {
                for (const QPolygonF& line : levelLines)
                    appendPartialPolyline(path, line, stage);
            }

            QColor contour = colours.onSurface;
            contour.setAlphaF(static_cast<float>(kContourAlpha));
            painter.setPen(QPen(contour, std::max<qreal>(1.0, density.dp(1.0)),
                                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
        }

        // 4. Border.
        painter.setPen(QPen(colours.outline, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(layout.field);

        // 5. Level labels, only once the contours are settled. The text is
        //    rotated along the local tangent and sits on a small surface patch so
        //    it interrupts the line instead of being struck through by it.
        if (stage >= 0.999 && !contourCache_.isEmpty()) {
            painter.setFont(labelFont);

            for (int index = 0; index < contourCache_.size(); ++index) {
                const QVector<QPolygonF>& lines = contourCache_.at(index);
                const QPolygonF* longest = nullptr;
                qreal longestLength = 0.0;

                for (const QPolygonF& line : lines) {
                    const qreal length = polylineLength(line);
                    if (length > longestLength) {
                        longestLength = length;
                        longest = &line;
                    }
                }

                if (longest == nullptr || longestLength < density.dp(24.0))
                    continue;

                QPointF point;
                QPointF tangent;
                if (!polylineSample(*longest, 0.42, point, tangent))
                    continue;

                qreal angle = std::atan2(tangent.y(), tangent.x()) * kDegreesPerRadian;
                if (angle > 90.0 || angle < -90.0)
                    angle += 180.0;

                const QString text = formatValue(levels.value(index));
                const qreal inset = density.dp(kLabelInsetDp);
                const qreal textWidth = labelMetrics.horizontalAdvance(text);
                const qreal boxWidth = textWidth + inset * 2.0;
                const qreal boxHeight = labelMetrics.height() + inset * 2.0;

                painter.save();
                painter.translate(point);
                painter.rotate(angle);
                const QRectF box(-boxWidth / 2.0, -boxHeight / 2.0, boxWidth, boxHeight);
                QColor patch = colours.surfaceContainerHigh;
                patch.setAlphaF(0.92f);
                painter.setPen(Qt::NoPen);
                painter.setBrush(patch);
                painter.drawRoundedRect(box, inset, inset);
                painter.setPen(colours.onSurface);
                painter.drawText(box, Qt::AlignCenter, text);
                painter.restore();
            }
        }

        // 6. Colour bar and its ticks at the contour levels.
        if (layout.bar.width() > 0.0 && layout.bar.height() > 0.0 && span > 0.0) {
            QLinearGradient gradient(layout.bar.topLeft(), layout.bar.bottomLeft());
            for (int stop = 0; stop <= 4; ++stop) {
                const qreal fraction = stop / 4.0;
                gradient.setColorAt(1.0 - fraction, rampColour(fraction));
            }

            painter.setPen(QPen(colours.outlineVariant, 1.0));
            painter.setBrush(gradient);
            painter.drawRect(layout.bar);

            QColor tick = colours.onSurface;
            tick.setAlphaF(0.6f);
            painter.setPen(QPen(tick, 1.0));
            for (qreal level : levels) {
                const qreal y = layout.bar.top()
                    + (maximum - level) / span * layout.bar.height();
                painter.drawLine(QPointF(layout.bar.left(), y),
                                 QPointF(layout.bar.right(), y));
            }

            painter.setFont(labelFont);
            painter.setPen(colours.onSurfaceVariant);

            const qreal labelLeft = layout.bar.right() + density.dp(kBarLabelGapDp);
            const qreal labelWidth = std::max<qreal>(0.0, plot.right() - labelLeft);
            if (labelWidth > 0.0) {
                painter.drawText(QRectF(labelLeft, layout.bar.top(), labelWidth,
                                        layout.labelHeight),
                                 Qt::AlignLeft | Qt::AlignTop,
                                 formatValue(maximum));
                painter.drawText(QRectF(labelLeft, layout.bar.bottom() - layout.labelHeight,
                                        labelWidth, layout.labelHeight),
                                 Qt::AlignLeft | Qt::AlignBottom,
                                 formatValue(minimum));
            }

            const QString caption = unit_.isEmpty()
                ? localized(QStringLiteral("chart.value"), QStringLiteral("Value"))
                : unit_;
            const qreal captionWidth = std::max<qreal>(0.0, plot.right() - layout.bar.left());
            painter.drawText(QRectF(layout.bar.left(),
                                    plot.top(),
                                    captionWidth, layout.labelHeight),
                             Qt::AlignLeft | Qt::AlignBottom,
                             labelMetrics.elidedText(caption, Qt::ElideRight, captionWidth));
        }

        // 7. Hover marker and value readout.
        if (hoveredRow_ >= 0 && hoveredRow_ < rows_
            && hoveredColumn_ >= 0 && hoveredColumn_ < columns_
            && std::isfinite(hoveredValue_)) {
            const QPointF point = hoverPosition_;
            painter.setPen(QPen(colours.primary, std::max<qreal>(1.5, density.dp(2.0))));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(point, density.dp(kHoverRadiusDp),
                                density.dp(kHoverRadiusDp));
            painter.drawLine(QPointF(point.x() - density.dp(3.0), point.y()),
                             QPointF(point.x() + density.dp(3.0), point.y()));
            painter.drawLine(QPointF(point.x(), point.y() - density.dp(3.0)),
                             QPointF(point.x(), point.y() + density.dp(3.0)));

            const QString text = unit_.isEmpty()
                ? QStringLiteral("r%1 c%2  %3")
                      .arg(hoveredRow_)
                      .arg(hoveredColumn_)
                      .arg(formatValue(hoveredValue_))
                : QStringLiteral("r%1 c%2  %3 %4")
                      .arg(hoveredRow_)
                      .arg(hoveredColumn_)
                      .arg(formatValue(hoveredValue_), unit_);

            const qreal inset = density.dp(kReadoutInsetDp);
            const qreal gap = density.dp(kReadoutGapDp);
            const qreal maximumWidth = std::max<qreal>(0.0, plot.width() - 2.0 * gap);
            const QString shown = labelMetrics.elidedText(text, Qt::ElideRight, maximumWidth);
            const qreal boxWidth = std::min(maximumWidth,
                                            labelMetrics.horizontalAdvance(shown) + 2.0 * inset);
            const qreal boxHeight = layout.labelHeight + 2.0 * inset;

            qreal boxLeft = point.x() + gap;
            if (boxLeft + boxWidth > plot.right())
                boxLeft = point.x() - gap - boxWidth;
            boxLeft = std::clamp(boxLeft, plot.left(), plot.right() - boxWidth);

            qreal boxTop = point.y() - boxHeight / 2.0;
            boxTop = std::clamp(boxTop, plot.top(), plot.bottom() - boxHeight);

            const QRectF box(boxLeft, boxTop, boxWidth, boxHeight);
            painter.setPen(QPen(colours.outlineVariant, 1.0));
            painter.setBrush(colours.surfaceContainerHigh);
            painter.drawRoundedRect(box, inset, inset);

            painter.setFont(labelFont);
            painter.setPen(colours.onSurface);
            painter.drawText(box.adjusted(inset, 0.0, -inset, 0.0),
                             Qt::AlignLeft | Qt::AlignVCenter, shown);
        }
    } catch (const std::exception& error) {
        qWarning("md::TopographyChart::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::TopographyChart::paintPlot failed with an unknown exception");
    }
}

void TopographyChart::mouseMoveEvent(QMouseEvent* event)
{
    try {
        View::mouseMoveEvent(event);
        const QPoint cell = cellAt(event->position());
        if (cell.x() >= 0 && cell.y() >= 0) {
            const qreal sample = fieldAt(event->position());
            if (std::isfinite(sample))
                setHoveredFieldCell(cell.y(), cell.x(), sample, event->position());
            else
                clearHoveredCell();
        } else {
            clearHoveredCell();
        }
    } catch (const std::exception& error) {
        qWarning("md::TopographyChart::mouseMoveEvent failed: %s", error.what());
        clearHoveredCell();
    } catch (...) {
        qWarning("md::TopographyChart::mouseMoveEvent failed with an unknown exception");
        clearHoveredCell();
    }
}

void TopographyChart::leaveEvent(QEvent* event)
{
    try {
        View::leaveEvent(event);
        clearHoveredCell();
    } catch (const std::exception& error) {
        qWarning("md::TopographyChart::leaveEvent failed: %s", error.what());
        clearHoveredCell();
    } catch (...) {
        qWarning("md::TopographyChart::leaveEvent failed with an unknown exception");
        clearHoveredCell();
    }
}

} // namespace md
