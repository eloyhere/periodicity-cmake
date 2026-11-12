// Periodicity — the input and selection components.
//
// Every control here is a real md::View: it implements the measure and paint
// contracts, paints its own design baseline surface and content, and never uses a
// stock QComboBox / QSpinBox as its final appearance. A QLineEdit is embedded
// only as the text-editing focus proxy for the text-derived controls; the
// container, the floating label, the underline, the helper/error line, the
// menu, the colour panels and the pickers are all drawn by the components.
//
// Data flows through two small value types declared in model.h:
//
//   Model   — an owned flat QVariant list with an optional label per row.
//   Adapter — a non-owning presentation bridge over any flat item model.
//
// A control owns its Adapter (a QObject child) but never its source model, so
// the application keeps the data and the control only borrows it.
//
// Notes on the text controls: TextInput places its QLineEdit through
// arrangeOverride(), so the editor follows resize() and arrange() exactly like
// every other child of a Periodicity widget. The editor is transparent and
// borderless; the component paints the container and the label around it.

#pragma once

#include <md/animation/animator.h>
#include <md/components/model.h>
#include <md/view/view.h>

#include <QColor>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QValidator>

#include <functional>
#include <limits>
#include <memory>

class QAbstractItemModel;
class QEvent;
class QKeyEvent;
class QLineEdit;
class QMouseEvent;
class QPainter;
class QRectF;
class QRegularExpressionValidator;
class QSortFilterProxyModel;
class QWheelEvent;

namespace md {

// ---------------------------------------------------------------------------
// TextInput — the design baseline filled text field.
//
// The container is the 56 dp filled surface with rounded top corners, a
// floating label, a 1 dp/2 dp underline and an optional supporting line below.
// The embedded QLineEdit is the focus proxy: typing, selection and the platform
// IME all keep working, while focus colour, error colour and the clear button
// are drawn here.
// ---------------------------------------------------------------------------
class TextInput : public View
{
    Q_OBJECT

public:
    explicit TextInput(QWidget* parent = nullptr);
    ~TextInput() override;

    [[nodiscard]] QString text() const;
    void setText(const QString& text);

    void setPlaceholderText(const QString& text);
    [[nodiscard]] QString placeholderText() const;

    // The floating label. When it is set the resting label doubles as the
    // placeholder, so the editor does not paint a second one underneath it.
    void setLabel(const QString& label);
    [[nodiscard]] QString label() const;

    // The supporting line. An error always wins over the helper text.
    void setHelperText(const QString& text);
    [[nodiscard]] QString helperText() const;
    void setErrorText(const QString& text);
    [[nodiscard]] QString errorText() const;
    [[nodiscard]] bool hasError() const;

    void setPassword(bool password);
    [[nodiscard]] bool isPassword() const;

    void setClearButtonEnabled(bool enabled);
    [[nodiscard]] bool isClearButtonEnabled() const;

    // True while the editor's validator accepts the text and no error is
    // presented. Derived controls add their own checks.
    [[nodiscard]] bool hasAcceptableInput() const;

    void setValidator(const QValidator* validator);
    [[nodiscard]] const QValidator* validator() const;

    [[nodiscard]] QLineEdit* editor() const noexcept;

    // Geometry of the painted parts, in widget coordinates. Exposed so that a
    // test or a custom delegate can hit-test exactly what is drawn.
    [[nodiscard]] QRectF fieldRect() const;
    [[nodiscard]] QRectF clearButtonRect() const;

    // The floating label's animated progress: 0 is the resting label centred in
    // the field (bodyLarge) and 1 is the floating label on its top line
    // (bodySmall). Exposed so a test or a layout tool can observe the
    // transition without sampling the painted pixels.
    [[nodiscard]] qreal labelProgress() const noexcept;

signals:
    void textChanged(const QString& text);
    void editingFinished();
    void clearClicked();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void arrangeOverride(const QRectF& content) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void onShown() override;
    void onHidden() override;
    [[nodiscard]] bool focusable() const override;

    // Derived controls describe why the current text is not acceptable. An
    // empty string means "no error"; the base class repaints accordingly.
    [[nodiscard]] virtual QString validationError() const;

    // Called after every user edit, before textChanged() is emitted.
    virtual void onEditorTextChanged(const QString& text);

    // Called when the embedded editor gains or loses focus.
    virtual void onEditorFocusChanged(bool focused);

    // Returns true when the key was consumed by the derived control. The
    // editor keeps normal editing behavior for every key that returns false.
    virtual bool processEditorKey(QKeyEvent* event);

    // Re-runs validationError() and updates the supporting line.
    void revalidate();

    [[nodiscard]] bool editorFocused() const;

private:
    void applyThemeToEditor();
    void layoutEditor();
    void syncPlaceholder();
    [[nodiscard]] bool hasSupportingText() const;

