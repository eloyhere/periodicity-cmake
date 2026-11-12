// Periodicity — Store, a named configuration bag with a global registry.
//
// A Store holds a QVariantMap and is addressed by an id. The registry makes
// the id unique inside the process: `Store::open("md.theme")` returns the same
// object for every caller, so the Font container, the Theme scope and the
// Language selector can publish their current configuration without passing
// pointers around.
//
// Stores are plain QObjects (no widgets, no painting) and are intended to be
// touched from the GUI thread, the same as the widgets that read them. Values
// are compared before they are written, so a redundant setValue does not emit.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

namespace md {

class Store final : public QObject
{
    Q_OBJECT

public:
    // Returns the store registered under `id`, creating it on first use. An
    // empty id is rejected and yields nullptr.
    static Store* open(const QString& id);

    // The store registered under `id`, or nullptr.
    [[nodiscard]] static Store* find(const QString& id);
    [[nodiscard]] static bool isOpen(const QString& id);
    [[nodiscard]] static QStringList ids();
    [[nodiscard]] static int count();

    [[nodiscard]] QString id() const;

    // Values -------------------------------------------------------------
    // An equal value is a no-op: valueChanged() is only emitted on a change.
    void setValue(const QString& key, const QVariant& value);
    [[nodiscard]] QVariant value(const QString& key,
                                 const QVariant& fallback = QVariant()) const;
    [[nodiscard]] bool hasValue(const QString& key) const;
    void remove(const QString& key);
    void clear();
    [[nodiscard]] QVariantMap values() const;
    [[nodiscard]] int size() const;

signals:
    void valueChanged(const QString& key, const QVariant& value);
    void changed();

private:
    explicit Store(const QString& id, QObject* parent = nullptr);
    ~Store() override;

    QString id_;
    QVariantMap values_;
};

} // namespace md
