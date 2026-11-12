// Periodicity — the input and selection components.
//
// The file is organised by control:
//
//   TextInput and its typed derivations (Email, Telephone, Regex, Number);
//   DropDown / Select and the menu popup they share with Complete;
//   Complete;
//   ColourPicker;
//   PickerBase and its two clients, NumberPicker and RingPicker.
//
// Two rules hold everywhere: the design baseline surface is painted by the component
// (a stock QComboBox or QSpinBox is never the final appearance), and data is
// read through md::Adapter so that a control never owns the application's
// model. A QLineEdit appears only as the transparent text-editing proxy of the
// text controls, and is placed by arrangeOverride() like any other child.

#include <md/components/input.h>

#include <md/core/geometry.h>
#include <md/theme/theme.h>
#include <md/tokens/state_layer.h>

#include <QAbstractItemModel>
#include <QDoubleValidator>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QRegularExpressionValidator>
#include <QScreen>
#include <QSortFilterProxyModel>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

namespace md {

namespace {

// Design baseline measurements, in dp. The components convert them through Density::dp().
constexpr qreal kFieldHeightDp = 56.0;
// Supporting text: the design baseline puts a bodySmall line (16 dp at the baseline scale) 4 dp
// below the field, so the supporting band stays 20 dp high.
constexpr qreal kSupportingLineDp = 16.0;
constexpr qreal kSupportingGapDp = 4.0;
constexpr qreal kFieldVerticalPaddingDp = 8.0;
constexpr qreal kHorizontalPaddingDp = 16.0;
constexpr qreal kFieldRadiusDp = 4.0;
constexpr qreal kMenuRadiusDp = 8.0;
constexpr qreal kMenuItemHeightDp = 48.0;
constexpr qreal kMenuPaddingDp = 4.0;
constexpr qreal kPickerWidthDp = 180.0;
constexpr qreal kPickerPaddingDp = 8.0;

QFont themeFont(const Theme& theme, const TextStyle& style)
{
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(static_cast<QFont::Weight>(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
    return font;
}

// The field is built from two type tokens, not from fixed insets: the floating
// label keeps its own line box (bodySmall) and the editor keeps bodyLarge's,
// with 8 dp above and below. At the baseline scales that is exactly 56 dp, so
// the default geometry is unchanged; at a larger font scale the field grows
// instead of letting the label overlap the input text.
qreal labelLineHeight(const Theme& theme)
{
    return theme.density().sp(theme.type().bodySmall.lineHeightSp);
}

qreal editorLineHeight(const Theme& theme)
{
    return theme.density().sp(theme.type().bodyLarge.lineHeightSp);
}

qreal fieldHeightFor(const Theme& theme)
{
    const qreal content = theme.density().dp(kFieldVerticalPaddingDp) + labelLineHeight(theme)
        + editorLineHeight(theme) + theme.density().dp(kFieldVerticalPaddingDp);
    return std::max(theme.density().dp(kFieldHeightDp), content);
}

// The supporting line's own line box, never smaller than the 16 dp bodySmall
// line the design baseline specifies: at a larger font scale the band grows with the text
// instead of clipping it.
qreal supportingHeightFor(const Theme& theme)
{
    return std::max(theme.density().dp(kSupportingLineDp),
                    theme.density().sp(theme.type().bodySmall.lineHeightSp));
}

qreal floatingLabelTop(const Theme& theme)
{
    return theme.density().dp(kFieldVerticalPaddingDp);
}

qreal editorTop(const Theme& theme)
{
    return floatingLabelTop(theme) + labelLineHeight(theme);
}

QColor withAlpha(const QColor& colour, qreal alpha)
{
    QColor result = colour;
    result.setAlphaF(static_cast<float>(std::clamp(alpha, 0.0, 1.0)));
    return result;
}

// Linear cross-fade between two colours, used by the floating label while it
// moves between its resting and floating roles. Rounding per channel keeps the
// two endpoints exact, so a fully settled label still paints the plain token
// colour.
QColor mixColour(const QColor& from, const QColor& to, qreal fraction)
{
    const qreal amount = std::clamp(fraction, 0.0, 1.0);
    const auto channel = [amount](int start, int end) {
        return static_cast<int>(qRound(start + (end - start) * amount));
    };

    return QColor(channel(from.red(), to.red()), channel(from.green(), to.green()),
                  channel(from.blue(), to.blue()), channel(from.alpha(), to.alpha()));
}

QString elided(const QFont& font, const QString& text, qreal width)
{
    if (text.isEmpty() || width <= 0.0)
        return {};

    return QFontMetricsF(font).elidedText(text, Qt::ElideRight, width);
}

// The filled text-field outline: rounded on the top corners only.
QPainterPath fieldPath(const QRectF& bounds, qreal radius)
{
    return CornerRadii{radius, radius, 0.0, 0.0}.toPath(bounds);
}

// Shared by both text controls: the editor is transparent and paints only its
// own text; the surface behind it belongs to the component.
void styleEditor(QLineEdit* editor, const Theme& theme, bool enabled)
{
    editor->setFrame(false);
    editor->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; border: 0; padding: 0px; }"));
    editor->setFont(themeFont(theme, theme.type().bodyLarge));
    editor->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    QPalette palette = editor->palette();
    palette.setColor(QPalette::Base, Qt::transparent);
    palette.setColor(QPalette::Text, enabled ? theme.colours().onSurface
                                             : withAlpha(theme.colours().onSurface, 0.38));
    palette.setColor(QPalette::PlaceholderText,
                     enabled ? theme.colours().onSurfaceVariant
                             : withAlpha(theme.colours().onSurfaceVariant, 0.38));
    palette.setColor(QPalette::Highlight, theme.colours().primary);
    palette.setColor(QPalette::HighlightedText, theme.colours().onPrimary);
    editor->setPalette(palette);
}

bool parseHexColour(const QString& text, QColor* colour, bool* hasAlpha)
{
    QString value = text.trimmed();
    if (value.startsWith(QLatin1Char('#')))
        value.remove(0, 1);

    if (value.size() == 3) {
        QString expanded;
        expanded.reserve(6);
        for (const QChar character : value) {
            expanded.append(character);
            expanded.append(character);
        }
        value = expanded;
    }

    if (value.size() != 6 && value.size() != 8)
        return false;

    bool ok = false;
    const uint parsed = value.toUInt(&ok, 16);
    if (!ok)
        return false;

    if (hasAlpha != nullptr)
        *hasAlpha = value.size() == 8;
    if (colour != nullptr) {
        *colour = value.size() == 6 ? QColor::fromRgb(parsed) : QColor::fromRgba(parsed);
    }
    return true;
}

// ---------------------------------------------------------------------------
// MenuPopup — the one list surface behind DropDown and Complete.
//
// A menu popup is a Qt::Popup: it takes the keyboard and closes itself when the
// user clicks outside. A suggestion popup is a non-activating tool window, so
// the QLineEdit keeps the keyboard while the list is visible; Complete closes
// it explicitly (Escape, selection, focus loss).
// ---------------------------------------------------------------------------
class MenuPopup final : public QWidget
{
public:
    enum class Kind {
        Menu,
        Suggestion,
    };

    MenuPopup(QWidget* anchor, Kind kind)
        : QWidget(anchor)
        , anchor_(anchor)
        , kind_(kind)
    {
        if (kind_ == Kind::Menu) {
            setWindowFlags(Qt::Popup | Qt::FramelessWindowHint
                           | Qt::NoDropShadowWindowHint);
            setFocusPolicy(Qt::StrongFocus);
        } else {
            setWindowFlags(Qt::Tool | Qt::FramelessWindowHint
                           | Qt::NoDropShadowWindowHint | Qt::WindowDoesNotAcceptFocus);
            setAttribute(Qt::WA_ShowWithoutActivating, true);
            setFocusPolicy(Qt::NoFocus);
        }

        setMouseTracking(true);
    }

    void setModel(QAbstractItemModel* model)
    {
        if (model_ != model)
            resetRipple();
        model_ = model;
        update();
    }

    void setCurrentIndex(int index)
    {
        current_ = index;
        update();
    }

    void setHighlighted(int index)
    {
        highlighted_ = index;
        update();
    }

    [[nodiscard]] int highlighted() const noexcept { return highlighted_; }

    void setMaxRows(int rows)
    {
        maxRows_ = std::max(1, rows);
    }

    void setActivateHandler(std::function<void(int)> handler)
    {
        activate_ = std::move(handler);
    }

    void setDismissHandler(std::function<void()> handler)
    {
        dismiss_ = std::move(handler);
    }

    void openBelow(const QRect& anchorRect)
    {
        const int rows = visibleRows();
        if (rows <= 0) {
            hide();
            return;
        }

        const Theme& theme = Theme::forWidget(anchor_);
        const qreal rowHeight = theme.density().dp(kMenuItemHeightDp);
        const qreal padding = theme.density().dp(kMenuPaddingDp);
        const int width = std::max(anchorRect.width(), 1);
        const int height = static_cast<int>(std::lround(rows * rowHeight + padding * 2.0));

        int x = anchor_->mapToGlobal(anchorRect.bottomLeft() + QPoint(0, 1)).x();
        int y = anchor_->mapToGlobal(anchorRect.bottomLeft() + QPoint(0, 1)).y();
        const int above = anchor_->mapToGlobal(anchorRect.topLeft()).y() - height;

        if (const QScreen* screen = anchor_->screen()) {
            const QRect available = screen->availableGeometry();
            if (y + height > available.bottom() + 1)
                y = above;
            x = std::clamp(x, available.left(),
                           std::max(available.left(), available.right() + 1 - width));
            y = std::clamp(y, available.top(),
                           std::max(available.top(), available.bottom() + 1 - height));
        }

        setGeometry(x, y, width, height);
        // A row ripple belongs to one presentation of the menu; opening it
        // again must not resume the previous one.
        resetRipple();
        show();
        raise();

        if (kind_ == Kind::Menu)
            setFocus(Qt::PopupFocusReason);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const Theme& theme = Theme::forWidget(anchor_);
        const ColorScheme& colours = theme.colours();
        const qreal unit = theme.density().dp(1.0);
        const qreal radius = theme.density().dp(kMenuRadiusDp);
        const qreal padding = theme.density().dp(kMenuPaddingDp);
        const qreal rowHeight = theme.density().dp(kMenuItemHeightDp);

        const QRectF bounds = QRectF(rect()).adjusted(unit * 0.5, unit * 0.5,
                                                      -unit * 0.5, -unit * 0.5);
        QPainterPath surface;
        surface.addRoundedRect(bounds, radius, radius);
        painter.fillPath(surface, colours.surfaceContainerHigh);
        painter.setPen(QPen(colours.outlineVariant, unit));
        painter.drawPath(surface);

        painter.save();
        painter.setClipPath(surface);

        const QFont itemFont = themeFont(theme, theme.type().bodyLarge);
        painter.setFont(itemFont);
        const int rows = visibleRows();

        for (int row = 0; row < rows; ++row) {
            const QRectF rowRect(bounds.left() + padding,
                                 bounds.top() + padding + row * rowHeight,
                                 std::max(0.0, bounds.width() - padding * 2.0), rowHeight);

            if (row == current_ || row == highlighted_) {
                QPainterPath pill;
                pill.addRoundedRect(rowRect.adjusted(0.0, unit * 2.0, 0.0, -unit * 2.0),
                                    radius, radius);
                painter.fillPath(pill, row == current_ ? colours.secondaryContainer
                                                       : withAlpha(colours.onSurface, 0.08));
            }

            // A row is not a widget, so the press feedback is painted here: a
            // circle growing from the press point (or the row centre for a
            // keyboard move) clipped to its row, under the label. Only the
            // active row has a ripple; every other row paints nothing.
            if (row == rippleRow_ && rippleProgress_ > 0.0) {
                try {
                    const auto cornerDistance = [this, &rowRect](qreal x, qreal y) {
                        return std::hypot(x - rippleOrigin_.x(), y - rippleOrigin_.y());
                    };
                    const qreal reach = std::max({
                        cornerDistance(rowRect.left(), rowRect.top()),
                        cornerDistance(rowRect.right(), rowRect.top()),
                        cornerDistance(rowRect.left(), rowRect.bottom()),
                        cornerDistance(rowRect.right(), rowRect.bottom()),
                    });
                    const qreal rippleRadius =
                        reach * std::clamp(rippleProgress_, 0.0, 1.0);

                    if (rippleRadius > 0.0) {
                        painter.save();
                        painter.setClipRect(rowRect);
                        painter.setPen(Qt::NoPen);
                        painter.setBrush(applyStateLayer(colours.onSurface,
                                                         ViewState::Pressed));
                        painter.drawEllipse(rippleOrigin_, rippleRadius, rippleRadius);
                        painter.restore();
                    }
                } catch (...) {
                    // The ripple is decoration: a throwing paint helper must not
                    // take the menu surface down with it.
                }
            }

            const QString label = model_ != nullptr
                ? model_->data(model_->index(row, 0), Qt::DisplayRole).toString()
                : QString();
            const qreal textWidth = std::max(0.0, rowRect.width() - unit * 48.0);
            painter.setPen(row == current_ ? colours.onSecondaryContainer
                                           : colours.onSurface);
            painter.drawText(QRectF(rowRect.left() + unit * 12.0, rowRect.top(),
                                    textWidth, rowRect.height()),
                             static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                             elided(itemFont, label, textWidth));

            if (row == current_) {
                const QPointF centre(rowRect.right() - unit * 24.0, rowRect.center().y());
                QPainterPath check;
                check.moveTo(centre.x() - unit * 5.0, centre.y());
                check.lineTo(centre.x() - unit * 1.5, centre.y() + unit * 3.5);
                check.lineTo(centre.x() + unit * 5.0, centre.y() - unit * 4.0);
                painter.setPen(QPen(colours.primary, unit * 2.0, Qt::SolidLine,
                                    Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(check);
            }
        }

        painter.restore();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        const int row = rowAt(event->position());
        if (row >= 0 && row != highlighted_) {
            highlighted_ = row;
            update();
        }
        QWidget::mouseMoveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }

        const int row = rowAt(event->position());
        pressedRow_ = row;
        if (row >= 0) {
            // Activate on release so the pressed row can show its ripple; the
            // press itself only starts the animation.
            startRipple(row, event->position());
            event->accept();
            return;
        }

        QWidget::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            QWidget::mouseReleaseEvent(event);
            return;
        }

        const int row = rowAt(event->position());
        const int pressed = pressedRow_;
        pressedRow_ = -1;

        if (row >= 0 && row == pressed) {
            const int chosen = row;
            if (activate_)
                activate_(chosen);
            hide();
            event->accept();
            return;
        }

        // Releasing outside the pressed row leaves no held state behind; an
        // unfinished ripple still runs to its end and clears itself there.
        if (rippleProgress_ >= 1.0 && rippleRow_ >= 0) {
            rippleRow_ = -1;
            rippleProgress_ = 0.0;
            update();
        }
        QWidget::mouseReleaseEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        const int rows = visibleRows();
        if (rows <= 0) {
            QWidget::keyPressEvent(event);
            return;
        }

        switch (event->key()) {
        case Qt::Key_Up:
            moveHighlight(highlighted_ <= 0 ? rows - 1 : highlighted_ - 1);
            event->accept();
            return;
        case Qt::Key_Down:
            moveHighlight((highlighted_ + 1) % rows);
            event->accept();
            return;
        case Qt::Key_Home:
            moveHighlight(0);
            event->accept();
            return;
        case Qt::Key_End:
            moveHighlight(rows - 1);
            event->accept();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            // Keyboard activation gets the same row ripple as a press. A menu
            // selection closes the popup immediately, so this is the settled
            // frame's first step, not a transition the user waits on.
            if (highlighted_ >= 0 && highlighted_ < rows)
                startKeyboardRipple(highlighted_);
            if (highlighted_ >= 0 && highlighted_ < rows && activate_) {
                const int chosen = highlighted_;
                activate_(chosen);
            }
            hide();
            event->accept();
            return;
        case Qt::Key_Escape:
            hide();
            event->accept();
            return;
        default:
            break;
        }

        QWidget::keyPressEvent(event);
    }

    void wheelEvent(QWheelEvent* event) override
    {
        const int rows = visibleRows();
        const int delta = event->angleDelta().y();
        if (rows <= 0 || delta == 0) {
            event->ignore();
            return;
        }

        if (highlighted_ < 0)
            highlighted_ = 0;
        const int step = delta > 0 ? -1 : 1;
        highlighted_ = (highlighted_ + step + rows) % rows;
        update();
        event->accept();
    }

    void hideEvent(QHideEvent* event) override
    {
        // No frame timer may survive a closed menu, and no row keeps feedback
        // it cannot show.
        resetRipple();
        QWidget::hideEvent(event);
        if (dismiss_)
            dismiss_();
    }

private:
    [[nodiscard]] int visibleRows() const
    {
        return std::min(model_ != nullptr ? model_->rowCount() : 0, maxRows_);
    }

    // The painted row rectangle, shared by paint and the ripple origin so a
    // press at a row's edge is clamped to exactly the box the user sees.
    [[nodiscard]] QRectF rowRectFor(int row) const
    {
        const Theme& theme = Theme::forWidget(anchor_);
        const qreal unit = theme.density().dp(1.0);
        const qreal padding = theme.density().dp(kMenuPaddingDp);
        const qreal rowHeight = theme.density().dp(kMenuItemHeightDp);
        const QRectF bounds = QRectF(rect()).adjusted(unit * 0.5, unit * 0.5,
                                                      -unit * 0.5, -unit * 0.5);
        return QRectF(bounds.left() + padding,
                      bounds.top() + padding + row * rowHeight,
                      std::max(0.0, bounds.width() - padding * 2.0), rowHeight);
    }

    void resetRipple()
    {
        rippleAnimator_.stop();
        rippleRow_ = -1;
        pressedRow_ = -1;
        rippleProgress_ = 0.0;
    }

    // Grows the pressed row's state layer from `origin` over medium2. Reduced
    // motion, a zero-length token and a hidden popup paint the settled layer at
    // once; there are no intermediate frames to show.
    void startRipple(int row, const QPointF& origin)
    {
        if (row < 0 || row >= visibleRows())
            return;

        const QRectF rowRect = rowRectFor(row);
        rippleRow_ = row;
        rippleOrigin_ = QPointF(std::clamp(origin.x(), rowRect.left(), rowRect.right()),
                                std::clamp(origin.y(), rowRect.top(), rowRect.bottom()));
        rippleProgress_ = 0.0;

        const Theme& theme = Theme::forWidget(anchor_);
        const MotionSpec spec = theme.motion().medium2();
        rippleAnimator_.stop();

        if (theme.motion().reducedMotion || spec.duration.count() <= 0 || !isVisible()) {
            rippleProgress_ = 1.0;
            update();
            return;
        }

        rippleAnimator_.start(spec, [this](qreal fraction) {
            // The Animator swallows a throwing callback; the local guard keeps
            // the same isolation around the repaint this callback triggers.
            try {
                rippleProgress_ = std::clamp(fraction, 0.0, 1.0);
                // A keyboard ripple is not held by a press, so it clears once
                // it has settled. A mouse ripple stays while the button is down.
                if (rippleProgress_ >= 1.0 && pressedRow_ != rippleRow_) {
                    rippleRow_ = -1;
                    rippleProgress_ = 0.0;
                }
                update();
            } catch (...) {
            }
        });
        update();
    }

    void startKeyboardRipple(int row)
    {
        if (row < 0 || row >= visibleRows())
            return;

        startRipple(row, rowRectFor(row).center());
    }

    void moveHighlight(int row)
    {
        highlighted_ = row;
        update();
        // WCAG 2.5.7: the keyboard path gets the same press feedback as the
        // pointer path, with the ripple originating at the row centre.
        startKeyboardRipple(highlighted_);
    }

    [[nodiscard]] int rowAt(const QPointF& position) const
    {
        const Theme& theme = Theme::forWidget(anchor_);
        const qreal padding = theme.density().dp(kMenuPaddingDp);
        const qreal rowHeight = theme.density().dp(kMenuItemHeightDp);
        if (rowHeight <= 0.0)
            return -1;

        const qreal local = position.y() - padding;
        if (local < 0.0)
            return -1;

        const int row = static_cast<int>(std::floor(local / rowHeight));
        return row >= 0 && row < visibleRows() ? row : -1;
    }

    QWidget* anchor_ = nullptr;
    Kind kind_ = Kind::Menu;
    QAbstractItemModel* model_ = nullptr;
    int current_ = -1;
    int highlighted_ = -1;
    int maxRows_ = 8;
    // The row whose press feedback is being painted. The ripple is a painted
    // value, not a child widget: the popup draws every row itself.
    int rippleRow_ = -1;
    int pressedRow_ = -1;
    QPointF rippleOrigin_;
    qreal rippleProgress_ = 0.0;
    animation::Animator rippleAnimator_;
    std::function<void(int)> activate_;
    std::function<void()> dismiss_;
};

} // namespace

// ---------------------------------------------------------------------------
// TextInput
// ---------------------------------------------------------------------------

TextInput::TextInput(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundColour(QColor(Qt::transparent));

    editor_ = new QLineEdit(this);
    editor_->installEventFilter(this);
    editor_->setClearButtonEnabled(false);
    setFocusProxy(editor_);

    applyThemeToEditor();

    connect(editor_, &QLineEdit::textChanged, this, [this](const QString& changed) {
        onEditorTextChanged(changed);
        updateLabelTransition();
        emit textChanged(changed);
    });
    connect(editor_, &QLineEdit::editingFinished, this, &TextInput::editingFinished);
    connect(editor_, &QLineEdit::selectionChanged, this, [this] { requestUpdate(); });

    requestUpdate();
}

TextInput::~TextInput()
{
    // The embedded editor is a QObject child and is destroyed after this
    // object's derived part. During teardown it can lose focus and emit
    // editingFinished(), which would forward a TextInput signal from an object
    // whose dynamic type is already a base class - Qt asserts on that. Detach
    // the editor first so no signal or event can reach a half-destroyed object.
    if (editor_ != nullptr) {
        editor_->removeEventFilter(this);
        QObject::disconnect(editor_, nullptr, this, nullptr);
    }
}

QString TextInput::text() const
{
    return editor_ != nullptr ? editor_->text() : QString();
}

void TextInput::setText(const QString& text)
{
    if (editor_ != nullptr)
        editor_->setText(text);
}

void TextInput::setPlaceholderText(const QString& text)
{
    if (placeholder_ == text)
        return;

    placeholder_ = text;
    syncPlaceholder();
    requestUpdate();
}

QString TextInput::placeholderText() const
{
    return placeholder_;
}

void TextInput::setLabel(const QString& label)
{
    if (label_ == label)
        return;

    label_ = label;
    syncPlaceholder();
    updateGeometry();
    requestUpdate();
}

QString TextInput::label() const
{
    return label_;
}

void TextInput::setHelperText(const QString& text)
{
    if (helper_ == text)
        return;

    helper_ = text;
    updateGeometry();
    requestUpdate();
}

QString TextInput::helperText() const
{
    return helper_;
}

void TextInput::setErrorText(const QString& text)
{
    if (errorText_ == text)
        return;

    errorText_ = text;
    updateGeometry();
    requestUpdate();
}

QString TextInput::errorText() const
{
    return errorText_;
}

bool TextInput::hasError() const
{
    return !errorText_.isEmpty();
}

void TextInput::setPassword(bool password)
{
    if (password_ == password)
        return;

    password_ = password;
    if (editor_ != nullptr) {
        editor_->setEchoMode(password_ ? QLineEdit::Password : QLineEdit::Normal);
    }
    requestUpdate();
}

bool TextInput::isPassword() const
{
    return password_;
}

void TextInput::setClearButtonEnabled(bool enabled)
{
    if (clearEnabled_ == enabled)
        return;

    clearEnabled_ = enabled;
    layoutEditor();
    requestUpdate();
}

bool TextInput::isClearButtonEnabled() const
{
    return clearEnabled_;
}

bool TextInput::hasAcceptableInput() const
{
    if (hasError() || editor_ == nullptr)
        return false;

    return editor_->hasAcceptableInput();
}

void TextInput::setValidator(const QValidator* validator)
{
    if (editor_ == nullptr)
        return;

    editor_->setValidator(validator);
    revalidate();
}

const QValidator* TextInput::validator() const
{
    return editor_ != nullptr ? editor_->validator() : nullptr;
}

QLineEdit* TextInput::editor() const noexcept
{
    return editor_;
}

QRectF TextInput::fieldRect() const
{
    const QRectF content = contentRect();
    const Theme& theme = Theme::forWidget(this);
    const qreal height = fieldHeightFor(theme);
    return QRectF(content.left(), content.top(), content.width(),
                  std::min(content.height(), height));
}

QRectF TextInput::clearButtonRect() const
{
    const Theme& theme = Theme::forWidget(this);
    const qreal side = theme.density().dp(48.0);
    const QRectF field = fieldRect();
    return QRectF(field.right() - side, field.center().y() - side / 2.0, side, side);
}

qreal TextInput::labelProgress() const noexcept
{
    return labelProgress_;
}

void TextInput::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    const qreal preferredWidth = density.dp(240.0);
    qreal preferredHeight = fieldHeightFor(theme);
    if (hasSupportingText()) {
        preferredHeight += density.dp(kSupportingGapDp) + supportingHeightFor(theme);
    }