    // The floating-label animation. The target is 1 while the editor has focus
    // or holds text; 0 otherwise. updateLabelTransition() moves labelProgress_
    // towards it over the theme's medium2 token, settling at once when the
    // theme asks for reduced motion or the field has no frames to show.
    [[nodiscard]] qreal labelTarget() const;
    void updateLabelTransition();
    void settleLabel();

    QLineEdit* editor_ = nullptr;
    QString placeholder_;
    QString label_;
    QString helper_;
    QString errorText_;
    // The painted floating-label progress in 0..1. It is interpolated, never a
    // boolean: the vertical position, the type size and the colour all read it.
    qreal labelProgress_ = 0.0;
    bool focused_ = false;
    bool password_ = false;
    bool clearEnabled_ = false;
    animation::Animator labelAnimator_;
};

// A filled text field pre-wired with an email validator.
class EmailInput final : public TextInput
{
    Q_OBJECT

public:
    explicit EmailInput(QWidget* parent = nullptr);

protected:
    [[nodiscard]] QString validationError() const override;
};

// A filled text field pre-wired with a telephone number validator.
class TelephoneInput final : public TextInput
{
    Q_OBJECT

public:
    explicit TelephoneInput(QWidget* parent = nullptr);

protected:
    [[nodiscard]] QString validationError() const override;
};

// A filled text field whose accepted format is an application-supplied
// regular expression.
class RegexInput final : public TextInput
{
    Q_OBJECT

public:
    explicit RegexInput(QWidget* parent = nullptr);

    void setPattern(const QRegularExpression& pattern);
    [[nodiscard]] QRegularExpression pattern() const;

protected:
    [[nodiscard]] QString validationError() const override;

private:
    QRegularExpressionValidator* regexValidator_ = nullptr;
};

// A numeric filled text field: range, step and an explicit error state for
// text that is not a number or lies outside the range.
class NumberInput final : public TextInput
{
    Q_OBJECT

public:
    explicit NumberInput(QWidget* parent = nullptr);
    ~NumberInput() override;

    void setRange(double minimum, double maximum);
    [[nodiscard]] double minimum() const noexcept;
    [[nodiscard]] double maximum() const noexcept;

    void setStep(double step);
    [[nodiscard]] double step() const noexcept;

    [[nodiscard]] double value() const;
    void setValue(double value);

    // Moves by `steps` whole steps, clamped to the range, and commits.
    void stepBy(double steps);

signals:
    void valueChanged(double value);

protected:
    [[nodiscard]] QString validationError() const override;
    void onEditorTextChanged(const QString& text) override;
    bool processEditorKey(QKeyEvent* event) override;

private:
    [[nodiscard]] double clampToRange(double value) const;
    [[nodiscard]] bool parseValue(const QString& text, double* value) const;
    void commit(double value);

    double minimum_ = std::numeric_limits<double>::lowest();
    double maximum_ = std::numeric_limits<double>::max();
    double step_ = 1.0;
    double lastValue_ = 0.0;
    bool updating_ = false;
};

// ---------------------------------------------------------------------------
// DropDown — a design baseline exposed menu.
//
// The closed control is a filled field that shows the current item. Opening it
// shows a top-level popup menu painted by this module: up/down move the
// highlight, Return/Space activate it, Escape and a click outside dismiss it.
// Items come from Model (through addItem()) or from any flat item model through
// setSourceModel(), which is borrowed, never owned.
//
// A borrowed source is observed: a reset or a row removal clamps the current
// index to the surviving rows (or -1 when none are left) and reports the change
// through currentIndexChanged()/currentValueChanged().
// ---------------------------------------------------------------------------
class DropDown : public View
{
    Q_OBJECT

public:
    explicit DropDown(QWidget* parent = nullptr);
    ~DropDown() override;

    // Non-owning. Passing a model replaces the internal one; the previous
    // model is not deleted.
    void setSourceModel(QAbstractItemModel* model);
    [[nodiscard]] QAbstractItemModel* sourceModel() const noexcept;
    [[nodiscard]] Adapter* adapter() const noexcept;

    // Presentation only; the stored values are untouched.
    void setFormatter(Adapter::Formatter formatter);

    void addItem(const QString& text, const QVariant& value = {});
    void addItems(const QStringList& texts);

    [[nodiscard]] int count() const;
    [[nodiscard]] QString itemText(int index) const;
    [[nodiscard]] QVariant itemValue(int index) const;

    [[nodiscard]] int currentIndex() const noexcept;
    void setCurrentIndex(int index);
    [[nodiscard]] QString currentText() const;
    [[nodiscard]] QVariant currentValue() const;
    void setCurrentValue(const QVariant& value);

    void setPlaceholderText(const QString& text);
    [[nodiscard]] QString placeholderText() const;

