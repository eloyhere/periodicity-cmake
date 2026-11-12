// Periodicity — typed object pool over BlockAllocator (structure).
//
// ObjectPool<T> gives BlockAllocator a lifetime: create() placement-constructs
// a T in a raw block, destroy() runs ~T() and then returns the block to the
// allocator's free list. Storage is recycled; object lifetime is not, so a
// recycled block never carries state from its previous tenant.
//
//     create(args...)   O(1) amortised (one chunk allocation per
//                       `blocksPerChunk` objects), plus T's constructor
//     destroy(pointer)  O(1), plus T's destructor
//
// The pool owns no bookkeeping for live objects: it cannot destroy them for
// you. Destroy everything before the pool is destroyed (the destructor asserts
// that nothing is live). Consequently clear() is only meaningful when inUse()
// is zero; it releases the chunks.
//
// Pointer stability: a block is never moved once a chunk is allocated, so the
// T* returned by create() stays valid until destroy() is called for it. The
// pool is movable, and a move keeps those pointers valid because the chunk
// buffers themselves are moved, not reallocated.
//
// Like every structure-layer container this is single-threaded and does not
// throw on the happy path. A throwing T constructor propagates; the slot is
// returned to the pool before the exception leaves create().

#pragma once

#include <cassert>
#include <cstddef>
#include <new>
#include <utility>

#include <md/structure/allocator/block_allocator.h>

namespace md::structure {

template <class T>
class ObjectPool
{
    static_assert(alignof(T) <= alignof(std::max_align_t),
                  "ObjectPool blocks are aligned for fundamental alignment only; "
                  "over-aligned types need a different allocator");

public:
    using value_type = T;
    using size_type = std::size_t;

    // `blocksPerChunk` is the allocation granularity, not a capacity. Use
    // reserve() to warm the pool.
    explicit ObjectPool(size_type blocksPerChunk = 64)
        : allocator_(sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T),
                     blocksPerChunk)
    {
    }

    ~ObjectPool()
    {
        assert(allocator_.inUse() == 0
               && "ObjectPool destroyed while live objects remain");
    }

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    // Moving keeps all previously returned T* addresses valid. The destination
    // must not hold live objects (its chunks are released without running
    // destructors).
    ObjectPool(ObjectPool&& other) noexcept = default;

    ObjectPool& operator=(ObjectPool&& other) noexcept
    {
        assert(allocator_.inUse() == 0
               && "move-assigning over an ObjectPool with live objects");
        allocator_ = std::move(other.allocator_);
        return *this;
    }

    // ---- Lifetime ---------------------------------------------------------

    template <class... Args>
    T* create(Args&&... args)
    {
        void* slot = allocator_.allocate();
        assert(slot != nullptr);
        if (slot == nullptr)
            return nullptr;

        try {
            return ::new (slot) T(std::forward<Args>(args)...);
        } catch (...) {
            // The block is reusable; the failed object never existed.
            allocator_.deallocate(slot);
            throw;
        }
    }

    // Runs ~T(). A null pointer is ignored; a pointer that this pool does not
    // own is ignored in release builds after a debug assertion.
    void destroy(T* object) noexcept
    {
        if (object == nullptr)
            return;

        assert(allocator_.owns(object) && "destroy() got a foreign pointer");
        if (!allocator_.owns(object))
            return;

        object->~T();
        allocator_.deallocate(object);
    }

    // ---- Capacity ---------------------------------------------------------

    // Pre-allocates enough chunks for `objects` live objects. Existing free
    // blocks are reused first, so this never shrinks.
    void reserve(size_type objects) { allocator_.reserve(objects); }

    // Blocks currently owned by the pool.
    [[nodiscard]] size_type capacity() const noexcept { return allocator_.capacity(); }

    // Live objects (create()d and not yet destroyed).
    [[nodiscard]] size_type inUse() const noexcept { return allocator_.inUse(); }

    // Free blocks on the allocator's free list (capacity - inUse).
    [[nodiscard]] size_type pooled() const noexcept { return allocator_.pooled(); }

    [[nodiscard]] size_type chunkCount() const noexcept
    {
        return allocator_.chunkCount();
    }

    [[nodiscard]] size_type blocksPerChunk() const noexcept
    {
        return allocator_.blocksPerChunk();
    }

    // Whether `object` points into one of this pool's chunks. A foreign pointer
    // can be rejected before destroy() so a debug assertion is not needed.
    [[nodiscard]] bool owns(const T* object) const noexcept
    {
        return allocator_.owns(object);
    }

    // ---- Teardown ---------------------------------------------------------

    // Releases every chunk. Precondition: no live object (asserted).
    void clear()
    {
        assert(allocator_.inUse() == 0 && "clear() with live objects");
        allocator_.clear();
    }

    // Releases chunks that never handed out a block; with no live object it
    // releases everything.
    void shrinkToFit() { allocator_.shrinkToFit(); }

private:
    BlockAllocator allocator_;
};

} // namespace md::structure
