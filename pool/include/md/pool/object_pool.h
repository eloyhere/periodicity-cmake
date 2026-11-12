// Periodicity — object pool with object lifetime (pool).
//
// A typed pool that recycles storage while giving every user a *fresh object*.
// The distinction from BlockPool is the whole point of this class:
//
//   BlockPool        hands out raw bytes; nothing is constructed or destroyed
//   ObjectPool<T>    starts T's lifetime on create(), ends it on destroy()
//
// So a block is recycled, but an object's lifetime is not: create() runs a
// constructor in the block and destroy() runs the destructor before the block
// goes back on the free list. No state can leak from one user of a block to the
// next, and a T that owns resources (a pixmap, a vector, a connection) releases
// them on destroy() exactly as it would on `delete`. What the pool saves is the
// allocation, not the lifetime.
//
// The three ways to hold an object, in increasing order of safety:
//
//   T* raw = pool.create(args...);   ...; pool.destroy(raw);
//   auto handle = pool.createHandle(args...);   // destroys itself at scope exit
//   pool.reserve(n);                            // warm the pool, no lifetime
//
// Objects must not outlive the pool: the destructor asserts that nothing is
// still live, because the storage is about to be released. A pool is therefore
// a long-lived member of whatever owns it (a view, a recycler, a scheduler),
// not a temporary.
//
// Costs, per operation:
//
//   create    O(1) amortised; one chunk allocation per `chunkBlocks` objects,
//             and nullptr only once `maxBlocks` is reached
//   destroy   O(1) plus T's destructor
//
// The pool is not thread-safe. It is meant for the GUI thread, like the rest of
// the module.

#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include <md/pool/block_pool.h>

namespace md::pool {

template <class T>
class ObjectPool
{
    static_assert(std::is_object_v<T>, "ObjectPool needs an object type");
    static_assert(!std::is_array_v<T>, "ObjectPool holds single objects");

public:
    using value_type = T;
    using size_type = std::size_t;

    struct Options
    {
        // Objects per chunk. The pool allocates in chunks, never per object.
        size_type chunkBlocks = 64;

        // Hard cap on the number of objects the pool may hold at once; 0 means
        // unbounded. A capped pool makes create() return nullptr when full,
        // which lets a caller impose backpressure instead of allocating.
        size_type maxBlocks = 0;

        // Blocks allocated up front, so the first create() does not allocate.
        size_type initialBlocks = 0;
    };

    explicit ObjectPool(Options options = Options{})
        : blocks_(BlockPool::Options{
              .blockSize = sizeof(T),
              .blockAlignment = alignof(T),
              .chunkBlocks = options.chunkBlocks,
              .maxBlocks = options.maxBlocks})
    {
        if (options.initialBlocks != 0)
            blocks_.reserve(options.initialBlocks);
    }

    ~ObjectPool()
    {
        // Every object must have been destroyed: the storage is released here,
        // so a live object would be left pointing at freed memory.
        assert(blocks_.inUse() == 0 && "objects must be destroyed before the pool");
    }

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) = delete;
    ObjectPool& operator=(ObjectPool&&) = delete;

    // ---- Lifetime ---------------------------------------------------------

    // Starts T's lifetime in a pooled block and returns the new object. The
    // constructor runs fresh, so no state survives from the block's previous
    // user. Returns nullptr when a capped pool is full.
    //
    // If T's constructor throws, the block is returned to the pool before the
    // exception propagates.
    template <class... Args>
    [[nodiscard]] T* create(Args&&... args)
    {
        void* block = blocks_.acquire();
        if (block == nullptr)
            return nullptr;

        try {
            return ::new (block) T(std::forward<Args>(args)...);
        } catch (...) {
            blocks_.release(block);
            throw;
        }
    }

    // Ends T's lifetime — the destructor runs — and returns the block. Passing
    // nullptr is a no-op, so destroy() composes with create() returning nullptr.
    //
    // Destroying an object twice, or one that belongs to another pool, is
    // refused: nothing is destroyed and nothing is linked, so a release build
    // cannot corrupt the free list. rejectedReleases() counts the violation;
    // a correct program never sees it.
    void destroy(T* object) noexcept
    {
        if (object == nullptr)
            return;

        if (!blocks_.isLive(object)) {
            // Hand the refusal to the block layer: release() is the single
            // authority that rejects a block which is not live (or not ours)
            // and records it in rejectedReleases(). It does not touch the
            // object, so a foreign pointer cannot have T's destructor run on it.
            blocks_.release(object);
            return;
        }

        std::destroy_at(object);
        blocks_.release(object);
    }

