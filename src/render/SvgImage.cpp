// SPDX-License-Identifier: MIT
#include "winui/render/SvgImage.hpp"

#include "winui/core/Geometry.hpp"
#include "winui/core/Utf8Path.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <cstddef>

namespace ui
{

namespace
{

bool is_xml_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// The root <svg ...> start tag, attributes and all, or empty. Comments, the XML declaration and a
// DOCTYPE may come first; the first element named `svg` (any namespace prefix) is the root.
std::string_view root_tag(std::string_view markup)
{
    std::size_t pos = 0;
    while ((pos = markup.find('<', pos)) != std::string_view::npos)
    {
        if (markup.substr(pos, 4) == "<!--")
        {
            const std::size_t end = markup.find("-->", pos + 4);
            if (end == std::string_view::npos)
            {
                return {};
            }
            pos = end + 3;
            continue;
        }
        if (pos + 1 < markup.size() && (markup[pos + 1] == '?' || markup[pos + 1] == '!'))
        {
            pos = markup.find('>', pos);
            if (pos == std::string_view::npos)
            {
                return {};
            }
            continue;
        }
        std::size_t name_end = pos + 1;
        while (name_end < markup.size() && !is_xml_space(markup[name_end]) && markup[name_end] != '>' &&
               markup[name_end] != '/')
        {
            ++name_end;
        }
        std::string_view name = markup.substr(pos + 1, name_end - pos - 1);
        if (const std::size_t colon = name.find(':'); colon != std::string_view::npos)
        {
            name = name.substr(colon + 1);
        }
        const std::size_t close = markup.find('>', name_end);
        if (close == std::string_view::npos)
        {
            return {};
        }
        if (name == "svg")
        {
            return markup.substr(name_end, close - name_end);
        }
        return {}; // the first element is the root; anything else is not an SVG document
    }
    return {};
}

std::optional<std::string_view> attribute(std::string_view tag, std::string_view name)
{
    std::size_t pos = 0;
    while (pos < tag.size())
    {
        while (pos < tag.size() && is_xml_space(tag[pos]))
        {
            ++pos;
        }
        const std::size_t name_start = pos;
        while (pos < tag.size() && !is_xml_space(tag[pos]) && tag[pos] != '=')
        {
            ++pos;
        }
        const std::string_view attr = tag.substr(name_start, pos - name_start);
        while (pos < tag.size() && is_xml_space(tag[pos]))
        {
            ++pos;
        }
        if (pos >= tag.size() || tag[pos] != '=')
        {
            if (pos == name_start)
            {
                ++pos;
            }
            continue;
        }
        ++pos;
        while (pos < tag.size() && is_xml_space(tag[pos]))
        {
            ++pos;
        }
        if (pos >= tag.size() || (tag[pos] != '"' && tag[pos] != '\''))
        {
            continue;
        }
        const char        quote = tag[pos++];
        const std::size_t end   = tag.find(quote, pos);
        if (end == std::string_view::npos)
        {
            return std::nullopt;
        }
        if (attr == name)
        {
            return tag.substr(pos, end - pos);
        }
        pos = end + 1;
    }
    return std::nullopt;
}

std::optional<float> parse_number(std::string_view& text)
{
    while (!text.empty() && (is_xml_space(text.front()) || text.front() == ','))
    {
        text.remove_prefix(1);
    }
    if (text.empty())
    {
        return std::nullopt;
    }
    // std::from_chars rejects a leading '+'; SVG allows it.
    if (text.front() == '+')
    {
        text.remove_prefix(1);
    }
    float value          = 0.0f;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{})
    {
        return std::nullopt;
    }
    text.remove_prefix(static_cast<std::size_t>(end - text.data()));
    return value;
}

// A length in DIPs, or nullopt for a missing, relative or unreadable one.
std::optional<float> parse_length(std::optional<std::string_view> attr)
{
    if (!attr)
    {
        return std::nullopt;
    }
    std::string_view     text  = *attr;
    std::optional<float> value = parse_number(text);
    if (!value || *value <= 0.0f)
    {
        return std::nullopt;
    }
    while (!text.empty() && is_xml_space(text.back()))
    {
        text.remove_suffix(1);
    }
    if (text.empty() || text == "px")
    {
        return value;
    }
    if (text == "pt")
    {
        return *value * 96.0f / 72.0f;
    }
    if (text == "pc")
    {
        return *value * 16.0f;
    }
    if (text == "in")
    {
        return *value * 96.0f;
    }
    if (text == "cm")
    {
        return *value * 96.0f / 2.54f;
    }
    if (text == "mm")
    {
        return *value * 96.0f / 25.4f;
    }
    return std::nullopt; // %, em, ex, ...: relative to something a standalone image does not have
}

} // namespace

Size svg_natural_size(std::string_view markup)
{
    const std::string_view tag = root_tag(markup);
    if (tag.empty())
    {
        return {};
    }
    std::optional<float> width  = parse_length(attribute(tag, "width"));
    std::optional<float> height = parse_length(attribute(tag, "height"));

    std::optional<Size> view_box;
    if (std::optional<std::string_view> box = attribute(tag, "viewBox"))
    {
        std::string_view     text = *box;
        std::optional<float> x    = parse_number(text);
        std::optional<float> y    = parse_number(text);
        std::optional<float> w    = parse_number(text);
        std::optional<float> h    = parse_number(text);
        if (x && y && w && h && *w > 0.0f && *h > 0.0f)
        {
            view_box = Size{*w, *h};
        }
    }

    if (width && height)
    {
        return Size{*width, *height};
    }
    if (view_box)
    {
        if (width)
        {
            return Size{*width, *width * view_box->height / view_box->width};
        }
        if (height)
        {
            return Size{*height * view_box->width / view_box->height, *height};
        }
        return *view_box;
    }
    return {};
}

SvgImage::SvgImage(std::string markup) :
    markup_(std::make_shared<const std::string>(std::move(markup))),
    natural_size_(svg_natural_size(*markup_))
{
}

SvgImage SvgImage::read(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("svg: cannot open " + path_to_utf8(path));
    }
    std::string markup{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    // A UTF-8 byte-order mark is not part of the document.
    if (markup.starts_with("\xEF\xBB\xBF"))
    {
        markup.erase(0, 3);
    }
    return SvgImage{std::move(markup)};
}

} // namespace ui
