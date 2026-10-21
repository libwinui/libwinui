// SPDX-License-Identifier: MIT
// Checks the gallery's `.code` snippets, which no compiler sees: every designator of a spec must
// be a field of that spec, in declaration order. Also that a snippet uses none of the gallery's own
// helpers or page state, and that every gallery page has its paragraph in page_about.cpp.
//
//     winui_check_snippets <repository root>
//
// Prints each problem as `file:line reason` and exits 1 when there is one. ctest runs it as the
// gallery_snippets test.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <cstdio>

namespace fs = std::filesystem;

namespace
{
std::string read_file(const fs::path& path)
{
    std::ifstream      in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    std::string s = text.str();
    std::erase(s, '\r');
    return s;
}

std::vector<std::string> split_lines(const std::string& text)
{
    std::vector<std::string> lines;
    std::size_t              start = 0;
    while (true)
    {
        const std::size_t end = text.find('\n', start);
        if (end == std::string::npos)
        {
            lines.push_back(text.substr(start));
            return lines;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool is_word(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           static_cast<unsigned char>(c) >= 0x80;
}

std::string strip(std::string_view s)
{
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && is_space(s[b]))
    {
        ++b;
    }
    while (e > b && is_space(s[e - 1]))
    {
        --e;
    }
    return std::string(s.substr(b, e - b));
}

int line_of(const std::string& text, std::size_t pos)
{
    return 1 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(pos), '\n'));
}

// Files under `dir` matching `pred`, as paths relative to `root` with forward slashes, sorted.
template <class Pred>
std::vector<std::string> files(const fs::path& root, const std::string& dir, bool recursive, Pred pred)
{
    std::vector<std::string> out;
    const fs::path           base = root / dir;
    const auto               add  = [&](const fs::directory_entry& e)
    {
        if (e.is_regular_file() && pred(e.path().filename().string()))
        {
            out.push_back(fs::relative(e.path(), root).generic_string());
        }
    };
    if (recursive)
    {
        for (const auto& e : fs::recursive_directory_iterator(base))
        {
            add(e);
        }
    }
    else
    {
        for (const auto& e : fs::directory_iterator(base))
        {
            add(e);
        }
    }
    std::ranges::sort(out);
    return out;
}

bool ends_with(const std::string& s, std::string_view suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// ---- spec name -> ordered field names, from the headers ----

// A field line: four spaces, a type, the name, an optional initializer, a semicolon and an
// optional comment.
const std::regex FieldLine(R"(^    .*?[\s\*&>]([a-z_][a-z0-9_]*)\s*(?:\{.*\}|=.*)?;\s*(?:///.*|//.*)?$)");

bool skipped_line(const std::string& stripped)
{
    static const char* const Prefixes[] =
        {"//", "using ", "[[", "friend", "static", "template", "explicit", "constexpr", "struct", "enum", "return"};
    return std::ranges::any_of(Prefixes, [&](const char* p) { return stripped.starts_with(p); });
}

// Whether the word `name` is followed by optional white space and an opening parenthesis in the
// code of `line` (its comment left out): a member function, not a field.
bool called(std::string line, const std::string& name)
{
    line = line.substr(0, line.find("//"));
    for (std::size_t at = line.find(name); at != std::string::npos; at = line.find(name, at + 1))
    {
        if (at > 0 && is_word(line[at - 1]))
        {
            continue;
        }
        std::size_t i = at + name.size();
        while (i < line.size() && is_space(line[i]))
        {
            ++i;
        }
        if (i < line.size() && line[i] == '(')
        {
            return true;
        }
    }
    return false;
}

// The name of a `struct Name` line, or empty.
std::string struct_name(const std::string& line)
{
    if (!line.starts_with("struct ") || line.size() == 7)
    {
        return {};
    }
    const std::string name = line.substr(7);
    return std::ranges::all_of(name, is_word) ? name : std::string{};
}

std::map<std::string, std::vector<std::string>> read_specs(const fs::path& root)
{
    std::map<std::string, std::vector<std::string>> specs;
    for (const std::string& path :
         files(root, "include/winui", true, [](const std::string& n) { return ends_with(n, ".hpp"); }))
    {
        const std::vector<std::string> lines = split_lines(read_file(root / path));
        for (std::size_t i = 0; i + 1 < lines.size(); ++i)
        {
            const std::string name = struct_name(lines[i]);
            if (name.empty() || lines[i + 1] != "{")
            {
                continue;
            }
            std::size_t end = i + 2;
            while (end < lines.size() && !lines[end].starts_with("};"))
            {
                ++end;
            }
            if (end == lines.size())
            {
                continue;
            }
            std::vector<std::string> fields;
            int                      depth = 0;
            for (std::size_t l = i + 2; l < end; ++l)
            {
                const std::string& line     = lines[l];
                const std::string  stripped = strip(line);
                if (depth == 0 && line.starts_with("    ") && !line.starts_with("     ") && !skipped_line(stripped))
                {
                    std::smatch m;
                    if (std::regex_match(line, m, FieldLine) && line.find(") &&") == std::string::npos &&
                        line.find(") const") == std::string::npos && !called(line, m[1].str()))
                    {
                        fields.push_back(m[1].str());
                    }
                }
                depth += static_cast<int>(std::ranges::count(line, '{')) - static_cast<int>(std::ranges::count(line, '}'));
                depth = std::max(depth, 0);
            }
            if (!fields.empty() && !specs.contains(name))
            {
                specs.emplace(name, std::move(fields));
            }
            i = end;
        }
    }
    return specs;
}

// ---- snippets ----

// Where the brace opened at `open_at` closes, skipping string literals; npos when it never does.
std::size_t block_end(const std::string& text, std::size_t open_at)
{
    int  depth  = 0;
    bool in_str = false;
    for (std::size_t i = open_at; i < text.size(); ++i)
    {
        const char c = text[i];
        if (in_str)
        {
            if (c == '\\')
            {
                ++i;
            }
            else if (c == '"')
            {
                in_str = false;
            }
        }
        else if (c == '"')
        {
            in_str = true;
        }
        else if (c == '{')
        {
            ++depth;
        }
        else if (c == '}')
        {
            if (--depth == 0)
            {
                return i;
            }
        }
    }
    return std::string::npos;
}

// The designator `.name =` (not `==`) at `i`, or empty.
std::string designator_at(const std::string& body, std::size_t i)
{
    std::size_t j = i + 1;
    if (j >= body.size() || !((body[j] >= 'a' && body[j] <= 'z') || body[j] == '_'))
    {
        return {};
    }
    while (j < body.size() && ((body[j] >= 'a' && body[j] <= 'z') || (body[j] >= '0' && body[j] <= '9') || body[j] == '_'))
    {
        ++j;
    }
    const std::string name = body.substr(i + 1, j - i - 1);
    while (j < body.size() && is_space(body[j]))
    {
        ++j;
    }
    if (j >= body.size() || body[j] != '=' || (j + 1 < body.size() && body[j + 1] == '='))
    {
        return {};
    }
    return name;
}

// The designators at depth 0 of a brace body.
std::vector<std::string> top_designators(const std::string& body)
{
    std::vector<std::string> out;
    int                      depth  = 0;
    bool                     in_str = false;
    for (std::size_t i = 0; i < body.size(); ++i)
    {
        const char c = body[i];
        if (in_str)
        {
            if (c == '\\')
            {
                ++i;
            }
            else if (c == '"')
            {
                in_str = false;
            }
        }
        else if (c == '"')
        {
            in_str = true;
        }
        else if (c == '{' || c == '(' || c == '[')
        {
            ++depth;
        }
        else if (c == '}' || c == ')' || c == ']')
        {
            --depth;
        }
        else if (c == '.' && depth == 0)
        {
            const std::string name = designator_at(body, i);
            std::size_t       p    = i;
            while (p > 0 && is_space(body[p - 1]))
            {
                --p;
            }
            if (!name.empty() && (p == 0 || body[p - 1] == ',' || body[p - 1] == '{'))
            {
                out.push_back(name);
            }
        }
    }
    return out;
}

// One `ui::Name{` or `ui::Name<...>{` in a snippet: the name and the position of its brace.
struct SpecUse
{
    std::string name;
    std::size_t brace = 0;
};

// Template arguments starting at `i` (on the '<'), with at most one nested `<...>`; the position
// after the closing '>', or npos.
std::size_t template_args_end(const std::string& code, std::size_t i)
{
    const auto plain = [&](std::size_t p)
    {
        while (p < code.size() && std::string_view("<>{};").find(code[p]) == std::string_view::npos)
        {
            ++p;
        }
        return p;
    };
    std::size_t p = plain(i + 1);
    if (p < code.size() && code[p] == '<')
    {
        std::size_t q = p + 1;
        while (q < code.size() && code[q] != '<' && code[q] != '>')
        {
            ++q;
        }
        if (q < code.size() && code[q] == '>')
        {
            p = plain(q + 1);
        }
    }
    return p < code.size() && code[p] == '>' ? p + 1 : std::string::npos;
}

std::vector<SpecUse> spec_uses(const std::string& code)
{
    std::vector<SpecUse> out;
    std::size_t          from = 0;
    while (true)
    {
        const std::size_t at = code.find("ui::", from);
        if (at == std::string::npos)
        {
            return out;
        }
        from = at + 1;
        if (at > 0 && is_word(code[at - 1]))
        {
            continue;
        }
        std::size_t p = at + 4;
        while (p < code.size() && is_word(code[p]))
        {
            ++p;
        }
        if (p == at + 4)
        {
            continue;
        }
        const std::string name = code.substr(at + 4, p - at - 4);
        if (p < code.size() && code[p] == '<')
        {
            const std::size_t after = template_args_end(code, p);
            if (after != std::string::npos && after < code.size() && code[after] == '{')
            {
                p = after;
            }
        }
        if (p < code.size() && code[p] == '{')
        {
            out.push_back({name, p});
            from = p + 1;
        }
    }
}

// Whether the snippet declares a local named `s` (`T s = ...`, `T s{...}`).
bool declares_s(const std::string& code)
{
    for (std::size_t i = 1; i < code.size(); ++i)
    {
        if (code[i] != 's' || !is_space(code[i - 1]))
        {
            continue;
        }
        std::size_t j = i + 1;
        while (j < code.size() && is_space(code[j]))
        {
            ++j;
        }
        if (j >= code.size() || std::string_view("=;{(").find(code[j]) == std::string_view::npos)
        {
            continue;
        }
        std::size_t k = i - 1;
        while (k > 0 && is_space(code[k]))
        {
            --k;
        }
        if (!is_space(code[k]) && (is_word(code[k]) || std::string_view(":<>&*").find(code[k]) != std::string_view::npos))
        {
            return true;
        }
    }
    return false;
}

// Whether the snippet uses the gallery page's state struct `s.` (not `x.s.` or `obj->s.`).
bool uses_state_struct(const std::string& code)
{
    for (std::size_t i = 0; i + 2 < code.size(); ++i)
    {
        if (code[i] == 's' && code[i + 1] == '.' && is_word(code[i + 2]) &&
            (i == 0 || !(is_word(code[i - 1]) || code[i - 1] == '.' || code[i - 1] == '>')))
        {
            return true;
        }
    }
    return false;
}

// The `{"Name", ...}` entries of a page file's `Pages = std::to_array<Page>({ ... });` table.
std::vector<std::string> page_names(const std::string& text)
{
    constexpr std::string_view Head = "std::to_array<Page>({";
    const std::size_t          head = text.find(Head);
    if (head == std::string::npos)
    {
        return {};
    }
    const std::size_t start = head + Head.size();
    const std::size_t end   = text.find("});", start);
    if (end == std::string::npos)
    {
        return {};
    }
    std::vector<std::string> names;
    const std::string        table = text.substr(start, end - start);
    for (std::size_t at = table.find("{\""); at != std::string::npos; at = table.find("{\"", at + 1))
    {
        const std::size_t close = table.find('"', at + 2);
        if (close != std::string::npos && close > at + 2 && close + 1 < table.size() && table[close + 1] == ',')
        {
            names.push_back(table.substr(at + 2, close - at - 2));
        }
    }
    return names;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: winui_check_snippets <repository root>\n");
        return 2;
    }
    const fs::path root     = argv[1];
    const auto     specs    = read_specs(root);
    int            total    = 0;
    int            problems = 0;
    const auto     report   = [&](const std::string& where, const std::string& why)
    {
        std::printf("%s %s\n", where.c_str(), why.c_str());
        ++problems;
    };

    const auto page_files = files(root,
                                  "examples/gallery",
                                  false,
                                  [](const std::string& n) { return n.starts_with("pages_") && ends_with(n, ".cpp"); });
    for (const std::string& path : page_files)
    {
        const std::string text = read_file(root / path);
        for (std::size_t open = text.find("R\"cpp("); open != std::string::npos; open = text.find("R\"cpp(", open + 1))
        {
            const std::size_t close = text.find(")cpp\"", open + 6);
            if (close == std::string::npos)
            {
                break;
            }
            ++total;
            const std::string                                code  = text.substr(open + 6, close - open - 6);
            const std::string                                where = path + ":" + std::to_string(line_of(text, open));
            static const std::pair<const char*, const char*> Banned[] = {
                {"with_state", "gallery helper"},
                {"options_panel", "gallery helper"},
                {"using namespace", "using namespace"},
                {"example(", "gallery helper"},
            };
            for (const auto& [bad, why] : Banned)
            {
                if (code.find(bad) != std::string::npos)
                {
                    report(where, why);
                }
            }
            // The page's state struct is `s`; a snippet's own local of that name is fine.
            if (!declares_s(code) && uses_state_struct(code))
            {
                report(where, "state-struct prefix s.");
            }
            for (const SpecUse& use : spec_uses(code))
            {
                const auto spec = specs.find(use.name);
                if (spec == specs.end())
                {
                    continue;
                }
                const std::size_t end = block_end(code, use.brace);
                if (end == std::string::npos)
                {
                    report(where, "unbalanced braces in " + use.name);
                    continue;
                }
                const std::vector<std::string>& fields = spec->second;
                std::ptrdiff_t                  last   = -1;
                for (const std::string& f : top_designators(code.substr(use.brace + 1, end - use.brace - 1)))
                {
                    const auto it = std::ranges::find(fields, f);
                    if (it == fields.end())
                    {
                        report(where, "ui::" + use.name + " has no field ." + f);
                        continue;
                    }
                    const std::ptrdiff_t at = it - fields.begin();
                    if (at < last)
                    {
                        report(where, "ui::" + use.name + ": ." + f + " is out of declaration order");
                    }
                    last = std::max(last, at);
                }
            }
            open = close;
        }
    }

    // ---- every page has its paragraph in page_about.cpp ----
    std::set<std::string> about_pages;
    {
        const std::string about = read_file(root / "examples/gallery/page_about.cpp");
        for (std::size_t at = about.find("About{\""); at != std::string::npos; at = about.find("About{\"", at + 1))
        {
            const std::size_t close = about.find('"', at + 7);
            if (close != std::string::npos && close > at + 7)
            {
                about_pages.insert(about.substr(at + 7, close - at - 7));
            }
        }
    }
    for (const std::string& path : page_files)
    {
        for (const std::string& name : page_names(read_file(root / path)))
        {
            if (!about_pages.contains(name))
            {
                report(path, "page \"" + name + "\" has no paragraph in page_about.cpp");
            }
        }
    }

    std::printf("snippets %d problems %d specs known %zu\n", total, problems, specs.size());
    return problems != 0 ? 1 : 0;
}