    void setMaxVisibleItems(int items);
    [[nodiscard]] int maxVisibleItems() const noexcept;

    void showPopup();
    void hidePopup();
    [[nodiscard]] bool isPopupVisible() const;
    [[nodiscard]] QWidget* popupWidget() const noexcept;

signals:
    void currentIndexChanged(int index);
    void currentValueChanged(const QVariant& value);
    void activated(int index);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;
    [[nodiscard]] bool focusable() const override;

    // The popup hooks; a subclass may reposition the menu but does not have to.
    [[nodiscard]] virtual QRect popupAnchorRect() const;

private:
    void ensureOwnedModel();
    void handlePopupActivated(int row);
    void handlePopupDismissed();
    void setAdapterSource(QAbstractItemModel* model);
    void refreshFromModel();

    Adapter* adapter_ = nullptr;
    std::unique_ptr<Model> ownedModel_;
    QWidget* popup_ = nullptr;
    QString placeholder_ = QStringLiteral("Select an option");
    int current_ = -1;
    int maxVisibleItems_ = 6;
    bool popupOpen_ = false;
    bool pressed_ = false;
};

// A DropDown that exists to make the single-selection role explicit.
class Select final : public DropDown
{
    Q_OBJECT

public:
    using DropDown::DropDown;

    [[nodiscard]] QVariant value() const { return currentValue(); }
    void setValue(const QVariant& value) { setCurrentValue(value); }
};

// ---------------------------------------------------------------------------
// Complete — a text field with filtered suggestions.
//
// The editor stays in charge of the keyboard: the suggestion popup is shown
// without activation, so typing continues to reach the QLineEdit. Up/down move
// the highlight, Return accepts it, Escape dismisses the list. Accepting a row
// copies its display text into the editor and reports the stored value.
// ---------------------------------------------------------------------------
class Complete final : public TextInput
{
    Q_OBJECT

public:
    explicit Complete(QWidget* parent = nullptr);
    ~Complete() override;

    void setSuggestions(const QStringList& items);
    [[nodiscard]] QStringList suggestions() const;

    // Non-owning alternative to setSuggestions().
    void setSourceModel(QAbstractItemModel* model);
    [[nodiscard]] QAbstractItemModel* sourceModel() const noexcept;
    [[nodiscard]] Adapter* adapter() const noexcept;
    void setFormatter(Adapter::Formatter formatter);

    [[nodiscard]] int visibleSuggestionCount() const;
    [[nodiscard]] int highlightedSuggestion() const noexcept;

    void showSuggestions();
    void hideSuggestions();
    [[nodiscard]] bool isPopupVisible() const;
    [[nodiscard]] QWidget* popupWidget() const noexcept;

signals:
    void suggestionActivated(int index, const QVariant& value);

protected:
    void onEditorTextChanged(const QString& text) override;
    void onEditorFocusChanged(bool focused) override;
    bool processEditorKey(QKeyEvent* event) override;

private:
    void applyFilter();
    void refreshPopup();
    void activateRow(int row);
    void handlePopupDismissed();

    std::unique_ptr<Model> ownedModel_;
    Adapter* adapter_ = nullptr;
    QSortFilterProxyModel* filter_ = nullptr;
    QWidget* popup_ = nullptr;
    int highlighted_ = -1;
    bool suppressFilter_ = false;
};

// ---------------------------------------------------------------------------
// ColourPicker — a saturation/value panel, a hue bar, an alpha bar and a hex
// field.
//
// Hue is stored separately from the colour so that a grey (where QColor
// reports no hue) does not lose the position of the hue bar, and alpha is part
// of every conversion, so picking a colour never resets transparency.
// ---------------------------------------------------------------------------
class ColourPicker final : public View
{
    Q_OBJECT

public:
    explicit ColourPicker(QWidget* parent = nullptr);
    ~ColourPicker() override;

    [[nodiscard]] QColor colour() const;
    void setColour(const QColor& colour);

    [[nodiscard]] int hue() const noexcept;
    [[nodiscard]] int saturation() const noexcept;
    [[nodiscard]] int value() const noexcept;
    [[nodiscard]] int alpha() const noexcept;
    void setAlpha(int alpha);

    [[nodiscard]] QString hexText() const;
    void setHexText(const QString& text);
    [[nodiscard]] QLineEdit* hexEditor() const noexcept;

    // Painted geometry, in widget coordinates.
    [[nodiscard]] QRectF saturationValueRect() const;
    [[nodiscard]] QRectF hueBarRect() const;
    [[nodiscard]] QRectF alphaBarRect() const;
    [[nodiscard]] QRectF hexRect() const;

signals:
    void colourChanged(const QColor& colour);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void arrangeOverride(const QRectF& content) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    [[nodiscard]] bool focusable() const override;

private:
    enum class Part {
        None,
        SaturationValue,
        Hue,
        Alpha,
    };

