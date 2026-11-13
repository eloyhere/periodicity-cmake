// Periodicity — line detection and line compatibility for the file module.
//
// A line terminator is LF ("\n"), CRLF ("\r\n") or a lone CR ("\r"). All three
// are real line breaks, and CRLF is exactly one line break (never CR followed
// by an empty LF line). A file may mix them; the line index records the
// terminator of every line so that lineEndingAt() can answer per line, while
// the document also exposes the dominant style used when new text is inserted.
//
// This is deliberately wider than md::syntax::LineIndex, which only breaks on
// LF because the syntax layer's offsets are a QString coordinate space (a whole
// decoded document). The file module indexes bytes and must treat CR-only
// files as multi-line. The two agree on CRLF and LF files.
//
// Replacement text may use any convention. RewriteOptions says which
// terminator the inserted text is normalised to, and whether the final-newline
// presence of the file is preserved when the splice reaches EOF. The bytes
// outside the replaced range are never rewritten by a replacement.

#pragma once

#include <QByteArray>
#include <QString>

#include <QtGlobal>

namespace md::file {

enum class LineEnding
{
    None,   // no terminator (an unterminated last line, or a file with no line breaks)
    Lf,     // "\n"
    CrLf,   // "\r\n"
    Cr,     // "\r"
    Mixed   // only returned by the aggregate query; never stored per line
};

struct LineEndingStats
{
    qint64 lf = 0;
    qint64 crlf = 0;
    qint64 cr = 0;
    qint64 lines = 0;
    qint64 linesWithoutTerminator = 0;

    [[nodiscard]] qint64 terminators() const noexcept { return lf + crlf + cr; }
    [[nodiscard]] bool isEmpty() const noexcept { return lines == 0; }
    [[nodiscard]] friend bool operator==(const LineEndingStats&, const LineEndingStats&) = default;
};

[[nodiscard]] QString lineEndingName(LineEnding ending);

// The terminator bytes in an ASCII-compatible encoding ("\n", "\r\n", "\r").
// LineEnding::None and LineEnding::Mixed return an empty array.
[[nodiscard]] QByteArray lineEndingBytes(LineEnding ending);

// The decoded terminator text ("\n", "\r\n", "\r"); empty for None/Mixed.
[[nodiscard]] QString lineEndingText(LineEnding ending);

// True for Lf, CrLf and Cr; false for None and Mixed.
[[nodiscard]] bool lineEndingIsConcrete(LineEnding ending) noexcept;

// How a replacement picks the terminator it writes:
//
//   PreserveDominant   the file's dominant detected style (default);
//   FollowReplacedLine the terminator of the last replaced line, falling back
//                      to the dominant style when that line is unterminated
//                      (an EOF line) or the file has no terminator at all;
//   ForceLf/ForceCrLf/ForceCr  an explicit override.
//
// The replaced *range* is always replaced in full; only the inserted text is
// normalised. Bytes outside the range keep their original terminators.
enum class LineEndingPolicy
{
    PreserveDominant,
    FollowReplacedLine,
    ForceLf,
    ForceCrLf,
    ForceCr
};

[[nodiscard]] QString lineEndingPolicyName(LineEndingPolicy policy);

struct RewriteOptions
{
    LineEndingPolicy lineEndingPolicy = LineEndingPolicy::PreserveDominant;

    // When the replaced byte span reaches EOF, keep the file's previous
    // final-newline presence: a file that ended with a terminator still does,
    // and one that did not still does not. Set false for an exact splice.
    bool preserveFinalNewline = true;
};

} // namespace md::file
