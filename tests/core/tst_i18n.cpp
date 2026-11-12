// Unit tests for md::i18n, the QtCore-only translation manager.

#include <md/core/i18n.h>

#include <QCoreApplication>
#include <QLocale>
#include <QStringList>
#include <QTest>

class TestI18n : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();

    void defaultsToSourceLanguage();
    void availableLocalesMirrorTheLanguageTable();
    void unknownLocaleIsRejectedWithoutChangingState();
    void installAndUninstallAreIdempotent();
    void currentLocaleFollowsTheInstalledTranslation();
    void chineseTranslationResolvesFrameworkStrings();
};

void TestI18n::cleanup()
{
    md::i18n::uninstall();
}

void TestI18n::defaultsToSourceLanguage()
{
    QCOMPARE(md::i18n::locale(), QStringLiteral("en"));
    QCOMPARE(md::i18n::currentLocale(), QLocale(QStringLiteral("en")));
}

void TestI18n::availableLocalesMirrorTheLanguageTable()
{
    const QStringList locales = md::i18n::availableLocales();
    QVERIFY(locales.size() >= 5);
    QCOMPARE(locales.first(), QStringLiteral("en"));
    QVERIFY(locales.contains(QStringLiteral("zh_CN")));

    // The table is stable and has no duplicate codes.
    for (const QString& code : locales) {
        QVERIFY(!code.isEmpty());
        QCOMPARE(locales.count(code), 1);
    }
}

void TestI18n::unknownLocaleIsRejectedWithoutChangingState()
{
    QVERIFY(md::i18n::install(QStringLiteral("zh_CN")));
    const QString installed = md::i18n::locale();
    QCOMPARE(installed, QStringLiteral("zh_CN"));

    QVERIFY(!md::i18n::install(QStringLiteral("xx_YY")));
    QCOMPARE(md::i18n::locale(), installed);
    QCOMPARE(md::i18n::currentLocale(), QLocale(QStringLiteral("zh_CN")));
}

void TestI18n::installAndUninstallAreIdempotent()
{
    // The source language has no translator, so both repeated installs and
    // repeated uninstalls are successful no-ops.
    QVERIFY(md::i18n::install(QStringLiteral("en")));
    QVERIFY(md::i18n::install(QStringLiteral("en")));
    QCOMPARE(md::i18n::locale(), QStringLiteral("en"));

    md::i18n::uninstall();
    md::i18n::uninstall();
    QCOMPARE(md::i18n::locale(), QStringLiteral("en"));

    // Installing an actual .qm twice replaces nothing and keeps working.
    QVERIFY(md::i18n::install(QStringLiteral("zh_CN")));
    QVERIFY(md::i18n::install(QStringLiteral("zh_CN")));
    QCOMPARE(md::i18n::locale(), QStringLiteral("zh_CN"));
}

void TestI18n::currentLocaleFollowsTheInstalledTranslation()
{
    QVERIFY(md::i18n::install(QStringLiteral("zh_CN")));
    QCOMPARE(md::i18n::currentLocale(), QLocale(QStringLiteral("zh_CN")));

    QVERIFY(md::i18n::install(QStringLiteral("en")));
    QCOMPARE(md::i18n::currentLocale(), QLocale(QStringLiteral("en")));
}

void TestI18n::chineseTranslationResolvesFrameworkStrings()
{
    QVERIFY(md::i18n::install(QStringLiteral("zh_CN")));

    QCOMPARE(QCoreApplication::translate("Schedule", "No events"),
             QStringLiteral("无事件"));
    QCOMPARE(QCoreApplication::translate("Schedule", "Week of %1").arg(QStringLiteral("2026-03-09")),
             QStringLiteral("2026-03-09 所在周"));
    QCOMPARE(QCoreApplication::translate("Language", "Language: %1").arg(QStringLiteral("中文")),
             QStringLiteral("语言：中文"));
}

QTEST_GUILESS_MAIN(TestI18n)
#include "tst_i18n.moc"
