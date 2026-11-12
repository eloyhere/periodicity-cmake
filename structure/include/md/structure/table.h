// Periodicity — tabular data for the table components.
//
// TableComponent reads a QAbstractItemModel; TableData is the small value table
// an application can build without writing a model, and VirtualTableSource is
// the row/column accessor a virtualised table pulls cells from on demand.

#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <Qt>

namespace md::structure {

struct TableColumn
{
    QString title;
    qreal widthDp = 120.0;
    Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter;
};

// A small in-memory table: headers plus rows of QVariant cells.
class TableData
{
public:
    TableData() = default;
    explicit TableData(QStringList headers);

    void setHeaders(QStringList headers);
    [[nodiscard]] QStringList headers() const { return headers_; }

    void addRow(QVector<QVariant> cells);
    void setRows(QVector<QVector<QVariant>> rows);
    void clear();

    [[nodiscard]] int rowCount() const noexcept { return rows_.size(); }
    [[nodiscard]] int columnCount() const noexcept { return headers_.size(); }
    [[nodiscard]] QVariant cell(int row, int column) const;
    [[nodiscard]] QString text(int row, int column) const;

private:
    QStringList headers_;
    QVector<QVector<QVariant>> rows_;
};

// The virtual table's data contract. The table asks only for the cells it is
// about to paint, so a source backed by a database or a generator stays cheap.
class VirtualTableSource
{
public:
    virtual ~VirtualTableSource() = default;

    [[nodiscard]] virtual int rowCount() const = 0;
    [[nodiscard]] virtual int columnCount() const = 0;
    [[nodiscard]] virtual QVariant cell(int row, int column) const = 0;
    [[nodiscard]] virtual QString header(int column) const = 0;
};

} // namespace md::structure
