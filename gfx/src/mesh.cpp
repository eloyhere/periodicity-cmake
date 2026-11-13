// Periodicity — gfx layer: mesh validation and the shape builders.
//
// Validation is shared by the two mesh types through the helpers at the top of
// this file. The builders compute their primitive count before allocating and
// return a default mesh when the request would exceed a cap, so failure can
// never leave a partially filled vertex or index list behind.

#include <md/gfx/mesh.h>

#include <QDebug>
#include <QSet>

#include <cmath>

namespace md::gfx {

namespace {

bool isFinite(qreal value)
{
    return std::isfinite(static_cast<double>(value));
}

bool isFinite(const QPointF& point)
{
    return isFinite(point.x()) && isFinite(point.y());
}

bool isFinite(const QVector3D& vector)
{
    return isFinite(vector.x()) && isFinite(vector.y()) && isFinite(vector.z());
}

bool isFinite(const QTransform& transform)
{
    return isFinite(transform.m11()) && isFinite(transform.m12())
        && isFinite(transform.m21()) && isFinite(transform.m22())
        && isFinite(transform.dx()) && isFinite(transform.dy());
}

bool isFinite(const QMatrix4x4& matrix)
{
    const float* data = matrix.constData();
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(static_cast<double>(data[i])))
            return false;
    }
    return true;
}

// Index list and vertex list agree on a legal primitive count.
bool primitiveCountIsValid(PrimitiveTopology topology, qsizetype indexCount,
                           qsizetype vertexCount)
{
    if (vertexCount > static_cast<qsizetype>(kMaxVertices))
        return false;

    switch (topology) {
    case PrimitiveTopology::Triangles:
        return indexCount % 3 == 0
            && indexCount / 3 <= static_cast<qsizetype>(kMaxTriangles);
    case PrimitiveTopology::Lines:
        return indexCount % 2 == 0
            && indexCount / 2 <= static_cast<qsizetype>(kMaxTriangles);
    case PrimitiveTopology::Points:
        return indexCount <= static_cast<qsizetype>(kMaxTriangles);
    }
    return false;
}

bool hasFinitePosition(const Vertex2D& vertex)
{
    return isFinite(vertex.position) && isFinite(vertex.uv);
}

bool hasFinitePosition(const Vertex3D& vertex)
{
    return isFinite(vertex.position) && isFinite(vertex.normal) && isFinite(vertex.uv);
}

} // namespace

// ---------------------------------------------------------------------------
// Vertices

bool Vertex2D::isFinite() const
{
    return hasFinitePosition(*this);
}

bool Vertex3D::isFinite() const
{
    return hasFinitePosition(*this);
}

// ---------------------------------------------------------------------------
// Mesh2D

Mesh2D::Mesh2D(QVector<Vertex2D> vertices, QVector<quint32> indices,
               PrimitiveTopology topology)
    : vertices_(std::move(vertices))
    , indices_(std::move(indices))
    , topology_(topology)
{
}

bool Mesh2D::isValid() const
{
    if (!primitiveCountIsValid(topology_, indices_.size(), vertices_.size()))
        return false;

    for (const Vertex2D& vertex : vertices_) {
        if (!hasFinitePosition(vertex))
            return false;
    }

    const quint32 vertexCount = static_cast<quint32>(vertices_.size());
    for (quint32 index : indices_) {
        if (index >= vertexCount)
            return false;
    }

    return true;
}

QRectF Mesh2D::bounds() const
{
    if (vertices_.isEmpty())
        return QRectF();

    qreal left = vertices_.front().position.x();
    qreal right = left;
    qreal top = vertices_.front().position.y();
    qreal bottom = top;

    for (const Vertex2D& vertex : vertices_) {
        left = qMin(left, vertex.position.x());
        right = qMax(right, vertex.position.x());
        top = qMin(top, vertex.position.y());
        bottom = qMax(bottom, vertex.position.y());
    }

    return QRectF(left, top, right - left, bottom - top);
}

Mesh2D Mesh2D::transformed(const QTransform& transform) const
{
    // A non-finite transform would turn every position into NaN and the result
    // would fail validation later; refuse it here and keep the source intact.
    if (!isFinite(transform)) {
        qWarning() << "gfx::Mesh2D::transformed: rejected a non-finite transform";
        return *this;
    }

    Mesh2D result = *this;
    for (Vertex2D& vertex : result.vertices_)
        vertex.position = transform.map(vertex.position);
    return result;
}

