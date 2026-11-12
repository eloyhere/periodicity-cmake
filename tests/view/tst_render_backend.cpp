// Tests for the optional GPU rendering path.
//
// ctest runs under the offscreen QPA plugin, which has no usable GL surface.
// These tests therefore assert the capability and fallback contracts only:
//
//   * Software is always available and is the default;
//   * asking for OpenGL on this platform keeps the view on Software and
//     records why in lastError();
//   * the flag walks a View subtree and can be turned back off;
//   * GpuCompositor and GpuCanvas report the backend unavailable and never
//     claim to be using the GPU.
//
// A positive-path test of the texture upload or the shader blit would need a
// real GL context, which this suite does not have. The visual baseline suite
// covers software output; GPU output is covered by the capability branch here.

#include <QApplication>
#include <QList>
#include <QPainter>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QTest>
#include <QtGlobal>

#include <md/components/chart_common.h>
#include <md/components/gpu_canvas.h>
#include <md/view/gpu_compositor.h>
#include <md/view/render_backend.h>
#include <md/view/view.h>

namespace {

// A minimal concrete view: a fixed measured size and no content of its own.
class TestView : public md::View
{
public:
    explicit TestView(QWidget* parent = nullptr)
        : md::View(parent)
    {
    }

protected:
    void measureOverride(md::MeasureSpec width, md::MeasureSpec height) override
    {
        setMeasuredSize(QSizeF(width.resolve(120.0), height.resolve(40.0)));
    }

    void paintContent(QPainter& painter, const QRectF& content) override
    {
        Q_UNUSED(painter);
        Q_UNUSED(content);
    }
};

} // namespace

class TestRenderBackend : public QObject
{
    Q_OBJECT

private slots:
    void capabilityQueryIsConsistent();
    void defaultIsSoftware();
    void unavailableRequestFallsBackAndExplains();
    void subtreeRequestStaysSoftware();
    void disablingRestoresSoftware();
    void compositorFallsBackWithoutGpu();
    void compositorCanHostAView();
    void gpuCanvasFallsBackWithoutGpu();
    void chartCanOptInThroughTheKernel();
    void backendNamesAreStable();
};

void TestRenderBackend::capabilityQueryIsConsistent()
{
    QVERIFY(md::isBackendAvailable(md::RenderBackend::Software));
    QCOMPARE(md::isBackendAvailable(md::RenderBackend::OpenGL),
             md::preferredBackend() == md::RenderBackend::OpenGL);

    // The offscreen platform is the one ctest uses; the compositor and canvas
    // must agree with the capability query rather than optimistically claim GL.
    if (QGuiApplication::platformName().compare(QStringLiteral("offscreen"),
                                                Qt::CaseInsensitive) == 0) {
        QVERIFY(!md::isBackendAvailable(md::RenderBackend::OpenGL));
        QCOMPARE(md::preferredBackend(), md::RenderBackend::Software);
    }
}

void TestRenderBackend::defaultIsSoftware()
{
    TestView view;

    QCOMPARE(view.renderBackend(), md::RenderBackend::Software);
    QVERIFY(!view.isGpuAccelerated());
    QCOMPARE(view.isGpuAccelerationAvailable(),
             md::isBackendAvailable(md::RenderBackend::OpenGL));
    QVERIFY(view.lastError().isEmpty());
}

void TestRenderBackend::unavailableRequestFallsBackAndExplains()
{
    if (md::isBackendAvailable(md::RenderBackend::OpenGL))
        QSKIP("This platform has a GL surface; the fallback branch is untestable here.");

    TestView view;
    view.setRenderBackend(md::RenderBackend::OpenGL);

    QCOMPARE(view.renderBackend(), md::RenderBackend::Software);
    QVERIFY(!view.isGpuAccelerated());
    QVERIFY(!view.lastError().isEmpty());
    QVERIFY(view.lastError().contains(QStringLiteral("GPU acceleration unavailable"),
                                      Qt::CaseInsensitive));

    // setGpuAccelerated() is the same switch; its diagnostic must be the same
    // kind, and it must not leave the view half-enabled.
    TestView other;
    other.setGpuAccelerated(true);
    QVERIFY(!other.isGpuAccelerated());
    QCOMPARE(other.renderBackend(), md::RenderBackend::Software);
    QVERIFY(!other.lastError().isEmpty());
}

