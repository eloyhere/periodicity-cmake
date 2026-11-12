#include <md/store/store.h>

#include <QHash>
#include <QPointer>

namespace md {
namespace {

// The registry is function-local so that static initialisation order cannot
// hand a half-built hash to a store created during another static's
// constructor.
QHash<QString, QPointer<Store>>& registry()
{
    static QHash<QString, QPointer<Store>> stores;
    return stores;
}

} // namespace

Store::Store(const QString& id, QObject* parent)
    : QObject(parent)
    , id_(id)
{
    setObjectName(QStringLiteral("md.store.") + id);
}

Store::~Store()
{
    auto it = registry().find(id_);
    if (it != registry().end() && it.value() == this)
        registry().erase(it);
}

Store* Store::open(const QString& id)
{
    if (id.isEmpty())
        return nullptr;

    const auto it = registry().constFind(id);
    if (it != registry().constEnd() && it.value() != nullptr)
        return it.value().data();

    auto* store = new Store(id);
    registry().insert(id, store);
    return store;
}

Store* Store::find(const QString& id)
{
    const auto it = registry().constFind(id);
    return it == registry().constEnd() ? nullptr : it.value().data();
}

bool Store::isOpen(const QString& id)
{
    return find(id) != nullptr;
}

QStringList Store::ids()
{
    QStringList result;
    result.reserve(registry().size());
    for (auto it = registry().constBegin(); it != registry().constEnd(); ++it) {
        if (it.value() != nullptr)
            result.append(it.key());
    }
    result.sort();
    return result;
}

int Store::count()
{
    int total = 0;
    for (auto it = registry().constBegin(); it != registry().constEnd(); ++it) {
        if (it.value() != nullptr)
            ++total;
    }
    return total;
}

QString Store::id() const
{
    return id_;
}

void Store::setValue(const QString& key, const QVariant& value)
{
    if (key.isEmpty())
        return;

    const auto existing = values_.constFind(key);
    if (existing != values_.constEnd() && existing.value() == value)
        return;

    values_.insert(key, value);
    emit valueChanged(key, value);
    emit changed();
}

QVariant Store::value(const QString& key, const QVariant& fallback) const
{
    const auto it = values_.constFind(key);
    return it == values_.constEnd() ? fallback : it.value();
}

bool Store::hasValue(const QString& key) const
{
    return values_.contains(key);
}

void Store::remove(const QString& key)
{
    const auto it = values_.find(key);
    if (it == values_.end())
        return;

    const QVariant old = it.value();
    values_.erase(it);
    emit valueChanged(key, QVariant());
    emit changed();
    Q_UNUSED(old);
}

void Store::clear()
{
    if (values_.isEmpty())
        return;

    values_.clear();
    emit changed();
}

QVariantMap Store::values() const
{
    return values_;
}

int Store::size() const
{
    return static_cast<int>(values_.size());
}

} // namespace md
