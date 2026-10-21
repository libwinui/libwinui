// SPDX-License-Identifier: MIT
#ifdef _WIN32

#include "Win32SpellChecker.hpp"

#include "../Backend.hpp"
#include "Win32String.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/SpellChecker.hpp"

#include <map>
#include <memory>
#include <objbase.h> // CoInitializeEx/CoCreateInstance; <windows.h> skips it under WIN32_LEAN_AND_MEAN
#include <spellcheck.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <cwctype>

namespace ui::win32
{
namespace
{

using Microsoft::WRL::ComPtr;

// Where Settings > Time & language > Typing keeps its switches. Undocumented; 0 is off, and a
// missing value reads as on, which is how a user who never touched the switch has it.
constexpr wchar_t TypingKey[]        = L"Software\\Microsoft\\TabletTip\\1.7";
constexpr wchar_t HighlightValue[]   = L"EnableSpellchecking";
constexpr wchar_t AutocorrectValue[] = L"EnableAutocorrection";

// A seed of classic English typos. Only words the OS dictionary flags get replaced, so a
// seed word that is valid in some variant is never applied.
constexpr std::pair<const wchar_t*, const wchar_t*> BuiltinEnglishAutocorrections[] = {
    {L"teh", L"the"},
    {L"adn", L"and"},
    {L"taht", L"that"},
    {L"wiht", L"with"},
    {L"hte", L"the"},
    {L"recieve", L"receive"},
    {L"recieved", L"received"},
    {L"beleive", L"believe"},
    {L"seperate", L"separate"},
    {L"definately", L"definitely"},
    {L"occured", L"occurred"},
    {L"occurence", L"occurrence"},
    {L"untill", L"until"},
    {L"becuase", L"because"},
    {L"accomodate", L"accommodate"},
    {L"acheive", L"achieve"},
    {L"goverment", L"government"},
    {L"enviroment", L"environment"},
    {L"tommorow", L"tomorrow"},
    {L"wierd", L"weird"},
};

[[nodiscard]] std::wstring lowercase(std::wstring_view s)
{
    std::wstring out{s};
    for (wchar_t& ch : out)
    {
        ch = static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(ch)));
    }
    return out;
}

// Carries the typed word's capitalization over to its correction: "Teh" → "The",
// "TEH" → "THE", "teh" → "the".
[[nodiscard]] std::wstring match_case(std::wstring_view typed, std::wstring_view correction)
{
    std::wstring out{correction};
    if (typed.empty() || out.empty() || !std::iswupper(static_cast<std::wint_t>(typed.front())))
    {
        return out;
    }
    bool all_upper = typed.size() > 1;
    for (wchar_t ch : typed)
    {
        if (std::iswalpha(static_cast<std::wint_t>(ch)) && !std::iswupper(static_cast<std::wint_t>(ch)))
        {
            all_upper = false;
            break;
        }
    }
    if (all_upper)
    {
        for (wchar_t& ch : out)
        {
            ch = static_cast<wchar_t>(std::towupper(static_cast<std::wint_t>(ch)));
        }
    }
    else
    {
        out.front() = static_cast<wchar_t>(std::towupper(static_cast<std::wint_t>(out.front())));
    }
    return out;
}

// 0 is off, anything else or absent is on.
[[nodiscard]] bool setting_on(const wchar_t* value_name)
{
    DWORD value = 0;
    DWORD size  = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, TypingKey, value_name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
    {
        return true;
    }
    return value != 0;
}

[[nodiscard]] std::wstring keyboard_language()
{
    const HKL    hkl    = GetKeyboardLayout(0);
    const LANGID langid = LOWORD(reinterpret_cast<std::uintptr_t>(hkl));
    wchar_t      tag[LOCALE_NAME_MAX_LENGTH]{};
    if (LCIDToLocaleName(MAKELCID(langid, SORT_DEFAULT), tag, LOCALE_NAME_MAX_LENGTH, 0) == 0)
    {
        return {};
    }
    return tag;
}

[[nodiscard]] std::vector<std::wstring> drain(IEnumString* strings)
{
    std::vector<std::wstring> out;
    if (!strings)
    {
        return out;
    }
    for (;;)
    {
        LPOLESTR s       = nullptr;
        ULONG    fetched = 0;
        if (strings->Next(1, &s, &fetched) != S_OK || fetched == 0)
        {
            break;
        }
        out.emplace_back(s);
        CoTaskMemFree(s);
    }
    return out;
}

} // namespace

