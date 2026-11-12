// Periodicity — the Mention component (framework.md §5.9).
//
// The editor is a stock QTextEdit: it already implements selection, undo, IME
// and multi-line layout, and its handling of QChar::ObjectReplacementCharacter
// is exactly the atomic-node behaviour this component promises. What is added
// here is (a) the char format that carries the mention name/value, (b) the
// QTextObjectInterface that draws the capsule, (c) the '@' trigger and the
// candidate list, and (d) a plain-text projection that expands nodes back to
// "@name" so extraction loses no information.
//
// The mention node is one document position. That is what makes "delete the
// whole node" and "cannot insert inside the node" true without any cursor
// patching: there is no inside.

#include <md/components/mention.h>


#include <md/theme/theme.h>

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QFrame>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFragment>
#include <QTextObjectInterface>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace md {
namespace {

// Object type and char-format properties of a mention node. Both live above
// Qt's user ranges so they cannot collide with a built-in format.
constexpr int kMentionObjectType = QTextFormat::UserObject + 1;
constexpr int kMentionNameProperty = QTextFormat::UserProperty + 1;
constexpr int kMentionValueProperty = QTextFormat::UserProperty + 2;

constexpr qreal kHorizontalPaddingDp = 8.0;
constexpr qreal kVerticalInsetDp = 2.0;

[[nodiscard]] QFont bodyFont(const Theme& theme)
{
    const TextStyle& style = theme.type().bodyLarge;
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
    font.setWeight(QFont::Weight(style.weight));
    return font;
}

[[nodiscard]] QString objectReplacementString()
{
    return QStringLiteral("\uFFFC");
}

} // namespace

// Paints one mention node. QTextObjectInterface must also be a QObject so that
// QAbstractTextDocumentLayout can qobject_cast the registered handler; the
// Q_OBJECT/Q_INTERFACES pair and the "mention.moc" include at the end of this
// file are what make that cast work.
class MentionObjectHandler final : public QObject, public QTextObjectInterface
{
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)

public:
    explicit MentionObjectHandler(Mention* owner)
        : QObject(owner)
        , owner_(owner)
    {
    }

    QSizeF intrinsicSize(QTextDocument*, int, const QTextFormat& format) override
    {
        const Theme& theme = Theme::forWidget(owner_);
        const QFontMetricsF metrics(bodyFont(theme));
        const QString name = format.property(kMentionNameProperty).toString();
        const qreal padding = theme.density().dp(kHorizontalPaddingDp);

        return QSizeF(metrics.horizontalAdvance(QStringLiteral("@") + name) + 2.0 * padding,
                      metrics.height() + theme.density().dp(4.0));
    }

    void drawObject(QPainter* painter, const QRectF& rect, QTextDocument*, int,
                    const QTextFormat& format) override
    {
        if (painter == nullptr || rect.isEmpty())
            return;

        const Theme& theme = Theme::forWidget(owner_);
        const ColorScheme& colours = theme.colours();
        const qreal unit = theme.density().dp(1.0);
        const QString name = format.property(kMentionNameProperty).toString();
        const QRectF pill = rect.adjusted(0.0, unit * kVerticalInsetDp, 0.0,
                                          -unit * kVerticalInsetDp);
        if (pill.isEmpty())
            return;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setRenderHint(QPainter::TextAntialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(owner_->isEnabled() ? colours.secondaryContainer
                                              : colours.surfaceContainerHighest);
        painter->drawRoundedRect(pill, pill.height() / 2.0, pill.height() / 2.0);
        painter->setFont(bodyFont(theme));
        painter->setPen(owner_->isEnabled() ? colours.onSecondaryContainer
                                            : colours.onSurfaceVariant);
        painter->drawText(pill, Qt::AlignCenter, QStringLiteral("@") + name);
        painter->restore();
    }

private:
    Mention* owner_ = nullptr;
};

