// SPDX-License-Identifier: MIT
#include "winui/render/TextAnalyzer.hpp"

#include "winui/core/Utf8.hpp"

#include <algorithm>
#include <atomic>
#include <string>
#include <string_view>
#include <utility>

#include <cstddef>

namespace ui
{

namespace
{

constexpr char32_t ZeroWidthJoiner = 0x200D;

// What joins onto the previous code point without starting a cluster (roughly UAX #29's Extend):
// combining marks, ZWNJ, variation selectors, skin-tone modifiers, tag characters. UI blocks only.
bool is_extend(char32_t cp)
{
    return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x0483 && cp <= 0x0489) || (cp >= 0x0591 && cp <= 0x05BD) ||
           (cp >= 0x0610 && cp <= 0x061A) || (cp >= 0x064B && cp <= 0x065F) || cp == 0x0670 ||
           (cp >= 0x0900 && cp <= 0x0903) || (cp >= 0x093A && cp <= 0x094F) || (cp >= 0x0E31 && cp <= 0x0E3A) ||
           (cp >= 0x0E47 && cp <= 0x0E4E) || (cp >= 0x1AB0 && cp <= 0x1AFF) || (cp >= 0x1DC0 && cp <= 0x1DFF) ||
           cp == 0x200C || (cp >= 0x20D0 && cp <= 0x20FF) || (cp >= 0xFE00 && cp <= 0xFE0F) ||
           (cp >= 0xFE20 && cp <= 0xFE2F) || (cp >= 0x1F3FB && cp <= 0x1F3FF) || (cp >= 0xE0020 && cp <= 0xE007F) ||
           (cp >= 0xE0100 && cp <= 0xE01EF);
}

bool is_regional_indicator(char32_t cp)
{
    return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

// What a ZWJ can join: the emoji blocks (UAX #29's Extended_Pictographic, roughly).
bool is_pictographic(char32_t cp)
{
    return (cp >= 0x2190 && cp <= 0x21FF) || (cp >= 0x2300 && cp <= 0x23FF) || (cp >= 0x2600 && cp <= 0x27BF) ||
           (cp >= 0x2B00 && cp <= 0x2BFF) || (cp >= 0x1F000 && cp <= 0x1FAFF);
}

bool is_control(char32_t cp)
{
    return cp < 0x20 || cp == 0x7F;
}

// Simple one-to-one case pairs: ASCII, Latin-1, Latin Extended-A, Greek and Cyrillic.
char32_t upper(char32_t cp)
{
    if (cp >= 'a' && cp <= 'z')
    {
        return cp - 0x20;
    }
    if (cp < 0x80)
    {
        return cp;
    }
    if ((cp >= 0xE0 && cp <= 0xFE && cp != 0xF7))
    {
        return cp - 0x20;
    }
    if (cp == 0xFF)
    {
        return 0x178;
    }
    if (cp >= 0x100 && cp <= 0x17F && cp != 0x130 && cp != 0x131 && cp != 0x138 && cp != 0x149 && cp != 0x17F)
    {
        // Latin Extended-A pairs upper/lower at even/odd, except the stretch 0x139..0x148 and
        // 0x179..0x17E, which pair odd/even.
        const bool odd_pairs = (cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E);
        const bool is_lower  = odd_pairs ? (cp % 2 == 0) : (cp % 2 == 1);
        return is_lower ? cp - 1 : cp;
    }
    if (cp >= 0x3B1 && cp <= 0x3C9 && cp != 0x3C2)
    {
        return cp - 0x20;
    }
    if (cp == 0x3C2)
    {
        return 0x3A3; // final sigma
    }
    if (cp >= 0x430 && cp <= 0x44F)
    {
        return cp - 0x20;
    }
    if (cp >= 0x450 && cp <= 0x45F)
    {
        return cp - 0x50;
    }
    return cp;
}

char32_t lower(char32_t cp)
{
    if (cp >= 'A' && cp <= 'Z')
    {
        return cp + 0x20;
    }
    if (cp < 0x80)
    {
        return cp;
    }
    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7)
    {
        return cp + 0x20;
    }
    if (cp == 0x178)
    {
        return 0xFF;
    }
    if (cp >= 0x100 && cp <= 0x17F && cp != 0x130 && cp != 0x131 && cp != 0x138 && cp != 0x149 && cp != 0x17F)
    {
        const bool odd_pairs = (cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E);
        const bool is_upper  = odd_pairs ? (cp % 2 == 1) : (cp % 2 == 0);
        return is_upper ? cp + 1 : cp;
    }
    if (cp >= 0x391 && cp <= 0x3A9 && cp != 0x3A2)
    {
        return cp + 0x20;
    }
    if (cp >= 0x410 && cp <= 0x42F)
    {
        return cp + 0x20;
    }
    if (cp >= 0x400 && cp <= 0x40F)
    {
        return cp + 0x50;
    }
    return cp;
}

template <typename Map>
std::string map_code_points(std::string_view text, Map map)
{
    std::string out;
    out.reserve(text.size());
    for (const char32_t cp : code_points(text))
    {
        append_utf8(out, map(cp));
    }
    return out;
}

const TextAnalyzer               g_builtin;
std::atomic<const TextAnalyzer*> g_installed{nullptr};

} // namespace

