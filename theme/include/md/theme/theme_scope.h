// Periodicity — scoped themes (framework.md §5.3, §10.1).
//
// A ThemeScope attaches a Theme value to the widget subtree rooted at a host
// widget. The scope is a QObject child of the host rather than a widget, so it
// adds no geometry, no paint and no focus participant; it is found by walking
// the widget parent chain in Theme::forWidget.
//
// Scopes nest: the nearest ancestor scope wins, which makes an embedded panel
// or a preview surface possible without touching the rest of the window.
//
// Lifetime. Installing or destroying a scope changes what its subtree resolves
// to, so the host subtree is sent QEvent::ThemeChange before a replaced scope
// is destroyed and when a scope is destroyed. Widgets that cached colours must
// therefore treat `ThemeChange` as "re-resolve"; anything that holds a
// `const Theme&` obtained from a scope has to re-fetch it after that event.
//
// Threading. A scope belongs to its host widget and is GUI-thread-only, like
// the widget tree it lives in.

#pragma once

#include <md/theme/theme.h>

#include <QObject>
#include <QtGlobal>

#include <memory>

class QWidget;

namespace md {

class Store;

class ThemeScope : public QObject
{
    Q_OBJECT

    // Theme::setApplicationTheme() re-resolves the derived scopes of a subtree
    // after publishing a new application theme.
    friend class Theme;

public:
    // A Theme container's adapter: derives the scope's theme from the theme
    // that would otherwise apply (the nearest ancestor scope or the
    // application theme). Installing an adapter makes the scope a derived
    // container rather than a fixed override.
    class Adapter
    {
    public:
        virtual ~Adapter();
        [[nodiscard]] virtual Theme resolve(const Theme& inherited) const = 0;
    };

    // Creates the scope as a direct child of `host` and returns it.
    //
    // If `host` already has a scope, the old scope is destroyed and replaced,
    // so a host never carries more than one theme. QEvent::ThemeChange is
    // posted to the host subtree for the replacement, because resolution for
    // every widget below the host changes. The ThemeChange event is delivered
    // on the next event-loop turn, after the new scope is installed.
    //
    // Any `const Theme&` previously returned by Theme::forWidget() for this
    // subtree (or by the replaced scope's theme()) is invalidated by the
    // replacement.
    //
    // A null host is ignored and yields nullptr: without a widget there is
    // nothing to attach to and nothing that could resolve the scope.
    [[nodiscard]] static ThemeScope* install(QWidget* host, Theme theme);

    // Installs a derived container: the theme is adapter->resolve(inherited)
    // and refresh() re-evaluates it after the adapter or an ancestor changed.
    // A null adapter is ignored and yields nullptr.
    //
    // Derived scopes below the host are refreshed as part of the install.
    [[nodiscard]] static ThemeScope* install(QWidget* host, std::unique_ptr<Adapter> adapter);

    // Destroys the scope and posts QEvent::ThemeChange to its host subtree, so
    // live widgets re-resolve the theme that now applies. A scope replaced by
    // install() is destroyed with that notification suppressed (install posted
    // it already).
    //
    // A scope that is destroyed because its host is being destroyed stays
    // silent: the whole subtree is going away, so there is nothing left to
    // re-resolve, and walking the subtree from a host's teardown would touch
    // child slots that Qt has already cleared. Destroying a scope explicitly
    // while its host stays alive still notifies the host subtree.
    ~ThemeScope() override;

    // The scope installed directly on `host`, or nullptr. The lookup is
    // deliberately direct-child only: a scope on an ancestor is found by
    // Theme::forWidget walking upwards, not by this function. A null host
    // yields nullptr.
    [[nodiscard]] static ThemeScope* find(const QWidget* host);

    [[nodiscard]] const Theme& theme() const noexcept { return theme_; }

    // Replaces the scope's theme and posts QEvent::ThemeChange to the host and
    // its descendants, so cached paints and resolved token lookups are
    // invalidated. Derived (adapter) scopes below the host are re-resolved
    // first, so they inherit the new value. Setting an equal theme is a no-op:
    // no signal, no event and no re-resolution.
    //
    // The themeChanged() signal is emitted immediately; the ThemeChange events
    // are posted, so they arrive on the next event-loop turn. A slot of
    // themeChanged() that throws is isolated: the exception stops that one
    // emission, but the descendant re-resolution and the posted notification
    // still run.
    void setTheme(Theme theme);

    // Re-resolves a derived scope from its parent theme. A fixed scope (no
    // adapter) is unchanged. Emits themeChanged() and posts ThemeChange only
    // when the resolved theme actually differs. A throwing Adapter::resolve()
    // is isolated: the previous theme is kept.
    void refresh();

    // Store-backed scope: the theme lives in the "md.theme" store under the
    // "theme" key, so a host can publish one theme and every bound scope
    // follows it.
    [[nodiscard]] static Store* store();
    static ThemeScope* installFromStore(QWidget* host);
    static void publishTheme(const Theme& theme);

signals:
    // Emitted after theme() has changed, before the posted ThemeChange events
    // are delivered.
    void themeChanged();

private:
    explicit ThemeScope(QWidget* host, Theme theme);

    // The shared body of setTheme()/refresh(): stores `theme` when it differs,
    // notifies the subtree and refreshes derived descendant scopes. Returns
    // true when the theme changed. Guarded against re-entrancy and against a
    // themeChanged() observer deleting the scope.
    bool applyTheme(Theme theme);

    // Re-resolves from the adapter. Returns true when the theme changed.
    bool refreshInternal();

    [[nodiscard]] bool hasAdapter() const noexcept { return adapter_ != nullptr; }

    // Re-resolves every derived scope in `root`'s subtree, parents before
    // children as far as the child order allows (the loop repeats until stable).
    // `skip` is the scope whose own change started the walk. A no-op when a
    // walk is already in progress, which bounds the recursion.
    static void refreshAdaptersInSubtree(QWidget* root, const ThemeScope* skip);

    QWidget* host_ = nullptr;
    std::unique_ptr<Adapter> adapter_;
    Theme theme_;

    // install() clears this on a scope it is about to replace, because it posts
    // the replacement notification itself.
    bool notifyOnDestroy_ = true;

    // Set while the destructor runs: a scope that is being removed must not
    // resolve for its descendants any more, so a derived scope below it
    // re-resolves against the theme that applies after the removal.
    bool retired_ = false;

    // Set when the host emits destroyed(), i.e. while the host is tearing down
    // and will delete this scope as one of its children. The destructor must
    // not walk or notify that subtree: it is being destroyed, and QObject's
    // child list transiently holds null entries during deleteChildren().
    bool hostDestroyed_ = false;
};

} // namespace md
