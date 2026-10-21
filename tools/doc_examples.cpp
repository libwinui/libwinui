// SPDX-License-Identifier: MIT
// Turns the `@code` examples in the public headers' doc comments into C++ that the build compiles,
// so an example that no longer compiles fails the build instead of reaching the reference.
//
//     winui_doc_examples <repository root> <output directory>
//
// Writes one `<header path with _ for />.cpp` per public header into the output directory,
// rewriting a file only when its text changed. Each example goes into a namespace of its own:
// declarations (structs, classes, enums, templates, function definitions) at namespace scope, the
// statements in between into a function body, in their order. The examples share the placeholders
// in tools/doc_examples/placeholders.hpp, such as `window` or `open_file()`.
//
// An example that can't compile on its own, because it shows a piece of a larger function, opts out
// with an HTML comment on the line before `@code`, which the reference doesn't show:
//
//     /// <!-- not compiled: shows one designator of a ui::PrintJob -->
//     /// @code

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <cctype>
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
    while (start <= text.size())
    {
        const std::size_t end = text.find('\n', start);
        lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
    return lines;
}

std::string trim(std::string_view s)
{
    const std::size_t b = s.find_first_not_of(" \t");
    if (b == std::string_view::npos)
    {
        return {};
    }
    const std::size_t e = s.find_last_not_of(" \t");
    return std::string(s.substr(b, e - b + 1));
}

struct Example
{
    int                      line = 0; // of the @code line, 1-based
    std::vector<std::string> code;
};

// The examples of one header, from its `///` lines.
std::vector<Example> examples_in(const std::string& text)
{
    std::vector<Example>           out;
    const std::vector<std::string> lines   = split_lines(text);
    bool                           in_code = false;
    bool                           skip    = false;
    Example                        current;
    std::string                    previous;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const std::string line = trim(lines[i]);
        if (!line.starts_with("///"))
        {
            in_code  = false;
            previous = {};
            continue;
        }
        std::string body = line.substr(3);
        if (body.starts_with(" "))
        {
            body.erase(0, 1);
        }
        // the code keeps its indentation relative to the comment
        const std::string raw = [&]
        {
            const std::size_t at = lines[i].find("///");
            std::string       r  = lines[i].substr(at + 3);
            if (r.starts_with(" "))
            {
                r.erase(0, 1);
            }
            return r;
        }();
        if (!in_code)
        {
            if (trim(body).starts_with("@code"))
            {
                in_code = true;
                skip    = previous.find("<!-- not compiled") != std::string::npos;
                current = Example{static_cast<int>(i + 1), {}};
            }
            else if (!trim(body).empty())
            {
                previous = body;
            }
            continue;
        }
        if (trim(body).starts_with("@endcode"))
        {
            in_code = false;
            if (!skip)
            {
                out.push_back(std::move(current));
            }
            previous = {};
            continue;
        }
        current.code.push_back(raw);
    }
    return out;
}

// One top-level statement of an example, with the comment lines before it.
struct Statement
{
    std::string text;
    bool        at_namespace_scope = false;
    int         line               = 0; // the example line it starts on, 0-based
};

bool starts_with_word(std::string_view s, std::string_view word)
{
    return s.starts_with(word) && (s.size() == word.size() ||
                                   !(std::isalnum(static_cast<unsigned char>(s[word.size()])) || s[word.size()] == '_'));
}

// The code with comments and string contents blanked, so brace counting sees only code.
std::string code_only(const std::string& s)
{
    std::string out = s;
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        if (out.compare(i, 2, "//") == 0)
        {
            while (i < out.size() && out[i] != '\n')
            {
                out[i++] = ' ';
            }
        }
        else if (out.compare(i, 2, "/*") == 0)
        {
            const std::size_t end  = out.find("*/", i + 2);
            const std::size_t stop = end == std::string::npos ? out.size() : end + 2;
            for (; i < stop; ++i)
            {
                if (out[i] != '\n')
                {
                    out[i] = ' ';
                }
            }
            --i;
        }
        else if (out[i] == 'R' && i + 1 < out.size() && out[i + 1] == '"')
        {
            const std::size_t open = out.find('(', i);
            if (open == std::string::npos)
            {
                continue;
            }
            const std::string delim = ")" + out.substr(i + 2, open - i - 2) + "\"";
            const std::size_t end   = out.find(delim, open);
            const std::size_t stop  = end == std::string::npos ? out.size() : end + delim.size();
            for (std::size_t j = open; j < stop - 1; ++j)
            {
                if (out[j] != '\n')
                {
                    out[j] = ' ';
                }
            }
            i = stop - 1;
        }
        else if (out[i] == '"' ||
                 (out[i] == '\'' && !(i > 0 && std::isxdigit(static_cast<unsigned char>(out[i - 1])) &&
                                      i + 1 < out.size() && std::isxdigit(static_cast<unsigned char>(out[i + 1])))))
        {
            const char  quote = out[i];
            std::size_t j     = i + 1;
            while (j < out.size() && out[j] != quote && out[j] != '\n')
            {
                if (out[j] == '\\')
                {
                    out[j++] = ' ';
                }
                if (j < out.size())
                {
                    out[j++] = ' ';
                }
            }
            i = j;
        }
    }
    return out;
}