    setMeasuredSize(QSizeF(width.resolve(preferredWidth), height.resolve(preferredHeight)));
}

void TextInput::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const bool enabled = isEnabled();
    const bool focused = editorFocused();
    const bool invalid = hasError();

    const QRectF field = fieldRect();
    if (field.isEmpty())
        return;

    const qreal radius = density.dp(kFieldRadiusDp);
    const qreal padding = density.dp(kHorizontalPaddingDp);

    const QColor container = enabled ? colours.surfaceContainerHighest
                                     : withAlpha(colours.onSurface, 0.04);
    const QColor underline = !enabled
        ? withAlpha(colours.onSurface, 0.12)
        : invalid ? colours.error : focused ? colours.primary : colours.onSurfaceVariant;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(container);
    painter.drawPath(fieldPath(field, radius));

    const qreal underlineWidth = density.dp(focused || invalid ? 2.0 : 1.0);
    const qreal underlineY = field.bottom() - underlineWidth / 2.0;
    painter.setPen(QPen(underline, underlineWidth));
    painter.drawLine(QPointF(field.left(), underlineY), QPointF(field.right(), underlineY));

    if (!label_.isEmpty()) {
        // The label interpolates between the two settled layouts rather than
        // switching between them: at 0 it is bodyLarge, vertically centred in
        // the whole field; at 1 it is bodySmall on the floating line. The box
        // (top and height) is interpolated with the same fraction as the type,
        // so AlignVCenter produces a continuous path. The left edge is fixed at
        // the field padding in both states; only the reserved trailing space
        // grows with the progress, so the label never jumps horizontally.
        //
        // Colour is part of the same interpolation: a resting label is
        // onSurfaceVariant and a floating one is primary while the field is
        // focused (and stays onSurfaceVariant when it floats because it holds
        // text). Error and disabled keep their own roles at both ends.
        const qreal progress = std::clamp(labelProgress_, 0.0, 1.0);
        const TextStyle& restingStyle = theme.type().bodyLarge;
        const TextStyle& floatingStyle = theme.type().bodySmall;

        TextStyle labelStyle = restingStyle;
        labelStyle.sizeSp = restingStyle.sizeSp
            + (floatingStyle.sizeSp - restingStyle.sizeSp) * progress;
        labelStyle.trackingSp = restingStyle.trackingSp
            + (floatingStyle.trackingSp - restingStyle.trackingSp) * progress;
        labelStyle.weight = static_cast<int>(std::lround(
            restingStyle.weight + (floatingStyle.weight - restingStyle.weight) * progress));

        const QFont labelFont = themeFont(theme, labelStyle);
        painter.setFont(labelFont);

        const QColor restingColour = !enabled ? withAlpha(colours.onSurface, 0.38)
            : invalid ? colours.error : colours.onSurfaceVariant;
        const QColor floatingColour = !enabled ? withAlpha(colours.onSurface, 0.38)
            : invalid ? colours.error : focused ? colours.primary : colours.onSurfaceVariant;
        painter.setPen(mixColour(restingColour, floatingColour, progress));

        // The clear button's box is reserved proportionally, so a long label
        // shortens smoothly instead of being re-elided in one step.
        const bool clearVisible = clearEnabled_ && enabled && !text().isEmpty();
        const qreal reserved = clearVisible ? density.dp(48.0) * progress : 0.0;
        const qreal labelTop = field.top() + floatingLabelTop(theme) * progress;
        const qreal labelHeight = field.height()
            + (labelLineHeight(theme) - field.height()) * progress;
        const QRectF labelRect(field.left() + padding, labelTop,
                               std::max(0.0, field.width() - padding * 2.0 - reserved),
                               std::max(0.0, labelHeight));
        painter.drawText(labelRect, static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                         elided(labelFont, label_, labelRect.width()));
    }