Mesh2D Mesh2D::rect(const QRectF& rectangle, const QColor& colour)
{
    if (!isFinite(rectangle.x()) || !isFinite(rectangle.y())
        || !isFinite(rectangle.width()) || !isFinite(rectangle.height())
        || rectangle.width() <= 0.0 || rectangle.height() <= 0.0) {
        qWarning() << "gfx::Mesh2D::rect: rejected non-finite or empty rectangle"
                   << rectangle;
        return Mesh2D();
    }

    const qreal left = rectangle.x();
    const qreal top = rectangle.y();
    const qreal right = left + rectangle.width();
    const qreal bottom = top + rectangle.height();

    QVector<Vertex2D> vertices;
    vertices.reserve(4);
    vertices.append({QPointF(left, top), colour, QPointF(0.0, 0.0)});
    vertices.append({QPointF(right, top), colour, QPointF(1.0, 0.0)});
    vertices.append({QPointF(right, bottom), colour, QPointF(1.0, 1.0)});
    vertices.append({QPointF(left, bottom), colour, QPointF(0.0, 1.0)});

    return Mesh2D(std::move(vertices), {0, 1, 2, 0, 2, 3});
}

Mesh2D Mesh2D::grid(const QRectF& rectangle, int columns, int rows,
                    const QColor& colour)
{
    if (columns <= 0 || rows <= 0 || !isFinite(rectangle.x())
        || !isFinite(rectangle.y()) || !isFinite(rectangle.width())
        || !isFinite(rectangle.height()) || rectangle.width() <= 0.0
        || rectangle.height() <= 0.0) {
        qWarning() << "gfx::Mesh2D::grid: rejected grid" << columns << "x" << rows
                   << "over" << rectangle;
        return Mesh2D();
    }

    // Check the caps on 64-bit counts before touching the allocator: a huge
    // column count must not first try to reserve a huge vertex array.
    const qint64 cells = static_cast<qint64>(columns) * static_cast<qint64>(rows);
    if (cells * 2 > static_cast<qint64>(kMaxTriangles)
        || (static_cast<qint64>(columns) + 1) * (static_cast<qint64>(rows) + 1)
            > static_cast<qint64>(kMaxVertices)) {
        qWarning() << "gfx::Mesh2D::grid: rejected" << columns << "x" << rows
                   << "cells (caps:" << kMaxTriangles << "triangles,"
                   << kMaxVertices << "vertices)";
        return Mesh2D();
    }

    const int stride = columns + 1;
    QVector<Vertex2D> vertices;
    vertices.reserve(stride * (rows + 1));

    for (int row = 0; row <= rows; ++row) {
        const qreal fy = static_cast<qreal>(row) / static_cast<qreal>(rows);
        for (int column = 0; column <= columns; ++column) {
            const qreal fx = static_cast<qreal>(column) / static_cast<qreal>(columns);
            vertices.append({QPointF(rectangle.x() + rectangle.width() * fx,
                                     rectangle.y() + rectangle.height() * fy),
                             colour, QPointF(fx, fy)});
        }
    }

    QVector<quint32> indices;
    indices.reserve(static_cast<qsizetype>(cells) * 6);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const quint32 topLeft = static_cast<quint32>(row * stride + column);
            const quint32 topRight = topLeft + 1;
            const quint32 bottomLeft = topLeft + static_cast<quint32>(stride);
            const quint32 bottomRight = bottomLeft + 1;

            indices.append(topLeft);
            indices.append(topRight);
            indices.append(bottomRight);
            indices.append(topLeft);
            indices.append(bottomRight);
            indices.append(bottomLeft);
        }
    }

    return Mesh2D(std::move(vertices), std::move(indices));
}

// ---------------------------------------------------------------------------
// Mesh3D

Mesh3D::Mesh3D(QVector<Vertex3D> vertices, QVector<quint32> indices,
               PrimitiveTopology topology)
    : vertices_(std::move(vertices))
    , indices_(std::move(indices))
    , topology_(topology)
{
}

bool Mesh3D::isValid() const
{
    if (!primitiveCountIsValid(topology_, indices_.size(), vertices_.size()))
        return false;

    for (const Vertex3D& vertex : vertices_) {
        if (!hasFinitePosition(vertex))
            return false;
    }

    const quint32 vertexCount = static_cast<quint32>(vertices_.size());
    for (quint32 index : indices_) {
        if (index >= vertexCount)
            return false;
    }

    return true;
}

