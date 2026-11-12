#pragma once

#include <QAbstractListModel>
#include <QIdentityProxyModel>
#include <QList>
#include <QStringList>
#include <QVariant>

#include <functional>
#include <utility>

namespace md {

// Flat owned data for selection controls. QVariant can hold registered user types.
// Explicit labels are optional; ValueRole always returns the original value.
class Model final : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role { ValueRole = Qt::UserRole };
    Q_ENUM(Role)
    explicit Model(QObject* parent = nullptr);
    explicit Model(QVariantList values, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setValues(QVariantList values, QStringList labels = {});
    void append(QVariant value, QString label = {});
    [[nodiscard]] const QVariantList& values() const noexcept { return values_; }
private:
    QVariantList values_;
    QStringList labels_;
};

// Non-owning bridge to any flat Qt item model. Formatting changes presentation,
// never the stored values. Controls own their Adapter, not its source model.
// Display delegates remain available through each control's setItemDelegate().
//
// labelFor() is the presentation mapping every control draws from: the
// formatter when one is set, otherwise the source's DisplayRole (or the value's
// own string conversion). value() always returns the untouched source value.
class Adapter final : public QIdentityProxyModel
{
    Q_OBJECT
public:
    using Formatter = std::function<QString(const QVariant&)>;
    explicit Adapter(QObject* parent = nullptr);
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    void setFormatter(Formatter formatter);
    void setLabelFor(Formatter formatter);
    void setValueRole(int role);
    [[nodiscard]] int valueRole() const noexcept { return valueRole_; }
    [[nodiscard]] QString labelFor(const QVariant& value) const;
    [[nodiscard]] QVariant value(int row) const;
    [[nodiscard]] QString text(int row) const;
private:
    void refresh();
    Formatter formatter_;
    int valueRole_ = Qt::UserRole;
};

// Text parsing is separate from list presentation so adapters can be reused
// without requiring a widget or QObject.
template<class T> class InputAdapter
{
public:
    virtual ~InputAdapter() = default;
    [[nodiscard]] virtual T fromText(const QString& text, bool* ok = nullptr) const = 0;
    [[nodiscard]] virtual QString toText(const T& value) const = 0;
};

template<class T> class FunctionalInputAdapter final : public InputAdapter<T>
{
public:
    using Parser = std::function<T(const QString&, bool*)>;
    using Formatter = std::function<QString(const T&)>;
    FunctionalInputAdapter(Parser parser, Formatter formatter)
        : parser_(std::move(parser)), formatter_(std::move(formatter)) {}
    T fromText(const QString& text, bool* ok = nullptr) const override
    { return parser_(text, ok); }
    QString toText(const T& value) const override { return formatter_(value); }
private:
    Parser parser_;
    Formatter formatter_;
};

} // namespace md