    if (clearEnabled_ && enabled && !text().isEmpty()) {
        const QRectF button = clearButtonRect();
        const QPointF centre = button.center();
        const qreal arm = density.dp(7.0);
        painter.setPen(QPen(colours.onSurfaceVariant, density.dp(1.6), Qt::SolidLine,
                            Qt::RoundCap));
        painter.drawLine(centre + QPointF(-arm, -arm), centre + QPointF(arm, arm));
        painter.drawLine(centre + QPointF(-arm, arm), centre + QPointF(arm, -arm));
    }

    if (hasSupportingText()) {
        const QFont supportingFont = themeFont(theme, theme.type().bodySmall);
        painter.setFont(supportingFont);
        painter.setPen(!enabled ? withAlpha(colours.onSurfaceVariant, 0.38)
                                : invalid ? colours.error : colours.onSurfaceVariant);
        const QRectF line(field.left() + padding,
                          field.bottom() + density.dp(kSupportingGapDp),
                          std::max(0.0, field.width() - padding * 2.0),
                          supportingHeightFor(theme));
        const QString supporting = invalid ? errorText_ : helper_;
        painter.drawText(line, static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                         elided(supportingFont, supporting, line.width()));
    }

    painter.restore();
}

void TextInput::arrangeOverride(const QRectF& content)
{
    Q_UNUSED(content);
    layoutEditor();
}

void TextInput::resizeEvent(QResizeEvent* event)
{
    View::resizeEvent(event);
    layoutEditor();
}

void TextInput::mousePressEvent(QMouseEvent* event)
{
    if (isEnabled() && event->button() == Qt::LeftButton && editor_ != nullptr) {
        if (clearEnabled_ && !text().isEmpty()
            && clearButtonRect().contains(event->position())) {
            editor_->clear();
            editor_->setFocus(Qt::MouseFocusReason);
            emit clearClicked();
            event->accept();
            return;
        }

        editor_->setFocus(Qt::MouseFocusReason);
    }

    View::mousePressEvent(event);
}

void TextInput::changeEvent(QEvent* event)
{
    View::changeEvent(event);

    if (event->type() == QEvent::ThemeChange) {
        applyThemeToEditor();
        // Turning reduced motion on while the label is in flight must not
        // leave a frame timer running: settle at the state the field reports.
        if (Theme::forWidget(this).motion().reducedMotion)
            settleLabel();
        requestUpdate();
    }
}

bool TextInput::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == editor_) {
        switch (event->type()) {
        case QEvent::FocusIn:
            focused_ = true;
            onEditorFocusChanged(true);
            updateLabelTransition();
            requestUpdate();
            break;
        case QEvent::FocusOut:
            focused_ = false;
            onEditorFocusChanged(false);
            updateLabelTransition();
            requestUpdate();
            break;
        case QEvent::KeyPress: {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (processEditorKey(keyEvent))
                return true;
            break;
        }
        default:
            break;
        }
    }

    return View::eventFilter(watched, event);
}

bool TextInput::focusable() const
{
    return isEnabled();
}

QString TextInput::validationError() const
{
    return {};
}

void TextInput::onEditorTextChanged(const QString& text)
{
    Q_UNUSED(text);
    revalidate();
    requestUpdate();
}

void TextInput::onEditorFocusChanged(bool focused)
{
    Q_UNUSED(focused);
}

bool TextInput::processEditorKey(QKeyEvent* event)
{
    Q_UNUSED(event);
    return false;
}

void TextInput::revalidate()
{
    const QString next = validationError();
    if (next == errorText_)
        return;

    errorText_ = next;
    updateGeometry();
    requestUpdate();
}

bool TextInput::editorFocused() const
{
    return focused_ || (editor_ != nullptr && editor_->hasFocus());
}

qreal TextInput::labelTarget() const
{
    // The label floats while the editor has focus or holds text. focused_ is
    // maintained by the event filter, so a FocusOut is reflected here even
    // while Qt is still completing the focus transfer.
    return (focused_ || !text().isEmpty()) ? 1.0 : 0.0;
}

void TextInput::settleLabel()
{
    labelAnimator_.stop();
    labelProgress_ = labelTarget();
    requestUpdate();
}

void TextInput::updateLabelTransition()
{
    const qreal target = labelTarget();

    // A field without a label has nothing to move, and a hidden field has no
    // frames to show: both settle at the target instead of keeping a frame
    // timer alive.
    if (label_.isEmpty() || !isVisible()) {
        labelAnimator_.stop();
        labelProgress_ = target;
        requestUpdate();
        return;
    }

    const Theme& theme = Theme::forWidget(this);
    const MotionSpec spec = theme.motion().medium2();
    if (theme.motion().reducedMotion || spec.duration.count() <= 0) {
        labelAnimator_.stop();
        labelProgress_ = target;
        requestUpdate();
        return;
    }

    if (std::abs(labelProgress_ - target) < 0.001) {
        labelAnimator_.stop();
        labelProgress_ = target;
        requestUpdate();
        return;
    }

    // Stop first and ramp again from the painted value. The Animator's own
    // retargeting starts the new run at its previous progress, which this
    // callback would then apply on top of `from` a second time.
    const qreal from = labelProgress_;
    labelAnimator_.stop();
    labelAnimator_.start(spec, [this, from, target](qreal fraction) {
        // The Animator already swallows a throwing callback; the local guard
        // keeps the same isolation here so a paint-side helper can never unwind
        // through Qt's animation stack.
        try {
            labelProgress_ = from + (target - from) * std::clamp(fraction, 0.0, 1.0);
            requestUpdate();
        } catch (...) {
        }
    });
    requestUpdate();
}