Bounds3D Mesh3D::bounds() const
{
    if (vertices_.isEmpty())
        return Bounds3D();

    Bounds3D bounds;
    bounds.minimum = vertices_.front().position;
    bounds.maximum = vertices_.front().position;

    for (const Vertex3D& vertex : vertices_) {
        bounds.minimum.setX(qMin(bounds.minimum.x(), vertex.position.x()));
        bounds.minimum.setY(qMin(bounds.minimum.y(), vertex.position.y()));
        bounds.minimum.setZ(qMin(bounds.minimum.z(), vertex.position.z()));
        bounds.maximum.setX(qMax(bounds.maximum.x(), vertex.position.x()));
        bounds.maximum.setY(qMax(bounds.maximum.y(), vertex.position.y()));
        bounds.maximum.setZ(qMax(bounds.maximum.z(), vertex.position.z()));
    }

    return bounds;
}

Mesh3D Mesh3D::transformed(const QMatrix4x4& transform) const
{
    if (!isFinite(transform)) {
        qWarning() << "gfx::Mesh3D::transformed: rejected a non-finite transform";
        return *this;
    }

    Mesh3D result = *this;
    for (Vertex3D& vertex : result.vertices_) {
        vertex.position = transform.map(vertex.position);
        const QVector3D normal = transform.mapVector(vertex.normal);
        // A degenerate transform can collapse the normal; keep it finite and
        // let the fragment stage see a zero normal rather than a NaN.
        vertex.normal = normal.lengthSquared() > 0.0F ? normal.normalized() : QVector3D();
    }
    return result;
}

Mesh3D Mesh3D::box(const QVector3D& size, const QColor& colour)
{
    if (!isFinite(size) || size.x() <= 0.0F || size.y() <= 0.0F || size.z() <= 0.0F) {
        qWarning() << "gfx::Mesh3D::box: rejected non-positive or non-finite size"
                   << size;
        return Mesh3D();
    }

    const qreal hx = static_cast<qreal>(size.x()) * 0.5;
    const qreal hy = static_cast<qreal>(size.y()) * 0.5;
    const qreal hz = static_cast<qreal>(size.z()) * 0.5;

    struct Face
    {
        QVector3D normal;
        QVector3D corners[4];
    };

    const Face faces[6] = {
        {QVector3D(1.0F, 0.0F, 0.0F),
         {QVector3D(hx, -hy, -hz), QVector3D(hx, hy, -hz),
          QVector3D(hx, hy, hz), QVector3D(hx, -hy, hz)}},
        {QVector3D(-1.0F, 0.0F, 0.0F),
         {QVector3D(-hx, -hy, hz), QVector3D(-hx, hy, hz),
          QVector3D(-hx, hy, -hz), QVector3D(-hx, -hy, -hz)}},
        {QVector3D(0.0F, 1.0F, 0.0F),
         {QVector3D(-hx, hy, hz), QVector3D(hx, hy, hz),
          QVector3D(hx, hy, -hz), QVector3D(-hx, hy, -hz)}},
        {QVector3D(0.0F, -1.0F, 0.0F),
         {QVector3D(-hx, -hy, -hz), QVector3D(hx, -hy, -hz),
          QVector3D(hx, -hy, hz), QVector3D(-hx, -hy, hz)}},
        {QVector3D(0.0F, 0.0F, 1.0F),
         {QVector3D(-hx, -hy, hz), QVector3D(hx, -hy, hz),
          QVector3D(hx, hy, hz), QVector3D(-hx, hy, hz)}},
        {QVector3D(0.0F, 0.0F, -1.0F),
         {QVector3D(-hx, hy, -hz), QVector3D(hx, hy, -hz),
          QVector3D(hx, -hy, -hz), QVector3D(-hx, -hy, -hz)}},
    };

    const QPointF uvs[4] = {QPointF(0.0, 0.0), QPointF(1.0, 0.0),
                            QPointF(1.0, 1.0), QPointF(0.0, 1.0)};

    QVector<Vertex3D> vertices;
    vertices.reserve(24);
    QVector<quint32> indices;
    indices.reserve(36);

    for (const Face& face : faces) {
        const quint32 base = static_cast<quint32>(vertices.size());
        for (int corner = 0; corner < 4; ++corner)
            vertices.append({face.corners[corner], face.normal, colour, uvs[corner]});

        indices.append(base);
        indices.append(base + 1);
        indices.append(base + 2);
        indices.append(base);
        indices.append(base + 2);
        indices.append(base + 3);
    }

    return Mesh3D(std::move(vertices), std::move(indices));
}

