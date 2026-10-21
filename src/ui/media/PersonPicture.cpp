// SPDX-License-Identifier: MIT
#include "winui/ui/media/PersonPicture.hpp"

#include "../../core/StringsDetail.hpp"
#include "../themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Strings.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/render/TextAnalyzer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

namespace
{

// ---- Initials ----

enum class CharacterType
{
    Other,
    Standard,
    Symbolic,
    Glyph,
};

struct CharacterRange
{
    char32_t      first;
    char32_t      last;
    CharacterType type;
};

// The script class of each character range, first match wins. Code points, so the
// supplementary-plane ranges match too, which they never could per UTF-16 unit.
constexpr CharacterRange CharacterRanges[] = {
    {0x0250, 0x02AF, CharacterType::Glyph},      {0x0600, 0x06FF, CharacterType::Glyph},
    {0x0750, 0x077F, CharacterType::Glyph},      {0x08A0, 0x08FF, CharacterType::Glyph},
    {0xFB50, 0xFDFF, CharacterType::Glyph},      {0xFE70, 0xFEFF, CharacterType::Glyph},
    {0x0900, 0x097F, CharacterType::Glyph},      {0xA8E0, 0xA8FF, CharacterType::Glyph},
    {0x0980, 0x09FF, CharacterType::Glyph},      {0x0A00, 0x0A7F, CharacterType::Glyph},
    {0x0A80, 0x0AFF, CharacterType::Glyph},      {0x0B00, 0x0B7F, CharacterType::Glyph},
    {0x0B80, 0x0BFF, CharacterType::Glyph},      {0x0C00, 0x0C7F, CharacterType::Glyph},
    {0x0C80, 0x0CFF, CharacterType::Glyph},      {0x0D00, 0x0D7F, CharacterType::Glyph},
    {0x0D80, 0x0DFF, CharacterType::Glyph},      {0x0E00, 0x0E7F, CharacterType::Glyph},
    {0x0E80, 0x0EFF, CharacterType::Glyph},      {0x4E00, 0x9FFF, CharacterType::Symbolic},
    {0x3400, 0x4DBF, CharacterType::Symbolic},   {0x20000, 0x2A6DF, CharacterType::Symbolic},
    {0x2A700, 0x2B73F, CharacterType::Symbolic}, {0x2B740, 0x2B81F, CharacterType::Symbolic},
    {0x2E80, 0x2EFF, CharacterType::Symbolic},   {0x3000, 0x303F, CharacterType::Symbolic},
    {0x31C0, 0x31EF, CharacterType::Symbolic},   {0x3200, 0x32FF, CharacterType::Symbolic},
    {0x3300, 0x33FF, CharacterType::Symbolic},   {0xF900, 0xFAFF, CharacterType::Symbolic},
    {0xFE30, 0xFE4F, CharacterType::Symbolic},   {0x2F800, 0x2FA1F, CharacterType::Symbolic},
    {0x0370, 0x03FF, CharacterType::Symbolic},   {0x0590, 0x05FF, CharacterType::Symbolic},
    {0x0530, 0x058F, CharacterType::Symbolic},   {0x0001, 0x007F, CharacterType::Standard},
    {0x0080, 0x00FF, CharacterType::Standard},   {0x0100, 0x017F, CharacterType::Standard},
    {0x0180, 0x024F, CharacterType::Standard},   {0x2C60, 0x2C7F, CharacterType::Standard},
    {0xA720, 0xA7FF, CharacterType::Standard},   {0xAB30, 0xAB6F, CharacterType::Standard},
    {0x1E00, 0x1EFF, CharacterType::Standard},   {0x0400, 0x04FF, CharacterType::Standard},
    {0x0500, 0x052F, CharacterType::Standard},   {0x0300, 0x036F, CharacterType::Standard},
};

CharacterType character_type(char32_t character)
{
    for (const CharacterRange& range : CharacterRanges)
    {
        if (character >= range.first && character <= range.last)
        {
            return range.type;
        }
    }
    return CharacterType::Other;
}

// A name's script class from its first three characters (stopping at a NUL or a byte-order
// mark), Glyph beating Symbolic beating Standard.
CharacterType character_type(std::string_view text)
{
    CharacterType result = CharacterType::Other;
    std::size_t   count  = 0;
    for (const char32_t cp : code_points(text))
    {
        if (count++ == 3 || cp == 0 || cp == 0xFEFF)
        {
            break;
        }
        switch (character_type(cp))
        {
            case CharacterType::Glyph:
                result = CharacterType::Glyph;
                break;
            case CharacterType::Symbolic:
                if (result != CharacterType::Glyph)
                {
                    result = CharacterType::Symbolic;
                }
                break;
            case CharacterType::Standard:
                if (result != CharacterType::Glyph && result != CharacterType::Symbolic)
                {
                    result = CharacterType::Standard;
                }
                break;
            case CharacterType::Other:
                break;
        }
    }
    return result;
}

// A word's first full character: skip leading ASCII punctuation (all of the word, if that is all there
// is, keeps it), then take one character and the combining diacritics after it.
std::string first_full_character(const std::string& word)
{
    std::size_t start = 0;
    while (start < word.size())
    {
        const char c = word[start];
        if ((c >= 0x0021 && c <= 0x002F) || (c >= 0x003A && c <= 0x0040) || (c >= 0x007B && c <= 0x007E))
        {
            ++start;
            continue;
        }
        break;
    }
    if (start >= word.size())
    {
        start = 0;
    }
    std::size_t end = next_code_point(word, start);
    while (end < word.size())
    {
        const char32_t cp = code_point_at(word, end);
        if (cp < 0x0300 || cp > 0x036F)
        {
            break;
        }
        end = next_code_point(word, end);
    }
    return word.substr(start, end - start);
}

// Drops a trailing bracketed part: "Ada Lovelace (Work)" is Ada Lovelace.
void strip_trailing_brackets(std::string& text)
{
    static constexpr std::string_view Delimiters[] = {"{}", "()", "[]"};
    if (text.empty())
    {
        return;
    }
    for (std::string_view delimiter : Delimiters)
    {
        if (text.back() != delimiter[1])
        {
            continue;
        }
        const std::size_t start = text.find_last_of(delimiter[0]);
        if (start == std::string::npos)
        {
            continue;
        }
        text.erase(start);
        return;
    }
}

std::vector<std::string> split_words(const std::string& text)
{
    std::vector<std::string> words;
    std::string              current;
    for (const char c : text)
    {
        if (c == ' ')
        {
            if (!current.empty())
            {
                words.push_back(std::move(current));
                current.clear();
            }
        }
        else
        {
            current.push_back(c);
        }
    }
    if (!current.empty())
    {
        words.push_back(std::move(current));
    }
    return words;
}

std::string to_upper(const std::string& text)
{
    return text_analyzer().to_upper(text);
}

// The badge's fill and text: the accent fill under on-accent text where the era has the dedicated
// tokens, the accent button's colors elsewhere.
Color badge_fill(const Theme& theme)
{
    return detail::controls(theme).person_picture_dedicated_fill ? theme.palette.accent_fill_default : theme.colors().accent;
}
Color badge_text_color(const Theme& theme)
{
    return detail::controls(theme).person_picture_dedicated_fill
               ? theme.palette.text_on_accent_primary
               : theme.colors().text_on_accent;
}

} // namespace

