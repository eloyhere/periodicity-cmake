// Store: the process-wide registry and value notifications.

#include <md/store/store.h>

#include <QCoreApplication>
#include <QList>
#include <QMetaType>
#include <QPointer>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>

#include <stdexcept>

namespace {

// A valueChanged() observer that records the exact payload it was handed, so a
// test can assert that an invalid QVariant really arrives as an invalid
// QVariant rather than as a QSignalSpy-wrapped one.
struct Notification
{
    QString key;
    QVariant value;
};

} // namespace

class TestStore : public QObject
{
    Q_OBJECT

private slots:
    void openIsStable();
    void differentIdsAreDifferentStores();
    void valuesEmitOnlyOnChange();
    void fallbackAndRemoval();
    void registryListsOpenStores();
    void clearNotifiesEveryKey();
    void removalIsDistinguishableFromInvalidValue();
    void emptyKeyIsRejected();
    void typeChangeIsAnOrdinaryChange();
    void closingDestroysTheStore();
    void throwingObserverDoesNotBreakSetValue();
    void capacityDefaultsTo1024();
    void evictionDropsTheOldestEntries();
    void readPromotesTheKey();
    void zeroCapacityIsUnbounded();
    void shrinkingCapacityEvictsAndNotifies();
    void removeAndClearDoNotEvict();
    void keysFollowRecencyOrder();
    void redundantWriteDoesNotPromote();
    void throwingEvictedObserverIsIsolated();
};

void TestStore::openIsStable()
{
    md::Store* first = md::Store::open(QStringLiteral("test.stable"));
    md::Store* second = md::Store::open(QStringLiteral("test.stable"));

    QVERIFY(first != nullptr);
    QCOMPARE(first, second);
    QCOMPARE(md::Store::find(QStringLiteral("test.stable")), first);
    QVERIFY(md::Store::isOpen(QStringLiteral("test.stable")));
    QCOMPARE(first->id(), QStringLiteral("test.stable"));
    QCOMPARE(md::Store::open(QString()), nullptr);
}

void TestStore::differentIdsAreDifferentStores()
{
    md::Store* first = md::Store::open(QStringLiteral("test.one"));
    md::Store* second = md::Store::open(QStringLiteral("test.two"));
    QVERIFY(first != second);
    QVERIFY(first->id() != second->id());
}

void TestStore::valuesEmitOnlyOnChange()
{
    md::Store* store = md::Store::open(QStringLiteral("test.values"));
    store->clear();

    QSignalSpy changed(store, &md::Store::valueChanged);
    QSignalSpy any(store, &md::Store::changed);

    QVERIFY(store->setValue(QStringLiteral("family"), QStringLiteral("Georgia")));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(any.count(), 1);
    QCOMPARE(changed.at(0).at(0).toString(), QStringLiteral("family"));
    QCOMPARE(store->value(QStringLiteral("family")).toString(), QStringLiteral("Georgia"));

    // An equal value of the same type is a no-op.
    QVERIFY(!store->setValue(QStringLiteral("family"), QStringLiteral("Georgia")));
    QCOMPARE(changed.count(), 1);

    QVERIFY(store->setValue(QStringLiteral("family"), QStringLiteral("Courier New")));
    QCOMPARE(changed.count(), 2);
}

void TestStore::fallbackAndRemoval()
{
    md::Store* store = md::Store::open(QStringLiteral("test.fallback"));
    store->clear();

    QCOMPARE(store->value(QStringLiteral("missing"), 42).toInt(), 42);
    QVERIFY(!store->hasValue(QStringLiteral("missing")));

    store->setValue(QStringLiteral("a"), 1);
    QVERIFY(store->hasValue(QStringLiteral("a")));
    QCOMPARE(store->size(), 1);

    store->remove(QStringLiteral("a"));
    QVERIFY(!store->hasValue(QStringLiteral("a")));
    QCOMPARE(store->size(), 0);

    store->setValue(QStringLiteral("b"), 2);
    store->clear();
    QCOMPARE(store->size(), 0);
}

void TestStore::registryListsOpenStores()
{
    md::Store::open(QStringLiteral("test.list.a"));
    md::Store::open(QStringLiteral("test.list.b"));

    const QStringList ids = md::Store::ids();
    QVERIFY(ids.contains(QStringLiteral("test.list.a")));
    QVERIFY(ids.contains(QStringLiteral("test.list.b")));
    QVERIFY(md::Store::count() >= 2);
}