    // A handle owns one object's lifetime: it destroys the object when it is
    // reset or goes out of scope, which is what keeps the pool's contract —
    // nothing live when the pool dies — satisfied by construction. It is
    // move-only, so ownership is never duplicated.
    class Handle
    {
    public:
        Handle() noexcept = default;

        ~Handle() { reset(); }

        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;

        Handle(Handle&& other) noexcept
            : pool_(other.pool_), object_(other.object_)
        {
            other.pool_ = nullptr;
            other.object_ = nullptr;
        }

        Handle& operator=(Handle&& other) noexcept
        {
            if (this != &other) {
                reset();
                pool_ = other.pool_;
                object_ = other.object_;
                other.pool_ = nullptr;
                other.object_ = nullptr;
            }
            return *this;
        }

        [[nodiscard]] T* get() const noexcept { return object_; }
        [[nodiscard]] T& operator*() const noexcept { return *object_; }
        [[nodiscard]] T* operator->() const noexcept { return object_; }
        [[nodiscard]] explicit operator bool() const noexcept { return object_ != nullptr; }

        // Ends the object's lifetime now and detaches the handle.
        void reset() noexcept
        {
            if (pool_ != nullptr && object_ != nullptr)
                pool_->destroy(object_);
            pool_ = nullptr;
            object_ = nullptr;
        }

        // Gives up ownership of a still-live object, which the caller must then
        // destroy() itself. The pool is unchanged.
        [[nodiscard]] T* release() noexcept
        {
            T* object = object_;
            pool_ = nullptr;
            object_ = nullptr;
            return object;
        }

    private:
        // Only the pool may hand out a non-empty handle; a caller cannot forge
        // one and end up destroying an object twice.
        friend class ObjectPool<T>;

        Handle(ObjectPool* pool, T* object) noexcept : pool_(pool), object_(object) {}

        ObjectPool* pool_ = nullptr;
        T* object_ = nullptr;
    };

    // Factory, defined after the handle because it returns one.
    template <class... Args>
    [[nodiscard]] Handle createHandle(Args&&... args)
    {
        T* object = create(std::forward<Args>(args)...);
        if (object == nullptr)
            return Handle{};
        return Handle(this, object);
    }

    // ---- Introspection ----------------------------------------------------

    // True when `object` is one of the pool's blocks, live or free. Used by the
    // debug assertions; also useful when a caller keeps objects from several
    // pools and has to find their owner.
    [[nodiscard]] bool owns(const T* object) const noexcept { return blocks_.owns(object); }

    // Blocks reserved so far; this is the most objects the pool can hold
    // without another chunk allocation.
    [[nodiscard]] size_type capacity() const noexcept { return blocks_.capacity(); }

    // Blocks currently on the free list.
    [[nodiscard]] size_type available() const noexcept { return blocks_.available(); }

    // Releases refused because the object was foreign or already destroyed.
    [[nodiscard]] size_type rejectedReleases() const noexcept
    {
        return blocks_.rejectedReleases();
    }

    // Objects whose lifetime is in progress.
    [[nodiscard]] size_type liveCount() const noexcept { return blocks_.inUse(); }

    [[nodiscard]] bool empty() const noexcept { return blocks_.inUse() == 0; }

    // Largest number of simultaneously live objects so far: the figure to size
    // chunkBlocks/maxBlocks from once a workload is measured.
    [[nodiscard]] size_type highWaterMark() const noexcept { return blocks_.highWaterMark(); }

    [[nodiscard]] size_type chunkCount() const noexcept { return blocks_.chunkCount(); }

    // ---- Bulk -------------------------------------------------------------

    // Pre-allocates storage for `objects` objects without creating any.
    void reserve(size_type objects) { blocks_.reserve(objects); }

    // Releases every chunk. Precondition: no object is live, because their
    // storage is about to be freed.
    void clear() noexcept { blocks_.clear(); }

private:
    BlockPool blocks_;
};

// An object whose lifetime ends with the handle; the spelling most call sites
// want, because it cannot be forgotten.
template <class T>
using PooledObject = typename ObjectPool<T>::Handle;

} // namespace md::pool