std::size_t TextAnalyzer::next_cluster(std::string_view text, std::size_t pos) const
{
    if (pos >= text.size())
    {
        return text.size();
    }
    const char32_t first = code_point_at(text, pos);
    std::size_t    i     = next_code_point(text, pos);
    if (first == '\r' && code_point_at(text, i) == '\n')
    {
        return i + 1;
    }
    if (is_control(first))
    {
        return i;
    }
    if (is_regional_indicator(first))
    {
        // A flag is a pair of regional indicators.
        return is_regional_indicator(code_point_at(text, i)) ? next_code_point(text, i) : i;
    }
    char32_t last = first;
    while (i < text.size())
    {
        const char32_t cp = code_point_at(text, i);
        if (is_extend(cp) || cp == ZeroWidthJoiner || (last == ZeroWidthJoiner && is_pictographic(cp)))
        {
            last = cp;
            i    = next_code_point(text, i);
            continue;
        }
        break;
    }
    return i;
}

std::size_t TextAnalyzer::previous_cluster(std::string_view text, std::size_t pos) const
{
    pos = std::min(pos, text.size());
    if (pos == 0)
    {
        return 0;
    }
    // Back up to a code point that certainly starts a cluster, then walk forward.
    std::size_t start = previous_code_point(text, pos);
    while (start > 0)
    {
        const char32_t cp     = code_point_at(text, start);
        const char32_t before = code_point_before(text, start);
        if (is_extend(cp) || cp == ZeroWidthJoiner || before == ZeroWidthJoiner || is_regional_indicator(cp) ||
            (cp == '\n' && before == '\r'))
        {
            start = previous_code_point(text, start);
            continue;
        }
        break;
    }
    std::size_t boundary = start;
    for (;;)
    {
        const std::size_t next = next_cluster(text, boundary);
        if (next >= pos)
        {
            return boundary;
        }
        boundary = next;
    }
}