void TestStore::clearNotifiesEveryKey()
{
    md::Store* store = md::Store::open(QStringLiteral("test.clear"));
    store->clear();

    store->setValue(QStringLiteral("a"), 1);
    store->setValue(QStringLiteral("b"), QStringLiteral("two"));

    QList<Notification> notifications;
    // A local context disconnects the lambda when the test ends, even though
    // the store itself lives on in the process-wide registry.
    QObject context;
    QObject::connect(store, &md::Store::valueChanged, &context,
                     [&notifications](const QString& key, const QVariant& value) {
                         notifications.append(Notification{key, value});
                     });
    QSignalSpy removed(store, &md::Store::removed);
    QSignalSpy cleared(store, &md::Store::cleared);
    QSignalSpy changed(store, &md::Store::changed);

    store->clear();

    // One notification per key, over a snapshot of the map, with an empty
    // payload; then the bulk signals.
    QCOMPARE(notifications.size(), 2);
    QStringList seen;
    for (const Notification& notification : notifications) {
        seen.append(notification.key);
        QVERIFY(!notification.value.isValid());
    }
    seen.sort();
    QCOMPARE(seen, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
    QCOMPARE(removed.count(), 2);
    QCOMPARE(cleared.count(), 1);
    QCOMPARE(changed.count(), 1);
    QVERIFY(!store->hasValue(QStringLiteral("a")));
    QVERIFY(!store->hasValue(QStringLiteral("b")));
    QVERIFY(store->values().isEmpty());

    // An empty store has nothing to report.
    QSignalSpy silent(store, &md::Store::changed);
    store->clear();
    QCOMPARE(silent.count(), 0);
}

void TestStore::removalIsDistinguishableFromInvalidValue()
{
    md::Store* store = md::Store::open(QStringLiteral("test.removal"));
    store->clear();

    QList<Notification> notifications;
    QObject context;
    QObject::connect(store, &md::Store::valueChanged, &context,
                     [&notifications](const QString& key, const QVariant& value) {
                         notifications.append(Notification{key, value});
                     });
    QSignalSpy removed(store, &md::Store::removed);

    QVERIFY(store->setValue(QStringLiteral("k"), 10));
    QCOMPARE(notifications.size(), 1);
    QVERIFY(notifications.constLast().value.isValid());
    QCOMPARE(notifications.constLast().value.toInt(), 10);

    // Writing an invalid value is a normal write: the key stays present and no
    // removed() is emitted. The valueChanged() payload is empty.
    QVERIFY(store->setValue(QStringLiteral("k"), QVariant()));
    QCOMPARE(notifications.size(), 2);
    QVERIFY(!notifications.constLast().value.isValid());
    QVERIFY(store->hasValue(QStringLiteral("k")));
    QCOMPARE(removed.count(), 0);

    // Removing the key emits the same empty payload, but hasValue() now says
    // the key is gone and removed() names it.
    store->remove(QStringLiteral("k"));
    QCOMPARE(notifications.size(), 3);
    QVERIFY(!notifications.constLast().value.isValid());
    QVERIFY(!store->hasValue(QStringLiteral("k")));
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.at(0).at(0).toString(), QStringLiteral("k"));

    // Storing an invalid value again makes it present once more, while the
    // removed() count is unchanged: hasValue() is the discriminator.
    QVERIFY(store->setValue(QStringLiteral("k"), QVariant()));
    QVERIFY(store->hasValue(QStringLiteral("k")));
    QCOMPARE(removed.count(), 1);
}

void TestStore::emptyKeyIsRejected()
{
    md::Store* store = md::Store::open(QStringLiteral("test.empty-key"));
    store->clear();

    QSignalSpy changed(store, &md::Store::valueChanged);
    QSignalSpy any(store, &md::Store::changed);

    QVERIFY(!store->setValue(QString(), 1));
    QVERIFY(!store->hasValue(QString()));
    QVERIFY(store->values().isEmpty());
    QCOMPARE(store->size(), 0);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(any.count(), 0);

    // A non-empty key still works, so the rejection is specific to the key.
    QVERIFY(store->setValue(QStringLiteral("ok"), 1));
    QCOMPARE(store->size(), 1);
}

void TestStore::typeChangeIsAnOrdinaryChange()
{
    md::Store* store = md::Store::open(QStringLiteral("test.types"));
    store->clear();

    QVERIFY(store->setValue(QStringLiteral("typed"), 1));
    QVERIFY(store->value(QStringLiteral("typed")).metaType() == QMetaType::fromType<int>());

    QSignalSpy changed(store, &md::Store::valueChanged);

    // Same text, different metatype: this is a change, not a redundant write.
    QVERIFY(store->setValue(QStringLiteral("typed"), QStringLiteral("1")));
    QCOMPARE(changed.count(), 1);
    QVERIFY(store->value(QStringLiteral("typed")).metaType() == QMetaType::fromType<QString>());

    // Equal value and equal metatype is the no-op path.
    QVERIFY(!store->setValue(QStringLiteral("typed"), QStringLiteral("1")));
    QCOMPARE(changed.count(), 1);
}

