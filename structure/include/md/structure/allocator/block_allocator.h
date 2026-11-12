// Periodicity — chunked fixed-size block allocator (structure).
//
// This allocator exists inside `structure` rather than in `pool` because
// `structure` may depend on `core` only (tools/check_layering.cmake) and the
// LRU order containers need a tiny, dependency-free node source. It is a
// low-level companion of ObjectPool<T> (allocator/object_pool.h), not a
// replacement for the general-purpose md::pool containers.
//
// A BlockAllocator serves fixed-size raw blocks from chunks. One chunk is a
// single contiguous array of `blocksPerChunk` blocks, allocated on demand with
// one `new[]`; the chunk array is owned through `std::unique_ptr<std::byte[]>`.
// Chunks never move, so a block address stays valid until the allocator is
// destroyed (or clear() is called, which requires that no block is live).
//
// Free blocks are threaded through an intrusive free list: the next pointer is
// stored in the first bytes of the free block itself, which is why blockSize
// must be at least sizeof(void*). Allocation is therefore a stack pop and
// deallocation a stack push — no search, no size, no user code.
//
//     allocate / deallocate     O(1)
//     reserve(n)                at most ceil(n / blocksPerChunk) chunk allocations
//     shrinkToFit()             releases chunks that never handed out a block,
//                               and everything once no block is live
//
// Alignment: a chunk is allocated with `new[]`, so its base is aligned for any
// type with fundamental alignment. The distance between blocks (the stride) is
// blockSize rounded up to alignof(std::max_align_t), so every block is aligned
// like a fresh `new`. Types over-aligned beyond std::max_align_t must use a
// different allocator.
//
// The allocator is not thread-safe and not copyable. Moves are supported and
// keep block addresses valid (the chunk buffers are moved, not reallocated).

#pragma once

#include <cassert>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace md::structure {

class BlockAllocator
{
public:
    using size_type = std::size_t;

    // blockSize is the number of usable bytes per block; blocksPerChunk is how
    // many blocks one `new[]` provides. Preconditions (asserted):
    //   * blockSize >= sizeof(void*)   (the free list stores a pointer in a block)
    //   * blocksPerChunk > 0
    //   * the rounded stride times blocksPerChunk fits in std::size_t
    explicit BlockAllocator(size_type blockSize, size_type blocksPerChunk = 64)
        : blockSize_(blockSize), blocksPerChunk_(blocksPerChunk)
    {
        assert(blockSize_ >= sizeof(void*)
               && "BlockAllocator needs room for the free-list link");
        assert(blocksPerChunk_ > 0 && "BlockAllocator needs a non-empty chunk");

        // A block must be able to hold the free-list link and keep every block
        // aligned at least as strictly as a fresh `new` allocation.
        const size_type usable = blockSize_ < sizeof(void*) ? sizeof(void*) : blockSize_;
        stride_ = roundUp(usable, alignof(std::max_align_t));

        assert(stride_ <= (std::numeric_limits<size_type>::max)() / blocksPerChunk_
               && "BlockAllocator chunk size overflows std::size_t");
    }

    BlockAllocator(const BlockAllocator&) = delete;
    BlockAllocator& operator=(const BlockAllocator&) = delete;

    // Moves transfer ownership of the chunks. Block addresses do not change, so
    // raw pointers obtained from `other` remain valid and now belong to `*this`.
    // The destination must not own live blocks: moving over it releases its
    // chunks without running any destructor (the allocator has no idea what the
    // blocks contain).
    BlockAllocator(BlockAllocator&& other) noexcept
        : blockSize_(other.blockSize_), stride_(other.stride_),
          blocksPerChunk_(other.blocksPerChunk_), chunks_(std::move(other.chunks_)),
          freeList_(other.freeList_), bumpChunk_(other.bumpChunk_), inUse_(other.inUse_)
    {
        other.freeList_ = nullptr;
        other.bumpChunk_ = npos;
        other.inUse_ = 0;
    }

    BlockAllocator& operator=(BlockAllocator&& other) noexcept
    {
        if (this != &other) {
            blockSize_ = other.blockSize_;
            stride_ = other.stride_;
            blocksPerChunk_ = other.blocksPerChunk_;
            chunks_ = std::move(other.chunks_);
            freeList_ = other.freeList_;
            bumpChunk_ = other.bumpChunk_;
            inUse_ = other.inUse_;

            other.freeList_ = nullptr;
            other.bumpChunk_ = npos;
            other.inUse_ = 0;
        }
        return *this;
    }

    ~BlockAllocator() = default;

    // ---- Sizes ------------------------------------------------------------

    [[nodiscard]] size_type blockSize() const noexcept { return blockSize_; }

    // Distance between consecutive block addresses; blockSize rounded up to
    // alignof(std::max_align_t).
    [[nodiscard]] size_type blockStride() const noexcept { return stride_; }

    [[nodiscard]] size_type blocksPerChunk() const noexcept { return blocksPerChunk_; }

    // Number of chunks currently owned.
    [[nodiscard]] size_type chunkCount() const noexcept { return chunks_.size(); }

