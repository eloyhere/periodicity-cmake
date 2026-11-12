// Periodicity — SearchBar implementation.
//
// The bar owns the 56 dp pill surface and paints the leading magnifier and the
// trailing clear affordance; the QLineEdit is only the editing engine. That is
// the same arrangement TextInput uses, except that SearchBar is not a TextInput
// subclass: its surface, metrics and focus behaviour differ enough that sharing
// the filled-field container would leave most of it switched off.
//
// The suggestion list is a private View hosted by a PopUp. The popup is created
// on first use and parented to the bar's window so that it can extend past the
// 56 dp bar. The keyboard stays in the QLineEdit: the bar's event filter turns
// Down/Up/Return/Escape into list navigation, exactly like Complete does.

#include <md/components/search_bar.h>

#include <md/components/popup.h>
#include <md/theme/theme.h>

#include <QColor>
#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace md {

namespace {

// M3 measurements, in dp. Every value is converted through Density::dp().
constexpr qreal kBarHeightDp = 56.0;
constexpr qreal kBarWidthDp = 360.0;
constexpr qreal kHorizontalPaddingDp = 16.0;
constexpr qreal kLeadingIconDp = 24.0;
constexpr qreal kTextGapDp = 12.0;
constexpr qreal kClearTargetDp = 48.0;
constexpr qreal kSuggestionRowDp = 48.0;
constexpr qreal kSuggestionPaddingDp = 4.0;
constexpr qreal kSuggestionRadiusDp = 4.0;
constexpr int kMaxSuggestionRows = 6;

// The popup's chrome around the list: a 6 dp shadow margin on both sides plus
// the 4 dp content padding on both sides. The list asks for exactly the space
// that remains, so the popup ends up as wide as the bar.
constexpr qreal kPopupChromeDp = 20.0;

QColor withAlpha(const QColor& colour, qreal alpha)
{
    QColor result = colour;
    result.setAlphaF(static_cast<float>(std::clamp(alpha, qreal(0.0), qreal(1.0))));
    return result;
}

QFont themeFont(const Theme& theme, const TextStyle& style)
{
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(static_cast<QFont::Weight>(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
    return font;
}

QString elided(const QFont& font, const QString& text, qreal width)
{
    if (text.isEmpty() || width <= 0.0)
        return {};

    return QFontMetricsF(font).elidedText(text, Qt::ElideRight, width);
}

// ---------------------------------------------------------------------------
// SuggestionList — the private row surface inside the PopUp.
//
// The owner hands it the already filtered labels, so the list never filters.
// Rows are 48 dp high with a 4 dp inset; the highlighted row gets the same
// 8% onSurface wash the menu popup uses. Clicks are reported back as row
// indices and never take focus away from the QLineEdit.
// ---------------------------------------------------------------------------
class SuggestionList final : public View
{
public:
    explicit SuggestionList(const SearchBar* owner)
        : owner_(owner)
    {
        setBackgroundColour(QColor(Qt::transparent));
        setAccessibleName(QStringLiteral("Search suggestions"));
        setFocusPolicy(Qt::NoFocus);
    }

    void setItems(const QStringList& items)
    {
        items_ = items;
        update();
    }

    void setHighlighted(int row)
    {
        if (highlighted_ == row)
            return;

        highlighted_ = row;
        update();
    }

    void setActivateHandler(std::function<void(int)> handler)
    {
        activate_ = std::move(handler);
    }

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override
    {
        const Theme& theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const qreal padding = density.dp(kSuggestionPaddingDp);
        const qreal rowHeight = density.dp(kSuggestionRowDp);

        qreal preferredWidth = density.dp(280.0);
        if (owner_ != nullptr) {
            preferredWidth = std::max(0.0, static_cast<qreal>(owner_->width())
                                              - density.dp(kPopupChromeDp));
        }

        const qreal preferredHeight = padding * 2.0
            + rowHeight * static_cast<qreal>(items_.size());
        setMeasuredSize(QSizeF(width.resolve(preferredWidth), height.resolve(preferredHeight)));
    }

    void paintContent(QPainter& painter, const QRectF& content) override
    {
        if (content.isEmpty() || items_.isEmpty())
            return;

        const Theme& theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const ColorScheme& colours = theme.colours();
        const qreal padding = density.dp(kSuggestionPaddingDp);
        const qreal rowHeight = density.dp(kSuggestionRowDp);
        const qreal radius = density.dp(kSuggestionRadiusDp);
        const QFont font = themeFont(theme, theme.type().bodyLarge);
        const int rows = static_cast<int>(items_.size());

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);

        for (int row = 0; row < rows; ++row) {
            const QRectF rowRect(padding, padding + rowHeight * static_cast<qreal>(row),
                                 std::max(0.0, content.width() - padding * 2.0), rowHeight);

            if (row == highlighted_) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(withAlpha(colours.onSurface, 0.08));
                painter.drawRoundedRect(
                    rowRect.adjusted(0.0, density.dp(2.0), 0.0, -density.dp(2.0)),
                    radius, radius);
            }

            painter.setFont(font);
            painter.setPen(colours.onSurface);
            const QRectF textRect = rowRect.adjusted(density.dp(16.0), 0.0,
                                                     -density.dp(16.0), 0.0);
            painter.drawText(textRect, static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                             elided(font, items_.at(row), textRect.width()));
        }

        painter.restore();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            const int row = rowAt(event->position());
            if (row >= 0) {
                event->accept();
                if (activate_)
                    activate_(row);
                return;
            }
        }

        View::mousePressEvent(event);
    }

private:
    [[nodiscard]] int rowAt(const QPointF& position) const
    {
        const Density& density = Theme::forWidget(this).density();
        const qreal padding = density.dp(kSuggestionPaddingDp);
        const qreal rowHeight = density.dp(kSuggestionRowDp);
        if (rowHeight <= 0.0)
            return -1;

        const qreal local = position.y() - padding;
        if (local < 0.0)
            return -1;

        const int row = static_cast<int>(std::floor(local / rowHeight));
        return row >= 0 && row < static_cast<int>(items_.size()) ? row : -1;
    }

