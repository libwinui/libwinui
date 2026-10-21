// SPDX-License-Identifier: MIT
#include "Rtf.hpp"

#include "winui/core/Color.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/ui/text/RichEditBox.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace ui::detail
{

namespace
{

// A point is 4/3 of a DIP and \fs counts half-points, so a DIP is 1.5 half-points. Multiplied
// and divided in that order, whole sizes round-trip exactly.
constexpr int DefaultHalfPoints = 24; // RTF's default size, 12pt

// Code page 1252's 0x80-0x9F; the rest of the page is Latin-1.
constexpr std::array<char32_t, 32> Cp1252High{
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

char32_t from_cp1252(unsigned char c)
{
    return c >= 0x80 && c <= 0x9F ? Cp1252High[c - 0x80] : static_cast<char32_t>(c);
}

constexpr std::array<std::string_view, 40> SkippedDestinations{
    "stylesheet",   "info",       "pict",      "object",
    "header",       "headerl",    "headerr",   "headerf",
    "footer",       "footerl",    "footerr",   "footerf",
    "footnote",     "annotation", "listtable", "listoverridetable",
    "rsidtbl",      "generator",  "themedata", "colorschememapping",
    "latentstyles", "datastore",  "xmlnstbl",  "fldinst",
    "pntext",       "pntxta",     "pntxtb",    "revtbl",
    "filetbl",      "nonshppict", "shp",       "mmathPr",
    "xe",           "tc",         "txe",       "userprops",
    "docvar",       "template",   "private",   "pgdsctbl",
};

enum class Destination
{
    Text,
    FontTable,
    ColorTable,
};

struct State
{
    Destination dest   = Destination::Text;
    bool        bold   = false;
    bool        italic = false;
    bool        under  = false;
    bool        strike = false;
    int         fg     = 0; // color table index, 0 the automatic color
    int         bg     = 0;
    int         font   = -1; // -1: \deff
    int         size   = DefaultHalfPoints;
    int         uc     = 1;
};

class Parser
{
public:
    explicit Parser(std::string_view rtf) : in_(rtf)
    {
    }

    std::vector<RichTextSpan> run()
    {
        while (pos_ < in_.size())
        {
            const char c = in_[pos_];
            if (c == '{')
            {
                ++pos_;
                stack_.push_back(state_);
                skip_fallback_ = 0;
            }
            else if (c == '}')
            {
                ++pos_;
                close_group();
            }
            else if (c == '\\')
            {
                ++pos_;
                control();
            }
            else if (c == '\r' || c == '\n')
            {
                ++pos_;
            }
            else
            {
                ++pos_;
                character(from_cp1252(static_cast<unsigned char>(c)));
            }
        }
        if (!out_.empty() && !out_.back().text.empty() && out_.back().text.back() == '\n')
        {
            out_.back().text.pop_back();
            if (out_.back().text.empty())
            {
                out_.pop_back();
            }
        }
        return std::move(out_);
    }

private:
    void close_group()
    {
        if (state_.dest == Destination::FontTable && !font_name_.empty())
        {
            finish_font();
        }
        if (stack_.empty())
        {
            return;
        }
        state_ = stack_.back();
        stack_.pop_back();
        skip_fallback_ = 0;
    }

    void skip_group()
    {
        int depth = 1;
        while (pos_ < in_.size() && depth > 0)
        {
            const char c = in_[pos_++];
            if (c == '\\')
            {
                if (in_.compare(pos_, 3, "bin") == 0)
                {
                    std::size_t p = pos_ + 3;
                    long        n = 0;
                    while (p < in_.size() && in_[p] >= '0' && in_[p] <= '9')
                    {
                        n = n * 10 + (in_[p++] - '0');
                    }
                    if (p < in_.size() && in_[p] == ' ')
                    {
                        ++p;
                    }
                    pos_ = std::min(in_.size(), p + static_cast<std::size_t>(n));
                }
                else if (pos_ < in_.size())
                {
                    ++pos_; // an escaped brace does not count
                }
            }
            else if (c == '{')
            {
                ++depth;
            }
            else if (c == '}')
            {
                --depth;
            }
        }
        if (!stack_.empty())
        {
            state_ = stack_.back();
            stack_.pop_back();
        }
        skip_fallback_ = 0;
    }

    void control()
    {
        if (pos_ >= in_.size())
        {
            return;
        }
        const char c = in_[pos_];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
        {
            ++pos_;
            symbol(c);
            return;
        }
        const std::size_t start = pos_;
        while (pos_ < in_.size() && ((in_[pos_] >= 'a' && in_[pos_] <= 'z') || (in_[pos_] >= 'A' && in_[pos_] <= 'Z')))
        {
            ++pos_;
        }
        const std::string_view word = in_.substr(start, pos_ - start);
        std::optional<int>     param;
        if (pos_ < in_.size() && (in_[pos_] == '-' || (in_[pos_] >= '0' && in_[pos_] <= '9')))
        {
            const bool negative = in_[pos_] == '-';
            if (negative)
            {
                ++pos_;
            }
            long value = 0;
            while (pos_ < in_.size() && in_[pos_] >= '0' && in_[pos_] <= '9')
            {
                value = std::min(value * 10 + (in_[pos_++] - '0'), 1L << 30);
            }
            param = static_cast<int>(negative ? -value : value);
        }
        if (pos_ < in_.size() && in_[pos_] == ' ')
        {
            ++pos_; // the delimiter belongs to the word
        }
        control_word(word, param);
    }

    void symbol(char c)
    {
        switch (c)
        {
            case '\\':
            case '{':
            case '}':
                character(static_cast<char32_t>(c));
                break;
            case '~':
                character(U'\x00A0');
                break;
            case '_':
                character(U'\x2011');
                break;
            case '\'':
            {
                if (pos_ + 2 <= in_.size())
                {
                    const std::string hex(in_.substr(pos_, 2));
                    pos_ += 2;
                    const auto value = static_cast<unsigned char>(std::strtoul(hex.c_str(), nullptr, 16));
                    if (skip_fallback_ > 0)
                    {
                        --skip_fallback_;
                        return;
                    }
                    character(from_cp1252(value));
                }
                break;
            }
            case '*':
                skip_group();
                break;
            case '\r':
            case '\n':
                character('\n'); // an escaped line end is \par
                break;
            default:
                break; // \- (an optional hyphen), \: and the rest draw nothing
        }
    }

    void control_word(std::string_view word, std::optional<int> param)
    {
        skip_fallback_ = 0;
        const int  n   = param.value_or(1);
        const bool on  = n != 0;

        if (std::find(SkippedDestinations.begin(), SkippedDestinations.end(), word) != SkippedDestinations.end())
        {
            skip_group();
            return;
        }
        if (word == "fonttbl")
        {
            state_.dest = Destination::FontTable;
        }
        else if (word == "colortbl")
        {
            state_.dest    = Destination::ColorTable;
            color_pending_ = false;
        }
        else if (word == "deff")
        {
            default_font_ = n;
        }
        else if (word == "f")
        {
            if (state_.dest == Destination::FontTable)
            {
                font_number_ = n;
            }
            else
            {
                state_.font = n;
            }
        }
        else if (word == "red" || word == "green" || word == "blue")
        {
            const auto v = static_cast<std::uint8_t>(std::clamp(n, 0, 255));
            (word == "red" ? red_ : word == "green" ? green_ : blue_) = v;
            color_pending_                                            = true;
        }
        else if (word == "b")
        {
            state_.bold = on;
        }
        else if (word == "i")
        {
            state_.italic = on;
        }
        else if (word == "ul" || word == "uld" || word == "uldb" || word == "ulw" || word == "ulwave" ||
                 word == "uldash" || word == "ulth" || word == "uldashd" || word == "ulhwave")
        {
            state_.under = on;
        }
        else if (word == "ulnone")
        {
            state_.under = false;
        }
        else if (word == "strike" || word == "striked")
        {
            state_.strike = on;
        }
        else if (word == "cf")
        {
            state_.fg = n;
        }
        else if (word == "highlight" || word == "cb" || word == "chcbpat")
        {
            state_.bg = n;
        }
        else if (word == "fs")
        {
            state_.size = n > 0 ? n : DefaultHalfPoints;
        }
        else if (word == "plain")
        {
            const Destination dest = state_.dest;
            const int         uc   = state_.uc;
            state_                 = State{};
            state_.dest            = dest;
            state_.uc              = uc;
        }
        else if (word == "uc")
        {
            state_.uc = std::max(0, n);
        }
        else if (word == "u")
        {
            int code = param.value_or(0);
            if (code < 0)
            {
                code += 65536;
            }
            character(static_cast<char32_t>(code));
            skip_fallback_ = state_.uc;
        }
        else if (word == "par" || word == "line" || word == "row")
        {
            character('\n');
        }
        else if (word == "tab" || word == "cell")
        {
            character('\t');
        }
        else if (word == "emdash")
        {
            character(U'\x2014');
        }
        else if (word == "endash")
        {
            character(U'\x2013');
        }
        else if (word == "bullet")
        {
            character(U'\x2022');
        }
        else if (word == "lquote")
        {
            character(U'\x2018');
        }
        else if (word == "rquote")
        {
            character(U'\x2019');
        }
        else if (word == "ldblquote")
        {
            character(U'\x201C');
        }
        else if (word == "rdblquote")
        {
            character(U'\x201D');
        }
        else if (word == "bin")
        {
            pos_ = std::min(in_.size(), pos_ + static_cast<std::size_t>(std::max(0, n)));
        }
        // Paragraph formatting, page setup, \pard, \ansi and the rest change nothing here.
    }

    void character(char32_t ch)
    {
        if (skip_fallback_ > 0)
        {
            --skip_fallback_;
            return;
        }
        // \uN counts UTF-16 units, so a character past the BMP arrives as two: pair them up.
        if (ch >= 0xD800 && ch <= 0xDBFF)
        {
            high_surrogate_ = ch;
            return;
        }
        if (ch >= 0xDC00 && ch <= 0xDFFF && high_surrogate_ != 0)
        {
            ch = 0x10000 + ((high_surrogate_ - 0xD800) << 10) + (ch - 0xDC00);
        }
        high_surrogate_ = 0;
        switch (state_.dest)
        {
            case Destination::FontTable:
                if (ch == ';')
                {
                    finish_font();
                }
                else if (!(font_name_.empty() && ch == ' '))
                {
                    append_utf8(font_name_, ch);
                }
                return;
            case Destination::ColorTable:
                if (ch == ';')
                {
                    colors_.push_back(
                        color_pending_ ? std::optional<Color>(Color::from_rgba(red_, green_, blue_)) : std::nullopt);
                    color_pending_ = false;
                    red_ = green_ = blue_ = 0;
                }
                return;
            case Destination::Text:
                break;
        }

        RichTextFormat format;
        format.bold          = state_.bold;
        format.italic        = state_.italic;
        format.underline     = state_.under;
        format.strikethrough = state_.strike;
        format.foreground    = color(state_.fg);
        format.background    = color(state_.bg);
        const auto font      = fonts_.find(state_.font >= 0 ? state_.font : default_font_);
        if (font != fonts_.end())
        {
            format.font_family = font->second;
        }
        format.font_size = static_cast<float>(state_.size) * 2.0f / 3.0f;

        if (out_.empty() || !(out_.back().format == format))
        {
            out_.push_back(RichTextSpan{{}, format});
        }
        append_utf8(out_.back().text, ch);
    }

    [[nodiscard]] std::optional<Color> color(int index) const
    {
        if (index <= 0 || static_cast<std::size_t>(index) >= colors_.size())
        {
            return std::nullopt;
        }
        return colors_[static_cast<std::size_t>(index)];
    }

    void finish_font()
    {
        while (!font_name_.empty() && font_name_.back() == ' ')
        {
            font_name_.pop_back();
        }
        fonts_[font_number_] = font_name_;
        font_name_.clear();
    }

    std::string_view   in_;
    std::size_t        pos_ = 0;
    State              state_{};
    std::vector<State> stack_;
    int                skip_fallback_  = 0;
    char32_t           high_surrogate_ = 0;

    std::map<int, std::string>        fonts_;
    int                               default_font_ = 0;
    int                               font_number_  = 0;
    std::string                       font_name_;
    std::vector<std::optional<Color>> colors_;
    bool                              color_pending_ = false;
    std::uint8_t                      red_           = 0;
    std::uint8_t                      green_         = 0;
    std::uint8_t                      blue_          = 0;

    std::vector<RichTextSpan> out_;
};

int channel(float c)
{
    return static_cast<int>(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f));
}

// Font names go into the table as they are, RTF-escaped.
void append_text(std::string& out, std::string_view text)
{
    for (const char32_t ch : code_points(text))
    {
        if (ch == '\\' || ch == '{' || ch == '}')
        {
            out += '\\';
            out += static_cast<char>(ch);
        }
        else if (ch == '\n')
        {
            out += "\\par\r\n";
        }
        else if (ch == '\t')
        {
            out += "\\tab ";
        }
        else if (ch >= 0x20 && ch < 0x80)
        {
            out += static_cast<char>(ch);
        }
        else if (ch >= 0x80)
        {
            // \uN takes a signed 16-bit UTF-16 unit, so a character past the BMP is written as
            // its surrogate pair; the ? is the fallback \uc1 says to skip.
            for (const char16_t unit : to_utf16(encode_utf8(ch)))
            {
                out += std::format("\\u{}?", static_cast<int>(static_cast<std::int16_t>(unit)));
            }
        }
    }
}

} // namespace

std::vector<RichTextSpan> parse_rtf(std::string_view rtf, bool* is_rtf)
{
    const std::size_t start = rtf.find_first_not_of(" \t\r\n");
    const bool        rtf_  = start != std::string_view::npos && rtf.compare(start, 5, "{\\rtf") == 0;
    if (is_rtf)
    {
        *is_rtf = rtf_;
    }
    if (!rtf_)
    {
        return {};
    }
    Parser parser(rtf.substr(start));
    return parser.run();
}

std::string write_rtf(const std::vector<RichTextSpan>& spans, const std::string& base_family, float base_size)
{
    std::vector<std::string> fonts{base_family};
    std::vector<Color>       colors;
    const auto               font_index = [&](const std::optional<std::string>& family) -> int
    {
        const std::string& name = family.value_or(base_family);
        auto               it   = std::find(fonts.begin(), fonts.end(), name);
        if (it == fonts.end())
        {
            fonts.push_back(name);
            return static_cast<int>(fonts.size()) - 1;
        }
        return static_cast<int>(it - fonts.begin());
    };
    const auto color_index = [&](const std::optional<Color>& color) -> int
    {
        if (!color)
        {
            return 0;
        }
        const Color c  = Color::from_rgba(static_cast<std::uint8_t>(channel(color->r)),
                                          static_cast<std::uint8_t>(channel(color->g)),
                                          static_cast<std::uint8_t>(channel(color->b)));
        auto        it = std::find(colors.begin(), colors.end(), c);
        if (it == colors.end())
        {
            colors.push_back(c);
            return static_cast<int>(colors.size());
        }
        return static_cast<int>(it - colors.begin()) + 1;
    };
    const auto half_points = [&](std::optional<float> size)
    { return static_cast<int>(std::lround(size.value_or(base_size) * 1.5f)); };

    // The tables come first in the file but are only known after a pass over the spans.
    for (const RichTextSpan& span : spans)
    {
        font_index(span.format.font_family);
        color_index(span.format.foreground);
        color_index(span.format.background);
    }

    std::string out = "{\\rtf1\\ansi\\ansicpg1252\\deff0\\nouicompat{\\fonttbl";
    for (std::size_t i = 0; i < fonts.size(); ++i)
    {
        out += std::format("{{\\f{}\\fnil ", i);
        append_text(out, fonts[i]);
        out += ";}";
    }
    out += "}\r\n";
    if (!colors.empty())
    {
        out += "{\\colortbl ;";
        for (const Color& c : colors)
        {
            out += std::format("\\red{}\\green{}\\blue{};", channel(c.r), channel(c.g), channel(c.b));
        }
        out += "}\r\n";
    }
    out += std::format("\\uc1\\pard\\f0\\fs{} ", half_points(std::nullopt));

    State written;
    written.font = 0;
    written.size = half_points(std::nullopt);
    for (const RichTextSpan& span : spans)
    {
        const RichTextFormat& f = span.format;
        std::string           words;
        const auto            flag = [&](bool want, bool& have, std::string_view on, std::string_view off)
        {
            if (want != have)
            {
                words += want ? on : off;
                have = want;
            }
        };
        flag(f.bold, written.bold, "\\b", "\\b0");
        flag(f.italic, written.italic, "\\i", "\\i0");
        flag(f.underline, written.under, "\\ul", "\\ulnone");
        flag(f.strikethrough, written.strike, "\\strike", "\\strike0");
        const auto number = [&](int want, int& have, std::string_view word)
        {
            if (want != have)
            {
                words += std::format("\\{}{}", word, want);
                have = want;
            }
        };
        number(color_index(f.foreground), written.fg, "cf");
        number(color_index(f.background), written.bg, "highlight");
        number(font_index(f.font_family), written.font, "f");
        number(half_points(f.font_size), written.size, "fs");
        if (!words.empty())
        {
            out += words;
            out += ' ';
        }
        append_text(out, span.text);
    }
    out += "\\par\r\n}\r\n";
    return out;
}

} // namespace ui::detail
