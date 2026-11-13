// Periodicity — view layer: the private half of layer backing.
//
// This header is deliberately not in include/ and not installed. It is the only
// place where the widget layer (view/) names the device layer's types
// (render::Layer, render::LayerCache, render::CpuCompositor), and it is included
// by view.cpp and layer_backing.cpp only. The public View header forward
// declares this struct, so a consumer of md::View never sees a render/ type and
// never links against one on purpose:
//
//     render ⊅ view      (render/ still never mentions a widget)
//     view   → render    (private: the layered paint path is view's business)
//
// That is the one dependency edge the L2 decision adds. It is stated here
// because it is the only place it exists.

#pragma once

#include <md/render/compositor.h>
#include <md/render/layer.h>
#include <md/render/layer_cache.h>
#include <md/theme/theme.h>
#include <md/view/layer_debug.h>

#include <QByteArray>
#include <QList>
#include <QPainter>
#include <QRectF>
#include <QScopeGuard>
#include <QSizeF>
#include <QString>
#include <QWidget>
#include <QtGlobal>

namespace md {

// One layer-backed View's state. A view that never calls setLayerBacked(true)
// never allocates one of these, so the default path pays nothing.
struct LayerBacking
{
    bool enabled = false;

    // The cache key: the owning view's process-unique coalescing key.
    quint64 key = 0;

    // The layer currently held for the view, or null. It is owned by the
    // process-wide cache, never by this struct; `release()` gives it back.
    render::Layer* layer = nullptr;

    // The revision the layer was last acquired with. It advances whenever the
    // resolved Theme changes, which is the only per-view state a theme change
    // needs.
    quint64 themeRevision = 0;
    Theme lastTheme;
    bool hasTheme = false;

    // The widget font's description, refreshed before every layered pass.
    // Layer::setFontSignature() turns a change into a full invalidation.
    QByteArray fontSignature;

    ~LayerBacking();

    // The process-wide cache and compositor. One of each for the process: the
    // byte budget is a process resource, and the CPU compositor is stateless.
    static render::LayerCache& cache();
    static render::CpuCompositor& compositor();

    // Returns the layer to the cache (drop it) and forgets the pointer. Called
    // when layer backing is turned off, while the debug mode is on, and from
    // the destructor, so a view never leaks a surface for a key nobody will
    // acquire again.
    void release();

    // Forwards one damage rectangle recorded by the view. A no-op until the
    // layer exists; a fresh layer is fully dirty anyway.
    void addDamage(const QRectF& rect);
    void addAllDamage();

    // Advances the theme revision when `theme` differs from the one the layer
    // was painted with.
    quint64 revisionFor(const Theme& theme);

    // Paints one frame through the layer and blits it into `owner`. Returns
    // false when the frame must take the direct path instead: not enabled, the
    // debug mode is on, the cache refused the layer, or the layer could not be
    // allocated. `paintStages` is invoked with a painter that is already
    // scaled to the layer's device pixel ratio and clipped to its damage.
    template <class PaintStages>
    bool paint(QWidget* owner, const Theme& theme, const QSizeF& logicalSize,
               qreal devicePixelRatio, PaintStages&& paintStages)
    {
        if (!enabled || owner == nullptr)
            return false;

        if (view::isLayerDebugUncached()) {
            release();
            return false;
        }

        const quint64 revision = revisionFor(theme);

        render::LayerCache& layerCache = cache();
        layerCache.beginFrame();
        const auto frameGuard = qScopeGuard([&layerCache] { layerCache.endFrame(); });

        render::Layer* acquired =
            layerCache.acquire(key, logicalSize, devicePixelRatio, revision);
        if (acquired == nullptr) {
            // The cache could not serve this frame without evicting a layer it
            // is already compositing, or the surface is not allocatable. Paint
            // directly; the retained surface (if any) stays where it is.
            layer = nullptr;
            return false;
        }
        layer = acquired;

        // A geometry, theme or font change has already marked the layer fully
        // dirty inside the cache/Layer; nothing else has to invalidate it.
        layer->setFontSignature(fontSignature);

        if (QPainter* painter = layer->beginPaint()) {
            // The guard ends the painter even when a component hook throws, so
            // a failed pass cannot leave the layer locked in "painting" state.
            const auto paintGuard = qScopeGuard([acquired] { acquired->endPaint(); });
            paintStages(*painter);
            layer->markClean();
        }

        QPainter windowPainter(owner);
        if (!windowPainter.isActive())
            return true;

        windowPainter.setRenderHint(QPainter::Antialiasing, true);
        windowPainter.setRenderHint(QPainter::TextAntialiasing, true);

        render::LayerRef ref;
        ref.layer = layer;
        ref.destination = QRectF(QPointF(0.0, 0.0), logicalSize);
        ref.opacity = 1.0;

        compositor().composeInto(windowPainter, QList<render::LayerRef>{ref});
        return true;
    }
};

} // namespace md
