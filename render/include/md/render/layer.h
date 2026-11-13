// Periodicity — render layer: a retained surface for one view.
//
// A Layer owns a RenderTarget at device resolution plus the bookkeeping that
// makes a cached widget possible:
//
//   * a content revision, which the owner bumps when the content changed
//     wholesale;
//   * a damage region, which accumulates the rectangles that still have to be
//     rasterised, in the layer's *logical* coordinates (the same coordinates
//     the owner paints in);
//   * the three global appearance inputs — theme revision, font signature and
//     device pixel ratio — each compared rather than trusted, because a theme,
//     font or scale change repaints everything and there is no smaller honest
//     answer.
//
// Painting. beginPaint() returns a QPainter over the layer image, already
// scaled by devicePixelRatio() and clipped to damage(). The caller runs the
// component's paint pipeline through it and then calls markClean(). Only the
// damaged pixels are touched, so every other pixel keeps the previous frame;
// that is the whole point of retaining the surface. A layer that is not dirty
// returns no painter at all, and the caller only has to composite what is
// already there.
//
// Coordinates. Everything a caller passes in or reads back is logical:
// addDamage(), damage(), damageBounds() and the painter's clip. The surface is
// devicePixels = ceil(logical * ratio), matching RenderTarget. Damage is
// clipped to bounds(): unlike DamageTracker, a layer cannot retain a pixel
// outside its own surface, so an overhang (a shadow outside the widget
// rectangle) is not representable here.
//
// This class carries no QWidget and includes no widget header. It is a
// render/ type; view/ is the layer that binds one to a widget.

#pragma once

#include <md/render/render_target.h>

#include <QByteArray>
#include <QList>
#include <QRegion>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QtGlobal>

#include <memory>

class QPainter;

namespace md::render {

class Layer
{
public:
    // The largest number of rectangles retained before they collapse into their
    // bounding rectangle, mirroring md::DamageTracker's policy.
    static constexpr int kMaxDamageRects = 16;

    // The accumulated-area fraction of bounds() above which the damage
    // collapses to the whole surface. "More than 60 % of the layer" is the
    // point where per-rectangle bookkeeping costs more than a full repaint.
    static constexpr qreal kFullDamageRatio = 0.6;

    // An invalid, unallocated layer. No painter can be begun on it.
    Layer();

    // Allocates ceil(logicalSize * devicePixelRatio) pixels. The whole surface
    // starts dirty (and transparent), so the first paint covers it. An invalid
    // size or ratio leaves the layer invalid and allocates nothing.
    explicit Layer(const QSizeF& logicalSize, qreal devicePixelRatio = 1.0,
                   quint64 themeRevision = 0);

    ~Layer();

    Layer(const Layer&) = delete;
    Layer& operator=(const Layer&) = delete;
    Layer(Layer&&) = delete;
    Layer& operator=(Layer&&) = delete;

    // Reallocates for a new logical size and ratio. Returns isValid() after the
    // call. Any change — including a failed one — marks the whole surface
    // dirty, because no pixel of the previous allocation can be trusted. A
    // layer that is already at the requested size and ratio is left untouched
    // and keeps its damage, which is what makes re-acquisition cheap.
    bool resize(const QSizeF& logicalSize, qreal devicePixelRatio);

    [[nodiscard]] bool isValid() const noexcept { return target_.isValid(); }

    // The size the owner paints in.
    [[nodiscard]] QSizeF logicalSize() const noexcept { return target_.logicalSize(); }

    // The allocated size in physical pixels; empty for an invalid layer.
    [[nodiscard]] QSize pixelSize() const noexcept { return target_.pixelSize(); }

    [[nodiscard]] qreal devicePixelRatio() const noexcept
    {
        return target_.devicePixelRatio();
    }

    // QRectF(0, 0, logicalSize()); empty for an invalid layer.
    [[nodiscard]] QRectF bounds() const noexcept;

    // The surface itself. Const access is what the compositor needs; the
    // mutable form exists for a device that rasterises into the layer directly
    // instead of through beginPaint().
    [[nodiscard]] const RenderTarget& target() const noexcept { return target_; }
    [[nodiscard]] RenderTarget& target() noexcept { return target_; }

