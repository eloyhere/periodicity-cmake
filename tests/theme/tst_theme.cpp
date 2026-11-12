// Unit tests for the theme module (L2): the bundle value type, scoped
// resolution, application theme notification and the one-way palette bridge.
//
// The suite needs a QApplication because scopes attach to widgets and because
// the notification path posts QEvent::ThemeChange to top-level windows.

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QPalette>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QWidget>

#include <md/core/density.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>
#include <md/tokens/color_scheme.h>
#include <md/tokens/motion.h>
#include <md/tokens/shape.h>
#include <md/tokens/type_scale.h>

using md::ColorScheme;
using md::Density;
using md::MotionScheme;
using md::Shape;
using md::ShapeScale;
using md::Theme;
using md::ThemeScope;
using md::TypeScale;

namespace {

// A widget that counts the ThemeChange events it receives, so a test can
// assert both that a notification happened and that it happened to the whole
// subtree rather than only to the host.
class ProbeWidget : public QWidget
{
public:
    explicit ProbeWidget(QWidget* parent = nullptr) : QWidget(parent) {}

    int themeChanges = 0;

protected:
    void changeEvent(QEvent* event) override
    {
        if (event->type() == QEvent::ThemeChange)
            ++themeChanges;
        QWidget::changeEvent(event);
    }
};

// Tokens with one member changed, used to prove that a withX() call changes
// exactly one subsystem.
TypeScale modifiedType()
{
    TypeScale type = TypeScale::baseline();
    type.headlineMedium.weight = 600;
    return type;
}

ShapeScale modifiedShapes()
{
    ShapeScale shapes = ShapeScale::baseline();
    shapes.medium = Shape::large();
    return shapes;
}

MotionScheme modifiedMotion()
{
    MotionScheme motion = MotionScheme::baseline();
    motion.reducedMotion = true;
    return motion;
}

ColorScheme modifiedColours()
{
    ColorScheme colours = ColorScheme::baselineDark();
    colours.primary = QColor(Qt::red);
    return colours;
}

} // namespace

class TestTheme : public QObject
{
    Q_OBJECT

private slots:
    void baselineThemes();
    void baselineThemesDifferOnlyInColour();
    void withColoursChangesOnlyColours();
    void withTypeChangesOnlyType();
    void withShapesChangesOnlyShapes();
    void withMotionChangesOnlyMotion();
    void withDensityChangesOnlyDensity();
    void copyAndEquality();
    void applicationThemeFallback();
    void applicationThemeNotification();
    void scopeInstallsOnHost();
    void scopeResolvesForChild();
    void nearestScopeWins();
    void scopeInstallReplacesExisting();
    void scopeSetThemeNotifiesSubtree();
    void nullHostIsIgnored();
    void paletteBridge();
    void paletteDisabledContent();

    // Appended boundary tests for the theme findings in issue.md: scope
    // lifetime/notification, adapter re-resolution, density normalisation,
    // the default palette and exception isolation.
    void defaultThemeHasAUsablePalette();
    void densityIsNormalised();
    void scopeDestructionNotifiesLiveWidgets();
    void scopeReplacementNotifiesLiveWidgets();
    void adapterScopeRefreshesAfterAncestorChange();
    void adapterScopeRefreshesAfterApplicationChange();
    void throwingObserverDoesNotBreakSetTheme();
};

void TestTheme::baselineThemes()
{
    const Theme light = Theme::baselineLight();
    const Theme dark = Theme::baselineDark();

    QVERIFY(light != dark);
    QVERIFY(light == Theme::baselineLight());
    QVERIFY(dark == Theme::baselineDark());

    // The baseline theme is fully populated: every subsystem matches its
    // factory and the density is the identity.
    QVERIFY(light.colours() == ColorScheme::baselineLight());
    QVERIFY(dark.colours() == ColorScheme::baselineDark());
    QVERIFY(light.type() == TypeScale::baseline());
    QVERIFY(dark.type() == TypeScale::baseline());
    QVERIFY(light.shapes() == ShapeScale::baseline());
    QVERIFY(dark.shapes() == ShapeScale::baseline());
    QVERIFY(light.motion() == MotionScheme::baseline());
    QVERIFY(dark.motion() == MotionScheme::baseline());
    QCOMPARE(light.density().scale, 1.0);
    QCOMPARE(light.density().fontScale, 1.0);
    QVERIFY(light.density() == Density{});
    QVERIFY(dark.density() == Density{});
}

