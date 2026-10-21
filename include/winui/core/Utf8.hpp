// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <string_view>

#include <cstddef>

// The escape is on purpose: without /utf-8 a literal ä would be read as two ANSI characters and pass.
static_assert(sizeof("\u00E4") == 3, "libwinui needs UTF-8 string literals: compile with /utf-8 (MSVC, clang-cl)");

namespace ui
{

/// U+FFFD, what malformed input decodes to.
inline constexpr char32_t ReplacementCharacter = 0xFFFD;

/// One decoded code point and the number of bytes it took, as returned by decode_utf8().
///
/// Example:
/// @code
/// const std::string text = "a\xC3\xA4"; // "a" then U+00E4
///
/// // A dump of the code points for a log, stepping by each one's length in bytes.
/// std::string dump;
/// for (std::size_t pos = 0; pos < text.size();)
/// {
///     const ui::Utf8Decoded d = ui::decode_utf8(text, pos); // length 1 for "a", 2 for U+00E4
///     dump += std::format("U+{:04X} ", static_cast<std::uint32_t>(d.code_point));
///     pos += d.length;
/// }
/// // dump == "U+0061 U+00E4 "
/// @endcode
struct Utf8Decoded
{
    char32_t    code_point = 0; ///< the decoded code point; U+FFFD for a malformed sequence
    std::size_t length     = 0; ///< at least 1 unless `pos` was at the end
};

/// The code point starting at byte `pos` of `text`. A malformed sequence decodes to U+FFFD and
/// takes only the bytes that belonged to it, so decoding carries on at the next one. At or past
/// the end the result is `{0, 0}`.
[[nodiscard]] Utf8Decoded decode_utf8(std::string_view text, std::size_t pos = 0);

/// Appends `code_point` to `out` as UTF-8. A surrogate or a value beyond U+10FFFF appends U+FFFD.
void append_utf8(std::string& out, char32_t code_point);

/// `code_point` as a UTF-8 string.
[[nodiscard]] std::string encode_utf8(char32_t code_point);

/// The byte offset of the code point after the one at `pos`; `text.size()` at or past the end.
[[nodiscard]] std::size_t next_code_point(std::string_view text, std::size_t pos);

/// The byte offset of the code point before `pos`; 0 at the start.
[[nodiscard]] std::size_t previous_code_point(std::string_view text, std::size_t pos);

/// True when `pos` is 0, `text.size()`, or the first byte of a code point.
[[nodiscard]] bool is_code_point_boundary(std::string_view text, std::size_t pos);

/// `pos` moved back onto the start of the code point it falls inside, clamped to the text.
[[nodiscard]] std::size_t floor_code_point(std::string_view text, std::size_t pos);

/// How many code points `text` holds, a malformed sequence counting as one.
[[nodiscard]] std::size_t code_point_count(std::string_view text);

/// True when `text` is well-formed UTF-8: no overlong forms, no surrogates, nothing past U+10FFFF.
[[nodiscard]] bool is_valid_utf8(std::string_view text);

/// `text` with every malformed sequence replaced by U+FFFD. libwinui passes text from outside,
/// such as keyboard input, the clipboard, files and the command line, through this once.
[[nodiscard]] std::string sanitize_utf8(std::string_view text);

/// `text` as UTF-16, for system and library APIs that take it.
[[nodiscard]] std::u16string to_utf16(std::string_view text);

/// `text`, which is UTF-16, as UTF-8.
[[nodiscard]] std::string from_utf16(std::u16string_view text);

/// How many UTF-16 code units `text` takes: one per code point, two past the BMP. The UTF-16
/// offset of byte `pos` is `utf16_length(text.substr(0, pos))`.
[[nodiscard]] std::size_t utf16_length(std::string_view text);

/// The byte offset of `text` that lies `units` UTF-16 code units in, the inverse of
/// utf16_length(). A count that ends between the two halves of a surrogate pair lands before
/// the pair; one past the end lands on `text.size()`.
[[nodiscard]] std::size_t utf8_offset_from_utf16(std::string_view text, std::size_t units);

/// Puts UTF-16 code units that arrive one at a time, as typed characters do, back together into
/// UTF-8. A high surrogate is held until its low half arrives. A half without its partner comes
/// out as U+FFFD. Keep one per input source. It is a plain value with one unit of state.
///
/// Example:
/// @code
/// // An emoji arrives as two units: the first returns nothing, the second the whole character.
/// void on_typed_unit(ui::Utf16Decoder& decoder, ui::TextBoxControl& editor, char16_t unit)
/// {
///     if (const std::string typed = decoder.feed(unit); !typed.empty())
///     {
///         editor.replace_selection(typed);
///     }
/// }
///
/// ui::Utf16Decoder decoder; // one per input source
/// @endcode
class Utf16Decoder
{
public:
    /// Feeds one unit and returns the text it completes: empty while a high surrogate waits,
    /// otherwise one code point, or two when a stranded high surrogate is flushed first.
    [[nodiscard]] std::string feed(char16_t unit);

private:
    char16_t high_ = 0;
};

/// The code points of a UTF-8 string, for a range-for: `for (char32_t cp : code_points(text))`.
///
/// A non-owning view: the string must outlive it. Malformed sequences yield U+FFFD, one per
/// sequence. The iterator's offset() gives the byte position of each code point, which is what
/// every text API in the framework takes.
///
/// Example:
/// @code
/// const std::string text = "Hello, world";
/// std::size_t letters = 0;
/// for (char32_t cp : ui::code_points(text))
/// {
///     letters += (cp >= U'a' && cp <= U'z') ? 1 : 0;
/// }
///
/// // offset() is the byte position the text APIs take: cut the string at the first comma.
/// std::string greeting = text;
/// const ui::CodePoints points = ui::code_points(text);
/// for (auto it = points.begin(); it != points.end(); ++it)
/// {
///     if (*it == U',')
///     {
///         greeting = text.substr(0, it.offset()); // "Hello"
///         break;
///     }
/// }
/// @endcode
class CodePoints
{
public:
    /// Forward iterator over the code points; compares by byte offset only.
    class iterator
    {
    public:
        /// Dereferencing yields a decoded code point by value.
        using value_type = char32_t;
        /// Required by std::weakly_incrementable; distances are not meaningful here.
        using difference_type = std::ptrdiff_t;

