// Unit tests for the feedback and floating components (framework.md §14).
//
// The suite is offscreen and deterministic: the application theme is pinned to
// the baseline light scheme, hosts are sized explicitly, and pointer and key
// input is delivered with QApplication::sendEvent / QTest::keyClick, exactly as
// the other component suites do.
//
// The assertions target the interaction contract rather than pixels: a menu
// moves its highlight with the arrow keys and flips above an anchor with no
// room below, an alert reports its severity role pair and closes, a
// notification queues messages and advances on timeout or action, every dialog
// family member opens, selects and resolves, and the language chip emits the
// new code and locale.

#include <QApplication>
#include <QWidget>
#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPixmap>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QtTest/QtTest>
#include <QWidget>

#include <chrono>

#include <md/components/alert.h>
#include <md/components/button.h>
#include <md/components/dialog.h>
#include <md/components/language.h>
#include <md/components/switching_overlay.h>
#include <md/components/menu.h>
#include <md/components/notification.h>
#include <md/components/text.h>
#include <md/theme/theme.h>

namespace {

void clickAt(QWidget& widget, const QPointF& position)
{
    const QPointF global = widget.mapToGlobal(position);

    QMouseEvent press(QEvent::MouseButtonPress, position, global, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &press);

    QMouseEvent release(QEvent::MouseButtonRelease, position, global, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &release);
}

QPointF centreOf(const QWidget& widget)
{
    return QPointF(static_cast<qreal>(widget.width()) / 2.0,
                   static_cast<qreal>(widget.height()) / 2.0);
}

md::Menu::MenuItem makeItem(const QString& text, const QString& shortcut = QString(),
                            bool enabled = true)
{
    md::Menu::MenuItem item;
    item.text = text;
    item.shortcut = shortcut;
    item.enabled = enabled;
    return item;
}

} // namespace

class TestFeedbackDialog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void menuKeyboardNavigationAndTrigger();
    void menuSubmenuAndOutsideClose();
    void menuFlipsAboveWhenNoRoomBelow();

    void alertSeverityAndClose();

    void notificationQueueAndAction();

    void confirmDialogAcceptRejectAndScrim();
    void messageDialogSingleAction();
    void singleChoiceDialogSelection();
    void multipleChoiceDialogSelection();
    void customDialogContentAndActions();

    void languageDefaultListAndSwitch();
    void languageMenuSelection();

    void componentsPaintWithoutCrashing();
};

void TestFeedbackDialog::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
    qRegisterMetaType<QLocale>("QLocale");
}

void TestFeedbackDialog::menuKeyboardNavigationAndTrigger()
{
    QWidget host;
    host.resize(800, 600);
    host.show();

    QWidget anchor(&host);
    anchor.setGeometry(100, 100, 160, 40);

    md::Menu menu(&host);
    menu.addItem(makeItem(QStringLiteral("Open"), QStringLiteral("Ctrl+O")));
    menu.addItem(makeItem(QStringLiteral("Disabled"), QString(), false));
    menu.addItem(makeItem(QStringLiteral("Recent")));
    menu.addSeparator();
    menu.addItem(makeItem(QStringLiteral("Quit"), QStringLiteral("Ctrl+Q")));

    QSignalSpy openedSpy(&menu, &md::Menu::opened);
    QSignalSpy closedSpy(&menu, &md::Menu::closed);
    QSignalSpy triggeredSpy(&menu, &md::Menu::itemTriggered);

    menu.setAnchor(&anchor);
    menu.open();

    QVERIFY(menu.isOpen());
    QCOMPARE(openedSpy.count(), 1);
    QCOMPARE(menu.count(), 5);
    QCOMPARE(menu.highlightedIndex(), 0);
    QVERIFY(menu.effectivePlacement() == md::Menu::Placement::Below);
    QVERIFY(menu.panelGlobalRect().width() > 0.0);
    QVERIFY(menu.panelGlobalRect().height() > 0.0);

    // Down skips the disabled row.
    QTest::keyClick(&menu, Qt::Key_Down);
    QCOMPARE(menu.highlightedIndex(), 2);
    QTest::keyClick(&menu, Qt::Key_Home);
    QCOMPARE(menu.highlightedIndex(), 0);
    QTest::keyClick(&menu, Qt::Key_End);
    QCOMPARE(menu.highlightedIndex(), 4);

    QTest::keyClick(&menu, Qt::Key_Return);
    QCOMPARE(triggeredSpy.count(), 1);
    QCOMPARE(triggeredSpy.at(0).at(0).toInt(), 4);
    QCOMPARE(triggeredSpy.at(0).at(1).toString(), QStringLiteral("Quit"));
    QVERIFY(!menu.isOpen());
    QCOMPARE(closedSpy.count(), 1);

    // A checkable item toggles and leaves the menu open.
    md::Menu::MenuItem checkable = makeItem(QStringLiteral("Grid"));
    checkable.checkable = true;
    menu.addItem(checkable);
    QCOMPARE(menu.count(), 6);

    menu.open();
    QVERIFY(menu.isOpen());
    QTest::keyClick(&menu, Qt::Key_End);
    QCOMPARE(menu.highlightedIndex(), 5);
    QTest::keyClick(&menu, Qt::Key_Return);

    QCOMPARE(triggeredSpy.count(), 2);
    QVERIFY(menu.isOpen());
    QVERIFY(menu.isItemChecked(5));
    QCOMPARE(closedSpy.count(), 1);

    QTest::keyClick(&menu, Qt::Key_Escape);
    QVERIFY(!menu.isOpen());
    QCOMPARE(closedSpy.count(), 2);

    // A pointer activation follows the same path as Return.
    menu.open();
    QVERIFY(menu.isOpen());
    clickAt(menu, menu.itemRect(0).center());
    QCOMPARE(triggeredSpy.count(), 3);
    QCOMPARE(triggeredSpy.at(2).at(1).toString(), QStringLiteral("Open"));
    QVERIFY(!menu.isOpen());
    QCOMPARE(closedSpy.count(), 3);
}

