// Tests for the colour-attribute layer on Theme and the theme switcher.

#include <md/components/theme_switcher.h>
#include <md/theme/theme.h>

#include <QApplication>
#include <QSignalSpy>
#include <QTest>

using md::Theme;

class TestThemeColours : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void seedsBecomePrimarySecondaryAccent();
    void attributeSpellingsAreEquivalent();
    void unknownAttributeIsInvalid();
    void derivedColoursAreReadable();
    void baselineReadsBack();
    void darkSeedsProduceADarkSurface();

    void switcherGeometry();
    void switcherClickApplies();
    void switcherKeyboardApplies();
};

void TestThemeColours::initTestCase()
{
    Theme::setApplicationTheme(Theme::baselineLight());
    QApplication::setPalette(Theme::baselineLight().toPalette());
}

void TestThemeColours::cleanupTestCase()
{
    Theme::setApplicationTheme(Theme::baselineLight());
    QApplication::setPalette(Theme::baselineLight().toPalette());
}

void TestThemeColours::seedsBecomePrimarySecondaryAccent()
{
    const QColor primary(0x67, 0x50, 0xA4);
    const QColor secondary(0x62, 0x5B, 0x71);
    const QColor accent(0x7D, 0x52, 0x63);

    const Theme theme = Theme::fromColours(primary, secondary, accent, false);

    QCOMPARE(theme.colours().primary, primary);
    QCOMPARE(theme.colours().secondary, secondary);
    QCOMPARE(theme.colours().tertiary, accent);
    QCOMPARE(theme.colourPrimary(), primary);
    QCOMPARE(theme.colourSecondary(), secondary);
    QCOMPARE(theme.colourAccent(), accent);
    QVERIFY(theme.colours().onPrimary != theme.colours().primary);
}

void TestThemeColours::attributeSpellingsAreEquivalent()
{
    const Theme theme = Theme::fromColours(Qt::blue, Qt::green, Qt::red, false);

    QCOMPARE(theme.colour(QStringLiteral("colorPrimary")), theme.colourPrimary());
    QCOMPARE(theme.colour(QStringLiteral("colourPrimary")), theme.colourPrimary());
    QCOMPARE(theme.colour(QStringLiteral("colorSecondary")), theme.colourSecondary());
    QCOMPARE(theme.colour(QStringLiteral("colourSecond")), theme.colourSecondary());
    QCOMPARE(theme.colour(QStringLiteral("colorAccent")), theme.colourAccent());
    QCOMPARE(theme.colour(QStringLiteral("colourAcent")), theme.colourAccent());
    QCOMPARE(theme.colour(QStringLiteral("colorOnPrimary")), theme.colours().onPrimary);
    QCOMPARE(theme.colour(QStringLiteral("colorSurface")), theme.colours().surface);
    QCOMPARE(theme.colour(QStringLiteral("colorError")), theme.colours().error);
}

void TestThemeColours::unknownAttributeIsInvalid()
{
    const Theme theme = Theme::baselineLight();
    QVERIFY(!theme.colour(QStringLiteral("colorNotAnAttribute")).isValid());
    QVERIFY(!theme.colour(QString()).isValid());
}

void TestThemeColours::derivedColoursAreReadable()
{
    const Theme theme = Theme::fromColours(QColor(0x00, 0x6A, 0x5B), QColor(0x4A, 0x63, 0x6B),
                                           QColor(0x5B, 0x5D, 0x91), false);
    QVERIFY(theme.colours().onSurface.isValid());
    QVERIFY(theme.colours().surface.lightness() > 128);
    QCOMPARE(theme.toPalette().color(QPalette::Window), theme.colours().surface);
    QVERIFY(theme.colours().outlineVariant.isValid());
}

