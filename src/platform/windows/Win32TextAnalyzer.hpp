// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32

#include "winui/render/TextAnalyzer.hpp"

#include <memory>

namespace ui::win32
{

/// The Windows TextAnalyzer: grapheme clusters, word characters and full case mapping from the
/// system icu.dll (Windows 10 1903+), loaded at run time, with the built-in analyzer and
/// LCMapStringEx as fallbacks on older systems.
///
/// The Win32 hosts install one via install_text_analyzer(); constructing one directly is for
/// a custom host. All offsets are UTF-8 byte offsets.
///
/// Example:
/// @code
/// const ui::win32::Win32TextAnalyzer analyzer;
/// if (analyzer.has_icu())
/// {
///     REQUIRE(analyzer.to_upper("stra\xC3\x9F" "e") == "STRASSE"); // full case mapping of U+00DF
/// }
/// const std::string text = "e\xCC\x81x"; // "e" + combining acute + "x"
/// REQUIRE(analyzer.next_cluster(text, 0) == 3); // the accent belongs to the "e"
/// @endcode
class Win32TextAnalyzer final : public TextAnalyzer
{
public:
    /// Loads icu.dll if present; never fails.
    Win32TextAnalyzer();
    /// Unloads icu.dll.
    ~Win32TextAnalyzer() override;
    /// Not copyable: owns the loaded library.
    Win32TextAnalyzer(const Win32TextAnalyzer&) = delete;
    /// Not copyable: owns the loaded library.
    Win32TextAnalyzer& operator=(const Win32TextAnalyzer&) = delete;

    /// Whether icu.dll was found; without it the fallbacks above answer.
    [[nodiscard]] bool has_icu() const;

    /// ICU's character break iterator over the UTF-8 text: the byte offset of the next grapheme
    /// cluster boundary after `pos` (emoji sequences and flags are one cluster).
    [[nodiscard]] std::size_t next_cluster(std::string_view text, std::size_t pos) const override;
    /// The byte offset of the grapheme cluster boundary before `pos`.
    [[nodiscard]] std::size_t previous_cluster(std::string_view text, std::size_t pos) const override;
    /// u_isalnum plus the combining-mark categories, so CJK and accented words select whole.
    [[nodiscard]] bool is_word_char(char32_t code_point) const override;
    /// Full, locale-independent upper-casing (U+00DF becomes "SS"); LCMapStringEx without ICU.
    [[nodiscard]] std::string to_upper(std::string_view text) const override;
    /// Full, locale-independent lower-casing; LCMapStringEx without ICU.
    [[nodiscard]] std::string to_lower(std::string_view text) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Installs the process's one Win32TextAnalyzer as text_analyzer() — what the Win32 hosts call
/// as they start. Idempotent. Call it yourself only from code that edits text before or without
/// ui::run().
void install_text_analyzer();

} // namespace ui::win32

#endif // _WIN32