void TestFeedbackDialog::menuSubmenuAndOutsideClose()
{
    QWidget host;
    host.resize(800, 600);
    host.show();

    QWidget anchor(&host);
    anchor.setGeometry(80, 80, 140, 36);

    md::Menu menu(&host);

    md::Menu::MenuItem file = makeItem(QStringLiteral("File"));
    file.submenu.push_back(makeItem(QStringLiteral("New"), QStringLiteral("Ctrl+N")));
    file.submenu.push_back(makeItem(QStringLiteral("Open..."), QStringLiteral("Ctrl+O")));
    menu.addItem(file);
    menu.addItem(makeItem(QStringLiteral("Edit")));

    QSignalSpy triggeredSpy(&menu, &md::Menu::itemTriggered);
    QSignalSpy closedSpy(&menu, &md::Menu::closed);

    menu.setAnchor(&anchor);
    menu.open();
    QCOMPARE(menu.highlightedIndex(), 0);

    QTest::keyClick(&menu, Qt::Key_Right);
    QVERIFY(menu.isSubmenuOpen());

    md::Menu* submenu = menu.submenuAt(0);
    QVERIFY(submenu != nullptr);
    QVERIFY(submenu->isOpen());
    QCOMPARE(submenu->count(), 2);
    QCOMPARE(submenu->highlightedIndex(), 0);

    QTest::keyClick(submenu, Qt::Key_Down);
    QCOMPARE(submenu->highlightedIndex(), 1);
    QTest::keyClick(submenu, Qt::Key_Return);

    QCOMPARE(triggeredSpy.count(), 1);
    QCOMPARE(triggeredSpy.at(0).at(0).toInt(), 1);
    QCOMPARE(triggeredSpy.at(0).at(1).toString(), QStringLiteral("Open..."));
    QVERIFY(!menu.isOpen());
    QCOMPARE(closedSpy.count(), 1);

    // Reopen, open the submenu again, and dismiss the whole tree with an
    // outside press.
    menu.open();
    QVERIFY(menu.isOpen());
    QTest::keyClick(&menu, Qt::Key_Right);
    QVERIFY(menu.isSubmenuOpen());

    const QPoint outside = host.mapToGlobal(QPoint(700, 500));
    QMouseEvent press(QEvent::MouseButtonPress, host.mapFromGlobal(outside), outside,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&host, &press);

    QVERIFY(!menu.isOpen());
    QCOMPARE(closedSpy.count(), 2);
}

