// SPDX-License-Identifier: MIT
#ifdef _WIN32

#include "DWriteCache.hpp"

#include "DWriteMeasurer.hpp"
#include "Win32String.hpp"
#include "winui/core/PerfCounters.hpp"

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include <cstddef>
#include <cstdint>

namespace ui::win32
{

namespace
{

// Everything about a piece of text a format, a layout or a measurement depends on. Fields that
// do not matter to one of the three are left at zero by its caller.
template <typename String>
struct BasicKey
{
    String        text{};
    String        family;
    float         font_size   = 0.0f;
    float         line_height = 0.0f;
    float         max_width   = 0.0f;
    float         max_height  = 0.0f;
    int           spacing     = 0;
    std::uint16_t weight      = 0;
    std::uint8_t  italic      = 0;
    std::uint8_t  wrapping    = 0;
    std::uint8_t  horizontal  = 0;
    std::uint8_t  vertical    = 0;
    std::uint8_t  decorations = 0;
};

using KeyView = BasicKey<std::string_view>;
using Key     = BasicKey<std::string>;

template <typename A, typename B>
bool same(const A& a, const B& b)
{
    return std::string_view{a.text} == std::string_view{b.text} &&
           std::string_view{a.family} == std::string_view{b.family} && a.font_size == b.font_size &&
           a.line_height == b.line_height && a.max_width == b.max_width && a.max_height == b.max_height &&
           a.spacing == b.spacing && a.weight == b.weight && a.italic == b.italic && a.wrapping == b.wrapping &&
           a.horizontal == b.horizontal && a.vertical == b.vertical && a.decorations == b.decorations;
}

template <typename K>
std::size_t hash_key(const K& k)
{
    std::size_t h   = std::hash<std::string_view>{}(std::string_view{k.text});
    const auto  mix = [&h](std::size_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
    mix(std::hash<std::string_view>{}(std::string_view{k.family}));
    mix(std::hash<float>{}(k.font_size));
    mix(std::hash<float>{}(k.line_height));
    mix(std::hash<float>{}(k.max_width));
    mix(std::hash<float>{}(k.max_height));
    mix(static_cast<std::size_t>(k.spacing));
    mix(static_cast<std::size_t>(k.weight) | (static_cast<std::size_t>(k.italic) << 16) |
        (static_cast<std::size_t>(k.wrapping) << 20) | (static_cast<std::size_t>(k.horizontal) << 24) |
        (static_cast<std::size_t>(k.vertical) << 28) | (static_cast<std::size_t>(k.decorations) << 32));
    return h;
}

struct KeyHash
{
    using is_transparent = void;
    std::size_t operator()(const Key& k) const
    {
        return hash_key(k);
    }
    std::size_t operator()(const KeyView& k) const
    {
        return hash_key(k);
    }
};

struct KeyEqual
{
    using is_transparent = void;
    template <typename A, typename B>
    bool operator()(const A& a, const B& b) const
    {
        return same(a, b);
    }
};

Key own(const KeyView& v)
{
    Key k{.text = std::string{v.text}, .family = std::string{v.family}};
    k.font_size   = v.font_size;
    k.line_height = v.line_height;
    k.max_width   = v.max_width;
    k.max_height  = v.max_height;
    k.spacing     = v.spacing;
    k.weight      = v.weight;
    k.italic      = v.italic;
    k.wrapping    = v.wrapping;
    k.horizontal  = v.horizontal;
    k.vertical    = v.vertical;
    k.decorations = v.decorations;
    return k;
}

// The fields a text format carries.
KeyView format_key(const TextStyle& style)
{
    KeyView k{.family = style.font_family};
    k.font_size   = style.font_size;
    k.line_height = style.line_height;
    k.weight      = static_cast<std::uint16_t>(style.weight);
    k.italic      = style.italic ? 1 : 0;
    k.wrapping    = static_cast<std::uint8_t>(style.wrapping);
    k.horizontal  = static_cast<std::uint8_t>(style.horizontal);
    k.vertical    = static_cast<std::uint8_t>(style.vertical);
    return k;
}

template <typename Value>
struct Entry
{
    Value         value;
    std::uint64_t last_used = 0;
};

template <typename Value>
using Map = std::unordered_map<Key, Entry<Value>, KeyHash, KeyEqual>;

// Layouts and metrics past this many entries are aged out; formats are few and stay.
constexpr std::size_t SoftLimit = 2048;
// An entry not used in this many frames goes once the cache is past its soft limit.
constexpr std::uint64_t MaxAge = 30;

struct Cache
{
    std::mutex                     mutex;
    std::uint64_t                  frame = 1;
    Map<ComPtr<IDWriteTextFormat>> formats;
    Map<ComPtr<IDWriteTextLayout>> layouts;
    Map<TextMetrics>               metrics;
};

Cache& cache()
{
    static Cache* instance = new Cache; // never destroyed: a static destructor would race the last frame
    return *instance;
}

template <typename Value>
void age_out(Map<Value>& map, std::uint64_t frame)
{
    if (map.size() <= SoftLimit)
    {
        return;
    }
    std::erase_if(map, [frame](const auto& item) { return item.second.last_used + MaxAge < frame; });
    if (map.size() > SoftLimit * 4)
    {
        // Everything is in use (one huge frame): start again rather than grow without bound.
        std::erase_if(map, [frame](const auto& item) { return item.second.last_used < frame; });
    }
}

ComPtr<IDWriteTextFormat> create_format(IDWriteFactory* dwrite, const TextStyle& style)
{
    ComPtr<IDWriteTextFormat> fmt;
    ::ui::detail::count(::ui::detail::PerfCounter::TextFormatsCreated);
    if (FAILED(dwrite->CreateTextFormat(widen(style.font_family).c_str(),
                                        nullptr,
                                        static_cast<DWRITE_FONT_WEIGHT>(style.weight),
                                        style.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                                        DWRITE_FONT_STRETCH_NORMAL,
                                        style.font_size,
                                        L"",
                                        &fmt)))
    {
        return nullptr;
    }
    // Only a wrapping style wraps. Other boxes are sized to their measured text, and a sub-pixel
    // measure/draw difference (Metro's 14.667px) would wrap and clip a trailing word.
    switch (style.wrapping)
    {
        case TextWrapping::NoWrap:
            fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            break;
        case TextWrapping::Wrap:
            fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            break;
        case TextWrapping::WrapWholeWords:
            fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_WHOLE_WORD);
            break;
    }
    if (style.line_height > 0.0f)
    {
        // Metro puts every run on an explicit line box, baseline at 80% of it (DirectWrite's
        // uniform-spacing convention).
        fmt->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, style.line_height, style.line_height * 0.8f);
    }
    switch (style.horizontal)
    {
        case TextAlign::Leading:
            fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            break;
        case TextAlign::Center:
            fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            break;
        case TextAlign::Trailing:
            fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            break;
    }
    switch (style.vertical)
    {
        case TextAlign::Leading:
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            break;
        case TextAlign::Center:
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            break;
        case TextAlign::Trailing:
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_FAR);
            break;
    }
    return fmt;
}