std::vector<Statement> statements_of(const std::vector<std::string>& lines)
{
    std::string text;
    for (const std::string& l : lines)
    {
        text += l + "\n";
    }
    const std::string code = code_only(text);

    std::vector<Statement> out;
    std::size_t            start       = 0;
    int                    depth       = 0;
    bool                   opens_block = false;
    const auto             flush       = [&](std::size_t end)
    {
        const std::string s = text.substr(start, end - start);
        const int line = static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(start), '\n'));
        start = end;
        if (trim(code_only(s)).find_first_not_of(" \t\n") == std::string::npos)
        {
            if (!out.empty())
            {
                out.back().text += s; // trailing comments
            }
            else if (!s.empty())
            {
                out.push_back({s, false, line});
            }
            return;
        }
        // the first code line decides where the statement goes
        std::string first = code_only(s);
        first             = first.substr(first.find_first_not_of(" \t\n"));
        Statement               st{s, false, line};
        static const std::regex function_definition(
            R"(^(?:\[\[[^\]]*\]\]\s*)*(?:inline\s+|static\s+|constexpr\s+|virtual\s+)*[\w:<>,\s\*&~]*?[\w:~>]+\s*\()");
        static const char* const statement_keywords[] = {
            "if",
            "for",
            "while",
            "switch",
            "do",
            "try",
            "else",
            "return",
            "co_await",
            "co_return",
            "co_yield",
        };
        const bool keyword = std::ranges::any_of(statement_keywords,
                                                 [&](const char* k) { return starts_with_word(first, k); });
        const char last    = code_only(s)[code_only(s).find_last_not_of(" \t\n")];
        if (first.starts_with("#") || starts_with_word(first, "struct") || starts_with_word(first, "class") ||
            starts_with_word(first, "enum") || starts_with_word(first, "union") || starts_with_word(first, "template") ||
            starts_with_word(first, "namespace") || starts_with_word(first, "extern"))
        {
            st.at_namespace_scope = true;
        }
        else if (last == '}' && !keyword && std::regex_search(first, function_definition) &&
                 first.find('(') < first.find('{'))
        {
            st.at_namespace_scope = true; // a function definition
        }
        out.push_back(std::move(st));
    };

    for (std::size_t i = 0; i < code.size(); ++i)
    {
        const char c = code[i];
        if (c == '#' && depth == 0 && (i == 0 || code[i - 1] == '\n'))
        {
            const std::size_t end = code.find('\n', i);
            i                     = end == std::string::npos ? code.size() - 1 : end;
            flush(i + 1);
            continue;
        }
        if (c == '{' && depth == 0)
        {
            // A block (a function, an if, a namespace) ends its statement; a braced initializer
            // doesn't. A block's brace follows `)`, `else`, `do`, `try` or a function qualifier.
            const std::size_t before = code.find_last_not_of(" \t\n", i == 0 ? 0 : i - 1);
            std::size_t       word   = before;
            while (word > 0 && (std::isalnum(static_cast<unsigned char>(code[word - 1])) || code[word - 1] == '_'))
            {
                --word;
            }
            const std::string previous = before == std::string::npos ? std::string{} : code.substr(word, before - word + 1);
            const std::string statement = trim(code_only(text.substr(start, i - start)));
            opens_block = (before != std::string::npos && code[before] == ')') || previous == "else" ||
                          previous == "do" || previous == "try" || previous == "const" || previous == "override" ||
                          previous == "noexcept" || previous == "final" || starts_with_word(statement, "namespace");
        }
        if (c == '(' || c == '{' || c == '[')
        {
            ++depth;
        }
        else if (c == ')' || c == ']')
        {
            --depth;
        }
        else if (c == '}')
        {
            --depth;
            if (depth == 0 && opens_block)
            {
                // A block ends the statement unless a ; , ) or else follows (a lambda, an initializer).
                const std::size_t next = code.find_first_not_of(" \t\n", i + 1);
                const bool continues = next != std::string::npos &&
                                       (code[next] == ';' || code[next] == ',' || code[next] == ')' || code[next] == '.' ||
                                        code[next] == '-' || code[next] == '(' || code.compare(next, 4, "else") == 0 ||
                                        code.compare(next, 5, "catch") == 0 || code.compare(next, 5, "while") == 0);
                if (!continues)
                {
                    flush(i + 1);
                }
            }
        }
        else if (c == ';' && depth == 0)
        {
            flush(i + 1);
        }
    }
    if (start < text.size())
    {
        flush(text.size());
    }
    return out;
}

