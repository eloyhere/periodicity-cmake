// Periodicity — the data model and its presentation adapter.
//
// Model is the small owned list every selection control can fall back to: it
// stores arbitrary QVariants, exposes them through ValueRole and renders a
// label per row (an explicit label when one was supplied, otherwise the
// value's own string conversion).
//
// Adapter is the non-owning bridge to a flat QAbstractItemModel. It never
// mutates or takes over the source: value() reads the configured value role,
// labelFor() applies the formatter, and data() maps both onto the roles the
// views ask for. The identity proxy already forwards row resets, insertions and
// removals; only the presentation cache (none) would need invalidation, so a
// formatter change simply republishes DisplayRole for every row.

#include <md/components/model.h>

#include <QHash>
#include <QMetaType>
#include <QModelIndex>

#include <utility>

namespace md {

namespace {

// The fallback presentation for a value: a string that the type itself can
// produce, and nothing for an opaque user type that has no string conversion
// (the Adapter formatter is the answer for those).
QString defaultLabel(const QVariant& value)
{
    if (!value.isValid() || value.isNull())
        return {};

    if (value.metaType().id() == QMetaType::QString)
        return value.toString();

    if (value.canConvert<QString>())
        return value.toString();

    return {};
}

} // namespace

Model::Model(QObject* parent)
    : QAbstractListModel(parent)
{
}

Model::Model(QVariantList values, QObject* parent)
    : QAbstractListModel(parent)
    , values_(std::move(values))
{
}

int Model::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;

    return static_cast<int>(values_.size());
}

QVariant Model::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= values_.size())
        return {};

    const int row = index.row();

    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        if (row < labels_.size()) {
            const QString& label = labels_.at(row);
            if (!label.isEmpty())
                return label;
        }
        return defaultLabel(values_.at(row));
    case ValueRole:
        return values_.at(row);
    default:
        break;
    }

    return {};
}

QHash<int, QByteArray> Model::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractListModel::roleNames();
    roles.insert(Qt::DisplayRole, QByteArrayLiteral("display"));
    roles.insert(ValueRole, QByteArrayLiteral("value"));
    return roles;
}

void Model::setValues(QVariantList values, QStringList labels)
{
    beginResetModel();

    values_ = std::move(values);

    if (labels.size() > values_.size())
        labels = labels.mid(0, static_cast<int>(values_.size()));
    while (labels.size() < values_.size())
        labels.append(QString());
    labels_ = std::move(labels);

    endResetModel();
}

void Model::append(QVariant value, QString label)
{
    const int row = static_cast<int>(values_.size());

    beginInsertRows(QModelIndex(), row, row);
    values_.append(std::move(value));
    while (labels_.size() < values_.size() - 1)
        labels_.append(QString());
    labels_.append(std::move(label));
    endInsertRows();
}

Adapter::Adapter(QObject* parent)
    : QIdentityProxyModel(parent)
{
}

QVariant Adapter::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};

    // A formatter owns presentation: it is applied to the stored value, never
    // to the source's display text. Without one, the source's own DisplayRole
    // is authoritative, which is what lets Model expose explicit labels. A
    // source that only stores values (empty DisplayRole) still gets a readable
    // row from the value itself.
    if (role == Qt::DisplayRole) {
        if (formatter_)
            return formatter_(value(index.row()));

        const QVariant shown = QIdentityProxyModel::data(index, role);
        if (shown.isValid() && !shown.toString().isEmpty())
            return shown;
        return labelFor(value(index.row()));
    }

    if (role == Qt::EditRole && formatter_)
        return formatter_(value(index.row()));

    return QIdentityProxyModel::data(index, role);
}

void Adapter::setFormatter(Formatter formatter)
{
    formatter_ = std::move(formatter);
    refresh();
}

void Adapter::setLabelFor(Formatter formatter)
{
    setFormatter(std::move(formatter));
}

void Adapter::setValueRole(int role)
{
    if (valueRole_ == role)
        return;

    valueRole_ = role;
    refresh();
}

QString Adapter::labelFor(const QVariant& value) const
{
    if (formatter_)
        return formatter_(value);

    return defaultLabel(value);
}

QVariant Adapter::value(int row) const
{
    const QAbstractItemModel* source = sourceModel();
    if (source == nullptr || row < 0 || row >= source->rowCount())
        return {};

    const QModelIndex sourceIndex = source->index(row, 0);
    if (!sourceIndex.isValid())
        return {};

    QVariant stored = source->data(sourceIndex, valueRole_);
    if (!stored.isValid() && valueRole_ != Qt::DisplayRole)
        stored = source->data(sourceIndex, Qt::DisplayRole);
    return stored;
}

QString Adapter::text(int row) const
{
    return data(index(row, 0), Qt::DisplayRole).toString();
}

void Adapter::refresh()
{
    const int rows = rowCount();
    if (rows <= 0)
        return;

    emit dataChanged(index(0, 0), index(rows - 1, 0),
                     {Qt::DisplayRole, Qt::EditRole, valueRole_});
}

} // namespace md
