// Periodicity — fixed-size block pool (pool).
//
// The engine under the object pools: an arena that hands out blocks of one
// fixed size and takes them back, without ever calling the allocator again once
// a chunk has been carved. It knows nothing about types, constructors or
// destructors — that is ObjectPool's job — so the same class serves a typed
// object pool and the size-classed allocator behind PoolResource.
//
// Design:
//
//   chunks     memory is reserved in chunks of `chunkBlocks` blocks; a chunk is
//              one allocation, and a pool that never exceeds its high-water
//              mark performs one allocation per chunk in total;
//   free list  free blocks are threaded through their own storage (the first
//              `sizeof(void*)` bytes of a free block hold the next pointer), so
//              the free list costs no memory of its own;
//   ownership  one bit per block records what is checked out, so a second
//              release of the same block — or a release of a block this pool
//              does not own — is refused instead of linking the block into the
//              free list twice. The check is part of the hot path in every build
//              because the corruption it prevents is a release-build bug, not a
//              debug-only one;
//   reuse      LIFO, so a block that was just released is the first one handed
//              out again — the best case for the cache and for the branch
//              predictor;
//   bounds     `maxBlocks` caps the pool; acquire() then returns nullptr
//              instead of growing. Zero means unbounded.
//
// Blocks are aligned to `blockAlignment`, which must be at least the alignment
// of a pointer (the free-list link is stored in the block). Over-aligned
// requests are honoured through the aligned operator new overloads.
//
// The pool owns its chunks; callers own nothing. A block stays valid until it
// is released or the pool is cleared or destroyed, which is why clear() and the
// destructor require that nothing is checked out.

#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>
#include <vector>

namespace md::pool {

class BlockPool
{
public:
    using size_type = std::size_t;

    struct Options
    {
        // Bytes per block. Raised to at least sizeof(void*) because a free
        // block stores the free-list link in place.
        size_type blockSize = 64;

        // Alignment of every block; chunk storage is allocated with it.
        size_type blockAlignment = alignof(std::max_align_t);

        // Blocks per chunk. One allocation covers this many blocks.
        size_type chunkBlocks = 64;

        // Hard cap on blocks; 0 means "grow as needed".
        size_type maxBlocks = 0;
    };

    explicit BlockPool(Options options = Options{})
        : options_(sanitise(options)),
          stride_(roundUp((std::max)(options_.blockSize, sizeof(void*)),
                          options_.blockAlignment))
    {
    }

    ~BlockPool()
    {
        for (const Chunk& chunk : chunks_)
            freeBytes(chunk.data, options_.blockAlignment);
    }

    BlockPool(const BlockPool&) = delete;
    BlockPool& operator=(const BlockPool&) = delete;

    BlockPool(BlockPool&& other) noexcept
        : options_(other.options_), stride_(other.stride_), chunks_(std::move(other.chunks_)),
          freeHead_(other.freeHead_), capacity_(other.capacity_), inUse_(other.inUse_),
          highWaterMark_(other.highWaterMark_), rejectedReleases_(other.rejectedReleases_)
    {
        other.freeHead_ = nullptr;
        other.capacity_ = 0;
        other.inUse_ = 0;
        other.highWaterMark_ = 0;
        other.rejectedReleases_ = 0;
    }

    BlockPool& operator=(BlockPool&& other) noexcept
    {
        if (this != &other) {
            for (const Chunk& chunk : chunks_)
                freeBytes(chunk.data, options_.blockAlignment);

            options_ = other.options_;
            stride_ = other.stride_;
            chunks_ = std::move(other.chunks_);
            freeHead_ = other.freeHead_;
            capacity_ = other.capacity_;
            inUse_ = other.inUse_;
            highWaterMark_ = other.highWaterMark_;
            rejectedReleases_ = other.rejectedReleases_;

            other.freeHead_ = nullptr;
            other.capacity_ = 0;
            other.inUse_ = 0;
            other.highWaterMark_ = 0;
            other.rejectedReleases_ = 0;
        }
        return *this;
    }

    // ---- Acquire and release ---------------------------------------------