    // Number of blocks currently owned (chunks * blocksPerChunk).
    [[nodiscard]] size_type capacity() const noexcept
    {
        return chunks_.size() * blocksPerChunk_;
    }

    // Number of blocks handed out and not yet returned.
    [[nodiscard]] size_type inUse() const noexcept { return inUse_; }

    // Number of blocks on the free list (capacity - inUse).
    [[nodiscard]] size_type pooled() const noexcept { return capacity() - inUse_; }

    // ---- Allocation -------------------------------------------------------

    // Pops a block from the free list, or carves one from the current chunk,
    // allocating a new chunk when the current one is exhausted.
    void* allocate()
    {
        if (freeList_ != nullptr) {
            void* block = freeList_;
            freeList_ = *static_cast<void**>(block);
            ++inUse_;
            return block;
        }

        ensureBumpChunk();

        Chunk& chunk = chunks_[bumpChunk_];
        std::byte* block = chunk.storage.get() + chunk.used * stride_;
        ++chunk.used;
        ++inUse_;
        return block;
    }

    // Returns a block obtained from allocate(). The size is not needed: the
    // block is pushed onto the intrusive free list.
    void deallocate(void* block) noexcept
    {
        assert(block != nullptr && "deallocate(nullptr) is not meaningful");
        if (block == nullptr)
            return;
        assert(inUse_ > 0 && "deallocate() called more often than allocate()");
        assert(owns(block) && "block does not belong to this allocator");

        *static_cast<void**>(block) = freeList_;
        freeList_ = block;
        --inUse_;
    }

    // Pre-allocates enough chunks for at least `blocks` blocks. Existing free
    // blocks are not touched; the capacity only grows.
    void reserve(size_type blocks)
    {
        const size_type needed =
            blocks / blocksPerChunk_ + (blocks % blocksPerChunk_ != 0 ? 1 : 0);
        while (chunks_.size() < needed)
            appendChunk();
    }

    // Releases chunks that have never handed out a block. When no block is live
    // this releases every chunk (and resets the free list).
    void shrinkToFit()
    {
        if (inUse_ == 0) {
            chunks_.clear();
            freeList_ = nullptr;
            bumpChunk_ = npos;
            return;
        }

        while (chunks_.size() > 1 && chunks_.back().used == 0
               && bumpChunk_ != chunks_.size() - 1) {
            chunks_.pop_back();
        }
    }

    // Releases every chunk. Precondition (asserted): no block is live. Calling
    // clear() with live blocks would leak whatever those blocks hold.
    void clear()
    {
        assert(inUse_ == 0 && "clear() requires that every block was returned");
        if (inUse_ != 0)
            return;
        chunks_.clear();
        freeList_ = nullptr;
        bumpChunk_ = npos;
    }

    // Whether `block` points at the start of a block owned by this allocator.
    // The check is a range scan over chunks (O(chunkCount())).
    [[nodiscard]] bool owns(const void* block) const noexcept
    {
        if (block == nullptr)
            return false;
        const auto* pointer = static_cast<const std::byte*>(block);
        const std::less<const std::byte*> before;
        const size_type chunkBytes = stride_ * blocksPerChunk_;
        for (const Chunk& chunk : chunks_) {
            const std::byte* begin = chunk.storage.get();
            const std::byte* end = begin + chunkBytes;
            if (!before(pointer, begin) && before(pointer, end))
                return true;
        }
        return false;
    }

private:
    static constexpr size_type npos = static_cast<size_type>(-1);

    struct Chunk
    {
        std::unique_ptr<std::byte[]> storage;
        size_type used = 0; // slots carved out by the bump cursor so far
    };

    static constexpr size_type roundUp(size_type value, size_type alignment) noexcept
    {
        return value / alignment * alignment
            + (value % alignment != 0 ? alignment : 0);
    }

    // Makes chunks_[bumpChunk_] the chunk with free tail space, creating a chunk
    // when the bump cursor reached the last one.
    void ensureBumpChunk()
    {
        if (bumpChunk_ != npos && chunks_[bumpChunk_].used < blocksPerChunk_)
            return;

        size_type candidate = bumpChunk_ == npos ? 0 : bumpChunk_ + 1;
        while (candidate < chunks_.size()
               && chunks_[candidate].used == blocksPerChunk_) {
            ++candidate;
        }

        if (candidate == chunks_.size()) {
            appendChunk();
            candidate = chunks_.size() - 1;
        }

        bumpChunk_ = candidate;
    }

    void appendChunk()
    {
        const size_type chunkBytes = stride_ * blocksPerChunk_;
        assert(stride_ == 0
               || chunkBytes / stride_ == blocksPerChunk_);

        Chunk chunk;
        chunk.storage.reset(new std::byte[chunkBytes]);
        chunk.used = 0;
        chunks_.push_back(std::move(chunk));
    }

    size_type blockSize_ = 0;
    size_type stride_ = 0;
    size_type blocksPerChunk_ = 0;
    std::vector<Chunk> chunks_;
    void* freeList_ = nullptr;
    size_type bumpChunk_ = npos;
    size_type inUse_ = 0;
};

} // namespace md::structure