void TextInput::onShown()
{
    // A hidden field does not tick, so show is a settlement point: the state
    // that changed while hidden is painted exactly, and the next focus or text
    // change starts its own transition.
    settleLabel();
}

void TextInput::onHidden()
{
    // The end state is what matters when the field is shown again; leaving a
    // frame timer running while nothing can be seen would be wasted work.
    settleLabel();
}

bool TextInput::hasSupportingText() const
{
    return !errorText_.isEmpty() || !helper_.isEmpty();
}

void TextInput::applyThemeToEditor()
{
    if (editor_ == nullptr)
        return;

    styleEditor(editor_, Theme::forWidget(this), isEnabled());
    syncPlaceholder();
}

void TextInput::layoutEditor()
{
    if (editor_ == nullptr)
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QRectF field = fieldRect();
    if (field.isEmpty())
        return;

    const qreal padding = density.dp(kHorizontalPaddingDp);
    const qreal top = field.top() + editorTop(theme);
    const qreal bottom = field.bottom() - density.dp(kFieldVerticalPaddingDp);
    qreal right = field.right() - padding;
    if (clearEnabled_)
        right -= density.dp(32.0);

    const int editorWidth = static_cast<int>(std::max(0.0, right - (field.left() + padding)));
    const int editorHeight = static_cast<int>(std::max(0.0, bottom - top));
    editor_->setGeometry(static_cast<int>(std::lround(field.left() + padding)),
                         static_cast<int>(std::lround(top)), editorWidth, editorHeight);
}

void TextInput::syncPlaceholder()
{
    if (editor_ == nullptr)
        return;

    // With a floating label the resting label is the placeholder; showing a
    // second one underneath it would overlap the label.
    editor_->setPlaceholderText(label_.isEmpty() ? placeholder_ : QString());
}

// ---------------------------------------------------------------------------
// TextInput derivations
// ---------------------------------------------------------------------------

namespace {

class EmailValidator final : public QValidator
{
public:
    using QValidator::QValidator;

    State validate(QString& input, int&) const override
    {
        if (input.isEmpty())
            return Intermediate;

        static const QRegularExpression expression(
            QStringLiteral("^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}$"));
        return expression.match(input).hasMatch() ? Acceptable : Invalid;
    }
};

class TelephoneValidator final : public QValidator
{
public:
    using QValidator::QValidator;

    State validate(QString& input, int&) const override
    {
        if (input.isEmpty())
            return Intermediate;

        static const QRegularExpression expression(QStringLiteral("^\\+?[0-9 ()-]{7,24}$"));
        return expression.match(input).hasMatch() ? Acceptable : Invalid;
    }
};

} // namespace

EmailInput::EmailInput(QWidget* parent)
    : TextInput(parent)
{
    setValidator(new EmailValidator(this));
    setPlaceholderText(QStringLiteral("name@example.com"));
}

QString EmailInput::validationError() const
{
    if (text().isEmpty() || editor() == nullptr)
        return {};

    return editor()->hasAcceptableInput() ? QString()
                                          : QStringLiteral("Enter a valid email address");
}

TelephoneInput::TelephoneInput(QWidget* parent)
    : TextInput(parent)
{
    setValidator(new TelephoneValidator(this));
    setPlaceholderText(QStringLiteral("+1 555 0100"));
}

QString TelephoneInput::validationError() const
{
    if (text().isEmpty() || editor() == nullptr)
        return {};

    return editor()->hasAcceptableInput() ? QString()
                                          : QStringLiteral("Enter a valid telephone number");
}

RegexInput::RegexInput(QWidget* parent)
    : TextInput(parent)
{
    regexValidator_ = new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral(".*")), this);
    setValidator(regexValidator_);
}

void RegexInput::setPattern(const QRegularExpression& pattern)
{
    regexValidator_->setRegularExpression(pattern);
    revalidate();
    requestUpdate();
}

QRegularExpression RegexInput::pattern() const
{
    return regexValidator_->regularExpression();
}

QString RegexInput::validationError() const
{
    if (text().isEmpty() || editor() == nullptr)
        return {};

    return editor()->hasAcceptableInput() ? QString()
                                          : QStringLiteral("Does not match the required format");
}

// ---------------------------------------------------------------------------
// NumberInput
// ---------------------------------------------------------------------------

NumberInput::NumberInput(QWidget* parent)
    : TextInput(parent)
{
    auto* numberValidator = new QDoubleValidator(this);
    numberValidator->setNotation(QDoubleValidator::StandardNotation);
    numberValidator->setLocale(QLocale::c());
    numberValidator->setDecimals(10);
    numberValidator->setRange(-1.0e300, 1.0e300, 10);
    setValidator(numberValidator);

    commit(0.0);
}

NumberInput::~NumberInput() = default;

void NumberInput::setRange(double minimum, double maximum)
{
    double low = minimum;
    double high = maximum;
    if (low > high)
        std::swap(low, high);

    if (low == minimum_ && high == maximum_) {
        revalidate();
        requestUpdate();
        return;
    }

    minimum_ = low;
    maximum_ = high;

    const double clamped = clampToRange(lastValue_);
    if (clamped != lastValue_)
        commit(clamped);
    else {
        revalidate();
        requestUpdate();
    }
}

double NumberInput::minimum() const noexcept
{
    return minimum_;
}

double NumberInput::maximum() const noexcept
{
    return maximum_;
}

void NumberInput::setStep(double step)
{
    const double next = step > 0.0 ? step : 1.0;
    if (next == step_)
        return;

    step_ = next;
}

double NumberInput::step() const noexcept
{
    return step_;
}

double NumberInput::value() const
{
    double parsed = 0.0;
    if (parseValue(text(), &parsed))
        return clampToRange(parsed);

    return lastValue_;
}

void NumberInput::setValue(double value)
{
    commit(value);
}

void NumberInput::stepBy(double steps)
{
    if (step_ <= 0.0)
        return;

    commit(lastValue_ + steps * step_);
}

QString NumberInput::validationError() const
{
    const QString current = text().trimmed();
    if (current.isEmpty())
        return {};

    double parsed = 0.0;
    if (!parseValue(current, &parsed))
        return QStringLiteral("Enter a number");

    if (parsed < minimum_ || parsed > maximum_)
        return QStringLiteral("Value is outside the allowed range");

    return {};
}

void NumberInput::onEditorTextChanged(const QString& changed)
{
    if (!updating_) {
        double parsed = 0.0;
        if (parseValue(changed, &parsed) && parsed >= minimum_ && parsed <= maximum_) {
            if (parsed != lastValue_) {
                lastValue_ = parsed;
                emit valueChanged(parsed);
            }
        }
    }

    TextInput::onEditorTextChanged(changed);
}

bool NumberInput::processEditorKey(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Up:
        stepBy(1.0);
        event->accept();
        return true;
    case Qt::Key_Down:
        stepBy(-1.0);
        event->accept();
        return true;
    case Qt::Key_PageUp:
        stepBy(10.0);
        event->accept();
        return true;
    case Qt::Key_PageDown:
        stepBy(-10.0);
        event->accept();
        return true;
    default:
        break;
    }

    return TextInput::processEditorKey(event);
}

double NumberInput::clampToRange(double value) const
{
    return std::clamp(value, minimum_, maximum_);
}

bool NumberInput::parseValue(const QString& text, double* value) const
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return false;

    bool ok = false;
    const double parsed = trimmed.toDouble(&ok);
    if (!ok)
        return false;

    if (value != nullptr)
        *value = parsed;
    return true;
}

void NumberInput::commit(double value)
{
    const double clamped = clampToRange(value);
    const bool changed = clamped != lastValue_;

    lastValue_ = clamped;

    updating_ = true;
    setText(QString::number(clamped, 'g', 12));
    updating_ = false;

    revalidate();
    requestUpdate();

    if (changed)
        emit valueChanged(clamped);
}

// ---------------------------------------------------------------------------
// DropDown
// ---------------------------------------------------------------------------

DropDown::DropDown(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover, true);
    setBackgroundColour(QColor(Qt::transparent));
    setShape(Theme::forWidget(this).shapes().extraSmall);

    adapter_ = new Adapter(this);
    requestUpdate();
}

DropDown::~DropDown()
{
    if (popup_ != nullptr) {
        auto* menu = static_cast<MenuPopup*>(popup_);
        menu->setActivateHandler({});
        menu->setDismissHandler({});
        menu->hide();
    }
}

void DropDown::setSourceModel(QAbstractItemModel* model)
{
    if (adapter_->sourceModel() == model)
        return;

    hidePopup();

    const int previousIndex = current_;
    const QVariant previousValue = currentValue();

    setAdapterSource(nullptr);
    ownedModel_.reset();
    setAdapterSource(model);

    current_ = count() > 0 ? 0 : -1;
    requestUpdate();

    if (previousIndex != current_)
        emit currentIndexChanged(current_);
    if (previousValue != currentValue())
        emit currentValueChanged(currentValue());
}

QAbstractItemModel* DropDown::sourceModel() const noexcept
{
    return adapter_->sourceModel();
}

Adapter* DropDown::adapter() const noexcept
{
    return adapter_;
}

void DropDown::setFormatter(Adapter::Formatter formatter)
{
    adapter_->setLabelFor(std::move(formatter));
    requestUpdate();
}

void DropDown::addItem(const QString& text, const QVariant& value)
{
    ensureOwnedModel();
    ownedModel_->append(value.isValid() ? value : QVariant(text), text);

    if (current_ < 0 || current_ >= count())
        current_ = count() > 0 ? 0 : -1;

    updateGeometry();
    requestUpdate();
}

void DropDown::addItems(const QStringList& texts)
{
    for (const QString& text : texts)
        addItem(text);
}

int DropDown::count() const
{
    return adapter_->rowCount();
}

QString DropDown::itemText(int index) const
{
    if (index < 0 || index >= count())
        return {};

    return adapter_->text(index);
}

QVariant DropDown::itemValue(int index) const
{
    if (index < 0 || index >= count())
        return {};

    return adapter_->value(index);
}

int DropDown::currentIndex() const noexcept
{
    return current_;
}

void DropDown::setCurrentIndex(int index)
{
    const int rows = count();
    if (rows <= 0) {
        if (current_ != -1) {
            current_ = -1;
            requestUpdate();
            emit currentIndexChanged(current_);
            emit currentValueChanged(currentValue());
        }
        return;
    }

    const int next = std::clamp(index, 0, rows - 1);
    if (next == current_)
        return;

    current_ = next;
    requestUpdate();
    emit currentIndexChanged(current_);
    emit currentValueChanged(currentValue());
}

QString DropDown::currentText() const
{
    if (current_ >= 0 && current_ < count())
        return adapter_->text(current_);

    return placeholder_;
}

QVariant DropDown::currentValue() const
{
    if (current_ >= 0 && current_ < count())
        return adapter_->value(current_);

    return {};
}

void DropDown::setCurrentValue(const QVariant& value)
{
    const int rows = count();
    for (int row = 0; row < rows; ++row) {
        if (adapter_->value(row) == value) {
            setCurrentIndex(row);
            return;
        }
    }
}

void DropDown::setPlaceholderText(const QString& text)
{
    if (placeholder_ == text)
        return;

    placeholder_ = text;
    requestUpdate();
}

QString DropDown::placeholderText() const
{
    return placeholder_;
}

void DropDown::setMaxVisibleItems(int items)
{
    maxVisibleItems_ = std::max(1, items);
}

