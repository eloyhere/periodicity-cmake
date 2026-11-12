// Periodicity — implementation of the accessibility bridge (accessible.h).
//
// The bridge is deliberately shallow: it reads the framework's dynamic
// properties when Qt asks a question, and it never caches anything, so a
// property write is visible to the next accessibility query immediately.
//
// No View code is involved. The factory is installed process-wide and claims
// only objects that qobject_cast to md::View; Qt's own factories keep handling
// every other widget.

#include <md/view/accessible.h>

#include <md/view/view.h>

#include <QAccessible>
#include <QAccessibleWidget>
#include <QMetaObject>
#include <QString>
#include <QVariant>
#include <QWidget>

namespace md::view {
namespace {

// The public property names. Kept as plain C strings because that is what
// QObject::property()/setProperty() take.
constexpr auto kRoleProperty = "md-role";
constexpr auto kDescriptionProperty = "md-description";
constexpr auto kCheckedProperty = "md-checked";
constexpr auto kCheckStateProperty = "md-check-state";

// The class name without its namespace: "md::Button" becomes "Button". The
// fallback matters for a view whose component never called setAccessibleName();
// a screen reader announcing "Button" is still better than an empty name, and
// the visible label cannot be recovered from the paint commands.
[[nodiscard]] QString classStem(const QMetaObject* metaObject)
{
    if (metaObject == nullptr)
        return {};

    const QString className = QString::fromUtf8(metaObject->className());
    const qsizetype separator = className.lastIndexOf(QLatin1String("::"));
    return separator < 0 ? className : className.mid(separator + 2);
}

[[nodiscard]] QString firstNonEmpty(const QString& first, const QString& second)
{
    return first.isEmpty() ? second : first;
}

// The QAccessible factory. Qt calls it for every object whose interface it
// needs; returning nullptr for anything that is not a View lets the next
// factory (including Qt's built-in one) answer instead.
QAccessibleInterface* createViewAccessible(const QString&, QObject* object)
{
    if (auto* view = qobject_cast<View*>(object))
        return new ViewAccessible(view);
    return nullptr;
}

} // namespace

ViewAccessible::ViewAccessible(View* view)
    : QAccessibleWidget(view)
{
    // The role passed to QAccessibleWidget is Client, its default. role() reads
    // md-role on demand and falls back to that default; setting it here would
    // freeze the role at construction time.
}

QAccessible::Role ViewAccessible::role() const
{
    const QWidget* w = widget();
    if (w == nullptr)
        return QAccessibleWidget::role();

    const QVariant role = w->property(kRoleProperty);
    bool ok = false;
    const int value = role.toInt(&ok);
    if (role.isValid() && ok)
        return static_cast<QAccessible::Role>(value);

    return QAccessibleWidget::role();
}

QString ViewAccessible::text(QAccessible::Text t) const
{
    const QWidget* w = widget();
    if (w == nullptr)
        return QAccessibleWidget::text(t);

    switch (t) {
    case QAccessible::Name: {
        const QString name = w->accessibleName();
        return name.isEmpty() ? classStem(w->metaObject()) : name;
    }
    case QAccessible::Description: {
        const QString description = firstNonEmpty(
            w->accessibleDescription(), w->property(kDescriptionProperty).toString());
        // Fall through to QAccessibleWidget for its toolTip() fallback when the
        // framework properties are empty.
        return description.isEmpty() ? QAccessibleWidget::text(t) : description;
    }
    case QAccessible::Help: {
        const QString help = firstNonEmpty(
            w->property(kDescriptionProperty).toString(), w->accessibleDescription());
        // QAccessibleWidget's Help is whatsThis(); keep it when we have no
        // framework description to offer.
        return help.isEmpty() ? QAccessibleWidget::text(t) : help;
    }
    default:
        break;
    }

    return QAccessibleWidget::text(t);
}

QAccessible::State ViewAccessible::state() const
{
    QAccessible::State result = QAccessibleWidget::state();

    const QWidget* w = widget();
    if (w == nullptr)
        return result;

    // md-checked is the coarse form: a boolean is enough for a switch or a
    // toggle button.
    const QVariant checked = w->property(kCheckedProperty);
    if (checked.isValid()) {
        result.checkable = true;
        result.checked = checked.toBool();
    }

    // md-check-state is the precise form and wins when both properties exist,
    // because it can express Qt::PartiallyChecked, which a bool cannot.
    const QVariant checkState = w->property(kCheckStateProperty);
    if (checkState.isValid()) {
        bool ok = false;
        const int value = checkState.toInt(&ok);
        if (ok) {
            const auto state = static_cast<Qt::CheckState>(value);
            result.checkable = true;
            result.checked = state == Qt::Checked;
            result.checkStateMixed = state == Qt::PartiallyChecked;
        }
    }

    // QAccessibleWidget already mirrors isEnabled() into disabled; restating it
    // makes the invalid bit below independent of the base implementation.
    result.disabled = !w->isEnabled();
    result.invalid = result.invalid || result.disabled;

    return result;
}

int ViewAccessible::childCount() const
{
    // Children are real QWidgets: QAccessibleWidget walks the widget tree.
    return QAccessibleWidget::childCount();
}

QAccessibleInterface* ViewAccessible::child(int index) const
{
    return QAccessibleWidget::child(index);
}

void installAccessibility()
{
    // A function-local static is initialised exactly once and thread-safely
    // (C++20 [stmt.dcl]), so the factory is registered once no matter how many
    // translation units or threads call this.
    static const bool installed = [] {
        QAccessible::installFactory(&createViewAccessible);
        return true;
    }();
    Q_UNUSED(installed);
}

void setAccessibleRole(QWidget* widget, QAccessible::Role role)
{
    if (widget == nullptr)
        return;

    const int value = static_cast<int>(role);
    const QVariant current = widget->property(kRoleProperty);
    if (current.isValid() && current.toInt() == value)
        return;

    widget->setProperty(kRoleProperty, value);

    // Qt 6.8 has no QAccessible::RoleChanged event: QAccessible::Event stops at
    // IdentifierChanged/InvalidEvent. role() reads the property on demand, so
    // the interface is already correct; this event only tells an active screen
    // reader to re-query instead of trusting a cached role. ObjectShow is the
    // available fallback: it is not a perfect semantic fit (the widget is
    // usually not being shown when its role is set), but the platform bridges
    // forward it, whereas ObjectReorder would be wrong because the hierarchy
    // has not changed. Sending it only on an actual change keeps the
    // notification to the case that needs it.
    QAccessibleEvent event(widget, QAccessible::ObjectShow);
    QAccessible::updateAccessibility(&event);
}

void setAccessibleDescription(QWidget* widget, const QString& description)
{
    if (widget == nullptr)
        return;

    if (widget->property(kDescriptionProperty).toString() == description)
        return;

    widget->setProperty(kDescriptionProperty, description);
}

} // namespace md::view
