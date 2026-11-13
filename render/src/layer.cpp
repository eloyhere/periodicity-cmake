#include <md/render/layer.h>

#include <QPainter>
#include <QtMath>

#include <cmath>

namespace md::render {

namespace {

bool isFiniteRect(const QRectF& rect)
{
    return std::isfinite(static_cast<double>(rect.x()))
        && std::isfinite(static_cast<double>(rect.y()))
        && std::isfinite(static_cast<double>(rect.width()))
        && std::isfinite(static_cast<double>(rect.height()));
}

qreal areaOf(const QRectF& rect)
{
    if (rect.isEmpty())
        return 0.0;
    return rect.width() * rect.height();
}

// Half-open rectangles do not "intersect" when they only share an edge, but an
// edge-touching pair is one region for repaint purposes: growing the first by
// half a logical pixel captures that without merging genuinely separate strips.
bool touches(const QRectF& a, const QRectF& b)
{
    return a.adjusted(-0.5, -0.5, 0.5, 0.5).intersects(b);
}

} // namespace

Layer::Layer() = default;

Layer::Layer(const QSizeF& logicalSize, qreal devicePixelRatio, quint64 themeRevision)
    : target_(logicalSize,
              RenderTargetFormat{QImage::Format_ARGB32_Premultiplied, devicePixelRatio})
    , themeRevision_(themeRevision)
{
    if (target_.isValid()) {
        // A fresh surface must start transparent: the layer is composited with
        // SourceOver, so an uninitialised image would show as garbage in every
        // pixel the owner does not paint.
        target_.clear(Qt::transparent);
        collapseToFull();
    }
}

Layer::~Layer() = default;

bool Layer::resize(const QSizeF& logicalSize, qreal devicePixelRatio)
{
    const bool same = target_.isValid()
        && target_.logicalSize() == logicalSize
        && qFuzzyCompare(target_.devicePixelRatio(), devicePixelRatio);
    if (same)
        return true;

    const bool ok = target_.resize(
        logicalSize,
        RenderTargetFormat{QImage::Format_ARGB32_Premultiplied, devicePixelRatio});

    // Whether the allocation succeeded or not, nothing retained from the old
    // surface may be shown again: the next pass must paint the whole layer.
    collapseToFull();
    return ok && target_.isValid();
}

QRectF Layer::bounds() const noexcept
{
    if (!target_.isValid())
        return QRectF();

    const QSizeF size = target_.logicalSize();
    return QRectF(0.0, 0.0, size.width(), size.height());
}

qint64 Layer::byteSize() const noexcept
{
    if (!target_.isValid())
        return 0;
    return static_cast<qint64>(target_.image().sizeInBytes());
}

void Layer::setContentRevision(quint64 revision)
{
    if (contentRevision_ == revision)
        return;

    contentRevision_ = revision;
    addAll();
}

void Layer::setThemeRevision(quint64 revision)
{
    if (themeRevision_ == revision)
        return;

    themeRevision_ = revision;
    addAll();
}

void Layer::setFontSignature(const QByteArray& signature)
{
    if (fontSignature_ == signature)
        return;

    fontSignature_ = signature;
    addAll();
}

bool Layer::syncEnvironment(quint64 themeRevision, const QByteArray& fontSignature)
{
    bool changed = false;

    if (themeRevision_ != themeRevision) {
        themeRevision_ = themeRevision;
        changed = true;
    }

    if (fontSignature_ != fontSignature) {
        fontSignature_ = fontSignature;
        changed = true;
    }

    if (changed)
        addAll();

    return changed;
}

void Layer::addDamage(const QRect& rect)
{
    addDamage(QRectF(rect));
}

void Layer::addDamage(const QRectF& rect)
{
    if (!target_.isValid() || full_)
        return;

    if (!isFiniteRect(rect))
        return;

    const QRectF clipped = rect.normalized().intersected(bounds());
    if (clipped.isEmpty())
        return;

    mergeDamage(clipped);
}

void Layer::addAll()
{
    if (!target_.isValid())
        return;

    collapseToFull();
}

void Layer::markClean()
{
    damage_.clear();
    full_ = false;
}

QRegion Layer::damage() const
{
    if (!target_.isValid())
        return QRegion();

    const QRect clip = bounds().toAlignedRect();
    if (full_)
        return QRegion(clip);

    QRegion region;
    for (const QRectF& rect : damage_)
        region += rect.toAlignedRect();
    return region.intersected(QRegion(clip));
}

QRectF Layer::damageBounds() const
{
    if (!target_.isValid())
        return QRectF();

    if (full_)
        return bounds();

    QRectF united;
    for (const QRectF& rect : damage_) {
        if (united.isNull())
            united = rect;
        else
            united = united.united(rect);
    }
    return united;
}

int Layer::damageRectCount() const noexcept
{
    if (!target_.isValid())
        return 0;
    return full_ ? 1 : static_cast<int>(damage_.size());
}

QPainter* Layer::beginPaint()
{
    if (painter_ != nullptr) {
        qWarning("render::Layer: beginPaint() called while a painter is active");
        return nullptr;
    }

    if (!target_.isValid() || !isDirty())
        return nullptr;

    // A full invalidation starts from transparent again. Without this, content
    // that disappeared (a removed graph line, a shorter row) would survive as a
    // ghost under the new frame, which is exactly the failure the debug mode
    // exists to catch.
    if (full_)
        target_.clear(Qt::transparent);

    auto painter = std::make_unique<QPainter>(&target_.imageRef());
    if (!painter->isActive()) {
        qWarning("render::Layer: could not begin a painter on the layer image");
        return nullptr;
    }

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);
    painter->scale(devicePixelRatio(), devicePixelRatio());

    if (!full_)
        painter->setClipRegion(damage(), Qt::ReplaceClip);

    painter_ = std::move(painter);
    return painter_.get();
}

void Layer::endPaint()
{
    if (painter_ == nullptr)
        return;

    painter_->end();
    painter_.reset();
}

void Layer::mergeDamage(const QRectF& rect)
{
    damage_.append(rect);

    bool merged = true;
    while (merged) {
        merged = false;
        for (int i = 0; i < damage_.size() && !merged; ++i) {
            for (int j = i + 1; j < damage_.size(); ++j) {
                if (!touches(damage_.at(i), damage_.at(j)))
                    continue;

                damage_[i] = damage_.at(i).united(damage_.at(j));
                damage_.removeAt(j);
                merged = true;
                break;
            }
        }
    }

    if (damage_.size() > kMaxDamageRects) {
        collapseToBounding();
        return;
    }

    const qreal fullArea = areaOf(bounds());
    if (fullArea > 0.0 && areaOf(damageBounds()) > kFullDamageRatio * fullArea)
        collapseToFull();
}

void Layer::collapseToBounding()
{
    const QRectF united = damageBounds();
    damage_.clear();
    if (!united.isEmpty())
        damage_.append(united);
}

void Layer::collapseToFull()
{
    damage_.clear();
    full_ = true;
}

} // namespace md::render