void TestFeedbackDialog::menuFlipsAboveWhenNoRoomBelow()
{
    QWidget host;
    host.resize(800, 600);
    host.show();

    QWidget anchor(&host);
    anchor.setGeometry(200, 520, 140, 40);

    md::Menu menu(&host);
    for (int index = 0; index < 6; ++index)
        menu.addItem(makeItem(QStringLiteral("Item %1").arg(index)));

    menu.setAnchor(&anchor);
    menu.open();

    QVERIFY(menu.isOpen());
    QVERIFY(menu.effectivePlacement() == md::Menu::Placement::Above);

    const QRectF panel = menu.panelGlobalRect();
    const qreal anchorTop = static_cast<qreal>(anchor.mapToGlobal(QPoint(0, 0)).y());

    QVERIFY(panel.bottom() <= anchorTop);
    QVERIFY(panel.top() >= 0.0);
    QVERIFY(panel.right() <= static_cast<qreal>(host.width()));
}

void TestFeedbackDialog::alertSeverityAndClose()
{
    md::Alert alert;
    alert.setTitle(QStringLiteral("Upload failed"));
    alert.setDescription(QStringLiteral("The file could not be uploaded. Check the connection."));
    alert.setSeverity(md::Alert::Severity::Error);
    alert.setActionText(QStringLiteral("Retry"));
    alert.arrange(QRect(0, 0, 480, 80));
    alert.show();

    const md::ColorScheme& colours = md::Theme::applicationTheme().colours();

    QCOMPARE(alert.severity(), md::Alert::Severity::Error);
    QCOMPARE(alert.containerColour(), colours.errorContainer);
    QCOMPARE(alert.contentColour(), colours.onErrorContainer);
    QVERIFY(alert.actionButton() != nullptr);
    QVERIFY(!alert.actionButton()->isHidden());
    QVERIFY(!alert.closeButtonRect().isEmpty());

    QSignalSpy actionSpy(&alert, &md::Alert::actionClicked);
    clickAt(*alert.actionButton(), centreOf(*alert.actionButton()));
    QCOMPARE(actionSpy.count(), 1);

    QSignalSpy closeSpy(&alert, &md::Alert::closeClicked);
    clickAt(alert, alert.closeButtonRect().center());
    QCOMPARE(closeSpy.count(), 1);
    QVERIFY(alert.isHidden());

    alert.setSeverity(md::Alert::Severity::Warning);
    QCOMPARE(alert.containerColour(), colours.tertiaryContainer);
    QCOMPARE(alert.contentColour(), colours.onTertiaryContainer);

    alert.setSeverity(md::Alert::Severity::Success);
    QCOMPARE(alert.containerColour(), colours.secondaryContainer);
    QCOMPARE(alert.contentColour(), colours.onSecondaryContainer);

    alert.setSeverity(md::Alert::Severity::Info);
    QCOMPARE(alert.containerColour(), colours.surfaceContainerHigh);
    QCOMPARE(alert.contentColour(), colours.onSurface);
}

void TestFeedbackDialog::notificationQueueAndAction()
{
    QWidget host;
    host.resize(640, 480);
    host.show();

    md::Notification notification(&host);
    QSignalSpy actionSpy(&notification, &md::Notification::actionTriggered);
    QSignalSpy closedSpy(&notification, &md::Notification::closed);

    notification.showMessage(QStringLiteral("Message one"), QStringLiteral("Undo"), 5000);

    QVERIFY(notification.isShowing());
    QVERIFY(notification.isVisible());
    QCOMPARE(notification.currentText(), QStringLiteral("Message one"));
    QCOMPARE(notification.currentActionText(), QStringLiteral("Undo"));
    QVERIFY(notification.actionRect().width() > 0.0);
    QVERIFY(host.rect().contains(notification.geometry()));

    notification.showMessage(QStringLiteral("Message two"), QString(), 400);
    QCOMPARE(notification.queuedCount(), 1);
    QCOMPARE(notification.currentText(), QStringLiteral("Message one"));

    clickAt(notification, notification.actionRect().center());
    QCOMPARE(actionSpy.count(), 1);
    QCOMPARE(notification.queuedCount(), 0);
    QCOMPARE(notification.currentText(), QStringLiteral("Message two"));

    QTRY_VERIFY(!notification.isShowing());
    QTRY_COMPARE(closedSpy.count(), 1);

    // A non-positive duration is sticky.
    notification.showMessage(QStringLiteral("Sticky"), QString(), 0);
    QVERIFY(notification.isShowing());
    QTest::qWait(60);
    QVERIFY(notification.isShowing());

    notification.close();
    QVERIFY(!notification.isShowing());
    QVERIFY(!notification.isVisible());
    QCOMPARE(closedSpy.count(), 2);

    // An anchor places the surface against that widget instead of the parent
    // corner.
    QWidget anchor(&host);
    anchor.setGeometry(40, 40, 120, 32);
    notification.setAnchor(&anchor);
    notification.showMessage(QStringLiteral("Anchored"));
    QVERIFY(notification.isShowing());
    QVERIFY(notification.geometry().top() >= anchor.geometry().bottom());

    notification.close();
    QVERIFY(!notification.isShowing());
    QCOMPARE(closedSpy.count(), 3);
    notification.setAnchor(nullptr);
}

