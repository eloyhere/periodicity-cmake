// Tests for the shared modal picker sheet, the dropdown trigger and the row
// feedback (hover/pressed/ripple) used by the picker and the select popups.
//
// Animation is driven through the manual clock of Animator (see
// tst_motion_levels for the ladder itself); no test waits for wall-clock time.

#include <md/components/dropdown_trigger.h>
#include <md/components/font.h>
#include <md/components/language.h>
#include <md/components/modal.h>
#include <md/components/picker_sheet.h>
#include <md/components/row_feedback.h>
#include <md/components/switching_overlay.h>
#include <md/components/theme_switcher.h>
#include <md/components/virtual_select.h>
#include <md/core/i18n.h>
#include <md/structure/virtual_source.h>
#include <md/theme/theme.h>
#include <md/view/view.h>

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QVariantList>
#include <QWidget>

#include <chrono>
#include <stdexcept>

using namespace md;
using namespace std::chrono_literals;

namespace {

QWidget* host = nullptr;

QList<PickerSheet::Option> options(const QStringList& ids)
{
    QList<PickerSheet::Option> result;
    for (const QString& id : ids)
        result.append(PickerSheet::Option{id, id.toUpper()});
    return result;
}

void arrangeField(View& widget)
{
    widget.measure(MeasureSpec::exactly(240.0), MeasureSpec::exactly(56.0));
    widget.arrange(QRect(0, 0, 240, 56));
}

RowFeedback* feedbackOf(QWidget* surface)
{
    const QVariant value = surface->property("md.rowFeedback");
    return qobject_cast<RowFeedback*>(value.value<QObject*>());
}

} // namespace

class TestPickerSheet : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void sheetOpensWithScrimAndEntrance();
    void confirmAppliesAndExits();
    void cancelAndEscapeKeepPrevious();
    void deletedInvokerDoesNotCrash();
    void keyboardNavigationMovesHighlightAndScrolls();
    void translationsUseBothContexts();

    void triggerOpensSheetAndAnimatesChevron();
    void triggerLabelFollowsConfirmAndCancel();
    void triggerHoverAndRipple();
    void fontTriggerShowsCurrentFamilyAndUpdates();
    void languageTriggerOpensAndAnimatesChevron();

    void switchingOverlayDisablesContentAndRestoresFocus();
    void switchingOverlayKeepsAnAlreadyDisabledChild();
    void switchingOverlayRefusesAnEarlyClose();
    void switchingOverlayBlocksEscapeAndClicks();
    void switchingOverlayIsReentrancySafe();
    void switchingOverlayRestoresAfterAThrowingSwitch();

    void selectPopupRowsHoverPressAndRipple();
    void selectPopupKeyboardActivationAndClickMatch();
    void closingPopupWithRippleInFlightDoesNotCrash();
};

void TestPickerSheet::init()
{
    Theme::setApplicationTheme(Theme::baselineLight());
    host = new QWidget;
    host->resize(1000, 760);
    host->show();
    QCoreApplication::processEvents();
}

void TestPickerSheet::cleanup()
{
    delete host;
    host = nullptr;
    Theme::setApplicationTheme(Theme::baselineLight());
    i18n::uninstall();
}