Mention::Mention(QWidget* parent)
    : View(parent)
    , editor_(new QTextEdit(this))
    , popup_(new QListWidget(this))
{
    setFocusPolicy(Qt::StrongFocus);
    setShape(Shape::small());
    setAccessibleName(tr("Text with mentions"));

    const Density& density = Theme::forWidget(this).density();
    const qreal marginX = density.dp(8.0);
    const qreal marginY = density.dp(6.0);

    editor_->setAcceptRichText(false);
    editor_->setFrameShape(QFrame::NoFrame);
    editor_->setFocusPolicy(Qt::StrongFocus);
    editor_->setTabChangesFocus(true);
    editor_->setPlaceholderText(tr("Type @ to mention someone"));
    editor_->setStyleSheet(
        QStringLiteral("QTextEdit { background: transparent; border: 0; }"));
    editor_->document()->setDocumentMargin(4.0);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(qRound(marginX), qRound(marginY), qRound(marginX),
                               qRound(marginY));
    layout->addWidget(editor_);

    setFocusProxy(editor_);

    popup_->setFocusPolicy(Qt::NoFocus);
    popup_->setFrameShape(QFrame::NoFrame);
    popup_->setUniformItemSizes(true);
    popup_->hide();

    connect(popup_, &QListWidget::itemClicked, this, [this](QListWidgetItem*) {
        if (popupActive_)
            chooseCurrentCandidate();
    });

    objectHandler_ = new MentionObjectHandler(this);
    editor_->document()->documentLayout()->registerHandler(kMentionObjectType, objectHandler_);

    editor_->installEventFilter(this);
    editor_->viewport()->installEventFilter(this);

    connect(editor_, &QTextEdit::textChanged, this, [this] {
        requestUpdate();

        // The editor inserts the character itself; this only notices a fresh
        // '@' before the cursor. Nothing is inserted inside a key filter, so
        // the path is safe for input methods and for host textChanged slots.
        //
        // Every boundary '@' starts a trigger, including one typed while a list
        // is already open: the existing QListWidget is retargeted at the new
        // '@' instead of a second popup being created. That is what makes a
        // chain such as "@alice @bob" work, and what keeps the two lists from
        // stacking. A '@' that does not sit on a boundary stays literal text.
        if (!suppressTrigger_) {
            const QTextCursor cursor = editor_->textCursor();
            const int position = cursor.position();
            const bool caretAfterAt = !cursor.hasSelection() && position > 0
                && editor_->document()->characterAt(position - 1) == QLatin1Char('@');
            const bool atBoundary = caretAfterAt
                && (position == 1
                    || editor_->document()->characterAt(position - 2).isSpace());

            if (atBoundary) {
                triggerPos_ = position - 1;
                popupActive_ = true;
            } else if (popupActive_ && !triggerIsValid()) {
                // Deleting the trigger '@' ends the mention right away: the
                // editor can report textChanged before the cursor notification
                // reaches cursorPositionChanged().
                cancelTrigger();
            }
        }

        // Filtering only; a moving caret is cursorPositionChanged()'s job,
        // because textChanged can run before the cursor has moved to the new
        // character.
        if (popupActive_)
            refreshPopup();

        emit textChanged();
    });

    connect(editor_, &QTextEdit::cursorPositionChanged, this, [this] {
        if (!popupActive_)
            return;

        // Validate the trigger against the document rather than the cursor
        // alone: right after an input method commit the cursor can still
        // report the old position for one notification.
        if (!triggerIsValid()) {
            cancelTrigger();
            return;
        }
        refreshPopup();
    });

    applyTheme();
}

Mention::~Mention() = default;

void Mention::applyTheme()
{
    const Theme& theme = Theme::forWidget(this);
    const QFont font = bodyFont(theme);
    editor_->setFont(font);
    editor_->document()->setDefaultFont(font);

    QString style = QStringLiteral(
        "QListWidget { background: %1; color: %2; border: 1px solid %3;"
        " border-radius: 6px; padding: 4px; }"
        "QListWidget::item { padding: 6px 8px; }"
        "QListWidget::item:selected { background: %4; color: %5; border-radius: 4px; }");
    style = style.arg(theme.colours().surfaceContainerHigh.name());
    style = style.arg(theme.colours().onSurface.name());
    style = style.arg(theme.colours().outlineVariant.name());
    style = style.arg(theme.colours().primaryContainer.name());
    style = style.arg(theme.colours().onPrimaryContainer.name());
    popup_->setStyleSheet(style);
}

void Mention::onThemeChanged()
{
    applyTheme();
    requestUpdate();
}

