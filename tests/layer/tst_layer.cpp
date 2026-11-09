// Unit tests for the layer module (L3b): composited paint stages.
//
// Like the drawable tests, every layer paints into a plain QImage. That is the
// layer contract — a layer receives a painter, a rectangle and a
// SurfaceContext and never looks for a widget — so the tests need no widget
// tree and run under the offscreen platform.

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTest>
#include <QtGlobal>

#include <md/core/view_state.h>
#include <md/layer/background.h>
#include <md/layer/elevation.h>
#include <md/layer/foreground.h>
#include <md/layer/layer.h>
#include <md/layer/layer_stack.h>
#include <md/layer/surface_context.h>
#include <md/theme/theme.h>
#include <md/tokens/elevation.h>
#include <md/tokens/shape.h>
#include <md/tokens/state_layer.h>

#include <chrono>
#include <memory>
#include <utility>
#include <vector>

using md::Elevation;
using md::Shape;
using md::Theme;
using md::ViewState;
using md::ViewStates;
using md::layer::BackgroundLayer;
using md::layer::ElevationLayer;
using md::layer::FocusRingLayer;
using md::layer::ForegroundLayer;
using md::layer::Layer;
using md::layer::LayerStack;
using md::layer::OutlineLayer;
using md::layer::StateOverlayLayer;
using md::layer::SurfaceContext;

namespace {

constexpr int kImageSize = 160;

QImage newImage()
{
    QImage image(kImageSize, kImageSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}

QImage renderLayer(const Layer& layer, const QRectF& bounds, const SurfaceContext& context)
{
    QImage image = newImage();

    QPainter painter(&image);
    layer.paint(painter, bounds, context);
    painter.end();

    return image;
}

QImage renderStack(const LayerStack& stack, const QRectF& bounds, const SurfaceContext& context)
{
    QImage image = newImage();

    QPainter painter(&image);
    stack.paint(painter, bounds, context);
    painter.end();

    return image;
}

bool isFullyTransparent(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) != 0)
                return false;
        }
    }
    return true;
}

int alphaAt(const QImage& image, int x, int y)
{
    return image.pixelColor(x, y).alpha();
}

// Premultiplied round-tripping at low alpha loses a few colour units, so a
// role-colour comparison allows a little slack; the light and dark roles differ
// by far more than the tolerance.
bool nearColour(const QColor& actual, const QColor& expected, int tolerance = 8)
{
    return qAbs(actual.red() - expected.red()) <= tolerance
        && qAbs(actual.green() - expected.green()) <= tolerance
        && qAbs(actual.blue() - expected.blue()) <= tolerance;
}

// Writes its name into a shared log on every paint, so the stack's traversal
// order is observable without inspecting pixels.
class RecordingLayer : public Layer
{
public:
    RecordingLayer(QString name, std::vector<QString>* log)
        : name_(std::move(name))
        , log_(log)
    {
    }

    void paint(QPainter&, const QRectF&, const SurfaceContext&) const override
    {
        if (log_ != nullptr)
            log_->push_back(name_);
    }

private:
    QString name_;
    std::vector<QString>* log_ = nullptr;
};

// Records the order in which hitTest() is consulted and claims (or declines)
// every point.
class ProbeLayer : public Layer
{
public:
    ProbeLayer(int id, std::vector<int>* order, bool claims)
        : id(id)
        , order_(order)
        , claims_(claims)
    {
    }

    void paint(QPainter&, const QRectF&, const SurfaceContext&) const override {}

    bool hitTest(const QPointF&) const override
    {
        if (order_ != nullptr)
            order_->push_back(id);
        return claims_;
    }

    int id = 0;

private:
    std::vector<int>* order_ = nullptr;
    bool claims_ = false;
};

// An animated layer that accumulates the frames the stack hands it.
class AnimationProbe : public Layer
{
public:
    void paint(QPainter&, const QRectF&, const SurfaceContext&) const override {}

    bool isAnimated() const noexcept override { return true; }

    void advance(std::chrono::milliseconds elapsed) override
    {
        advanced += elapsed.count();
    }

    qint64 advanced = 0;
};

} // namespace

class TestLayer : public QObject
{
    Q_OBJECT

private slots:
    void surfaceContextResolvesDefaults();
    void backgroundFillsWithTonalSurface();
    void elevationLayerDrawsOnlyAboveLevel0();
    void elevationLayerHonoursExplicitConfiguration();
    void stateOverlayOnlyForInteractionStates();
    void focusRingOnlyWhenFocused();
    void outlineLayerStrokesContour();
    void stackPaintsInInsertionOrder();
    void stackHitTestsInReverseOrder();
    void stackInsertClearAndAnimation();
};

