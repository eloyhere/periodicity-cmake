// Store: the process-wide registry and value notifications.

#include <md/store/store.h>

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

class TestStore : public QObject
{
    Q_OBJECT

private slots:
    void openIsStable();
    void differentIdsAreDifferentStores();
    void valuesEmitOnlyOnChange();
    void fallbackAndRemoval();
    void registryListsOpenStores();
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

    store->setValue(QStringLiteral("family"), QStringLiteral("Georgia"));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(any.count(), 1);
    QCOMPARE(changed.at(0).at(0).toString(), QStringLiteral("family"));
    QCOMPARE(store->value(QStringLiteral("family")).toString(), QStringLiteral("Georgia"));

    store->setValue(QStringLiteral("family"), QStringLiteral("Georgia"));
    QCOMPARE(changed.count(), 1);

    store->setValue(QStringLiteral("family"), QStringLiteral("Courier New"));
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

QTEST_MAIN(TestStore)
#include "tst_store.moc"
