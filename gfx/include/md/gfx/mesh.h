// Periodicity — gfx layer: vertex formats and mesh geometry.
//
// A mesh is pure data: vertices, an index list and a topology. It never draws.
// The two vertex formats are the ones the software rasterizer understands:
//
//   Vertex2D  position in target pixels, colour, texture coordinate
//   Vertex3D  position in model space, normal, colour, texture coordinate
//
// Validation is the point of this file. A mesh is valid only when every one of
// these holds, and a broken mesh is refused whole rather than drawn partially:
//
//   - vertex and index counts are inside kMaxVertices / kMaxTriangles;
//   - the index list matches the topology (3 per triangle, 2 per line);
//   - every index addresses an existing vertex;
//   - every position, normal and texture coordinate is finite.
//
// Every builder validates its arguments, computes the primitive count before it
// allocates, and returns a default (empty) mesh on failure. An empty mesh is a
// valid no-op: it has no primitives to draw. Builders never return a half-built
// mesh, so a caller cannot accidentally draw the tail of a failed build.
//
// transformed() returns a new mesh; the source is never modified.

#pragma once

#include <QColor>
#include <QMatrix4x4>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTransform>
#include <QVector>
#include <QVector3D>

namespace md::gfx {

// A mesh that exceeds either cap is invalid. The caps are deliberately small
// enough that validation is cheap and large enough for UI-scale geometry. The
// triangle cap is the tighter of the two for a square grid: 181 x 181 cells is
// 65 522 triangles, while the vertex cap alone would allow 255 x 255 cells.
inline constexpr int kMaxVertices = 65536;
inline constexpr int kMaxTriangles = 65536;

enum class PrimitiveTopology
{
    Triangles,
    Lines,
    Points,
};

struct Vertex2D
{
    QPointF position;
    QColor colour = QColor(Qt::white);
    QPointF uv;

    [[nodiscard]] bool isFinite() const;
};

struct Vertex3D
{
    QVector3D position;
    QVector3D normal = QVector3D(0.0F, 0.0F, 1.0F);
    QColor colour = QColor(Qt::white);
    QPointF uv;

    [[nodiscard]] bool isFinite() const;
};

// Axis-aligned bounds for 3D geometry. An empty mesh reports minimum and
// maximum at the origin (isEmpty() is then true because minimum == maximum).
struct Bounds3D
{
    QVector3D minimum;
    QVector3D maximum;

    [[nodiscard]] bool isEmpty() const { return minimum == maximum; }
};

class Mesh2D
{
public:
    Mesh2D() = default;
    Mesh2D(QVector<Vertex2D> vertices, QVector<quint32> indices,
           PrimitiveTopology topology = PrimitiveTopology::Triangles);

    const QVector<Vertex2D>& vertices() const { return vertices_; }
    const QVector<quint32>& indices() const { return indices_; }
    PrimitiveTopology topology() const { return topology_; }

    bool isValid() const;
    bool isEmpty() const { return indices_.isEmpty() && vertices_.isEmpty(); }

    // Bounding rectangle of the positions. An empty mesh returns QRectF().
    QRectF bounds() const;

    // A copy with every position mapped through `transform`. UVs, colours and
    // topology are copied unchanged; the source mesh is untouched.
    Mesh2D transformed(const QTransform& transform) const;

    // Axis-aligned rectangle, two triangles, UV (0,0)-(1,1).
    static Mesh2D rect(const QRectF& rectangle,
                       const QColor& colour = QColor(Qt::white));

    // `columns` x `rows` cells, each split into two triangles, UV spanning the
    // rectangle. Returns an empty mesh when either count is not positive or the
    // result would exceed kMaxTriangles / kMaxVertices.
    static Mesh2D grid(const QRectF& rectangle, int columns, int rows,
                       const QColor& colour = QColor(Qt::white));

private:
    QVector<Vertex2D> vertices_;
    QVector<quint32> indices_;
    PrimitiveTopology topology_ = PrimitiveTopology::Triangles;
};

class Mesh3D
{
public:
    Mesh3D() = default;
    Mesh3D(QVector<Vertex3D> vertices, QVector<quint32> indices,
           PrimitiveTopology topology = PrimitiveTopology::Triangles);

    const QVector<Vertex3D>& vertices() const { return vertices_; }
    const QVector<quint32>& indices() const { return indices_; }
    PrimitiveTopology topology() const { return topology_; }

    bool isValid() const;
    bool isEmpty() const { return indices_.isEmpty() && vertices_.isEmpty(); }

    Bounds3D bounds() const;

    // Positions and normals are transformed; normals are renormalised. The
    // expected input is affine. The source mesh is untouched.
    Mesh3D transformed(const QMatrix4x4& transform) const;

    // A closed axis-aligned box centred on the origin: 24 vertices, 36 indices,
    // four vertices per face so each face carries its own normal.
    static Mesh3D box(const QVector3D& size,
                      const QColor& colour = QColor(Qt::white));

    // A flat triangle surface on the XZ plane at y = 0, centred on the origin,
    // normal +Y, `divisions` cells per axis, UV spanning [0, 1].
    static Mesh3D plane(const QSizeF& extent, int divisions,
                        const QColor& colour = QColor(Qt::white));

    // A line lattice on the XZ plane at y = 0, centred on the origin, with
    // `divisions` cells per axis (so divisions + 1 lines each way).
    static Mesh3D grid(const QSizeF& extent, int divisions,
                       const QColor& colour = QColor(Qt::white));

private:
    QVector<Vertex3D> vertices_;
    QVector<quint32> indices_;
    PrimitiveTopology topology_ = PrimitiveTopology::Triangles;
};

} // namespace md::gfx