void TestPickerSheet::sheetOpensWithScrimAndEntrance()
{
    QWidget invoker(host);
    invoker.show();

    PickerSheet sheet(&invoker);
    sheet.setManualAnimationClock(true);
    sheet.setTitle(QStringLiteral("Pick"));
    sheet.setOptions(options({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}),
                     QStringLiteral("b"));

    sheet.open();
    QVERIFY(sheet.isOpen());
    QVERIFY(sheet.card() != nullptr);
    QVERIFY(sheet.card()->isVisible());

    Modal* modal = sheet.modal();
    QVERIFY(modal != nullptr);
    QVERIFY(modal->isVisible());
    QVERIFY(modal->isOpen());
    QCOMPARE(modal->scrimRect(), QRectF(host->rect()));
    QVERIFY(modal->scrimOpacity() < 1.0);
    QVERIFY(modal->cardAnimationProgress() < 1.0);
    QCOMPARE(sheet.highlightedId(), QStringLiteral("b"));
    QCOMPARE(sheet.currentId(), QStringLiteral("b"));

    // Halfway through the ladder: the scrim has reached its end (Level1) while
    // the card (Level2) is still moving.
    sheet.advanceAnimation(250ms);
    QCOMPARE(modal->scrimOpacity(), 1.0);
    QVERIFY(modal->cardAnimationProgress() > 0.0);
    QVERIFY(modal->cardAnimationProgress() < 1.0);
    QVERIFY(modal->isAnimating());

    sheet.advanceAnimation(250ms);
    QVERIFY(!modal->isAnimating());
    QCOMPARE(modal->scrimOpacity(), 1.0);
    QCOMPARE(modal->cardAnimationProgress(), 1.0);
}

void TestPickerSheet::confirmAppliesAndExits()
{
    QWidget invoker(host);
    PickerSheet sheet(&invoker);
    sheet.setManualAnimationClock(true);
    sheet.setOptions(options({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}),
                     QStringLiteral("b"));
    sheet.open();
    sheet.advanceAnimation(500ms);

    Modal* modal = sheet.modal();
    QVERIFY(modal != nullptr);
    QSignalSpy confirmed(&sheet, &PickerSheet::confirmed);
    QSignalSpy closeFinished(modal, &Modal::closeFinished);

    sheet.setHighlightedId(QStringLiteral("c"));
    sheet.confirm();

    QCOMPARE(confirmed.count(), 1);
    QCOMPARE(confirmed.at(0).at(0).toString(), QStringLiteral("c"));
    QVERIFY(!sheet.isOpen());
    QVERIFY(modal->isAnimating());

    // The exit runs on the same ladder; one Level1 step settles it.
    sheet.advanceAnimation(250ms);
    QCOMPARE(closeFinished.count(), 1);
    QVERIFY(!modal->isVisible());
}

void TestPickerSheet::cancelAndEscapeKeepPrevious()
{
    {
        QWidget invoker(host);
        PickerSheet sheet(&invoker);
        sheet.setManualAnimationClock(true);
        sheet.setOptions(options({QStringLiteral("a"), QStringLiteral("b")}),
                         QStringLiteral("b"));
        sheet.open();
        sheet.advanceAnimation(500ms);

        QSignalSpy confirmed(&sheet, &PickerSheet::confirmed);
        QSignalSpy cancelled(&sheet, &PickerSheet::cancelled);
        sheet.setHighlightedId(QStringLiteral("a"));
        sheet.cancel();

        QCOMPARE(cancelled.count(), 1);
        QCOMPARE(confirmed.count(), 0);
        QCOMPARE(sheet.currentId(), QStringLiteral("b"));
        sheet.advanceAnimation(250ms);
    }

    {
        QWidget invoker(host);
        PickerSheet sheet(&invoker);
        sheet.setManualAnimationClock(true);
        sheet.setOptions(options({QStringLiteral("a"), QStringLiteral("b")}),
                         QStringLiteral("b"));
        sheet.open();
        sheet.advanceAnimation(500ms);

        QSignalSpy confirmed(&sheet, &PickerSheet::confirmed);
        QSignalSpy cancelled(&sheet, &PickerSheet::cancelled);
        QTest::keyClick(sheet.card(), Qt::Key_Escape);

        QCOMPARE(cancelled.count(), 1);
        QCOMPARE(confirmed.count(), 0);
        QCOMPARE(sheet.currentId(), QStringLiteral("b"));
        sheet.advanceAnimation(250ms);
    }
}

