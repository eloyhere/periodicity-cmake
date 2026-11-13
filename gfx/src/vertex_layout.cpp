// Periodicity — gfx layer: vertex-layout validation.
//
// The layout is a description, not storage. Everything here is checking and
// answering offsets; nothing touches a vertex buffer, which is why the same
// layout works for an interleaved buffer in memory and for one staged in a
// worker thread.

#include <md/gfx/vertex_layout.h>

#include <QSet>

namespace md::gfx {

VertexLayout::VertexLayout(int stride, QVector<Attribute> attributes)
    : stride_(stride)
    , attributes_(std::move(attributes))
{
}

int VertexLayout::sizeOf(AttributeType type)
{
    switch (type) {
    case AttributeType::Float:
        return 4;
    case AttributeType::Float2:
        return 8;
    case AttributeType::Float3:
        return 12;
    case AttributeType::Float4:
        return 16;
    case AttributeType::UByte4Normalised:
        return 4;
    }
    return 0;
}

int VertexLayout::componentCount(AttributeType type)
{
    switch (type) {
    case AttributeType::Float:
        return 1;
    case AttributeType::Float2:
        return 2;
    case AttributeType::Float3:
        return 3;
    case AttributeType::Float4:
    case AttributeType::UByte4Normalised:
        return 4;
    }
    return 0;
}

bool VertexLayout::isValid() const
{
    if (stride_ <= 0 || attributes_.isEmpty()
        || attributes_.size() > kMaxAttributes) {
        return false;
    }

    QSet<QString> names;
    names.reserve(attributes_.size());

    for (const Attribute& attribute : attributes_) {
        if (attribute.name.isEmpty() || names.contains(attribute.name))
            return false;
        names.insert(attribute.name);

        const int size = sizeOf(attribute.type);
        if (size <= 0 || attribute.offset < 0)
            return false;

        // Written as a subtraction so a huge offset cannot overflow the sum.
        if (attribute.offset > stride_ - size)
            return false;
    }

    return true;
}

int VertexLayout::offsetOf(const QString& name) const
{
    // An invalid layout addresses nothing: a caller that ignores isValid()
    // cannot accidentally read a stale offset out of a vertex.
    if (!isValid())
        return -1;

    for (const Attribute& attribute : attributes_) {
        if (attribute.name == name)
            return attribute.offset;
    }
    return -1;
}

} // namespace md::gfx