void TestTheme::baselineThemesDifferOnlyInColour()
{
    const Theme light = Theme::baselineLight();
    const Theme dark = Theme::baselineDark();

    QVERIFY(light.colours() != dark.colours());
    QVERIFY(light.colours().surface != dark.colours().surface);
    QVERIFY(light.colours().onSurface != dark.colours().onSurface);
    QVERIFY(light.colours().primary != dark.colours().primary);

    QVERIFY(light.type() == dark.type());
    QVERIFY(light.shapes() == dark.shapes());
    QVERIFY(light.motion() == dark.motion());
    QVERIFY(light.density() == dark.density());
}

void TestTheme::withColoursChangesOnlyColours()
{
    const Theme base = Theme::baselineLight();
    const ColorScheme colours = modifiedColours();
    const Theme themed = base.withColours(colours);

    QVERIFY(themed.colours() == colours);
    QVERIFY(themed.colours() != base.colours());
    QVERIFY(themed.type() == base.type());
    QVERIFY(themed.shapes() == base.shapes());
    QVERIFY(themed.motion() == base.motion());
    QVERIFY(themed.density() == base.density());
    QVERIFY(themed != base);

    // With the same value the result is indistinguishable from the original.
    QVERIFY(base.withColours(base.colours()) == base);
}

void TestTheme::withTypeChangesOnlyType()
{
    const Theme base = Theme::baselineLight();
    const TypeScale type = modifiedType();
    const Theme themed = base.withType(type);

    QVERIFY(themed.type() == type);
    QVERIFY(themed.type() != base.type());
    QVERIFY(themed.colours() == base.colours());
    QVERIFY(themed.shapes() == base.shapes());
    QVERIFY(themed.motion() == base.motion());
    QVERIFY(themed.density() == base.density());
    QVERIFY(themed != base);
}

void TestTheme::withShapesChangesOnlyShapes()
{
    const Theme base = Theme::baselineLight();
    const ShapeScale shapes = modifiedShapes();
    const Theme themed = base.withShapes(shapes);

    QVERIFY(themed.shapes() == shapes);
    QVERIFY(themed.shapes() != base.shapes());
    QVERIFY(themed.colours() == base.colours());
    QVERIFY(themed.type() == base.type());
    QVERIFY(themed.motion() == base.motion());
    QVERIFY(themed.density() == base.density());
    QVERIFY(themed != base);
}

void TestTheme::withMotionChangesOnlyMotion()
{
    const Theme base = Theme::baselineLight();
    const MotionScheme motion = modifiedMotion();
    const Theme themed = base.withMotion(motion);

    QVERIFY(themed.motion() == motion);
    QVERIFY(themed.motion().reducedMotion);
    QVERIFY(themed.motion() != base.motion());
    QVERIFY(themed.colours() == base.colours());
    QVERIFY(themed.type() == base.type());
    QVERIFY(themed.shapes() == base.shapes());
    QVERIFY(themed.density() == base.density());
    QVERIFY(themed != base);
}

void TestTheme::withDensityChangesOnlyDensity()
{
    const Theme base = Theme::baselineLight();
    const Density density{1.25, 1.5};
    const Theme themed = base.withDensity(density);

    QCOMPARE(themed.density().scale, 1.25);
    QCOMPARE(themed.density().fontScale, 1.5);
    QVERIFY(themed.density() != base.density());
    QVERIFY(themed.colours() == base.colours());
    QVERIFY(themed.type() == base.type());
    QVERIFY(themed.shapes() == base.shapes());
    QVERIFY(themed.motion() == base.motion());
    QVERIFY(themed != base);

    // Density participates in the value semantics rather than being a global.
    QCOMPARE(base.density().dp(4.0), 4.0);
    QCOMPARE(themed.density().dp(4.0), 5.0);
}

void TestTheme::copyAndEquality()
{
    const Theme base = Theme::baselineDark();

    Theme copy = base;
    QVERIFY(copy == base);
    QVERIFY(copy == Theme::baselineDark());

    const Theme assigned = copy;
    QVERIFY(assigned == base);

    copy = copy.withDensity(Density{2.0, 1.0});
    QVERIFY(copy != base);
    QVERIFY(assigned == base);

    // Copies are independent values: changing one never changes the other.
    QCOMPARE(copy.density().scale, 2.0);
    QCOMPARE(assigned.density().scale, 1.0);
}