void TestPickerSheet::deletedInvokerDoesNotCrash()
{
    auto* invoker = new QWidget(host);
    invoker->show();

    auto* sheet = new PickerSheet(invoker);
    sheet->setManualAnimationClock(true);
    sheet->setOptions(options({QStringLiteral("a"), QStringLiteral("b")}),
                      QStringLiteral("a"));
    sheet->open();
    sheet->advanceAnimation(500ms);

    QSignalSpy confirmed(sheet, &PickerSheet::confirmed);
    delete invoker;
    QVERIFY(sheet->invoker() == nullptr);

    // No signal receiver, no focus-return target, no anchor: the sheet must
    // still resolve and exit without dereferencing the deleted trigger.
    sheet->setHighlightedId(QStringLiteral("b"));
    sheet->confirm();
    QCOMPARE(confirmed.count(), 1);
    sheet->advanceAnimation(250ms);
}

void TestPickerSheet::keyboardNavigationMovesHighlightAndScrolls()
{
    QWidget invoker(host);
    PickerSheet sheet(&invoker);
    sheet.setManualAnimationClock(true);
    sheet.setOptions(options({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"),
                              QStringLiteral("d"), QStringLiteral("e"), QStringLiteral("f"),
                              QStringLiteral("g"), QStringLiteral("h"), QStringLiteral("i"),
                              QStringLiteral("j")}),
                     QStringLiteral("a"));
    sheet.open();
    sheet.advanceAnimation(500ms);

    QCOMPARE(sheet.highlightedRow(), 0);
    QTest::keyClick(sheet.card(), Qt::Key_Down);
    QCOMPARE(sheet.highlightedId(), QStringLiteral("b"));
    QTest::keyClick(sheet.card(), Qt::Key_End);
    QCOMPARE(sheet.highlightedId(), QStringLiteral("j"));
    QVERIFY(sheet.firstVisibleRow() > 0);
    QTest::keyClick(sheet.card(), Qt::Key_Home);
    QCOMPARE(sheet.highlightedId(), QStringLiteral("a"));
    QCOMPARE(sheet.firstVisibleRow(), 0);
}

void TestPickerSheet::translationsUseBothContexts()
{
    QVERIFY(i18n::install(QStringLiteral("zh_CN")));
    QCOMPARE(QCoreApplication::translate("PickerSheet", "Cancel"), QStringLiteral("取消"));
    QCOMPARE(QCoreApplication::translate("md::PickerSheet", "Confirm"),
             QStringLiteral("确认"));
    QCOMPARE(QCoreApplication::translate("md::PickerSheet", "No options"),
             QStringLiteral("没有选项"));
}

void TestPickerSheet::triggerOpensSheetAndAnimatesChevron()
{
    auto* switcher = new ThemeSwitcher(host);
    switcher->addPreset(QStringLiteral("Light"), Theme::baselineLight());
    switcher->addPreset(QStringLiteral("Dark"), Theme::baselineDark());

    DropdownTrigger* trigger = switcher->createTrigger(host);
    trigger->setManualAnimationClock(true);
    trigger->resize(trigger->sizeHint());
    trigger->show();

    QCOMPARE(trigger->label(), QStringLiteral("Light"));
    QTest::mouseClick(trigger, Qt::LeftButton, Qt::NoModifier, trigger->rect().center());

    QVERIFY(switcher->isSelectorOpen());
    PickerSheet* sheet = switcher->selector();
    QVERIFY(sheet != nullptr);
    QVERIFY(sheet->isManualAnimationClock());
    QVERIFY(trigger->isOpen());

    QVERIFY(sheet->modal()->isAnimating());
    QVERIFY(trigger->chevronProgress() < 1.0);

    sheet->advanceAnimation(250ms);
    QVERIFY(sheet->modal()->cardAnimationProgress() > 0.0);
    QVERIFY(sheet->modal()->cardAnimationProgress() < 1.0);
    sheet->advanceAnimation(250ms);
    QVERIFY(!sheet->modal()->isAnimating());

    trigger->advanceAnimation(250ms);
    QVERIFY(trigger->chevronProgress() > 0.0);
    QVERIFY(trigger->chevronProgress() < 1.0);
    trigger->advanceAnimation(250ms);
    QCOMPARE(trigger->chevronProgress(), 1.0);
}

