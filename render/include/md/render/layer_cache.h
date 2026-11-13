// Periodicity — render layer: the retained-surface cache.
//
// One layer per opted-in view is too much memory to leave unbounded, so the
// cache owns every layer and enforces a byte budget with least-recently-used
// eviction. The caller keeps an opaque key (the view's own identity) and asks
// for the layer by key; it never allocates, resizes or destroys a Layer itself.
//
// Keying. The key is the only identity the cache knows: the same key always
// names the same surface. Geometry (logical size, device pixel ratio), the
// theme revision and the font signature are *validated* on every acquire, not
// folded into the key, because a view that was resized or re-themed must keep
// its surface object — reallocating and repainting is the cache's job, and the
// caller must not have to notice.
//
//   * a different logical size or ratio reallocates the surface and marks the
//     whole layer dirty (the previous pixels describe the wrong geometry);
//   * a different theme revision or font signature marks the whole layer dirty
//     (the previous pixels describe the wrong appearance);
//   * a different content revision is carried by the Layer itself.
//
// None of those counts as a miss: the surface was found by key. A miss is a key
// that has no surface at all — first use, invalidation, or eviction.
//
// Eviction guarantee — a layer is never evicted while it is being composited in
// the same frame. The mechanism is frame pinning:
//
//   1. beginFrame() opens a frame; the first open frame clears the pin set.
//      Nested frames are reference counted, so a nested paint cannot close the
//      outer frame early.
//   2. Every acquire() made while a frame is open pins its key. release() does
//      not unpin: within one frame a handle may be handed to the compositor
//      more than once, and the cache must not reason about that.
//   3. Eviction (makeRoom, trimToBudget, setBudgetBytes) only ever considers
//      keys that are not pinned. If no unpinned layer is large enough to free,
//      acquire() refuses (returns nullptr, records lastError(), counts refused)
//      rather than evicting a layer the current frame is compositing. The
//      caller then paints that frame on the direct path, which is slower but
//      correct.
//   4. endFrame() stamps every pinned key as most recently used, clears the pin
//      set, destroys layers whose invalidation was deferred and trims back to
//      the budget. Eviction therefore only ever happens at a frame boundary.
//
// Invalidate() and invalidateAll() destroy a layer immediately when it is not
// pinned. When it is pinned, the destruction is deferred to endFrame() and the
// key is marked doomed; acquire() of a doomed key in the same frame is refused
// rather than handing back a surface the caller just discarded. That is what
// keeps a LayerRef list from dangling until it has finished compositing.
//
// Budget. kDefaultBudgetBytes is 64 MiB. A negative budget means "no cap"; a
// zero budget means nothing may be cached (every acquire is refused). Lowering
// the budget evicts unpinned layers immediately; pinned layers survive until
// the frame closes.
//
// Threading. The cache is not thread-safe; like every device type in render/ it
// belongs to the UI thread.

#pragma once

#include <md/render/layer.h>

#include <QSet>
#include <QSizeF>
#include <QString>
#include <QtGlobal>

#include <memory>
#include <optional>
#include <unordered_map>

namespace md::render {

// Counters and current size of one cache. `stats()` fills `bytes`, `layers` and
// `peakBytes` from the live cache, so the returned struct is always coherent.
struct LayerCacheStats
{
    // Every acquire() call, including the refused ones.
    quint64 acquires = 0;

    // The layer was found by key (whether or not it had to be reallocated or
    // marked fully dirty afterwards).
    quint64 hits = 0;

    // No layer existed for the key; one was allocated.
    quint64 misses = 0;

    // release() calls that named a live layer.
    quint64 releases = 0;

    // Layers dropped by the byte budget.
    quint64 evicted = 0;

    // Layers dropped by invalidate(), invalidateAll() or a budget of zero.
    quint64 invalidated = 0;

    // Acquires that could not be served without evicting a pinned layer, or
    // whose surface could not be allocated. The caller paints directly instead.
    quint64 refused = 0;

    qint64 bytes = 0;
    qint64 peakBytes = 0;
    int layers = 0;
};

class LayerCache
{
public:
    using Key = quint64;

