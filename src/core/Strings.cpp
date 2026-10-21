// SPDX-License-Identifier: MIT
#include "winui/core/Strings.hpp"

#include "StringsDetail.hpp"

namespace ui
{

namespace
{
struct BuiltInText
{
    Strings                 strings{};
    Property<std::uint64_t> revision{0};
    Signal                  changed{};
};

BuiltInText& built_in()
{
    static BuiltInText text;
    return text;
}
} // namespace

const Strings& strings()
{
    return built_in().strings;
}

void set_strings(Strings replacement)
{
    BuiltInText& text = built_in();
    text.strings      = std::move(replacement);
    text.revision     = text.revision.get() + 1;
    text.changed.raise();
}

const Signal& strings_changed()
{
    return built_in().changed;
}

namespace detail
{

const Property<std::uint64_t>& strings_revision()
{
    return built_in().revision;
}

std::string fill(std::string_view pattern, std::initializer_list<std::string_view> args)
{
    std::string out;
    out.reserve(pattern.size());
    for (std::size_t i = 0; i < pattern.size(); ++i)
    {
        const char c = pattern[i];
        if ((c == '{' || c == '}') && i + 1 < pattern.size() && pattern[i + 1] == c)
        {
            out += c;
            ++i;
            continue;
        }
        if (c == '{')
        {
            const std::size_t close = pattern.find('}', i + 1);
            if (close != std::string_view::npos && close > i + 1)
            {
                std::size_t index  = 0;
                bool        digits = true;
                for (std::size_t j = i + 1; j < close; ++j)
                {
                    const char d = pattern[j];
                    if (d < '0' || d > '9')
                    {
                        digits = false;
                        break;
                    }
                    index = index * 10 + static_cast<std::size_t>(d - '0');
                }
                if (digits && index < args.size())
                {
                    out += *(args.begin() + index);
                    i = close;
                    continue;
                }
            }
        }
        out += c;
    }
    return out;
}

} // namespace detail

} // namespace ui