void TestStore::closingDestroysTheStore()
{
    const QString id = QStringLiteral("test.close");
    md::Store* store = md::Store::open(id);
    store->setValue(QStringLiteral("key"), 1);

    QPointer<md::Store> guard(store);

    QVERIFY(md::Store::close(id));
    QVERIFY(guard.isNull());
    QCOMPARE(md::Store::find(id), nullptr);
    QVERIFY(!md::Store::isOpen(id));
    QVERIFY(!md::Store::close(id));
    QVERIFY(!md::Store::close(QString()));

    // A fresh open() constructs a new, empty store rather than resurrecting
    // the closed object or its values.
    md::Store* reopened = md::Store::open(id);
    QVERIFY(reopened != nullptr);
    QCOMPARE(md::Store::find(id), reopened);
    QVERIFY(!reopened->hasValue(QStringLiteral("key")));
    QCOMPARE(reopened->size(), 0);
    QVERIFY(md::Store::isOpen(id));
}

void TestStore::throwingObserverDoesNotBreakSetValue()
{
    const QString id = QStringLiteral("test.throwing");
    md::Store* store = md::Store::open(id);
    store->clear();

    bool threw = false;
    QObject context;
    QObject::connect(store, &md::Store::valueChanged, &context,
                     [&threw](const QString& key, const QVariant&) {
                         if (key != QStringLiteral("boom"))
                             return;
                         threw = true;
                         throw std::runtime_error("observer");
                     });

    // The exception must not escape setValue(), and the write must still have
    // happened and left the store usable.
    QVERIFY(store->setValue(QStringLiteral("boom"), 7));
    QVERIFY(threw);
    QVERIFY(store->hasValue(QStringLiteral("boom")));
    QCOMPARE(store->value(QStringLiteral("boom")).toInt(), 7);

    QSignalSpy changed(store, &md::Store::changed);
    QVERIFY(store->setValue(QStringLiteral("after"), 2));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(store->value(QStringLiteral("after")).toInt(), 2);

    // A caught exception must not leave the store looking "mid-notification";
    // close() therefore destroys it synchronously.
    QPointer<md::Store> guard(store);
    QVERIFY(md::Store::close(id));
    QVERIFY(guard.isNull());
}

// --- LRU capacity ------------------------------------------------------
// The value map is a bounded LRU cache: a write into a full store drops the
// oldest entry and reports it through evicted(), while an explicit
// remove()/clear() never does.

void TestStore::capacityDefaultsTo1024()
{
    md::Store* store = md::Store::open(QStringLiteral("test.lru.default"));
    QCOMPARE(store->capacity(), 1024);
    QCOMPARE(store->size(), 0);
}

void TestStore::evictionDropsTheOldestEntries()
{
    md::Store* store = md::Store::open(QStringLiteral("test.lru.evict"));
    store->clear();
    store->setCapacity(3);
    QCOMPARE(store->capacity(), 3);

    QSignalSpy evicted(store, &md::Store::evicted);
    const QStringList writes{QStringLiteral("a"), QStringLiteral("b"),
                             QStringLiteral("c"), QStringLiteral("d"),
                             QStringLiteral("e")};
    for (int i = 0; i < writes.size(); ++i)
        QVERIFY(store->setValue(writes.at(i), i));

    // Five writes into three slots drop exactly the two oldest entries.
    QCOMPARE(store->size(), 3);
    QCOMPARE(evicted.count(), 2);
    QCOMPARE(evicted.at(0).at(0).toString(), QStringLiteral("a"));
    QCOMPARE(evicted.at(1).at(0).toString(), QStringLiteral("b"));
    QVERIFY(!store->hasValue(QStringLiteral("a")));
    QVERIFY(!store->hasValue(QStringLiteral("b")));
    QVERIFY(store->hasValue(QStringLiteral("c")));
    QCOMPARE(store->keys(), (QStringList{QStringLiteral("c"), QStringLiteral("d"),
                                         QStringLiteral("e")}));
}

