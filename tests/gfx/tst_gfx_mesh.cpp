// gfx layer tests: geometry validation, bounds and transforms.
//
// The contract says invalid geometry is refused whole, not clamped or partially
// drawn. These tests pin that down at the mesh boundary: an out-of-range index,
// a non-finite coordinate and an oversized primitive count must all make
// isValid() false, while the builders must still produce exactly the counts and
// bounds their comments promise.

#include <md/gfx/mesh.h>
#include <md/gfx/vertex_layout.h>

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTest>
#include <QTransform>
#include <QVector3D>

#include <cmath>
#include <limits>

using namespace md::gfx;

namespace {

Vertex2D vertex2D(qreal x, qreal y, const QColor& colour = QColor(Qt::white),
                  qreal u = 0.0, qreal v = 0.0)
{
    return Vertex2D{QPointF(x, y), colour, QPointF(u, v)};
}

Vertex3D vertex3D(float x, float y, float z,
                  const QColor& colour = QColor(Qt::white))
{
    return Vertex3D{QVector3D(x, y, z), QVector3D(0.0F, 0.0F, 1.0F), colour,
                    QPointF()};
}

} // namespace

class TestGfxMesh : public QObject
{
    Q_OBJECT

private slots:
    void vertexLayoutValidates();
    void rejectsOutOfRangeIndex();
    void rejectsNonFiniteVertex();
    void rejectsOversizedMesh();
    void gridCountsAndBounds();
    void transformedDoesNotMutateSource();
    void boxAndPlaneAreValid();
    void gridBuilderRefusesOversizedRequest();
};

void TestGfxMesh::vertexLayoutValidates()
{
    const VertexLayout layout(
        24, {Attribute{QStringLiteral("position"), AttributeType::Float3, 0},
             Attribute{QStringLiteral("colour"), AttributeType::UByte4Normalised, 12}});

    QVERIFY(layout.isValid());
    QCOMPARE(layout.stride(), 24);
    QCOMPARE(layout.offsetOf(QStringLiteral("position")), 0);
    QCOMPARE(layout.offsetOf(QStringLiteral("colour")), 12);
    QCOMPARE(layout.offsetOf(QStringLiteral("missing")), -1);
    QCOMPARE(VertexLayout::sizeOf(AttributeType::Float3), 12);
    QCOMPARE(VertexLayout::componentCount(AttributeType::UByte4Normalised), 4);

    // Duplicate names make the layout ambiguous.
    const VertexLayout duplicate(
        8, {Attribute{QStringLiteral("a"), AttributeType::Float, 0},
            Attribute{QStringLiteral("a"), AttributeType::Float, 4}});
    QVERIFY(!duplicate.isValid());
    QCOMPARE(duplicate.offsetOf(QStringLiteral("a")), -1);

    // The last attribute runs past the stride.
    const VertexLayout overrun(
        8, {Attribute{QStringLiteral("a"), AttributeType::Float3, 0}});
    QVERIFY(!overrun.isValid());

    // A negative offset can never be read.
    const VertexLayout negative(
        12, {Attribute{QStringLiteral("a"), AttributeType::Float, -4}});
    QVERIFY(!negative.isValid());
}

void TestGfxMesh::rejectsOutOfRangeIndex()
{
    const Mesh2D mesh({vertex2D(0, 0), vertex2D(10, 0), vertex2D(0, 10)},
                      {0, 1, 3});
    QVERIFY(!mesh.isValid());

    const Mesh3D mesh3D({vertex3D(0, 0, 0), vertex3D(1, 0, 0), vertex3D(0, 1, 0)},
                        {0, 1, 7});
    QVERIFY(!mesh3D.isValid());
}

void TestGfxMesh::rejectsNonFiniteVertex()
{
    const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
    const qreal infinity = std::numeric_limits<qreal>::infinity();

    QVERIFY(!Mesh2D({vertex2D(nan, 0), vertex2D(10, 0), vertex2D(0, 10)},
                    {0, 1, 2})
                 .isValid());

    Vertex2D infiniteUv = vertex2D(0, 0);
    infiniteUv.uv = QPointF(infinity, 0.0);
    QVERIFY(!Mesh2D({infiniteUv, vertex2D(10, 0), vertex2D(0, 10)}, {0, 1, 2})
                 .isValid());

    Vertex3D badNormal = vertex3D(0, 0, 0);
    badNormal.normal = QVector3D(static_cast<float>(nan), 0.0F, 0.0F);
    QVERIFY(!Mesh3D({badNormal, vertex3D(1, 0, 0), vertex3D(0, 1, 0)}, {0, 1, 2})
                 .isValid());
}

