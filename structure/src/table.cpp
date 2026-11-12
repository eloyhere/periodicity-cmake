#include <md/structure/table.h>

namespace md::structure {

TableData::TableData(QStringList headers)
    : headers_(std::move(headers))
{
}

void TableData::setHeaders(QStringList headers)
{
    headers_ = std::move(headers);
}

void TableData::addRow(QVector<QVariant> cells)
{
    rows_.append(std::move(cells));
}

void TableData::setRows(QVector<QVector<QVariant>> rows)
{
    rows_ = std::move(rows);
}

void TableData::clear()
{
    headers_.clear();
    rows_.clear();
}

QVariant TableData::cell(int row, int column) const
{
    if (row < 0 || row >= rows_.size())
        return {};
    const QVector<QVariant>& cells = rows_.at(row);
    if (column < 0 || column >= cells.size())
        return {};
    return cells.at(column);
}

QString TableData::text(int row, int column) const
{
    return cell(row, column).toString();
}

} // namespace md::structure