        /// A singular iterator; only assignable.
        iterator() = default;
        /// Positioned at byte `pos` of `text`, which must be a code-point boundary.
        iterator(std::string_view text, std::size_t pos) : text_(text), pos_(pos)
        {
        }

        /// The code point at the current position (U+FFFD if malformed).
        [[nodiscard]] char32_t operator*() const
        {
            return decode_utf8(text_, pos_).code_point;
        }
        /// Advances past the current code point.
        iterator& operator++()
        {
            pos_ = next_code_point(text_, pos_);
            return *this;
        }
        /// Post-increment.
        iterator operator++(int)
        {
            iterator old = *this;
            ++*this;
            return old;
        }
        /// The byte offset of the current code point.
        [[nodiscard]] std::size_t offset() const
        {
            return pos_;
        }
        /// Same byte offset; only meaningful between iterators over the same text.
        [[nodiscard]] bool operator==(const iterator& other) const
        {
            return pos_ == other.pos_;
        }

    private:
        std::string_view text_;
        std::size_t      pos_ = 0;
    };

    /// A view over `text`, which must outlive it.
    explicit CodePoints(std::string_view text) : text_(text)
    {
    }
    /// The first code point.
    [[nodiscard]] iterator begin() const
    {
        return {text_, 0};
    }
    /// One past the last code point (byte offset `text.size()`).
    [[nodiscard]] iterator end() const
    {
        return {text_, text_.size()};
    }

private:
    std::string_view text_;
};

/// A CodePoints view over `text`, for a range-for. `text` must outlive the loop.
[[nodiscard]] inline CodePoints code_points(std::string_view text)
{
    return CodePoints{text};
}

} // namespace ui
