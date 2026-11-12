// Periodicity — tree data for the tree-based selectors.
//
// A TreeNode is a plain value: a label, an opaque value and its children. It is
// what TreeSelect walks when it is given an in-memory hierarchy instead of a Qt
// item model, and what the flattening helper turns into the visible rows a
// virtualised tree paints.
//
// The selection rule the tree components implement lives here as a predicate:
// a node with children is a branch and is never selectable; only leaves are.

#pragma once

#include <QString>
#include <QVector>
#include <QVariant>

namespace md::structure {

struct TreeNode
{
    QVariant value;
    QString label;
    QVector<TreeNode> children;

    TreeNode() = default;
    TreeNode(QString labelText, QVariant nodeValue = {},
             QVector<TreeNode> childNodes = {})
        : value(std::move(nodeValue))
        , label(std::move(labelText))
        , children(std::move(childNodes))
    {
    }

    [[nodiscard]] bool hasChildren() const noexcept { return !children.isEmpty(); }
    [[nodiscard]] bool isLeaf() const noexcept { return children.isEmpty(); }
};

// One visible row of a flattened tree: the node, how deep it sits, and whether
// it is currently expanded. `row` is the position in the flattened list.
struct TreeRow
{
    const TreeNode* node = nullptr;
    int depth = 0;
    bool expanded = false;
};

// Flattens `roots` into the rows that are visible with `expandedKeys` expanded.
// A branch is visible when everything above it is expanded; its children follow
// immediately when the branch itself is expanded. Keys are matched against
// TreeNode::value (QVariant equality), so callers keep their own identity. A
// vector rather than a QSet because Qt does not hash QVariant.
[[nodiscard]] QVector<TreeRow> flattenTree(const QVector<TreeNode>& roots,
                                           const QVector<QVariant>& expandedKeys);

} // namespace md::structure