    // The allocated bytes, as QImage accounts for them; 0 for an invalid layer.
    [[nodiscard]] qint64 byteSize() const noexcept;

    // Revisions -----------------------------------------------------------
    //
    // Every setter compares before it assigns. A different value means the
    // retained pixels describe the old content, the old theme or the old font,
    // so the whole surface becomes dirty. That is deliberately coarse: there is
    // no smaller region that a theme or font change leaves untouched.

    [[nodiscard]] quint64 contentRevision() const noexcept { return contentRevision_; }
    void setContentRevision(quint64 revision);

    [[nodiscard]] quint64 themeRevision() const noexcept { return themeRevision_; }
    void setThemeRevision(quint64 revision);

    [[nodiscard]] const QByteArray& fontSignature() const noexcept
    {
        return fontSignature_;
    }
    void setFontSignature(const QByteArray& signature);

    // The one-call form of the two global appearance inputs above: compares and
    // stores both, marks the whole surface dirty once when either changed, and
    // returns whether it did. A host that repaints on a theme or font change
    // calls this instead of remembering two revisions itself.
    bool syncEnvironment(quint64 themeRevision, const QByteArray& fontSignature);

    // Damage ---------------------------------------------------------------

    // Records one damaged rectangle in logical coordinates. Empty, invalid and
    // non-finite rectangles are ignored; a rectangle is clipped to bounds().
    // Overlapping or touching rectangles merge, more than kMaxDamageRects
    // collapse into their bounding rectangle, and damage above
    // kFullDamageRatio of the surface collapses to the whole surface.
    void addDamage(const QRect& rect);
    void addDamage(const QRectF& rect);

    // Marks the whole surface dirty. The next beginPaint() clears the surface
    // to transparent before painting, so content that disappeared does not
    // survive as a ghost.
    void addAll();

    // Forgets the accumulated damage without painting. Called by the owner
    // after a pass that ran to completion; a failed or partial pass must not
    // call it, or the region it did not paint is claimed as clean.
    void markClean();

    [[nodiscard]] bool isDirty() const noexcept { return full_ || !damage_.isEmpty(); }
    [[nodiscard]] bool isFullDamage() const noexcept { return full_; }

    // The coalesced damage as a region in logical coordinates, clipped to
    // bounds(). Empty when the layer is clean or invalid.
    [[nodiscard]] QRegion damage() const;

    // The union of the retained damage rectangles, or bounds() when the whole
    // surface is dirty. Empty when the layer is clean or invalid.
    [[nodiscard]] QRectF damageBounds() const;

    // The number of retained rectangles; a full layer reports one.
    [[nodiscard]] int damageRectCount() const noexcept;

    // Painting -------------------------------------------------------------

    // Begins a painter over the layer image. The painter is scaled by
    // devicePixelRatio() (so the caller keeps painting in logical coordinates)
    // and clipped to damage(); antialiasing and text antialiasing are on, which
    // is the same pair of hints the direct widget path uses.
    //
    // Returns nullptr when the layer is invalid, not dirty, or when a painter is
    // already active. The returned pointer is owned by the layer and is only
    // valid until endPaint(); the caller must not keep or delete it.
    QPainter* beginPaint();

    // Ends the painter begun by beginPaint(). A no-op when none is active. This
    // does not clear the damage record: only the caller knows whether the pass
    // ran to completion, so it calls markClean() itself.
    void endPaint();

    [[nodiscard]] bool isPainting() const noexcept { return painter_ != nullptr; }

private:
    // Appends one clipped rectangle and re-applies the coalescing policy.
    void mergeDamage(const QRectF& rect);

    // Replaces the list with its bounding rectangle.
    void collapseToBounding();

    // Replaces the list with the whole surface.
    void collapseToFull();

    RenderTarget target_;
    QList<QRectF> damage_;
    bool full_ = false;

    quint64 contentRevision_ = 0;
    quint64 themeRevision_ = 0;
    QByteArray fontSignature_;

    std::unique_ptr<QPainter> painter_;
};

} // namespace md::render