void TestFeedbackDialog::confirmDialogAcceptRejectAndScrim()
{
    QWidget host;
    host.resize(640, 480);
    host.show();

    md::ConfirmDialog dialog(&host);
    dialog.setTitle(QStringLiteral("Delete item"));
    dialog.setText(QStringLiteral("This action cannot be undone."));

    QCOMPARE(dialog.actionCount(), 2);
    QCOMPARE(dialog.confirmText(), QStringLiteral("确认"));
    QCOMPARE(dialog.cancelText(), QStringLiteral("取消"));

    QSignalSpy acceptedSpy(&dialog, &md::Dialog::accepted);
    QSignalSpy rejectedSpy(&dialog, &md::Dialog::rejected);

    dialog.open();
    QVERIFY(dialog.isOpen());
    QCOMPARE(dialog.geometry(), host.rect());
    QVERIFY(!dialog.surfaceRect().isEmpty());
    QVERIFY(host.rect().contains(dialog.surfaceRect().toRect()));
    QVERIFY(dialog.bodyRect().height() > 0.0);
    QVERIFY(dialog.bodyRect().bottom() <= dialog.surfaceRect().bottom());

    // The overlay follows the parent's resize.
    host.resize(720, 520);
    QCoreApplication::processEvents();
    QCOMPARE(dialog.geometry(), host.rect());
    QCOMPARE(dialog.surfaceRect().center(), QRectF(host.rect()).center());

    clickAt(*dialog.actionButton(1), centreOf(*dialog.actionButton(1)));
    QCOMPARE(acceptedSpy.count(), 1);
    QVERIFY(!dialog.isOpen());
    QVERIFY(dialog.isHidden());

    dialog.open();
    QVERIFY(dialog.isOpen());
    clickAt(*dialog.actionButton(0), centreOf(*dialog.actionButton(0)));
    QCOMPARE(rejectedSpy.count(), 1);
    QVERIFY(!dialog.isOpen());

    // Escape rejects.
    dialog.open();
    QTest::keyClick(&dialog, Qt::Key_Escape);
    QCOMPARE(rejectedSpy.count(), 2);
    QVERIFY(!dialog.isOpen());

    // A scrim press rejects when dismissal is enabled...
    dialog.setScrimDismissible(true);
    dialog.open();
    QVERIFY(dialog.isOpen());
    clickAt(dialog, QPointF(4.0, 4.0));
    QCOMPARE(rejectedSpy.count(), 3);
    QVERIFY(!dialog.isOpen());

    // ...and is ignored when it is disabled.
    dialog.setScrimDismissible(false);
    dialog.open();
    clickAt(dialog, QPointF(4.0, 4.0));
    QVERIFY(dialog.isOpen());
    dialog.close();
    QVERIFY(!dialog.isOpen());
}

void TestFeedbackDialog::messageDialogSingleAction()
{
    QWidget host;
    host.resize(480, 360);
    host.show();

    md::MessageDialog dialog(&host);
    dialog.setTitle(QStringLiteral("Saved"));
    dialog.setMessage(QStringLiteral("All changes are stored locally."));

    QCOMPARE(dialog.actionCount(), 1);
    QCOMPARE(dialog.buttonText(), QStringLiteral("知道了"));
    QCOMPARE(dialog.message(), QStringLiteral("All changes are stored locally."));

    QSignalSpy acceptedSpy(&dialog, &md::Dialog::accepted);
    dialog.open();
    QVERIFY(dialog.isOpen());
    QVERIFY(dialog.bodyRect().height() > 0.0);

    clickAt(*dialog.actionButton(0), centreOf(*dialog.actionButton(0)));
    QCOMPARE(acceptedSpy.count(), 1);
    QVERIFY(!dialog.isOpen());
}