    const SearchBar* owner_ = nullptr;
    QStringList items_;
    int highlighted_ = -1;
    std::function<void(int)> activate_;
};

} // namespace

// ---------------------------------------------------------------------------
// SearchBar
// ---------------------------------------------------------------------------

SearchBar::SearchBar(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("Search"));
    syncAppearance();

    editor_ = new QLineEdit(this);
    editor_->setAccessibleName(QStringLiteral("Search"));
    editor_->setClearButtonEnabled(false);
    editor_->installEventFilter(this);
    setFocusProxy(editor_);
    applyThemeToEditor();

    connect(editor_, &QLineEdit::textChanged, this, [this](const QString& value) {
        handleTextChanged(value);
    });
    connect(editor_, &QLineEdit::selectionChanged, this, [this] { requestUpdate(); });

    requestUpdate();
}

SearchBar::~SearchBar()
{
    if (popup_ != nullptr) {
        popup_->close();
        delete popup_.data();
    }
}

void SearchBar::setPlaceholder(const QString& text)
{
    if (placeholder_ == text)
        return;

    placeholder_ = text;
    if (editor_ != nullptr)
        editor_->setPlaceholderText(placeholder_);

    requestUpdate();
}

QString SearchBar::placeholder() const
{
    return placeholder_;
}

void SearchBar::setText(const QString& text)
{
    if (editor_ != nullptr)
        editor_->setText(text);
}

QString SearchBar::text() const
{
    return editor_ != nullptr ? editor_->text() : QString();
}

void SearchBar::clear()
{
    if (editor_ == nullptr)
        return;

    hideSuggestions();
    editor_->clear();
    requestUpdate();
}

void SearchBar::setSuggestions(const QStringList& suggestions)
{
    hideSuggestions();
    suggestions_ = suggestions;
    rebuildFilter();
}

QStringList SearchBar::suggestions() const
{
    return suggestions_;
}

