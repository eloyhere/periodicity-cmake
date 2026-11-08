// Periodicity — scoped theme installation and notification (framework.md §5.3,
// §10.1).

#include <md/theme/theme_scope.h>

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

} // namespace md