void TestFeedbackDialog::singleChoiceDialogSelection()
{
    QWidget host;
    host.resize(600, 420);
    host.show();

    md::SingleChoiceDialog dialog(&host);
    dialog.setTitle(QStringLiteral("Pick one"));
    dialog.setItems(QStringList({QStringLiteral("Alpha"), QStringLiteral("Beta"),
                                 QStringLiteral("Gamma")}));

    QSignalSpy changedSpy(&dialog, &md::SingleChoiceDialog::selectionChanged);
    QSignalSpy acceptedSpy(&dialog, &md::Dialog::accepted);

    QCOMPARE(dialog.selectedIndex(), -1);
    QVERIFY(dialog.selectedValue().isEmpty());

    dialog.open();
    QVERIFY(dialog.isOpen());
    QVERIFY(!dialog.itemRect(0).isEmpty());
    QCOMPARE(dialog.itemRect(1).height(), 48.0);

    clickAt(dialog, dialog.itemRect(1).center());
    QCOMPARE(dialog.selectedIndex(), 1);
    QCOMPARE(dialog.selectedValue(), QStringLiteral("Beta"));
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(changedSpy.at(0).at(0).toInt(), 1);

    dialog.setSelectedIndex(2);
    QCOMPARE(dialog.selectedValue(), QStringLiteral("Gamma"));
    QCOMPARE(changedSpy.count(), 2);

    clickAt(*dialog.actionButton(1), centreOf(*dialog.actionButton(1)));
    QCOMPARE(acceptedSpy.count(), 1);
    QCOMPARE(dialog.selectedIndex(), 2);
}

void TestFeedbackDialog::multipleChoiceDialogSelection()
{
    QWidget host;
    host.resize(600, 420);
    host.show();

    md::MultipleChoiceDialog dialog(&host);
    dialog.setTitle(QStringLiteral("Choose colours"));
    dialog.setItems(QStringList({QStringLiteral("Red"), QStringLiteral("Green"),
                                 QStringLiteral("Blue")}));

    QSignalSpy changedSpy(&dialog, &md::MultipleChoiceDialog::selectionChanged);
    dialog.open();
    QVERIFY(dialog.isOpen());
    QVERIFY(!dialog.itemRect(0).isEmpty());

    dialog.setItemChecked(0, true);
    dialog.setItemChecked(2, true);

    const QList<int> firstExpected{0, 2};
    QVERIFY(dialog.selectedIndices() == firstExpected);
    QCOMPARE(dialog.selectedValues(),
             QStringList({QStringLiteral("Red"), QStringLiteral("Blue")}));
    QCOMPARE(changedSpy.count(), 2);

    clickAt(dialog, dialog.itemRect(1).center());
    QVERIFY(dialog.isItemChecked(1));
    const QList<int> allExpected{0, 1, 2};
    QVERIFY(dialog.selectedIndices() == allExpected);

    clickAt(dialog, dialog.itemRect(0).center());
    QVERIFY(!dialog.isItemChecked(0));
    const QList<int> remainingExpected{1, 2};
    QVERIFY(dialog.selectedIndices() == remainingExpected);

    QSignalSpy acceptedSpy(&dialog, &md::Dialog::accepted);
    clickAt(*dialog.actionButton(1), centreOf(*dialog.actionButton(1)));
    QCOMPARE(acceptedSpy.count(), 1);
    QCOMPARE(dialog.selectedValues(),
             QStringList({QStringLiteral("Green"), QStringLiteral("Blue")}));
}