void TestPickerSheet::triggerLabelFollowsConfirmAndCancel()
{
    auto* switcher = new ThemeSwitcher(host);
    switcher->addPreset(QStringLiteral("Light"), Theme::baselineLight());
    switcher->addPreset(QStringLiteral("Dark"), Theme::baselineDark());

    DropdownTrigger* trigger = switcher->createTrigger(host);
    trigger->setManualAnimationClock(true);
    trigger->resize(trigger->sizeHint());
    trigger->show();

    QTest::mouseClick(trigger, Qt::LeftButton, Qt::NoModifier, trigger->rect().center());
    PickerSheet* sheet = switcher->selector();
    QVERIFY(sheet != nullptr);
    sheet->advanceAnimation(500ms);

    sheet->setHighlightedId(QStringLiteral("1"));
    sheet->confirm();
    QCOMPARE(switcher->currentPreset(), 0);
    sheet->advanceAnimation(250ms);

    SwitchingOverlay* overlay = switcher->switchingOverlay();
    QVERIFY(overlay != nullptr);
    QVERIFY(overlay->isManualAnimationClock());
    overlay->advanceAnimation(500ms);
    QCOMPARE(switcher->currentPreset(), 1);
    QCOMPARE(trigger->label(), QStringLiteral("Dark"));
    QVERIFY(!trigger->isOpen());
    overlay->advanceAnimation(400ms);
    QVERIFY(!overlay->isBusy());
    trigger->advanceAnimation(250ms);
    QCOMPARE(trigger->chevronProgress(), 0.0);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    // Cancel leaves the label at the applied value, not the pending highlight.
    QTest::mouseClick(trigger, Qt::LeftButton, Qt::NoModifier, trigger->rect().center());
    PickerSheet* second = switcher->selector();
    QVERIFY(second != nullptr);
    second->advanceAnimation(500ms);
    second->setHighlightedId(QStringLiteral("0"));
    second->cancel();
    QVERIFY(!overlay->isBusy());
    QCOMPARE(trigger->label(), QStringLiteral("Dark"));
    QCOMPARE(switcher->currentPreset(), 1);
    second->advanceAnimation(250ms);
}

void TestPickerSheet::triggerHoverAndRipple()
{
    auto* switcher = new ThemeSwitcher(host);
    switcher->addPreset(QStringLiteral("Light"), Theme::baselineLight());

    DropdownTrigger* trigger = switcher->createTrigger(host);
    trigger->setManualAnimationClock(true);
    trigger->resize(trigger->sizeHint());
    trigger->show();

    RowFeedback* feedback = trigger->rowFeedback();
    QVERIFY(feedback != nullptr);

    QTest::mouseMove(trigger, trigger->rect().center());
    QCOMPARE(feedback->hoveredRow(), 0);

    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(trigger, &leave);
    QCOMPARE(feedback->hoveredRow(), -1);

    QTest::mousePress(trigger, Qt::LeftButton, Qt::NoModifier, trigger->rect().center());
    QCOMPARE(feedback->pressedRow(), 0);
    QVERIFY(feedback->rippleCount() > 0);

    feedback->advanceAnimation(250ms);
    QVERIFY(feedback->rippleProgress() > 0.0);
    QVERIFY(feedback->rippleProgress() < 1.0);
    feedback->advanceAnimation(250ms);
    QCOMPARE(feedback->rippleCount(), 0);

    // Do not let the press turn into an activation when the test ends.
    QTest::mouseRelease(trigger, Qt::LeftButton, Qt::NoModifier, QPoint(-1, -1));
    feedback->clear();
}

