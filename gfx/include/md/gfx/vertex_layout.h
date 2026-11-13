// Periodicity — gfx layer: how a packed vertex buffer is read.
//
// A VertexLayout never owns vertex data. It records the stride between vertices
// and, for each named attribute, its component type and byte offset. A caller
// that reads a buffer through an invalid layout would run off the end of a
// vertex, so validation happens once, at construction, and isValid() reports it:
//
//   - stride must be positive;
//   - every attribute name must be non-empty and unique;
//   - every offset must be non-negative and the attribute's size must fit
//     inside the stride;
//   - at most kMaxAttributes attributes.
//
// An invalid layout is inert: offsetOf() returns -1 for every name, so ignoring
// the diagnostic cannot turn into an out-of-bounds read. There is no alignment
// requirement beyond the size fits: gfx reads attributes with memcpy, so a
// packed buffer is legal.

#pragma once

#include <QString>
#include <QVector>

namespace md::gfx {

// Four floats is the widest attribute the software rasterizer reads.
inline constexpr int kMaxAttributes = 16;

enum class AttributeType
{
    Float,
    Float2,
    Float3,
    Float4,
    // Four bytes in RGBA order, mapped to [0, 1] by dividing by 255.
    UByte4Normalised,
};

struct Attribute
{
    QString name;
    AttributeType type = AttributeType::Float3;
    int offset = 0;
};

class VertexLayout
{
public:
    VertexLayout() = default;
    VertexLayout(int stride, QVector<Attribute> attributes);

    int stride() const { return stride_; }
    const QVector<Attribute>& attributes() const { return attributes_; }

    bool isValid() const;

    // -1 when the layout is invalid or `name` is not one of its attributes.
    int offsetOf(const QString& name) const;

    // Bytes occupied by one component set, and its component count.
    static int sizeOf(AttributeType type);
    static int componentCount(AttributeType type);

private:
    int stride_ = 0;
    QVector<Attribute> attributes_;
};

} // namespace md::gfx