void TestLayer::surfaceContextResolvesDefaults()
{
    const Theme theme = Theme::baselineLight();
    SurfaceContext context{theme};

    // The default state is enabled and idle; the colours are derived.
    QVERIFY(context.state == ViewStates(ViewState::Enabled));
    QCOMPARE(context.elevation, Elevation::Level0);
    QVERIFY(!context.containerColour.isValid());
    QVERIFY(!context.contentColour.isValid());

    QCOMPARE(context.resolvedContainerColour(), theme.colours().surface);
    QCOMPARE(context.resolvedContentColour(), theme.colours().onSurface);

    // The container follows the tonal surface mapping of the elevation.
    context.elevation = Elevation::Level3;
    QCOMPARE(context.resolvedContainerColour(),
             theme.colours().surfaceAtElevation(Elevation::Level3));

    // An explicit colour, even a transparent one, wins over the derivation.
    context.containerColour = QColor(Qt::transparent);
    QCOMPARE(context.resolvedContainerColour(), QColor(Qt::transparent));

    context.contentColour = QColor(Qt::red);
    QCOMPARE(context.resolvedContentColour(), QColor(Qt::red));
}

void TestLayer::backgroundFillsWithTonalSurface()
{
    const Theme theme = Theme::baselineLight();
    const QRectF bounds(20.0, 20.0, 120.0, 120.0);

    BackgroundLayer background;

    SurfaceContext context{theme};
    context.shape = Shape::medium();

    // Level0 is the plain surface role, not a tonal step.
    context.elevation = Elevation::Level0;
    const QImage level0 = renderLayer(background, bounds, context);
    QCOMPARE(level0.pixelColor(80, 80),
             theme.colours().surfaceAtElevation(Elevation::Level0));
    QCOMPARE(level0.pixelColor(80, 80), theme.colours().surface);

    // Level3 paints a different tonal surface.
    context.elevation = Elevation::Level3;
    const QImage level3 = renderLayer(background, bounds, context);
    QCOMPARE(level3.pixelColor(80, 80),
             theme.colours().surfaceAtElevation(Elevation::Level3));
    QVERIFY(level3.pixelColor(80, 80) != level0.pixelColor(80, 80));

    // The fill is clipped to the rounded shape: a corner pixel and a pixel
    // outside the bounds stay transparent.
    QCOMPARE(alphaAt(level3, 20, 20), 0);
    QCOMPARE(alphaAt(level3, 2, 80), 0);

    // An explicit container colour bypasses the tonal lookup entirely.
    context.containerColour = QColor(Qt::red);
    QCOMPARE(renderLayer(background, bounds, context).pixelColor(80, 80),
             QColor(Qt::red));
}

void TestLayer::elevationLayerDrawsOnlyAboveLevel0()
{
    const Theme theme = Theme::baselineLight();
    const QRectF bounds(40.0, 40.0, 80.0, 80.0);

    ElevationLayer layer;
    layer.setShape(Shape::medium());

    SurfaceContext context{theme};
    context.shape = Shape::medium();
    context.elevation = Elevation::Level0;

    // Level0 is a strict no-op: not one pixel, and no reserved margin.
    QVERIFY(isFullyTransparent(renderLayer(layer, bounds, context)));
    QCOMPARE(layer.shadowMargins(), QMarginsF());

    context.elevation = Elevation::Level3;
    const QImage image = renderLayer(layer, bounds, context);

    QVERIFY(!isFullyTransparent(image));

    // The mask covers the shape interior...
    QVERIFY(alphaAt(image, 80, 80) > 0);

    // ...and the drop shadow is painted outside the bounds rectangle,
    // unclipped (y = 125 is five pixels below the surface).
    QVERIFY(alphaAt(image, 80, 125) > 0);
}

void TestLayer::elevationLayerHonoursExplicitConfiguration()
{
    const Theme theme = Theme::baselineLight();
    const QRectF bounds(40.0, 40.0, 80.0, 80.0);

    ElevationLayer layer;
    QCOMPARE(layer.shadowMargins(), QMarginsF());

    layer.setElevation(Elevation::Level3);
    layer.setShape(Shape::medium());
    layer.setColour(QColor(Qt::black));

    QCOMPARE(layer.elevation(), Elevation::Level3);
    QVERIFY(layer.shadowMargins().left() > 0.0);

    // The context is flat; the explicit layer configuration wins.
    SurfaceContext flat{theme};
    QVERIFY(layer.shadowMargins(flat).left() > 0.0);
    QVERIFY(alphaAt(renderLayer(layer, bounds, flat), 80, 125) > 0);

    // An explicit Level0 disables the shadow even when the context is
    // elevated: the layer's own setting is authoritative.
    layer.setElevation(Elevation::Level0);

    SurfaceContext raised{theme};
    raised.elevation = Elevation::Level3;
    QVERIFY(isFullyTransparent(renderLayer(layer, bounds, raised)));
    QCOMPARE(layer.shadowMargins(raised), QMarginsF());
}