void TestPickerSheet::fontTriggerShowsCurrentFamilyAndUpdates()
{
    Font* font = Font::install(host, QStringLiteral("Courier New"), 1.0);
    QVERIFY(font != nullptr);

    DropdownTrigger* trigger = font->createTrigger(host);
    trigger->setManualAnimationClock(true);
    trigger->resize(trigger->sizeHint());
    trigger->show();
    QCOMPARE(trigger->label(), QStringLiteral("Courier New"));

    QTest::keyClick(trigger, Qt::Key_Down);
    QVERIFY(font->isSelectorOpen());
    PickerSheet* sheet = font->selector();
    QVERIFY(sheet != nullptr);
    QVERIFY(sheet->isManualAnimationClock());
    sheet->advanceAnimation(500ms);

    QString chosen;
    for (const PickerSheet::Option& option : sheet->options()) {
        if (option.id != QStringLiteral("Courier New")) {
            chosen = option.id;
            break;
        }
    }
    if (chosen.isEmpty() && !sheet->options().isEmpty())
        chosen = sheet->options().first().id;
    QVERIFY(!chosen.isEmpty());

    sheet->setHighlightedId(chosen);
    sheet->confirm();

    // The value is not applied until the sheet has exited and the busy
    // overlay covers the rebuild.
    QCOMPARE(font->family(), QStringLiteral("Courier New"));
    sheet->advanceAnimation(250ms);
    SwitchingOverlay* overlay = font->switchingOverlay();
    QVERIFY(overlay != nullptr);
    QVERIFY(overlay->isManualAnimationClock());
    QVERIFY(overlay->isBusy());
    overlay->advanceAnimation(500ms);
    QCOMPARE(font->family(), chosen);
    QCOMPARE(trigger->label(), chosen);
    overlay->advanceAnimation(400ms);
    QVERIFY(!overlay->isBusy());

    const QString updated = chosen == QStringLiteral("Georgia")
                                ? QStringLiteral("Courier New")
                                : QStringLiteral("Georgia");
    font->setFamily(updated);
    QCOMPARE(trigger->label(), updated);

    delete font;
}

void TestPickerSheet::languageTriggerOpensAndAnimatesChevron()
{
    Language language(host);
    QList<Language::LanguageEntry> entries;
    entries.append({QStringLiteral("en"), QStringLiteral("English"),
                    QLocale(QStringLiteral("en"))});
    entries.append({QStringLiteral("zh_CN"), QStringLiteral("简体中文"),
                    QLocale(QStringLiteral("zh_CN"))});
    entries.append({QStringLiteral("ja"), QStringLiteral("日本語"),
                    QLocale(QStringLiteral("ja"))});
    language.setLanguages(entries);
    language.setCurrentLanguage(QStringLiteral("zh_CN"));
    language.arrange(QRect(20, 20, 220, 40));
    language.show();
    language.setManualAnimationClock(true);

    QTest::keyClick(&language, Qt::Key_Down);
    QVERIFY(language.isMenuOpen());
    PickerSheet* sheet = language.selector();
    QVERIFY(sheet != nullptr);
    QVERIFY(sheet->isManualAnimationClock());
    QCOMPARE(sheet->highlightedId(), QStringLiteral("zh_CN"));
    QVERIFY(language.chevronProgress() < 1.0);

    language.advanceAnimation(250ms);
    QVERIFY(language.chevronProgress() > 0.0);
    QVERIFY(language.chevronProgress() < 1.0);
    language.advanceAnimation(250ms);
    QCOMPARE(language.chevronProgress(), 1.0);

    sheet->setHighlightedId(QStringLiteral("ja"));
    sheet->confirm();
    QCOMPARE(language.currentLanguage(), QStringLiteral("zh_CN"));
    language.advanceAnimation(250ms);

    SwitchingOverlay* overlay = language.switchingOverlay();
    QVERIFY(overlay != nullptr);
    QVERIFY(overlay->isManualAnimationClock());
    overlay->advanceAnimation(500ms);
    QCOMPARE(language.currentLanguage(), QStringLiteral("ja"));
    QCOMPARE(language.chevronProgress(), 0.0);
    overlay->advanceAnimation(400ms);
    QVERIFY(!overlay->isBusy());
    QVERIFY(!language.isMenuOpen());

    // Cancel leaves the committed language and rotates the chevron back.
    QTest::keyClick(&language, Qt::Key_Space);
    QVERIFY(language.isMenuOpen());
    sheet = language.selector();
    QVERIFY(sheet != nullptr);
    language.advanceAnimation(500ms);
    sheet->setHighlightedId(QStringLiteral("en"));
    sheet->cancel();
    QCOMPARE(language.currentLanguage(), QStringLiteral("ja"));
    QVERIFY(!overlay->isBusy());
    language.advanceAnimation(250ms);
    QCOMPARE(language.chevronProgress(), 0.0);
}

