// Periodicity — scoped theme installation and notification (framework.md §5.3,
// §10.1).

#include <md/theme/theme_scope.h>

#include <md/store/store.h>

#include <QCoreApplication>
#include <QEvent>
#include <QList>
#include <QWidget>

#include <utility>

namespace md {

namespace {

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

ThemeScope::ThemeScope(QWidget* host, Theme theme)
    : QObject(host)
    , host_(host)
    , theme_(std::move(theme))
{
}

ThemeScope* ThemeScope::install(QWidget* host, Theme theme)
{
    if (host == nullptr)
        return nullptr;

    // One scope per host: a second install replaces the first rather than
    // stacking another value on the same widget.
    if (ThemeScope* existing = find(host))
        delete existing;

    return new ThemeScope(host, std::move(theme));
}

ThemeScope::Adapter::~Adapter() = default;

ThemeScope* ThemeScope::install(QWidget* host, std::unique_ptr<Adapter> adapter)
{
    if (host == nullptr || adapter == nullptr)
        return nullptr;

    if (ThemeScope* existing = find(host))
        delete existing;

    auto* scope = new ThemeScope(host, Theme::applicationTheme());
    scope->adapter_ = std::move(adapter);
    scope->refresh();
    return scope;
}

void ThemeScope::refresh()
{
    if (host_ == nullptr || adapter_ == nullptr)
        return;

    // Resolve against what would apply without this scope: the nearest
    // ancestor scope, else the application theme.
    const Theme inherited = Theme::forWidget(host_->parentWidget());
    setTheme(adapter_->resolve(inherited));
}

ThemeScope* ThemeScope::find(const QWidget* host)
{
    if (host == nullptr)
        return nullptr;

    return host->findChild<ThemeScope*>(QString(), Qt::FindDirectChildrenOnly);
}

void ThemeScope::setTheme(Theme theme)
{
    if (theme_ == theme)
        return;

    theme_ = std::move(theme);

    emit themeChanged();

    // The host is this object's parent by construction; the cast is a guard
    // against a scope that was re-parented to a plain QObject.
    if (QWidget* host = qobject_cast<QWidget*>(parent()))
        postThemeChangeToSubtree(host);
}

Store* ThemeScope::store()
{
    return Store::open(QStringLiteral("md.theme"));
}

ThemeScope* ThemeScope::installFromStore(QWidget* host)
{
    if (host == nullptr)
        return nullptr;

    Store* values = store();
    Theme theme = Theme::applicationTheme();
    const QVariant stored = values->value(QStringLiteral("theme"));
    if (stored.canConvert<Theme>())
        theme = stored.value<Theme>();

    ThemeScope* scope = install(host, std::move(theme));
    if (scope == nullptr)
        return nullptr;

    QObject::connect(values, &Store::valueChanged, scope,
                     [scope](const QString& key, const QVariant& value) {
                         if (key == QStringLiteral("theme") && value.canConvert<Theme>())
                             scope->setTheme(value.value<Theme>());
                     });
    return scope;
}

void ThemeScope::publishTheme(const Theme& theme)
{
    store()->setValue(QStringLiteral("theme"), QVariant::fromValue(theme));
}

} // namespace md
