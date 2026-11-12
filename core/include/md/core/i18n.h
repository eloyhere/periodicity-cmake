// Periodicity — process-wide Qt translation management.
//
// The framework ships one compiled Qt translation per language at
//   :/i18n/periodicity_<locale>.qm
// (for example :/i18n/periodicity_zh_CN.qm). This header is the only place
// that knows that path: a host calls install() once and every Schedule or
// Language instance picks the locale up through currentLocale()/tr().
//
// Only QtCore is involved. There is no QApplication, no widgets include and no
// file-system fallback: the .qm files are expected in the application resource
// bundle, which makes the same code usable in a tool that has no GUI.
//
// The table is deliberately larger than the translations shipped in this
// change: it is the framework's supported-language list. Installing a locale
// whose .qm is not embedded fails without disturbing the installed language,
// which lets a host offer a language row before its translation is built.

#pragma once

#include <QLocale>
#include <QString>
#include <QStringList>

namespace md::i18n {

// Every locale code the framework knows, in display order. The first entry is
// the source language and is always available, even without a .qm file.
[[nodiscard]] QStringList availableLocales();

// The code installed by the last successful install(), or "en" if none.
[[nodiscard]] QString locale();

// Loads :/i18n/periodicity_<locale>.qm and installs it. Unknown locale codes
// and known codes whose .qm is not embedded return false and leave the current
// language untouched. "en" is the source language: install("en") always
// succeeds and simply removes the active translator. Re-installing the active
// locale is a no-op that returns true.
[[nodiscard]] bool install(const QString& locale);

// Removes the active translator and returns to the source language ("en").
void uninstall();

// QLocale(locale()); useful for day/month names, firstDayOfWeek() and dates.
// install() deliberately does not call QLocale::setDefault(), so callers that
// need locale-aware formatting must query this function rather than QLocale().
[[nodiscard]] QLocale currentLocale();

} // namespace md::i18n