    // One block of blockSize() bytes, or nullptr when a bounded pool is full.
    // Throws std::bad_alloc when the system allocator fails.
    [[nodiscard]] void* acquire()
    {
        if (freeHead_ == nullptr)
            addChunk();
        if (freeHead_ == nullptr)
            return nullptr;

        void* block = freeHead_;
        size_type index = 0;
        Chunk* chunk = findChunk(block, &index);
        assert(chunk != nullptr && "the free list references a foreign block");

        freeHead_ = *static_cast<void**>(block);
        if (chunk != nullptr)
            mark(*chunk, index, true);

        ++inUse_;
        if (inUse_ > highWaterMark_)
            highWaterMark_ = inUse_;
        return block;
    }

    // Returns a block to the pool. The block must come from this pool and must
    // not already be free; a release that violates either rule is refused and
    // counted in rejectedReleases(). It is never linked into the free list, in
    // debug and release builds alike: a second link would hand the same storage
    // to two owners later.
    void release(void* block) noexcept
    {
        size_type index = 0;
        Chunk* chunk = findChunk(block, &index);
        if (chunk == nullptr || !isMarked(*chunk, index)) {
            ++rejectedReleases_;
            return;
        }

        mark(*chunk, index, false);
        *static_cast<void**>(block) = freeHead_;
        freeHead_ = block;
        assert(inUse_ > 0 && "the live map and the accounting disagree");
        --inUse_;
    }

    // ---- Capacity ---------------------------------------------------------

    [[nodiscard]] size_type blockSize() const noexcept { return options_.blockSize; }
    [[nodiscard]] size_type blockAlignment() const noexcept { return options_.blockAlignment; }

    // Blocks handed out so far. Grows by chunk, never shrinks until clear().
    [[nodiscard]] size_type capacity() const noexcept { return capacity_; }
    [[nodiscard]] size_type inUse() const noexcept { return inUse_; }
    [[nodiscard]] size_type available() const noexcept { return capacity_ - inUse_; }

    // Largest number of blocks that were checked out at the same time; the
    // figure to size a pool from once a workload is known.
    [[nodiscard]] size_type highWaterMark() const noexcept { return highWaterMark_; }

    // Releases refused because the block was foreign or already free. The free
    // list and the accounting are unchanged when this grows: the pool reports
    // the contract violation instead of corrupting itself.
    [[nodiscard]] size_type rejectedReleases() const noexcept { return rejectedReleases_; }

    [[nodiscard]] size_type chunkCount() const noexcept { return chunks_.size(); }

    // True when `block` is one of this pool's blocks (free or in use).
    [[nodiscard]] bool owns(const void* block) const noexcept
    {
        if (block == nullptr)
            return false;

        const auto address = reinterpret_cast<std::uintptr_t>(block);
        for (const Chunk& chunk : chunks_) {
            const auto begin = reinterpret_cast<std::uintptr_t>(chunk.data);
            const auto end = begin + static_cast<std::uintptr_t>(chunk.count) * stride_;
            if (address >= begin && address < end)
                return (address - begin) % stride_ == 0;
        }
        return false;
    }

    // True when `block` belongs to this pool and is currently checked out.
    [[nodiscard]] bool isLive(const void* block) const noexcept
    {
        size_type index = 0;
        const Chunk* chunk = findChunk(block, &index);
        return chunk != nullptr && isMarked(*chunk, index);
    }

    // ---- Bulk -------------------------------------------------------------

    // Grows the pool until it can hand out `blocks` blocks without allocating
    // again. Pre-warming a pool takes the allocation spikes out of the hot path.
    void reserve(size_type blocks)
    {
        while (capacity_ < blocks) {
            const size_type before = capacity_;
            addChunk();
            if (capacity_ == before)
                break; // bounded, and the cap has been reached
        }
    }

    // Frees every chunk. Precondition: nothing is checked out, because the
    // blocks would be freed under their users.
    void clear() noexcept
    {
        assert(inUse_ == 0 && "clear() requires every block to be released");
        for (const Chunk& chunk : chunks_)
            freeBytes(chunk.data, options_.blockAlignment);
        chunks_.clear();
        freeHead_ = nullptr;
        capacity_ = 0;
        inUse_ = 0;
        // highWaterMark_ is historical and deliberately survives clear().
    }

private:
    struct Chunk
    {
        std::byte* data = nullptr;
        size_type count = 0;

