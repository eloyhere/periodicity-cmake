#include <md/components/picker_sheet.h>

#include <md/components/font.h>
#include <md/components/modal.h>
#include <md/components/row_feedback.h>
#include <md/theme/theme.h>
#include <md/tokens/motion.h>
#include <md/tokens/state_layer.h>
#include <md/view/view.h>

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QShowEvent>
#include <QVariant>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace md {
namespace {

constexpr qreal kWidthDp = 420.0;
constexpr qreal kPaddingDp = 20.0;
constexpr qreal kHeaderHeightDp = 28.0;
constexpr qreal kHeaderGapDp = 8.0;
constexpr qreal kRowHeightDp = 48.0;
constexpr qreal kRowGapDp = 2.0;
constexpr qreal kRowRadiusDp = 12.0;
constexpr qreal kListFooterGapDp = 12.0;
constexpr qreal kFooterHeightDp = 48.0;
constexpr qreal kButtonWidthDp = 96.0;
constexpr qreal kButtonGapDp = 8.0;
constexpr qreal kScrollBarWidthDp = 4.0;
constexpr int kMaxVisibleRows = 7;

QFont labelFont(const Theme& theme, const TextStyle& style, const QWidget* context)
{
    return Font::resolve(context, style, theme.density());
}

QColor withAlpha(const QColor& colour, qreal alpha)
{
    QColor result = colour;
    result.setAlphaF(static_cast<float>(std::clamp(alpha, qreal(0.0), qreal(1.0))));
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// PickerSheetCard — the painted sheet: title, scrollable checked list and the
// Cancel/Confirm affordances.
//
// It is a single focusable View. The Modal traps Tab inside it, so focus never
// leaves the list; Up/Down/Home/End move the highlight, Enter applies it and
// Escape cancels. Mouse presses on a row only move the highlight — the value is
// committed by Confirm (or Enter), which is what makes Cancel/Escape leave the
// previous value untouched.
// ---------------------------------------------------------------------------
class PickerSheetCard final : public View
{
public:
    explicit PickerSheetCard(QWidget* parent = nullptr)
        : View(parent)
        , feedback_(this)
    {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setInteractionFeedbackEnabled(false);
        setBackgroundColour(Theme::forWidget(this).colours().surfaceContainerHigh);
        setShape(Theme::forWidget(this).shapes().extraLarge);
        setElevation(Elevation::Level2);
        setAccessibleName(PickerSheet::tr("Choose an option"));

        // Exposed as a property so a test (or a host tool) can inspect the row
        // feedback of the exact surface it is driving without a private cast.
        setProperty("md.rowFeedback", QVariant::fromValue<QObject*>(&feedback_));
        connect(&feedback_, &RowFeedback::repaintRequested, this,
                qOverload<>(&QWidget::update));
    }

    void setTitle(const QString& title)
    {
        title_ = title;
        syncAccessibleText();
        updateGeometry();
        update();
    }

    void setOptions(const QList<PickerSheet::Option>& options, const QString& currentId)
    {
        options_ = options;
        currentId_ = currentId;

        int highlighted = 0;
        for (int index = 0; index < static_cast<int>(options_.size()); ++index) {
            if (options_.at(index).id == currentId_) {
                highlighted = index;
                break;
            }
        }
        highlighted_ = highlighted;
        firstVisible_ = 0;
        ensureVisible(highlighted_);
        syncAccessibleText();
        updateGeometry();
        update();
    }

    void setHighlightedId(const QString& id)
    {
        for (int index = 0; index < static_cast<int>(options_.size()); ++index) {
            if (options_.at(index).id == id) {
                highlighted_ = index;
                ensureVisible(index);
                syncAccessibleText();
                update();
                return;
            }
        }
    }

    [[nodiscard]] QString highlightedId() const
    {
        if (highlighted_ < 0 || highlighted_ >= static_cast<int>(options_.size()))
            return currentId_;
        return options_.at(highlighted_).id;
    }

    [[nodiscard]] int highlightedRow() const noexcept { return highlighted_; }
    [[nodiscard]] int firstVisibleRow() const noexcept { return firstVisible_; }
    [[nodiscard]] RowFeedback* feedback() noexcept { return &feedback_; }

    void setManualAnimationClock(bool enabled) { feedback_.setManualAnimationClock(enabled); }

    void advanceAnimation(std::chrono::milliseconds delta)
    {
        feedback_.advanceAnimation(delta);
    }

    std::function<void(const QString&)> onConfirm;
    std::function<void()> onCancel;

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override
    {
        const Density& density = Theme::forWidget(this).density();
        const int visible = visibleRows();
        const qreal heightDp = kPaddingDp + kHeaderHeightDp + kHeaderGapDp
                               + visible * kRowHeightDp + std::max(0, visible - 1) * kRowGapDp
                               + kListFooterGapDp + kFooterHeightDp + kPaddingDp;
        setMeasuredSize(QSizeF(width.resolve(density.dp(kWidthDp)),
                               height.resolve(density.dp(heightDp))));
    }

    void paintContent(QPainter& painter, const QRectF& content) override
    {
        if (content.isEmpty())
            return;

        const Theme& theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const ColorScheme& colours = theme.colours();

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setClipPath(shape().path(content));

        if (!title_.isEmpty()) {
            painter.setFont(labelFont(theme, theme.type().titleLarge, this));
            painter.setPen(colours.onSurface);
            painter.drawText(titleRect(), static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                             title_);
        }

        const QRectF list = listRect();
        painter.save();
        painter.setClipRect(list);

        if (options_.isEmpty()) {
            painter.setFont(labelFont(theme, theme.type().bodyLarge, this));
            painter.setPen(colours.onSurfaceVariant);
            painter.drawText(list, static_cast<int>(Qt::AlignCenter),
                             PickerSheet::tr("No options"));
        }

        for (int index = 0; index < static_cast<int>(options_.size()); ++index) {
            const QRectF row = rowRect(index);
            if (!row.intersects(list))
                continue;

            const bool selected = options_.at(index).id == currentId_;
            const qreal radius = density.dp(kRowRadiusDp);

            if (selected) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(colours.secondaryContainer);
                painter.drawRoundedRect(row, radius, radius);
            }

            feedback_.paintRow(painter, index, row, index == highlighted_,
                               radius, colours.onSurface);

            const QFont itemFont = labelFont(theme, theme.type().bodyLarge, this);
            const QFontMetricsF metrics(itemFont);
            const qreal trailing = selected ? density.dp(36.0) : density.dp(12.0);
            const QRectF text = row.adjusted(density.dp(16.0), 0.0, -trailing, 0.0);

            painter.setFont(itemFont);
            painter.setPen(selected ? colours.onSecondaryContainer : colours.onSurface);
            const QString label = options_.at(index).label.isEmpty()
                                      ? options_.at(index).id
                                      : options_.at(index).label;
            painter.drawText(text, static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                             metrics.elidedText(label, Qt::ElideRight, text.width()));

            if (selected) {
                const QPointF centre(row.right() - density.dp(20.0), row.center().y());
                QPainterPath check;
                check.moveTo(centre.x() - density.dp(5.0), centre.y());
                check.lineTo(centre.x() - density.dp(1.5), centre.y() + density.dp(3.5));
                check.lineTo(centre.x() + density.dp(5.0), centre.y() - density.dp(4.0));
                painter.setPen(QPen(colours.primary, density.dp(2.0), Qt::SolidLine,
                                    Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(check);
            }
        }

        if (options_.size() > visibleRows()) {
            const QRectF track = scrollBarRect();
            if (!track.isEmpty()) {
                const qreal fraction = qreal(highlighted_)
                                       / qreal(std::max(1, static_cast<int>(options_.size()) - 1));
                const qreal thumbHeight = std::max(track.height() * 0.25, track.height() * 0.4);
                const qreal travel = track.height() - thumbHeight;
                const QRectF thumb(track.left(), track.top() + travel * fraction,
                                   track.width(), thumbHeight);

                painter.setPen(Qt::NoPen);
                painter.setBrush(withAlpha(colours.onSurface, 0.10));
                painter.drawRoundedRect(track, track.width() * 0.5, track.width() * 0.5);
                painter.setBrush(withAlpha(colours.onSurface, 0.38));
                painter.drawRoundedRect(thumb, thumb.width() * 0.5, thumb.width() * 0.5);
            }
        }

        painter.restore();

        paintFooterButton(painter, cancelRect(), PickerSheet::tr("Cancel"),
                          colours.onSurfaceVariant, false, cancelHover_, cancelPressed_);
        paintFooterButton(painter, confirmRect(), PickerSheet::tr("Confirm"),
                          colours.primary, true, confirmHover_, confirmPressed_);

        painter.restore();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        View::mousePressEvent(event);

        if (event->button() != Qt::LeftButton)
            return;

        const QPointF position = event->position();
        if (cancelRect().contains(position)) {
            cancelPressed_ = true;
            cancelHover_ = true;
            update();
            event->accept();
            return;
        }
        if (confirmRect().contains(position) && !options_.isEmpty()) {
            confirmPressed_ = true;
            confirmHover_ = true;
            update();
            event->accept();
            return;
        }

        const int row = indexAt(position);
        if (row >= 0) {
            highlighted_ = row;
            ensureVisible(row);
            feedback_.pointerPressed(row, position, rowRect(row));
            syncAccessibleText();
            update();
            event->accept();
            return;
        }

        feedback_.pointerLeft();
        update();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        const QPointF position = event->position();
        const int row = indexAt(position);
        feedback_.pointerMoved(row);

        const bool cancel = cancelRect().contains(position);
        const bool confirm = confirmRect().contains(position) && !options_.isEmpty();
        if (cancel != cancelHover_ || confirm != confirmHover_) {
            cancelHover_ = cancel;
            confirmHover_ = confirm;
            update();
        }

        View::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        const QPointF position = event->position();
        const bool cancel = cancelPressed_ && cancelRect().contains(position);
        const bool confirm = confirmPressed_ && confirmRect().contains(position)
                             && !options_.isEmpty();
        const int pressedRow = feedback_.pressedRow();
        const bool rowPress = pressedRow >= 0 && indexAt(position) == pressedRow;

        cancelPressed_ = false;
        confirmPressed_ = false;
        feedback_.pointerReleased();

        // No member may be touched after a callback: the sheet can delete this
        // card (or itself) from the slot.
        if (cancel) {
            update();
            const std::function<void()> callback = onCancel;
            if (callback)
                callback();
            return;
        }
        if (confirm) {
            update();
            const QString id = highlightedId();
            const std::function<void(const QString&)> callback = onConfirm;
            if (callback)
                callback(id);
            return;
        }
        if (rowPress)
            highlighted_ = pressedRow;

        update();
        View::mouseReleaseEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        feedback_.pointerLeft();
        cancelHover_ = false;
        confirmHover_ = false;
        update();
        View::leaveEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (!options_.isEmpty()) {
            switch (event->key()) {
            case Qt::Key_Up:
                moveHighlight(-1);
                event->accept();
                return;
            case Qt::Key_Down:
                moveHighlight(1);
                event->accept();
                return;
            case Qt::Key_Home:
                moveHighlight(-static_cast<int>(options_.size()));
                event->accept();
                return;
            case Qt::Key_End:
                moveHighlight(static_cast<int>(options_.size()));
                event->accept();
                return;
            case Qt::Key_PageUp:
                moveHighlight(-visibleRows());
                event->accept();
                return;
            case Qt::Key_PageDown:
                moveHighlight(visibleRows());
                event->accept();
                return;
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Space: {
                const int row = highlighted_;
                if (row >= 0 && row < static_cast<int>(options_.size())) {
                    const QRectF rect = rowRect(row);
                    feedback_.triggerRipple(row, rect.center(), rect);
                    const QString id = options_.at(row).id;
                    const std::function<void(const QString&)> callback = onConfirm;
                    if (callback)
                        callback(id);
                }
                event->accept();
                return;
            }
            default:
                break;
            }
        }

        if (event->key() == Qt::Key_Escape) {
            const std::function<void()> callback = onCancel;
            if (callback)
                callback();
            event->accept();
            return;
        }

        if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
            // The modal's focus trap owns Tab. Accepting it here keeps the
            // card's own handling deterministic if the trap ever lets it pass.
            event->accept();
            return;
        }

        View::keyPressEvent(event);
    }

    void wheelEvent(QWheelEvent* event) override
    {
        const int delta = event->angleDelta().y();
        if (delta == 0) {
            View::wheelEvent(event);
            return;
        }

        const int maxFirst = std::max(0, static_cast<int>(options_.size()) - visibleRows());
        firstVisible_ = std::clamp(firstVisible_ + (delta < 0 ? 3 : -3), 0, maxFirst);
        update();
        event->accept();
    }

    void focusInEvent(QFocusEvent* event) override
    {
        View::focusInEvent(event);
        update();
    }

    void focusOutEvent(QFocusEvent* event) override
    {
        View::focusOutEvent(event);
        update();
    }

    void changeEvent(QEvent* event) override
    {
        View::changeEvent(event);
        if (event != nullptr && event->type() == QEvent::LanguageChange) {
            setAccessibleName(PickerSheet::tr("Choose an option"));
            syncAccessibleText();
            update();
        }
    }

    void onThemeChanged() override
    {
        const Theme& theme = Theme::forWidget(this);
        setBackgroundColour(theme.colours().surfaceContainerHigh);
        setShape(theme.shapes().extraLarge);
        setElevation(Elevation::Level2);
        update();
    }

    void onHidden() override { feedback_.clear(); }

    // The keyboard indicator is the highlighted row, not a ring around the
    // whole sheet; drawing the base focus ring would frame the entire card.
    [[nodiscard]] bool focusable() const override { return false; }

private:
    [[nodiscard]] int visibleRows() const
    {
        return std::clamp(static_cast<int>(options_.size()), 1, kMaxVisibleRows);
    }

    [[nodiscard]] QRectF titleRect() const
    {
        const QRectF content = contentRect();
        const Density& density = Theme::forWidget(this).density();
        return QRectF(content.left() + density.dp(kPaddingDp),
                      content.top() + density.dp(kPaddingDp),
                      std::max<qreal>(0.0, content.width() - density.dp(kPaddingDp) * 2.0),
                      density.dp(kHeaderHeightDp));
    }

    [[nodiscard]] QRectF listRect() const
    {
        const QRectF content = contentRect();
        const Density& density = Theme::forWidget(this).density();
        const qreal padding = density.dp(kPaddingDp);
        const qreal top = content.top() + padding + density.dp(kHeaderHeightDp)
                          + density.dp(kHeaderGapDp);
        const int visible = visibleRows();
        const qreal height = visible * density.dp(kRowHeightDp)
                             + std::max(0, visible - 1) * density.dp(kRowGapDp);
        return QRectF(content.left() + padding, top,
                      std::max<qreal>(0.0, content.width() - padding * 2.0), height);
    }

    [[nodiscard]] QRectF rowRect(int index) const
    {
        if (index < 0 || index >= static_cast<int>(options_.size()))
            return {};

        const QRectF list = listRect();
        const Density& density = Theme::forWidget(this).density();
        const qreal step = density.dp(kRowHeightDp) + density.dp(kRowGapDp);
        const qreal top = list.top() + qreal(index - firstVisible_) * step;
        const qreal width = std::max<qreal>(0.0, list.width()
                                                      - (options_.size() > visibleRows()
                                                             ? density.dp(kScrollBarWidthDp) + 6.0
                                                             : 0.0));
        return QRectF(list.left(), top, width, density.dp(kRowHeightDp));
    }

    [[nodiscard]] int indexAt(const QPointF& position) const
    {
        const QRectF list = listRect();
        if (!list.contains(position))
            return -1;

        const Density& density = Theme::forWidget(this).density();
        const qreal step = density.dp(kRowHeightDp) + density.dp(kRowGapDp);
        const int relative = static_cast<int>(std::floor((position.y() - list.top()) / step));
        if (relative < 0)
            return -1;

        const int index = firstVisible_ + relative;
        if (index < 0 || index >= static_cast<int>(options_.size()))
            return -1;
        return rowRect(index).contains(position) ? index : -1;
    }

    [[nodiscard]] QRectF scrollBarRect() const
    {
        const QRectF list = listRect();
        const Density& density = Theme::forWidget(this).density();
        return QRectF(list.right() - density.dp(kScrollBarWidthDp), list.top(),
                      density.dp(kScrollBarWidthDp), list.height());
    }

    [[nodiscard]] QRectF cancelRect() const { return footerButtonRect(0); }
    [[nodiscard]] QRectF confirmRect() const { return footerButtonRect(1); }

    [[nodiscard]] QRectF footerButtonRect(int index) const
    {
        const QRectF list = listRect();
        const Density& density = Theme::forWidget(this).density();
        const qreal width = density.dp(kButtonWidthDp);
        const qreal gap = density.dp(kButtonGapDp);
        const qreal right = contentRect().right() - density.dp(kPaddingDp);
        const qreal left = right - width;
        return QRectF(index == 1 ? left : left - gap - width,
                      list.bottom() + density.dp(kListFooterGapDp), width,
                      density.dp(kFooterHeightDp));
    }

    void paintFooterButton(QPainter& painter, const QRectF& rect, const QString& text,
                           const QColor& colour, bool primary, bool hovered,
                           bool pressed) const
    {
        const Theme& theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const ViewState state = pressed ? ViewState::Pressed
                                        : (hovered ? ViewState::Hovered : ViewState::Hovered);
        const bool showLayer = hovered || pressed;

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (showLayer) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(applyStateLayer(colour, state));
            painter.drawRoundedRect(rect, density.dp(12.0), density.dp(12.0));
        }

        QFont font = labelFont(theme, theme.type().labelLarge, this);
        if (primary)
            font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.setPen(colour);
        painter.drawText(rect, static_cast<int>(Qt::AlignCenter), text);
        painter.restore();
    }

    void moveHighlight(int delta)
    {
        if (options_.isEmpty())
            return;

        highlighted_ = std::clamp(highlighted_ + delta, 0,
                                  static_cast<int>(options_.size()) - 1);
        ensureVisible(highlighted_);
        syncAccessibleText();
        update();
    }

    void ensureVisible(int index)
    {
        if (index < 0)
            return;

        const int visible = visibleRows();
        if (index < firstVisible_)
            firstVisible_ = index;
        else if (index >= firstVisible_ + visible)
            firstVisible_ = index - visible + 1;

        const int maxFirst = std::max(0, static_cast<int>(options_.size()) - visible);
        firstVisible_ = std::clamp(firstVisible_, 0, maxFirst);
    }

    void syncAccessibleText()
    {
        const QString name = title_.isEmpty() ? PickerSheet::tr("Choose an option") : title_;
        setAccessibleName(name);
        if (highlighted_ >= 0 && highlighted_ < static_cast<int>(options_.size())) {
            const PickerSheet::Option& option = options_.at(highlighted_);
            setAccessibleDescription(PickerSheet::tr("%1: %2")
                                         .arg(name, option.label.isEmpty() ? option.id
                                                                           : option.label));
        } else {
            setAccessibleDescription(name);
        }
    }

    QString title_;
    QList<PickerSheet::Option> options_;
    QString currentId_;
    int highlighted_ = 0;
    int firstVisible_ = 0;

    bool cancelHover_ = false;
    bool confirmHover_ = false;
    bool cancelPressed_ = false;
    bool confirmPressed_ = false;

    RowFeedback feedback_;
};

// ---------------------------------------------------------------------------
// PickerSheet
// ---------------------------------------------------------------------------

PickerSheet::PickerSheet(QWidget* invoker, QObject* parent)
    : QObject(parent != nullptr ? parent : (invoker != nullptr ? invoker->window() : nullptr))
    , invoker_(invoker)
{
}

PickerSheet::~PickerSheet()
{
    if (modal_ != nullptr) {
        modal_->disconnect(this);
        delete modal_.data();
    }
}

void PickerSheet::setTitle(const QString& title)
{
    title_ = title;
    if (card_ != nullptr)
        card_->setTitle(title);
}

QString PickerSheet::title() const
{
    return title_;
}

void PickerSheet::setOptions(const QList<Option>& options, const QString& currentId)
{
    options_ = options;
    currentId_ = currentId;

    if (highlightedId_.isEmpty() || indexOf(highlightedId_) < 0) {
        if (indexOf(currentId_) >= 0)
            highlightedId_ = currentId_;
        else if (!options_.isEmpty())
            highlightedId_ = options_.first().id;
        else
            highlightedId_.clear();
    }

    if (card_ != nullptr) {
        card_->setOptions(options_, currentId_);
        card_->setHighlightedId(highlightedId_);
    }
}

QList<PickerSheet::Option> PickerSheet::options() const
{
    return options_;
}

QString PickerSheet::currentId() const
{
    return currentId_;
}

QString PickerSheet::highlightedId() const
{
    // Keyboard and pointer navigation live in the card; read it first so the
    // public value always reflects what Enter or Confirm would apply.
    return card_ != nullptr ? card_->highlightedId() : highlightedId_;
}

int PickerSheet::highlightedRow() const
{
    return card_ != nullptr ? card_->highlightedRow() : indexOf(highlightedId_);
}

void PickerSheet::setHighlightedId(const QString& id)
{
    if (indexOf(id) < 0)
        return;
    highlightedId_ = id;
    if (card_ != nullptr)
        card_->setHighlightedId(id);
}

void PickerSheet::setAnchor(QWidget* anchor)
{
    anchor_ = anchor;
    if (modal_ != nullptr) {
        modal_->setAnchor(anchor != nullptr ? anchor : invoker_.data());
        modal_->setFocusReturn(anchor != nullptr ? anchor : invoker_.data());
    }
}

QWidget* PickerSheet::invoker() const
{
    return invoker_.data();
}

QWidget* PickerSheet::card() const
{
    return card_.data();
}

Modal* PickerSheet::modal() const
{
    return modal_.data();
}

RowFeedback* PickerSheet::rowFeedback() const
{
    return card_ != nullptr ? card_->feedback() : nullptr;
}

void PickerSheet::open()
{
    if (isOpen())
        return;

    // A closed one-shot sheet can be reused before its deleteLater() turn; drop
    // the old surface rather than stacking a second modal in the window.
    if (modal_ != nullptr) {
        modal_->disconnect(this);
        delete modal_.data();
        modal_.clear();
        card_.clear();
    }

    QWidget* host = invoker_ != nullptr ? invoker_->window() : nullptr;
    if (host == nullptr)
        return;

    outcome_ = Outcome::None;
    cancelledEmitted_ = false;
    confirmedEmitted_ = false;

    auto* modal = new Modal(host);
    modal_ = modal;
    modal->setLevel(Modal::Level::Dialog);
    modal->setDismissOnClick(true);
    modal->setDismissOnEscape(true);

    auto* card = new PickerSheetCard(nullptr);
    card_ = card;
    card->setTitle(title_);
    card->setOptions(options_, currentId_);
    card->setHighlightedId(highlightedId_);

    QPointer<PickerSheet> self(this);
    card->onCancel = [self] {
        if (self != nullptr)
            self->cancel();
    };
    card->onConfirm = [self](const QString& id) {
        if (self != nullptr)
            self->confirmWith(id);
    };

    modal->setCard(card);
    modal->setAnchor(anchor_ != nullptr ? anchor_.data() : invoker_.data());
    modal->setFocusReturn(anchor_ != nullptr ? anchor_.data() : invoker_.data());
    modal->setManualAnimationClock(manualClock_);

    connect(modal, &Modal::closed, this, [self] {
        if (self != nullptr)
            self->handleModalClosed();
    });
    connect(modal, &Modal::closeFinished, this, [self] {
        if (self == nullptr)
            return;
        if (self->modal_ != nullptr)
            self->modal_->deleteLater();
        self->modal_.clear();
        self->card_.clear();
        emit self->finished();
    });

    modal->open();
}

void PickerSheet::close()
{
    cancel();
}

void PickerSheet::cancel()
{
    if (!isOpen() || confirmedEmitted_)
        return;

    if (!cancelledEmitted_) {
        outcome_ = Outcome::Cancelled;
        cancelledEmitted_ = true;

        QPointer<PickerSheet> self(this);
        emit cancelled();
        if (self.isNull() || modal_.isNull())
            return;
    }

    if (modal_ != nullptr && modal_->isOpen())
        modal_->close();
}

void PickerSheet::confirm()
{
    confirmWith(highlightedId());
}

void PickerSheet::confirmWith(const QString& id)
{
    if (!isOpen() || id.isEmpty() || confirmedEmitted_)
        return;

    outcome_ = Outcome::Confirmed;
    confirmedEmitted_ = true;

    const QString selected = id;
    QPointer<PickerSheet> self(this);
    emit confirmed(selected);
    if (self.isNull() || modal_.isNull())
        return;

    if (modal_->isOpen())
        modal_->close();
}

bool PickerSheet::isOpen() const
{
    return modal_ != nullptr && modal_->isOpen();
}

bool PickerSheet::isAnimating() const
{
    return modal_ != nullptr && modal_->isAnimating();
}

void PickerSheet::setManualAnimationClock(bool enabled)
{
    manualClock_ = enabled;
    if (modal_ != nullptr)
        modal_->setManualAnimationClock(enabled);
    if (card_ != nullptr)
        card_->setManualAnimationClock(enabled);
}

bool PickerSheet::isManualAnimationClock() const
{
    return manualClock_;
}

void PickerSheet::advanceAnimation(std::chrono::milliseconds delta)
{
    if (modal_ != nullptr)
        modal_->advanceAnimation(delta);
    if (card_ != nullptr)
        card_->advanceAnimation(delta);
}

int PickerSheet::firstVisibleRow() const
{
    return card_ != nullptr ? card_->firstVisibleRow() : 0;
}

void PickerSheet::handleModalClosed()
{
    if (outcome_ != Outcome::None || cancelledEmitted_)
        return;

    outcome_ = Outcome::Cancelled;
    cancelledEmitted_ = true;

    QPointer<PickerSheet> self(this);
    emit cancelled();
    Q_UNUSED(self);
}

int PickerSheet::indexOf(const QString& id) const
{
    for (int index = 0; index < static_cast<int>(options_.size()); ++index) {
        if (options_.at(index).id == id)
            return index;
    }
    return -1;
}

} // namespace md