void TestFeedbackDialog::customDialogContentAndActions()
{
    QWidget host;
    host.resize(640, 480);
    host.show();

    md::Text content(QStringLiteral("A regular View placed in the dialog body."));
    content.setWordWrap(true);

    md::CustomDialog dialog(&host);
    dialog.setTitle(QStringLiteral("Custom"));
    dialog.setContent(&content);

    QCOMPARE(dialog.content(), &content);

    const int detailsIndex = dialog.addAction(QStringLiteral("Details"),
                                              md::CustomDialog::ActionRole::Custom);
    const int acceptIndex =
        dialog.addAction(QStringLiteral("OK"), md::CustomDialog::ActionRole::Accept);

    QCOMPARE(detailsIndex, 0);
    QCOMPARE(acceptIndex, 1);
    QCOMPARE(dialog.actionCount(), 2);

    QSignalSpy actionSpy(&dialog, &md::Dialog::actionTriggered);
    QSignalSpy acceptedSpy(&dialog, &md::Dialog::accepted);

    dialog.open();
    QVERIFY(dialog.isOpen());
    QVERIFY(dialog.bodyRect().height() > 0.0);
    QVERIFY(!content.geometry().isEmpty());
    QVERIFY(dialog.surfaceRect().contains(QRectF(content.geometry())));

    clickAt(*dialog.actionButton(0), centreOf(*dialog.actionButton(0)));
    QCOMPARE(actionSpy.count(), 1);
    QCOMPARE(actionSpy.at(0).at(0).toInt(), 0);
    QVERIFY(dialog.isOpen());
    QVERIFY(acceptedSpy.isEmpty());

    clickAt(*dialog.actionButton(1), centreOf(*dialog.actionButton(1)));
    QCOMPARE(actionSpy.count(), 2);
    QCOMPARE(acceptedSpy.count(), 1);
    QVERIFY(!dialog.isOpen());
}

void TestFeedbackDialog::languageDefaultListAndSwitch()
{
    QWidget host;
    host.resize(640, 480);
    host.show();

    md::Language language(&host);
    QVERIFY(language.languages().size() >= 5);
    QCOMPARE(language.currentLanguage(), QStringLiteral("en"));

    language.arrange(QRect(24, 24, 180, 40));
    language.show();

    QList<md::Language::LanguageEntry> entries;
    entries.append({QStringLiteral("en"), QStringLiteral("English"),
                    QLocale(QStringLiteral("en"))});
    entries.append({QStringLiteral("zh_CN"), QStringLiteral("简体中文"),
                    QLocale(QStringLiteral("zh_CN"))});
    entries.append({QStringLiteral("ja"), QStringLiteral("日本語"),
                    QLocale(QStringLiteral("ja"))});
    language.setLanguages(entries);

    QCOMPARE(language.languages().size(), 3);
    QCOMPARE(language.currentLanguage(), QStringLiteral("en"));
    QCOMPARE(language.currentEntry().nativeName, QStringLiteral("English"));

    QString changedCode;
    QLocale changedLocale;
    int changeCount = 0;
    connect(&language, &md::Language::languageChanged, this,
            [&](QString code, QLocale locale) {
                changedCode = code;
                changedLocale = locale;
                ++changeCount;
            });

    QVERIFY(language.setCurrentLanguage(QStringLiteral("zh_CN")));
    QCOMPARE(language.currentLanguage(), QStringLiteral("zh_CN"));
    QCOMPARE(changeCount, 1);
    QCOMPARE(changedCode, QStringLiteral("zh_CN"));
    QCOMPARE(changedLocale.name(), QLocale(QStringLiteral("zh_CN")).name());

    // Selecting the current language is a silent no-op.
    QVERIFY(language.setCurrentLanguage(QStringLiteral("zh_CN")));
    QCOMPARE(changeCount, 1);

    QVERIFY(!language.setCurrentLanguage(QStringLiteral("xx")));
    QCOMPARE(language.currentLanguage(), QStringLiteral("zh_CN"));

    QString installedCode;
    language.setTranslatorInstaller([&](const QString& code) { installedCode = code; });

    QVERIFY(language.setCurrentLanguage(QStringLiteral("ja")));
    QCOMPARE(changeCount, 2);
    QCOMPARE(installedCode, QStringLiteral("ja"));
}

