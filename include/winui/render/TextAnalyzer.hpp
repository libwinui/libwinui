// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <string_view>
#include <utility>

#include <cstddef>

namespace ui
{

/// Unicode text segmentation and case mapping over UTF-8: where a caret may stop, what counts as
/// a word character, and how CharacterCasing changes typed text.
///
/// The base class is itself the built-in, deterministic implementation. A cluster is a code point
/// plus its trailing combining marks, variation selectors, emoji modifiers and ZWJ sequences, and
/// casing covers Latin, Greek and Cyrillic. A platform analyzer overrides whichever members it
/// can answer better, and the library installs one with set_text_analyzer() at start-up. Every
/// offset is a byte offset on a code-point boundary.
///
/// Example:
/// @code
/// // The editing keys of a custom text control.
/// const ui::TextAnalyzer& analyzer = ui::text_analyzer();
///
/// const std::string text  = "Hello world";
/// std::size_t       caret = 0;
/// caret = analyzer.next_cluster(text, caret);     // 1: Right arrow from the start
/// caret = analyzer.previous_cluster(text, caret); // 0: and back
///
/// // Ctrl+Right: past the rest of the word, then past the spaces after it.
/// while (caret < text.size() && analyzer.is_word_char(ui::code_point_at(text, caret)))
/// {
///     caret = analyzer.next_cluster(text, caret);
/// }
/// while (caret < text.size() && !analyzer.is_word_char(ui::code_point_at(text, caret)))
/// {
///     caret = analyzer.next_cluster(text, caret);
/// }
/// // caret == 6, the start of "world"
///
/// // A double-click selects the word under the pointer.
/// const auto [start, end] = ui::word_run(text, 8, true); // {6, 11}: "world"
///
/// // Typed text in an upper-case field.
/// const std::string typed = analyzer.to_upper("hello"); // "HELLO"
/// @endcode
class TextAnalyzer
{
public:
    virtual ~TextAnalyzer() = default;

    /// The grapheme-cluster boundary after `pos`: where Right arrow and Delete stop.
    /// `text.size()` at or past the end.
    [[nodiscard]] virtual std::size_t next_cluster(std::string_view text, std::size_t pos) const;

    /// The grapheme-cluster boundary before `pos`: where Left arrow and Backspace stop. 0 at the start.
    [[nodiscard]] virtual std::size_t previous_cluster(std::string_view text, std::size_t pos) const;

    /// Whether `code_point` belongs to a word: letters, digits, marks and '_'.
    [[nodiscard]] virtual bool is_word_char(char32_t code_point) const;

    /// `text` in upper case, which may change its length ("ß" stays one code point here but
    /// can become "SS" on a platform that does full case mapping).
    [[nodiscard]] virtual std::string to_upper(std::string_view text) const;

    /// `text` in lower case.
    [[nodiscard]] virtual std::string to_lower(std::string_view text) const;
};

/// The analyzer in use: the one a host installed with set_text_analyzer(), or the built-in one.
/// Never null; the reference stays valid while the installed analyzer lives.
[[nodiscard]] const TextAnalyzer& text_analyzer();

/// Installs `analyzer` process-wide; not owned, and it has to outlive its use. nullptr goes
/// back to the built-in one. The library installs the platform's analyzer at start-up.
void set_text_analyzer(const TextAnalyzer* analyzer);

/// Decodes the UTF-8 code point starting at byte `pos`, or 0 at the end.
[[nodiscard]] char32_t code_point_at(std::string_view text, std::size_t pos);

/// Decodes the UTF-8 code point that ends at byte `pos`, or 0 at the start.
[[nodiscard]] char32_t code_point_before(std::string_view text, std::size_t pos);

/// The run around `pos` whose code points all agree with the one at `pos` (the one before it
/// at the end) on text_analyzer().is_word_char(): a word, or the spaces and punctuation between
/// two. What a double-click selects. With `stop_at_line_break` the run never extends across a
/// '\n'. Returns the half-open byte range `{start, end}`; `{0, 0}` for empty text.
[[nodiscard]] std::pair<std::size_t, std::size_t> word_run(std::string_view text, std::size_t pos, bool stop_at_line_break);

/// Where Ctrl+Left goes from `pos`: back over white space, then to the start of the word (or the
/// run of punctuation) before it.
[[nodiscard]] std::size_t previous_word_start(std::string_view text, std::size_t pos);
/// Where Ctrl+Right goes from `pos`: past the word (or the run of punctuation) at it, then past the
/// white space after it, to the start of the next.
[[nodiscard]] std::size_t next_word_start(std::string_view text, std::size_t pos);

} // namespace ui