struct Win32SpellChecker::Impl
{
    bool                         com_owned     = false;
    bool                         factory_tried = false;
    ComPtr<ISpellCheckerFactory> factory;
    // Per language tag; a null entry records "no dictionary for this tag" so an
    // unsupported layout isn't re-probed on every keystroke.
    std::map<std::wstring, ComPtr<ISpellChecker>> checkers;

    std::wstring pinned_language;
    std::wstring language;
    bool         highlight   = true;
    bool         autocorrect = true;
    bool         builtin     = true;

    std::uint64_t generation = 1;

    // Lowercase key → correction, see add_autocorrection().
    std::map<std::wstring, std::wstring> autocorrections;

    ~Impl()
    {
        // Release every COM interface before CoUninitialize, or teardown crashes.
        checkers.clear();
        factory.Reset();
        if (com_owned)
        {
            CoUninitialize();
        }
    }

    ISpellChecker* current()
    {
        if (language.empty())
        {
            return nullptr;
        }
        if (!factory_tried)
        {
            factory_tried    = true;
            const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            com_owned        = co == S_OK || co == S_FALSE;
            CoCreateInstance(__uuidof(SpellCheckerFactory), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        }
        if (!factory)
        {
            return nullptr;
        }
        auto it = checkers.find(language);
        if (it == checkers.end())
        {
            ComPtr<ISpellChecker> checker;
            BOOL                  supported = FALSE;
            if (SUCCEEDED(factory->IsSupported(language.c_str(), &supported)) && supported)
            {
                factory->CreateSpellChecker(language.c_str(), &checker);
            }
            it = checkers.emplace(language, std::move(checker)).first;
        }
        return it->second.Get();
    }

    [[nodiscard]] bool english() const
    {
        return language.size() >= 2 && (language[0] == L'e' || language[0] == L'E') &&
               (language[1] == L'n' || language[1] == L'N') && (language.size() == 2 || language[2] == L'-');
    }
};

Win32SpellChecker::Win32SpellChecker(const std::string& language, bool builtin_autocorrections) :
    impl_(std::make_unique<Impl>())
{
    impl_->pinned_language = widen(language);
    impl_->language        = impl_->pinned_language.empty() ? keyboard_language() : impl_->pinned_language;
    impl_->builtin         = builtin_autocorrections;
    impl_->highlight       = setting_on(HighlightValue);
    impl_->autocorrect     = setting_on(AutocorrectValue);
}

Win32SpellChecker::~Win32SpellChecker() = default;

void Win32SpellChecker::refresh_language()
{
    if (!impl_->pinned_language.empty())
    {
        return;
    }
    std::wstring tag = keyboard_language();
    if (tag != impl_->language)
    {
        impl_->language = std::move(tag);
        ++impl_->generation;
    }
}

void Win32SpellChecker::refresh_settings()
{
    const bool highlight   = setting_on(HighlightValue);
    const bool autocorrect = setting_on(AutocorrectValue);
    if (highlight != impl_->highlight || autocorrect != impl_->autocorrect)
    {
        impl_->highlight   = highlight;
        impl_->autocorrect = autocorrect;
        ++impl_->generation;
    }
}

bool Win32SpellChecker::available() const
{
    return impl_->current() != nullptr;
}

std::string Win32SpellChecker::language() const
{
    return narrow(impl_->language);
}

bool Win32SpellChecker::highlight_enabled() const
{
    return impl_->highlight;
}

bool Win32SpellChecker::autocorrect_enabled() const
{
    return impl_->autocorrect;
}

std::uint64_t Win32SpellChecker::generation() const
{
    return impl_->generation;
}

std::vector<SpellingError> Win32SpellChecker::check(std::string_view text)
{
    std::vector<SpellingError> out;
    ISpellChecker*             checker = impl_->current();
    if (!checker || text.empty())
    {
        return out;
    }
    // ISpellChecker speaks UTF-16 and reports its ranges in UTF-16 units; they are mapped back
    // onto `text`'s byte offsets below.
    const std::wstring         owned = widen(text);
    ComPtr<IEnumSpellingError> errors;
    if (FAILED(checker->Check(owned.c_str(), &errors)) || !errors)
    {
        return out;
    }
    for (;;)
    {
        ComPtr<ISpellingError> error;
        if (errors->Next(&error) != S_OK) // S_FALSE: no more errors
        {
            break;
        }
        ULONG             start  = 0;
        ULONG             length = 0;
        CORRECTIVE_ACTION action = CORRECTIVE_ACTION_NONE;
        error->get_StartIndex(&start);
        error->get_Length(&length);
        error->get_CorrectiveAction(&action);
        if (action == CORRECTIVE_ACTION_NONE || start + length > owned.size())
        {
            continue;
        }

        const std::size_t first = utf8_offset_from_utf16(text, start);
        const std::size_t last  = utf8_offset_from_utf16(text, start + length);
        SpellingError     e{first, last - first, SpellingAction::Suggest, {}};
        if (action == CORRECTIVE_ACTION_REPLACE)
        {
            LPWSTR replacement = nullptr;
            if (SUCCEEDED(error->get_Replacement(&replacement)) && replacement)
            {
                e.action      = SpellingAction::Replace;
                e.replacement = narrow(replacement);
                CoTaskMemFree(replacement);
            }
        }
        else if (action == CORRECTIVE_ACTION_DELETE)
        {
            e.action = SpellingAction::Delete;
        }
        else
        {
            // A plain misspelling becomes Replace when the app's autocorrection table has an entry.
            const std::wstring_view word = std::wstring_view{owned}.substr(start, length);
            const auto              fix  = impl_->autocorrections.find(lowercase(word));
            if (fix != impl_->autocorrections.end())
            {
                e.action      = SpellingAction::Replace;
                e.replacement = narrow(match_case(word, fix->second));
            }
            else if (impl_->builtin && impl_->english())
            {
                const std::wstring key = lowercase(word);
                for (const auto& [from, to] : BuiltinEnglishAutocorrections)
                {
                    if (key == from)
                    {
                        e.action      = SpellingAction::Replace;
                        e.replacement = narrow(match_case(word, to));
                        break;
                    }
                }
            }
        }
        out.push_back(std::move(e));
    }
    return out;
}

std::vector<std::string> Win32SpellChecker::suggest(std::string_view word)
{
    ISpellChecker* checker = impl_->current();
    if (!checker || word.empty())
    {
        return {};
    }
    const std::wstring  owned = widen(word);
    ComPtr<IEnumString> suggestions;
    if (FAILED(checker->Suggest(owned.c_str(), &suggestions)))
    {
        return {};
    }
    std::vector<std::string> out;
    for (const std::wstring& suggestion : drain(suggestions.Get()))
    {
        out.push_back(narrow(suggestion));
    }
    return out;
}

void Win32SpellChecker::add_to_dictionary(std::string_view word)
{
    if (ISpellChecker* checker = impl_->current(); checker && !word.empty())
    {
        const std::wstring owned = widen(word);
        checker->Add(owned.c_str());
        ++impl_->generation;
    }
}

void Win32SpellChecker::ignore_for_session(std::string_view word)
{
    if (ISpellChecker* checker = impl_->current(); checker && !word.empty())
    {
        const std::wstring owned = widen(word);
        checker->Ignore(owned.c_str());
        ++impl_->generation;
    }
}

void Win32SpellChecker::add_autocorrection(std::string_view from, std::string_view to)
{
    if (from.empty())
    {
        return;
    }
    impl_->autocorrections[lowercase(widen(from))] = widen(to);
    ++impl_->generation;
}

} // namespace ui::win32

namespace ui::detail
{

void link_spell_checker()
{
    win32::spell_checker_link.create = [](bool builtin_autocorrections) -> std::unique_ptr<SpellChecker>
    { return std::make_unique<win32::Win32SpellChecker>(std::string{}, builtin_autocorrections); };
    win32::spell_checker_link.refresh = [](SpellChecker& checker, bool language, bool settings)
    {
        auto& win32 = static_cast<win32::Win32SpellChecker&>(checker);
        if (language)
        {
            win32.refresh_language();
        }
        if (settings)
        {
            win32.refresh_settings();
        }
    };
}

} // namespace ui::detail


#endif // _WIN32
