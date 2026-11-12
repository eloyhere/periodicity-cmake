// Unit tests for the Language selector and its md::i18n integration.

#include <md/components/language.h>
#include <md/core/i18n.h>

#include <QApplication>
#include <QSignalSpy>
#include <QTest>
#include <QWidget>

class TestLanguage : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();

    void defaultsToTheInstalledLocale();
    void listsEverySupportedLocale();
    void selectingChineseEmitsLocaleChangedAndInstalls();
    void accessibleDescriptionNamesTheCurrentLanguage();
    void unknownLocaleIsRejectedSilently();
};

void TestLanguage::cleanup()
{
    md::i18n::uninstall();
}

void TestLanguage::defaultsToTheInstalledLocale()
{
    QWidget host;
    md::Language language(&host);

    QCOMPARE(language.currentLocale(), QStringLiteral("en"));
    QCOMPARE(language.currentLanguage(), QStringLiteral("en"));
    QCOMPARE(language.currentEntry().nativeName, QStringLiteral("English"));
}

void TestLanguage::listsEverySupportedLocale()
{
    QWidget host;
    md::Language language(&host);

    const QStringList available = md::i18n::availableLocales();
    const QList<md::Language::LanguageEntry> entries = language.languages();

    QCOMPARE(entries.size(), available.size());
    for (int index = 0; index < available.size(); ++index) {
        QCOMPARE(entries.at(index).code, available.at(index));
        QCOMPARE(entries.at(index).locale, QLocale(available.at(index)));
    }
}

void TestLanguage::selectingChineseEmitsLocaleChangedAndInstalls()
{
    QWidget host;
    md::Language language(&host);
    QSignalSpy localeSpy(&language, &md::Language::localeChanged);

    QVERIFY(language.setCurrentLanguage(QStringLiteral("zh_CN")));
    QCOMPARE(language.currentLocale(), QStringLiteral("zh_CN"));
    QCOMPARE(localeSpy.count(), 1);
    QCOMPARE(localeSpy.at(0).at(0).toString(), QStringLiteral("zh_CN"));

    // The selector installs the language itself; the global manager follows.
    QCOMPARE(md::i18n::locale(), QStringLiteral("zh_CN"));
    QCOMPARE(md::i18n::currentLocale(), QLocale(QStringLiteral("zh_CN")));

    QVERIFY(language.setCurrentLanguage(QStringLiteral("en")));
    QCOMPARE(localeSpy.count(), 2);
    QCOMPARE(md::i18n::locale(), QStringLiteral("en"));
}

void TestLanguage::accessibleDescriptionNamesTheCurrentLanguage()
{
    QWidget host;
    md::Language language(&host);

    QCOMPARE(language.accessibleName(), QStringLiteral("Language"));
    QVERIFY(language.accessibleDescription().contains(QStringLiteral("English")));

    QVERIFY(language.setCurrentLanguage(QStringLiteral("zh_CN")));
    QVERIFY(language.accessibleDescription().contains(QStringLiteral("中文")));
    QCOMPARE(language.accessibleDescription(), QStringLiteral("语言：中文"));
}

void TestLanguage::unknownLocaleIsRejectedSilently()
{
    QWidget host;
    md::Language language(&host);
    QSignalSpy localeSpy(&language, &md::Language::localeChanged);

    QVERIFY(!language.setCurrentLanguage(QStringLiteral("xx_YY")));
    QCOMPARE(language.currentLocale(), QStringLiteral("en"));
    QCOMPARE(localeSpy.count(), 0);
    QCOMPARE(md::i18n::locale(), QStringLiteral("en"));
}

QTEST_MAIN(TestLanguage)
#include "tst_language.moc"
