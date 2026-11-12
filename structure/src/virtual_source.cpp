#include <md/structure/virtual_source.h>

namespace md::structure {

QString VirtualSource::label(int index) const
{
    return item(index).toString();
}

ListSource::ListSource(QVariantList values)
    : values_(std::move(values))
{
}

void ListSource::setValues(QVariantList values)
{
    values_ = std::move(values);
}

int ListSource::count() const
{
    return values_.size();
}

QVariant ListSource::item(int index) const
{
    if (index < 0 || index >= values_.size())
        return {};
    return values_.at(index);
}

QString ListSource::label(int index) const
{
    return item(index).toString();
}

bool VirtualTreeSource::hasChildren(NodeId node) const
{
    return childCount(node) > 0;
}

} // namespace md::structure
