// Periodicity — the Mention component (framework.md §5.9).
//
// A multi-line text editor in which "@name" is an atomic node. Typing '@' opens
// a candidate list; Up/Down move through it, Enter/Tab insert the highlighted
// candidate, Escape dismisses it, and typing filters it by prefix. The node is
// stored as a QChar::ObjectReplacementCharacter with a QTextCharFormat that
// carries the name and the value, and QTextObjectInterface paints it as a
// rounded capsule. Because the node occupies exactly one document position,
// the editor itself provides the atomic semantics: Left/Right step over the
// whole node, Backspace/Delete remove it in one action, and no character can be
// inserted inside it.
//
// plainText() is not the document's raw text: it expands every node back to
// "@name", so copying a message out of the editor loses only the decoration and
// not the text. mentions() reports the [start, length] range of every node in
// that same plain-text coordinate space together with the node's value.

#pragma once

#include <md/view/view.h>

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QResizeEvent;
class QTextEdit;
class QListWidget;

namespace md {

class MentionObjectHandler;

// One entry of the candidate list. `value` is opaque to the component; when it
// is empty the name is used as the value.
struct MentionCandidate
{
    QString name;
    QVariant value;
};

// One atomic mention as it appears in plainText(): `text` is "@name", `length`
// counts those characters, and `start` is the offset in plainText().
struct MentionSpan
{
    int start = 0;
    int length = 0;
    QString name;
    QString text;
    QVariant value;
};

class Mention : public View
{
    Q_OBJECT

public:
    explicit Mention(QWidget* parent = nullptr);
    ~Mention() override;

    // The text with every mention expanded to "@name"; block separators are
    // newlines, so the ranges in mentions() index this string.
    [[nodiscard]] QString plainText() const;

    // Replaces the contents with plain text. Mentions are not reconstructed
    // from "@name" here; use insertMention() to build an atomic node.
    void setText(const QString& text);
    void clear();

    [[nodiscard]] QTextEdit* editor() const noexcept { return editor_; }

    [[nodiscard]] QList<MentionSpan> mentions() const;
    [[nodiscard]] int mentionCount() const;
    [[nodiscard]] bool hasMentions() const;

    // The candidate names offered after '@'. setCandidates() stores each name
    // as its own value; addCandidate() may attach an explicit value.
    void setCandidates(const QStringList& names);
    [[nodiscard]] QStringList candidates() const;
    void addCandidate(const QString& name, const QVariant& value = {});
    void clearCandidates();

    [[nodiscard]] bool isCandidatePopupVisible() const;
    [[nodiscard]] QStringList visibleCandidates() const;
    [[nodiscard]] int currentCandidate() const;

    // Inserts an atomic node at the current cursor position, replacing the
    // selection. Emits mentionInserted().
    void insertMention(const QString& name, const QVariant& value = {});

signals:
    void mentionInserted(const QString& name, const QVariant& value);
    void textChanged();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void onThemeChanged() override;

private:
    void applyTheme();
    void cancelTrigger();
    void refreshPopup();
    void positionPopup();
    void moveCandidate(int delta);
    void chooseCurrentCandidate();
    void insertCandidate(const MentionCandidate& candidate);
    [[nodiscard]] bool handleEditorKey(QKeyEvent* event);
    [[nodiscard]] bool triggerIsValid() const;
    [[nodiscard]] QString queryText() const;

    QTextEdit* editor_ = nullptr;
    QListWidget* popup_ = nullptr;
    MentionObjectHandler* objectHandler_ = nullptr;
    QList<MentionCandidate> candidateData_;
    int triggerPos_ = -1;
    bool popupActive_ = false;
    bool refreshingPopup_ = false;
    bool forwardingKey_ = false;
    // Set while setText() replaces the document, so a host-supplied string that
    // happens to end in " @" cannot open the candidate list.
    bool suppressTrigger_ = false;
};

} // namespace md
