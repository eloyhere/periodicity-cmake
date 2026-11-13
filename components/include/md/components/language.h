// Periodicity — Language, a language-switcher chip.
//
// The component paints the current language's native name (English, 中文,
// 日本語, …) together with a globe and a chevron. Clicking it opens a modal
// selector with one row per supported locale; choosing a row installs the
// matching Qt translation through md::i18n and emits localeChanged().
//
// The locale list comes from i18n::availableLocales(), so a row can exist
// before its .qm is embedded: selecting such a row keeps the selector's state
// and warns no louder than i18n::install() returning false. A host that owns
// its own translation files can still call setTranslatorInstaller() to
// supplement (or replace) the built-in loader.
//
// The chip remains usable as the language container of a widget subtree: the
// Adapter/installOn() API is unchanged, and a successful selection notifies the
// scope after the global translation has changed.

#pragma once

#include <md/view/view.h>

#include <QCoreApplication>
#include <QIcon>
#include <QList>
#include <QLocale>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <chrono>
#include <functional>
#include <memory>

class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;
class QWidget;

namespace md {

namespace animation {
class Animator;
}

class PickerSheet;
class Store;
class SwitchingOverlay;

class Language : public View
{
    Q_OBJECT

public:
    struct LanguageEntry
    {
        QString code;
        QString nativeName;
        QLocale locale;
    };

    explicit Language(QWidget* parent = nullptr);
    ~Language() override;

    // The built-in list is used until this is called. When the current code is
    // not present in the new list the first entry becomes current; that
    // configuration change is silent (no languageChanged()).
    void setLanguages(const QList<LanguageEntry>& languages);
    [[nodiscard]] QList<LanguageEntry> languages() const;

    // Selects `code`. Returns false when the code is unknown. Selecting the
    // current code is a no-op and does not emit. A successful change emits
    // languageChanged() and invokes the installer, if one was set.
    bool setCurrentLanguage(const QString& code);
    [[nodiscard]] QString currentLanguage() const;
    // The locale code currently selected ("en", "zh_CN", …). This is the value
    // handed to md::i18n::install(); it is a code, not a QLocale.
    [[nodiscard]] QString currentLocale() const;
    [[nodiscard]] LanguageEntry currentEntry() const;

    void setTranslatorInstaller(std::function<void(const QString& code)> installer);

    // Configuration container --------------------------------------------
    // A Language is also the language container of a widget subtree: install
    // it on a host and descendants resolve their strings through translate().
    // The Adapter is owned by the selector; its direction is applied to the
    // host when the scope is installed or the language changes.
    class Adapter
    {
    public:
        virtual ~Adapter();

        // The string for `key`; the default returns the key itself.
        [[nodiscard]] virtual QString text(const QString& key) const;
        // The BCP 47 tag of the current language, or an empty string.
        [[nodiscard]] virtual QString locale() const;
        [[nodiscard]] virtual Qt::LayoutDirection direction() const;
        // The key→string map published to the "md.language" store so any View
        // can resolve `localized()` without a Language pointer.
        [[nodiscard]] virtual QVariantMap strings() const;
    };

    void setAdapter(std::unique_ptr<Adapter> adapter);
    [[nodiscard]] Adapter* adapter() const noexcept { return adapter_.get(); }

    // Makes this selector the language container of `host` and its subtree.
    void installOn(QWidget* host);
    // The nearest Language container of `context`; walks the parent chain.
    [[nodiscard]] static const Language* forWidget(const QWidget* context);
    // The translated string of `context`, or `fallback` when no container is
    // installed. An empty fallback returns `key`.
    [[nodiscard]] static QString translate(const QWidget* context, const QString& key,
                                           const QString& fallback = QString());
    [[nodiscard]] static Qt::LayoutDirection layoutDirectionFor(const QWidget* context);

    // Store-backed container: the selected code lives in "md.language".
    [[nodiscard]] static Store* store();
    static Language* installFromStore(QWidget* host);
    void publish();

    // Replaces the painted globe glyph.
    void setIcon(const QIcon& icon);
    [[nodiscard]] QIcon icon() const;

    // Opens the modal language selector. The name is kept for hosts that
    // already call openMenu(); the surface is a modal now, not a popup menu.
    void openMenu();
    void closeMenu();
    [[nodiscard]] bool isMenuOpen() const;

    [[nodiscard]] PickerSheet* selector() const noexcept;
    [[nodiscard]] SwitchingOverlay* switchingOverlay() const noexcept;
    [[nodiscard]] QString lastError() const;
    void setManualSwitchClock(bool enabled);
    [[nodiscard]] bool isManualSwitchClock() const noexcept;

    // The chip is the dropdown trigger: 0 is the closed chevron (down) and 1
    // the open one (up). It rotates through the same motion ladder as the
    // sheet, and the label is the committed language, so a cancelled sheet
    // never changes it.
    [[nodiscard]] qreal chevronProgress() const noexcept;
    void setManualAnimationClock(bool enabled);
    [[nodiscard]] bool isManualAnimationClock() const noexcept;
    void advanceAnimation(std::chrono::milliseconds delta);

    // The selector surface, or nullptr until it has been opened once. It is the
    // card of the shared PickerSheet, which is a child of the window.
    [[nodiscard]] QWidget* selectorPanel() const;

    // The built-in common-language list.
    [[nodiscard]] static QList<LanguageEntry> defaultLanguages();

signals:
    // The existing configuration-container signal, kept for hosts that need the
    // QLocale alongside the code.
    void languageChanged(QString code, QLocale locale);
    // Emitted whenever the selected locale actually changes. The argument is
    // the code passed to md::i18n::install().
    void localeChanged(const QString& locale);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;
    void onThemeChanged() override;

private:
    void showSelector();
    void handleSelection(const QString& code);
    void syncAppearance();
    void syncAccessibleDescription();
    void notifyScope();
    void setChevronOpen(bool open);
    void setChevronProgress(qreal progress);
    void startPendingSwitch();

    QList<LanguageEntry> languages_;
    QString current_;
    QIcon icon_;
    std::function<void(const QString& code)> installer_;
    std::unique_ptr<Adapter> adapter_;
    QPointer<QWidget> scopeHost_;

    // The shared modal sheet. It owns the scrim, the card and the entrance and
    // exit motion; the chip only supplies the locale rows and the current code.
    QPointer<PickerSheet> picker_;
    QPointer<SwitchingOverlay> overlay_;
    QString pendingLanguage_;
    bool hasPendingLanguage_ = false;
    bool manualSwitchClock_ = false;
    animation::Animator* chevronAnimator_ = nullptr;
    qreal chevronProgress_ = 0.0;
    // Where the chevron run is heading. The finished handler has to snap to this
    // rather than to menuOpen_: the menu flag flips when the overlay work runs,
    // which is after the close animation finishes, so reading it there snapped a
    // closing chevron back to fully open.
    qreal chevronTarget_ = 0.0;
    bool menuOpen_ = false;
    bool pressed_ = false;
};

} // namespace md