int DropDown::maxVisibleItems() const noexcept
{
    return maxVisibleItems_;
}

void DropDown::showPopup()
{
    if (count() <= 0)
        return;

    if (popup_ == nullptr)
        popup_ = new MenuPopup(this, MenuPopup::Kind::Menu);

    auto* menu = static_cast<MenuPopup*>(popup_);
    menu->setModel(adapter_);
    menu->setCurrentIndex(current_);
    menu->setMaxRows(maxVisibleItems_);
    menu->setHighlighted(current_ >= 0 ? current_ : 0);
    menu->setActivateHandler([this](int row) { handlePopupActivated(row); });
    menu->setDismissHandler([this] { handlePopupDismissed(); });
    menu->openBelow(popupAnchorRect());

    popupOpen_ = menu->isVisible();
}

void DropDown::hidePopup()
{
    if (popup_ != nullptr)
        popup_->hide();

    popupOpen_ = false;
}

bool DropDown::isPopupVisible() const
{
    return popupOpen_ && popup_ != nullptr && popup_->isVisible();
}

QWidget* DropDown::popupWidget() const noexcept
{
    return popup_;
}

void DropDown::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    setMeasuredSize(QSizeF(width.resolve(density.dp(240.0)),
                           height.resolve(density.dp(kFieldHeightDp))));
}

void DropDown::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const bool enabled = isEnabled();
    const bool focused = hasFocus();

    const QRectF field(content.left(), content.top(), content.width(),
                       std::min(content.height(), density.dp(kFieldHeightDp)));
    if (field.isEmpty())
        return;

    const qreal padding = density.dp(kHorizontalPaddingDp);
    const qreal radius = density.dp(kFieldRadiusDp);

    const QColor container = enabled ? colours.surfaceContainerHighest
                                     : withAlpha(colours.onSurface, 0.04);
    const QColor underline = !enabled
        ? withAlpha(colours.onSurface, 0.12)
        : focused ? colours.primary : colours.outline;
    const QColor textColour = current_ >= 0
        ? (enabled ? colours.onSurface : withAlpha(colours.onSurface, 0.38))
        : (enabled ? colours.onSurfaceVariant : withAlpha(colours.onSurfaceVariant, 0.38));

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(container);
    painter.drawPath(fieldPath(field, radius));

    const qreal underlineWidth = density.dp(focused ? 2.0 : 1.0);
    painter.setPen(QPen(underline, underlineWidth));
    const qreal underlineY = field.bottom() - underlineWidth / 2.0;
    painter.drawLine(QPointF(field.left(), underlineY), QPointF(field.right(), underlineY));

    const QFont valueFont = themeFont(theme, theme.type().bodyLarge);
    painter.setFont(valueFont);
    painter.setPen(textColour);

    const QString label = current_ >= 0 ? adapter_->text(current_) : placeholder_;
    const qreal textWidth = std::max(0.0, field.width() - padding * 2.0 - density.dp(32.0));
    painter.drawText(QRectF(field.left() + padding, field.top(), textWidth, field.height()),
                     static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                     elided(valueFont, label, textWidth));

    const QPointF chevron(field.right() - density.dp(24.0), field.center().y());
    QPainterPath arrow;
    arrow.moveTo(chevron.x() - density.dp(5.0), chevron.y() - density.dp(2.5));
    arrow.lineTo(chevron.x(), chevron.y() + density.dp(2.5));
    arrow.lineTo(chevron.x() + density.dp(5.0), chevron.y() - density.dp(2.5));
    painter.setPen(QPen(enabled ? colours.onSurfaceVariant
                                : withAlpha(colours.onSurfaceVariant, 0.38),
                        density.dp(2.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(arrow);

    painter.restore();
}

void DropDown::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        pressed_ = QRectF(rect()).contains(event->position());

    View::mousePressEvent(event);
}

void DropDown::mouseReleaseEvent(QMouseEvent* event)
{
    const bool inside = QRectF(rect()).contains(event->position());
    const bool activate = pressed_ && inside && event->button() == Qt::LeftButton;
    pressed_ = false;

    View::mouseReleaseEvent(event);

    if (!activate || !isEnabled())
        return;

    if (isPopupVisible())
        hidePopup();
    else
        showPopup();
}

void DropDown::keyPressEvent(QKeyEvent* event)
{
    if (!isEnabled()) {
        View::keyPressEvent(event);
        return;
    }

    const int rows = count();

    switch (event->key()) {
    case Qt::Key_Down:
        if (rows > 0)
            setCurrentIndex(current_ < 0 ? 0 : current_ + 1);
        event->accept();
        return;
    case Qt::Key_Up:
        if (rows > 0)
            setCurrentIndex(current_ <= 0 ? 0 : current_ - 1);
        event->accept();
        return;
    case Qt::Key_Home:
        if (rows > 0)
            setCurrentIndex(0);
        event->accept();
        return;
    case Qt::Key_End:
        if (rows > 0)
            setCurrentIndex(rows - 1);
        event->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        showPopup();
        event->accept();
        return;
    default:
        break;
    }

    View::keyPressEvent(event);
}

void DropDown::changeEvent(QEvent* event)
{
    View::changeEvent(event);

    if (event->type() == QEvent::ThemeChange) {
        setShape(Theme::forWidget(this).shapes().extraSmall);
        requestUpdate();
    }
}

bool DropDown::focusable() const
{
    return isEnabled();
}

QRect DropDown::popupAnchorRect() const
{
    return QRect(0, 0, width(), height());
}

void DropDown::ensureOwnedModel()
{
    if (ownedModel_)
        return;

    ownedModel_ = std::make_unique<Model>();
    setAdapterSource(ownedModel_.get());
}

void DropDown::setAdapterSource(QAbstractItemModel* model)
{
    QAbstractItemModel* previous = adapter_->sourceModel();
    if (previous == model)
        return;

    if (previous != nullptr)
        QObject::disconnect(previous, nullptr, this, nullptr);

    adapter_->setSourceModel(model);

    if (model == nullptr)
        return;

    // A reset or a row removal can invalidate the held index: the handler
    // clamps it to the new count and reports the change. The try/catch keeps a
    // model that throws while it mutates from breaking the widget.
    connect(model, &QAbstractItemModel::rowsInserted, this, [this] {
        try {
            refreshFromModel();
        } catch (...) {
        }
    });
    connect(model, &QAbstractItemModel::rowsRemoved, this, [this] {
        try {
            refreshFromModel();
        } catch (...) {
        }
    });
    connect(model, &QAbstractItemModel::modelReset, this, [this] {
        try {
            refreshFromModel();
        } catch (...) {
        }
    });
}

void DropDown::refreshFromModel()
{
    const int rows = count();
    const int previousIndex = current_;

    current_ = rows <= 0 ? -1 : std::clamp(current_, 0, rows - 1);
    requestUpdate();

    if (previousIndex == current_)
        return;

    emit currentIndexChanged(current_);

    // The model has already shrunk by the time this slot runs, so the value the
    // control held before the change can no longer be read back from it. The
    // value follows the index: report it whenever the clamp moves the row, and
    // report the invalid value when the model emptied (-1 has no value).
    emit currentValueChanged(currentValue());
}

void DropDown::handlePopupActivated(int row)
{
    setCurrentIndex(row);
    emit activated(row);
    hidePopup();
}

void DropDown::handlePopupDismissed()
{
    popupOpen_ = false;
    requestUpdate();

    if (isVisible())
        setFocus(Qt::PopupFocusReason);
}

// ---------------------------------------------------------------------------
// Complete
// ---------------------------------------------------------------------------

Complete::Complete(QWidget* parent)
    : TextInput(parent)
{
    adapter_ = new Adapter(this);

    filter_ = new QSortFilterProxyModel(this);
    filter_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    filter_->setFilterRole(Qt::DisplayRole);
    filter_->setSourceModel(adapter_);

    setClearButtonEnabled(false);
}

Complete::~Complete()
{
    if (popup_ != nullptr) {
        auto* menu = static_cast<MenuPopup*>(popup_);
        menu->setActivateHandler({});
        menu->setDismissHandler({});
        menu->hide();
    }
}

void Complete::setSuggestions(const QStringList& items)
{
    QVariantList values;
    values.reserve(items.size());
    for (const QString& item : items)
        values.append(item);

    ownedModel_ = std::make_unique<Model>();
    ownedModel_->setValues(std::move(values));

    adapter_->setSourceModel(nullptr);
    adapter_->setSourceModel(ownedModel_.get());
    filter_->invalidate();

    hideSuggestions();
}

QStringList Complete::suggestions() const
{
    QStringList result;
    const int rows = adapter_->rowCount();
    result.reserve(rows);
    for (int row = 0; row < rows; ++row)
        result.append(adapter_->text(row));
    return result;
}

void Complete::setSourceModel(QAbstractItemModel* model)
{
    if (adapter_->sourceModel() == model)
        return;

    hideSuggestions();

    adapter_->setSourceModel(nullptr);
    ownedModel_.reset();
    adapter_->setSourceModel(model);
    filter_->invalidate();
}

QAbstractItemModel* Complete::sourceModel() const noexcept
{
    return adapter_->sourceModel();
}

Adapter* Complete::adapter() const noexcept
{
    return adapter_;
}

void Complete::setFormatter(Adapter::Formatter formatter)
{
    adapter_->setLabelFor(std::move(formatter));
    filter_->invalidate();

    if (isPopupVisible())
        refreshPopup();
}

int Complete::visibleSuggestionCount() const
{
    return filter_ != nullptr ? filter_->rowCount() : 0;
}

int Complete::highlightedSuggestion() const noexcept
{
    return highlighted_;
}

void Complete::showSuggestions()
{
    if (filter_ == nullptr)
        return;

    applyFilter();
    if (filter_->rowCount() <= 0) {
        hideSuggestions();
        return;
    }

    refreshPopup();
}

void Complete::hideSuggestions()
{
    if (popup_ != nullptr)
        popup_->hide();

    highlighted_ = -1;
}

bool Complete::isPopupVisible() const
{
    return popup_ != nullptr && popup_->isVisible();
}

QWidget* Complete::popupWidget() const noexcept
{
    return popup_;
}

void Complete::onEditorTextChanged(const QString& changed)
{
    TextInput::onEditorTextChanged(changed);

    if (suppressFilter_ || filter_ == nullptr)
        return;

    applyFilter();

    if (isPopupVisible()) {
        if (changed.isEmpty() || filter_->rowCount() <= 0)
            hideSuggestions();
        else
            refreshPopup();
        return;
    }

    if (editorFocused() && !changed.isEmpty() && filter_->rowCount() > 0)
        showSuggestions();
}

void Complete::onEditorFocusChanged(bool focused)
{
    if (focused)
        return;

    // A click on the suggestion list itself must reach the popup, so the list
    // is only dismissed for a focus change that is not a click on it.
    if (popup_ != nullptr && popup_->isVisible() && popup_->underMouse())
        return;

    hideSuggestions();
}

bool Complete::processEditorKey(QKeyEvent* event)
{
    const int rows = visibleSuggestionCount();

    switch (event->key()) {
    case Qt::Key_Down:
        if (!isPopupVisible()) {
            if (rows > 0) {
                showSuggestions();
                event->accept();
                return true;
            }
            return false;
        }
        highlighted_ = rows > 0 ? (highlighted_ + 1) % rows : -1;
        if (popup_ != nullptr)
            static_cast<MenuPopup*>(popup_)->setHighlighted(highlighted_);
        event->accept();
        return true;
    case Qt::Key_Up:
        if (!isPopupVisible())
            return false;
        highlighted_ = rows > 0 ? (highlighted_ <= 0 ? rows - 1 : highlighted_ - 1) : -1;
        if (popup_ != nullptr)
            static_cast<MenuPopup*>(popup_)->setHighlighted(highlighted_);
        event->accept();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (isPopupVisible() && highlighted_ >= 0) {
            activateRow(highlighted_);
            event->accept();
            return true;
        }
        return false;
    case Qt::Key_Escape:
        if (isPopupVisible()) {
            hideSuggestions();
            event->accept();
            return true;
        }
        return false;
    case Qt::Key_Tab:
        hideSuggestions();
        return false;
    default:
        break;
    }

    return TextInput::processEditorKey(event);
}

void Complete::applyFilter()
{
    if (filter_ == nullptr)
        return;

    filter_->setFilterFixedString(text());
}

void Complete::refreshPopup()
{
    if (filter_ == nullptr || filter_->rowCount() <= 0) {
        hideSuggestions();
        return;
    }

    if (popup_ == nullptr)
        popup_ = new MenuPopup(this, MenuPopup::Kind::Suggestion);

    auto* menu = static_cast<MenuPopup*>(popup_);
    menu->setModel(filter_);
    menu->setCurrentIndex(-1);
    menu->setMaxRows(6);

    if (highlighted_ >= filter_->rowCount())
        highlighted_ = -1;

    menu->setHighlighted(highlighted_);
    menu->setActivateHandler([this](int row) { activateRow(row); });
    menu->setDismissHandler([this] { handlePopupDismissed(); });
    menu->openBelow(QRect(0, 0, width(), static_cast<int>(std::lround(fieldRect().height()))));
}

void Complete::activateRow(int row)
{
    if (filter_ == nullptr || row < 0 || row >= filter_->rowCount())
        return;

    const QModelIndex index = filter_->index(row, 0);
    const QVariant stored = index.data(adapter_->valueRole());
    const QString label = index.data(Qt::DisplayRole).toString();

    suppressFilter_ = true;
    setText(label);
    suppressFilter_ = false;

    hideSuggestions();
    emit suggestionActivated(row, stored);
}

void Complete::handlePopupDismissed()
{
    highlighted_ = -1;
}

// ---------------------------------------------------------------------------
// ColourPicker
// ---------------------------------------------------------------------------

ColourPicker::ColourPicker(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundColour(QColor(Qt::transparent));

    hexEditor_ = new QLineEdit(this);
    hexEditor_->installEventFilter(this);
    hexEditor_->setPlaceholderText(QStringLiteral("#RRGGBB"));
    hexEditor_->setClearButtonEnabled(false);

    applyColour(QColor(103, 80, 164));
}

ColourPicker::~ColourPicker()
{
    if (hexEditor_ != nullptr)
        hexEditor_->removeEventFilter(this);
}

QColor ColourPicker::colour() const
{
    return composed();
}

void ColourPicker::setColour(const QColor& colour)
{
    applyColour(colour);
}

int ColourPicker::hue() const noexcept
{
    return hue_;
}

int ColourPicker::saturation() const noexcept
{
    return saturation_;
}

int ColourPicker::value() const noexcept
{
    return value_;
}

int ColourPicker::alpha() const noexcept
{
    return alpha_;
}

void ColourPicker::setAlpha(int alpha)
{
    const int next = std::clamp(alpha, 0, 255);
    if (next == alpha_)
        return;

    alpha_ = next;
    syncHexEditor();
    requestUpdate();
    emit colourChanged(composed());
}

QString ColourPicker::hexText() const
{
    return hexEditor_ != nullptr ? hexEditor_->text() : QString();
}

void ColourPicker::setHexText(const QString& text)
{
    if (hexEditor_ == nullptr)
        return;

    syncingHex_ = true;
    hexEditor_->setText(text);
    syncingHex_ = false;
    commitHexEditor();
}

QLineEdit* ColourPicker::hexEditor() const noexcept
{
    return hexEditor_;
}

QRectF ColourPicker::saturationValueRect() const
{
    const Density& density = Theme::forWidget(this).density();
    const qreal margin = density.dp(12.0);
    const qreal gap = density.dp(8.0);
    const qreal hueHeight = density.dp(20.0);
    const qreal hexHeight = density.dp(48.0);

    const qreal width = std::max(0.0, static_cast<qreal>(this->width()) - margin * 2.0);
    const qreal height = std::max(0.0, static_cast<qreal>(this->height()) - margin * 2.0
                                           - hueHeight - hexHeight - gap * 2.0);
    return QRectF(margin, margin, width, height);
}

QRectF ColourPicker::hueBarRect() const
{
    const Density& density = Theme::forWidget(this).density();
    const qreal margin = density.dp(12.0);
    const qreal gap = density.dp(8.0);
    const qreal height = density.dp(20.0);
    const QRectF panel = saturationValueRect();
    const qreal width = std::max(0.0, (panel.width() - gap) / 2.0);
    return QRectF(margin, panel.bottom() + gap, width, height);
}

QRectF ColourPicker::alphaBarRect() const
{
    const QRectF hueRect = hueBarRect();
    const QRectF panel = saturationValueRect();
    const qreal gap = Theme::forWidget(this).density().dp(8.0);
    return QRectF(hueRect.right() + gap, hueRect.top(),
                  std::max(0.0, panel.width() - hueRect.width() - gap), hueRect.height());
}

QRectF ColourPicker::hexRect() const
{
    const Density& density = Theme::forWidget(this).density();
    const qreal margin = density.dp(12.0);
    const qreal gap = density.dp(8.0);
    const qreal height = density.dp(48.0);
    const QRectF hueRect = hueBarRect();
    return QRectF(margin, hueRect.bottom() + gap,
                  std::max(0.0, static_cast<qreal>(this->width()) - margin * 2.0), height);
}

void ColourPicker::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();

    const qreal preferredWidth = density.dp(280.0);
    const qreal preferredHeight = density.dp(12.0) * 2.0 + density.dp(140.0)
        + density.dp(20.0) + density.dp(48.0) + density.dp(8.0) * 2.0;

    setMeasuredSize(QSizeF(width.resolve(preferredWidth), height.resolve(preferredHeight)));
}