void TestFeedbackDialog::languageMenuSelection()
{
    QWidget host;
    host.resize(640, 480);
    host.show();

    md::Language language(&host);
    QList<md::Language::LanguageEntry> entries;
    entries.append({QStringLiteral("en"), QStringLiteral("English"),
                    QLocale(QStringLiteral("en"))});
    entries.append({QStringLiteral("zh_CN"), QStringLiteral("简体中文"),
                    QLocale(QStringLiteral("zh_CN"))});
    entries.append({QStringLiteral("ja"), QStringLiteral("日本語"),
                    QLocale(QStringLiteral("ja"))});
    language.setLanguages(entries);
    language.setCurrentLanguage(QStringLiteral("ja"));
    language.setManualAnimationClock(true);

    language.arrange(QRect(24, 24, 180, 40));
    language.show();

    QString installedCode;
    language.setTranslatorInstaller([&](const QString& code) { installedCode = code; });
    int changeCount = 0;
    connect(&language, &md::Language::languageChanged, this,
            [&](QString, QLocale) { ++changeCount; });

    language.openMenu();
    QVERIFY(language.isMenuOpen());
    QVERIFY(language.selectorPanel() != nullptr);
    QVERIFY(language.selectorPanel()->isVisible());

    // The shared sheet highlights the committed value ("ja"); Up moves to
    // "zh_CN" and Enter confirms it.
    QTest::keyClick(language.selectorPanel(), Qt::Key_Up);
    QTest::keyClick(language.selectorPanel(), Qt::Key_Return);

    // The sheet exits first; the value is applied while the busy overlay is up.
    language.advanceAnimation(std::chrono::milliseconds(250));
    auto* overlay = language.switchingOverlay();
    QVERIFY(overlay != nullptr);
    QVERIFY(overlay->isBusy());
    overlay->advanceAnimation(std::chrono::milliseconds(500));
    QCOMPARE(language.currentLanguage(), QStringLiteral("zh_CN"));
    QCOMPARE(changeCount, 1);
    QCOMPARE(installedCode, QStringLiteral("zh_CN"));
    overlay->advanceAnimation(std::chrono::milliseconds(400));
    overlay->advanceAnimation(std::chrono::milliseconds(250));
    QVERIFY(!language.isMenuOpen());
}

void TestFeedbackDialog::componentsPaintWithoutCrashing()
{
    QWidget host;
    host.resize(640, 480);
    host.show();

    QWidget anchor(&host);
    anchor.setGeometry(80, 80, 140, 36);

    md::Menu menu(&host);
    menu.addItem(makeItem(QStringLiteral("Open"), QStringLiteral("Ctrl+O")));
    md::Menu::MenuItem file = makeItem(QStringLiteral("File"));
    file.submenu.push_back(makeItem(QStringLiteral("New")));
    menu.addItem(file);
    menu.setAnchor(&anchor);
    menu.open();
    QVERIFY(menu.isOpen());
    QVERIFY(!menu.grab().isNull());

    QTest::keyClick(&menu, Qt::Key_Down);
    QTest::keyClick(&menu, Qt::Key_Right);
    QVERIFY(menu.isSubmenuOpen());
    QVERIFY(!menu.grab().isNull());
    if (md::Menu* submenu = menu.submenuAt(1))
        QVERIFY(!submenu->grab().isNull());
    menu.close();

    md::Alert alert;
    alert.setTitle(QStringLiteral("Heads up"));
    alert.setDescription(QStringLiteral("Something happened."));
    alert.setSeverity(md::Alert::Severity::Warning);
    alert.setActionText(QStringLiteral("Details"));
    alert.arrange(QRect(0, 0, 480, 80));
    alert.show();
    QVERIFY(!alert.grab().isNull());
    alert.close();

    md::Notification notification(&host);
    notification.showMessage(QStringLiteral("Saved"), QStringLiteral("Undo"), 0);
    QVERIFY(notification.isShowing());
    QVERIFY(!notification.grab().isNull());
    notification.close();

    md::ConfirmDialog confirm(&host);
    confirm.setTitle(QStringLiteral("Confirm"));
    confirm.setText(QStringLiteral("Proceed?"));
    confirm.open();
    QVERIFY(!confirm.grab().isNull());
    confirm.accept();

    md::SingleChoiceDialog single(&host);
    single.setItems(QStringList({QStringLiteral("One"), QStringLiteral("Two")}));
    single.setSelectedIndex(0);
    single.open();
    QVERIFY(!single.grab().isNull());
    single.close();

    md::MultipleChoiceDialog multiple(&host);
    multiple.setItems(QStringList({QStringLiteral("One"), QStringLiteral("Two")}));
    multiple.setItemChecked(0, true);
    multiple.open();
    QVERIFY(!multiple.grab().isNull());
    multiple.close();

    md::Language language(&host);
    language.arrange(QRect(24, 24, 180, 40));
    language.show();
    QVERIFY(!language.grab().isNull());
}

QTEST_MAIN(TestFeedbackDialog)
#include "tst_feedback_dialog.moc"
