// SPDX-License-Identifier: MIT
// Writes the public API surface to api/surface.txt and api/surface-testing.txt.
//
// One line per public declaration, from the Doxygen XML of docs/tools/Doxyfile: every type in
// namespace ui with its bases and public and protected members, spec fields in declaration order,
// enumerators with their values, and the namespace's free functions, aliases and constants. The
// testing tier (ui::testing) gets its own file; the namespaces control_map.toml hides are left out.
// A change to either file in a commit is the signal that the public API moved.
//
//     winui_api_surface <repository root> [--xml-dir <dir>] [--check]
//
// The XML defaults to <root>/build/doxygen/xml; run doxygen first (the api-surface and api-check
// targets do). --check writes nothing and exits 1 when a file would change.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace fs = std::filesystem;

namespace
{
// ---- a small XML reader: elements, attributes, text and tails, the five entities and &#...; ----

struct Element
{
    std::string                                      tag;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::string                                      text; // before the first child
    std::string                                      tail; // after this element, inside its parent
    std::vector<std::unique_ptr<Element>>            children;

    [[nodiscard]] std::string get(std::string_view name, std::string_view fallback = {}) const
    {
        for (const auto& [key, value] : attributes)
        {
            if (key == name)
            {
                return value;
            }
        }
        return std::string(fallback);
    }
    [[nodiscard]] const Element* find(std::string_view name) const
    {
        for (const auto& child : children)
        {
            if (child->tag == name)
            {
                return child.get();
            }
        }
        return nullptr;
    }
    [[nodiscard]] std::vector<const Element*> find_all(std::string_view name) const
    {
        std::vector<const Element*> out;
        for (const auto& child : children)
        {
            if (child->tag == name)
            {
                out.push_back(child.get());
            }
        }
        return out;
    }
    void append_text(std::string& out) const
    {
        out += text;
        for (const auto& child : children)
        {
            child->append_text(out);
            out += child->tail;
        }
    }
};

[[noreturn]] void fail(const std::string& message)
{
    std::fprintf(stderr, "winui_api_surface: %s\n", message.c_str());
    std::exit(2);
}

void append_utf8(std::string& out, unsigned long cp)
{
    if (cp < 0x80)
    {
        out += static_cast<char>(cp);
    }
    else if (cp < 0x800)
    {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else
    {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string decode(std::string_view raw)
{
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i)
    {
        if (raw[i] != '&')
        {
            out += raw[i];
            continue;
        }
        const std::size_t semi = raw.find(';', i);
        if (semi == std::string_view::npos)
        {
            fail("unterminated entity");
        }
        const std::string_view name = raw.substr(i + 1, semi - i - 1);
        if (name == "lt")
        {
            out += '<';
        }
        else if (name == "gt")
        {
            out += '>';
        }
        else if (name == "amp")
        {
            out += '&';
        }
        else if (name == "quot")
        {
            out += '"';
        }
        else if (name == "apos")
        {
            out += '\'';
        }
        else if (name.starts_with("#x"))
        {
            append_utf8(out, std::strtoul(std::string(name.substr(2)).c_str(), nullptr, 16));
        }
        else if (name.starts_with("#"))
        {
            append_utf8(out, std::strtoul(std::string(name.substr(1)).c_str(), nullptr, 10));
        }
        else
        {
            fail("unknown entity &" + std::string(name) + ";");
        }
        i = semi;
    }
    return out;
}

class Parser
{
public:
    explicit Parser(std::string source) : s_(std::move(source))
    {
    }

    std::unique_ptr<Element> document()
    {
        skip_misc();
        if (p_ >= s_.size() || s_[p_] != '<')
        {
            fail("no root element");
        }
        return element();
    }

private:
    // White space, the XML declaration, processing instructions, comments and a DOCTYPE.
    void skip_misc()
    {
        while (p_ < s_.size())
        {
            if (std::isspace(static_cast<unsigned char>(s_[p_])) != 0)
            {
                ++p_;
            }
            else if (s_.compare(p_, 2, "<?") == 0)
            {
                p_ = s_.find("?>", p_) + 2;
            }
            else if (s_.compare(p_, 4, "<!--") == 0)
            {
                p_ = s_.find("-->", p_) + 3;
            }
            else if (s_.compare(p_, 2, "<!") == 0)
            {
                p_ = s_.find('>', p_) + 1;
            }
            else
            {
                return;
            }
        }
    }

    std::string name()
    {
        const std::size_t start = p_;
        while (p_ < s_.size() && std::string_view(" \t\r\n/>=").find(s_[p_]) == std::string_view::npos)
        {
            ++p_;
        }
        return s_.substr(start, p_ - start);
    }

    void skip_space()
    {
        while (p_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[p_])) != 0)
        {
            ++p_;
        }
    }

    // Character data up to the next markup; comments and processing instructions in it are dropped.
    std::string chars()
    {
        std::string out;
        while (p_ < s_.size())
        {
            const std::size_t lt = s_.find('<', p_);
            out += decode(std::string_view(s_).substr(p_, lt - p_));
            p_ = lt;
            if (s_.compare(p_, 4, "<!--") == 0)
            {
                p_ = s_.find("-->", p_) + 3;
            }
            else if (s_.compare(p_, 9, "<![CDATA[") == 0)
            {
                const std::size_t end = s_.find("]]>", p_);
                out += s_.substr(p_ + 9, end - p_ - 9);
                p_ = end + 3;
            }
            else if (s_.compare(p_, 2, "<?") == 0)
            {
                p_ = s_.find("?>", p_) + 2;
            }
            else
            {
                break;
            }
        }
        return out;
    }

    std::unique_ptr<Element> element()
    {
        auto e = std::make_unique<Element>();
        ++p_; // '<'
        e->tag = name();
        while (true)
        {
            skip_space();
            if (s_.compare(p_, 2, "/>") == 0)
            {
                p_ += 2;
                return e;
            }
            if (s_[p_] == '>')
            {
                ++p_;
                break;
            }
            std::string key = name();
            skip_space();
            if (s_[p_] != '=')
            {
                fail("attribute without a value in <" + e->tag + ">");
            }
            ++p_;
            skip_space();
            const char        quote = s_[p_];
            const std::size_t end   = s_.find(quote, p_ + 1);
            e->attributes.emplace_back(std::move(key), decode(std::string_view(s_).substr(p_ + 1, end - p_ - 1)));
            p_ = end + 1;
        }
        e->text = chars();
        while (true)
        {
            if (p_ >= s_.size())
            {
                fail("unclosed <" + e->tag + ">");
            }
            if (s_.compare(p_, 2, "</") == 0)
            {
                p_ = s_.find('>', p_) + 1;
                return e;
            }
            auto child  = element();
            child->tail = chars();
            e->children.push_back(std::move(child));
        }
    }

    std::string s_;
    std::size_t p_ = 0;
};

std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        fail("cannot read " + path.generic_string());
    }
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::unique_ptr<Element> parse_file(const fs::path& path)
{
    return Parser(read_file(path)).document();
}