void TestTheme::applicationThemeFallback()
{
    const Theme original = Theme::applicationTheme();

    // A null widget has no scope to find, so it resolves the application
    // theme, whatever that currently is.
    QVERIFY(Theme::forWidget(nullptr) == Theme::applicationTheme());

    Theme::setApplicationTheme(Theme::baselineDark());
    QVERIFY(Theme::applicationTheme() == Theme::baselineDark());
    QVERIFY(Theme::forWidget(nullptr) == Theme::baselineDark());

    // A widget outside every scope follows the application theme too.
    QWidget plain;
    QVERIFY(Theme::forWidget(&plain) == Theme::baselineDark());

    Theme::setApplicationTheme(original);
    QVERIFY(Theme::applicationTheme() == original);
}

void TestTheme::applicationThemeNotification()
{
    const Theme original = Theme::applicationTheme();

    ProbeWidget host;
    auto* child = new ProbeWidget(&host);

    Theme::setApplicationTheme(Theme::baselineDark());

    // The events are posted, so they arrive on the next event-loop turn.
    QTRY_VERIFY(host.themeChanges >= 1);
    QTRY_VERIFY(child->themeChanges >= 1);

    const int hostChanges = host.themeChanges;
    Theme::setApplicationTheme(original);
    QTRY_VERIFY(host.themeChanges > hostChanges);
    QVERIFY(Theme::applicationTheme() == original);
}

void TestTheme::scopeInstallsOnHost()
{
    QWidget host;
    ThemeScope* scope = ThemeScope::install(&host, Theme::baselineDark());

    QVERIFY(scope != nullptr);
    QCOMPARE(ThemeScope::find(&host), scope);
    QCOMPARE(scope->parent(), &host);
    QVERIFY(scope->theme() == Theme::baselineDark());
    QVERIFY(host.findChildren<ThemeScope*>().size() == 1);
    QVERIFY(Theme::forWidget(&host) == Theme::baselineDark());
}

void TestTheme::scopeResolvesForChild()
{
    const Theme original = Theme::applicationTheme();

    QWidget host;
    QWidget* child = new QWidget(&host);
    QWidget* grandchild = new QWidget(child);

    QVERIFY(ThemeScope::install(&host, Theme::baselineDark()) != nullptr);

    QVERIFY(Theme::forWidget(&host) == Theme::baselineDark());
    QVERIFY(Theme::forWidget(child) == Theme::baselineDark());
    QVERIFY(Theme::forWidget(grandchild) == Theme::baselineDark());

    // Resolution is local to the subtree: an unrelated widget keeps the
    // application theme.
    QWidget outside;
    QVERIFY(Theme::forWidget(&outside) == original);

    // A direct-child lookup does not find the host's scope through a child.
    QCOMPARE(ThemeScope::find(child), nullptr);
}

void TestTheme::nearestScopeWins()
{
    QWidget outerHost;
    QWidget* innerHost = new QWidget(&outerHost);
    QWidget* leaf = new QWidget(innerHost);

    ThemeScope* outer = ThemeScope::install(&outerHost, Theme::baselineDark());
    ThemeScope* inner = ThemeScope::install(innerHost, Theme::baselineLight());

    QCOMPARE(ThemeScope::find(&outerHost), outer);
    QCOMPARE(ThemeScope::find(innerHost), inner);
    QVERIFY(outer->theme() == Theme::baselineDark());
    QVERIFY(inner->theme() == Theme::baselineLight());

    QVERIFY(Theme::forWidget(&outerHost) == Theme::baselineDark());
    QVERIFY(Theme::forWidget(innerHost) == Theme::baselineLight());
    QVERIFY(Theme::forWidget(leaf) == Theme::baselineLight());

    // Removing the inner scope promotes the outer one for the leaf.
    delete inner;
    QCOMPARE(ThemeScope::find(innerHost), nullptr);
    QVERIFY(Theme::forWidget(leaf) == Theme::baselineDark());
}

void TestTheme::scopeInstallReplacesExisting()
{
    QWidget host;

    ThemeScope* first = ThemeScope::install(&host, Theme::baselineLight());
    QVERIFY(first != nullptr);

    // The old scope is destroyed rather than stacked underneath the new one;
    // a QPointer notices the destruction even if the allocator reuses the
    // address for the replacement.
    QPointer<ThemeScope> replaced = first;

    ThemeScope* second = ThemeScope::install(&host, Theme::baselineDark());
    QVERIFY(second != nullptr);
    QVERIFY(replaced.isNull());
    QCOMPARE(ThemeScope::find(&host), second);
    QVERIFY(host.findChildren<ThemeScope*>().size() == 1);
    QVERIFY(second->theme() == Theme::baselineDark());
    QVERIFY(Theme::forWidget(&host) == Theme::baselineDark());
}

