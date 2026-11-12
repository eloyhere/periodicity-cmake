// Periodicity — process-wide Qt translation management.

#include <md/core/i18n.h>

#include <QCoreApplication>
#include <QFile>
#include <QTranslator>

#include <array>
#include <utility>

namespace md::i18n {
namespace {

struct LocaleEntry
{
    const char* code;
};

// Display order is also the order of the Language menu. Keep the source
// language first: Language::defaultLanguages() uses availableLocales().
constexpr std::array<LocaleEntry, 10> kLocales{{
    {"en"},
    {"zh_CN"},
    {"zh_TW"},
    {"ja"},
    {"ko"},
    {"fr"},
    {"de"},
    {"es"},
    {"ru"},
    {"pt_BR"},
}};

QString installedLocale = QStringLiteral("en");

// The .qm files live in a generated qrc object inside the static core archive.
// Nothing else refers to that object, so the linker would drop it; this call is
// what pulls the catalog into every executable that links core. It runs once,
// on first install(), and is harmless when the resource is already registered.
void ensureTranslationResources()
{
    static const bool initialized = [] {
        // The catalog object is compiled into a private object library and
        // merged into this archive. Referencing its initialiser here is what
        // makes the linker keep it; without LinguistTools there is no catalog
        // and the definition is absent, so the call is compiled out.
#if defined(PERIODICITY_HAS_I18N_CATALOGS)
        Q_INIT_RESOURCE(i18n);
#endif
        return true;
    }();
    Q_UNUSED(initialized);
}

// A QTranslator is designed to be kept alive for the lifetime of the process.
// A single instance is reloaded for each language, so removing and installing
// it cannot leak or leave a stale translator behind.
QTranslator translator;
bool translatorInstalled = false;

[[nodiscard]] bool isKnownLocale(const QString& locale)
{
    for (const LocaleEntry& entry : kLocales) {
        if (locale == QLatin1String(entry.code))
            return true;
    }
    return false;
}

[[nodiscard]] QString resourcePath(const QString& locale)
{
    return QStringLiteral(":/i18n/periodicity_%1.qm").arg(locale);
}

// Restores `previous` after a failed load. The caller has already removed the
// translator, so this either reinstalls the previous translation or leaves the
// source language active.
void restoreTranslator(const QString& previous)
{
    if (previous.isEmpty() || previous == QLatin1String("en"))
        return;

    if (!QFile::exists(resourcePath(previous)))
        return;

    if (translator.load(resourcePath(previous)))
        translatorInstalled = QCoreApplication::installTranslator(&translator);
}

} // namespace

QStringList availableLocales()
{
    QStringList locales;
    locales.reserve(static_cast<qsizetype>(kLocales.size()));
    for (const LocaleEntry& entry : kLocales)
        locales.append(QString::fromLatin1(entry.code));
    return locales;
}

QString locale()
{
    return installedLocale;
}

bool install(const QString& newLocale)
{
    if (!isKnownLocale(newLocale))
        return false;

    ensureTranslationResources();

    if (newLocale == QLatin1String("en")) {
        uninstall();
        return true;
    }

    if (translatorInstalled && installedLocale == newLocale)
        return true;

    if (QCoreApplication::instance() == nullptr)
        return false;

    const QString previous = installedLocale;
    if (translatorInstalled) {
        QCoreApplication::removeTranslator(&translator);
        translatorInstalled = false;
    }

    const QString path = resourcePath(newLocale);
    if (!QFile::exists(path)) {
        restoreTranslator(previous);
        return false;
    }

    if (!translator.load(path)) {
        restoreTranslator(previous);
        return false;
    }

    if (!QCoreApplication::installTranslator(&translator)) {
        restoreTranslator(previous);
        return false;
    }

    translatorInstalled = true;
    installedLocale = newLocale;
    return true;
}

void uninstall()
{
    if (translatorInstalled && QCoreApplication::instance() != nullptr)
        QCoreApplication::removeTranslator(&translator);

    translatorInstalled = false;
    installedLocale = QStringLiteral("en");
}

QLocale currentLocale()
{
    return QLocale(installedLocale);
}

} // namespace md::i18n
