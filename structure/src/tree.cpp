#include <md/structure/tree.h>

namespace md::structure {
namespace {

bool isExpanded(const QVector<QVariant>& keys, const QVariant& value)
{
    return keys.contains(value);
}

void appendRows(const QVector<TreeNode>& nodes, int depth, const QVector<QVariant>& expandedKeys,
                QVector<TreeRow>& rows)
{
    for (const TreeNode& node : nodes) {
        const bool expanded = node.hasChildren() && isExpanded(expandedKeys, node.value);
        rows.append(TreeRow{&node, depth, expanded});

        if (expanded)
            appendRows(node.children, depth + 1, expandedKeys, rows);
    }
}

} // namespace

QVector<TreeRow> flattenTree(const QVector<TreeNode>& roots, const QVector<QVariant>& expandedKeys)
{
    QVector<TreeRow> rows;
    rows.reserve(roots.size());
    appendRows(roots, 0, expandedKeys, rows);
    return rows;
}

} // namespace md::structure
