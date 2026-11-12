// Periodicity — Store, a named configuration bag with a global registry.
//
// A Store holds a structure::UnorderedLRUMap<QString, QVariant> and is
// addressed by an id. The registry makes the id unique inside the process:
// `Store::open("md.theme")` returns the same object for every caller, so the
// Font container, the Theme scope and the Language selector can publish their
// current configuration without passing pointers around.
//
// Threading. The registry is mutex-guarded, so `find()`, `isOpen()`, `ids()`
// and `count()` may be called from any thread. A Store itself is a QObject:
// it belongs to the thread that opened it (the GUI thread), and its values,
// signals and lifetime are GUI-thread-only. `open()`, `close()` and every
// mutator assert that affinity in debug builds; a release build is not
// suddenly cross-thread-safe.
//
// Values are untyped. Any QVariant is accepted and the reader decides how to
// interpret it, so `value("count").toInt()` coerces a stored string. A change
// of the stored type (int → QString) is an ordinary change and emits
// valueChanged(); there is no schema and no per-value size limit. Redundancy is
// checked with metaType() first and QVariant::operator== only for the same
// type, so a huge value is never deep-compared against a value of another
// type; a type without operator== always counts as a change.
//
// The value map is bounded. capacity() is the maximum number of entries
// (default 1024) and 0 means unbounded; setCapacity() changes it. Writing a
// new key into a full store first evicts the least-recently-used entry and
// emits evicted(key). A read through value() promotes its key, so even a const
// read decides which entry a later write drops; hasValue(), keys() and
// values() never promote. Capacity eviction is not a removal: it emits
// evicted() only, never valueChanged(), removed() or changed().
//
// Removal and an invalid value both arrive as valueChanged(key, QVariant()),
// because a signal argument cannot be absent. Use hasValue(key) to tell them
// apart: it is false after a removal and true after setValue(key, QVariant()).
// remove() also emits removed(key), and clear() emits it once per key.

#pragma once

#include <md/structure/lru/unordered_lru_map.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

namespace md {

class Store final : public QObject
{
    Q_OBJECT

public:
    // Returns the store registered under `id`, creating it on first use. An
    // empty id is rejected and yields nullptr.
    //
    // The registry is mutex-guarded, so parallel calls for the same id still
    // yield one object in the registry. open() itself is GUI-thread-only (the
    // debug build asserts it) because it may construct the QObject; use find()
    // for a registry lookup from another thread.
    static Store* open(const QString& id);

    // Destroys the store registered under `id` and returns true when one was
    // open; an unknown or empty id yields false. Every pointer previously
    // returned by open()/find() for that id dangles afterwards.
    //
    // When close() is called from one of the store's own slots, the registry
    // entry is released immediately but the QObject is deleted on the next
    // event-loop turn, so the active signal emission can unwind safely.
    [[nodiscard]] static bool close(const QString& id);

    // The store registered under `id`, or nullptr. The pointer is valid until
    // close() (or the process ends); the registry lookup itself is thread-safe.
    [[nodiscard]] static Store* find(const QString& id);
    [[nodiscard]] static bool isOpen(const QString& id);
    [[nodiscard]] static QStringList ids();
    [[nodiscard]] static int count();

    [[nodiscard]] QString id() const;

    // Values -------------------------------------------------------------
    // Writes `value` under `key` and returns true when the key was written.
    //
    // Returns false, without emitting anything, when `key` is empty (the
    // write is rejected rather than stored under an unnamed key) or when the
    // stored value is already equal (same metaType() and QVariant::operator==).
    // A redundant write is a complete no-op: it does not promote the key, so
    // recency changes only when the stored value changes.
    //
    // Writing a new key into a full store evicts the least-recently-used entry
    // first, emitting evicted(key) before this key's valueChanged().
    //
    // A throwing observer is isolated: the exception is logged, the store's
    // bookkeeping still runs, and the value stays written. Only synchronous
    // (directly invoked) observers are covered; a queued slot throws inside
    // the event loop, where the store cannot intercept it.
    bool setValue(const QString& key, const QVariant& value);

