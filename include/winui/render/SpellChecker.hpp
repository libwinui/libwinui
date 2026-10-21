// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <cstddef>
#include <cstdint>

namespace ui
{

/// What the checker suggests doing about one error.
enum class SpellingAction
{
    Suggest, ///< Misspelled: offer suggest()'s list.
    Replace, ///< An autocorrection is registered: `replacement` may be applied without asking.
    Delete,  ///< Offer to delete it, as for a repeated word.
};

/// One finding of SpellChecker::check(): a range of the checked text and what to do about it.
///
/// Example:
/// @code
/// void autocorrect(ui::SpellChecker& checker, std::string& text)
/// {
///     for (const ui::SpellingError& e : checker.check(text))
///     {
///         if (e.action == ui::SpellingAction::Replace && checker.autocorrect_enabled())
///         {
///             text.replace(e.start, e.length, e.replacement);
///             break; // offsets after this one are stale now
///         }
///     }
/// }
/// @endcode
struct SpellingError
{
    std::size_t    start  = 0;                       ///< Offset of the range in the checked text.
    std::size_t    length = 0;                       ///< Length of the range.
    SpellingAction action = SpellingAction::Suggest; ///< What the checker suggests doing.
    std::string    replacement;                      ///< The correction; valid only for SpellingAction::Replace.

    friend bool operator==(const SpellingError&, const SpellingError&) = default;
};

/// The spell-checking service a text control asks for misspellings, suggestions and
/// autocorrections.
///
/// Call it from a text control. The library implements it, and a test installs TestSpellChecker.
///
/// A control doesn't call the system spell checker itself. The window installs a checker with
/// Window::set_spell_checker() (non-owning) and hands it to the focused control through
/// Element::sync_spell_checker(). The library's checker uses the system's spell checking and
/// adds what the system leaves to apps: it picks the language from the keyboard layout, and it
/// reports the user's two typing settings through highlight_enabled() and autocorrect_enabled().
///
/// Example:
/// @code
/// // A text control asks the window's checker. An element of your own can do the same.
/// class Note : public ui::Element
/// {
/// public:
///     void sync_spell_checker(ui::SpellChecker& checker) override
///     {
///         if (!checker.available() || !checker.highlight_enabled() || checked_at_ == checker.generation())
///         {
///             return;
///         }
///         errors_     = checker.check(text_); // offsets into text_, in text order
///         checked_at_ = checker.generation(); // moves on when a word is added or ignored
///     }
///
///     // On "Add to dictionary" in a context menu:
///     static void add_word(ui::SpellChecker& checker)
///     {
///         checker.add_to_dictionary("libwinui"); // every field re-checks
///     }
///
/// private:
///     std::string                    text_;
///     std::vector<ui::SpellingError> errors_;
///     std::optional<std::uint64_t>   checked_at_;
/// };
/// @endcode
class SpellChecker
{
public:
    virtual ~SpellChecker() = default;

    /// False when no dictionary is installed for the current language. Every check()
    /// then returns nothing, and a control shows no squiggles.
    [[nodiscard]] virtual bool available() const = 0;
    /// The BCP-47 tag of the language being checked, e.g. "en-US".
    [[nodiscard]] virtual std::string language() const = 0;

    /// "Highlight misspelled words", the first of the user's two typing settings (see the class
    /// comment). A control draws squiggles only while it is on, and applies
    /// SpellingAction::Replace without asking only while autocorrect_enabled() is. Neither setting
    /// changes what check() returns.
    [[nodiscard]] virtual bool highlight_enabled() const = 0;
    /// "Autocorrect misspelled words": whether a control may apply SpellingAction::Replace
    /// without asking. See highlight_enabled().
    [[nodiscard]] virtual bool autocorrect_enabled() const = 0;

    /// Bumped whenever something changed that can alter a previous check()'s answer: a word
    /// added or ignored, the language switched, a setting flipped. A control caches the
    /// generation its errors were computed at and re-checks once this moves on, which is how
    /// "Add to dictionary" in one field clears the same squiggle from every other field.
    [[nodiscard]] virtual std::uint64_t generation() const = 0;

    /// Every error in UTF-8 `text`, in text order; empty when all is well or !available().
    [[nodiscard]] virtual std::vector<SpellingError> check(std::string_view text) = 0;
    /// Replacement candidates for a misspelled `word`, best first; empty when there are none.
    [[nodiscard]] virtual std::vector<std::string> suggest(std::string_view word) = 0;

    /// Treats `word` as correctly spelled from now on. The library's checker adds it to the user's
    /// own dictionary, shared with every other app that uses the system spell checker.
    virtual void add_to_dictionary(std::string_view word) = 0;
    /// Same, but only for the rest of this session.
    virtual void ignore_for_session(std::string_view word) = 0;

    /// Registers an app-owned correction: check() reports a misspelled `from` as
    /// SpellingAction::Replace with `to` as its replacement. The correction lasts for this
    /// session in this process and never reaches the user's system-wide autocorrect list. The
    /// system offers no built-in corrections, so the table is the app's own.
    virtual void add_autocorrection(std::string_view from, std::string_view to) = 0;
};

} // namespace ui
