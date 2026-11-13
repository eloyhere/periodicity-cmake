#include <md/render/layer_cache.h>

#include <QList>
#include <QtMath>

#include <cmath>

namespace md::render {

namespace {

QString refusalText(const QString& reason)
{
    return QStringLiteral("render::LayerCache: %1").arg(reason);
}

} // namespace

LayerCache::LayerCache(qint64 budgetBytes)
    : budgetBytes_(budgetBytes)
{
}

LayerCache::~LayerCache() = default;

Layer* LayerCache::acquire(Key key, const QSizeF& logicalSize, qreal devicePixelRatio,
                           quint64 themeRevision)
{
    ++counters_.acquires;
    lastError_.clear();

    // Pin before anything can evict: from this point until endFrame() the layer
    // this call returns is part of the frame being composited.
    if (frameDepth_ > 0)
        pinned_.insert(key);

    if (doomed_.contains(key)) {
        ++counters_.refused;
        lastError_ = refusalText(QStringLiteral(
            "layer %1 was invalidated while the current frame holds it").arg(key));
        return nullptr;
    }

    const auto found = layers_.find(key);
    if (found != layers_.end()) {
        Entry& entry = found->second;
        Layer* layer = entry.layer.get();
        const qint64 before = bytesOf(*layer);

        const bool geometryMatches = layer->isValid()
            && layer->logicalSize() == logicalSize
            && qFuzzyCompare(layer->devicePixelRatio(), devicePixelRatio);

        if (!geometryMatches) {
            const qint64 estimate = estimateBytes(logicalSize, devicePixelRatio);
            const qint64 growth = estimate > before ? estimate - before : 0;
            if (growth > 0 && !makeRoom(growth, key)) {
                ++counters_.refused;
                lastError_ = refusalText(QStringLiteral(
                    "no unpinned layer can free %1 bytes for layer %2")
                                             .arg(growth)
                                             .arg(key));
                return nullptr;
            }

            if (!layer->resize(logicalSize, devicePixelRatio)) {
                // The surface cannot exist at the requested geometry (an
                // invalid size or ratio, or above the rasterizer's pixel cap).
                // The failed resize has already released the image, so the old
                // accounting has to go too; dropping the entry keeps the next
                // acquire honest instead of returning a stale surface.
                bytes_ -= before;
                if (bytes_ < 0)
                    bytes_ = 0;
                erase(key, false);
                ++counters_.refused;
                lastError_ = refusalText(
                    QStringLiteral("layer %1 cannot be allocated at %2 x %3 ratio %4")
                        .arg(key)
                        .arg(logicalSize.width())
                        .arg(logicalSize.height())
                        .arg(devicePixelRatio));
                return nullptr;
            }

            bytes_ += bytesOf(*layer) - before;
            noteBytes();
        }

        layer->setThemeRevision(themeRevision);
        entry.lastUse = ++useSerial_;
        ++counters_.hits;
        return layer;
    }

    // A key with no surface: make room first, so a refused allocation does not
    // disturb the layers that are already cached.
    const qint64 estimate = estimateBytes(logicalSize, devicePixelRatio);
    if (estimate <= 0) {
        ++counters_.refused;
        lastError_ = refusalText(QStringLiteral("layer %1 has an unusable geometry").arg(key));
        return nullptr;
    }

    if (!makeRoom(estimate, std::nullopt)) {
        ++counters_.refused;
        lastError_ = refusalText(
            QStringLiteral("no unpinned layer can free %1 bytes for layer %2")
                .arg(estimate)
                .arg(key));
        return nullptr;
    }

    auto layer = std::make_unique<Layer>(logicalSize, devicePixelRatio, themeRevision);
    if (!layer->isValid()) {
        ++counters_.refused;
        lastError_ = refusalText(
            QStringLiteral("layer %1 could not allocate %2 x %3 pixels")
                .arg(key)
                .arg(qCeil(logicalSize.width() * devicePixelRatio))
                .arg(qCeil(logicalSize.height() * devicePixelRatio)));
        return nullptr;
    }

    Layer* raw = layer.get();
    bytes_ += bytesOf(*raw);
    noteBytes();
    layers_.emplace(key, Entry{std::move(layer), ++useSerial_});
    ++counters_.misses;
    return raw;
}

void LayerCache::release(Key key)
{
    const auto found = layers_.find(key);
    if (found == layers_.end())
        return;

    ++counters_.releases;
    found->second.lastUse = ++useSerial_;
    // Deliberately not removed from pinned_: within one frame a caller may hand
    // the same layer to the compositor twice, and eviction must not reason
    // about which of those handles is still live. endFrame() unpins.
}

void LayerCache::invalidate(Key key)
{
    const auto found = layers_.find(key);
    if (found == layers_.end())
        return;

    if (frameDepth_ > 0 && pinned_.contains(key)) {
        // The layer may still be referenced by a LayerRef that this frame is
        // compositing. Mark it dirty now, destroy it when the frame closes.
        doomed_.insert(key);
        found->second.layer->addAll();
        return;
    }

    erase(key, true);
}

void LayerCache::invalidateAll()
{
    if (frameDepth_ == 0) {
        counters_.invalidated += static_cast<quint64>(layers_.size());
        layers_.clear();
        doomed_.clear();
        bytes_ = 0;
        return;
    }

    QList<Key> removable;
    for (auto& entry : layers_) {
        if (pinned_.contains(entry.first)) {
            doomed_.insert(entry.first);
            entry.second.layer->addAll();
        } else {
            removable.append(entry.first);
        }
    }

    for (const Key key : removable)
        erase(key, true);
}

void LayerCache::beginFrame()
{
    ++frameDepth_;
    if (frameDepth_ == 1)
        pinned_.clear();
}

void LayerCache::endFrame()
{
    if (frameDepth_ == 0)
        return;

    --frameDepth_;
    if (frameDepth_ > 0)
        return;

    // Everything the frame touched becomes the most recently used: "used in
    // the frame that just closed" is a single point in LRU time.
    for (auto& entry : layers_) {
        if (pinned_.contains(entry.first))
            entry.second.lastUse = ++useSerial_;
    }
    pinned_.clear();

    const QList<Key> doomed = doomed_.values();
    doomed_.clear();
    for (const Key key : doomed)
        erase(key, true);

    trimToBudget();
}

void LayerCache::setBudgetBytes(qint64 budgetBytes)
{
    budgetBytes_ = budgetBytes;
    trimToBudget();
}

bool LayerCache::contains(Key key) const
{
    return layers_.find(key) != layers_.end();
}

Layer* LayerCache::find(Key key) const
{
    const auto found = layers_.find(key);
    return found == layers_.end() ? nullptr : found->second.layer.get();
}

LayerCacheStats LayerCache::stats() const
{
    LayerCacheStats result = counters_;
    result.bytes = bytes_;
    result.peakBytes = peakBytes_;
    result.layers = static_cast<int>(layers_.size());
    return result;
}

void LayerCache::resetStats()
{
    counters_ = LayerCacheStats{};
    peakBytes_ = bytes_;
}

bool LayerCache::makeRoom(qint64 incomingBytes, std::optional<Key> skipKey)
{
    if (budgetBytes_ < 0)
        return true;

    while (bytes_ + incomingBytes > budgetBytes_) {
        const std::optional<Key> victim = oldestEvictable(skipKey);
        if (!victim.has_value())
            return false;
        evict(*victim);
    }

    return true;
}

std::optional<LayerCache::Key> LayerCache::oldestEvictable(
    std::optional<Key> skipKey) const
{
    std::optional<Key> best;
    quint64 bestUse = 0;

    for (const auto& entry : layers_) {
        const Key key = entry.first;
        if (skipKey.has_value() && key == *skipKey)
            continue;

        // The guarantee: a layer the open frame is compositing is not a
        // candidate, whatever its age.
        if (frameDepth_ > 0 && pinned_.contains(key))
            continue;

        // A layer already doomed is going to be destroyed anyway; evicting a
        // live one instead would only add churn.
        if (doomed_.contains(key))
            continue;

        if (!best.has_value() || entry.second.lastUse < bestUse) {
            best = key;
            bestUse = entry.second.lastUse;
        }
    }

    return best;
}

void LayerCache::evict(Key key)
{
    erase(key, false);
    ++counters_.evicted;
}

void LayerCache::erase(Key key, bool countInvalidated)
{
    const auto found = layers_.find(key);
    if (found == layers_.end())
        return;

    bytes_ -= bytesOf(*found->second.layer);
    if (bytes_ < 0)
        bytes_ = 0;

    layers_.erase(found);

    if (countInvalidated)
        ++counters_.invalidated;
}

void LayerCache::trimToBudget()
{
    if (budgetBytes_ < 0)
        return;

    while (bytes_ > budgetBytes_) {
        const std::optional<Key> victim = oldestEvictable(std::nullopt);
        if (!victim.has_value())
            return;
        evict(*victim);
    }
}

void LayerCache::noteBytes()
{
    if (bytes_ > peakBytes_)
        peakBytes_ = bytes_;
}

qint64 LayerCache::bytesOf(const Layer& layer)
{
    return layer.byteSize();
}

qint64 LayerCache::estimateBytes(const QSizeF& logicalSize, qreal devicePixelRatio)
{
    if (!std::isfinite(static_cast<double>(logicalSize.width()))
        || !std::isfinite(static_cast<double>(logicalSize.height()))
        || !std::isfinite(static_cast<double>(devicePixelRatio))
        || logicalSize.width() <= 0.0 || logicalSize.height() <= 0.0
        || devicePixelRatio <= 0.0) {
        return 0;
    }

    const qreal width = logicalSize.width() * devicePixelRatio;
    const qreal height = logicalSize.height() * devicePixelRatio;
    if (!std::isfinite(static_cast<double>(width))
        || !std::isfinite(static_cast<double>(height))) {
        return 0;
    }

    // RenderTarget allocates ceil(logical * ratio) pixels; the same rounding
    // here keeps the budget and the accounting in step.
    const qint64 pixelWidth = qCeil(width);
    const qint64 pixelHeight = qCeil(height);
    if (pixelWidth <= 0 || pixelHeight <= 0)
        return 0;

    // 4 bytes per pixel for every format RenderTarget can produce today.
    return pixelWidth * pixelHeight * 4;
}

} // namespace md::render