    // The stored value, or `fallback` when the key is absent. A key holding an
    // invalid QVariant is present and returns that invalid value.
    //
    // This is an LRU read: a hit promotes `key` to most-recently-used even
    // though the method is const, so a later write into a full store evicts a
    // different key. hasValue() is the non-promoting presence test.
    [[nodiscard]] QVariant value(const QString& key,
                                 const QVariant& fallback = QVariant()) const;

    // True when `key` is present, even when its stored QVariant is invalid.
    // This is the discriminator between `setValue(key, QVariant())` and
    // `remove(key)`/`clear()`; both emit an empty valueChanged() payload.
    // A membership probe only: it does not promote the key.
    [[nodiscard]] bool hasValue(const QString& key) const;

    // Removes `key`. Emits valueChanged(key, QVariant()) and removed(key), then
    // changed(). An absent key is a no-op. Use hasValue() to distinguish this
    // from storing an invalid value. An explicit removal never emits evicted().
    void remove(const QString& key);

    // Removes every key. The keys are snapshotted first, then the map is
    // cleared, then valueChanged(key, QVariant()) and removed(key) are emitted
    // once per key, followed by one cleared() and one changed(). Observers
    // therefore see an already empty store; a slot that re-adds a key during
    // the notifications is not removed a second time. An explicit clear never
    // emits evicted() and keeps the current capacity().
    void clear();

    // Every entry as a QVariantMap. QVariantMap sorts by key, so the snapshot
    // carries no recency information; unlike value(), it promotes nothing.
    [[nodiscard]] QVariantMap values() const;

    // The keys in recency order: oldest (the next entry a write would evict)
    // first, the most recently used last. Like values(), this is a
    // non-promoting snapshot: after setValue("a", 1); setValue("b", 2);
    // value("a"), keys() is {"b", "a"}.
    [[nodiscard]] QStringList keys() const;

    [[nodiscard]] int size() const;

    // Capacity -----------------------------------------------------------
    // The maximum number of entries the store holds; 0 means unbounded. The
    // default is 1024 (structure::UnorderedLRUMap's default).
    [[nodiscard]] int capacity() const noexcept;

    // Sets that bound. `entries <= 0` means unbounded. Growing is immediate;
    // shrinking below size() evicts the least-recently-used entries at once,
    // oldest first, emitting evicted() once per dropped key.
    void setCapacity(int entries);

signals:
    // The value under `key` changed. `value` is QVariant() for a removal; call
    // hasValue() to tell a removal from an invalid value.
    void valueChanged(const QString& key, const QVariant& value);

    // `key` was removed by remove() or clear(). Never emitted by setValue().
    void removed(const QString& key);

    // clear() removed at least one key; emitted after the per-key signals.
    void cleared();

    // Any change made by setValue(), remove() or clear(): after
    // valueChanged()/removed(), and once after clear()'s per-key signals.
    // Capacity eviction is not reported here; it emits evicted() only.
    void changed();

    // `key` was dropped by the capacity policy: an insertion into a full store
    // or a shrinking setCapacity(). Emitted once per dropped key, oldest first.
    // Never emitted by remove(), clear(), close() or the destructor.
    //
    // The signal runs while the store is still applying the capacity policy,
    // so a slot connected here must not re-enter setValue(), setCapacity(),
    // remove() or clear() on the same store. Use a queued connection to defer
    // work that touches the store.
    void evicted(const QString& key);

private:
    explicit Store(const QString& id, QObject* parent = nullptr);
    ~Store() override;

    // Each emitter isolates a throwing observer, so one bad slot cannot abort
    // the remaining notifications or leave the store half-updated.
    void notifyValueChanged(const QString& key, const QVariant& value) noexcept;
    void notifyRemoved(const QString& key) noexcept;
    void notifyCleared() noexcept;
    void notifyChanged() noexcept;
    // Forwards the map's eviction callback as evicted(); also isolates a
    // throwing observer. Called with the store still inside a map mutation.
    void notifyEvicted(const QString& key) noexcept;

    QString id_;

    // The bounded value map. mutable because value() is a const LRU read: it
    // promotes the key it returns. The eviction handler is installed by the
    // constructor and reports capacity drops through notifyEvicted().
    mutable structure::UnorderedLRUMap<QString, QVariant> values_;

    // Number of notifications currently on the stack. close() reads it to
    // defer destruction when it is called from one of the store's own slots.
    int notifyDepth_ = 0;
};

} // namespace md