std::string PersonPictureControl::initials_from_display_name(std::string_view display_name)
{
    if (character_type(display_name) != CharacterType::Standard)
    {
        return {};
    }
    std::string name{display_name};
    strip_trailing_brackets(name);
    const std::vector<std::string> words = split_words(name);
    if (words.empty())
    {
        return {};
    }
    std::string result = first_full_character(words.front());
    if (words.size() > 1)
    {
        result += first_full_character(words.back());
    }
    return to_upper(result);
}

std::string PersonPictureControl::effective_initials() const
{
    return !initials_.empty() ? initials_ : initials_from_display_name(display_name_);
}

float PersonPictureControl::effective_diameter() const
{
    // Keep the circle by applying one side to both, the smaller when both
    // changed, else the one that did.
    const std::optional<float> w = width();
    const std::optional<float> h = height();
    if (w && h)
    {
        return std::max(1.0f, std::min(*w, *h));
    }
    if (w)
    {
        return std::max(1.0f, *w);
    }
    if (h)
    {
        return std::max(1.0f, *h);
    }
    return diameter_;
}

std::string PersonPictureControl::badge_content() const
{
    if (badge_number_ > 0)
    {
        return badge_number_ <= 99 ? std::to_string(badge_number_) : std::string{"99+"};
    }
    return {};
}

Rect PersonPictureControl::badge_bounds() const
{
    const float side  = std::min(bounds().width, bounds().height);
    const float badge = side * 0.5f;
    // Wide enough for the number at 60% of the badge's height, with a quarter of the height free at
    // each end; never narrower than a circle.
    const float width = badge_number_ > 0 ? std::max(badge, badge_text_ratio_ * badge * 0.6f + badge * 0.5f) : badge;
    // Right/top aligned in the picture, moved 4 DIPs out.
    const float right = bounds().left() + (bounds().width + side) * 0.5f;
    const float top   = bounds().top() + (bounds().height - side) * 0.5f;
    return Rect{right - width + 4.0f, top - 4.0f, width, badge};
}

std::string PersonPictureControl::automation_name_fallback() const
{
    std::string                       name;
    const Strings::PersonPictureText& t = strings().person_picture;
    if (is_group_)
    {
        name = t.group;
    }
    else if (!display_name_.empty())
    {
        name = display_name_;
    }
    else if (!initials_.empty())
    {
        name = initials_;
    }
    else
    {
        name = t.person;
    }

    if (badge_number_ > 0)
    {
        const std::string badge = !badge_text_.empty() ? std::format("{} {}", badge_number_, badge_text_)
                                  : t.badge            ? t.badge(static_cast<std::size_t>(badge_number_))
                                                       : std::to_string(badge_number_);
        return detail::fill(t.with_badge, {name, badge});
    }
    if (!badge_icon_.empty())
    {
        return detail::fill(t.with_badge, {name, badge_text_.empty() ? t.icon : badge_text_});
    }
    return name;
}

