// Periodicity — theme bundle and resolution (framework.md §5.3, §10.1–10.3).
//
// The bundle itself is a value type; the only state this file owns is the
// application-wide theme, published as an immutable snapshot behind a mutex so
// a worker can resolve it without racing a GUI-thread theme switch.

#include <md/theme/theme.h>

#include <md/core/logging.h>
#include <md/theme/theme_scope.h>
#include <md/tokens/state_layer.h>

#include <QPointer>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QWidget>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace md {

namespace {

// Bounds for the density stored in a Theme. A zero or negative scale would
// collapse or invert every dp value; an unbounded one would overflow the
// integer geometry that consumes dp(). Density itself stays a plain core value
// type; the Theme boundary is where it is normalised.
constexpr qreal kMinDensityScale = 0.1;
constexpr qreal kMaxDensityScale = 4.0;

qreal normalisedScale(qreal value)
{
    // A non-finite or non-positive scale means "no zoom": treat it as 1.0
    // rather than clamping a broken value to the lower bound.
    if (!qIsFinite(value) || value <= 0.0)
        return 1.0;
    return std::clamp(value, kMinDensityScale, kMaxDensityScale);
}

Density normalisedDensity(Density density)
{
    density.scale = normalisedScale(density.scale);
    density.fontScale = normalisedScale(density.fontScale);
    return density;
}

// A default-constructed ColorScheme has uninitialised (invalid) QColors. The
// theme only requires the core roles to be usable; a scheme that has them is
// taken as-is, anything else falls back to the baseline light palette.
bool hasUsablePalette(const ColorScheme& colours)
{
    return colours.surface.isValid() && colours.onSurface.isValid() && colours.primary.isValid();
}

ColorScheme sanitisedColours(ColorScheme colours)
{
    if (hasUsablePalette(colours))
        return colours;
    return ColorScheme::baselineLight();
}

// The published application theme. `current` is an immutable snapshot and
// `retained` keeps every replaced snapshot alive, so a reference handed out by
// applicationTheme() remains valid for the lifetime of the process — the
// documented contract, and the reason the storage is not freed when it is
// replaced.
struct ApplicationThemeStorage
{
    QMutex mutex;
    std::shared_ptr<const Theme> current;
    std::vector<std::shared_ptr<const Theme>> retained;
};

ApplicationThemeStorage& applicationThemeStorage()
{
    // Deliberately leaked: resolution can run during static destruction, and a
    // returned reference must outlive the widgets that read it.
    static ApplicationThemeStorage* storage = [] {
        auto* created = new ApplicationThemeStorage;
        created->current = std::make_shared<Theme>(Theme::baselineLight());
        return created;
    }();
    return *storage;
}

std::shared_ptr<const Theme> applicationThemeSnapshot()
{
    ApplicationThemeStorage& storage = applicationThemeStorage();
    QMutexLocker locker(&storage.mutex);
    return storage.current;
}

// Publishes `theme` as the application theme. Returns false when it equals the
// current theme, in which case nothing is re-resolved and nothing is posted.
bool publishApplicationTheme(Theme theme)
{
    auto snapshot = std::make_shared<Theme>(std::move(theme));

    ApplicationThemeStorage& storage = applicationThemeStorage();
    QMutexLocker locker(&storage.mutex);
    if (*storage.current == *snapshot)
        return false;

    // Copy (not move) the old snapshot into retained: if the vector allocation
    // threw, `current` would otherwise be left null.
    storage.retained.push_back(storage.current);
    storage.current = std::move(snapshot);
    return true;
}

// Posts a ThemeChange to `root` and every descendant. Posting (rather than
// sending) is what makes the notification cheap and re-entrancy free: a widget
// that changes its own theme in a changeEvent handler cannot recurse into the
// setter that started the propagation.
void postThemeChangeToSubtree(QWidget* root)
{
    if (root == nullptr || QCoreApplication::instance() == nullptr)
        return;

    QCoreApplication::postEvent(root, new QEvent(QEvent::ThemeChange));

    const QList<QWidget*> descendants = root->findChildren<QWidget*>();
    for (QWidget* widget : descendants)
        QCoreApplication::postEvent(widget, new QEvent(QEvent::ThemeChange));
}

} // namespace

Theme::Theme()
    : Theme(ColorScheme::baselineLight(), TypeScale::baseline(), ShapeScale::baseline(),
            MotionScheme::baseline(), Density{})
{
}

Theme::Theme(ColorScheme colours, TypeScale type, ShapeScale shapes, MotionScheme motion,
             Density density)
    : colours_(sanitisedColours(std::move(colours)))
    , type_(std::move(type))
    , shapes_(std::move(shapes))
    , motion_(std::move(motion))
    , density_(normalisedDensity(density))
{
}

Theme Theme::withColours(ColorScheme colours) const
{
    Theme themed = *this;
    themed.colours_ = sanitisedColours(std::move(colours));
    return themed;
}

Theme Theme::withType(TypeScale type) const
{
    Theme themed = *this;
    themed.type_ = std::move(type);
    return themed;
}

Theme Theme::withShapes(ShapeScale shapes) const
{
    Theme themed = *this;
    themed.shapes_ = std::move(shapes);
    return themed;
}

