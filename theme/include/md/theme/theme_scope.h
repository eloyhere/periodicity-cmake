// Periodicity — scoped themes (framework.md §5.3, §10.1).
//
// A ThemeScope attaches a Theme value to the widget subtree rooted at a host
// widget. The scope is a QObject child of the host rather than a widget, so it
// adds no geometry, no paint and no focus participant; it is found by walking
// the widget parent chain in Theme::forWidget.
//
// Scopes nest: the nearest ancestor scope wins, which makes an embedded panel
// or a preview surface possible without touching the rest of the window.

#pragma once

#include <md/theme/theme.h>

#include <QObject>
#include <QtGlobal>

class QWidget;

namespace md {

class ThemeScope : public QObject
{
    Q_OBJECT

public:
    // Creates the scope as a direct child of `host` and returns it.
    //
    // If `host` already has a scope, the old scope is destroyed and replaced,
    // so a host never carries more than one theme. A null host is ignored and
    // yields nullptr: without a widget there is nothing to attach to and
    // nothing that could resolve the scope.
    [[nodiscard]] static ThemeScope* install(QWidget* host, Theme theme);

    // The scope installed directly on `host`, or nullptr. The lookup is
    // deliberately direct-child only: a scope on an ancestor is found by
    // Theme::forWidget walking upwards, not by this function. A null host
    // yields nullptr.
    [[nodiscard]] static ThemeScope* find(const QWidget* host);

    [[nodiscard]] const Theme& theme() const noexcept { return theme_; }

    // Replaces the scope's theme and posts QEvent::ThemeChange to the host and
    // its descendants, so cached paints and resolved token lookups are
    // invalidated. Setting an equal theme is a no-op: no signal and no event.
    //
    // The themeChanged() signal is emitted immediately; the ThemeChange events
    // are posted, so they arrive on the next event-loop turn.
    void setTheme(Theme theme);

signals:
    // Emitted after theme() has changed, before the posted ThemeChange events
    // are delivered.
    void themeChanged();

private:
    explicit ThemeScope(QWidget* host, Theme theme);

    Theme theme_;
};

} // namespace md
