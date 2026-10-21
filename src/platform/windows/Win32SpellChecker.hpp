// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32

#include "winui/render/SpellChecker.hpp"

#include <memory>
#include <string>

namespace ui::win32
{

/// The Windows SpellChecker: the OS spell checking API for the current keyboard language, plus
/// the user's Typing settings ("Highlight misspelled words", "Autocorrect misspelled words").
///
/// ui::run() creates one per window (configured by ui::SpellCheckOptions) and installs it
/// with Window::set_spell_checker(); TextBox squiggles and autocorrects through it. Construct one
/// yourself for a custom host. UI thread only; unavailable (no
/// squiggles) when the language has no installed dictionary.
///
/// Example:
/// @code
/// ui::win32::Win32SpellChecker checker{"en-US"};
/// if (checker.available())
/// {
///     const std::vector<ui::SpellingError> errors = checker.check("Ths is a tset.");
///     REQUIRE(errors.size() == 2);
///     REQUIRE(errors[0].start == 0); // byte offsets
///     const std::vector<std::string> fixes = checker.suggest("Ths"); // contains "This"
///     checker.ignore_for_session("libwinui");
/// }
/// testing::seams(window).set_spell_checker(&checker); // non-owning; must outlive the window
/// @endcode
// What the host reaches a Win32SpellChecker through, so that it never names the class: set by
// detail::link_spell_checker(), which only a spell-checking control calls. UI thread only.
struct SpellCheckerLink
{
    std::unique_ptr<SpellChecker> (*create)(bool builtin_autocorrections) = nullptr;
    // Win32SpellChecker::refresh_language() and/or refresh_settings().
    void (*refresh)(SpellChecker& checker, bool language, bool settings) = nullptr;
};
inline SpellCheckerLink spell_checker_link{};

class Win32SpellChecker final : public SpellChecker
{
public:
    /// `language` empty (the default): follow the keyboard layout. Non-empty (a BCP-47 tag,
    /// e.g. "en-US"): pinned to it; refresh_language() is then a no-op, so the language
    /// no longer depends on the machine's layout.
    /// `builtin_autocorrections`: seed a small table of common English typos (teh → the,
    /// recieve → receive, ...) applied whenever the active language is English; see
    /// SpellChecker::add_autocorrection() for why the table is the app's, not the OS's.
    explicit Win32SpellChecker(const std::string& language = {}, bool builtin_autocorrections = true);
    /// Releases the COM checkers, and the COM init if this object did it.
    ~Win32SpellChecker() override;

    /// Not copyable: owns COM objects.
    Win32SpellChecker(const Win32SpellChecker&) = delete;
    /// Not copyable: owns COM objects.
    Win32SpellChecker& operator=(const Win32SpellChecker&) = delete;

    /// Host hook: re-derives the language from the keyboard layout (WM_INPUTLANGCHANGE,
    /// WM_SETFOCUS). No-op for a pinned language. Bumps generation() only if it changed.
    void refresh_language();
    /// Host hook: re-reads the Typing switches (WM_SETTINGCHANGE, WM_SETFOCUS). Bumps generation()
    /// only if a setting changed.
    void refresh_settings();

    // ---- SpellChecker ----
    /// Whether a dictionary exists for language(); false means no checking at all.
    [[nodiscard]] bool available() const override;
    /// The active BCP-47 tag, e.g. "en-US": the pinned one or the keyboard layout's.
    [[nodiscard]] std::string language() const override;
    /// Settings > Typing > "Highlight misspelled words": `EnableSpellchecking` under
    /// HKCU\Software\Microsoft\TabletTip\1.7, on when the value is missing.
    [[nodiscard]] bool highlight_enabled() const override;
    /// Settings > Typing > "Autocorrect misspelled words": `EnableAutocorrection` under the same
    /// key, on when the value is missing.
    [[nodiscard]] bool autocorrect_enabled() const override;
    /// Increases whenever language, settings or dictionaries change, so callers re-check.
    [[nodiscard]] std::uint64_t generation() const override;

    /// ISpellChecker::Check over `text`; each error's start/length are UTF-8 byte offsets.
    [[nodiscard]] std::vector<SpellingError> check(std::string_view text) override;
    /// ISpellChecker::Suggest: replacements for `word`, best first; empty when unavailable.
    [[nodiscard]] std::vector<std::string> suggest(std::string_view word) override;

    /// ISpellChecker::Add: into the user's dictionary, which Windows keeps. Bumps generation().
    void add_to_dictionary(std::string_view word) override;
    /// ISpellChecker::Ignore: accepted until the process exits. Bumps generation().
    void ignore_for_session(std::string_view word) override;
    /// Adds an app-level autocorrection `from` -> `to`: matched case-insensitively, replaced
    /// in the typo's case, and taking precedence over the built-in table. Bumps generation().
    void add_autocorrection(std::string_view from, std::string_view to) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ui::win32

#endif // _WIN32