Theme Theme::withMotion(MotionScheme motion) const
{
    Theme themed = *this;
    themed.motion_ = std::move(motion);
    return themed;
}

Theme Theme::withDensity(Density density) const
{
    Theme themed = *this;
    themed.density_ = normalisedDensity(density);
    return themed;
}

QPalette Theme::toPalette() const
{
    // The constructors already sanitise the scheme; this fallback keeps a theme
    // that was produced some other way from mapping invalid colours into a
    // QPalette, which would make stock widgets transparent black.
    const ColorScheme colours =
        hasUsablePalette(colours_) ? colours_ : ColorScheme::baselineLight();

    QPalette palette;

    palette.setColor(QPalette::Window, colours.surface);
    palette.setColor(QPalette::WindowText, colours.onSurface);
    palette.setColor(QPalette::Base, colours.surfaceContainerLowest);
    palette.setColor(QPalette::Text, colours.onSurface);
    palette.setColor(QPalette::Button, colours.surfaceContainer);
    palette.setColor(QPalette::ButtonText, colours.onSurface);
    palette.setColor(QPalette::Highlight, colours.primary);
    palette.setColor(QPalette::HighlightedText, colours.onPrimary);
    palette.setColor(QPalette::ToolTipBase, colours.inverseSurface);
    palette.setColor(QPalette::ToolTipText, colours.inverseOnSurface);
    palette.setColor(QPalette::PlaceholderText, colours.onSurfaceVariant);

    // Disabled content is the ordinary content colour at the token opacity,
    // not a separately chosen grey (framework.md §3.6).
    QColor disabledText = colours.onSurface;
    disabledText.setAlphaF(static_cast<float>(kDisabledContentOpacity));
    palette.setColor(QPalette::Disabled, QPalette::Text, disabledText);

    QColor disabledButtonText = colours.onSurface;
    disabledButtonText.setAlphaF(static_cast<float>(kDisabledContentOpacity));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledButtonText);

    return palette;
}

Theme Theme::baselineLight()
{
    return Theme(ColorScheme::baselineLight(), TypeScale::baseline(), ShapeScale::baseline(),
                 MotionScheme::baseline(), Density{});
}

Theme Theme::baselineDark()
{
    return Theme(ColorScheme::baselineDark(), TypeScale::baseline(), ShapeScale::baseline(),
                 MotionScheme::baseline(), Density{});
}

const Theme& Theme::forWidget(const QWidget* widget)
{
    if (widget != nullptr) {
        // Walking the widget parent chain is GUI-thread-only; resolving the
        // application theme without a widget is not.
        const QCoreApplication* application = QCoreApplication::instance();
        Q_ASSERT(application == nullptr || QThread::currentThread() == application->thread());
        Q_UNUSED(application);

        // The scope on the widget itself counts as the nearest scope: a
        // component may theme its own subtree rather than requiring an
        // ancestor to do it.
        for (const QWidget* current = widget; current != nullptr;
             current = current->parentWidget()) {
            if (const ThemeScope* scope = ThemeScope::find(current))
                return scope->theme();
        }
    }

    return applicationTheme();
}

void Theme::setApplicationTheme(Theme theme)
{
    QCoreApplication* application = QCoreApplication::instance();

    // Posting to the widget tree is GUI-thread-only. Reject the call rather
    // than post events from a worker thread; the stored theme is left untouched
    // so that the failure leaves a coherent state behind.
    if (application != nullptr && QThread::currentThread() != application->thread()) {
        qCWarning(mdTheme) << "Theme::setApplicationTheme() must be called on the GUI thread;"
                              " the application theme was not changed";
        return;
    }

    if (!publishApplicationTheme(std::move(theme)))
        return;

    if (application == nullptr)
        return;

    const auto* widgetApplication = qobject_cast<QApplication*>(application);
    if (widgetApplication == nullptr)
        return;

    // Every scope lives on some widget, so walking all widgets finds the
    // derived scopes even when their host has not been shown yet. Derived
    // scopes must see the new application theme before their hosts process the
    // ThemeChange that follows.
    // Snapshot through QPointer: refreshAdaptersInSubtree() emits themeChanged,
    // and a slot that rebuilds a subtree deletes widgets that are still in
    // allWidgets(). Iterating the raw list would then call ThemeScope::find() on
    // freed memory.
    const QWidgetList widgets = QApplication::allWidgets();
    QList<QPointer<QWidget>> snapshot;
    snapshot.reserve(widgets.size());
    for (QWidget* widget : widgets)
        snapshot.append(QPointer<QWidget>(widget));

    for (const QPointer<QWidget>& entry : snapshot) {
        QWidget* widget = entry.data();
        if (widget == nullptr)
            continue;
        if (ThemeScope::find(widget) != nullptr)
            ThemeScope::refreshAdaptersInSubtree(widget, nullptr);
    }

    const QWidgetList topLevels = QApplication::topLevelWidgets();
    for (QWidget* topLevel : topLevels)
        postThemeChangeToSubtree(topLevel);
}

const Theme& Theme::applicationTheme()
{
    // The snapshot is owned by the storage for the lifetime of the process, so
    // dropping the local shared_ptr does not invalidate the reference.
    const std::shared_ptr<const Theme> snapshot = applicationThemeSnapshot();
    return *snapshot;
}

} // namespace md
