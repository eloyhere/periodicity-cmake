// Periodicity — precondition-violation tests for the structure module.
//
// A few operations document a precondition and defend it twice: a debug build
// asserts, and a release build rejects the operation and leaves the structure
// untouched. Asserting is the right debug behaviour, but it aborts the test
// process, so this suite disables the standard assertion before instantiating
// the headers and checks the documented release-mode result instead. That is
// the behaviour a shipped binary relies on: a rejected call returns nullptr
// without corrupting the structure.
//
// Q_ASSERT is controlled by QT_NO_DEBUG and is unaffected by NDEBUG, so the
// Qt test framework itself is still compiled with its usual checks.

#include <QtTest/QtTest>

#include <memory>
#include <utility>

#ifndef NDEBUG
#define NDEBUG 1
#endif

#include <md/structure/forest/sibling_forest.h>
#include <md/structure/tree/binary_plus_tree.h>
#include <md/structure/tree/multi_tree.h>

using md::structure::BinaryPlusNode;
using md::structure::BinaryPlusTree;
using md::structure::MultiTree;
using md::structure::SiblingForest;

class TestContracts : public QObject
{
    Q_OBJECT

private slots:
    void binaryPlusTreeRejectsAncestorGraft();
    void siblingForestRejectsForeignSubtree();
    void multiTreeRejectsNullParent();
};

void TestContracts::binaryPlusTreeRejectsAncestorGraft()
{
    // Releasing the root keeps the subtree alive through raw pointers, so the
    // released root can be offered as the child of one of its own descendants.
    // Grafting it would close the parent chain into a cycle; the guard must
    // reject the graft before touching the tree.
    BinaryPlusTree<int> tree;
    auto& root = tree.insertRoot(10);
    auto& child = tree.insertRight(&root, 20);
    tree.insertLeft(&child, 15);

    std::unique_ptr<BinaryPlusNode<int>> released = tree.releaseRoot();
    QVERIFY(tree.empty());
    QCOMPARE(tree.size(), std::size_t(0));

    // `child` lives inside the released subtree, so `released` is one of its
    // ancestors — the exact cycle the ancestor walk exists to catch.
    QVERIFY(tree.setChild(&child, true, std::move(released)) == nullptr);
    QVERIFY(tree.empty());
    QCOMPARE(tree.size(), std::size_t(0));

    // The null-parent guard is a recoverable rejection too.
    QVERIFY(tree.setChild(nullptr, true, nullptr) == nullptr);

    // The tree still accepts an ordinary graft after the rejection.
    auto& freshRoot = tree.insertRoot(1);
    auto graft = std::make_unique<BinaryPlusNode<int>>(2);
    BinaryPlusNode<int>* graftRaw = graft.get();
    QVERIFY(tree.setChild(&freshRoot, true, std::move(graft)) == nullptr);
    QVERIFY(freshRoot.right.get() == graftRaw);
    QVERIFY(graftRaw->parent == &freshRoot);
    QCOMPARE(tree.size(), std::size_t(2));
    QCOMPARE(tree.height(), 2);
}

void TestContracts::siblingForestRejectsForeignSubtree()
{
    SiblingForest<int> forest;
    auto& root = forest.addRoot(1);
    auto& child = forest.appendChild(&root, 2);

    SiblingForest<int> other;
    auto& foreignRoot = other.addRoot(9);
    auto& foreignChild = other.appendChild(&foreignRoot, 10);

    // A node from another forest is rejected before any link is touched.
    QVERIFY(forest.removeSubtree(&foreignChild) == nullptr);
    QCOMPARE(forest.size(), std::size_t(2));
    QVERIFY(forest.root(0) == &root);
    QVERIFY(forest.root(0)->firstChild.get() == &child);
    QCOMPARE(other.size(), std::size_t(2));
    QVERIFY(foreignChild.parent == &foreignRoot);

    // A subtree that is already detached is no longer part of this forest
    // either, so asking again is a no-op rather than a second unlink.
    auto removed = forest.removeSubtree(&root);
    QVERIFY(removed != nullptr);
    QVERIFY(forest.empty());
    QVERIFY(forest.removeSubtree(removed.get()) == nullptr);
    QCOMPARE(removed->value, 1);
}

void TestContracts::multiTreeRejectsNullParent()
{
    MultiTree<int, 2> tree;
    QVERIFY(tree.appendChild(nullptr, 1) == nullptr);
    QVERIFY(tree.empty());
    QCOMPARE(tree.size(), std::size_t(0));
}

QTEST_APPLESS_MAIN(TestContracts)
#include "tst_contracts.moc"