void ColourPicker::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const bool enabled = isEnabled();

    const QRectF panel = saturationValueRect();
    const QRectF hueRect = hueBarRect();
    const QRectF alphaRect = alphaBarRect();
    const QRectF hexArea = hexRect();
    const qreal radius = density.dp(8.0);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (!panel.isEmpty()) {
        QPainterPath clip;
        clip.addRoundedRect(panel, radius, radius);
        painter.save();
        painter.setClipPath(clip);
        painter.fillRect(panel, QColor::fromHsv(hue_, 255, 255));

        QLinearGradient whiteGradient(panel.topLeft(), panel.topRight());
        whiteGradient.setColorAt(0.0, QColor(255, 255, 255));
        whiteGradient.setColorAt(1.0, QColor(255, 255, 255, 0));
        painter.fillRect(panel, whiteGradient);

        QLinearGradient blackGradient(panel.topLeft(), panel.bottomLeft());
        blackGradient.setColorAt(0.0, QColor(0, 0, 0, 0));
        blackGradient.setColorAt(1.0, QColor(0, 0, 0));
        painter.fillRect(panel, blackGradient);

        const QPointF marker(panel.left() + panel.width() * saturation_ / 255.0,
                             panel.top() + panel.height() * (255 - value_) / 255.0);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(255, 255, 255), density.dp(2.0)));
        painter.drawEllipse(marker, density.dp(6.0), density.dp(6.0));
        painter.setPen(QPen(QColor(0, 0, 0, 120), density.dp(1.0)));
        painter.drawEllipse(marker, density.dp(7.5), density.dp(7.5));
        painter.restore();
    }

    if (!hueRect.isEmpty()) {
        QLinearGradient hueGradient(hueRect.topLeft(), hueRect.topRight());
        hueGradient.setColorAt(0.0, QColor::fromHsv(0, 255, 255));
        hueGradient.setColorAt(1.0 / 6.0, QColor::fromHsv(60, 255, 255));
        hueGradient.setColorAt(2.0 / 6.0, QColor::fromHsv(120, 255, 255));
        hueGradient.setColorAt(3.0 / 6.0, QColor::fromHsv(180, 255, 255));
        hueGradient.setColorAt(4.0 / 6.0, QColor::fromHsv(240, 255, 255));
        hueGradient.setColorAt(5.0 / 6.0, QColor::fromHsv(300, 255, 255));
        hueGradient.setColorAt(1.0, QColor::fromHsv(0, 255, 255));

        QPainterPath clip;
        clip.addRoundedRect(hueRect, density.dp(4.0), density.dp(4.0));
        painter.fillPath(clip, hueGradient);

        const qreal markerX = hueRect.left() + hueRect.width() * hue_ / 360.0;
        painter.setPen(QPen(colours.onSurface, density.dp(2.0)));
        painter.drawLine(QPointF(markerX, hueRect.top()), QPointF(markerX, hueRect.bottom()));
    }

    if (!alphaRect.isEmpty()) {
        QPainterPath clip;
        clip.addRoundedRect(alphaRect, density.dp(4.0), density.dp(4.0));
        painter.save();
        painter.setClipPath(clip);

        const qreal cell = density.dp(8.0);
        for (int row = 0; row * cell < alphaRect.height(); ++row) {
            for (int column = 0; column * cell < alphaRect.width(); ++column) {
                const qreal cellX = alphaRect.left() + column * cell;
                const qreal cellY = alphaRect.top() + row * cell;
                const QRectF cellRect(cellX, cellY, cell, cell);
                const bool light = ((row + column) % 2) == 0;
                painter.fillRect(cellRect.intersected(alphaRect),
                                 light ? QColor(255, 255, 255) : QColor(204, 204, 204));
            }
        }

        QColor base = QColor::fromHsv(hue_, saturation_, value_);
        QLinearGradient alphaGradient(alphaRect.topLeft(), alphaRect.topRight());
        QColor transparentBase = base;
        transparentBase.setAlpha(0);
        alphaGradient.setColorAt(0.0, transparentBase);
        alphaGradient.setColorAt(1.0, base);
        painter.fillRect(alphaRect, alphaGradient);
        painter.restore();

        const qreal markerX = alphaRect.left() + alphaRect.width() * alpha_ / 255.0;
        painter.setPen(QPen(colours.onSurface, density.dp(2.0)));
        painter.drawLine(QPointF(markerX, alphaRect.top()),
                         QPointF(markerX, alphaRect.bottom()));
    }

    if (!hexArea.isEmpty()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(enabled ? colours.surfaceContainerHighest
                                 : withAlpha(colours.onSurface, 0.04));
        painter.drawPath(fieldPath(hexArea, density.dp(kFieldRadiusDp)));

        const bool hexFocused = hexEditor_ != nullptr && hexEditor_->hasFocus();
        const QColor border = !hexValid_ ? colours.error
                             : hexFocused ? colours.primary
                                          : colours.outline;
        const qreal width = density.dp(hexFocused || !hexValid_ ? 2.0 : 1.0);
        painter.setPen(QPen(border, width));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(fieldPath(hexArea.adjusted(width / 2.0, width / 2.0,
                                                    -width / 2.0, -width / 2.0),
                                   density.dp(kFieldRadiusDp)));
    }

    painter.restore();
}