QString Mention::plainText() const
{
    QString text;
    const QTextDocument* document = editor_->document();
    bool firstBlock = true;

    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        if (!firstBlock)
            text += QLatin1Char('\n');
        firstBlock = false;

        for (auto iterator = block.begin(); !iterator.atEnd(); ++iterator) {
            const QTextFragment fragment = iterator.fragment();
            if (!fragment.isValid())
                continue;

            const QTextCharFormat format = fragment.charFormat();
            const QString fragmentText = fragment.text();

            if (format.objectType() != kMentionObjectType) {
                text += fragmentText;
                continue;
            }

            const QString name = format.property(kMentionNameProperty).toString();
            const int length = static_cast<int>(fragmentText.size());
            for (int index = 0; index < length; ++index) {
                if (fragmentText.at(index).unicode() == 0xFFFC) {
                    text += QLatin1Char('@');
                    text += name;
                } else {
                    text += fragmentText.at(index);
                }
            }
        }
    }

    return text;
}

QList<MentionSpan> Mention::mentions() const
{
    QList<MentionSpan> spans;
    const QTextDocument* document = editor_->document();
    int offset = 0;
    bool firstBlock = true;

    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        if (!firstBlock)
            ++offset;
        firstBlock = false;

        for (auto iterator = block.begin(); !iterator.atEnd(); ++iterator) {
            const QTextFragment fragment = iterator.fragment();
            if (!fragment.isValid())
                continue;

            const QTextCharFormat format = fragment.charFormat();
            const QString fragmentText = fragment.text();
            const int length = static_cast<int>(fragmentText.size());

            if (format.objectType() != kMentionObjectType) {
                offset += length;
                continue;
            }

            const QString name = format.property(kMentionNameProperty).toString();
            const QVariant stored = format.property(kMentionValueProperty);
            for (int index = 0; index < length; ++index) {
                if (fragmentText.at(index).unicode() != 0xFFFC) {
                    ++offset;
                    continue;
                }

                MentionSpan span;
                span.start = offset;
                span.length = 1 + static_cast<int>(name.size());
                span.name = name;
                span.text = QStringLiteral("@") + name;
                span.value = stored.isValid() ? stored : QVariant(name);
                spans.append(span);
                offset += span.length;
            }
        }
    }

    return spans;
}

int Mention::mentionCount() const
{
    return static_cast<int>(mentions().size());
}

bool Mention::hasMentions() const
{
    return !mentions().isEmpty();
}

void Mention::setText(const QString& text)
{
    cancelTrigger();

    // Replacing the contents is a host action, not typing: the text must not
    // end up opening a candidate list even when it happens to end in " @".
    suppressTrigger_ = true;
    editor_->setPlainText(text);
    suppressTrigger_ = false;

    requestUpdate();
}

void Mention::clear()
{
    setText(QString());
}

void Mention::setCandidates(const QStringList& names)
{
    candidateData_.clear();
    for (const QString& name : names) {
        if (!name.isEmpty())
            candidateData_.append(MentionCandidate{name, QVariant(name)});
    }

    if (popupActive_)
        refreshPopup();
}

QStringList Mention::candidates() const
{
    QStringList names;
    for (const MentionCandidate& candidate : candidateData_)
        names.append(candidate.name);
    return names;
}

void Mention::addCandidate(const QString& name, const QVariant& value)
{
    if (name.isEmpty())
        return;

    candidateData_.append(MentionCandidate{name, value.isValid() ? value : QVariant(name)});
    if (popupActive_)
        refreshPopup();
}

void Mention::clearCandidates()
{
    candidateData_.clear();
    if (popupActive_)
        refreshPopup();
}

bool Mention::isCandidatePopupVisible() const
{
    return popupActive_;
}

QStringList Mention::visibleCandidates() const
{
    QStringList names;
    for (int row = 0; row < popup_->count(); ++row) {
        if (const QListWidgetItem* item = popup_->item(row))
            names.append(item->text());
    }
    return names;
}

int Mention::currentCandidate() const
{
    return popup_->currentRow();
}

QString Mention::queryText() const
{
    if (triggerPos_ < 0)
        return QString();

    const QTextCursor cursor = editor_->textCursor();
    const int end = cursor.hasSelection() ? cursor.selectionEnd() : cursor.position();
    const int start = triggerPos_ + 1;
    if (end <= start)
        return QString();

    const QString documentText = editor_->document()->toPlainText();
    const int clampedEnd = std::min(end, static_cast<int>(documentText.size()));

    return documentText.mid(start, std::max(0, clampedEnd - start));
}