// ---- text helpers ----

bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
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

// Runs of white space as one space, none at the ends.
std::string one_line(std::string_view s)
{
    std::string out;
    bool        gap = false;
    for (const char c : s)
    {
        if (is_space(c))
        {
            gap = !out.empty();
            continue;
        }
        if (gap)
        {
            out += ' ';
            gap = false;
        }
        out += c;
    }
    return out;
}

std::string text_of(const Element* e)
{
    if (e == nullptr)
    {
        return {};
    }
    std::string out;
    e->append_text(out);
    return strip(out);
}

std::string replace_all(std::string s, std::string_view from, std::string_view to)
{
    for (std::size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
    {
        s.replace(at, from.size(), to);
    }
    return s;
}

// Doxygen's spacing ('std::unique_ptr< T >', 'x=default') back to the headers' style.
std::string tidy(std::string_view code)
{
    // White space as single spaces, and none inside angle brackets.
    std::string s;
    bool        in_space = false;
    for (const char c : code)
    {
        if (is_space(c))
        {
            if (!in_space)
            {
                s += ' ';
            }
            in_space = true;
            continue;
        }
        in_space = false;
        s += c;
    }
    s = replace_all(std::move(s), "< ", "<");
    s = replace_all(std::move(s), " >", ">");

    // 'T &x' / 'T * x' -> 'T& x' / 'T* x'
    std::string t;
    for (std::size_t i = 0; i < s.size();)
    {
        if (s[i] == '&' || s[i] == '*')
        {
            while (!t.empty() && t.back() == ' ')
            {
                t.pop_back();
            }
            while (i < s.size() && (s[i] == '&' || s[i] == '*'))
            {
                t += s[i++];
            }
            while (i < s.size() && s[i] == ' ')
            {
                ++i;
            }
            t += ' ';
            continue;
        }
        t += s[i++];
    }

    // No space after '&' or '*' before ')', ',', '>', '=' or the end.
    std::string u;
    for (std::size_t i = 0; i < t.size(); ++i)
    {
        u += t[i];
        if ((t[i] == '&' || t[i] == '*') && i + 1 < t.size() && t[i + 1] == ' ' &&
            (i + 2 == t.size() || std::string_view("),>=").find(t[i + 2]) != std::string_view::npos))
        {
            ++i;
        }
    }

    // ' = ' around a lone '=' (not '==', '!=', '<=', '>=').
    std::string v;
    for (std::size_t i = 0; i < u.size(); ++i)
    {
        const bool lone = u[i] == '=' && (i == 0 || std::string_view("=!<>").find(u[i - 1]) == std::string_view::npos) &&
                          (i + 1 == u.size() || u[i + 1] != '=');
        v += lone ? std::string(" = ") : std::string(1, u[i]);
    }

    std::string w;
    for (const char c : v)
    {
        if (c == ' ' && !w.empty() && w.back() == ' ')
        {
            continue;
        }
        w += c;
    }
    return strip(w);
}

std::string code_type(const Element* e)
{
    return tidy(text_of(e));
}

// 'D:/.../include/winui/ui/input/Button.hpp' -> 'ui/input/Button.hpp'.
std::string header_of(std::string path)
{
    std::ranges::replace(path, '\\', '/');
    const std::size_t at = path.find("winui/");
    return at == std::string::npos ? path : path.substr(at + 6);
}

int line_of(const Element* location)
{
    return location != nullptr ? std::atoi(location->get("line", "0").c_str()) : 0;
}

// ---- the model ----

struct Member
{
    std::string                 kind; // function | variable | enum | typedef | friend
    std::string                 name;
    std::string                 prot;
    bool                        is_static = false;
    const Element*              type      = nullptr;
    std::string                 argsstring;
    std::string                 definition;
    const Element*              initializer = nullptr;
    const Element*              templ       = nullptr;
    std::string                 virt;
    std::vector<const Element*> enumvalues;
    std::string                 header;
    int                         line = 0;
    std::string                 requires_clause;
};

struct Compound
{
    std::unique_ptr<Element> xml; // owns everything the members point into
    std::string              kind;
    std::string              name;
    std::string              header;
    std::vector<Member>      members;
    std::vector<std::string> bases;
    const Element*           templ = nullptr;
    std::string              prot;
};

Member parse_member(const Element& m)
{
    const Element* loc = m.find("location");
    Member         out;
    out.kind            = m.get("kind");
    out.name            = text_of(m.find("name"));
    out.prot            = m.get("prot", "public");
    out.is_static       = m.get("static") == "yes";
    out.type            = m.find("type");
    out.argsstring      = text_of(m.find("argsstring"));
    out.definition      = text_of(m.find("definition"));
    out.initializer     = m.find("initializer");
    out.templ           = m.find("templateparamlist");
    out.virt            = m.get("virt", "non-virtual");
    out.enumvalues      = m.find_all("enumvalue");
    out.header          = loc != nullptr ? header_of(loc->get("file")) : std::string{};
    out.line            = line_of(loc);
    out.requires_clause = text_of(m.find("requiresclause"));
    return out;
}

std::optional<Compound> parse_compound(const fs::path& xml_dir, const std::string& refid)
{
    Compound c;
    c.xml              = parse_file(xml_dir / (refid + ".xml"));
    const Element* def = c.xml->find("compounddef");
    if (def == nullptr)
    {
        return std::nullopt;
    }
    c.kind             = def->get("kind");
    const Element* loc = def->find("location");
    for (const Element* section : def->find_all("sectiondef"))
    {
        for (const Element* m : section->find_all("memberdef"))
        {
            Member member = parse_member(*m);
            // the positional-init guard every spec and list entry declares first (core/Designated.hpp)
            if (member.name != "designated_only_")
            {
                c.members.push_back(std::move(member));
            }
        }
    }
    // A class/struct compound's <includes> is the header that declares it.
    const Element* inc = def->find("includes");
    if (inc != nullptr)
    {
        c.header = header_of(inc->text);
    }
    else if (loc != nullptr)
    {
        c.header = header_of(loc->get("file"));
    }
    c.name = text_of(def->find("compoundname"));
    for (const Element* base : def->find_all("basecompoundref"))
    {
        c.bases.push_back(text_of(base));
    }
    c.templ = def->find("templateparamlist");
    c.prot  = def->get("prot", "public");
    return c;
}

// The class, struct, namespace and concept compounds in index order.
std::vector<Compound> load_model(const fs::path& xml_dir)
{
    const auto            index = parse_file(xml_dir / "index.xml");
    std::vector<Compound> out;
    for (const Element* c : index->find_all("compound"))
    {
        const std::string kind = c->get("kind");
        if (kind == "class" || kind == "struct" || kind == "namespace" || kind == "concept")
        {
            if (auto compound = parse_compound(xml_dir, c->get("refid")))
            {
                out.push_back(std::move(*compound));
            }
        }
    }
    return out;
}

// ---- rendering ----

std::string template_prefix(const Element* e)
{
    if (e == nullptr)
    {
        return {};
    }
    std::string params;
    for (const Element* p : e->find_all("param"))
    {
        std::string s = strip(code_type(p->find("type")) + " " + text_of(p->find("declname")));
        if (const Element* d = p->find("defval"))
        {
            s += " = " + code_type(d);
        }
        params += (params.empty() ? "" : ", ") + s;
    }
    return "template <" + params + ">\n";
}

std::string signature(const Member& m)
{
    if (m.kind == "typedef")
    {
        return m.definition;
    }
    if (m.kind == "variable")
    {
        std::string init;
        if (m.initializer != nullptr)
        {
            init                    = " " + code_type(m.initializer);
            const std::string first = strip(init);
            if (!first.starts_with("=") && !first.starts_with("{"))
            {
                init = " =" + init;
            }
        }
        return (m.is_static ? "static " : "") + code_type(m.type) + " " + m.name + init;
    }
    if (m.kind == "enum")
    {
        std::string out = "enum class " + m.name;
        if (m.type != nullptr && !text_of(m.type).empty())
        {
            out += " : " + code_type(m.type);
        }
        return out;
    }
    const std::string ret  = code_type(m.type);
    const char*       qual = m.is_static ? "static "
                             : (m.virt != "non-virtual" && m.argsstring.find("override") == std::string::npos)
                                 ? "virtual "
                                 : "";
    std::string       sig  = qual + (ret.empty() ? "" : ret + " ") + m.name + tidy(m.argsstring);
    if (!m.requires_clause.empty())
    {
        sig += "\n    requires " + strip(m.requires_clause);
    }
    return template_prefix(m.templ) + sig;
}

std::vector<std::string> member_lines(const Member& m, const std::string& indent)
{
    const std::string prot = m.prot == "public" ? "" : m.prot + " ";
    if (m.kind == "enum")
    {
        std::vector<std::string> lines{indent + prot + one_line(signature(m))};
        for (const Element* v : m.enumvalues)
        {
            const std::string init = code_type(v->find("initializer"));
            lines.push_back(indent + "    " + text_of(v->find("name")) + (init.empty() ? "" : " " + init));
        }
        return lines;
    }
    if (m.kind == "friend")
    {
        return {};
    }
    std::string sig = one_line(signature(m));
    if (m.kind == "function" && m.virt == "pure-virtual" && sig.find("= 0") == std::string::npos)
    {
        sig += " = 0";
    }
    return {indent + prot + sig};
}

std::vector<std::string> compound_lines(const Compound& c)
{
    std::string head = c.kind + " " + c.name;
    if (c.templ != nullptr)
    {
        head = one_line(template_prefix(c.templ)) + " " + head;
    }
    for (std::size_t i = 0; i < c.bases.size(); ++i)
    {
        head += (i == 0 ? " : " : ", ") + c.bases[i];
    }
    std::vector<std::string>   lines{head + "  [" + c.header + "]"};
    std::vector<const Member*> members;
    for (const Member& m : c.members)
    {
        if (m.prot == "public" || m.prot == "protected")
        {
            members.push_back(&m);
        }
    }
    // Declaration order; members declared in another header (a macro, a base) last.
    std::ranges::stable_sort(members,
                             [&](const Member* a, const Member* b)
                             {
                                 const bool ao = a->header != c.header;
                                 const bool bo = b->header != c.header;
                                 return ao != bo ? bo : a->line < b->line;
                             });
    for (const Member* m : members)
    {
        std::ranges::move(member_lines(*m, "    "), std::back_inserter(lines));
    }
    return lines;
}

std::vector<std::string> namespace_lines(const Compound& c)
{
    std::vector<const Member*> members;
    for (const Member& m : c.members)
    {
        if (m.prot == "public" && m.kind != "friend")
        {
            members.push_back(&m);
        }
    }
    std::ranges::stable_sort(members,
                             [](const Member* a, const Member* b)
                             { return a->header != b->header ? a->header < b->header : a->line < b->line; });
    std::vector<std::string> lines;
    const std::string*       header = nullptr;
    for (const Member* m : members)
    {
        if (header == nullptr || *header != m->header)
        {
            header = &m->header;
            lines.push_back("namespace " + c.name + "  [" + m->header + "]");
        }
        std::ranges::move(member_lines(*m, "    "), std::back_inserter(lines));
    }
    return lines;
}

const std::string Header = "# generated by tools/api_surface.cpp from include/winui; do not edit\n";

std::string surface(const std::vector<const Compound*>& sorted, const std::vector<std::string>& hidden, bool testing)
{
    const auto in_tier = [&](const std::string& name)
    {
        if (!(name == "ui" || name.starts_with("ui::")))
        {
            return false;
        }
        if (std::ranges::any_of(hidden, [&](const std::string& h) { return name.starts_with(h); }))
        {
            return false;
        }
        return name.starts_with("ui::testing") == testing;
    };
    std::string body;
    for (const Compound* c : sorted)
    {
        if (!in_tier(c->name) || c->prot != "public")
        {
            continue;
        }
        for (const std::string& line : c->kind == "namespace" ? namespace_lines(*c) : compound_lines(*c))
        {
            body += line + "\n";
        }
        body += "\n";
    }
    while (!body.empty() && is_space(body.back()))
    {
        body.pop_back();
    }
    return Header + "\n" + body + "\n";
}

// The namespaces control_map.toml's `hidden_namespaces = [...]` lists, less ui::testing (which
// gets its own file).
std::vector<std::string> hidden_namespaces(const fs::path& root)
{
    const std::string toml = read_file(root / "docs/tools/control_map.toml");
    const std::size_t key  = toml.find("\nhidden_namespaces");
    if (key == std::string::npos)
    {
        fail("control_map.toml has no hidden_namespaces");
    }
    const std::size_t        open  = toml.find('[', key);
    const std::size_t        close = toml.find(']', open);
    std::vector<std::string> out;
    for (std::size_t q = toml.find('"', open); q < close; q = toml.find('"', q + 1))
    {
        const std::size_t end  = toml.find('"', q + 1);
        std::string       name = toml.substr(q + 1, end - q - 1);
        if (name != "ui::testing")
        {
            out.push_back(std::move(name));
        }
        q = end;
    }
    return out;
}
} // namespace