QLineEdit* SearchBar::editor() const noexcept
{
    return editor_;
}

QRectF SearchBar::clearButtonRect() const
{
    const QRectF content = contentRect();
    const Density& density = Theme::forWidget(this).density();
    const qreal side = density.dp(kClearTargetDp);
    const QPointF centre(content.right() - density.dp(kHorizontalPaddingDp)
                             - density.dp(kLeadingIconDp) / 2.0,
                         content.center().y());
    return QRectF(centre.x() - side / 2.0, centre.y() - side / 2.0, side, side);
}

void SearchBar::showSuggestions()
{
    rebuildFilter();
    if (filtered_.isEmpty()) {
        hideSuggestions();
        return;
    }

    highlighted_ = -1;
    refreshPopup();
}

void SearchBar::hideSuggestions()
{
    if (popup_ != nullptr && popup_->isOpen())
        popup_->close();

    highlighted_ = -1;
    if (list_ != nullptr)
        static_cast<SuggestionList*>(list_.data())->setHighlighted(-1);
}

bool SearchBar::isPopupVisible() const
{
    return popup_ != nullptr && popup_->isOpen();
}

int SearchBar::visibleSuggestionCount() const
{
    return static_cast<int>(filtered_.size());
}

int SearchBar::highlightedSuggestion() const noexcept
{
    return highlighted_;
}

QWidget* SearchBar::popupWidget() const noexcept
{
    return popup_.data();
}

void SearchBar::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    const qreal preferredWidth = density.dp(kBarWidthDp);
    const qreal preferredHeight = density.dp(kBarHeightDp);
    setMeasuredSize(QSizeF(width.resolve(preferredWidth), height.resolve(preferredHeight)));
}

