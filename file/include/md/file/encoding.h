// Periodicity — the text encodings the file module understands.
//
// The file module never guesses a byte order mark away and never silently
// falls back to Latin-1: a BOM decides UTF-8/16/32, a BOM-less file is UTF-8
// when it is strictly valid UTF-8, and otherwise the caller's fallback applies
// (System by default, i.e. the platform's local 8-bit encoding).
//
// The names are deliberately about the *file* encoding, not about a
// QTextCodec: Qt 6 removed QTextCodec, so decoding and encoding go through
// QStringDecoder / QStringEncoder (QStringConverter). That also bounds what
// can be selected: the converter knows the Unicode encodings, Latin-1 and the
// platform's local 8-bit encoding, but not an arbitrary legacy code page by
// number. "System" is therefore the only non-Unicode, non-Latin-1 choice, and
// it is the platform code page on Windows.
//
// Utf8 and Utf8Bom are the same converter with a different BOM policy, kept as
// two values so that a document can report "UTF-8 without BOM" and "UTF-8 with
// BOM" distinctly. UTF-16 and UTF-32 have explicit endianness variants; a BOM
// is a property of the file (FileFormat::bom), not part of these enum values.

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringView>

#include <optional>

namespace md::file {

enum class TextEncoding
{
    Utf8,
    Utf8Bom,
    Utf16LE,
    Utf16BE,
    Utf32LE,
    Utf32BE,
    Latin1,
    System
};

// The canonical spelling, e.g. "UTF-8", "UTF-8 BOM", "UTF-16LE", "Latin-1",
// "System".
[[nodiscard]] QString textEncodingName(TextEncoding encoding);

// Parses the canonical spelling (case-insensitive; '-' and '_' are ignored, so
// "utf16-le", "UTF-16LE" and "utf16le" all parse). Returns nullopt for an
// unknown name.
[[nodiscard]] std::optional<TextEncoding> textEncodingFromName(QStringView name);

// The byte order mark implied by the enum value: Utf8Bom, Utf16* and Utf32*
// return their BOM; Utf8, Latin1 and System return an empty array. An empty
// result is not an error: it only means "this encoding has no BOM of its own".
[[nodiscard]] QByteArray textEncodingBom(TextEncoding encoding);

// 1 for UTF-8/Latin-1/System, 2 for UTF-16, 4 for UTF-32. Used for byte-offset
// boundary checks and for line terminators.
[[nodiscard]] int textEncodingUnitSize(TextEncoding encoding) noexcept;

// True for every UTF encoding (including Utf8Bom).
[[nodiscard]] bool textEncodingIsUnicode(TextEncoding encoding) noexcept;

// True for Utf8 and Utf8Bom: the only encoding whose code units are bytes and
// whose continuation bytes need checking at an arbitrary byte offset.
[[nodiscard]] bool textEncodingIsUtf8(TextEncoding encoding) noexcept;

} // namespace md::file