int main(int argc, char** argv)
{
    std::optional<fs::path> root;
    std::optional<fs::path> xml_dir;
    bool                    check = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--check")
        {
            check = true;
        }
        else if (arg == "--xml-dir" && i + 1 < argc)
        {
            xml_dir = argv[++i];
        }
        else if (!root && !arg.starts_with("--"))
        {
            root = arg;
        }
        else
        {
            std::fprintf(stderr, "usage: winui_api_surface <repository root> [--xml-dir <dir>] [--check]\n");
            return 2;
        }
    }
    if (!root)
    {
        std::fprintf(stderr, "usage: winui_api_surface <repository root> [--xml-dir <dir>] [--check]\n");
        return 2;
    }
    const std::vector<std::string> hidden    = hidden_namespaces(*root);
    const std::vector<Compound>    compounds = load_model(xml_dir.value_or(*root / "build/doxygen/xml"));
    std::vector<const Compound*>   sorted;
    for (const Compound& c : compounds)
    {
        sorted.push_back(&c);
    }
    std::ranges::stable_sort(sorted, [](const Compound* a, const Compound* b) { return a->name < b->name; });

    bool stale = false;
    for (const auto& [name, testing] : {std::pair{"surface.txt", false}, std::pair{"surface-testing.txt", true}})
    {
        const fs::path    path = *root / "api" / name;
        const std::string text = surface(sorted, hidden, testing);
        if (fs::exists(path) && read_file(path) == text)
        {
            continue;
        }
        stale = true;
        if (!check)
        {
            fs::create_directories(path.parent_path());
            std::ofstream(path, std::ios::binary) << text;
        }
        std::printf("%s: api/%s\n", check ? "stale" : "wrote", name);
    }
    return check && stale ? 1 : 0;
}