void TestLayer::stateOverlayOnlyForInteractionStates()
{
    const Theme theme = Theme::baselineLight();
    const QRectF bounds(20.0, 20.0, 120.0, 120.0);

    StateOverlayLayer overlay;

    SurfaceContext context{theme};
    context.shape = Shape::medium();
    context.contentColour = QColor(Qt::black);

    // Enabled but idle: no overlay.
    context.state = ViewStates(ViewState::Enabled);
    QVERIFY(isFullyTransparent(renderLayer(overlay, bounds, context)));

    // Hovered: the 8 % token over the content colour.
    context.state = ViewStates(ViewState::Enabled | ViewState::Hovered);
    const QImage hovered = renderLayer(overlay, bounds, context);
    const int expectedHover = qRound(md::stateLayerOpacity(ViewState::Hovered) * 255.0);
    QVERIFY(qAbs(alphaAt(hovered, 80, 80) - expectedHover) <= 2);
    QCOMPARE(hovered.pixelColor(80, 80).red(), 0);

    // Clipped to the rounded shape.
    QCOMPARE(alphaAt(hovered, 20, 20), 0);

    // Pressed is stronger: the 12 % token.
    context.state = ViewStates(ViewState::Enabled | ViewState::Pressed);
    const QImage pressed = renderLayer(overlay, bounds, context);
    const int expectedPressed = qRound(md::stateLayerOpacity(ViewState::Pressed) * 255.0);
    QVERIFY(qAbs(alphaAt(pressed, 80, 80) - expectedPressed) <= 2);
    QVERIFY(alphaAt(pressed, 80, 80) > alphaAt(hovered, 80, 80));

    // A disabled component (Enabled clear) paints nothing even if a stale
    // hover bit is still set.
    context.state = ViewStates(ViewState::Hovered);
    QVERIFY(isFullyTransparent(renderLayer(overlay, bounds, context)));

    // Without an explicit content colour the overlay uses the theme's
    // onSurface role.
    SurfaceContext themed{theme};
    themed.shape = Shape::medium();
    themed.state = ViewStates(ViewState::Enabled | ViewState::Hovered);
    const QColor pixel = renderLayer(overlay, bounds, themed).pixelColor(80, 80);
    QVERIFY(nearColour(pixel, theme.colours().onSurface));
    QVERIFY(qAbs(pixel.alpha() - expectedHover) <= 2);
}

void TestLayer::focusRingOnlyWhenFocused()
{
    const Theme theme = Theme::baselineLight();
    const QRectF bounds(20.0, 20.0, 120.0, 120.0);

    FocusRingLayer ring;

    SurfaceContext context{theme};
    context.shape = Shape::medium();

    // Enabled and hovered but not focused: no ring.
    context.state = ViewStates(ViewState::Enabled | ViewState::Hovered);
    QVERIFY(isFullyTransparent(renderLayer(ring, bounds, context)));

    // Focused: a 2 dp ring 2 dp outside the shape.
    context.state = ViewStates(ViewState::Enabled | ViewState::Focused);
    const QImage focused = renderLayer(ring, bounds, context);

    // The ring path sits at x = 18 and the 2 dp pen covers 17..19, so the
    // pixel at x = 18 is covered; the surface interior is untouched.
    QVERIFY(alphaAt(focused, 18, 80) > 0);
    QVERIFY(nearColour(focused.pixelColor(18, 80), theme.colours().secondary));
    QCOMPARE(alphaAt(focused, 80, 80), 0);
    QCOMPARE(alphaAt(focused, 2, 80), 0);
}

void TestLayer::outlineLayerStrokesContour()
{
    const Theme theme = Theme::baselineLight();
    const QRectF bounds(20.0, 20.0, 120.0, 120.0);

    OutlineLayer outline;

    SurfaceContext context{theme};
    context.shape = Shape::medium();
    context.state = ViewStates(ViewState::Enabled);

    const QImage image = renderLayer(outline, bounds, context);

    // The 1 dp contour is inset by half the stroke, so it lies inside the
    // bounds: the pixel on the left edge is painted, the interior is not.
    QVERIFY(alphaAt(image, 20, 80) > 0);
    QVERIFY(nearColour(image.pixelColor(20, 80), theme.colours().outlineVariant));
    QCOMPARE(alphaAt(image, 80, 80), 0);
    QCOMPARE(alphaAt(image, 2, 80), 0);
}

