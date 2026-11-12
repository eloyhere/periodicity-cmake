// Tests for the configuration containers: Font, ThemeScope and Language.

#include <md/components/font.h>
#include <md/components/language.h>
#include <md/components/text.h>
#include <md/store/store.h>
#include <md/theme/theme.h>
#include <md/theme/theme_scope.h>

#include <QApplication>
#include <QEvent>
#include <QSignalSpy>
#include <QTest>
#include <QWidget>

#include <memory>

namespace {

class DarkAdapter final : public md::ThemeScope::Adapter
{
public:
    md::Theme resolve(const md::Theme& inherited) const override
    {
        Q_UNUSED(inherited);
        return md::Theme::baselineDark();
    }
};

class StringsAdapter final : public md::Language::Adapter
{
public:
    QString text(const QString& key) const override
    {
        return key == QStringLiteral("hello") ? QStringLiteral("Bonjour") : QString();
    }

    QString locale() const override { return QStringLiteral("fr-FR"); }

    Qt::LayoutDirection direction() const override { return Qt::RightToLeft; }

    QVariantMap strings() const override
    {
        return {{QStringLiteral("table.empty"), QStringLiteral("Keine Daten")}};
    }
};

class LanguageSpy : public QObject
{
public:
    int changes = 0;

protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::LanguageChange)
            ++changes;
        return false;
    }
};

} // namespace

class TestConfigContainers : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void fontContainerOverridesFamilyAndScale();
    void nestedFontContainerWins();
    void themeAdapterDerivesFromInherited();
    void languageContainerProvidesStringsAndDirection();
    void viewLocalizedReadsTheLanguageStore();

    void cleanupTestCase();
};

void TestConfigContainers::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestConfigContainers::cleanupTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestConfigContainers::fontContainerOverridesFamilyAndScale()
{
    QWidget host;
    auto* text = new md::Text(QStringLiteral("Hello"), &host);

    const md::Theme& theme = md::Theme::forWidget(&host);
    const md::TextStyle& style = theme.type().bodyLarge;
    const QFont baseline = md::Font::resolve(text, style, theme.density());
    QCOMPARE(baseline.family(), QString(style.family));

    md::Font* font = md::Font::install(&host, QStringLiteral("Courier New"), 2.0);
    QVERIFY(font != nullptr);
    QCOMPARE(md::Font::find(&host), font);
    QCOMPARE(md::Font::forWidget(text), font);

    const QFont resolved = md::Font::resolve(text, style, theme.density());
    QCOMPARE(resolved.family(), QStringLiteral("Courier New"));
    QCOMPARE(resolved.pixelSize(), baseline.pixelSize() * 2);

    font->setScale(1.5);
    QCOMPARE(md::Font::resolve(text, style, theme.density()).pixelSize(),
             qRound(baseline.pixelSize() * 1.5));

    delete font;
    QCOMPARE(md::Font::forWidget(text), nullptr);
    QCOMPARE(md::Font::resolve(text, style, theme.density()).family(),
             QString(style.family));
}

void TestConfigContainers::nestedFontContainerWins()
{
    QWidget outer;
    QWidget* inner = new QWidget(&outer);
    md::Font::install(&outer, QStringLiteral("Courier New"), 1.0);
    md::Font::install(inner, QStringLiteral("Georgia"), 1.0);

    const md::Theme& theme = md::Theme::forWidget(&outer);
    const md::TextStyle& style = theme.type().bodyLarge;

    QCOMPARE(md::Font::resolve(inner, style, theme.density()).family(),
             QStringLiteral("Georgia"));
    QCOMPARE(md::Font::resolve(&outer, style, theme.density()).family(),
             QStringLiteral("Courier New"));
}

void TestConfigContainers::themeAdapterDerivesFromInherited()
{
    QWidget host;
    auto* child = new QWidget(&host);

    QCOMPARE(md::Theme::forWidget(child).colours().surface,
             md::Theme::baselineLight().colours().surface);

    md::ThemeScope* scope =
        md::ThemeScope::install(&host, std::make_unique<DarkAdapter>());
    QVERIFY(scope != nullptr);
    QCOMPARE(md::Theme::forWidget(child).colours().surface,
             md::Theme::baselineDark().colours().surface);

    scope->refresh();
    QCOMPARE(md::Theme::forWidget(child).colours().surface,
             md::Theme::baselineDark().colours().surface);
}

void TestConfigContainers::languageContainerProvidesStringsAndDirection()
{
    QWidget host;
    auto* child = new QWidget(&host);
    host.show();

    auto* language = new md::Language(&host);
    language->setAdapter(std::make_unique<StringsAdapter>());
    language->installOn(&host);

    QCOMPARE(md::Language::forWidget(child), language);
    QCOMPARE(md::Language::translate(child, QStringLiteral("hello")),
             QStringLiteral("Bonjour"));
    QCOMPARE(md::Language::translate(child, QStringLiteral("missing"),
                                     QStringLiteral("fallback")),
             QStringLiteral("fallback"));
    QCOMPARE(md::Language::layoutDirectionFor(child), Qt::RightToLeft);
    QCOMPARE(host.layoutDirection(), Qt::RightToLeft);
    QCOMPARE(md::Language::store()->value(QStringLiteral("strings")).toMap()
                 .value(QStringLiteral("table.empty")).toString(),
             QStringLiteral("Keine Daten"));

    LanguageSpy spy;
    child->installEventFilter(&spy);

    language->setCurrentLanguage(language->languages().value(1).code);
    QCoreApplication::processEvents();
    QVERIFY(spy.changes >= 1);
}

void TestConfigContainers::viewLocalizedReadsTheLanguageStore()
{
    md::Store* language = md::Store::open(QStringLiteral("md.language"));
    language->setValue(QStringLiteral("strings"),
                       QVariantMap{{QStringLiteral("table.empty"),
                                    QStringLiteral("Keine Daten")}});

    md::Text text(QStringLiteral("row"));
    QCOMPARE(text.localized(QStringLiteral("table.empty"), QStringLiteral("No data")),
             QStringLiteral("Keine Daten"));
    QCOMPARE(text.localized(QStringLiteral("missing"), QStringLiteral("Fallback")),
             QStringLiteral("Fallback"));

    language->setValue(QStringLiteral("strings"), QVariantMap());
}

QTEST_MAIN(TestConfigContainers)
#include "tst_config_containers.moc"
