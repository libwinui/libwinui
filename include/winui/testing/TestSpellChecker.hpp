// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Utf8.hpp"
#include "winui/render/SpellChecker.hpp"
#include "winui/render/TextAnalyzer.hpp"

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cstddef>
#include <cstdint>

namespace ui::testing
{

/// Deterministic, in-process SpellChecker for headless tests. It uses no system dictionary, only
/// the words the test declared misspelled.
///
/// Every change (a new misspelling, a settings flip, add/ignore/autocorrect) bumps generation(),
/// which tells a synced control to check again. Always reports "en-US" while available. Words are
/// runs of letters, digits, underscores and apostrophes, matched with exact case.
///
/// Example:
/// @code
/// ui::testing::HeadlessTextMeasurer measurer;
/// ui::TextBoxControl tb{"Ths is a tset"};
/// const ui::Size size = tb.measure({200.0f, ui::Unbounded}, measurer, ui::Theme::default_light());
/// tb.arrange({100.0f, 100.0f, 200.0f, size.height});
/// ui::testing::set_focused(tb, true);
///
/// ui::testing::TestSpellChecker checker;
/// checker.add_misspelling("Ths", {"This"});
/// checker.add_misspelling("tset", {"test"});
/// tb.sync_spell_checker(checker);
/// tb.tick(ui::Duration{0}); // checking happens on the next tick, never inside the sync
///
/// REQUIRE(tb.spelling_errors().size() == 2);
/// REQUIRE(checker.check_count() == 1);
/// REQUIRE(checker.suggest("Ths") == std::vector<std::string>{"This"});
/// @endcode
class TestSpellChecker final : public SpellChecker
{
public:
    // ---- test setup ----
    /// Declares `word` (exact case) misspelled, with the list suggest() returns for it.
    void add_misspelling(std::string word, std::vector<std::string> suggestions = {})
    {
        misspellings_[std::move(word)] = std::move(suggestions);
        ++generation_;
    }
    /// Simulates a missing or present system dictionary. While unavailable, checks find nothing
    /// and language() is empty. Default true.
    void set_available(bool available)
    {
        available_ = available;
        ++generation_;
    }
    /// Flips the user's "highlight misspelled words" setting (default true).
    void set_highlight_enabled(bool enabled)
    {
        highlight_ = enabled;
        ++generation_;
    }
    /// Flips the user's "autocorrect misspelled words" setting (default true).
    void set_autocorrect_enabled(bool enabled)
    {
        autocorrect_ = enabled;
        ++generation_;
    }

    // ---- test inspection ----
    /// How many times check() ran, available or not, so a test can show a control didn't check
    /// again.
    [[nodiscard]] int check_count() const
    {
        return check_count_;
    }
    /// Words passed to add_to_dictionary(), in order.
    [[nodiscard]] const std::vector<std::string>& added() const
    {
        return added_;
    }
    /// Words passed to ignore_for_session(), in order.
    [[nodiscard]] const std::vector<std::string>& ignored() const
    {
        return ignored_;
    }

    // ---- SpellChecker ----
    /// The set_available() state.
    [[nodiscard]] bool available() const override
    {
        return available_;
    }
    /// "en-US" while available, empty otherwise.
    [[nodiscard]] std::string language() const override
    {
        return available_ ? "en-US" : "";
    }
    /// The set_highlight_enabled() state.
    [[nodiscard]] bool highlight_enabled() const override
    {
        return highlight_;
    }
    /// The set_autocorrect_enabled() state.
    [[nodiscard]] bool autocorrect_enabled() const override
    {
        return autocorrect_;
    }
    /// Starts at 1 and bumps on every change of the checker's state.
    [[nodiscard]] std::uint64_t generation() const override
    {
        return generation_;
    }

    /// Reports each declared misspelling (SpellingAction::Suggest) or autocorrection
    /// (SpellingAction::Replace) in `text`, as byte offsets; added or ignored words are skipped.
    /// Words are runs of letters, digits, underscores and apostrophes, matched with exact case.
    /// This doesn't reproduce how the system checker splits words.
    [[nodiscard]] std::vector<SpellingError> check(std::string_view text) override
    {
        ++check_count_;
        std::vector<SpellingError> errors;
        if (!available_)
        {
            return errors;
        }
        std::size_t i = 0;
        while (i < text.size())
        {
            if (!is_word_char(code_point_at(text, i)))
            {
                i = next_code_point(text, i);
                continue;
            }
            const std::size_t start = i;
            while (i < text.size() && is_word_char(code_point_at(text, i)))
            {
                i = next_code_point(text, i);
            }
            const std::string word{text.substr(start, i - start)};
            if (accepted_.contains(word))
            {
                continue;
            }
            if (const auto fix = autocorrections_.find(word); fix != autocorrections_.end())
            {
                errors.push_back(SpellingError{start, word.size(), SpellingAction::Replace, fix->second});
            }
            else if (misspellings_.contains(word))
            {
                errors.push_back(SpellingError{start, word.size(), SpellingAction::Suggest, {}});
            }
        }
        return errors;
    }

    /// The suggestions declared with add_misspelling() for `word`, or empty.
    [[nodiscard]] std::vector<std::string> suggest(std::string_view word) override
    {
        const auto it = misspellings_.find(std::string{word});
        return it != misspellings_.end() ? it->second : std::vector<std::string>{};
    }

    /// Accepts `word` from now on and records it in added().
    void add_to_dictionary(std::string_view word) override
    {
        added_.emplace_back(word);
        accepted_.emplace(word);
        ++generation_;
    }
    /// Accepts `word` from now on and records it in ignored().
    void ignore_for_session(std::string_view word) override
    {
        ignored_.emplace_back(word);
        accepted_.emplace(word);
        ++generation_;
    }
    /// Makes check() report `from` as a Replace error carrying `to`; wins over a misspelling.
    void add_autocorrection(std::string_view from, std::string_view to) override
    {
        autocorrections_[std::string{from}] = std::string{to};
        ++generation_;
    }

private:
    [[nodiscard]] static bool is_word_char(char32_t ch)
    {
        return text_analyzer().is_word_char(ch) || ch == '\'';
    }

    std::map<std::string, std::vector<std::string>> misspellings_;
    std::map<std::string, std::string>              autocorrections_;
    std::set<std::string>                           accepted_;
    std::vector<std::string>                        added_;
    std::vector<std::string>                        ignored_;

    bool          available_   = true;
    bool          highlight_   = true;
    bool          autocorrect_ = true;
    std::uint64_t generation_  = 1;
    int           check_count_ = 0;
};

} // namespace ui::testing