std::string generated_for(const std::string& header_path, const std::vector<Example>& examples)
{
    std::string ns = header_path;
    for (char& c : ns)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)))
        {
            c = '_';
        }
    }
    std::ostringstream out;
    out << "// Generated by winui_doc_examples from " << header_path << ". Do not edit.\n";
    // The header itself too, for one the umbrella headers leave out.
    out << "#include \"placeholders.hpp\"\n#include <" << header_path.substr(std::string_view("include/").size())
        << ">\n\n";
    int index = 0;
    for (const Example& example : examples)
    {
        const std::vector<Statement> statements = statements_of(example.code);
        std::string                  directives;
        for (const Statement& s : statements)
        {
            const std::string first = trim(s.text.substr(s.text.find_first_not_of(" \t\n")));
            if (first.starts_with("#"))
            {
                directives += first + "\n";
            }
        }
        out << directives;
        out << "// " << header_path << ":" << example.line << "\n";
        out << "namespace doc_" << ns << "_" << index++ << "\n{\nusing namespace doc_placeholders;\n";
        for (const Statement& s : statements)
        {
            if (s.at_namespace_scope && !trim(s.text.substr(s.text.find_first_not_of(" \t\n"))).starts_with("#"))
            {
                out << "#line " << example.line + 1 + s.line << " \"" << header_path << "\"\n" << s.text << "\n";
            }
        }
        out << "[[maybe_unused]] void example()\n{\n";
        for (const Statement& s : statements)
        {
            if (!s.at_namespace_scope)
            {
                // A spec shown on its own (`ui::Button{...}` with no `;`) is an expression statement.
                const std::string        code       = code_only(s.text);
                const std::size_t        last       = code.find_last_not_of(" \t\n");
                const std::size_t        first      = code.find_first_not_of(" \t\n");
                static const char* const blocks[]   = {"if", "for", "while", "switch", "do", "try", "else", "{"};
                const bool               expression = last != std::string::npos && code[last] == '}' &&
                                                      std::ranges::none_of(blocks,
                                                                           [&](const char* k)
                                                                           {
                                                                 return starts_with_word(code.substr(first), k) ||
                                                                        code[first] == '{';
                                                                           });
                out << "#line " << example.line + 1 + s.line << " \"" << header_path << "\"\n"
                    << s.text << (expression ? ";" : "") << "\n";
            }
        }
        out << "}\n} // namespace\n\n";
    }
    return out.str();
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: winui_doc_examples <repository root> <output directory>\n");
        return 2;
    }
    const fs::path root = argv[1];
    const fs::path out  = argv[2];
    fs::create_directories(out);

    std::set<std::string> written;
    int                   count = 0;
    for (const auto& entry : fs::recursive_directory_iterator(root / "include" / "winui"))
    {
        if (entry.path().extension() != ".hpp")
        {
            continue;
        }
        const std::string    rel      = fs::relative(entry.path(), root).generic_string();
        std::vector<Example> examples = examples_in(read_file(entry.path()));
        count += static_cast<int>(examples.size());
        std::string name = fs::relative(entry.path(), root / "include" / "winui").generic_string();
        std::ranges::replace(name, '/', '_');
        name.replace(name.size() - 4, 4, ".cpp");
        written.insert(name);
        const std::string text = generated_for(rel, examples);
        const fs::path    file = out / name;
        if (!fs::exists(file) || read_file(file) != text)
        {
            std::ofstream(file, std::ios::binary) << text;
        }
    }
    std::printf("%d examples\n", count);
    return 0;
}