void TestTheme::scopeSetThemeNotifiesSubtree()
{
    ProbeWidget host;
    auto* child = new ProbeWidget(&host);
    auto* grandchild = new ProbeWidget(child);

    ThemeScope* scope = ThemeScope::install(&host, Theme::baselineLight());
    QSignalSpy changedSpy(scope, &ThemeScope::themeChanged);

    scope->setTheme(Theme::baselineDark());

    QVERIFY(scope->theme() == Theme::baselineDark());
    QCOMPARE(int(changedSpy.count()), 1);
    QVERIFY(Theme::forWidget(grandchild) == Theme::baselineDark());

    // ThemeChange is posted, so the whole subtree sees it eventually.
    QTRY_VERIFY(host.themeChanges >= 1);
    QTRY_VERIFY(child->themeChanges >= 1);
    QTRY_VERIFY(grandchild->themeChanges >= 1);

    // Setting the same theme is a no-op: no signal and no further events.
    const int hostChanges = host.themeChanges;
    const int childChanges = child->themeChanges;
    scope->setTheme(Theme::baselineDark());
    QCOMPARE(int(changedSpy.count()), 1);
    QTest::qWait(20);
    QCOMPARE(host.themeChanges, hostChanges);
    QCOMPARE(child->themeChanges, childChanges);
}

void TestTheme::nullHostIsIgnored()
{
    QVERIFY(ThemeScope::install(nullptr, Theme::baselineLight()) == nullptr);
    QCOMPARE(ThemeScope::find(nullptr), nullptr);
}

void TestTheme::paletteBridge()
{
    const Theme theme = Theme::baselineDark();
    const QPalette palette = theme.toPalette();

    QVERIFY(palette.color(QPalette::Window) == theme.colours().surface);
    QVERIFY(palette.color(QPalette::WindowText) == theme.colours().onSurface);
    QVERIFY(palette.color(QPalette::Base) == theme.colours().surfaceContainerLowest);
    QVERIFY(palette.color(QPalette::Text) == theme.colours().onSurface);
    QVERIFY(palette.color(QPalette::Button) == theme.colours().surfaceContainer);
    QVERIFY(palette.color(QPalette::ButtonText) == theme.colours().onSurface);
    QVERIFY(palette.color(QPalette::Highlight) == theme.colours().primary);
    QVERIFY(palette.color(QPalette::HighlightedText) == theme.colours().onPrimary);
    QVERIFY(palette.color(QPalette::ToolTipBase) == theme.colours().inverseSurface);
    QVERIFY(palette.color(QPalette::ToolTipText) == theme.colours().inverseOnSurface);
    QVERIFY(palette.color(QPalette::PlaceholderText) == theme.colours().onSurfaceVariant);

    // The bridge is one-way and derived: the two baseline themes yield
    // different palettes.
    const QPalette lightPalette = Theme::baselineLight().toPalette();
    QVERIFY(lightPalette.color(QPalette::Window) != palette.color(QPalette::Window));
}

void TestTheme::paletteDisabledContent()
{
    const Theme theme = Theme::baselineLight();
    const QPalette palette = theme.toPalette();

    const QColor disabledText = palette.color(QPalette::Disabled, QPalette::Text);
    QCOMPARE(disabledText.red(), theme.colours().onSurface.red());
    QCOMPARE(disabledText.green(), theme.colours().onSurface.green());
    QCOMPARE(disabledText.blue(), theme.colours().onSurface.blue());
    QVERIFY(qAbs(disabledText.alphaF() - 0.38) < 0.004);

    const QColor disabledButtonText = palette.color(QPalette::Disabled, QPalette::ButtonText);
    QCOMPARE(disabledButtonText.red(), theme.colours().onSurface.red());
    QCOMPARE(disabledButtonText.green(), theme.colours().onSurface.green());
    QCOMPARE(disabledButtonText.blue(), theme.colours().onSurface.blue());
    QVERIFY(qAbs(disabledButtonText.alphaF() - 0.38) < 0.004);

    // Enabled content stays fully opaque.
    QCOMPARE(palette.color(QPalette::Active, QPalette::Text).alpha(), 255);
    QCOMPARE(palette.color(QPalette::Active, QPalette::ButtonText).alpha(), 255);
    QCOMPARE(palette.color(QPalette::Inactive, QPalette::Text).alpha(), 255);
}

