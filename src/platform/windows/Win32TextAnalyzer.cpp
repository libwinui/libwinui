// SPDX-License-Identifier: MIT
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Win32TextAnalyzer.hpp"

#include "ProcAddress.hpp"
#include "Win32String.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/TextAnalyzer.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace ui::win32
{

namespace
{

// Declared by hand: the SDK's <icu.h> links against icu.lib, a load-time dependency a
// Windows before 1903 cannot satisfy.
using UErrorCode = int;
struct UText;
struct UBreakIterator;
struct UCaseMap;

constexpr int CharacterBreak = 0;  // UBRK_CHARACTER
constexpr int Done           = -1; // UBRK_DONE
constexpr int BufferOverflow = 15; // U_BUFFER_OVERFLOW_ERROR

// u_charType's categories that count as part of a word next to letters and digits.
constexpr std::int8_t NonSpacingMark       = 6;  // U_NON_SPACING_MARK
constexpr std::int8_t EnclosingMark        = 7;  // U_ENCLOSING_MARK
constexpr std::int8_t CombiningSpacingMark = 8;  // U_COMBINING_SPACING_MARK
constexpr std::int8_t ConnectorPunctuation = 22; // U_CONNECTOR_PUNCTUATION

bool failed(UErrorCode status)
{
    return status > 0;
}

struct Icu
{
    UText* (*utext_openUTF8)(UText*, const char*, std::int64_t, UErrorCode*)                   = nullptr;
    UText* (*utext_close)(UText*)                                                              = nullptr;
    UBreakIterator* (*ubrk_open)(int, const char*, const char16_t*, std::int32_t, UErrorCode*) = nullptr;
    void (*ubrk_setUText)(UBreakIterator*, UText*, UErrorCode*)                                = nullptr;
    std::int32_t (*ubrk_following)(UBreakIterator*, std::int32_t)                              = nullptr;
    std::int32_t (*ubrk_preceding)(UBreakIterator*, std::int32_t)                              = nullptr;
    void (*ubrk_close)(UBreakIterator*)                                                        = nullptr;
    UCaseMap* (*ucasemap_open)(const char*, std::uint32_t, UErrorCode*)                        = nullptr;
    void (*ucasemap_close)(UCaseMap*)                                                          = nullptr;
    std::int32_t (*ucasemap_utf8ToUpper)(const UCaseMap*, char*, std::int32_t, const char*, std::int32_t, UErrorCode*) = nullptr;
    std::int32_t (*ucasemap_utf8ToLower)(const UCaseMap*, char*, std::int32_t, const char*, std::int32_t, UErrorCode*) = nullptr;
    std::int8_t (*u_isalnum)(std::int32_t)  = nullptr;
    std::int8_t (*u_charType)(std::int32_t) = nullptr;

    HMODULE module = nullptr;

    bool load()
    {
        module = LoadLibraryExW(L"icu.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module == nullptr)
        {
            return false;
        }
        const auto get = [this](auto& fn, const char* name)
        {
            fn = proc_address<std::remove_reference_t<decltype(fn)>>(module, name);
            return fn != nullptr;
        };
        const bool ok = get(utext_openUTF8, "utext_openUTF8") && get(utext_close, "utext_close") &&
                        get(ubrk_open, "ubrk_open") && get(ubrk_setUText, "ubrk_setUText") &&
                        get(ubrk_following, "ubrk_following") && get(ubrk_preceding, "ubrk_preceding") &&
                        get(ubrk_close, "ubrk_close") && get(ucasemap_open, "ucasemap_open") &&
                        get(ucasemap_close, "ucasemap_close") && get(ucasemap_utf8ToUpper, "ucasemap_utf8ToUpper") &&
                        get(ucasemap_utf8ToLower, "ucasemap_utf8ToLower") && get(u_isalnum, "u_isalnum") &&
                        get(u_charType, "u_charType");
        if (!ok)
        {
            FreeLibrary(module);
            module = nullptr;
        }
        return ok;
    }
};

// LCMapStringEx's casing, for a Windows without ICU: linguistic, in the user's locale.
std::string lcmap(std::string_view text, DWORD flags)
{
    const std::wstring wide = widen(text);
    if (wide.empty())
    {
        return {};
    }
    const int    size = static_cast<int>(wide.size());
    const int    n    = LCMapStringEx(LOCALE_NAME_USER_DEFAULT,
                                      flags | LCMAP_LINGUISTIC_CASING,
                                      wide.data(),
                                      size,
                                      nullptr,
                                      0,
                                      nullptr,
                                      nullptr,
                                      0);
    std::wstring out(static_cast<std::size_t>(std::max(n, 0)), L'\0');
    if (n <= 0 ||
        LCMapStringEx(LOCALE_NAME_USER_DEFAULT, flags | LCMAP_LINGUISTIC_CASING, wide.data(), size, out.data(), n, nullptr, nullptr, 0) ==
            0)
    {
        return std::string{text};
    }
    return narrow(out);
}

} // namespace

struct Win32TextAnalyzer::Impl
{
    Icu             icu;
    bool            has_icu  = false;
    UBreakIterator* clusters = nullptr;
    UCaseMap*       casing   = nullptr;
    // The break iterator holds the text it was last given, so one call at a time.
    std::mutex mutex;

    // Runs `step` (ubrk_following or ubrk_preceding) at `pos` over `text`; nullopt when ICU
    // can't (the text is past int32 range, or opening the UText failed).
    std::optional<std::size_t> boundary(std::string_view text, std::size_t pos, bool forward)
    {
        if (clusters == nullptr || text.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        {
            return std::nullopt;
        }
        const std::lock_guard lock{mutex};
        UErrorCode            status = 0;
        UText* ut = icu.utext_openUTF8(nullptr, text.data(), static_cast<std::int64_t>(text.size()), &status);
        if (failed(status) || ut == nullptr)
        {
            return std::nullopt;
        }
        icu.ubrk_setUText(clusters, ut, &status);
        std::optional<std::size_t> result;
        if (!failed(status))
        {
            const std::int32_t at = forward ? icu.ubrk_following(clusters, static_cast<std::int32_t>(pos))
                                            : icu.ubrk_preceding(clusters, static_cast<std::int32_t>(pos));
            result                = at == Done ? (forward ? text.size() : 0) : static_cast<std::size_t>(at);
        }
        // The iterator keeps a shallow clone pointing into `text`; it is never read again before
        // the next call hands it new text, which closes the clone.
        icu.utext_close(ut);
        return result;
    }

    std::optional<std::string> map_case(std::string_view text, bool upper)
    {
        if (casing == nullptr || text.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() / 3))
        {
            return std::nullopt;
        }
        const auto   map  = upper ? icu.ucasemap_utf8ToUpper : icu.ucasemap_utf8ToLower;
        const auto   size = static_cast<std::int32_t>(text.size());
        std::string  out(text.size(), '\0');
        UErrorCode   status = 0;
        std::int32_t n = map(casing, out.data(), static_cast<std::int32_t>(out.size()), text.data(), size, &status);
        if (status == BufferOverflow)
        {
            out.assign(static_cast<std::size_t>(n), '\0');
            status = 0;
            n      = map(casing, out.data(), n, text.data(), size, &status);
        }
        if (failed(status))
        {
            return std::nullopt;
        }
        out.resize(static_cast<std::size_t>(n));
        return out;
    }
};

Win32TextAnalyzer::Win32TextAnalyzer() : impl_(std::make_unique<Impl>())
{
    Impl& d   = *impl_;
    d.has_icu = d.icu.load();
    if (!d.has_icu)
    {
        return;
    }
    UErrorCode status = 0;
    d.clusters        = d.icu.ubrk_open(CharacterBreak, "", nullptr, 0, &status);
    if (failed(status))
    {
        d.clusters = nullptr;
    }
    status   = 0;
    d.casing = d.icu.ucasemap_open("", 0, &status); // "": ICU's default locale, the user's
    if (failed(status))
    {
        d.casing = nullptr;
    }
}

Win32TextAnalyzer::~Win32TextAnalyzer()
{
    Impl& d = *impl_;
    if (d.clusters != nullptr)
    {
        d.icu.ubrk_close(d.clusters);
    }
    if (d.casing != nullptr)
    {
        d.icu.ucasemap_close(d.casing);
    }
    if (d.icu.module != nullptr)
    {
        FreeLibrary(d.icu.module);
    }
}

bool Win32TextAnalyzer::has_icu() const
{
    return impl_->has_icu;
}

std::size_t Win32TextAnalyzer::next_cluster(std::string_view text, std::size_t pos) const
{
    if (pos >= text.size())
    {
        return text.size();
    }
    if (const auto at = impl_->boundary(text, pos, true))
    {
        return std::clamp(*at, next_code_point(text, pos), text.size());
    }
    return TextAnalyzer::next_cluster(text, pos);
}

std::size_t Win32TextAnalyzer::previous_cluster(std::string_view text, std::size_t pos) const
{
    pos = std::min(pos, text.size());
    if (pos == 0)
    {
        return 0;
    }
    if (const auto at = impl_->boundary(text, pos, false))
    {
        return std::min(*at, previous_code_point(text, pos));
    }
    return TextAnalyzer::previous_cluster(text, pos);
}

bool Win32TextAnalyzer::is_word_char(char32_t code_point) const
{
    const Icu& icu = impl_->icu;
    if (!impl_->has_icu)
    {
        return TextAnalyzer::is_word_char(code_point);
    }
    const auto cp = static_cast<std::int32_t>(code_point);
    if (icu.u_isalnum(cp) != 0)
    {
        return true;
    }
    const std::int8_t type = icu.u_charType(cp);
    return type == NonSpacingMark || type == EnclosingMark || type == CombiningSpacingMark || type == ConnectorPunctuation;
}

std::string Win32TextAnalyzer::to_upper(std::string_view text) const
{
    if (auto out = impl_->map_case(text, true))
    {
        return std::move(*out);
    }
    return lcmap(text, LCMAP_UPPERCASE);
}

std::string Win32TextAnalyzer::to_lower(std::string_view text) const
{
    if (auto out = impl_->map_case(text, false))
    {
        return std::move(*out);
    }
    return lcmap(text, LCMAP_LOWERCASE);
}

void install_text_analyzer()
{
    static const Win32TextAnalyzer analyzer;
    set_text_analyzer(&analyzer);
}

} // namespace ui::win32

#endif // _WIN32
