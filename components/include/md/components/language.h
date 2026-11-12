// Periodicity — Language, a language-switcher chip.
//
// The component paints the current language's native name (English,
// 简体中文, 日本語, …) together with a globe and a chevron. Clicking it opens an
// anchored Menu with one checkable row per language; choosing a row updates
// currentLanguage() and emits languageChanged(code, locale).
//
// The component never installs a translation itself. A host that owns .qm
// files can hand over an installer with setTranslatorInstaller(); the callback
// receives the new language code every time the selection actually changes.
// That keeps resource loading, QTranslator ownership and persistence in the
// application layer.

#pragma once

#include <md/view/view.h>

#include <QIcon>
#include <QList>
#include <QLocale>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <functional>
#include <memory>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;
class QWidget;

namespace md {

class Menu;
class Store;

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

    void openMenu();
    void closeMenu();
    [[nodiscard]] bool isMenuOpen() const;

    // The menu panel, once it has been created. It is a child of the window,
    // not of this widget; the component owns it.
    [[nodiscard]] Menu* menu() const;

    // The built-in common-language list.
    [[nodiscard]] static QList<LanguageEntry> defaultLanguages();

signals:
    void languageChanged(QString code, QLocale locale);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void onThemeChanged() override;

private:
    void rebuildMenu();
    void handleMenuTrigger(int index);
    void syncAppearance();
    void notifyScope();

    QList<LanguageEntry> languages_;
    QString current_;
    QIcon icon_;
    std::function<void(const QString& code)> installer_;
    std::unique_ptr<Adapter> adapter_;
    QPointer<QWidget> scopeHost_;

    QPointer<Menu> menu_;
    bool menuOpen_ = false;
    bool pressed_ = false;
};

} // namespace md