void TestRenderBackend::subtreeRequestStaysSoftware()
{
    TestView parent;
    TestView* child = new TestView(&parent);
    TestView* grandchild = new TestView(child);

    parent.setGpuAccelerated(true);

    // On this platform the request cannot be honoured; every level of the
    // subtree must agree with the parent rather than keeping a stale flag.
    QCOMPARE(child->isGpuAccelerated(), parent.isGpuAccelerated());
    QCOMPARE(grandchild->isGpuAccelerated(), parent.isGpuAccelerated());
    QCOMPARE(parent.renderBackend(), md::RenderBackend::Software);
    QCOMPARE(child->renderBackend(), md::RenderBackend::Software);
    QCOMPARE(grandchild->renderBackend(), md::RenderBackend::Software);
    QVERIFY(!parent.lastError().isEmpty());
}

void TestRenderBackend::disablingRestoresSoftware()
{
    TestView view;
    view.setGpuAccelerated(false);
    QVERIFY(!view.isGpuAccelerated());
    QCOMPARE(view.renderBackend(), md::RenderBackend::Software);

    view.setGpuAccelerated(true);
    view.setGpuAccelerated(false);
    QVERIFY(!view.isGpuAccelerated());
    QCOMPARE(view.renderBackend(), md::RenderBackend::Software);
}

void TestRenderBackend::compositorFallsBackWithoutGpu()
{
    QCOMPARE(md::GpuCompositor::isAvailable(),
             md::isBackendAvailable(md::RenderBackend::OpenGL));

    if (md::GpuCompositor::isAvailable())
        QSKIP("A GL surface is available; the no-GL compositor branch is untestable here.");

    md::GpuCompositor compositor;
    auto* view = new TestView(&compositor);
    compositor.addView(view);

    QVERIFY(!compositor.isUsingGpu());
    QCOMPARE(compositor.effectiveBackend(), md::RenderBackend::Software);
    QVERIFY(compositor.views().contains(view));
}

void TestRenderBackend::compositorCanHostAView()
{
    md::GpuCompositor compositor;
    auto* view = new TestView;

    view->setGpuAccelerated(true); // unavailable here, so it stays software
    compositor.addView(view);

    QCOMPARE(view->parentWidget(), &compositor);
    QVERIFY(compositor.views().contains(view));
    QCOMPARE(compositor.effectiveBackend(), md::RenderBackend::Software);
    QVERIFY(!compositor.isUsingGpu());

    compositor.removeView(view);
    QVERIFY(!compositor.views().contains(view));
    delete view;
}

void TestRenderBackend::gpuCanvasFallsBackWithoutGpu()
{
    QCOMPARE(md::GpuCanvas::isAvailable(),
             md::isBackendAvailable(md::RenderBackend::OpenGL));

    if (md::GpuCanvas::isAvailable())
        QSKIP("A GL surface is available; the QPainter-on-GL path needs a visual run.");

    md::GpuCanvas canvas;
    auto* view = new TestView;
    canvas.setView(view);

    QCOMPARE(canvas.view(), view);
    QCOMPARE(view->parentWidget(), &canvas);
    QVERIFY(!canvas.isUsingGpu());
    QCOMPARE(canvas.effectiveBackend(), md::RenderBackend::Software);
}

void TestRenderBackend::chartCanOptInThroughTheKernel()
{
    // Every component inherits the switch from View; ChartBase is the one the
    // gallery uses most, so it stands in for "all components".
    md::ChartBase chart;

    QVERIFY(!chart.isGpuAccelerated());
    QCOMPARE(chart.renderBackend(), md::RenderBackend::Software);

    chart.setGpuAccelerated(true);
    QCOMPARE(chart.renderBackend(), md::RenderBackend::Software);

    if (!md::isBackendAvailable(md::RenderBackend::OpenGL))
        QVERIFY(!chart.lastError().isEmpty());
}

void TestRenderBackend::backendNamesAreStable()
{
    QCOMPARE(QString::fromLatin1(md::backendName(md::RenderBackend::Software)),
             QStringLiteral("Software"));
    QCOMPARE(QString::fromLatin1(md::backendName(md::RenderBackend::OpenGL)),
             QStringLiteral("OpenGL"));
}

QTEST_MAIN(TestRenderBackend)
#include "tst_render_backend.moc"