void TestStore::readPromotesTheKey()
{
    md::Store* store = md::Store::open(QStringLiteral("test.lru.promote"));
    store->clear();
    store->setCapacity(3);
    QVERIFY(store->setValue(QStringLiteral("a"), 1));
    QVERIFY(store->setValue(QStringLiteral("b"), 2));
    QVERIFY(store->setValue(QStringLiteral("c"), 3));
    QCOMPARE(store->keys(), (QStringList{QStringLiteral("a"), QStringLiteral("b"),
                                         QStringLiteral("c")}));

    // A hit through value() promotes "a" to newest, even though the read is
    // const...
    QCOMPARE(store->value(QStringLiteral("a")).toInt(), 1);
    QCOMPARE(store->keys(), (QStringList{QStringLiteral("b"), QStringLiteral("c"),
                                         QStringLiteral("a")}));
    // ...while a miss changes nothing.
    QCOMPARE(store->value(QStringLiteral("missing"), -1).toInt(), -1);
    QCOMPARE(store->keys(), (QStringList{QStringLiteral("b"), QStringLiteral("c"),
                                         QStringLiteral("a")}));

    // The next insert therefore evicts "b", not the promoted "a".
    QSignalSpy evicted(store, &md::Store::evicted);
    QVERIFY(store->setValue(QStringLiteral("d"), 4));
    QCOMPARE(evicted.count(), 1);
    QCOMPARE(evicted.at(0).at(0).toString(), QStringLiteral("b"));
    QVERIFY(store->hasValue(QStringLiteral("a")));
    QVERIFY(!store->hasValue(QStringLiteral("b")));
    QCOMPARE(store->keys(), (QStringList{QStringLiteral("c"), QStringLiteral("a"),
                                         QStringLiteral("d")}));
}

void TestStore::zeroCapacityIsUnbounded()
{
    md::Store* store = md::Store::open(QStringLiteral("test.lru.unbounded"));
    store->clear();
    store->setCapacity(0);
    QCOMPARE(store->capacity(), 0);

    QSignalSpy evicted(store, &md::Store::evicted);
    QStringList expected;
    for (int i = 0; i < 5; ++i) {
        const QString key = QStringLiteral("key.") + QString::number(i);
        expected.append(key);
        QVERIFY(store->setValue(key, i));
    }

    QCOMPARE(store->size(), 5);
    QCOMPARE(evicted.count(), 0);
    QCOMPARE(store->keys(), expected);

    // A negative bound is not meaningful and is treated as unbounded.
    store->setCapacity(-1);
    QCOMPARE(store->capacity(), 0);
    QCOMPARE(store->size(), 5);
    QCOMPARE(evicted.count(), 0);
}

void TestStore::shrinkingCapacityEvictsAndNotifies()
{
    md::Store* store = md::Store::open(QStringLiteral("test.lru.shrink"));
    store->clear();
    store->setCapacity(0);
    const QStringList writes{QStringLiteral("a"), QStringLiteral("b"),
                             QStringLiteral("c"), QStringLiteral("d"),
                             QStringLiteral("e")};
    for (int i = 0; i < writes.size(); ++i)
        QVERIFY(store->setValue(writes.at(i), i));

    QSignalSpy evicted(store, &md::Store::evicted);
    store->setCapacity(2);

    // The three least-recently-used entries go, oldest first.
    QCOMPARE(store->capacity(), 2);
    QCOMPARE(store->size(), 2);
    QCOMPARE(evicted.count(), 3);
    QCOMPARE(evicted.at(0).at(0).toString(), QStringLiteral("a"));
    QCOMPARE(evicted.at(1).at(0).toString(), QStringLiteral("b"));
    QCOMPARE(evicted.at(2).at(0).toString(), QStringLiteral("c"));
    QCOMPARE(store->keys(), (QStringList{QStringLiteral("d"), QStringLiteral("e")}));
    QVERIFY(!store->hasValue(QStringLiteral("a")));

    // Growing back neither resurrects the victims nor emits anything.
    store->setCapacity(8);
    QCOMPARE(store->capacity(), 8);
    QCOMPARE(store->size(), 2);
    QCOMPARE(evicted.count(), 3);
    QCOMPARE(store->keys(), (QStringList{QStringLiteral("d"), QStringLiteral("e")}));
}

void TestStore::removeAndClearDoNotEvict()
{
    md::Store* store = md::Store::open(QStringLiteral("test.lru.explicit"));
    store->clear();
    store->setCapacity(2);
    QVERIFY(store->setValue(QStringLiteral("a"), 1));
    QVERIFY(store->setValue(QStringLiteral("b"), 2));

    QSignalSpy evicted(store, &md::Store::evicted);
    store->remove(QStringLiteral("a"));
    QCOMPARE(evicted.count(), 0);

    // Filling the freed slot is not an eviction either.
    QVERIFY(store->setValue(QStringLiteral("c"), 3));
    QCOMPARE(store->size(), 2);
    QCOMPARE(evicted.count(), 0);

    store->clear();
    QCOMPARE(store->size(), 0);
    QCOMPARE(evicted.count(), 0);

    // Neither is destroying the store with close().
    QVERIFY(md::Store::close(QStringLiteral("test.lru.explicit")));
    QCOMPARE(evicted.count(), 0);
}