    void applyColour(const QColor& colour);
    void applyPointer(Part part, const QPointF& position);
    void syncHexEditor();
    void commitHexEditor();
    [[nodiscard]] QColor composed() const;

    int hue_ = 0;
    int saturation_ = 255;
    int value_ = 255;
    int alpha_ = 255;
    QLineEdit* hexEditor_ = nullptr;
    Part dragged_ = Part::None;
    bool updating_ = false;
    bool syncingHex_ = false;
    bool hexValid_ = true;
};

// ---------------------------------------------------------------------------
// PickerBase — the shared wheel for NumberPicker and RingPicker.
//
// A picker shows a vertical strip of rows with the current row centred and
// highlighted; rows further away fade. The wheel, the arrow keys and dragging
// move it, and in cyclic mode the ends join. The data is always read through an
// Adapter, so rows may be any registered QVariant type.
// ---------------------------------------------------------------------------
class PickerBase : public View
{
    Q_OBJECT

public:
    explicit PickerBase(QWidget* parent = nullptr);
    ~PickerBase() override;

    void setSourceModel(QAbstractItemModel* model);
    [[nodiscard]] QAbstractItemModel* sourceModel() const noexcept;
    [[nodiscard]] Adapter* adapter() const noexcept;
    void setFormatter(Adapter::Formatter formatter);

    [[nodiscard]] int count() const;
    [[nodiscard]] int currentIndex() const noexcept;
    [[nodiscard]] QVariant currentValue() const;
    [[nodiscard]] QString currentText() const;

    // Wraps in cyclic mode and clamps otherwise.
    void setCurrentIndex(int index);
    void setCurrentValue(const QVariant& value);

    void setCyclic(bool cyclic);
    [[nodiscard]] bool isCyclic() const noexcept;

    // Item metrics, in dp; itemHeight() reports the resolved pixels.
    void setItemHeightDp(qreal dp);
    [[nodiscard]] qreal itemHeightDp() const noexcept;
    [[nodiscard]] qreal itemHeight() const;

    void setVisibleItemCount(int count);
    [[nodiscard]] int visibleItemCount() const noexcept;

    // Hit-testing helpers: the row under a widget-local point and the height of
    // one row in device-independent pixels.
    [[nodiscard]] int rowAt(const QPointF& position) const;
    [[nodiscard]] bool isSettling() const noexcept;

signals:
    void currentIndexChanged(int index);
    void currentValueChanged(const QVariant& value);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    [[nodiscard]] bool focusable() const override;

    [[nodiscard]] virtual QString textForRow(int row) const;
    [[nodiscard]] virtual QVariant valueForRow(int row) const;

    // Installs an internally owned model. Subclasses expose this as setValues().
    void setOwnedValues(QVariantList values, QStringList labels = {});

private:
    void applyIndex(int index);
    void step(int steps);
    void consumeOffset();
    void startSettling();
    void stopSettling();
    void tick();
    [[nodiscard]] int normalized(int index) const;
    [[nodiscard]] qreal centerY() const;
    void setAdapterSource(QAbstractItemModel* model);
    void refreshFromModel();

    Adapter* adapter_ = nullptr;
    std::unique_ptr<Model> ownedModel_;
    QTimer settleTimer_;
    int current_ = -1;
    int visibleItems_ = 5;
    qreal itemHeightDp_ = 40.0;
    qreal offset_ = 0.0;
    qreal velocity_ = 0.0;
    QPointF pressPosition_;
    QPointF lastPosition_;
    bool dragging_ = false;
    bool moved_ = false;
    bool cyclic_ = true;
};

// A cyclic numeric picker over an explicit value set or a min/max/step range.
class NumberPicker final : public PickerBase
{
    Q_OBJECT

public:
    explicit NumberPicker(QWidget* parent = nullptr);
    ~NumberPicker() override;

    // Builds the value set minimum, minimum + step, ... up to maximum.
    void setRange(double minimum, double maximum);
    void setStep(double step);

    [[nodiscard]] double minimum() const noexcept;
    [[nodiscard]] double maximum() const noexcept;
    [[nodiscard]] double step() const noexcept;

    // An explicit ordered set of numeric values; replaces any range.
    void setValues(const QVariantList& values);

    [[nodiscard]] double value() const;
    void setValue(double value);

signals:
    void valueChanged(double value);

private:
    void rebuildRange();

    double minimum_ = 0.0;
    double maximum_ = 10.0;
    double step_ = 1.0;
};

// A cyclic picker over arbitrary QVariant values.
class RingPicker final : public PickerBase
{
    Q_OBJECT

public:
    explicit RingPicker(QWidget* parent = nullptr);
    ~RingPicker() override;

    void setValues(QVariantList values, QStringList labels = {});
};

} // namespace md