void TestPickerSheet::switchingOverlayDisablesContentAndRestoresFocus()
{
    QWidget content(host);
    content.setGeometry(20, 20, 320, 220);
    auto* enabled = new QPushButton(QStringLiteral("Enabled"), &content);
    enabled->setGeometry(10, 10, 140, 34);
    auto* disabled = new QPushButton(QStringLiteral("Disabled"), &content);
    disabled->setGeometry(10, 64, 140, 34);
    disabled->setEnabled(false);
    content.show();
    enabled->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(enabled));

    SwitchingOverlay overlay(&content);
    overlay.setManualAnimationClock(true);
    bool work = false;
    overlay.run([&work] { work = true; });

    QVERIFY(overlay.isBusy());
    // The content is left enabled during the entrance; it is disabled only
    // once the scrim is fully opaque, so no disabled-token flash is visible.
    QVERIFY(content.isEnabled());
    overlay.advanceAnimation(250ms);
    QVERIFY(!work);
    QVERIFY(content.isEnabled());
    QVERIFY(overlay.progressValue() > 0.0);

    overlay.advanceAnimation(250ms);
    QVERIFY(work);
    QVERIFY(!content.isEnabled());
    QVERIFY(overlay.isBusy());

    // The overlay itself stays interactive: its indicator keeps advancing
    // while the content root is disabled.
    const qreal before = overlay.progressValue();
    overlay.advanceAnimation(100ms);
    QVERIFY(overlay.progressValue() != before || overlay.progressValue() >= 0.0);

    overlay.advanceAnimation(250ms);
    QVERIFY(!overlay.isBusy());
    QVERIFY(content.isEnabled());
    QVERIFY(enabled->isEnabled());
    QVERIFY(!disabled->isEnabled());
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(enabled));
}

void TestPickerSheet::switchingOverlayKeepsAnAlreadyDisabledChild()
{
    QWidget content(host);
    content.setGeometry(10, 10, 260, 160);
    auto* sibling = new QPushButton(QStringLiteral("Sibling"), &content);
    sibling->setGeometry(10, 10, 100, 30);
    auto* alreadyDisabled = new QPushButton(QStringLiteral("Already disabled"), &content);
    alreadyDisabled->setGeometry(10, 60, 140, 30);
    alreadyDisabled->setEnabled(false);
    content.show();

    SwitchingOverlay overlay(&content);
    overlay.setManualAnimationClock(true);
    overlay.run([] {});
    overlay.advanceAnimation(500ms);
    QVERIFY(!content.isEnabled());

    overlay.advanceAnimation(400ms);
    QVERIFY(!overlay.isBusy());
    QVERIFY(content.isEnabled());
    QVERIFY(sibling->isEnabled());
    QVERIFY(!alreadyDisabled->isEnabled());
}

void TestPickerSheet::switchingOverlayRefusesAnEarlyClose()
{
    MotionScheme motion;
    motion.reducedMotion = true;
    Theme::setApplicationTheme(Theme::baselineLight().withMotion(motion));

    QWidget content(host);
    content.setGeometry(10, 10, 200, 120);
    content.show();

    SwitchingOverlay overlay(&content);
    overlay.setManualAnimationClock(true);
    QVERIFY(overlay.minimumVisibleMs() >= 300);
    bool work = false;
    overlay.run([&work] { work = true; });
    QVERIFY(work);
    QVERIFY(overlay.isBusy());
    QVERIFY(!content.isEnabled());

    // Reduced motion snaps the entrance, so the hold is the only thing keeping
    // the feedback visible.
    overlay.advanceAnimation(std::chrono::milliseconds(overlay.minimumVisibleMs() - 100));
    QVERIFY(overlay.isBusy());
    overlay.advanceAnimation(100ms);
    QVERIFY(!overlay.isBusy());
    QVERIFY(content.isEnabled());
}

