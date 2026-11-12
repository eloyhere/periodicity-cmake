// Periodicity — SearchBar, the design baseline search field.
//
// SearchBar paints the 56 dp pill, the leading magnifier, the placeholder and
// the trailing clear affordance itself; a transparent QLineEdit is embedded as
// the focus proxy, so caret movement, selection, undo and the platform IME keep
// working exactly as they do in TextInput. The bar is full-height and full
// (pill) shaped, uses surfaceContainerHigh and puts onSurfaceVariant on the
// placeholder and both icons.
//
// Suggestions are filtered case-insensitively while typing and presented in an
// anchored PopUp below the bar (at most six rows). Down/Up move the highlighted
// row, Return activates it, Escape closes the list before the field loses
// focus, and Return in the field submits the current query. The popup is
// created lazily, so a bar that never shows suggestions allocates no surface.
//
// The popup is parented to the bar's window rather than to the bar: a child of
// the 56 dp widget would be clipped where the suggestion list extends below it.

#pragma once

#include <md/view/view.h>

#include <QPointer>
#include <QString>
#include <QStringList>

class QEvent;
class QKeyEvent;
class QLineEdit;
class QMouseEvent;
class QPainter;
class QResizeEvent;

namespace md {

class PopUp;

class SearchBar : public View
{
    Q_OBJECT

public:
    explicit SearchBar(QWidget* parent = nullptr);
    ~SearchBar() override;

    void setPlaceholder(const QString& text);
    [[nodiscard]] QString placeholder() const;

    void setText(const QString& text);
    [[nodiscard]] QString text() const;

    void clear();

    void setSuggestions(const QStringList& suggestions);
    [[nodiscard]] QStringList suggestions() const;

    // The embedded editor, exposed the same way TextInput exposes its own.
    [[nodiscard]] QLineEdit* editor() const noexcept;

    // The 48 dp touch target of the trailing clear affordance, in widget
    // coordinates. Exposed so that a test or a custom delegate can hit-test
    // exactly what is drawn.
    [[nodiscard]] QRectF clearButtonRect() const;

    // Suggestion-list access. Complete exposes the same pair so that a host can
    // show the list explicitly instead of relying on a focused editor; the
    // introspection helpers make the filtered state observable.
    void showSuggestions();
    void hideSuggestions();
    [[nodiscard]] bool isPopupVisible() const;
    [[nodiscard]] int visibleSuggestionCount() const;
    [[nodiscard]] int highlightedSuggestion() const noexcept;
    [[nodiscard]] QWidget* popupWidget() const noexcept;

signals:
    void textChanged(const QString& text);
    void submitted(const QString& text);
    void suggestionActivated(const QString& text);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void arrangeOverride(const QRectF& content) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void onThemeChanged() override;
    void onEnabledChanged(bool enabled) override;

private:
    [[nodiscard]] bool clearVisible() const;
    [[nodiscard]] bool editorFocused() const;

    void syncAppearance();
    void applyThemeToEditor();
    void layoutEditor();

    void rebuildFilter();
    void ensurePopup();
    void refreshPopup();
    void handleTextChanged(const QString& value);
    [[nodiscard]] bool moveHighlight(int delta);
    void activateSuggestion(int row);
    void submit();

    QLineEdit* editor_ = nullptr;
    QPointer<PopUp> popup_;
    QPointer<View> list_;
    QString placeholder_;
    QStringList suggestions_;
    QStringList filtered_;
    int highlighted_ = -1;
    bool suppressSuggestions_ = false;
};

} // namespace md