bool Mention::triggerIsValid() const
{
    if (!popupActive_ || triggerPos_ < 0)
        return false;

    const QTextDocument* document = editor_->document();
    if (triggerPos_ >= document->characterCount() - 1
        || document->characterAt(triggerPos_) != QLatin1Char('@')) {
        return false;
    }

    const QTextCursor cursor = editor_->textCursor();
    const int caret = cursor.hasSelection() ? cursor.selectionEnd() : cursor.position();
    return caret >= triggerPos_;
}

void Mention::cancelTrigger()
{
    popupActive_ = false;
    triggerPos_ = -1;
    if (popup_ != nullptr)
        popup_->hide();
}

void Mention::refreshPopup()
{
    if (!popupActive_ || refreshingPopup_)
        return;

    // A popup rebuild can reach host code through QListWidget signals; a guard
    // keeps that from re-entering this function and clearing the list twice.
    refreshingPopup_ = true;
    struct Reset final
    {
        bool& flag;
        ~Reset() { flag = false; }
    } reset{refreshingPopup_};

    const QString query = queryText();
    popup_->clear();

    for (const MentionCandidate& candidate : candidateData_) {
        if (query.isEmpty() || candidate.name.startsWith(query, Qt::CaseInsensitive))
            popup_->addItem(candidate.name);
    }

    if (popup_->count() == 0) {
        popupActive_ = false;
        popup_->hide();
        return;
    }

    popup_->setCurrentRow(0);
    positionPopup();
    popup_->show();
    popup_->raise();
}

void Mention::positionPopup()
{
    if (!popupActive_)
        return;

    const QRect cursorRect = editor_->cursorRect();
    const QPoint anchor = editor_->viewport()->mapTo(this, cursorRect.bottomLeft());
    const int minimumWidth = 160;
    const int popupWidth = std::clamp(editor_->width() / 2, minimumWidth,
                                      std::max(minimumWidth, width() - 16));
    const int popupHeight = std::min(180, std::max(32, popup_->count() * 32 + 8));
    const int x = std::clamp(anchor.x(), 8, std::max(8, width() - popupWidth - 8));

    int y = anchor.y() + 4;
    if (y + popupHeight > height() - 8)
        y = std::max(8, anchor.y() - popupHeight - 4);

    popup_->setGeometry(x, y, popupWidth, popupHeight);
}

void Mention::moveCandidate(int delta)
{
    const int count = popup_->count();
    if (count <= 0)
        return;

    const int current = std::max(0, popup_->currentRow());
    popup_->setCurrentRow((current + delta + count) % count);
    if (QListWidgetItem* item = popup_->currentItem())
        popup_->scrollToItem(item);
}

void Mention::chooseCurrentCandidate()
{
    if (!popupActive_ || popup_->count() == 0)
        return;

    QListWidgetItem* item = popup_->currentItem();
    if (item == nullptr)
        item = popup_->item(0);
    if (item == nullptr)
        return;

    const QString name = item->text();
    MentionCandidate chosen{name, QVariant(name)};
    for (const MentionCandidate& candidate : candidateData_) {
        if (candidate.name == name) {
            chosen = candidate;
            break;
        }
    }

    insertCandidate(chosen);
}

void Mention::insertCandidate(const MentionCandidate& candidate)
{
    const int start = triggerPos_;
    popupActive_ = false;
    triggerPos_ = -1;
    popup_->hide();

    QTextCursor cursor = editor_->textCursor();
    int end = cursor.hasSelection() ? cursor.selectionEnd() : cursor.position();
    if (start < 0 || end < start)
        return;

    end = std::min(end, static_cast<int>(editor_->document()->characterCount()) - 1);
    if (end < start)
        return;

    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();

    const QVariant stored = candidate.value.isValid() ? candidate.value : QVariant(candidate.name);
    QTextCharFormat format;
    format.setObjectType(kMentionObjectType);
    format.setProperty(kMentionNameProperty, candidate.name);
    format.setProperty(kMentionValueProperty, stored);
    cursor.insertText(objectReplacementString(), format);
    editor_->setTextCursor(cursor);

    requestUpdate();
    emit mentionInserted(candidate.name, stored);
}