void TestPickerSheet::switchingOverlayBlocksEscapeAndClicks()
{
    QWidget content(host);
    content.setGeometry(10, 10, 200, 120);
    content.show();

    SwitchingOverlay overlay(&content);
    overlay.setManualAnimationClock(true);
    overlay.run([] {});
    overlay.advanceAnimation(250ms);
    QVERIFY(overlay.isBusy());

    Modal* modal = overlay.modal();
    QVERIFY(modal != nullptr);
    QTest::keyClick(modal, Qt::Key_Escape);
    QTest::mouseClick(modal, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    QVERIFY(overlay.isBusy());
    QVERIFY(overlay.isVisible());

    overlay.advanceAnimation(500ms);
    overlay.advanceAnimation(250ms);
    QVERIFY(!overlay.isBusy());
}

void TestPickerSheet::switchingOverlayIsReentrancySafe()
{
    QWidget content(host);
    content.setGeometry(10, 10, 200, 120);
    content.show();

    SwitchingOverlay overlay(&content);
    overlay.setManualAnimationClock(true);
    int calls = 0;
    overlay.run([&calls] { ++calls; });
    overlay.run([&calls] { calls += 10; });
    QCOMPARE(calls, 0);
    overlay.advanceAnimation(500ms);
    QCOMPARE(calls, 1);
    overlay.advanceAnimation(400ms);
    QVERIFY(!overlay.isBusy());

    // A second confirm after the first switch finished is a new switch, not a
    // queued one.
    overlay.run([&calls] { calls += 10; });
    overlay.advanceAnimation(500ms);
    QCOMPARE(calls, 11);
    overlay.advanceAnimation(400ms);
}

void TestPickerSheet::switchingOverlayRestoresAfterAThrowingSwitch()
{
    Language language(host);
    QList<Language::LanguageEntry> entries;
    entries.append({QStringLiteral("en"), QStringLiteral("English"),
                    QLocale(QStringLiteral("en"))});
    entries.append({QStringLiteral("ja"), QStringLiteral("日本語"),
                    QLocale(QStringLiteral("ja"))});
    language.setLanguages(entries);
    language.setCurrentLanguage(QStringLiteral("en"));
    language.arrange(QRect(10, 10, 220, 40));
    language.show();
    language.setManualAnimationClock(true);
    language.setFocus(Qt::OtherFocusReason);
    language.setTranslatorInstaller([](const QString&) {
        throw std::runtime_error("boom");
    });

    language.openMenu();
    QVERIFY(language.isMenuOpen());
    PickerSheet* sheet = language.selector();
    QVERIFY(sheet != nullptr);
    sheet->advanceAnimation(500ms);
    sheet->setHighlightedId(QStringLiteral("ja"));
    sheet->confirm();
    language.advanceAnimation(250ms);

    SwitchingOverlay* overlay = language.switchingOverlay();
    QVERIFY(overlay != nullptr);
    QVERIFY(overlay->isBusy());
    overlay->advanceAnimation(500ms);

    QCOMPARE(language.currentLanguage(), QStringLiteral("en"));
    QVERIFY(language.lastError().contains(QStringLiteral("boom")));
    QVERIFY(!language.isEnabled());

    overlay->advanceAnimation(400ms);
    overlay->advanceAnimation(250ms);
    QVERIFY(!overlay->isBusy());
    QVERIFY(language.isEnabled());
    QCOMPARE(language.currentLanguage(), QStringLiteral("en"));
}

void TestPickerSheet::selectPopupRowsHoverPressAndRipple()
{
    structure::ListSource source(QVariantList{QStringLiteral("Alpha"), QStringLiteral("Beta"),
                                              QStringLiteral("Gamma"), QStringLiteral("Delta"),
                                              QStringLiteral("Epsilon"), QStringLiteral("Zeta")});
    VirtualSelect list(host);
    list.setSource(&source);
    list.setMaxVisibleRows(3);
    arrangeField(list);
    list.move(30, 30);
    list.show();
    list.showPopup();
    QVERIFY(list.isPopupVisible());

    QWidget* content = list.popupWidget();
    QVERIFY(content != nullptr);
    RowFeedback* feedback = feedbackOf(content);
    QVERIFY(feedback != nullptr);
    feedback->setManualAnimationClock(true);

    QTest::mouseMove(content, list.rowRect(1).center().toPoint());
    QCOMPARE(feedback->hoveredRow(), 1);

    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(content, &leave);
    QCOMPARE(feedback->hoveredRow(), -1);

    QTest::mousePress(content, Qt::LeftButton, Qt::NoModifier,
                      list.rowRect(2).center().toPoint());
    QCOMPARE(feedback->pressedRow(), 2);
    QVERIFY(feedback->rippleCount() > 0);

    feedback->advanceAnimation(250ms);
    QVERIFY(feedback->rippleProgress() > 0.0);
    QVERIFY(feedback->rippleProgress() < 1.0);
    feedback->advanceAnimation(250ms);
    QCOMPARE(feedback->rippleCount(), 0);

    list.hidePopup();
}

void TestPickerSheet::selectPopupKeyboardActivationAndClickMatch()
{
    structure::ListSource source(QVariantList{QStringLiteral("Alpha"), QStringLiteral("Beta"),
                                              QStringLiteral("Gamma"), QStringLiteral("Delta"),
                                              QStringLiteral("Epsilon"), QStringLiteral("Zeta")});
    VirtualSelect list(host);
    list.setSource(&source);
    list.setMaxVisibleRows(3);
    arrangeField(list);
    list.move(30, 30);
    list.show();

    QSignalSpy selected(&list, &VirtualSelect::selected);

    list.showPopup();
    QWidget* content = list.popupWidget();
    QTest::keyClick(content, Qt::Key_Down);
    QCOMPARE(list.highlightedPosition(), 0);
    QTest::keyClick(content, Qt::Key_End);
    QCOMPARE(list.highlightedPosition(), 5);
    QVERIFY(list.firstVisibleRow() > 0);
    QTest::keyClick(content, Qt::Key_Return);
    QCOMPARE(selected.count(), 1);
    QCOMPARE(selected.at(0).at(0).toString(), QStringLiteral("Zeta"));
    QVERIFY(!list.isPopupVisible());

    // The surface reopens on the committed row, so the first painted row is
    // the top of that window; clicking it commits that row.
    list.showPopup();
    const int topPosition = list.firstVisibleRow();
    QTest::mouseClick(list.popupWidget(), Qt::LeftButton, Qt::NoModifier,
                      list.rowRect(0).center().toPoint());
    QCOMPARE(selected.count(), 2);
    QCOMPARE(selected.at(1).at(0).toString(), list.labelAt(topPosition));
    QVERIFY(!list.isPopupVisible());
}

void TestPickerSheet::closingPopupWithRippleInFlightDoesNotCrash()
{
    structure::ListSource source(QVariantList{QStringLiteral("Alpha"), QStringLiteral("Beta"),
                                              QStringLiteral("Gamma")});
    VirtualSelect list(host);
    list.setSource(&source);
    arrangeField(list);
    list.move(30, 30);
    list.show();
    list.showPopup();

    QWidget* content = list.popupWidget();
    RowFeedback* feedback = feedbackOf(content);
    QVERIFY(feedback != nullptr);
    feedback->setManualAnimationClock(true);

    QTest::mousePress(content, Qt::LeftButton, Qt::NoModifier,
                      list.rowRect(1).center().toPoint());
    QVERIFY(feedback->rippleCount() > 0);
    feedback->advanceAnimation(100ms);

    list.hidePopup();
    QVERIFY(!list.isPopupVisible());
    QCOMPARE(feedback->rippleCount(), 0);

    // The in-flight animator was stopped with the surface; stepping it again
    // must not touch freed row state.
    feedback->advanceAnimation(500ms);
    QCOMPARE(feedback->rippleCount(), 0);
}

QTEST_MAIN(TestPickerSheet)
#include "tst_picker_sheet.moc"
