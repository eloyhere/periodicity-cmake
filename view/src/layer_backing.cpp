#include "layer_backing.h"

#include <md/view/view.h>

#include <QApplication>
#include <QCoreApplication>
#include <QWidget>

#include <memory>

namespace md {

namespace {

// The process-wide uncached switch. UI-thread only, like the widgets it
// affects: every View reads it from paintEvent().
bool g_layerDebugUncached = false;

} // namespace

render::LayerCache& LayerBacking::cache()
{
    // Deliberately never destroyed. A View may be destroyed during static
    // destruction or in a test that outlives its QApplication, and its
    // LayerBacking destructor still has to invalidate its key; a function-local
    // static object would already be gone at that point.
    static render::LayerCache* instance = new render::LayerCache;
    return *instance;
}

render::CpuCompositor& LayerBacking::compositor()
{
    static render::CpuCompositor instance;
    return instance;
}

LayerBacking::~LayerBacking()
{
    release();
}

void LayerBacking::release()
{
    if (layer == nullptr)
        return;

    // Discard, not release: a caller that stopped using the layer should not
    // leave its bytes in the cache under a key nobody will acquire again.
    cache().invalidate(key);
    layer = nullptr;
}

void LayerBacking::addDamage(const QRectF& rect)
{
    if (layer != nullptr)
        layer->addDamage(rect);
}

void LayerBacking::addAllDamage()
{
    if (layer != nullptr)
        layer->addAll();
}

quint64 LayerBacking::revisionFor(const Theme& theme)
{
    if (!hasTheme || !(theme == lastTheme)) {
        lastTheme = theme;
        hasTheme = true;
        ++themeRevision;
    }
    return themeRevision;
}

// ---------------------------------------------------------------------------
// View: the layer-backed paint path
// ---------------------------------------------------------------------------

bool View::isLayerBacked() const noexcept
{
    return layerBacking_ != nullptr && layerBacking_->enabled;
}

bool View::hasLayerSurface() const noexcept
{
    return layerBacking_ != nullptr && layerBacking_->layer != nullptr;
}

QSize View::layerPixelSize() const noexcept
{
    if (layerBacking_ == nullptr || layerBacking_->layer == nullptr)
        return QSize();
    return layerBacking_->layer->pixelSize();
}

quint64 View::layerThemeRevision() const noexcept
{
    return layerBacking_ != nullptr ? layerBacking_->themeRevision : 0;
}

qreal View::layerDevicePixelRatio() const
{
    return devicePixelRatioF();
}

void View::setLayerBacked(bool enabled)
{
    if (!enabled) {
        if (layerBacking_ == nullptr || !layerBacking_->enabled)
            return;

        layerBacking_->enabled = false;
        layerBacking_->release();
        requestUpdate();
        return;
    }

    if (layerBacking_ != nullptr && layerBacking_->enabled)
        return;

    if (layerBacking_ == nullptr)
        layerBacking_ = std::make_unique<LayerBacking>();

    layerBacking_->enabled = true;
    layerBacking_->key = updateKey_;

    // Layer backing needs a damage record: "rasterise only what changed" is
    // exactly the record the view already keeps, and paintContent() reads it.
    // Turning layer backing off again leaves damage tracking on; a caller that
    // wants neither calls setDamageTrackingEnabled(false), which drops the
    // layer first.
    if (!isDamageTrackingEnabled())
        setDamageTrackingEnabled(true);

    // The first layered pass must paint everything: a newly enabled view has no
    // retained pixels, and requestUpdate() is also what marks the whole view
    // (and, once it exists, the whole layer) dirty.
    requestUpdate();
}

bool View::paintThroughLayer(const Theme& theme, const QRectF& content,
                             const layer::SurfaceContext& context)
{
    if (layerBacking_ == nullptr || !layerBacking_->enabled)
        return false;

    LayerBacking& backing = *layerBacking_;

    if (view::isLayerDebugUncached()) {
        // The debug mode compares the two paths, so it must hold no layer at
        // all: a retained surface left behind would make "uncached" only half
        // true.
        backing.release();
        return false;
    }

    // The font's description is the per-widget input the layer compares. It is
    // refreshed before every pass, so a font change is caught even when no
    // FontChange event reached changeEvent().
    backing.fontSignature = font().toString().toUtf8();

    const QSizeF logicalSize(rect().size());

    return backing.paint(
        this, theme, logicalSize, layerDevicePixelRatio(),
        [this, &backing, &theme, &content, &context](QPainter& painter) {
            // The layer may have decided the whole surface is stale (a theme,
            // font, geometry or content revision change). paintContent() reads
            // the view's own record, so the two have to agree; otherwise a
            // damage-aware component would skip content on the very pass that
            // clears the whole layer.
            if (damageTrackingEnabled_ && backing.layer != nullptr
                && backing.layer->isFullDamage() && !damage_.isFull()) {
                damage_.setFullRect(QRectF(rect()));
                damage_.addAll();
            }

            paintStages(painter, theme, content, context);
        });
}

} // namespace md

namespace md::view {

void setLayerDebugUncached(bool uncached)
{
    if (g_layerDebugUncached == uncached)
        return;

    g_layerDebugUncached = uncached;

    // Make the switch visible without waiting for unrelated repaints. The
    // layers themselves are dropped by each view's next paint; scheduling that
    // paint here is what turns "on the next paint" into "now".
    if (QCoreApplication::instance() != nullptr) {
        const QWidgetList widgets = QApplication::allWidgets();
        for (QWidget* widget : widgets) {
            if (widget != nullptr)
                widget->update();
        }
    }
}

bool isLayerDebugUncached()
{
    return g_layerDebugUncached;
}

int liveLayerCount()
{
    return LayerBacking::cache().layerCount();
}

} // namespace md::view