void Mention::insertMention(const QString& name, const QVariant& value)
{
    if (name.isEmpty())
        return;

    cancelTrigger();

    QTextCursor cursor = editor_->textCursor();
    if (cursor.hasSelection())
        cursor.removeSelectedText();

    const QVariant stored = value.isValid() ? value : QVariant(name);
    QTextCharFormat format;
    format.setObjectType(kMentionObjectType);
    format.setProperty(kMentionNameProperty, name);
    format.setProperty(kMentionValueProperty, stored);
    cursor.insertText(objectReplacementString(), format);
    editor_->setTextCursor(cursor);

    requestUpdate();
    emit mentionInserted(name, stored);
}

bool Mention::handleEditorKey(QKeyEvent* event)
{
    if (event == nullptr || event->type() != QEvent::KeyPress)
        return false;

    const int key = event->key();

    if (popupActive_) {
        switch (key) {
        case Qt::Key_Down:
            moveCandidate(1);
            return true;
        case Qt::Key_Up:
            moveCandidate(-1);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Tab:
            chooseCurrentCandidate();
            return true;
        case Qt::Key_Escape:
            cancelTrigger();
            return true;
        case Qt::Key_Backspace:
            // Let the editor delete the character; textChanged then either
            // re-filters the query or closes the list when the '@' is gone.
            return false;
        default:
            break;
        }

        // Cursor movement and ordinary characters are left to the editor; its
        // cursorPositionChanged signal cancels the trigger once the cursor
        // leaves the query and textChanged re-filters or retriggers the list.
        //
        // A second '@' is deliberately *not* consumed here. The editor inserts
        // it like any other character, and textChanged retargets the trigger
        // when the new '@' sits on a word boundary. That keeps each '@' a real
        // trigger ("@alice @bob") without a key filter inserting text or
        // opening a popup re-entrantly, and a '@' in the middle of a word stays
        // literal.
        return false;
    }

    return false;
}

bool Mention::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == editor_ && event->type() == QEvent::KeyPress)
        return handleEditorKey(static_cast<QKeyEvent*>(event));

    if (watched == editor_ && event->type() == QEvent::FocusOut) {
        if (popupActive_)
            cancelTrigger();
    }

    if (watched == editor_->viewport() && event->type() == QEvent::MouseButtonPress) {
        if (popupActive_)
            cancelTrigger();
    }

    return View::eventFilter(watched, event);
}

void Mention::keyPressEvent(QKeyEvent* event)
{
    if (!isEnabled()) {
        View::keyPressEvent(event);
        return;
    }

    // The editor is the focus proxy in normal use; this path makes a key event
    // delivered directly to the Mention widget (tests, accessibility tools)
    // behave identically. The guard matters because an ignored forwarded key
    // would otherwise propagate back to this widget and be forwarded again.
    if (forwardingKey_) {
        event->ignore();
        return;
    }

    editor_->setFocus(Qt::OtherFocusReason);
    QKeyEvent forwarded(event->type(), event->key(), event->modifiers(), event->text(),
                        event->isAutoRepeat(), event->count());
    forwardingKey_ = true;
    QApplication::sendEvent(editor_, &forwarded);
    forwardingKey_ = false;
    event->accept();
}

void Mention::mousePressEvent(QMouseEvent* event)
{
    editor_->setFocus(Qt::MouseFocusReason);
    View::mousePressEvent(event);
}

void Mention::resizeEvent(QResizeEvent* event)
{
    View::resizeEvent(event);
    if (popupActive_)
        positionPopup();
}

void Mention::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    setMeasuredSize(QSizeF(width.resolve(density.dp(320.0)),
                           height.resolve(density.dp(120.0))));
}

void Mention::paintContent(QPainter& painter, const QRectF& content)
{
    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();
    const qreal unit = theme.density().dp(1.0);
    const bool focused = hasFocus() || editor_->hasFocus();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.setPen(Qt::NoPen);
    painter.setBrush(colours.surfaceContainerHighest);
    painter.drawRoundedRect(content, unit * 8.0, unit * 8.0);

    painter.setPen(QPen(focused ? colours.primary : colours.outline,
                        focused ? unit * 2.0 : unit));
    const qreal underlineY = content.bottom() - unit;
    painter.drawLine(QPointF(content.left() + unit * 8.0, underlineY),
                     QPointF(content.right() - unit * 8.0, underlineY));

    painter.restore();
}

} // namespace md

#include "mention.moc"