void TestLayer::stackPaintsInInsertionOrder()
{
    const Theme theme = Theme::baselineLight();
    const QRectF bounds(20.0, 20.0, 120.0, 120.0);

    SurfaceContext context{theme};

    std::vector<QString> log;
    LayerStack stack;
    stack.add(std::make_unique<RecordingLayer>(QStringLiteral("background"), &log));
    stack.add(std::make_unique<RecordingLayer>(QStringLiteral("state overlay"), &log));
    stack.add(std::make_unique<RecordingLayer>(QStringLiteral("focus ring"), &log));

    QVERIFY(stack.size() == 3);
    QVERIFY(stack.at(0) != nullptr);
    QVERIFY(stack.at(3) == nullptr);

    QImage image = newImage();
    QPainter painter(&image);
    stack.paint(painter, bounds, context);
    painter.end();

    QVERIFY(log.size() == 3);
    QCOMPARE(log[0], QStringLiteral("background"));
    QCOMPARE(log[1], QStringLiteral("state overlay"));
    QCOMPARE(log[2], QStringLiteral("focus ring"));

    // A real stack composites in the same order: the background is laid down
    // first, then the state overlay darkens it.
    LayerStack layered;
    layered.add(std::make_unique<BackgroundLayer>());
    layered.add(std::make_unique<StateOverlayLayer>());

    SurfaceContext plain{theme};
    plain.shape = Shape::medium();
    plain.elevation = Elevation::Level3;

    SurfaceContext hovered = plain;
    hovered.state = ViewStates(ViewState::Enabled | ViewState::Hovered);

    const QImage base = renderStack(layered, bounds, plain);
    const QImage overlay = renderStack(layered, bounds, hovered);
    QVERIFY(overlay.pixelColor(80, 80) != base.pixelColor(80, 80));
}

void TestLayer::stackHitTestsInReverseOrder()
{
    std::vector<int> queried;

    LayerStack stack;
    stack.add(std::make_unique<ProbeLayer>(0, &queried, true));
    stack.add(std::make_unique<ProbeLayer>(1, &queried, true));

    // The topmost layer is asked first and claims the point, so the lower
    // layer is never consulted.
    QVERIFY(stack.hitTest(QPointF(50.0, 50.0)));
    QVERIFY(queried.size() == 1);
    QCOMPARE(queried[0], 1);

    // A declining top layer lets the next one down claim the hit.
    queried.clear();
    LayerStack fallback;
    fallback.add(std::make_unique<ProbeLayer>(0, &queried, true));
    fallback.add(std::make_unique<ProbeLayer>(1, &queried, false));
    QVERIFY(fallback.hitTest(QPointF(50.0, 50.0)));
    QVERIFY(queried.size() == 2);
    QCOMPARE(queried[0], 1);
    QCOMPARE(queried[1], 0);

    // Nobody claims: no hit, and an empty stack reports none either.
    queried.clear();
    LayerStack none;
    none.add(std::make_unique<ProbeLayer>(0, &queried, false));
    QVERIFY(!none.hitTest(QPointF(50.0, 50.0)));
    QVERIFY(!LayerStack().hitTest(QPointF(50.0, 50.0)));
}

void TestLayer::stackInsertClearAndAnimation()
{
    LayerStack stack;
    QVERIFY(stack.empty());
    QVERIFY(!stack.anyAnimated());

    stack.add(std::make_unique<ProbeLayer>(0, nullptr, false));
    stack.insert(0, std::make_unique<ProbeLayer>(1, nullptr, false));
    stack.insert(999, std::make_unique<ProbeLayer>(2, nullptr, false));
    QVERIFY(stack.size() == 3);

    // insert(0) puts a layer at the bottom of the paint order; an index past
    // the end appends.
    const auto* first = dynamic_cast<const ProbeLayer*>(stack.at(0));
    const auto* last = dynamic_cast<const ProbeLayer*>(stack.at(2));
    QVERIFY(first != nullptr);
    QVERIFY(last != nullptr);
    QCOMPARE(first->id, 1);
    QCOMPARE(last->id, 2);

    // A null layer is ignored rather than stored.
    stack.insert(1, nullptr);
    QVERIFY(stack.size() == 3);

    auto animation = std::make_unique<AnimationProbe>();
    auto* animationPointer = animation.get();
    stack.add(std::move(animation));

    QVERIFY(stack.anyAnimated());
    stack.advance(std::chrono::milliseconds(16));
    QCOMPARE(animationPointer->advanced, qint64(16));

    stack.clear();
    QVERIFY(stack.empty());
    QVERIFY(!stack.anyAnimated());
}

QTEST_MAIN(TestLayer)

#include "tst_layer.moc"
