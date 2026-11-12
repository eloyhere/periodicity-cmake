#include <QApplication>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTest>
#include <md/components/form.h>

class TestFormControls final : public QObject {
    Q_OBJECT
private slots:
    void requiredAndPatternRules() {
        QLineEdit* editor = new QLineEdit;
        md::FormItem item(QStringLiteral("Email"), editor);
        item.setRules({md::FormRule::required(), md::FormRule::matches(
            QRegularExpression(QStringLiteral("^[^@]+@[^@]+\\.[^@]+$")), QStringLiteral("Bad email"))});
        QVERIFY(!item.validate());
        QCOMPARE(item.errorText(), QStringLiteral("This field is required"));
        editor->setText(QStringLiteral("invalid"));
        QVERIFY(!item.validate());
        QCOMPARE(item.errorText(), QStringLiteral("Bad email"));
        editor->setText(QStringLiteral("a@b.test"));
        QVERIFY(item.validate());
        QVERIFY(item.errorText().isEmpty());
    }
    void formValidatesAllItems() {
        md::Form form;
        auto* first = new md::FormItem(QStringLiteral("First"), new QLineEdit);
        auto* second = new md::FormItem(QStringLiteral("Second"), new QLineEdit);
        first->setRules({md::FormRule::required()});
        second->setRules({md::FormRule::required()});
        form.addItem(first);
        form.addItem(second);
        QSignalSpy spy(&form, &md::Form::validationChanged);
        QVERIFY(!form.validate());
        QCOMPARE(spy.count(), 1);
        qobject_cast<QLineEdit*>(first->editor())->setText(QStringLiteral("one"));
        qobject_cast<QLineEdit*>(second->editor())->setText(QStringLiteral("two"));
        QVERIFY(form.validate());
    }
};
QTEST_MAIN(TestFormControls)
#include "tst_form_controls.moc"
