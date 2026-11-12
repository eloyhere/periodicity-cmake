// Periodicity — the ChordDiagram component.
//
// The diagram is laid out from the matrix, never from the widget's catalogue:
// every positive unordered pair becomes one ribbon slot, and both of its
// endpoints receive the pair's value in their incident lists. The node arcs are
// then allocated in proportion to those totals. That construction is what makes
// the rim width one global constant times the pair value:
//
//   nodeSpan  = availableArc * nodeTotal / totalWeight
//   pairSpan  = nodeSpan * pairValue / nodeTotal
//             = availableArc * pairValue / totalWeight
//
// so ribbonWidth() is exactly radius * pairSpan for every pair, independent of
// how many ribbons share a node. Gaps and the fixed arcs of isolated nodes are
// accounted for once when availableArc is computed, and do not affect the
// proportionality between positive pairs.
//
// Ribbons are filled paths: each edge follows its source sub-arc, bends towards
// the centre with two cubic segments, follows the target sub-arc in reverse and
// bends back. During the reveal the target sub-arc grows from zero to its full
// span, so a ribbon appears to sweep out from its source arc. The layout itself
// is always settled, which keeps ribbonWidth() and the hit tests stable while
// the animation runs.
//
// Every entry point catches locally so a broken theme cannot unwind into Qt.

#include <md/components/chord_diagram.h>

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
#include <exception>
#include <limits>
#include <utility>