void ColourPicker::arrangeOverride(const QRectF& content)
{
    Q_UNUSED(content);

    if (hexEditor_ == nullptr)
        return;

    const Density& density = Theme::forWidget(this).density();
    const QRectF area = hexRect();
    const QRectF editorRect = area.adjusted(density.dp(12.0), density.dp(4.0),
                                            -density.dp(12.0), -density.dp(4.0));
    hexEditor_->setGeometry(static_cast<int>(std::lround(editorRect.left())),
                            static_cast<int>(std::lround(editorRect.top())),
                            static_cast<int>(std::lround(editorRect.width())),
                            static_cast<int>(std::lround(editorRect.height())));
}

void ColourPicker::resizeEvent(QResizeEvent* event)
{
    View::resizeEvent(event);
    arrangeOverride(contentRect());
}

void ColourPicker::mousePressEvent(QMouseEvent* event)
{
    if (!isEnabled() || event->button() != Qt::LeftButton) {
        View::mousePressEvent(event);
        return;
    }

    const QPointF position = event->position();
    Part part = Part::None;

    if (saturationValueRect().contains(position))
        part = Part::SaturationValue;
    else if (hueBarRect().contains(position))
        part = Part::Hue;
    else if (alphaBarRect().contains(position))
        part = Part::Alpha;

    dragged_ = part;
    if (part != Part::None) {
        setFocus(Qt::MouseFocusReason);
        applyPointer(part, position);
        event->accept();
    }

    View::mousePressEvent(event);
}

void ColourPicker::mouseMoveEvent(QMouseEvent* event)
{
    if (dragged_ != Part::None && isEnabled())
        applyPointer(dragged_, event->position());

    View::mouseMoveEvent(event);
}

void ColourPicker::mouseReleaseEvent(QMouseEvent* event)
{
    dragged_ = Part::None;
    View::mouseReleaseEvent(event);
}

void ColourPicker::keyPressEvent(QKeyEvent* event)
{
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);

    switch (event->key()) {
    case Qt::Key_Left:
        applyColour(QColor::fromHsv((hue_ + 359) % 360, saturation_, value_, alpha_));
        event->accept();
        return;
    case Qt::Key_Right:
        applyColour(QColor::fromHsv((hue_ + 1) % 360, saturation_, value_, alpha_));
        event->accept();
        return;
    case Qt::Key_Up:
        if (shift)
            applyColour(QColor::fromHsv(hue_, std::clamp(saturation_ + 8, 0, 255), value_,
                                        alpha_));
        else
            applyColour(QColor::fromHsv(hue_, saturation_,
                                        std::clamp(value_ + 8, 0, 255), alpha_));
        event->accept();
        return;
    case Qt::Key_Down:
        if (shift)
            applyColour(QColor::fromHsv(hue_, std::clamp(saturation_ - 8, 0, 255), value_,
                                        alpha_));
        else
            applyColour(QColor::fromHsv(hue_, saturation_,
                                        std::clamp(value_ - 8, 0, 255), alpha_));
        event->accept();
        return;
    default:
        break;
    }

    View::keyPressEvent(event);
}

void ColourPicker::changeEvent(QEvent* event)
{
    View::changeEvent(event);

    if (event->type() == QEvent::ThemeChange && hexEditor_ != nullptr)
        styleEditor(hexEditor_, Theme::forWidget(this), isEnabled());
}

bool ColourPicker::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == hexEditor_) {
        switch (event->type()) {
        case QEvent::KeyPress: {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                commitHexEditor();
                event->accept();
                return true;
            }
            break;
        }
        case QEvent::FocusIn:
        case QEvent::FocusOut:
            requestUpdate();
            break;
        default:
            break;
        }
    }

    return View::eventFilter(watched, event);
}

bool ColourPicker::focusable() const
{
    return isEnabled();
}

void ColourPicker::applyColour(const QColor& colour)
{
    if (!colour.isValid())
        return;

    const QColor before = composed();

    const int hue = colour.hsvHue();
    if (hue >= 0)
        hue_ = hue;
    saturation_ = colour.hsvSaturation();
    value_ = colour.value();
    alpha_ = colour.alpha();

    syncHexEditor();
    requestUpdate();

    if (composed() != before)
        emit colourChanged(composed());
}

void ColourPicker::applyPointer(Part part, const QPointF& position)
{
    int nextHue = hue_;
    int nextSaturation = saturation_;
    int nextValue = value_;
    int nextAlpha = alpha_;

    const QRectF panel = saturationValueRect();
    const QRectF hueRect = hueBarRect();
    const QRectF alphaRect = alphaBarRect();

    switch (part) {
    case Part::SaturationValue:
        if (panel.isEmpty())
            return;
        nextSaturation = std::clamp(
            static_cast<int>(std::lround((position.x() - panel.left()) / panel.width() * 255.0)),
            0, 255);
        nextValue = std::clamp(
            static_cast<int>(std::lround((panel.bottom() - position.y()) / panel.height() * 255.0)),
            0, 255);
        break;
    case Part::Hue:
        if (hueRect.isEmpty())
            return;
        nextHue = std::clamp(
            static_cast<int>(std::lround((position.x() - hueRect.left()) / hueRect.width()
                                         * 359.0)),
            0, 359);
        break;
    case Part::Alpha:
        if (alphaRect.isEmpty())
            return;
        nextAlpha = std::clamp(
            static_cast<int>(std::lround((position.x() - alphaRect.left()) / alphaRect.width()
                                         * 255.0)),
            0, 255);
        break;
    case Part::None:
        return;
    }

    if (nextHue == hue_ && nextSaturation == saturation_ && nextValue == value_
        && nextAlpha == alpha_) {
        return;
    }

    const QColor before = composed();
    hue_ = nextHue;
    saturation_ = nextSaturation;
    value_ = nextValue;
    alpha_ = nextAlpha;

    syncHexEditor();
    requestUpdate();

    if (composed() != before)
        emit colourChanged(composed());
}

void ColourPicker::syncHexEditor()
{
    if (hexEditor_ == nullptr || syncingHex_)
        return;

    // While the field has focus the user's text is left alone; the colour is
    // still applied from the buttons and the other panels.
    if (hexEditor_->hasFocus())
        return;

    const QColor current = composed();
    const QString text = current.alpha() == 255
        ? current.name(QColor::HexRgb)
        : current.name(QColor::HexArgb);

    syncingHex_ = true;
    hexEditor_->setText(text.toUpper());
    syncingHex_ = false;
    hexValid_ = true;
}

void ColourPicker::commitHexEditor()
{
    if (hexEditor_ == nullptr)
        return;

    QColor parsed;
    bool hasAlpha = false;
    if (!parseHexColour(hexEditor_->text(), &parsed, &hasAlpha)) {
        hexValid_ = false;
        requestUpdate();
        return;
    }

    if (!hasAlpha)
        parsed.setAlpha(alpha_);

    hexValid_ = true;
    applyColour(parsed);
}

QColor ColourPicker::composed() const
{
    return QColor::fromHsv(hue_, saturation_, value_, alpha_);
}

// ---------------------------------------------------------------------------
// PickerBase
// ---------------------------------------------------------------------------

PickerBase::PickerBase(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover, true);
    setBackgroundColour(QColor(Qt::transparent));
    setShape(Shape::small());

    adapter_ = new Adapter(this);

    settleTimer_.setInterval(16);
    connect(&settleTimer_, &QTimer::timeout, this, &PickerBase::tick);

    requestUpdate();
}

PickerBase::~PickerBase() = default;

void PickerBase::setSourceModel(QAbstractItemModel* model)
{
    if (adapter_->sourceModel() == model)
        return;

    setAdapterSource(nullptr);
    ownedModel_.reset();
    setAdapterSource(model);
    refreshFromModel();
}

QAbstractItemModel* PickerBase::sourceModel() const noexcept
{
    return adapter_->sourceModel();
}

Adapter* PickerBase::adapter() const noexcept
{
    return adapter_;
}

void PickerBase::setFormatter(Adapter::Formatter formatter)
{
    adapter_->setLabelFor(std::move(formatter));
    requestUpdate();
}

int PickerBase::count() const
{
    return adapter_->rowCount();
}

int PickerBase::currentIndex() const noexcept
{
    return current_;
}

QVariant PickerBase::currentValue() const
{
    if (current_ >= 0 && current_ < count())
        return valueForRow(current_);

    return {};
}

QString PickerBase::currentText() const
{
    if (current_ >= 0 && current_ < count())
        return textForRow(current_);

    return {};
}

void PickerBase::setCurrentIndex(int index)
{
    if (count() <= 0)
        return;

    offset_ = 0.0;
    applyIndex(index);
    requestUpdate();
}

void PickerBase::setCurrentValue(const QVariant& value)
{
    const int rows = count();
    for (int row = 0; row < rows; ++row) {
        if (valueForRow(row) == value) {
            setCurrentIndex(row);
            return;
        }
    }
}

void PickerBase::setCyclic(bool cyclic)
{
    if (cyclic_ == cyclic)
        return;

    cyclic_ = cyclic;
    offset_ = 0.0;
    refreshFromModel();
}

bool PickerBase::isCyclic() const noexcept
{
    return cyclic_;
}

void PickerBase::setItemHeightDp(qreal dp)
{
    const qreal next = dp > 0.0 ? dp : 1.0;
    if (next == itemHeightDp_)
        return;

    itemHeightDp_ = next;
    updateGeometry();
    requestUpdate();
}

qreal PickerBase::itemHeightDp() const noexcept
{
    return itemHeightDp_;
}

qreal PickerBase::itemHeight() const
{
    return Theme::forWidget(this).density().dp(itemHeightDp_);
}

void PickerBase::setVisibleItemCount(int count)
{
    const int next = std::max(1, count);
    if (next == visibleItems_)
        return;

    visibleItems_ = next;
    updateGeometry();
    requestUpdate();
}

int PickerBase::visibleItemCount() const noexcept
{
    return visibleItems_;
}

int PickerBase::rowAt(const QPointF& position) const
{
    const int rows = count();
    const qreal height = itemHeight();
    if (rows <= 0 || height <= 0.0)
        return -1;

    const qreal delta = position.y() - centerY() - offset_;
    const int slot = static_cast<int>(std::lround(delta / height));
    const int half = visibleItems_ / 2;
    if (slot < -half || slot > half)
        return -1;

    return normalized(current_ + slot);
}

bool PickerBase::isSettling() const noexcept
{
    return settleTimer_.isActive();
}

void PickerBase::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    const qreal preferredHeight = density.dp(itemHeightDp_) * visibleItems_
        + density.dp(kPickerPaddingDp);

    setMeasuredSize(QSizeF(width.resolve(density.dp(kPickerWidthDp)),
                           height.resolve(preferredHeight)));
}

void PickerBase::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const int rows = count();
    if (rows <= 0)
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const bool enabled = isEnabled();

    const qreal height = itemHeight();
    if (height <= 0.0)
        return;

    const qreal centre = centerY();
    const int half = visibleItems_ / 2;
    const qreal inset = density.dp(12.0);

    const QFont centreFont = themeFont(theme, theme.type().titleMedium);
    const QFont sideFont = themeFont(theme, theme.type().bodyMedium);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(content);

    const QRectF highlightRect(content.left() + inset, centre - height / 2.0 + density.dp(4.0),
                               std::max(0.0, content.width() - inset * 2.0),
                               height - density.dp(8.0));
    if (!highlightRect.isEmpty() && enabled) {
        QPainterPath pill;
        pill.addRoundedRect(highlightRect, height / 2.0, height / 2.0);
        painter.fillPath(pill, colours.secondaryContainer);
    }

    for (int slot = -half; slot <= half; ++slot) {
        const int row = normalized(current_ + slot);
        if (row < 0)
            continue;

        const qreal rowCentre = centre + slot * height + offset_;
        const QRectF rowRect(content.left() + inset, rowCentre - height / 2.0,
                             std::max(0.0, content.width() - inset * 2.0), height);
        if (!rowRect.intersects(content))
            continue;

        const bool centreRow = slot == 0;
        qreal opacity = 1.0 - 0.28 * qAbs(slot);
        opacity = std::clamp(opacity, 0.15, 1.0);
        if (!enabled)
            opacity *= 0.38;

        painter.setOpacity(opacity);
        painter.setFont(centreRow ? centreFont : sideFont);
        painter.setPen(centreRow ? colours.onSecondaryContainer : colours.onSurface);

        const QString label = textForRow(row);
        const QFont& font = centreRow ? centreFont : sideFont;
        painter.drawText(rowRect, static_cast<int>(Qt::AlignCenter),
                         elided(font, label, rowRect.width() - inset));
    }

    painter.restore();
}