    // 64 MiB: enough for roughly forty 640 x 480 layers at ratio 2, which is
    // more than the benchmark pages migrate at once.
    static constexpr qint64 kDefaultBudgetBytes = 64LL * 1024 * 1024;

    explicit LayerCache(qint64 budgetBytes = kDefaultBudgetBytes);
    ~LayerCache();

    LayerCache(const LayerCache&) = delete;
    LayerCache& operator=(const LayerCache&) = delete;
    LayerCache(LayerCache&&) = delete;
    LayerCache& operator=(LayerCache&&) = delete;

    // The layer for `key`, allocating or reconfiguring it as needed. Returns
    // nullptr when the surface cannot be allocated or when serving the key
    // would require evicting a layer pinned by the open frame. A returned
    // layer is pinned until the open frame closes; outside a frame it is
    // simply cached.
    //
    // `logicalSize` is the size the owner paints in and `devicePixelRatio` the
    // ratio the surface is allocated at. `themeRevision` is compared with the
    // layer's own and marks the whole surface dirty when it differs.
    Layer* acquire(Key key, const QSizeF& logicalSize, qreal devicePixelRatio,
                   quint64 themeRevision = 0);

    // Records that the caller is done with the layer for now. The layer stays
    // cached (a later acquire is a hit) and becomes the most recently used.
    // Does not unpin within an open frame; see the guarantee above.
    void release(Key key);

    // Drops one layer. Immediate outside a frame; deferred to endFrame() when
    // the layer is pinned in the open frame.
    void invalidate(Key key);

    // Drops every layer (deferred for the pinned ones, exactly like
    // invalidate()). The next acquire of any key is a miss.
    void invalidateAll();

    // Frame boundary. beginFrame()/endFrame() are reference counted, so nested
    // painting is safe; eviction only runs when the outermost frame closes.
    void beginFrame();
    void endFrame();
    [[nodiscard]] bool isInFrame() const noexcept { return frameDepth_ > 0; }

    // Budget. A negative budget disables the cap; zero refuses everything.
    [[nodiscard]] qint64 budgetBytes() const noexcept { return budgetBytes_; }
    void setBudgetBytes(qint64 budgetBytes);

    [[nodiscard]] qint64 bytes() const noexcept { return bytes_; }
    [[nodiscard]] int layerCount() const noexcept
    {
        return static_cast<int>(layers_.size());
    }

    [[nodiscard]] bool contains(Key key) const;
    [[nodiscard]] Layer* find(Key key) const;

    [[nodiscard]] LayerCacheStats stats() const;
    void resetStats();

    // Why the last acquire() was refused; empty after a successful one.
    [[nodiscard]] QString lastError() const { return lastError_; }

private:
    struct Entry
    {
        std::unique_ptr<Layer> layer;
        quint64 lastUse = 0;
    };

    using Store = std::unordered_map<Key, Entry>;

    // Frees unpinned layers until `incomingBytes` more would fit. Returns false
    // when that is impossible without touching a pinned layer. `skipKey` is the
    // layer being reconfigured, if any; it is not a candidate for its own room.
    bool makeRoom(qint64 incomingBytes, std::optional<Key> skipKey);

    // The least recently used layer that may be evicted right now. A pinned
    // layer is never returned; `skipKey` excludes the layer being reconfigured.
    [[nodiscard]] std::optional<Key> oldestEvictable(std::optional<Key> skipKey) const;

    void evict(Key key);
    void erase(Key key, bool countInvalidated);
    void trimToBudget();
    void noteBytes();

    [[nodiscard]] static qint64 bytesOf(const Layer& layer);
    [[nodiscard]] static qint64 estimateBytes(const QSizeF& logicalSize,
                                              qreal devicePixelRatio);

    Store layers_;

    // Keys acquired during the open frame. Never evicted until it closes.
    QSet<Key> pinned_;

    // Keys invalidated while pinned; destroyed at endFrame().
    QSet<Key> doomed_;

    quint64 useSerial_ = 0;
    qint64 budgetBytes_ = kDefaultBudgetBytes;
    qint64 bytes_ = 0;
    qint64 peakBytes_ = 0;
    int frameDepth_ = 0;

    // Counters only; stats() adds bytes/layers/peakBytes from the live store.
    LayerCacheStats counters_;
    QString lastError_;
};

} // namespace md::render