void TestStore::keysFollowRecencyOrder()
{
    md::Store* store = md::Store::open(QStringLiteral("test.lru.keys"));
    store->clear();
    store->setCapacity(0);
    QVERIFY(store->setValue(QStringLiteral("a"), 1));
    QVERIFY(store->setValue(QStringLiteral("b"), 2));
    QVERIFY(store->setValue(QStringLiteral("c"), 3));
    QCOMPARE(store->keys(), (QStringList{QStringLiteral("a"), QStringLiteral("b"),
                                         QStringLiteral("c")}));

    // A changed write promotes the existing key.
    QVERIFY(store->setValue(QStringLiteral("a"), 10));
    QCOMPARE(store->keys(), (QStringList{QStringLiteral("b"), QStringLiteral("c"),
                                         QStringLiteral("a")}));

    // The const probes do not promote.
    QVERIFY(store->hasValue(QStringLiteral("b")));
    QVERIFY(store->values().contains(QStringLiteral("b")));
    QCOMPARE(store->keys(), (QStringList{QStringLiteral("b"), QStringLiteral("c"),
                                         QStringLiteral("a")}));

    // value() does, and keys() itself is a pure read.
    QCOMPARE(store->value(QStringLiteral("b")).toInt(), 2);
    QCOMPARE(store->keys(), (QStringList{QStringLiteral("c"), QStringLiteral("a"),
                                         QStringLiteral("b")}));
    QCOMPARE(store->keys(), (QStringList{QStringLiteral("c"), QStringLiteral("a"),
                                         QStringLiteral("b")}));

    store->clear();
    QVERIFY(store->keys().isEmpty());
}

void TestStore::redundantWriteDoesNotPromote()
{
    md::Store* store = md::Store::open(QStringLiteral("test.lru.redundant"));
    store->clear();
    store->setCapacity(2);
    QVERIFY(store->setValue(QStringLiteral("a"), 1));
    QVERIFY(store->setValue(QStringLiteral("b"), 2));
    QCOMPARE(store->keys(), (QStringList{QStringLiteral("a"), QStringLiteral("b")}));

    // The equal-value write is a no-op: "a" stays the oldest entry.
    QVERIFY(!store->setValue(QStringLiteral("a"), 1));
    QCOMPARE(store->keys(), (QStringList{QStringLiteral("a"), QStringLiteral("b")}));

    QSignalSpy evicted(store, &md::Store::evicted);
    QVERIFY(store->setValue(QStringLiteral("c"), 3));
    QCOMPARE(evicted.count(), 1);
    QCOMPARE(evicted.at(0).at(0).toString(), QStringLiteral("a"));
    QCOMPARE(store->keys(), (QStringList{QStringLiteral("b"), QStringLiteral("c")}));
}

void TestStore::throwingEvictedObserverIsIsolated()
{
    md::Store* store = md::Store::open(QStringLiteral("test.lru.throwing"));
    store->clear();
    store->setCapacity(2);
    QVERIFY(store->setValue(QStringLiteral("a"), 1));
    QVERIFY(store->setValue(QStringLiteral("b"), 2));

    bool threw = false;
    QObject context;
    QObject::connect(store, &md::Store::evicted, &context,
                     [&threw](const QString& key) {
                         if (key != QStringLiteral("a"))
                             return;
                         threw = true;
                         throw std::runtime_error("evicted observer");
                     });

    // The exception must not escape the capacity policy, and the write must
    // still have happened and left the store usable.
    QVERIFY(store->setValue(QStringLiteral("c"), 3));
    QVERIFY(threw);
    QVERIFY(!store->hasValue(QStringLiteral("a")));
    QVERIFY(store->hasValue(QStringLiteral("c")));
    QCOMPARE(store->size(), 2);

    QSignalSpy changed(store, &md::Store::changed);
    QVERIFY(store->setValue(QStringLiteral("d"), 4));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(store->keys(), (QStringList{QStringLiteral("c"), QStringLiteral("d")}));
    QVERIFY(md::Store::close(QStringLiteral("test.lru.throwing")));
}

QTEST_MAIN(TestStore)
#include "tst_store.moc"
