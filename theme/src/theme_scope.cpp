// Periodicity — scoped theme installation and notification (framework.md §5.3,
// §10.1).

#include <md/theme/theme_scope.h>

#include <md/core/logging.h>
#include <md/store/store.h>

#include <QCoreApplication>
#include <QEvent>
#include <QList>
#include <QPointer>
#include <QScopedValueRollback>
#include <QWidget>

#include <utility>

namespace md {

namespace {

// GUI-thread-only re-entrancy guard: an ancestor's theme change refreshes the
// derived scopes below it, and each refresh would otherwise start another walk.
bool g_refreshingDerivedScopes = false;

// Depth-first collection of the widgets below `root`.
//
// QWidget::findChildren() is deliberately not used: QObjectPrivate::
// deleteChildren() nulls a child's slot in its parent's child list *before*
// destroying it, and QWidget::findChildren() asserts on such a null slot
// instead of skipping it. A walk can be re-entered from a host destructor (a
// scope destroyed with its host) or from a themeChanged() slot that deletes a
// widget, so the transient null must be tolerated.
void collectDescendantWidgets(const QObject* root, QList<QWidget*>& widgets)
{
    const QObjectList children = root->children();
    for (QObject* child : children) {
        if (child == nullptr)
            continue;
        if (auto* widget = qobject_cast<QWidget*>(child))
            widgets.append(widget);
        collectDescendantWidgets(child, widgets);
    }
}

// The ThemeScope counterpart of collectDescendantWidgets(). QPointer is used
// because a themeChanged() observer reached while refreshing one scope may
// delete another widget (and with it that widget's scope) before the walk has
// visited it.
void collectDescendantScopes(const QObject* root, QList<QPointer<ThemeScope>>& scopes)
{
    const QObjectList children = root->children();
    for (QObject* child : children) {
        if (child == nullptr)
            continue;
        if (auto* scope = qobject_cast<ThemeScope*>(child))
            scopes.append(scope);
        collectDescendantScopes(child, scopes);
    }
}

void postThemeChangeToSubtree(QWidget* root)
{
    if (root == nullptr || QCoreApplication::instance() == nullptr)
        return;

    QCoreApplication::postEvent(root, new QEvent(QEvent::ThemeChange));

    QList<QWidget*> descendants;
    collectDescendantWidgets(root, descendants);
    for (QWidget* widget : descendants)
        QCoreApplication::postEvent(widget, new QEvent(QEvent::ThemeChange));
}

// Returns the host widget when `object` is still parented to one. A scope that
// was re-parented to a plain QObject has no subtree to notify.
QWidget* widgetHostOf(QObject* object)
{
    return object == nullptr ? nullptr : qobject_cast<QWidget*>(object->parent());
}

} // namespace

ThemeScope::ThemeScope(QWidget* host, Theme theme)
    : QObject(host)
    , host_(host)
    , theme_(std::move(theme))
{
    // QWidget emits destroyed() before it deletes its children, so the flag is
    // already set when this scope is deleted as part of the host's teardown.
    QObject::connect(host, &QObject::destroyed, this, [this] { hostDestroyed_ = true; });
}

ThemeScope::~ThemeScope()
{
    if (!notifyOnDestroy_ || hostDestroyed_)
        return;

    QWidget* host = widgetHostOf(this);
    if (host == nullptr)
        return;

    // A scope controls resolution for the whole host subtree, so removing it
    // changes what cached widgets should paint with. Retire it first: while it
    // is still parented, resolution for its descendants must already skip it,
    // so a derived scope below it re-resolves against the theme that applies
    // once this scope is gone.
    retired_ = true;
    refreshAdaptersInSubtree(host, this);
    postThemeChangeToSubtree(host);
}

ThemeScope* ThemeScope::install(QWidget* host, Theme theme)
{
    if (host == nullptr)
        return nullptr;

    // One scope per host: a second install replaces the first rather than
    // stacking another value on the same widget. Notify before the old scope
    // disappears so live widgets invalidate what they cached from it; the
    // posted event is delivered after the new scope is in place, so it resolves
    // to the replacement. The destructor would post the same events again.
    ThemeScope* existing = find(host);
    if (existing != nullptr) {
        postThemeChangeToSubtree(host);
        existing->notifyOnDestroy_ = false;
        delete existing;
    }

    auto* scope = new ThemeScope(host, std::move(theme));

    // Derived scopes below the host inherit this value.
    refreshAdaptersInSubtree(host, scope);

    return scope;
}

ThemeScope::Adapter::~Adapter() = default;

ThemeScope* ThemeScope::install(QWidget* host, std::unique_ptr<Adapter> adapter)
{
    if (host == nullptr || adapter == nullptr)
        return nullptr;

    ThemeScope* existing = find(host);
    if (existing != nullptr) {
        postThemeChangeToSubtree(host);
        existing->notifyOnDestroy_ = false;
        delete existing;
    }

    auto* scope = new ThemeScope(host, Theme::forWidget(host->parentWidget()));
    scope->adapter_ = std::move(adapter);
    scope->refreshInternal();
    refreshAdaptersInSubtree(host, scope);
    return scope;
}

void ThemeScope::setTheme(Theme theme)
{
    applyTheme(std::move(theme));
}

void ThemeScope::refresh()
{
    refreshInternal();
}

bool ThemeScope::refreshInternal()
{
    if (host_ == nullptr || adapter_ == nullptr)
        return false;

    Theme resolved;
    try {
        // Resolve against what would apply without this scope: the nearest
        // ancestor scope, else the application theme.
        resolved = adapter_->resolve(Theme::forWidget(host_->parentWidget()));
    } catch (...) {
        qCWarning(mdTheme) << "ThemeScope::refresh(): the adapter threw;"
                              " the previous theme was kept";
        return false;
    }

    return applyTheme(std::move(resolved));
}

bool ThemeScope::applyTheme(Theme theme)
{
    if (theme_ == theme)
        return false;

    theme_ = std::move(theme);

    // A themeChanged() slot may destroy the scope; remember that so the
    // notification below does not run on a destroyed object.
    QPointer<ThemeScope> self(this);
    try {
        emit themeChanged();
    } catch (...) {
        qCWarning(mdTheme) << "ThemeScope: a themeChanged observer threw;"
                              " the exception was isolated";
    }

    if (self.isNull())
        return true;

    // Derived scopes inherit this value, so re-resolve them before the host's
    // own ThemeChange is delivered.
    if (QWidget* host = widgetHostOf(this)) {
        refreshAdaptersInSubtree(host, this);
        postThemeChangeToSubtree(host);
    }

    return true;
}

ThemeScope* ThemeScope::find(const QWidget* host)
{
    if (host == nullptr)
        return nullptr;

    ThemeScope* scope = host->findChild<ThemeScope*>(QString(), Qt::FindDirectChildrenOnly);

    // A scope in its destructor no longer applies: resolution has to fall
    // through to the next ancestor while the removal is being propagated.
    return (scope != nullptr && scope->retired_) ? nullptr : scope;
}

void ThemeScope::refreshAdaptersInSubtree(QWidget* root, const ThemeScope* skip)
{
    if (root == nullptr || g_refreshingDerivedScopes)
        return;

    QScopedValueRollback<bool> guard(g_refreshingDerivedScopes, true);

    // findChildren() does not guarantee parents before children, so a pass can
    // resolve a child against a stale inherited theme. Repeat until nothing
    // changes; the bound stops an adapter that keeps flipping its result.
    constexpr int kMaxPasses = 8;
    for (int pass = 0; pass < kMaxPasses; ++pass) {
        bool changed = false;
        QList<QPointer<ThemeScope>> scopes;
        collectDescendantScopes(root, scopes);
        for (const QPointer<ThemeScope>& scope : scopes) {
            if (scope.isNull() || scope == skip || !scope->hasAdapter())
                continue;
            if (scope->refreshInternal())
                changed = true;
        }
        if (!changed)
            break;
    }
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
                         // Slot entry: a throwing theme observer must not
                         // break the store's notification loop.
                         try {
                             if (key == QStringLiteral("theme") && value.canConvert<Theme>())
                                 scope->setTheme(value.value<Theme>());
                         } catch (...) {
                             qCWarning(mdTheme) << "ThemeScope::installFromStore():"
                                                   " a theme observer threw;"
                                                   " the exception was isolated";
                         }
                     });
    return scope;
}

void ThemeScope::publishTheme(const Theme& theme)
{
    store()->setValue(QStringLiteral("theme"), QVariant::fromValue(theme));
}

} // namespace md