void TestThemeColours::baselineReadsBack()
{
    const Theme light = Theme::baselineLight();
    QCOMPARE(light.colourPrimary(), light.colours().primary);
    QCOMPARE(light.colourSecondary(), light.colours().secondary);
    QCOMPARE(light.colourAccent(), light.colours().tertiary);

    const Theme dark = Theme::baselineDark();
    QCOMPARE(dark.colourPrimary(), dark.colours().primary);
    QVERIFY(dark.colours().surface.lightness() < 128);
}

void TestThemeColours::darkSeedsProduceADarkSurface()
{
    const Theme dark = Theme::fromColours(QColor(0xCF, 0xBC, 0xFF), QColor(0xCC, 0xC2, 0xDC),
                                          QColor(0xEF, 0xB8, 0xC8), true);
    QVERIFY(dark.colours().surface.lightness() < 128);
    QCOMPARE(dark.colourPrimary(), QColor(0xCF, 0xBC, 0xFF));
}

void TestThemeColours::switcherGeometry()
{
    md::ThemeSwitcher switcher;
    switcher.addPreset(QStringLiteral("Baseline"), Theme::baselineLight());
    switcher.addPreset(QStringLiteral("Teal"), QColor(0x00, 0x6A, 0x5B),
                       QColor(0x4A, 0x63, 0x6B), QColor(0x5B, 0x5D, 0x91));
    switcher.resize(switcher.sizeHint());

    QCOMPARE(switcher.presetCount(), 2);
    QVERIFY(switcher.presetRect(0).isValid());
    QVERIFY(switcher.presetRect(2).isNull());

    for (int swatch = 0; swatch < 3; ++swatch) {
        const QRectF chip = switcher.swatchRect(0, swatch);
        QVERIFY(chip.isValid());
        QVERIFY(switcher.presetRect(0).contains(chip.center()));
    }

    QCOMPARE(md::ThemeSwitcher::swatchLabel(0), QStringLiteral("colorPrimary"));
    QCOMPARE(md::ThemeSwitcher::swatchLabel(2), QStringLiteral("colorAccent"));
}

void TestThemeColours::switcherClickApplies()
{
    md::ThemeSwitcher switcher;
    switcher.addPreset(QStringLiteral("One"), Theme::baselineLight());
    switcher.addPreset(QStringLiteral("Two"), QColor(0x00, 0x6A, 0x5B),
                       QColor(0x4A, 0x63, 0x6B), QColor(0x5B, 0x5D, 0x91));
    switcher.resize(switcher.sizeHint());

    QSignalSpy activated(&switcher, &md::ThemeSwitcher::presetActivated);
    QSignalSpy applied(&switcher, &md::ThemeSwitcher::themeApplied);

    const QRectF row = switcher.presetRect(1);
    QTest::mouseClick(&switcher, Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(row.center().x()), int(row.center().y())));

    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated.at(0).at(0).toInt(), 1);
    QCOMPARE(applied.count(), 1);
    QCOMPARE(switcher.currentPreset(), 1);
    QCOMPARE(Theme::applicationTheme().colours().primary, QColor(0x00, 0x6A, 0x5B));
}

void TestThemeColours::switcherKeyboardApplies()
{
    md::ThemeSwitcher switcher;
    switcher.addPreset(QStringLiteral("One"), Theme::baselineLight());
    switcher.addPreset(QStringLiteral("Two"), QColor(0x00, 0x6A, 0x5B),
                       QColor(0x4A, 0x63, 0x6B), QColor(0x5B, 0x5D, 0x91));
    switcher.resize(switcher.sizeHint());

    QSignalSpy activated(&switcher, &md::ThemeSwitcher::presetActivated);

    QTest::keyClick(&switcher, Qt::Key_Down);
    QCOMPARE(switcher.currentPreset(), 1);
    QCOMPARE(activated.count(), 0);

    QTest::keyClick(&switcher, Qt::Key_Return);
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated.at(0).at(0).toInt(), 1);
}

QTEST_MAIN(TestThemeColours)
#include "tst_theme_android.moc"