QTEST_MAIN(TestTheme)

#include "tst_theme.moc"

// ---------------------------------------------------------------------------
// Appended boundary tests: default palette, density normalisation, scope
// destruction/replacement notification, adapter re-resolution and exception
// isolation. The tests above are unchanged.
// ---------------------------------------------------------------------------

namespace {

// A derived scope whose adapter keeps the inherited tokens but doubles the
// density, so a test can tell whether re-resolution actually ran.
class DensityAdapter final : public ThemeScope::Adapter
{
public:
    [[nodiscard]] Theme resolve(const Theme& inherited) const override
    {
        return inherited.withDensity(Density{2.0, 2.0});
    }
};

} // namespace

void TestTheme::defaultThemeHasAUsablePalette()
{
    // The default constructor is the metatype placeholder, but it must not be
    // a palette of invalid colours: it is the baseline light theme.
    const Theme defaulted;
    QVERIFY(defaulted.colours() == ColorScheme::baselineLight());
    QVERIFY(defaulted == Theme::baselineLight());
    QCOMPARE(defaulted.density().scale, 1.0);
    QVERIFY(defaulted.toPalette().color(QPalette::Window).rgba()
            == Theme::baselineLight().colours().surface.rgba());

    // An explicitly empty scheme (all members uninitialised) is substituted by
    // the baseline at the Theme boundary.
    const Theme emptyScheme(ColorScheme{}, TypeScale::baseline(), ShapeScale::baseline(),
                            MotionScheme::baseline(), Density{});
    QVERIFY(emptyScheme.colours() == ColorScheme::baselineLight());
    QVERIFY(emptyScheme.toPalette().color(QPalette::Window).isValid());
    QVERIFY(emptyScheme.toPalette().color(QPalette::Text).isValid());
    QVERIFY(emptyScheme.colourPrimary().isValid());
    QVERIFY(emptyScheme.colourPrimaryDark().isValid());
    QVERIFY(emptyScheme.colourSecondaryDark().isValid());
}

void TestTheme::densityIsNormalised()
{
    const Theme base = Theme::baselineLight();

    const Density zero{0.0, 0.0};
    const Theme fromZero = base.withDensity(zero);
    QCOMPARE(fromZero.density().scale, 1.0);
    QCOMPARE(fromZero.density().fontScale, 1.0);
    QCOMPARE(fromZero.density().dp(4.0), 4.0);
    QCOMPARE(fromZero.density().sp(4.0), 4.0);
    QCOMPARE(zero.scale, 0.0); // the caller's value is not modified

    const Theme fromNegative = base.withDensity(Density{-2.0, -0.5});
    QCOMPARE(fromNegative.density().scale, 1.0);
    QCOMPARE(fromNegative.density().fontScale, 1.0);

    const Theme fromNan = base.withDensity(Density{qQNaN(), qQNaN()});
    QCOMPARE(fromNan.density().scale, 1.0);
    QCOMPARE(fromNan.density().fontScale, 1.0);

    const Theme fromHuge = base.withDensity(Density{1.0e9, 1.0e9});
    QCOMPARE(fromHuge.density().scale, 4.0);
    QCOMPARE(fromHuge.density().fontScale, 4.0);

    const Theme fromTiny = base.withDensity(Density{0.01, 0.01});
    QCOMPARE(fromTiny.density().scale, 0.1);
    QCOMPARE(fromTiny.density().fontScale, 0.1);

    // Valid values survive unchanged.
    const Theme valid = base.withDensity(Density{1.25, 1.5});
    QCOMPARE(valid.density().scale, 1.25);
    QCOMPARE(valid.density().fontScale, 1.5);

    // The explicit constructor goes through the same normalisation.
    const Theme constructed(base.colours(), base.type(), base.shapes(), base.motion(),
                            Density{0.0, 8.0});
    QCOMPARE(constructed.density().scale, 1.0);
    QCOMPARE(constructed.density().fontScale, 4.0);
}

