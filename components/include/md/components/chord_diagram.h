// Periodicity — the ChordDiagram component.
//
// A chord diagram over a square adjacency/connectivity matrix. Every node owns
// an arc on one circle; the arc length is proportional to the node's total
// incident weight, so a node with more traffic reads as a larger arc. Each
// positive pair (i, j) becomes one ribbon whose thickness is proportional to the
// pair's weight: the pair is drawn once, and an asymmetric matrix uses
// max(value(i,j), value(j,i)) as the undirected strength. The diagonal is not a
// ribbon, because a self-link has no two arcs to bridge.
//
// Because the node arcs are proportional to their totals, every ribbon's width
// at the rim is R * availableArc * value / totalWeight. That single factor is
// what ribbonWidth() reports and what the geometry tests compare, so a pair
// with three times the weight paints a ribbon three times as wide.
//
// Layout. The circle is inscribed in plotRect() with room for the node arcs and
// their labels. nodeCenter(), nodeRadius() and nodeAt() expose the settled
// geometry before or after a paint. Node names are drawn on curved baselines
// outside the arcs, flipped on the left half so they stay upright.
//
// Interaction. Hovering a node's arc (or the ring beside it) highlights that
// node's ribbons and dims the rest; clicking an arc selects the node and emits
// nodeActivated(int). The base View state layer is switched off because it would
// tint the ribbons and destroy the node-colour reading.
//
// Reveal. Ribbons sweep out from their source arc to their target arc over the
// theme's Level2 motion; the node arcs and labels remain as the frame. A chart
// that has never been shown paints finished, and reduced motion (or a
// zero-duration spec) snaps synchronously.
//
// Data gate. ChartBase selects paintPlot()/paintEmptyState() from hasData(),
// which reads ChartBase::series(); a matrix is not a series. The component keeps
// one unnamed presence point in sync while at least one positive ribbon exists.
// A matrix with no positive off-diagonal entry therefore paints the shared empty
// state instead of a ring of empty nodes.

#pragma once

#include <md/components/chart_common.h>
#include <md/view/view.h>

#include <QColor>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

class QEvent;
class QMouseEvent;
class QPainter;

namespace md {

class ChordDiagram : public ChartBase
{
    Q_OBJECT

public:
    explicit ChordDiagram(QWidget* parent = nullptr);

    // Replaces the matrix. The node count is the larger of the name count and
    // the row count; names and rows are padded so the stored matrix is square.
    // Missing cells are NaN and contribute nothing. A negative or non-finite
    // weight is treated as no ribbon.
    void setMatrix(QStringList names, QVector<QVector<qreal>> values);

    [[nodiscard]] int nodeCount() const noexcept;
    [[nodiscard]] QStringList names() const;
    [[nodiscard]] const QVector<QVector<qreal>>& matrix() const noexcept;

    // One matrix cell, or NaN outside the matrix.
    [[nodiscard]] qreal value(int source, int target) const;

    // The live diagonal is symmetric within `tolerance`. The diagram itself is
    // always undirected; this accessor simply reports the input's shape.
    [[nodiscard]] bool isSymmetric(qreal tolerance = 1e-9) const;

    // The number of painted ribbons, one per positive unordered pair.
    [[nodiscard]] int ribbonCount() const;

    // The settled ribbon thickness at the node rim, in ChartBase-local pixels,
    // for the pair {source, target}. Zero when the pair has no positive weight
    // or the diagram has no room to paint.
    [[nodiscard]] qreal ribbonWidth(int source, int target) const;

    // The radius of the node circle in ChartBase-local pixels.
    [[nodiscard]] qreal nodeRadius() const;

    // The point on the node circle at the middle of `node`'s arc. An invalid
    // point for an out-of-range node or a chart with no room to paint.
    [[nodiscard]] QPointF nodeCenter(int node) const;

    // The node whose arc is under a ChartBase-local pixel, or -1. The test
    // accepts the arc ring and a small band on either side of it, which makes
    // the labels hoverable too.
    [[nodiscard]] int nodeAt(QPointF position) const;

    [[nodiscard]] int hoveredNode() const noexcept;
    [[nodiscard]] int selectedNode() const noexcept;
    void setSelectedNode(int node);

    // The unit appended to the hover/selection weight caption. Empty by default.
    void setUnit(const QString& unit);
    [[nodiscard]] QString unit() const;

    [[nodiscard]] qreal revealProgress() const noexcept;
    [[nodiscard]] bool isRevealing() const noexcept;

signals:
    // Emitted on a release that lands on the same node the press started on.
    // The node is selected first, so a handler may read selectedNode().
    void nodeActivated(int node);

protected:
    void paintPlot(QPainter& painter, const QRectF& plot) override;
    void paintPlot(QPainter& painter, const QRectF& plot, qreal progress) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct NodeSlot
    {
        qreal start = 0.0; // radians clockwise from twelve o'clock
        qreal span = 0.0;
        qreal total = 0.0;
    };

    struct RibbonSlot
    {
        int source = -1; // the lower node index, so the pair has one owner
        int target = -1;
        qreal value = 0.0;
        qreal sourceStart = 0.0;
        qreal sourceSpan = 0.0;
        qreal targetStart = 0.0;
        qreal targetSpan = 0.0;
    };

    struct Layout
    {
        bool valid = false;
        QPointF centre;
        qreal radius = 0.0;
        qreal ring = 0.0;
        qreal labelGap = 0.0;
        qreal labelHeight = 0.0;
        qreal gap = 0.0;
        QVector<NodeSlot> nodes;
        QVector<RibbonSlot> ribbons;
    };

    [[nodiscard]] Layout buildLayout(const QRectF& plot) const;
    [[nodiscard]] qreal strength(int first, int second) const;
    [[nodiscard]] qreal incidentTotal(int node) const;

    [[nodiscard]] QColor nodeColour(int node) const;

    [[nodiscard]] static QPointF pointAt(const Layout& layout, qreal angle, qreal radius);
    [[nodiscard]] static QPolygonF arcPoints(const Layout& layout, qreal start, qreal span,
                                             int segments);
    [[nodiscard]] static QPainterPath ribbonPath(const Layout& layout,
                                                 const RibbonSlot& ribbon,
                                                 qreal targetFraction);

    // Draws one name along the arc outside `node`, elided to the available arc
    // length and flipped on the left half so it stays upright.
    void drawNodeLabel(QPainter& painter, const Layout& layout, int node, bool emphasised);

    [[nodiscard]] bool hasRibbons() const noexcept;
    void syncPresenceSeries();

    void setHoveredNode(int node);

    QStringList names_;
    QVector<QVector<qreal>> values_;
    int nodeCount_ = 0;

    QString unit_;

    int hoveredNode_ = -1;
    int selectedNode_ = -1;
    int pressedNode_ = -1;
};

} // namespace md