        // One bit per block, set while the block is checked out. The free list
        // alone cannot answer "is this block already free?" cheaply, and that
        // answer is what keeps a second release from corrupting the list in a
        // release build.
        std::vector<std::uint64_t> live;
    };

    [[nodiscard]] static constexpr size_type roundUp(size_type value,
                                                     size_type alignment) noexcept
    {
        return alignment <= 1 ? value : ((value + alignment - 1) / alignment) * alignment;
    }

    [[nodiscard]] static Options sanitise(Options options) noexcept
    {
        if (options.blockSize == 0)
            options.blockSize = sizeof(void*);
        if (options.blockAlignment < alignof(void*))
            options.blockAlignment = alignof(void*);
        if (options.chunkBlocks == 0)
            options.chunkBlocks = 1;
        return options;
    }

    [[nodiscard]] static void* allocateBytes(size_type bytes, size_type alignment)
    {
        if (alignment > alignof(std::max_align_t))
            return ::operator new(bytes, std::align_val_t{alignment});
        return ::operator new(bytes);
    }

    static void freeBytes(void* data, size_type alignment) noexcept
    {
        if (data == nullptr)
            return;
        if (alignment > alignof(std::max_align_t))
            ::operator delete(data, std::align_val_t{alignment});
        else
            ::operator delete(data);
    }

    void addChunk()
    {
        size_type count = options_.chunkBlocks;
        if (options_.maxBlocks != 0) {
            if (capacity_ >= options_.maxBlocks)
                return;
            count = (std::min)(count, options_.maxBlocks - capacity_);
        }
        if (count == 0)
            return;

        auto* data = static_cast<std::byte*>(allocateBytes(count * stride_,
                                                           options_.blockAlignment));
        try {
            Chunk chunk;
            chunk.data = data;
            chunk.count = count;
            chunk.live.assign((count + 63) / 64, std::uint64_t{0});
            chunks_.push_back(std::move(chunk));
        } catch (...) {
            // The chunk table or its live map could not grow; the chunk itself
            // must not leak.
            freeBytes(data, options_.blockAlignment);
            throw;
        }
        capacity_ += count;

        // Thread the new chunk onto the free list in reverse address order, so
        // the lowest block is handed out first.
        for (size_type index = count; index-- > 0;) {
            void* block = data + index * stride_;
            *static_cast<void**>(block) = freeHead_;
            freeHead_ = block;
        }
    }

    // The chunk that owns `block`, or nullptr when the pool does not own it.
    // `index` receives the block's position inside that chunk.
    [[nodiscard]] const Chunk* findChunk(const void* block, size_type* index) const noexcept
    {
        if (block == nullptr)
            return nullptr;

        const auto address = reinterpret_cast<std::uintptr_t>(block);
        for (const Chunk& chunk : chunks_) {
            const auto begin = reinterpret_cast<std::uintptr_t>(chunk.data);
            if (address < begin)
                continue;

            const auto offset = address - begin;
            if (offset >= static_cast<std::uintptr_t>(chunk.count) * stride_
                || offset % stride_ != 0) {
                continue;
            }

            *index = static_cast<size_type>(offset / stride_);
            return &chunk;
        }
        return nullptr;
    }

    [[nodiscard]] Chunk* findChunk(const void* block, size_type* index) noexcept
    {
        return const_cast<Chunk*>(std::as_const(*this).findChunk(block, index));
    }

    [[nodiscard]] static bool isMarked(const Chunk& chunk, size_type index) noexcept
    {
        return (chunk.live[index / 64] & (std::uint64_t{1} << (index % 64))) != 0;
    }

    static void mark(Chunk& chunk, size_type index, bool live) noexcept
    {
        const std::uint64_t bit = std::uint64_t{1} << (index % 64);
        std::uint64_t& word = chunk.live[index / 64];
        if (live)
            word |= bit;
        else
            word &= ~bit;
    }

    Options options_{};
    size_type stride_ = 0;
    std::vector<Chunk> chunks_;
    void* freeHead_ = nullptr;
    size_type capacity_ = 0;
    size_type inUse_ = 0;
    size_type highWaterMark_ = 0;
    size_type rejectedReleases_ = 0;
};

} // namespace md::pool
