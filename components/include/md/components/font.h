// Periodicity — Font, a font configuration container.
//
// Font attaches an Adapter to a widget subtree, the same shape as ThemeScope:
// the container is a QObject child of the host, adds no geometry and is found
// by walking the widget parent chain. Text and Typography ask Font::resolve()
// for the font of a role, so a panel can switch family or scale without
// touching every label.
//
// Adapters are owned by the container. The default implementation only rewrites
// the family and multiplies the size; a host with a variable-font axis or a
// per-role mapping can override font() entirely.

#pragma once

#include <md/core/density.h>
#include <md/tokens/type_scale.h>

#include <QFont>
#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>

class QWidget;

namespace md {

class DropdownTrigger;
class PickerSheet;
class Store;
class SwitchingOverlay;

class Font final : public QObject
{
    Q_OBJECT

public:
    ~Font() override;

    class Adapter
    {
    public:
        virtual ~Adapter();

        // The family used for `baseFamily`; return it unchanged to keep the
        // role's own family.
        [[nodiscard]] virtual QString family(const QString& baseFamily) const;

        // Multiplier applied to the role's pixel size. 1.0 keeps the token.
        [[nodiscard]] virtual qreal scale() const;

        // The complete font for a role. The default builds the token font,
        // applies family() and scale().
        [[nodiscard]] virtual QFont font(const TextStyle& style,
                                         const Density& density) const;
    };

    // Installs (or replaces) the container directly on `host`. The container
    // owns `adapter`. A null host or adapter yields nullptr.
    static Font* install(QWidget* host, std::unique_ptr<Adapter> adapter);
    // Convenience for the common "one family, one scale" case.
    static Font* install(QWidget* host, const QString& family, qreal scale = 1.0);

    // Store-backed container: the family and scale live in the "md.font"
    // store, so every container created this way follows the same values.
    [[nodiscard]] static Store* store();
    static Font* installFromStore(QWidget* host);
    // Writes this container's family and scale to the store.
    void publish();

    // The container installed directly on `host`, or nullptr.
    [[nodiscard]] static Font* find(const QWidget* host);
    // The nearest container on `context` or an ancestor, or nullptr.
    [[nodiscard]] static const Font* forWidget(const QWidget* context);

    // The font a Text/Typography node should paint with. Without a container
    // this is exactly the token font.
    [[nodiscard]] static QFont resolve(const QWidget* context, const TextStyle& style,
                                       const Density& density);

    [[nodiscard]] Adapter* adapter() const noexcept { return adapter_.get(); }
    void setAdapter(std::unique_ptr<Adapter> adapter);

    [[nodiscard]] QString family() const;
    void setFamily(const QString& family);
    [[nodiscard]] qreal scale() const;
    void setScale(qreal scale);

    // Re-resolves every descendant: posts QEvent::FontChange to the host and
    // its children after emitting changed().
    void notifyChanged();

    // Selector -----------------------------------------------------------
    // Font is a non-visual configuration container, so the chip → selector
    // affordance lives at the host layer: these entry points present the
    // family list in a modal whose card carries the enter/exit motion.
    //
    // Opens a modal family selector above the host's window, anchored at
    // `anchor` (the chip) when given and at the host otherwise. Choosing a
    // family updates this container through setFamily() and closes the modal.
    void openSelector(QWidget* anchor = nullptr);

    // The sheet currently backing the selector, or nullptr. Exposed so a host
    // (or a test) can drive the deterministic clock of an already open sheet.
    [[nodiscard]] PickerSheet* selector() const noexcept;

    // The selector content as a standalone View, parented to `parent`. The
    // caller owns the result; choosing a family still calls setFamily() on
    // this container. Exposed so a host can place the list in its own surface.
    QWidget* createSelector(QWidget* parent = nullptr);

    // The visible dropdown control: a row with the current family, a trailing
    // chevron that rotates while the sheet is open, hover/ripple feedback and
    // the click/Space/Enter/ArrowDown affordances. The caller owns it.
    DropdownTrigger* createTrigger(QWidget* parent = nullptr);

    // True while the modal opened by openSelector() is on screen.
    [[nodiscard]] bool isSelectorOpen() const noexcept;

    // The busy overlay used after a confirmed choice, and the reason its last
    // switch failed (empty on success).
    [[nodiscard]] SwitchingOverlay* switchingOverlay() const noexcept;
    [[nodiscard]] QString lastError() const;

    // Deterministic clock for the busy overlay. It is inherited from the
    // trigger's manual clock when one is used.
    void setManualSwitchClock(bool enabled);
    [[nodiscard]] bool isManualSwitchClock() const noexcept;

signals:
    void changed();

private:
    Font(QWidget* host, std::unique_ptr<Adapter> adapter);

    QPointer<QWidget> host_;
    std::unique_ptr<Adapter> adapter_;
    QString family_;
    qreal scale_ = 1.0;
    QPointer<PickerSheet> selector_;
    QPointer<SwitchingOverlay> overlay_;
    QString pendingFamily_;
    bool hasPendingFamily_ = false;
    bool manualSwitchClock_ = false;

    void startPendingSwitch();
};

} // namespace md