void SearchBar::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const QColor iconColour = isEnabled() ? colours.onSurfaceVariant
                                          : withAlpha(colours.onSurfaceVariant, 0.38);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // The leading magnifier: a 24 dp box with a 16 dp inset, drawn as a ring
    // plus a short 45° handle.
    const QRectF iconRect(content.left() + density.dp(kHorizontalPaddingDp),
                          content.center().y() - density.dp(kLeadingIconDp) / 2.0,
                          density.dp(kLeadingIconDp), density.dp(kLeadingIconDp));
    painter.setPen(QPen(iconColour, density.dp(2.0), Qt::SolidLine, Qt::RoundCap,
                        Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    const QPointF lensCentre = iconRect.center() + QPointF(-density.dp(1.5),
                                                           -density.dp(1.5));
    const qreal lensRadius = density.dp(6.5);
    painter.drawEllipse(lensCentre, lensRadius, lensRadius);
    painter.drawLine(QPointF(lensCentre.x() + lensRadius * 0.72,
                             lensCentre.y() + lensRadius * 0.72),
                     QPointF(iconRect.right() - density.dp(3.0),
                             iconRect.bottom() - density.dp(3.0)));

    // The trailing clear affordance exists only while there is text to clear.
    if (clearVisible()) {
        const QRectF target = clearButtonRect();
        const QPointF centre = target.center();
        // The 48 dp target surrounds a 24 dp icon slot; the glyph uses the
        // same 14 dp span as Material's close icon.
        const qreal arm = density.dp(7.0);
        painter.setPen(QPen(iconColour, density.dp(2.0), Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(centre + QPointF(-arm, -arm), centre + QPointF(arm, arm));
        painter.drawLine(centre + QPointF(-arm, arm), centre + QPointF(arm, -arm));
    }

    painter.restore();
}

void SearchBar::arrangeOverride(const QRectF& content)
{
    Q_UNUSED(content);
    layoutEditor();
}

void SearchBar::resizeEvent(QResizeEvent* event)
{
    View::resizeEvent(event);
    layoutEditor();

    if (popup_ != nullptr && popup_->isOpen())
        refreshPopup();
}

void SearchBar::mousePressEvent(QMouseEvent* event)
{
    if (isEnabled() && event->button() == Qt::LeftButton && editor_ != nullptr) {
        if (clearVisible() && clearButtonRect().contains(event->position())) {
            clear();
            editor_->setFocus(Qt::MouseFocusReason);
            event->accept();
            return;
        }

        editor_->setFocus(Qt::MouseFocusReason);
    }

    View::mousePressEvent(event);
}

void SearchBar::keyPressEvent(QKeyEvent* event)
{
    // The editor is the focus proxy, so keys normally arrive there. A host may
    // still deliver a key to the bar itself (or a test may type into it), so
    // the editing engine receives it either way.
    if (editor_ != nullptr) {
        QCoreApplication::sendEvent(editor_, event);
        return;
    }

    View::keyPressEvent(event);
}

bool SearchBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == editor_) {
        switch (event->type()) {
        case QEvent::FocusIn:
            requestUpdate();
            break;
        case QEvent::FocusOut:
            hideSuggestions();
            requestUpdate();
            break;
        case QEvent::KeyPress: {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            switch (keyEvent->key()) {
            case Qt::Key_Down:
                if (moveHighlight(1)) {
                    keyEvent->accept();
                    return true;
                }
                break;
            case Qt::Key_Up:
                if (moveHighlight(-1)) {
                    keyEvent->accept();
                    return true;
                }
                break;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                if (isPopupVisible() && highlighted_ >= 0)
                    activateSuggestion(highlighted_);
                else
                    submit();

                keyEvent->accept();
                return true;
            case Qt::Key_Escape:
                if (isPopupVisible())
                    hideSuggestions();
                else
                    editor_->clearFocus();

                keyEvent->accept();
                return true;
            default:
                break;
            }
            break;
        }
        default:
            break;
        }
    }

    return View::eventFilter(watched, event);
}

void SearchBar::onThemeChanged()
{
    syncAppearance();
    applyThemeToEditor();
    layoutEditor();

    if (list_ != nullptr)
        list_->update();

    requestUpdate();
}

void SearchBar::onEnabledChanged(bool enabled)
{
    Q_UNUSED(enabled);

    applyThemeToEditor();
    layoutEditor();
    if (!isEnabled())
        hideSuggestions();

    requestUpdate();
}

bool SearchBar::clearVisible() const
{
    return isEnabled() && editor_ != nullptr && !editor_->text().isEmpty();
}

bool SearchBar::editorFocused() const
{
    return editor_ != nullptr && (editor_->hasFocus() || hasFocus());
}

void SearchBar::syncAppearance()
{
    const Theme& theme = Theme::forWidget(this);
    setBackgroundColour(theme.colours().surfaceContainerHigh);
    setShape(theme.shapes().full);
}

void SearchBar::applyThemeToEditor()
{
    if (editor_ == nullptr)
        return;

    const Theme& theme = Theme::forWidget(this);
    const bool enabled = isEnabled();

    editor_->setFrame(false);
    editor_->setStyleSheet(
        QStringLiteral("QLineEdit { background: transparent; border: 0; padding: 0px; }"));
    editor_->setFont(themeFont(theme, theme.type().bodyLarge));
    editor_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    QPalette palette = editor_->palette();
    palette.setColor(QPalette::Base, Qt::transparent);
    palette.setColor(QPalette::Text, enabled ? theme.colours().onSurface
                                             : withAlpha(theme.colours().onSurface, 0.38));
    palette.setColor(QPalette::PlaceholderText,
                     enabled ? theme.colours().onSurfaceVariant
                             : withAlpha(theme.colours().onSurfaceVariant, 0.38));
    palette.setColor(QPalette::Highlight, theme.colours().primary);
    palette.setColor(QPalette::HighlightedText, theme.colours().onPrimary);
    editor_->setPalette(palette);
}

void SearchBar::layoutEditor()
{
    if (editor_ == nullptr)
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QRectF content = contentRect();
    if (content.isEmpty())
        return;

    const qreal padding = density.dp(kHorizontalPaddingDp);
    const qreal icon = density.dp(kLeadingIconDp);
    const qreal gap = density.dp(kTextGapDp);
    const qreal left = content.left() + padding + icon + gap;
    const qreal right = content.right() - padding - (clearVisible() ? icon + gap : 0.0);

    const qreal lineHeight = std::max(density.dp(kLeadingIconDp),
                                      density.sp(theme.type().bodyLarge.lineHeightSp));
    const qreal editorHeight = std::min(content.height(), lineHeight);

    const int x = static_cast<int>(std::lround(left));
    const int y = static_cast<int>(std::lround(content.center().y() - editorHeight / 2.0));
    const int editorWidth = static_cast<int>(std::lround(std::max(0.0, right - left)));
    const int editorHeightPx = static_cast<int>(std::lround(editorHeight));
    editor_->setGeometry(x, y, editorWidth, editorHeightPx);
}

void SearchBar::rebuildFilter()
{
    filtered_.clear();

    const QString needle = text();
    for (const QString& suggestion : suggestions_) {
        if (needle.isEmpty() || suggestion.contains(needle, Qt::CaseInsensitive)) {
            filtered_.append(suggestion);
            if (static_cast<int>(filtered_.size()) >= kMaxSuggestionRows)
                break;
        }
    }
}

void SearchBar::ensurePopup()
{
    if (popup_ != nullptr)
        return;

    QWidget* host = window();
    if (host == nullptr)
        host = this;

    auto* popup = new PopUp(host);
    popup->setAnchor(this);
    popup->setPlacement(PopUp::Placement::Below);
    popup->setArrowVisible(false);
    popup->setContentPadding(kSuggestionPaddingDp);

    auto* list = new SuggestionList(this);
    list->setActivateHandler([this](int row) { activateSuggestion(row); });
    popup->setContent(list);

    list_ = list;
    popup_ = popup;

    connect(popup, &PopUp::closed, this, [this] {
        highlighted_ = -1;
        if (list_ != nullptr)
            static_cast<SuggestionList*>(list_.data())->setHighlighted(-1);
    });
}

void SearchBar::refreshPopup()
{
    ensurePopup();
    if (popup_ == nullptr || list_ == nullptr)
        return;

    // PopUp measures its content again on open(); closing first is what makes
    // a list whose row count changed pick up its new height.
    if (popup_->isOpen())
        popup_->close();

    static_cast<SuggestionList*>(list_.data())->setItems(filtered_);
    static_cast<SuggestionList*>(list_.data())->setHighlighted(highlighted_);
    popup_->open();
}

void SearchBar::handleTextChanged(const QString& value)
{
    emit textChanged(value);

    rebuildFilter();
    layoutEditor();
    requestUpdate();

    if (suppressSuggestions_)
        return;

    if (value.isEmpty() || filtered_.isEmpty()) {
        hideSuggestions();
        return;
    }

    if (editorFocused())
        showSuggestions();
    else
        hideSuggestions();
}

bool SearchBar::moveHighlight(int delta)
{
    if (filtered_.isEmpty())
        rebuildFilter();

    if (filtered_.isEmpty())
        return false;

    const int rows = static_cast<int>(filtered_.size());
    if (!isPopupVisible()) {
        showSuggestions();
        if (!isPopupVisible())
            return false;

        highlighted_ = delta > 0 ? 0 : rows - 1;
    } else if (highlighted_ < 0) {
        highlighted_ = delta > 0 ? 0 : rows - 1;
    } else {
        highlighted_ = (highlighted_ + delta + rows) % rows;
    }

    if (list_ != nullptr)
        static_cast<SuggestionList*>(list_.data())->setHighlighted(highlighted_);

    return true;
}

void SearchBar::activateSuggestion(int row)
{
    if (row < 0 || row >= static_cast<int>(filtered_.size()))
        return;

    const QString value = filtered_.at(row);

    suppressSuggestions_ = true;
    setText(value);
    suppressSuggestions_ = false;

    hideSuggestions();
    emit suggestionActivated(value);
}

void SearchBar::submit()
{
    const QString value = text();
    hideSuggestions();
    emit submitted(value);
}

} // namespace md
