// Periodicity — virtual data sources.
//
// A virtual source answers "how many" and "give me item n" instead of
// materialising every row. The virtualised selectors (VirtualSelect,
// VirtualTreeSelect) and VirtualTable read through these interfaces, so a list
// of a million rows costs the same as a list of ten.
//
// A flat source has an index; a tree source addresses nodes by an opaque id
// supplied by the source itself, which keeps arbitrary hierarchies (database
// ids, pointer handles) out of the component.

#pragma once

#include <QString>
#include <QVariant>
#include <QVariantList>

namespace md::structure {

class VirtualSource
{
public:
    virtual ~VirtualSource() = default;

    [[nodiscard]] virtual int count() const = 0;
    [[nodiscard]] virtual QVariant item(int index) const = 0;

    // The presentation text of an item. The default is the value's own string.
    [[nodiscard]] virtual QString label(int index) const;
};

// A VirtualSource over a QVariantList, which is what most callers start with.
class ListSource final : public VirtualSource
{
public:
    ListSource() = default;
    explicit ListSource(QVariantList values);

    void setValues(QVariantList values);
    [[nodiscard]] QVariantList values() const { return values_; }

    [[nodiscard]] int count() const override;
    [[nodiscard]] QVariant item(int index) const override;
    [[nodiscard]] QString label(int index) const override;

private:
    QVariantList values_;
};

// The virtual tree's data contract. Node ids are stable handles chosen by the
// source; root nodes are addressed with parent id 0 via rootAt().
class VirtualTreeSource
{
public:
    using NodeId = quint64;

    virtual ~VirtualTreeSource() = default;

    [[nodiscard]] virtual int rootCount() const = 0;
    [[nodiscard]] virtual NodeId rootAt(int index) const = 0;

    [[nodiscard]] virtual int childCount(NodeId parent) const = 0;
    [[nodiscard]] virtual NodeId childAt(NodeId parent, int index) const = 0;

    [[nodiscard]] virtual QVariant value(NodeId node) const = 0;
    [[nodiscard]] virtual QString label(NodeId node) const = 0;

    // A node with children is a branch and never selectable; the default asks
    // the source for its child count.
    [[nodiscard]] virtual bool hasChildren(NodeId node) const;
};

} // namespace md::structure
