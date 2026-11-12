// Periodicity — the accessibility bridge for the component kernel.
//
// Every Periodicity widget paints itself with QPainter, so Qt's default
// accessibility object sees a bare QWidget: a generic role, no name unless a
// component happened to call setAccessibleName(), and none of the framework's
// state. ViewAccessible closes that gap for the whole library from one place,
// without View knowing that accessibility exists:
//
//   * installAccessibility() registers a QAccessible factory once; afterwards
//     QAccessible::queryAccessibleInterface() returns a ViewAccessible for
//     every md::View and leaves every other object to Qt's own factories and
//     accessibility plugins;
//   * ViewAccessible maps a small set of dynamic properties onto the
//     QAccessible contract, so a component opts in by setting a property
//     instead of subclassing QAccessibleWidget itself;
//   * setAccessibleRole()/setAccessibleDescription() write those properties.
//
// Properties read by ViewAccessible:
//
//   md-role         int (QAccessible::Role)   — role, falling back to Client
//   md-description  QString                   — Description/Help text
//   md-checked      bool                      — checked state
//   md-check-state  int (Qt::CheckState)      — checked/partially-checked state
//
// installAccessibility() must be called after QApplication has been
// constructed and before the first View is created — in practice once, at the
// top of main(). Calling it again is a no-op.
//
// The bridge deliberately does not touch View: accessibility is a host concern
// here, exactly like the theme scope, and components remain free of
// QAccessibleWidget subclasses and Q_OBJECT boilerplate.

#pragma once

#include <QAccessible>
// QAccessibleWidget is the base class, so it cannot be forward-declared; this
// header is what makes ViewAccessible a complete type for every component.
#include <QAccessibleWidget>
#include <QString>

class QWidget;

namespace md {

class View;

namespace view {

// Installs the process-wide accessibility factory.
//
// The factory claims md::View instances and returns nullptr for everything
// else, so Qt's built-in factories continue to handle the remaining widgets
// (line edits, scroll areas, ...). The install is idempotent: a function-local
// static records that the factory has been registered, and later calls do
// nothing.
//
// QApplication must already exist when this is called, and it is best called
// before any View is created: an object that Qt has already cached an
// interface for keeps that interface until it is destroyed.
void installAccessibility();

// The QAccessibleInterface for one md::View.
//
// View is a real QWidget, so its accessible children are its real QWidget
// children. childCount() and child() therefore delegate to QAccessibleWidget,
// which walks the widget tree; there is no parallel accessible tree to keep in
// sync with the visual one. Qt 6's QAccessibleInterface has no navigate()
// helper at all: hierarchy queries go through childCount()/child()/childAt()/
// parent()/indexOfChild(), all supplied by QAccessibleWidget.
//
// actionInterface() and valueInterface() are inherited rather than overridden:
// QAccessibleInterface declares them as non-virtual inline helpers that
// forward to interface_cast(), and QAccessibleWidget::interface_cast() supplies
// the action interface (itself) and a null value interface. Inheriting them is
// how this class returns "QAccessibleWidget's implementation"; an override
// would never be called.
class ViewAccessible : public QAccessibleWidget
{
public:
    explicit ViewAccessible(View* view);

    // md-role when the property is present, QAccessibleWidget's role otherwise.
    [[nodiscard]] QAccessible::Role role() const override;

    // Name: accessibleName(), falling back to the class name without its
    // namespace (so md::Button reports "Button"). Description/Help:
    // accessibleDescription() and md-description.
    [[nodiscard]] QString text(QAccessible::Text t) const override;

    // QAccessibleWidget's state plus the check properties. A disabled view is
    // also reported invalid.
    [[nodiscard]] QAccessible::State state() const override;

    // Delegated to QAccessibleWidget: children are real QWidgets.
    [[nodiscard]] int childCount() const override;
    [[nodiscard]] QAccessibleInterface* child(int index) const override;
};

// Writes the md-role property (as an int) and notifies accessibility when the
// value actually changes. Callers should use it after the widget's other
// accessible properties are set; it is safe to call before the widget is shown.
void setAccessibleRole(QWidget* widget, QAccessible::Role role);

// Writes the md-description property, which ViewAccessible exposes as the
// accessible description (and, when QWidget::accessibleDescription() is empty,
// as the help text).
void setAccessibleDescription(QWidget* widget, const QString& description);

} // namespace view
} // namespace md