Mesh3D Mesh3D::plane(const QSizeF& extent, int divisions, const QColor& colour)
{
    if (!isFinite(extent.width()) || !isFinite(extent.height())
        || extent.width() <= 0.0 || extent.height() <= 0.0 || divisions <= 0) {
        qWarning() << "gfx::Mesh3D::plane: rejected extent" << extent
                   << "divisions" << divisions;
        return Mesh3D();
    }

    const qint64 cells = static_cast<qint64>(divisions) * divisions;
    if (cells * 2 > static_cast<qint64>(kMaxTriangles)
        || (static_cast<qint64>(divisions) + 1)
                * (static_cast<qint64>(divisions) + 1)
            > static_cast<qint64>(kMaxVertices)) {
        qWarning() << "gfx::Mesh3D::plane: rejected divisions" << divisions
                   << "(caps:" << kMaxTriangles << "triangles," << kMaxVertices
                   << "vertices)";
        return Mesh3D();
    }

    const int stride = divisions + 1;
    QVector<Vertex3D> vertices;
    vertices.reserve(stride * stride);

    for (int row = 0; row <= divisions; ++row) {
        const qreal fz = static_cast<qreal>(row) / static_cast<qreal>(divisions);
        for (int column = 0; column <= divisions; ++column) {
            const qreal fx = static_cast<qreal>(column) / static_cast<qreal>(divisions);
            vertices.append({QVector3D(static_cast<float>(-extent.width() * 0.5
                                                         + extent.width() * fx),
                                       0.0F,
                                       static_cast<float>(-extent.height() * 0.5
                                                          + extent.height() * fz)),
                             QVector3D(0.0F, 1.0F, 0.0F), colour, QPointF(fx, fz)});
        }
    }

    QVector<quint32> indices;
    indices.reserve(static_cast<qsizetype>(cells) * 6);
    for (int row = 0; row < divisions; ++row) {
        for (int column = 0; column < divisions; ++column) {
            const quint32 topLeft = static_cast<quint32>(row * stride + column);
            const quint32 topRight = topLeft + 1;
            const quint32 bottomLeft = topLeft + static_cast<quint32>(stride);
            const quint32 bottomRight = bottomLeft + 1;

            // Counter-clockwise seen from +Y: the visible side of the ground.
            indices.append(topLeft);
            indices.append(bottomLeft);
            indices.append(bottomRight);
            indices.append(topLeft);
            indices.append(bottomRight);
            indices.append(topRight);
        }
    }

    return Mesh3D(std::move(vertices), std::move(indices));
}

Mesh3D Mesh3D::grid(const QSizeF& extent, int divisions, const QColor& colour)
{
    if (!isFinite(extent.width()) || !isFinite(extent.height())
        || extent.width() <= 0.0 || extent.height() <= 0.0 || divisions <= 0) {
        qWarning() << "gfx::Mesh3D::grid: rejected extent" << extent
                   << "divisions" << divisions;
        return Mesh3D();
    }

    const qint64 lines = (static_cast<qint64>(divisions) + 1) * 2;
    if (lines * 2 > static_cast<qint64>(kMaxVertices)
        || lines > static_cast<qint64>(kMaxTriangles)) {
        qWarning() << "gfx::Mesh3D::grid: rejected divisions" << divisions
                   << "(caps:" << kMaxTriangles << "segments," << kMaxVertices
                   << "vertices)";
        return Mesh3D();
    }

    QVector<Vertex3D> vertices;
    vertices.reserve(static_cast<qsizetype>(lines) * 2);
    QVector<quint32> indices;
    indices.reserve(static_cast<qsizetype>(lines) * 2);

    const qreal halfWidth = extent.width() * 0.5;
    const qreal halfHeight = extent.height() * 0.5;
    const QVector3D up(0.0F, 1.0F, 0.0F);

    auto addLine = [&](const QVector3D& from, const QVector3D& to) {
        const quint32 base = static_cast<quint32>(vertices.size());
        vertices.append({from, up, colour, QPointF(0.0, 0.0)});
        vertices.append({to, up, colour, QPointF(1.0, 0.0)});
        indices.append(base);
        indices.append(base + 1);
    };

    for (int i = 0; i <= divisions; ++i) {
        const qreal offset = static_cast<qreal>(i) / static_cast<qreal>(divisions);
        const qreal z = -halfHeight + extent.height() * offset;
        addLine(QVector3D(static_cast<float>(-halfWidth), 0.0F, static_cast<float>(z)),
                QVector3D(static_cast<float>(halfWidth), 0.0F, static_cast<float>(z)));

        const qreal x = -halfWidth + extent.width() * offset;
        addLine(QVector3D(static_cast<float>(x), 0.0F, static_cast<float>(-halfHeight)),
                QVector3D(static_cast<float>(x), 0.0F, static_cast<float>(halfHeight)));
    }

    return Mesh3D(std::move(vertices), std::move(indices), PrimitiveTopology::Lines);
}

} // namespace md::gfx