bool TextAnalyzer::is_word_char(char32_t cp) const
{
    if (cp < 0x80)
    {
        return (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || cp == '_';
    }
    if (cp < 0x100)
    {
        // Latin-1: the letters, and the feminine/masculine ordinals and micro sign.
        return (cp >= 0xC0 && cp != 0xD7 && cp != 0xF7) || cp == 0xAA || cp == 0xB5 || cp == 0xBA;
    }
    if (is_extend(cp))
    {
        return true;
    }
    // Spaces, punctuation and symbols of the blocks that hold nothing else; everything else past
    // Latin-1 is taken for a letter, which it overwhelmingly is.
    const bool punctuation = (cp >= 0x2000 && cp <= 0x2BFF) || (cp >= 0x2E00 && cp <= 0x2E7F) ||
                             (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0xFE10 && cp <= 0xFE1F) ||
                             (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF0F) ||
                             (cp >= 0xFF1A && cp <= 0xFF20) || (cp >= 0xFF3B && cp <= 0xFF40) ||
                             (cp >= 0xFF5B && cp <= 0xFF65) || (cp >= 0xE000 && cp <= 0xF8FF) ||
                             (cp >= 0x1F000 && cp <= 0x1FAFF) || cp == 0xFEFF || cp == 0xFFFD;
    return !punctuation;
}

std::string TextAnalyzer::to_upper(std::string_view text) const
{
    return map_code_points(text, upper);
}

std::string TextAnalyzer::to_lower(std::string_view text) const
{
    return map_code_points(text, lower);
}

const TextAnalyzer& text_analyzer()
{
    const TextAnalyzer* installed = g_installed.load(std::memory_order_acquire);
    return installed != nullptr ? *installed : g_builtin;
}

void set_text_analyzer(const TextAnalyzer* analyzer)
{
    g_installed.store(analyzer, std::memory_order_release);
}

char32_t code_point_at(std::string_view text, std::size_t pos)
{
    return decode_utf8(text, pos).code_point;
}

char32_t code_point_before(std::string_view text, std::size_t pos)
{
    if (pos == 0)
    {
        return 0;
    }
    return decode_utf8(text, previous_code_point(text, pos)).code_point;
}

namespace
{
bool is_white_space(char32_t ch)
{
    return (ch >= 0x09 && ch <= 0x0D) || ch == ' ' || ch == 0x85 || ch == 0xA0 || ch == 0x1680 ||
           (ch >= 0x2000 && ch <= 0x200A) || ch == 0x2028 || ch == 0x2029 || ch == 0x202F || ch == 0x205F || ch == 0x3000;
}
} // namespace

std::size_t previous_word_start(std::string_view text, std::size_t pos)
{
    const TextAnalyzer& analyzer = text_analyzer();
    std::size_t         i        = std::min(pos, text.size());
    while (i > 0 && is_white_space(code_point_before(text, i)))
    {
        i = previous_code_point(text, i);
    }
    if (i > 0)
    {
        const bool word = analyzer.is_word_char(code_point_before(text, i));
        while (i > 0 && !is_white_space(code_point_before(text, i)) &&
               analyzer.is_word_char(code_point_before(text, i)) == word)
        {
            i = previous_code_point(text, i);
        }
    }
    return i;
}

std::size_t next_word_start(std::string_view text, std::size_t pos)
{
    const TextAnalyzer& analyzer = text_analyzer();
    std::size_t         i        = std::min(pos, text.size());
    if (i < text.size() && !is_white_space(code_point_at(text, i)))
    {
        const bool word = analyzer.is_word_char(code_point_at(text, i));
        while (i < text.size() && !is_white_space(code_point_at(text, i)) &&
               analyzer.is_word_char(code_point_at(text, i)) == word)
        {
            i = next_code_point(text, i);
        }
    }
    while (i < text.size() && is_white_space(code_point_at(text, i)))
    {
        i = next_code_point(text, i);
    }
    return i;
}

std::pair<std::size_t, std::size_t> word_run(std::string_view text, std::size_t pos, bool stop_at_line_break)
{
    if (text.empty())
    {
        return {0, 0};
    }
    const TextAnalyzer& analyzer = text_analyzer();
    const std::size_t   at = pos < text.size() ? floor_code_point(text, pos) : previous_code_point(text, text.size());
    const bool          is_word = analyzer.is_word_char(code_point_at(text, at));
    const auto          same    = [&](char32_t cp)
    { return !(stop_at_line_break && cp == '\n') && analyzer.is_word_char(cp) == is_word; };

    std::size_t start = at;
    while (start > 0 && same(code_point_before(text, start)))
    {
        start = previous_code_point(text, start);
    }
    std::size_t end = next_code_point(text, at);
    while (end < text.size() && same(code_point_at(text, end)))
    {
        end = next_code_point(text, end);
    }
    return {start, end};
}

} // namespace ui