namespace md {
namespace {

constexpr qreal kPi = 3.14159265358979323846;
constexpr qreal kTwoPi = 2.0 * kPi;
constexpr qreal kRadiansPerDegree = kPi / 180.0;
constexpr qreal kDegreesPerRadian = 180.0 / kPi;

constexpr qreal kRingDp = 9.0;
constexpr qreal kLabelGapDp = 6.0;
constexpr qreal kNodeGapDegrees = 2.4;
constexpr qreal kIsolatedArcDegrees = 5.0;
constexpr qreal kMinimumRadiusDp = 22.0;
constexpr qreal kRibbonAlpha = 0.52;
constexpr qreal kDimmedRibbonAlpha = 0.16;
constexpr qreal kHighlightedRibbonAlpha = 0.86;
constexpr qreal kGuideAlpha = 0.4;

[[nodiscard]] qreal normaliseAngle(qreal angle)
{
    qreal result = std::fmod(angle, kTwoPi);
    if (result < 0.0)
        result += kTwoPi;
    return result;
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

ChordDiagram::ChordDiagram(QWidget* parent)
    : ChartBase(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setAxesVisible(false);

    // Ribbons are a colour reading. The whole-surface state layer would tint
    // them after paintPlot(), so the diagram supplies its own node-local hover.
    setInteractionFeedbackEnabled(false);
    animation().setDuration(MotionLevel::Level2);
}

void ChordDiagram::setMatrix(QStringList names, QVector<QVector<qreal>> values)
{
    int count = static_cast<int>(names.size());
    count = std::max<int>(count, static_cast<int>(values.size()));
    for (const QVector<qreal>& row : values)
        count = std::max(count, static_cast<int>(row.size()));
    count = std::max(0, count);

    names_ = std::move(names);
    while (names_.size() < count)
        names_.append(QString::number(names_.size() + 1));
    if (names_.size() > count)
        names_ = names_.mid(0, count);

    values_.clear();
    values_.reserve(count);
    for (int row = 0; row < count; ++row) {
        QVector<qreal> cells(count, std::numeric_limits<qreal>::quiet_NaN());
        if (row < values.size()) {
            const QVector<qreal>& source = values.at(row);
            const qsizetype length = std::min<qsizetype>(source.size(), count);
            for (qsizetype column = 0; column < length; ++column) {
                const qreal cell = source.at(column);
                if (std::isfinite(cell))
                    cells[column] = cell;
            }
        }
        values_.append(std::move(cells));
    }

    nodeCount_ = count;
    hoveredNode_ = -1;
    pressedNode_ = -1;
    if (selectedNode_ >= nodeCount_)
        selectedNode_ = -1;

    syncPresenceSeries();
    requestUpdate();
}

int ChordDiagram::nodeCount() const noexcept
{
    return nodeCount_;
}

QStringList ChordDiagram::names() const
{
    return names_;
}

const QVector<QVector<qreal>>& ChordDiagram::matrix() const noexcept
{
    return values_;
}

qreal ChordDiagram::value(int source, int target) const
{
    if (source < 0 || source >= nodeCount_ || target < 0 || target >= nodeCount_)
        return std::numeric_limits<qreal>::quiet_NaN();
    if (source >= values_.size() || target >= values_.at(source).size())
        return std::numeric_limits<qreal>::quiet_NaN();

    return values_.at(source).at(target);
}

qreal ChordDiagram::strength(int first, int second) const
{
    qreal direct = value(first, second);
    qreal reverse = value(second, first);
    direct = std::isfinite(direct) && direct > 0.0 ? direct : 0.0;
    reverse = std::isfinite(reverse) && reverse > 0.0 ? reverse : 0.0;
    return std::max(direct, reverse);
}

qreal ChordDiagram::incidentTotal(int node) const
{
    qreal total = 0.0;
    for (int other = 0; other < nodeCount_; ++other) {
        if (other != node)
            total += strength(node, other);
    }
    return total;
}

bool ChordDiagram::isSymmetric(qreal tolerance) const
{
    if (nodeCount_ <= 0)
        return true;
    if (!std::isfinite(tolerance) || tolerance < 0.0)
        tolerance = 0.0;

    for (int row = 0; row < nodeCount_; ++row) {
        for (int column = 0; column < nodeCount_; ++column) {
            const qreal first = value(row, column);
            const qreal second = value(column, row);
            const bool firstFinite = std::isfinite(first);
            const bool secondFinite = std::isfinite(second);

            if (firstFinite != secondFinite)
                return false;
            if (!firstFinite)
                continue;

            const qreal scale = std::max({qreal(1.0), std::abs(first), std::abs(second)});
            if (std::abs(first - second) > tolerance * scale)
                return false;
        }
    }

    return true;
}

bool ChordDiagram::hasRibbons() const noexcept
{
    for (int first = 0; first < nodeCount_; ++first) {
        for (int second = first + 1; second < nodeCount_; ++second) {
            if (strength(first, second) > 0.0)
                return true;
        }
    }
    return false;
}

int ChordDiagram::ribbonCount() const
{
    int count = 0;
    for (int first = 0; first < nodeCount_; ++first) {
        for (int second = first + 1; second < nodeCount_; ++second) {
            if (strength(first, second) > 0.0)
                ++count;
        }
    }
    return count;
}

QColor ChordDiagram::nodeColour(int node) const
{
    if (node < 0 || node >= nodeCount_)
        return QColor();

    const QVector<QColor> palette = chartPalette();
    if (palette.isEmpty())
        return Theme::forWidget(this).colours().primary;

    return palette.at(node % palette.size());
}

QPointF ChordDiagram::pointAt(const Layout& layout, qreal angle, qreal radius)
{
    return QPointF(layout.centre.x() + std::sin(angle) * radius,
                   layout.centre.y() - std::cos(angle) * radius);
}

QPolygonF ChordDiagram::arcPoints(const Layout& layout, qreal start, qreal span, int segments)
{
    QPolygonF points;
    const int count = std::max(2, segments);
    points.reserve(count + 1);
    for (int index = 0; index <= count; ++index) {
        const qreal fraction = static_cast<qreal>(index) / static_cast<qreal>(count);
        points.append(pointAt(layout, start + span * fraction, layout.radius));
    }
    return points;
}

QPainterPath ChordDiagram::ribbonPath(const Layout& layout, const RibbonSlot& ribbon,
                                      qreal targetFraction)
{
    const QPolygonF source = arcPoints(layout, ribbon.sourceStart, ribbon.sourceSpan, 28);
    const QPolygonF target = arcPoints(layout, ribbon.targetStart,
                                       ribbon.targetSpan * std::clamp(targetFraction, 0.0, 1.0),
                                       28);
    if (source.size() < 2 || target.size() < 2)
        return QPainterPath();

    const auto control = [&layout](const QPointF& point) {
        return layout.centre + (point - layout.centre) * 0.35;
    };

    QPainterPath path;
    path.moveTo(source.first());
    for (qsizetype index = 1; index < source.size(); ++index)
        path.lineTo(source.at(index));

    const QPointF sourceEnd = source.last();
    const QPointF targetEnd = target.last();
    path.cubicTo(control(sourceEnd), control(targetEnd), targetEnd);

    for (qsizetype index = target.size() - 2; index >= 0; --index)
        path.lineTo(target.at(index));

    path.cubicTo(control(target.first()), control(source.first()), source.first());
    path.closeSubpath();
    return path;
}

ChordDiagram::Layout ChordDiagram::buildLayout(const QRectF& plot) const
{
    Layout layout;
    if (plot.isEmpty() || nodeCount_ < 2)
        return layout;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFontMetricsF metrics(Font::resolve(this, theme.type().labelSmall, density));

    const qreal ring = density.dp(kRingDp);
    const qreal labelGap = density.dp(kLabelGapDp);
    const qreal outerMargin = ring * 0.5 + labelGap + metrics.height() * 1.35;
    const qreal radius = std::min(plot.width(), plot.height()) / 2.0 - outerMargin;

    if (!(radius > density.dp(kMinimumRadiusDp)))
        return layout;

    layout.valid = true;
    layout.centre = plot.center();
    layout.radius = radius;
    layout.ring = ring;
    layout.labelGap = labelGap;
    layout.labelHeight = metrics.height();
    layout.gap = kNodeGapDegrees * kRadiansPerDegree;

    QVector<qreal> totals(nodeCount_, 0.0);

    for (int first = 0; first < nodeCount_; ++first) {
        for (int second = first + 1; second < nodeCount_; ++second) {
            const qreal weight = strength(first, second);
            if (!(weight > 0.0))
                continue;

            RibbonSlot ribbon;
            ribbon.source = first;
            ribbon.target = second;
            ribbon.value = weight;
            layout.ribbons.append(ribbon);
            totals[first] += weight;
            totals[second] += weight;
        }
    }

    layout.nodes.resize(nodeCount_);
    for (int node = 0; node < nodeCount_; ++node)
        layout.nodes[node].total = totals.at(node);

    if (layout.ribbons.isEmpty()) {
        // No chords: an evenly divided ring keeps node labels readable. The
        // base paints the empty state in this case; the geometry is still
        // coherent for nodeAt()/nodeCenter() before that decision is made.
        const qreal gap = kNodeGapDegrees * kRadiansPerDegree;
        const qreal slot = (kTwoPi - gap * nodeCount_) / nodeCount_;
        qreal cursor = 0.0;
        for (int node = 0; node < nodeCount_; ++node) {
            layout.nodes[node].start = cursor + gap / 2.0;
            layout.nodes[node].span = std::max<qreal>(0.0, slot);
            cursor += layout.nodes[node].span + gap;
        }
        return layout;
    }

    int isolated = 0;
    for (qreal total : totals) {
        if (!(total > 0.0))
            ++isolated;
    }

    const qreal isolatedArc = isolated > 0 ? kIsolatedArcDegrees * kRadiansPerDegree : 0.0;
    qreal gap = kNodeGapDegrees * kRadiansPerDegree;
    qreal available = kTwoPi - gap * nodeCount_ - isolatedArc * isolated;

    if (available <= 0.2) {
        gap = kTwoPi / (nodeCount_ * 8.0);
        available = kTwoPi - gap * nodeCount_ - isolatedArc * isolated;
        if (available <= 0.05)
            return Layout();
    }

    layout.gap = gap;
    qreal totalWeight = 0.0;
    for (qreal total : totals)
        totalWeight += total;

    qreal cursor = 0.0;
    for (int node = 0; node < nodeCount_; ++node) {
        const qreal total = totals.at(node);
        layout.nodes[node].start = cursor + gap / 2.0;
        layout.nodes[node].span = total > 0.0
            ? available * total / totalWeight
            : isolatedArc;
        cursor += layout.nodes[node].span + gap;
    }

    // Distribute each node's arc over its incident ribbons, in partner order so
    // the drawing is deterministic. A pair's two allocations cover exactly the
    // same global width because both use the same availableArc/totalWeight
    // factor (see the file comment).
    for (int node = 0; node < nodeCount_; ++node) {
        QVector<int> incident;
        for (qsizetype index = 0; index < layout.ribbons.size(); ++index) {
            const RibbonSlot& ribbon = layout.ribbons.at(index);
            if (ribbon.source == node || ribbon.target == node)
                incident.append(static_cast<int>(index));
        }

        std::sort(incident.begin(), incident.end(), [&](int left, int right) {
            const RibbonSlot& a = layout.ribbons.at(left);
            const RibbonSlot& b = layout.ribbons.at(right);
            const int partnerA = a.source == node ? a.target : a.source;
            const int partnerB = b.source == node ? b.target : b.source;
            return partnerA < partnerB;
        });

        const qreal total = layout.nodes.at(node).total;
        qreal arcCursor = layout.nodes.at(node).start;
        for (int index : incident) {
            RibbonSlot& ribbon = layout.ribbons[index];
            const qreal share = total > 0.0 ? ribbon.value / total : 0.0;
            const qreal span = layout.nodes.at(node).span * share;

            if (ribbon.source == node) {
                ribbon.sourceStart = arcCursor;
                ribbon.sourceSpan = span;
            } else {
                ribbon.targetStart = arcCursor;
                ribbon.targetSpan = span;
            }
            arcCursor += span;
        }
    }

    return layout;
}

qreal ChordDiagram::ribbonWidth(int source, int target) const
{
    if (source == target)
        return 0.0;

    const Layout layout = buildLayout(plotRect());
    if (!layout.valid)
        return 0.0;

    for (const RibbonSlot& ribbon : layout.ribbons) {
        if ((ribbon.source == source && ribbon.target == target)
            || (ribbon.source == target && ribbon.target == source)) {
            return layout.radius * ribbon.sourceSpan;
        }
    }
    return 0.0;
}

qreal ChordDiagram::nodeRadius() const
{
    const Layout layout = buildLayout(plotRect());
    return layout.valid ? layout.radius : 0.0;
}

QPointF ChordDiagram::nodeCenter(int node) const
{
    const Layout layout = buildLayout(plotRect());
    if (!layout.valid || node < 0 || node >= layout.nodes.size())
        return QPointF();

    const NodeSlot& slot = layout.nodes.at(node);
    return pointAt(layout, slot.start + slot.span / 2.0, layout.radius);
}

int ChordDiagram::nodeAt(QPointF position) const
{
    const Layout layout = buildLayout(plotRect());
    if (!layout.valid || layout.radius <= 0.0)
        return -1;

    if (!std::isfinite(position.x()) || !std::isfinite(position.y()))
        return -1;

    const qreal dx = position.x() - layout.centre.x();
    const qreal dy = position.y() - layout.centre.y();
    const qreal distance = std::hypot(dx, dy);
    const qreal inner = std::max<qreal>(0.0, layout.radius - layout.ring * 1.4);
    const qreal outer = layout.radius + layout.ring * 1.4 + layout.labelHeight * 1.4;
    if (distance < inner || distance > outer)
        return -1;

    const qreal angle = normaliseAngle(std::atan2(dx, -dy));
    for (int node = 0; node < layout.nodes.size(); ++node) {
        const NodeSlot& slot = layout.nodes.at(node);
        if (normaliseAngle(angle - slot.start) <= slot.span)
            return node;
    }

    return -1;
}

int ChordDiagram::hoveredNode() const noexcept
{
    return hoveredNode_;
}

int ChordDiagram::selectedNode() const noexcept
{
    return selectedNode_;
}

void ChordDiagram::setSelectedNode(int node)
{
    const int next = node >= 0 && node < nodeCount_ ? node : -1;
    if (selectedNode_ == next)
        return;

    selectedNode_ = next;
    requestUpdate();
}

void ChordDiagram::setUnit(const QString& unit)
{
    if (unit_ == unit)
        return;

    unit_ = unit;
    requestUpdate();
}

QString ChordDiagram::unit() const
{
    return unit_;
}

qreal ChordDiagram::revealProgress() const noexcept
{
    return animation().progress();
}

bool ChordDiagram::isRevealing() const noexcept
{
    return animation().isRunning();
}

void ChordDiagram::syncPresenceSeries()
{
    QVector<ChartSeries> presence;

    if (hasRibbons()) {
        ChartSeries marker;
        marker.points.append(QPointF(0.0, 0.0));
        presence.append(marker);
    }

    ChartBase::setSeries(std::move(presence));
}

void ChordDiagram::setHoveredNode(int node)
{
    if (hoveredNode_ == node)
        return;

    hoveredNode_ = node;
    requestUpdate();
}

void ChordDiagram::drawNodeLabel(QPainter& painter, const Layout& layout, int node,
                                 bool emphasised)
{
    const QString name = names_.value(node);
    if (name.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QFont font = Font::resolve(this, theme.type().labelSmall, density);
    const QFontMetricsF metrics(font);

    const qreal labelRadius = layout.radius + layout.ring * 0.5 + layout.labelGap;
    const NodeSlot& slot = layout.nodes.at(node);
    const qreal availableWidth = slot.span * labelRadius * 0.9;
    if (!(availableWidth > metrics.horizontalAdvance(QStringLiteral(".."))))
        return;

    const QString text = metrics.elidedText(name, Qt::ElideRight, availableWidth);
    if (text.isEmpty())
        return;

    const qreal totalWidth = metrics.horizontalAdvance(text);
    if (!(totalWidth > 0.0))
        return;

    const qreal totalAngle = totalWidth / labelRadius;
    const qreal middle = slot.start + slot.span / 2.0;
    const bool flip = middle > kPi * 0.5 && middle < kPi * 1.5;
    qreal cursor = flip ? middle + totalAngle / 2.0 : middle - totalAngle / 2.0;

    painter.setFont(font);
    painter.setPen(emphasised ? theme.colours().onSurface
                              : theme.colours().onSurfaceVariant);

    for (const QChar character : text) {
        const qreal advance = metrics.horizontalAdvance(character);
        if (!(advance > 0.0))
            continue;

        const qreal characterAngle = advance / labelRadius;
        const qreal centreAngle = flip ? cursor - characterAngle / 2.0
                                       : cursor + characterAngle / 2.0;
        const QPointF point = pointAt(layout, centreAngle, labelRadius);
        qreal rotation = centreAngle * kDegreesPerRadian;
        if (flip)
            rotation += 180.0;

        painter.save();
        painter.translate(point);
        painter.rotate(rotation);
        const QRectF glyphRect(-advance / 2.0, -metrics.height() / 2.0,
                               advance, metrics.height());
        painter.drawText(glyphRect, Qt::AlignCenter, QString(character));
        painter.restore();

        cursor += flip ? -characterAngle : characterAngle;
    }
}

void ChordDiagram::paintPlot(QPainter& painter, const QRectF& plot)
{
    paintPlot(painter, plot, 1.0);
}

void ChordDiagram::paintPlot(QPainter& painter, const QRectF& plot, qreal progress)
{
    if (plot.isEmpty() || nodeCount_ < 2)
        return;

    const PainterStateGuard guard(painter);

    try {
        const qreal stage = std::clamp(progress, 0.0, 1.0);
        const Theme& theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const ColorScheme& colours = theme.colours();
        const Layout layout = buildLayout(plot);
        if (!layout.valid)
            return;

        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setBrush(Qt::NoBrush);

        // 1. Guide rings: a quiet radial grid under the chords.
        QColor guide = colours.outlineVariant;
        guide.setAlphaF(static_cast<float>(kGuideAlpha));
        QPen guidePen(guide, std::max<qreal>(1.0, density.dp(0.75)), Qt::DashLine);
        painter.setPen(guidePen);
        painter.drawEllipse(layout.centre, layout.radius, layout.radius);
        painter.drawEllipse(layout.centre, layout.radius * 0.66, layout.radius * 0.66);
        painter.drawEllipse(layout.centre, layout.radius * 0.33, layout.radius * 0.33);

        // 2. Ribbons, sweeping from the source arc to the target arc.
        const qreal targetFraction = stage;
        if (targetFraction > 0.005) {
            for (const RibbonSlot& ribbon : layout.ribbons) {
                const QPainterPath path = ribbonPath(layout, ribbon, targetFraction);
                if (path.isEmpty())
                    continue;

                QColor colour = nodeColour(ribbon.source);
                if (!colour.isValid())
                    colour = colours.primary;

                const bool touchesHover = hoveredNode_ >= 0
                    && (ribbon.source == hoveredNode_ || ribbon.target == hoveredNode_);
                const bool touchesSelection = selectedNode_ >= 0
                    && (ribbon.source == selectedNode_ || ribbon.target == selectedNode_);

                qreal alpha = kRibbonAlpha;
                if (hoveredNode_ >= 0)
                    alpha = touchesHover ? kHighlightedRibbonAlpha : kDimmedRibbonAlpha;
                else if (touchesSelection)
                    alpha = kHighlightedRibbonAlpha * 0.85;

                colour.setAlphaF(static_cast<float>(alpha));
                painter.setBrush(colour);

                if (touchesHover || touchesSelection) {
                    QColor outline = colour;
                    outline.setAlphaF(0.95f);
                    painter.setPen(QPen(outline, 1.0));
                } else {
                    painter.setPen(Qt::NoPen);
                }

                painter.drawPath(path);
            }
        }

        // 3. Node arcs. The ring is the node's identity; it is always fully
        //    painted, while the ribbons carry the reveal.
        for (int node = 0; node < layout.nodes.size(); ++node) {
            QColor colour = nodeColour(node);
            if (!colour.isValid())
                colour = colours.primary;

            qreal width = layout.ring;
            if (node == selectedNode_)
                width *= 1.25;
            else if (node == hoveredNode_)
                width *= 1.15;

            QPen pen(colour, width);
            pen.setCapStyle(Qt::FlatCap);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPolyline(arcPoints(layout, layout.nodes.at(node).start,
                                           layout.nodes.at(node).span, 32));
        }

        // 4. Curved node names.
        for (int node = 0; node < layout.nodes.size(); ++node) {
            drawNodeLabel(painter, layout, node,
                          node == hoveredNode_ || node == selectedNode_);
        }

        // 5. Caption: the focused node and its incident weight, otherwise the
        //    diagram's size. This is the diagram's unit-bearing readout.
        const int focus = hoveredNode_ >= 0 ? hoveredNode_ : selectedNode_;
        QString caption;
        if (focus >= 0) {
            const QString total = formatValue(incidentTotal(focus));
            caption = unit_.isEmpty()
                ? QStringLiteral("%1  ·  weight %2").arg(names_.value(focus), total)
                : QStringLiteral("%1  ·  weight %2 %3").arg(names_.value(focus), total, unit_);
        } else {
            caption = QStringLiteral("%1 nodes  ·  %2 ribbons")
                          .arg(nodeCount_)
                          .arg(ribbonCount());
        }

        const QFont captionFont = Font::resolve(this, theme.type().labelSmall, density);
        const QFontMetricsF captionMetrics(captionFont);
        painter.setFont(captionFont);
        painter.setPen(focus >= 0 ? colours.onSurface : colours.onSurfaceVariant);
        painter.drawText(QRectF(plot.left(), plot.bottom() - captionMetrics.height(),
                                plot.width(), captionMetrics.height()),
                         Qt::AlignLeft | Qt::AlignBottom,
                         captionMetrics.elidedText(caption, Qt::ElideRight, plot.width()));
    } catch (const std::exception& error) {
        qWarning("md::ChordDiagram::paintPlot failed: %s", error.what());
    } catch (...) {
        qWarning("md::ChordDiagram::paintPlot failed with an unknown exception");
    }
}

void ChordDiagram::mousePressEvent(QMouseEvent* event)
{
    try {
        View::mousePressEvent(event);
        pressedNode_ = event->button() == Qt::LeftButton
            ? nodeAt(event->position())
            : -1;
    } catch (const std::exception& error) {
        qWarning("md::ChordDiagram::mousePressEvent failed: %s", error.what());
        pressedNode_ = -1;
    } catch (...) {
        qWarning("md::ChordDiagram::mousePressEvent failed with an unknown exception");
        pressedNode_ = -1;
    }
}

void ChordDiagram::mouseReleaseEvent(QMouseEvent* event)
{
    try {
        View::mouseReleaseEvent(event);
        if (event->button() != Qt::LeftButton) {
            pressedNode_ = -1;
            return;
        }

        const int hit = nodeAt(event->position());
        if (hit >= 0 && hit == pressedNode_) {
            setSelectedNode(hit);
            emit nodeActivated(hit);
        } else if (hit < 0) {
            setSelectedNode(-1);
        }

        pressedNode_ = -1;
    } catch (const std::exception& error) {
        qWarning("md::ChordDiagram::mouseReleaseEvent failed: %s", error.what());
        pressedNode_ = -1;
    } catch (...) {
        qWarning("md::ChordDiagram::mouseReleaseEvent failed with an unknown exception");
        pressedNode_ = -1;
    }
}

void ChordDiagram::mouseMoveEvent(QMouseEvent* event)
{
    try {
        View::mouseMoveEvent(event);
        setHoveredNode(nodeAt(event->position()));
    } catch (const std::exception& error) {
        qWarning("md::ChordDiagram::mouseMoveEvent failed: %s", error.what());
        setHoveredNode(-1);
    } catch (...) {
        qWarning("md::ChordDiagram::mouseMoveEvent failed with an unknown exception");
        setHoveredNode(-1);
    }
}

void ChordDiagram::leaveEvent(QEvent* event)
{
    try {
        View::leaveEvent(event);
        setHoveredNode(-1);
    } catch (const std::exception& error) {
        qWarning("md::ChordDiagram::leaveEvent failed: %s", error.what());
        setHoveredNode(-1);
    } catch (...) {
        qWarning("md::ChordDiagram::leaveEvent failed with an unknown exception");
        setHoveredNode(-1);
    }
}

} // namespace md
