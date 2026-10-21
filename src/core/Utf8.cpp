// SPDX-License-Identifier: MIT
#include "winui/core/Utf8.hpp"

#include <algorithm>
#include <string>
#include <string_view>

#include <cstddef>
#include <cstdint>

namespace ui
{
namespace
{

bool is_surrogate(char32_t cp)
{
    return cp >= 0xD800 && cp <= 0xDFFF;
}

void append_utf16(std::u16string& out, char32_t cp)
{
    if (cp >= 0x10000)
    {
        cp -= 0x10000;
        out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
        out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        return;
    }
    out.push_back(static_cast<char16_t>(cp));
}

bool is_continuation(char byte)
{
    return (static_cast<std::uint8_t>(byte) & 0xC0) == 0x80;
}

} // namespace

Utf8Decoded decode_utf8(std::string_view text, std::size_t pos)
{
    if (pos >= text.size())
    {
        return {};
    }
    const auto lead = static_cast<std::uint8_t>(text[pos]);
    if (lead < 0x80)
    {
        return {lead, 1};
    }

    std::size_t length = 0;
    char32_t    cp     = 0;
    char32_t    min    = 0; // the smallest code point that needs `length` bytes: below it is overlong
    if ((lead & 0xE0) == 0xC0)
    {
        length = 2;
        cp     = lead & 0x1F;
        min    = 0x80;
    }
    else if ((lead & 0xF0) == 0xE0)
    {
        length = 3;
        cp     = lead & 0x0F;
        min    = 0x800;
    }
    else if ((lead & 0xF8) == 0xF0)
    {
        length = 4;
        cp     = lead & 0x07;
        min    = 0x10000;
    }
    else
    {
        return {ReplacementCharacter, 1}; // a stray continuation byte, or 0xF8..0xFF
    }

    std::size_t consumed = 1;
    while (consumed < length && pos + consumed < text.size() && is_continuation(text[pos + consumed]))
    {
        cp = (cp << 6) | (static_cast<std::uint8_t>(text[pos + consumed]) & 0x3F);
        ++consumed;
    }

    // A truncated sequence swallows only the bytes that belonged to it, so whatever follows
    // still decodes.
    if (consumed < length || cp < min || cp > 0x10FFFF || is_surrogate(cp))
    {
        return {ReplacementCharacter, consumed};
    }
    return {cp, consumed};
}

void append_utf8(std::string& out, char32_t cp)
{
    if (is_surrogate(cp) || cp > 0x10FFFF)
    {
        cp = ReplacementCharacter;
    }
    if (cp < 0x80)
    {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp < 0x800)
    {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp < 0x10000)
    {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string encode_utf8(char32_t code_point)
{
    std::string out;
    append_utf8(out, code_point);
    return out;
}

std::size_t next_code_point(std::string_view text, std::size_t pos)
{
    if (pos >= text.size())
    {
        return text.size();
    }
    return pos + decode_utf8(text, pos).length;
}

std::size_t previous_code_point(std::string_view text, std::size_t pos)
{
    if (pos == 0)
    {
        return 0;
    }
    pos = std::min(pos, text.size());
    // Walk back over at most three continuation bytes to a lead byte, and take it only when its
    // sequence really ends at `pos`. Otherwise the byte just before `pos` stands alone as U+FFFD.
    std::size_t start = pos - 1;
    for (int i = 0; i < 3 && start > 0 && is_continuation(text[start]); ++i)
    {
        --start;
    }
    if (start + decode_utf8(text, start).length == pos)
    {
        return start;
    }
    return pos - 1;
}

bool is_code_point_boundary(std::string_view text, std::size_t pos)
{
    if (pos == 0 || pos >= text.size())
    {
        return pos == 0 || pos == text.size();
    }
    if (!is_continuation(text[pos]))
    {
        return true;
    }
    // A continuation byte is a boundary only when it is stray: no lead byte before it whose
    // sequence reaches past it.
    std::size_t start = pos;
    for (int i = 0; i < 3 && start > 0 && is_continuation(text[start]); ++i)
    {
        --start;
    }
    return is_continuation(text[start]) || start + decode_utf8(text, start).length <= pos;
}

std::size_t floor_code_point(std::string_view text, std::size_t pos)
{
    if (pos >= text.size())
    {
        return text.size();
    }
    // Inside a sequence: back to its lead byte, at most three continuation bytes away.
    while (!is_code_point_boundary(text, pos))
    {
        --pos;
    }
    return pos;
}

std::size_t code_point_count(std::string_view text)
{
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size(); i = next_code_point(text, i))
    {
        ++count;
    }
    return count;
}

bool is_valid_utf8(std::string_view text)
{
    std::size_t i = 0;
    while (i < text.size())
    {
        const Utf8Decoded d = decode_utf8(text, i);
        if (d.code_point == ReplacementCharacter)
        {
            // U+FFFD itself is valid when it was spelled out (EF BF BD), not when it stands in.
            if (d.length != 3 || text.substr(i, 3) != "\xEF\xBF\xBD")
            {
                return false;
            }
        }
        i += d.length;
    }
    return true;
}

std::string sanitize_utf8(std::string_view text)
{
    if (is_valid_utf8(text))
    {
        return std::string{text};
    }
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size())
    {
        const Utf8Decoded d = decode_utf8(text, i);
        append_utf8(out, d.code_point);
        i += d.length;
    }
    return out;
}

std::u16string to_utf16(std::string_view text)
{
    std::u16string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size())
    {
        const Utf8Decoded d = decode_utf8(text, i);
        append_utf16(out, d.code_point);
        i += d.length;
    }
    return out;
}

std::string from_utf16(std::u16string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        char32_t cp = text[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size())
        {
            const char32_t low = text[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF)
            {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        append_utf8(out, cp); // an unpaired surrogate becomes U+FFFD in there
    }
    return out;
}

std::size_t utf16_length(std::string_view text)
{
    std::size_t units = 0;
    for (std::size_t i = 0; i < text.size();)
    {
        const Utf8Decoded d = decode_utf8(text, i);
        units += d.code_point >= 0x10000 ? 2 : 1;
        i += d.length;
    }
    return units;
}

std::size_t utf8_offset_from_utf16(std::string_view text, std::size_t units)
{
    std::size_t i = 0;
    while (i < text.size())
    {
        const Utf8Decoded d    = decode_utf8(text, i);
        const std::size_t size = d.code_point >= 0x10000 ? 2 : 1;
        if (units < size)
        {
            break;
        }
        units -= size;
        i += d.length;
    }
    return i;
}

std::string Utf16Decoder::feed(char16_t unit)
{
    std::string out;
    if (unit >= 0xD800 && unit <= 0xDBFF)
    {
        if (high_ != 0)
        {
            append_utf8(out, ReplacementCharacter);
        }
        high_ = unit;
        return out;
    }
    if (unit >= 0xDC00 && unit <= 0xDFFF && high_ != 0)
    {
        append_utf8(out, 0x10000 + ((static_cast<char32_t>(high_) - 0xD800) << 10) + (unit - 0xDC00));
        high_ = 0;
        return out;
    }
    if (high_ != 0)
    {
        append_utf8(out, ReplacementCharacter);
        high_ = 0;
    }
    append_utf8(out, unit); // a lone low surrogate becomes U+FFFD in there
    return out;
}

} // namespace ui
