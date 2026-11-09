// Periodicity — theme bundle and resolution (framework.md §5.3, §10.1–10.3).
//
// The bundle itself is a value type; the only state this file owns is the
// application-wide theme, held in a function-local static so that resolution
// from `forWidget(nullptr)` cannot fail and cannot allocate.

#include <md/theme/theme.h>

#include <md/core/logging.h>
#include <md/theme/theme_scope.h>
#include <md/tokens/state_layer.h>

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QList>
#include <QThread>
#include <QWidget>

#include <utility>

namespace md {

namespace {

// The application-wide theme, created on first use as the baseline light
// theme. `setApplicationTheme` replaces the value; because the storage has
// static lifetime, references handed out by applicationTheme() stay valid for
// the life of the process.
Theme& applicationThemeStorage()
{
    static Theme stored = Theme::baselineLight();
    return stored;
}

// Posts a ThemeChange to `root` and every descendant. Posting (rather than
// sending) is what makes the notification cheap and re-entrancy free: a widget
// that changes its own theme in a changeEvent handler cannot recurse into the
// setter that started the propagation.
void postThemeChangeToSubtree(QWidget* root)
{
    if (root == nullptr)
        return;

    QCoreApplication::postEvent(root, new QEvent(QEvent::ThemeChange));

    const QList<QWidget*> descendants = root->findChildren<QWidget*>();
    for (QWidget* widget : descendants)
        QCoreApplication::postEvent(widget, new QEvent(QEvent::ThemeChange));
}

} // namespace

Theme::Theme(ColorScheme colours, TypeScale type, ShapeScale shapes, MotionScheme motion,
             Density density)
    : colours_(std::move(colours))
    , type_(std::move(type))
    , shapes_(std::move(shapes))
    , motion_(std::move(motion))
    , density_(density)
{
}

Theme Theme::withColours(ColorScheme colours) const
{
    Theme themed = *this;
    themed.colours_ = std::move(colours);
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
    themed.density_ = density;
    return themed;
}

QPalette Theme::toPalette() const
{
    QPalette palette;

    palette.setColor(QPalette::Window, colours_.surface);
    palette.setColor(QPalette::WindowText, colours_.onSurface);
    palette.setColor(QPalette::Base, colours_.surfaceContainerLowest);
    palette.setColor(QPalette::Text, colours_.onSurface);
    palette.setColor(QPalette::Button, colours_.surfaceContainer);
    palette.setColor(QPalette::ButtonText, colours_.onSurface);
    palette.setColor(QPalette::Highlight, colours_.primary);
    palette.setColor(QPalette::HighlightedText, colours_.onPrimary);
    palette.setColor(QPalette::ToolTipBase, colours_.inverseSurface);
    palette.setColor(QPalette::ToolTipText, colours_.inverseOnSurface);
    palette.setColor(QPalette::PlaceholderText, colours_.onSurfaceVariant);

    // Disabled content is the ordinary content colour at the token opacity,
    // not a separately chosen grey (framework.md §3.6).
    QColor disabledText = colours_.onSurface;
    disabledText.setAlphaF(static_cast<float>(kDisabledContentOpacity));
    palette.setColor(QPalette::Disabled, QPalette::Text, disabledText);

    QColor disabledButtonText = colours_.onSurface;
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
    // The scope on the widget itself counts as the nearest scope: a component
    // may theme its own subtree rather than requiring an ancestor to do it.
    for (const QWidget* current = widget; current != nullptr;
         current = current->parentWidget()) {
        if (const ThemeScope* scope = ThemeScope::find(current))
            return scope->theme();
    }

    return applicationTheme();
}

void Theme::setApplicationTheme(Theme theme)
{
    QCoreApplication* application = QCoreApplication::instance();

    // The widget tree is GUI-thread-only. Reject the call rather than post
    // events from a worker thread; the stored theme is left untouched so that
    // the failure leaves a coherent state behind.
    if (application != nullptr && QThread::currentThread() != application->thread()) {
        qCWarning(mdTheme) << "Theme::setApplicationTheme() must be called on the GUI thread;"
                              " the application theme was not changed";
        return;
    }

    applicationThemeStorage() = std::move(theme);

    if (application == nullptr)
        return;

    const auto* widgetApplication = qobject_cast<QApplication*>(application);
    if (widgetApplication == nullptr)
        return;

    const QWidgetList topLevels = QApplication::topLevelWidgets();
    for (QWidget* topLevel : topLevels)
        postThemeChangeToSubtree(topLevel);
}

const Theme& Theme::applicationTheme()
{
    return applicationThemeStorage();
}

} // namespace md