// Under the lock.
ComPtr<IDWriteTextFormat> format_locked(Cache& c, IDWriteFactory* dwrite, const TextStyle& style)
{
    const KeyView key = format_key(style);
    if (auto it = c.formats.find(key); it != c.formats.end())
    {
        it->second.last_used = c.frame;
        return it->second.value;
    }
    ComPtr<IDWriteTextFormat> fmt = create_format(dwrite, style);
    if (fmt)
    {
        c.formats.emplace(own(key), Entry<ComPtr<IDWriteTextFormat>>{fmt, c.frame});
    }
    return fmt;
}

} // namespace

ComPtr<IDWriteTextFormat> cached_text_format(IDWriteFactory* dwrite, const TextStyle& style)
{
    Cache&                c = cache();
    const std::lock_guard lock{c.mutex};
    return format_locked(c, dwrite, style);
}

ComPtr<IDWriteTextLayout> cached_text_layout(
    IDWriteFactory*  dwrite,
    std::string_view text,
    const TextStyle& style,
    float            max_width,
    float            max_height)
{
    KeyView key     = format_key(style);
    key.text        = text;
    key.max_width   = max_width;
    key.max_height  = max_height;
    key.spacing     = style.character_spacing;
    key.decorations = static_cast<std::uint8_t>((style.underline ? 1 : 0) | (style.strikethrough ? 2 : 0));

    Cache&                c = cache();
    const std::lock_guard lock{c.mutex};
    if (auto it = c.layouts.find(key); it != c.layouts.end())
    {
        it->second.last_used = c.frame;
        return it->second.value;
    }
    const ComPtr<IDWriteTextFormat> fmt = format_locked(c, dwrite, style);
    if (!fmt)
    {
        return nullptr;
    }
    const std::wstring        wide = widen(text);
    ComPtr<IDWriteTextLayout> layout;
    ::ui::detail::count(::ui::detail::PerfCounter::TextLayoutsCreated);
    if (FAILED(dwrite->CreateTextLayout(wide.c_str(), static_cast<UINT32>(wide.size()), fmt.Get(), max_width, max_height, &layout)))
    {
        return nullptr;
    }
    apply_run_format(layout.Get(), style, static_cast<UINT32>(wide.size()));
    c.layouts.emplace(own(key), Entry<ComPtr<IDWriteTextLayout>>{layout, c.frame});
    return layout;
}

void next_text_frame()
{
    Cache&                c = cache();
    const std::lock_guard lock{c.mutex};
    ++c.frame;
    age_out(c.layouts, c.frame);
    age_out(c.metrics, c.frame);
}


namespace
{
KeyView metrics_key(std::string_view text, const TextStyle& style, float max_width)
{
    KeyView key    = format_key(style);
    key.text       = text;
    key.max_width  = max_width;
    key.spacing    = style.character_spacing;
    key.horizontal = 0; // alignment moves lines inside the box, not the box's size
    key.vertical   = 0;
    return key;
}
} // namespace

bool find_text_metrics(std::string_view text, const TextStyle& style, float max_width, TextMetrics& out)
{
    Cache&                c = cache();
    const std::lock_guard lock{c.mutex};
    if (auto it = c.metrics.find(metrics_key(text, style, max_width)); it != c.metrics.end())
    {
        it->second.last_used = c.frame;
        out                  = it->second.value;
        return true;
    }
    return false;
}

void store_text_metrics(std::string_view text, const TextStyle& style, float max_width, const TextMetrics& metrics)
{
    Cache&                c = cache();
    const std::lock_guard lock{c.mutex};
    c.metrics.insert_or_assign(own(metrics_key(text, style, max_width)), Entry<TextMetrics>{metrics, c.frame});
}


} // namespace ui::win32

#endif // _WIN32
