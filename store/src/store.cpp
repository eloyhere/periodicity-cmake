#include <md/store/store.h>

#include <QCoreApplication>
#include <QDebug>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QThread>

#include <cstddef>
#include <vector>

namespace md {
namespace {

// The registry is function-local so that static initialisation order cannot
// hand a half-built hash to a store created during another static's
// constructor. Entries are QPointers so that a destroyed store can never be
// handed out, even if its destructor ran after the entry was already replaced.
QHash<QString, QPointer<Store>>& registry()
{
    static QHash<QString, QPointer<Store>> stores;
    return stores;
}

// Guards every access to the registry, so open()/find()/ids()/count()/close()
// never mutate the hash concurrently. It deliberately does not guard a store's
// values: those belong to the GUI thread (see the header).
QMutex& registryMutex()
{
    static QMutex mutex;
    return mutex;
}

// The debug-build affinity check for the QObject side of a Store. Before
// QCoreApplication exists there is no GUI thread yet, so the check passes and
// lets an application build stores during static setup.
void assertGuiThread()
{
    const QCoreApplication* application = QCoreApplication::instance();
    Q_ASSERT(application == nullptr || QThread::currentThread() == application->thread());
    Q_UNUSED(application);
}

} // namespace

Store::Store(const QString& id, QObject* parent)
    : QObject(parent)
    , id_(id)
{
    setObjectName(QStringLiteral("md.store.") + id);

    // The map owns the eviction policy; the store only forwards it as a signal.
    // The map calls the handler after the victim has left both of its
    // containers, and notifyEvicted() isolates a throwing observer, so the
    // callback itself never throws into the map.
    values_.setEvictionHandler(
        [this](const QString& key, const QVariant&) { notifyEvicted(key); });
}

Store::~Store()
{
    QMutexLocker locker(&registryMutex());
    const auto it = registry().find(id_);
    if (it != registry().end() && it.value().data() == this)
        registry().erase(it);
}

Store* Store::open(const QString& id)
{
    if (id.isEmpty())
        return nullptr;

    assertGuiThread();

    {
        QMutexLocker locker(&registryMutex());
        const auto it = registry().constFind(id);
        if (it != registry().constEnd()) {
            if (Store* existing = it.value().data())
                return existing;
        }
    }

    // Build outside the lock: the constructor does no registry work, and the
    // lock stays out of its allocation.
    auto* created = new Store(id);

    QMutexLocker locker(&registryMutex());
    auto it = registry().find(id);
    if (it == registry().end()) {
        registry().insert(id, created);
        return created;
    }

    // A parallel caller won the race (possible in a release build, where the
    // affinity assert is compiled out). Keep the registry unique and discard
    // the duplicate; values are GUI-thread-only anyway.
    Store* existing = it.value().data();
    if (existing == nullptr) {
        it.value() = created; // stale entry left by a cleared QPointer
        return created;
    }

    locker.unlock();
    delete created;
    return existing;
}

bool Store::close(const QString& id)
{
    if (id.isEmpty())
        return false;

    assertGuiThread();

    Store* store = nullptr;
    {
        QMutexLocker locker(&registryMutex());
        const auto it = registry().find(id);
        if (it == registry().end())
            return false;
        store = it.value().data();
        registry().erase(it);
    }

    if (store == nullptr)
        return false;

    if (store->notifyDepth_ > 0) {
        // Called from one of the store's own slots: deleting the object now
        // would unwind through its active emission. It is already gone from
        // the registry, so find()/isOpen() report the close immediately; the
        // object itself is deleted on the next event-loop turn.
        store->deleteLater();
        return true;
    }

    delete store;
    return true;
}

Store* Store::find(const QString& id)
{
    QMutexLocker locker(&registryMutex());
    const auto it = registry().constFind(id);
    return it == registry().constEnd() ? nullptr : it.value().data();
}

bool Store::isOpen(const QString& id)
{
    return find(id) != nullptr;
}

QStringList Store::ids()
{
    QMutexLocker locker(&registryMutex());

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
    QMutexLocker locker(&registryMutex());

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

bool Store::setValue(const QString& key, const QVariant& value)
{
    assertGuiThread();

    if (key.isEmpty())
        return false;

    // peek() rather than find(): a redundant write must not promote the key,
    // so an equal value remains a complete no-op (no signals, no recency
    // change).
    if (const QVariant* existing = values_.peek(key)) {
        if (existing->metaType() == value.metaType() && *existing == value)
            return false;
    }

    // put() promotes an existing key. For a new key in a full store it evicts
    // the oldest entry first, which the map reports through the handler
    // installed in the constructor (i.e. evicted() runs before valueChanged()).
    values_.put(key, value);
    notifyValueChanged(key, value);
    notifyChanged();
    return true;
}

QVariant Store::value(const QString& key, const QVariant& fallback) const
{
    // find() is the promoting lookup, so a const read still records the access
    // in the mutable map: that is the LRU contract documented in the header.
    if (const QVariant* stored = values_.find(key))
        return *stored;
    return fallback;
}

bool Store::hasValue(const QString& key) const
{
    return values_.contains(key); // membership only, never promotes
}

void Store::remove(const QString& key)
{
    assertGuiThread();

    // erase() bypasses the eviction handler, so an explicit removal never
    // emits evicted().
    if (!values_.erase(key))
        return;

    notifyValueChanged(key, QVariant());
    notifyRemoved(key);
    notifyChanged();
}

void Store::clear()
{
    assertGuiThread();

    if (values_.empty())
        return;

    // Snapshot before mutating: the notifications run arbitrary code, and an
    // observer that re-adds a key must not invalidate the iteration or be
    // removed by this call (the map was cleared before the first emit).
    // clear() does not run the eviction handler, so no evicted() is emitted.
    const QStringList order = keys();
    values_.clear();

    for (const QString& key : order) {
        notifyValueChanged(key, QVariant());
        notifyRemoved(key);
    }

    notifyCleared();
    notifyChanged();
}

QVariantMap Store::values() const
{
    QVariantMap result;
    // forEach() walks recency order without promoting anything; QVariantMap
    // sorts by key, so the returned snapshot carries no recency information.
    values_.forEach([&result](const QString& key, const QVariant& value) {
        result.insert(key, value);
    });
    return result;
}

QStringList Store::keys() const
{
    // The map's keys() is a non-promoting snapshot in eviction order.
    const std::vector<QString> ordered = values_.keys();
    QStringList result;
    result.reserve(static_cast<int>(ordered.size()));
    for (const QString& key : ordered)
        result.append(key);
    return result;
}

int Store::size() const
{
    return static_cast<int>(values_.size());
}

int Store::capacity() const noexcept
{
    return static_cast<int>(values_.capacity());
}

void Store::setCapacity(int entries)
{
    assertGuiThread();

    // Negative input is not a meaningful bound; treat it like 0 (unbounded)
    // instead of converting it to a huge size_t. Shrinking below size() drops
    // the oldest entries immediately, and the map's handler reports each one.
    values_.setCapacity(entries > 0 ? static_cast<std::size_t>(entries) : 0);
}

void Store::notifyValueChanged(const QString& key, const QVariant& value) noexcept
{
    ++notifyDepth_;
    try {
        emit valueChanged(key, value);
    } catch (...) {
        qWarning() << "md::Store: a valueChanged observer threw; the exception was isolated";
    }
    --notifyDepth_;
}

void Store::notifyRemoved(const QString& key) noexcept
{
    ++notifyDepth_;
    try {
        emit removed(key);
    } catch (...) {
        qWarning() << "md::Store: a removed observer threw; the exception was isolated";
    }
    --notifyDepth_;
}

void Store::notifyCleared() noexcept
{
    ++notifyDepth_;
    try {
        emit cleared();
    } catch (...) {
        qWarning() << "md::Store: a cleared observer threw; the exception was isolated";
    }
    --notifyDepth_;
}

void Store::notifyChanged() noexcept
{
    ++notifyDepth_;
    try {
        emit changed();
    } catch (...) {
        qWarning() << "md::Store: a changed observer threw; the exception was isolated";
    }
    --notifyDepth_;
}

void Store::notifyEvicted(const QString& key) noexcept
{
    // Called from the map's eviction handler, i.e. while the map is applying
    // the capacity policy. notifyDepth_ must cover the emission so that
    // close() called from an evicted() slot defers destruction instead of
    // deleting the store (and the map) from inside its own mutation.
    ++notifyDepth_;
    try {
        emit evicted(key);
    } catch (...) {
        qWarning() << "md::Store: an evicted observer threw; the exception was isolated";
    }
    --notifyDepth_;
}

} // namespace md