void TestTheme::scopeDestructionNotifiesLiveWidgets()
{
    ProbeWidget host;
    auto* child = new ProbeWidget(&host);
    auto* grandchild = new ProbeWidget(child);

    ThemeScope* scope = ThemeScope::install(&host, Theme::baselineDark());
    QVERIFY(scope != nullptr);
    QVERIFY(Theme::forWidget(grandchild) == Theme::baselineDark());

    QTest::qWait(20);
    const int hostChanges = host.themeChanges;
    const int childChanges = child->themeChanges;
    const int grandchildChanges = grandchild->themeChanges;

    // Destroying a scope changes resolution for the whole subtree, so live
    // widgets must be told to re-resolve even though no setTheme() ran.
    delete scope;

    QCOMPARE(ThemeScope::find(&host), nullptr);
    QVERIFY(Theme::forWidget(grandchild) == Theme::applicationTheme());
    QTRY_VERIFY(host.themeChanges > hostChanges);
    QTRY_VERIFY(child->themeChanges > childChanges);
    QTRY_VERIFY(grandchild->themeChanges > grandchildChanges);
}

void TestTheme::scopeReplacementNotifiesLiveWidgets()
{
    ProbeWidget host;
    auto* child = new ProbeWidget(&host);

    QVERIFY(ThemeScope::install(&host, Theme::baselineLight()) != nullptr);
    QTest::qWait(20);
    const int hostChanges = host.themeChanges;
    const int childChanges = child->themeChanges;

    ThemeScope* replacement = ThemeScope::install(&host, Theme::baselineDark());
    QVERIFY(replacement != nullptr);
    QCOMPARE(ThemeScope::find(&host), replacement);
    QVERIFY(Theme::forWidget(child) == Theme::baselineDark());

    QTRY_VERIFY(host.themeChanges > hostChanges);
    QTRY_VERIFY(child->themeChanges > childChanges);
}

void TestTheme::adapterScopeRefreshesAfterAncestorChange()
{
    QWidget outerHost;
    auto* innerHost = new QWidget(&outerHost);
    auto* leaf = new QWidget(innerHost);

    ThemeScope* outer = ThemeScope::install(&outerHost, Theme::baselineDark());
    QVERIFY(outer != nullptr);

    ThemeScope* inner = ThemeScope::install(innerHost, std::make_unique<DensityAdapter>());
    QVERIFY(inner != nullptr);
    QVERIFY(Theme::forWidget(leaf).colours() == ColorScheme::baselineDark());
    QCOMPARE(Theme::forWidget(leaf).density().scale, 2.0);

    // The derived scope re-resolves when an ancestor changes, without an
    // explicit refresh() call and without an event-loop turn: resolution is
    // synchronous, only the notification is posted.
    outer->setTheme(Theme::baselineLight());
    QVERIFY(Theme::forWidget(leaf).colours() == ColorScheme::baselineLight());
    QCOMPARE(Theme::forWidget(leaf).density().scale, 2.0);

    // A manual refresh still works and is a no-op when nothing changed.
    inner->refresh();
    QCOMPARE(Theme::forWidget(leaf).density().scale, 2.0);
}

void TestTheme::adapterScopeRefreshesAfterApplicationChange()
{
    const Theme original = Theme::applicationTheme();

    QWidget host;
    auto* child = new QWidget(&host);
    QVERIFY(ThemeScope::install(&host, std::make_unique<DensityAdapter>()) != nullptr);

    Theme::setApplicationTheme(Theme::baselineDark());
    QVERIFY(Theme::forWidget(child).colours() == ColorScheme::baselineDark());
    QCOMPARE(Theme::forWidget(child).density().scale, 2.0);

    Theme::setApplicationTheme(Theme::baselineLight());
    QVERIFY(Theme::forWidget(child).colours() == ColorScheme::baselineLight());
    QCOMPARE(Theme::forWidget(child).density().scale, 2.0);

    Theme::setApplicationTheme(original);
}

void TestTheme::throwingObserverDoesNotBreakSetTheme()
{
    ProbeWidget host;
    auto* child = new ProbeWidget(&host);

    ThemeScope* scope = ThemeScope::install(&host, Theme::baselineLight());
    QVERIFY(scope != nullptr);
    QTest::qWait(20);
    const int hostChanges = host.themeChanges;

    bool threw = false;
    QObject::connect(scope, &ThemeScope::themeChanged, scope, [&threw] {
        threw = true;
        throw 7; // an arbitrary payload: the isolation catches everything
    });

    scope->setTheme(Theme::baselineDark());

    QVERIFY(threw);
    QVERIFY(scope->theme() == Theme::baselineDark());
    QVERIFY(Theme::forWidget(child) == Theme::baselineDark());

    // The posted notification still reaches the subtree, and a later change
    // still works after the isolated exception.
    QTRY_VERIFY(host.themeChanges > hostChanges);
    scope->setTheme(Theme::baselineLight());
    QVERIFY(scope->theme() == Theme::baselineLight());
}