void PickerBase::mousePressEvent(QMouseEvent* event)
{
    if (!isEnabled() || event->button() != Qt::LeftButton) {
        View::mousePressEvent(event);
        return;
    }

    pressPosition_ = event->position();
    lastPosition_ = pressPosition_;
    dragging_ = true;
    moved_ = false;
    velocity_ = 0.0;
    stopSettling();
    setFocus(Qt::MouseFocusReason);
    event->accept();

    View::mousePressEvent(event);
}

void PickerBase::mouseMoveEvent(QMouseEvent* event)
{
    if (dragging_ && isEnabled()) {
        const QPointF position = event->position();
        const QPointF delta = position - lastPosition_;
        lastPosition_ = position;

        if (!moved_ && (position - pressPosition_).manhattanLength() > 3.0)
            moved_ = true;

        if (moved_) {
            offset_ += delta.y();
            velocity_ = 0.6 * velocity_ + 0.4 * delta.y();
            consumeOffset();
            requestUpdate();
        }
    }

    View::mouseMoveEvent(event);
}

void PickerBase::mouseReleaseEvent(QMouseEvent* event)
{
    if (dragging_ && isEnabled()) {
        dragging_ = false;

        if (!moved_) {
            const int row = rowAt(event->position());
            offset_ = 0.0;
            if (row >= 0)
                applyIndex(row);
            requestUpdate();
        } else {
            startSettling();
        }
    }

    View::mouseReleaseEvent(event);
}

void PickerBase::wheelEvent(QWheelEvent* event)
{
    if (!isEnabled()) {
        event->ignore();
        return;
    }

    const int delta = event->angleDelta().y();
    if (delta == 0) {
        event->ignore();
        return;
    }

    const int steps = delta / 120;
    step(steps == 0 ? (delta > 0 ? -1 : 1) : -steps);
    event->accept();
}

void PickerBase::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Up:
        step(-1);
        event->accept();
        return;
    case Qt::Key_Down:
        step(1);
        event->accept();
        return;
    case Qt::Key_Home:
        if (count() > 0) {
            offset_ = 0.0;
            applyIndex(0);
        }
        event->accept();
        return;
    case Qt::Key_End:
        if (count() > 0) {
            offset_ = 0.0;
            applyIndex(count() - 1);
        }
        event->accept();
        return;
    case Qt::Key_PageUp:
        step(-visibleItems_);
        event->accept();
        return;
    case Qt::Key_PageDown:
        step(visibleItems_);
        event->accept();
        return;
    default:
        break;
    }

    View::keyPressEvent(event);
}

void PickerBase::hideEvent(QHideEvent* event)
{
    stopSettling();
    View::hideEvent(event);
}

bool PickerBase::focusable() const
{
    return isEnabled();
}

QString PickerBase::textForRow(int row) const
{
    return adapter_->text(row);
}

QVariant PickerBase::valueForRow(int row) const
{
    return adapter_->value(row);
}

void PickerBase::setOwnedValues(QVariantList values, QStringList labels)
{
    setAdapterSource(nullptr);

    ownedModel_ = std::make_unique<Model>();
    ownedModel_->setValues(std::move(values), std::move(labels));
    setAdapterSource(ownedModel_.get());

    refreshFromModel();
}

void PickerBase::applyIndex(int index)
{
    const int rows = count();
    if (rows <= 0)
        return;

    const int next = cyclic_ ? normalized(index) : std::clamp(index, 0, rows - 1);
    if (next == current_)
        return;

    current_ = next;
    requestUpdate();
    emit currentIndexChanged(current_);
    emit currentValueChanged(currentValue());
}

void PickerBase::step(int steps)
{
    if (count() <= 0 || steps == 0)
        return;

    offset_ = 0.0;
    applyIndex(current_ + steps);
}

void PickerBase::consumeOffset()
{
    const qreal height = itemHeight();
    const int rows = count();
    if (height <= 0.0 || rows <= 0)
        return;

    bool changed = false;

    while (offset_ >= height) {
        offset_ -= height;
        const int next = cyclic_ ? normalized(current_ - 1) : std::max(0, current_ - 1);
        if (next == current_) {
            if (!cyclic_)
                offset_ = 0.0;
            break;
        }
        current_ = next;
        changed = true;
    }

    while (offset_ <= -height) {
        offset_ += height;
        const int next = cyclic_ ? normalized(current_ + 1) : std::min(rows - 1, current_ + 1);
        if (next == current_) {
            if (!cyclic_)
                offset_ = 0.0;
            break;
        }
        current_ = next;
        changed = true;
    }

    if (changed) {
        requestUpdate();
        emit currentIndexChanged(current_);
        emit currentValueChanged(currentValue());
    }
}

void PickerBase::startSettling()
{
    const qreal height = itemHeight();
    if (height <= 0.0) {
        offset_ = 0.0;
        return;
    }

    if (std::abs(velocity_) < 0.5 && std::abs(offset_) < 0.5) {
        offset_ = 0.0;
        requestUpdate();
        return;
    }

    settleTimer_.start();
}

void PickerBase::stopSettling()
{
    settleTimer_.stop();
    velocity_ = 0.0;
}

void PickerBase::tick()
{
    const qreal height = itemHeight();
    if (height <= 0.0) {
        stopSettling();
        offset_ = 0.0;
        return;
    }

    if (std::abs(velocity_) > 0.6) {
        offset_ += velocity_;
        velocity_ *= 0.88;
        consumeOffset();
    } else {
        velocity_ = 0.0;
        offset_ *= 0.35;
        if (std::abs(offset_) < 0.5) {
            offset_ = 0.0;
            settleTimer_.stop();
        }
    }

    requestUpdate();
}

int PickerBase::normalized(int index) const
{
    const int rows = count();
    if (rows <= 0)
        return -1;

    const int wrapped = index % rows;
    return wrapped < 0 ? wrapped + rows : wrapped;
}

qreal PickerBase::centerY() const
{
    return contentRect().center().y();
}

void PickerBase::setAdapterSource(QAbstractItemModel* model)
{
    QAbstractItemModel* previous = adapter_->sourceModel();
    if (previous == model)
        return;

    if (previous != nullptr)
        QObject::disconnect(previous, nullptr, this, nullptr);

    adapter_->setSourceModel(model);

    if (model == nullptr)
        return;

    // A reset or a row removal can invalidate the held index: the handler
    // clamps it to the new count and reports the change. The try/catch keeps a
    // model that throws while it mutates from breaking the widget.
    connect(model, &QAbstractItemModel::rowsInserted, this, [this] {
        try {
            refreshFromModel();
        } catch (...) {
        }
    });
    connect(model, &QAbstractItemModel::rowsRemoved, this, [this] {
        try {
            refreshFromModel();
        } catch (...) {
        }
    });
    connect(model, &QAbstractItemModel::modelReset, this, [this] {
        try {
            refreshFromModel();
        } catch (...) {
        }
    });
}

void PickerBase::refreshFromModel()
{
    stopSettling();
    offset_ = 0.0;

    const int rows = count();
    const int previous = current_;

    // A model that shrank leaves the held row outside the new range. Clamp it
    // (even in cyclic mode: wrapping would jump to the first row instead of
    // the nearest survivor) and report the change; an empty model has no row.
    current_ = rows <= 0 ? -1 : std::clamp(current_, 0, rows - 1);

    requestUpdate();

    if (previous != current_) {
        emit currentIndexChanged(current_);
        emit currentValueChanged(currentValue());
    }
}

// ---------------------------------------------------------------------------
// NumberPicker
// ---------------------------------------------------------------------------

NumberPicker::NumberPicker(QWidget* parent)
    : PickerBase(parent)
{
    setCyclic(true);

    connect(this, &PickerBase::currentValueChanged, this, [this](const QVariant&) {
        emit valueChanged(value());
    });

    setRange(0.0, 10.0);
}

NumberPicker::~NumberPicker() = default;

void NumberPicker::setRange(double minimum, double maximum)
{
    double low = minimum;
    double high = maximum;
    if (low > high)
        std::swap(low, high);

    if (low == minimum_ && high == maximum_)
        return;

    minimum_ = low;
    maximum_ = high;
    rebuildRange();
}

void NumberPicker::setStep(double step)
{
    const double next = step > 0.0 ? step : 1.0;
    if (next == step_)
        return;

    step_ = next;
    rebuildRange();
}

double NumberPicker::minimum() const noexcept
{
    return minimum_;
}

double NumberPicker::maximum() const noexcept
{
    return maximum_;
}

double NumberPicker::step() const noexcept
{
    return step_;
}

void NumberPicker::setValues(const QVariantList& values)
{
    setOwnedValues(values);

    if (values.isEmpty())
        return;

    bool ok = false;
    const double first = values.first().toDouble(&ok);
    if (ok)
        minimum_ = first;

    const double last = values.last().toDouble(&ok);
    if (ok)
        maximum_ = last;

    if (values.size() > 1) {
        const double second = values.at(1).toDouble(&ok);
        if (ok) {
            const double delta = second - minimum_;
            if (delta > 0.0)
                step_ = delta;
        }
    }
}

double NumberPicker::value() const
{
    bool ok = false;
    const double current = currentValue().toDouble(&ok);
    return ok ? current : 0.0;
}

void NumberPicker::setValue(double value)
{
    const int rows = count();
    int best = -1;
    double bestDistance = 0.0;

    for (int row = 0; row < rows; ++row) {
        bool ok = false;
        const double candidate = valueForRow(row).toDouble(&ok);
        if (!ok)
            continue;

        const double distance = std::abs(candidate - value);
        if (best < 0 || distance < bestDistance) {
            best = row;
            bestDistance = distance;
        }
    }

    if (best >= 0)
        setCurrentIndex(best);
}

void NumberPicker::rebuildRange()
{
    QVariantList values;

    const double span = maximum_ - minimum_;
    if (span >= 0.0 && step_ > 0.0) {
        const double rawCount = std::floor(span / step_ + 1e-9) + 1.0;
        const double capped = std::min(rawCount, 10000.0);
        const int rows = static_cast<int>(capped);
        values.reserve(rows);

        for (int index = 0; index < rows; ++index) {
            const double candidate = minimum_ + static_cast<double>(index) * step_;
            if (candidate > maximum_ + step_ * 1e-6)
                break;
            values.append(candidate);
        }
    }

    setOwnedValues(std::move(values));
}

// ---------------------------------------------------------------------------
// RingPicker
// ---------------------------------------------------------------------------

RingPicker::RingPicker(QWidget* parent)
    : PickerBase(parent)
{
    setCyclic(true);
}

RingPicker::~RingPicker() = default;

void RingPicker::setValues(QVariantList values, QStringList labels)
{
    setOwnedValues(std::move(values), std::move(labels));
}

} // namespace md