Size PersonPictureControl::measure_override(Size available, const MeasureContext& context) const
{
    if (badge_number_ > 0)
    {
        TextStyle style;
        style.font_size   = 100.0f;
        style.weight      = FontWeight::SemiBold;
        badge_text_ratio_ = context.measurer.measure(badge_content(), style).width / style.font_size;
    }
    const float d = effective_diameter();
    Size        desired{d, d};
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    // Stay circular even if one axis got clamped tighter.
    const float side = std::min(desired.width, desired.height);
    return Size{side, side};
}

void PersonPictureControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    const Point  center{bounds().left() + bounds().width * 0.5f, bounds().top() + bounds().height * 0.5f};
    const float  radius = std::min(bounds().width, bounds().height) * 0.5f;
    if (radius <= 0.0f)
    {
        return;
    }

    // The ellipse's dedicated fill and stroke are tokens only the Fluent palettes carry, so other
    // eras keep the `button_normal` look.
    const Color fill_color   = detail::controls(theme).person_picture_dedicated_fill
                                   ? theme.palette.control_alt_fill_quaternary
                                   : theme.colors().control_fill;
    const Color stroke_color = detail::controls(theme).person_picture_dedicated_fill ? theme.palette.card_stroke_default
                                                                                     : theme.colors().control_stroke;
    renderer.fill_ellipse(center, radius, radius, fill_color);
    const float stroke = theme.border_width;
    renderer.stroke_ellipse(center, radius - stroke * 0.5f, radius - stroke * 0.5f, stroke_color, stroke);

    // The text is 42% of the width, shared by the initials and both glyphs.
    const float font_size = std::max(1.0f, radius * 2.0f * 0.42f);

    if (is_group_)
    {
        draw_icon(renderer, glyphs::People, bounds(), theme.colors().text, font_size, theme.icon_font_family);
    }
    else if (!profile_picture_.empty())
    {
        renderer.push_clip_ellipse(center, radius, radius);
        renderer.draw_image(profile_picture_, bounds(), Stretch::UniformToFill);
        renderer.pop_clip();
    }
    else if (const std::string initials = effective_initials(); !initials.empty())
    {
        TextStyle style;
        style.font_size = font_size;
        style.weight    = FontWeight::SemiBold;
        renderer.draw_text(initials, bounds(), theme.colors().text, style);
    }
    else
    {
        draw_icon(renderer, glyphs::Contact, bounds(), theme.colors().text, font_size, theme.icon_font_family);
    }

    if (!has_badge())
    {
        return;
    }
    // The badge: an accent circle (its 2px stroke is transparent, so not drawn), then the number
    // or the icon in it at 60% of its size.
    const Rect  badge = badge_bounds();
    const Point badge_center{badge.left() + badge.width * 0.5f, badge.top() + badge.height * 0.5f};
    const float badge_radius = badge.height * 0.5f;
    if (badge.width > badge.height)
    {
        renderer.fill_rounded_rect(badge, badge_radius, badge_fill(theme));
    }
    else
    {
        renderer.fill_ellipse(badge_center, badge_radius, badge_radius, badge_fill(theme));
    }
    const float badge_font = std::max(1.0f, badge.height * 0.6f);
    if (badge_number_ <= 0)
    {
        badge_icon_.draw_in_slot(renderer, badge, badge_text_color(theme), badge_font, theme.icon_font_family);
        return;
    }
    TextStyle style;
    style.font_size = badge_font;
    style.weight    = FontWeight::SemiBold;
    renderer.draw_text(badge_content(), badge, badge_text_color(theme), style);
}

std::unique_ptr<ui::Element> PersonPicture::build() &&
{
    auto node = std::make_unique<ui::PersonPictureControl>();

    apply(diameter, *node, [raw = node.get()](const float& v) { raw->set_diameter(v); });
    apply(display_name, *node, [raw = node.get()](const std::string& v) { raw->set_display_name(v); });
    apply(initials, *node, [raw = node.get()](const std::string& v) { raw->set_initials(v); });
    apply(group, *node, [raw = node.get()](const bool& v) { raw->set_group(v); });
    apply(profile_picture, *node, [raw = node.get()](const ui::Bitmap& v) { raw->set_profile_picture(v); });
    apply(badge_number, *node, [raw = node.get()](const int& v) { raw->set_badge_number(v); });
    apply(badge_icon, *node, [raw = node.get()](const IconSource& v) { raw->set_badge_icon(v); });
    apply(badge_text, *node, [raw = node.get()](const std::string& v) { raw->set_badge_text(v); });

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