void TestGfxMesh::rejectsOversizedMesh()
{
    // One triangle over the documented cap. The index list is legal in every
    // other respect, so the count itself is what must be refused.
    const QVector<quint32> indices(static_cast<qsizetype>(3) * (kMaxTriangles + 1), 0);
    const Mesh2D mesh({vertex2D(0, 0), vertex2D(1, 0), vertex2D(0, 1)}, indices);
    QVERIFY(!mesh.isValid());
    QVERIFY(mesh.indices().size() > static_cast<qsizetype>(kMaxTriangles) * 3);
}

void TestGfxMesh::gridCountsAndBounds()
{
    const Mesh2D grid = Mesh2D::grid(QRectF(0.0, 0.0, 100.0, 50.0), 4, 2);
    QVERIFY(grid.isValid());
    QCOMPARE(grid.topology(), PrimitiveTopology::Triangles);
    QCOMPARE(grid.vertices().size(), 15);
    QCOMPARE(grid.indices().size(), 48); // 4 * 2 cells * 2 triangles * 3
    QCOMPARE(grid.bounds(), QRectF(0.0, 0.0, 100.0, 50.0));

    const Mesh2D rectangle = Mesh2D::rect(QRectF(10.0, 20.0, 30.0, 40.0),
                                          QColor(Qt::red));
    QVERIFY(rectangle.isValid());
    QCOMPARE(rectangle.vertices().size(), 4);
    QCOMPARE(rectangle.indices().size(), 6);
    QCOMPARE(rectangle.bounds(), QRectF(10.0, 20.0, 30.0, 40.0));
    QCOMPARE(rectangle.vertices().front().colour, QColor(Qt::red));

    QVERIFY(Mesh2D::grid(QRectF(0, 0, 10, 10), 0, 4).isEmpty());
    QVERIFY(Mesh2D::rect(QRectF(0, 0, 0, 10)).isEmpty());
}

void TestGfxMesh::transformedDoesNotMutateSource()
{
    const Mesh2D source = Mesh2D::rect(QRectF(0.0, 0.0, 10.0, 10.0));
    const QPointF firstPosition = source.vertices().front().position;

    const Mesh2D moved = source.transformed(QTransform().translate(5.0, 7.0));

    QCOMPARE(source.vertices().front().position, firstPosition);
    QCOMPARE(source.bounds(), QRectF(0.0, 0.0, 10.0, 10.0));
    QCOMPARE(moved.bounds(), QRectF(5.0, 7.0, 10.0, 10.0));
    QCOMPARE(moved.vertices().front().uv, source.vertices().front().uv);

    const Mesh3D box = Mesh3D::box(QVector3D(2.0F, 2.0F, 2.0F));
    const Bounds3D sourceBounds = box.bounds();
    const Mesh3D offset = box.transformed(QMatrix4x4());
    QCOMPARE(box.bounds().minimum, sourceBounds.minimum);
    QCOMPARE(offset.bounds().minimum, sourceBounds.minimum);
}

void TestGfxMesh::boxAndPlaneAreValid()
{
    const Mesh3D box = Mesh3D::box(QVector3D(2.0F, 4.0F, 6.0F));
    QVERIFY(box.isValid());
    QCOMPARE(box.vertices().size(), 24);
    QCOMPARE(box.indices().size(), 36);
    QCOMPARE(box.bounds().minimum.x(), -1.0F);
    QCOMPARE(box.bounds().minimum.y(), -2.0F);
    QCOMPARE(box.bounds().minimum.z(), -3.0F);
    QCOMPARE(box.bounds().maximum.x(), 1.0F);
    QCOMPARE(box.bounds().maximum.y(), 2.0F);
    QCOMPARE(box.bounds().maximum.z(), 3.0F);

    const Mesh3D plane = Mesh3D::plane(QSizeF(10.0, 10.0), 4);
    QVERIFY(plane.isValid());
    QCOMPARE(plane.vertices().size(), 25);
    QCOMPARE(plane.indices().size(), 96); // 4 * 4 cells * 2 triangles * 3
    QCOMPARE(plane.bounds().minimum.y(), 0.0F);
    QCOMPARE(plane.bounds().maximum.y(), 0.0F);

    const Mesh3D lattice = Mesh3D::grid(QSizeF(8.0, 8.0), 4);
    QVERIFY(lattice.isValid());
    QCOMPARE(lattice.topology(), PrimitiveTopology::Lines);
    QCOMPARE(lattice.indices().size(), 20); // 10 segments
}

void TestGfxMesh::gridBuilderRefusesOversizedRequest()
{
    // 181 cells would still fit the triangle cap; 182 does not. The builder must
    // answer with an empty mesh before it allocates anything.
    const Mesh2D refused = Mesh2D::grid(QRectF(0, 0, 10, 10), 400, 400);
    QVERIFY(refused.isEmpty());
    QVERIFY(refused.indices().isEmpty());

    const Mesh3D refusedPlane = Mesh3D::plane(QSizeF(10, 10), 400);
    QVERIFY(refusedPlane.isEmpty());
}

QTEST_GUILESS_MAIN(TestGfxMesh)

#include "tst_gfx_mesh.moc"
