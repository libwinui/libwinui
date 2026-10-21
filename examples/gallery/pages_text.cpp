// SPDX-License-Identifier: MIT
#include "gallery.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cstddef>
#include <cstdint>
#include <cwctype>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

using ui::operator""_fr;

namespace gallery
{
namespace
{
std::string lower(std::string_view text)
{
    return ui::text_analyzer().to_lower(text);
}

std::vector<std::string> split(const std::string& text)
{
    std::vector<std::string> parts;
    std::stringstream        in(text);
    std::string              part;
    while (std::getline(in, part, ' '))
    {
        parts.push_back(part);
    }
    if (parts.empty())
    {
        parts.emplace_back(); // "".Split(" ") is one empty token, which every name contains
    }
    return parts;
}

ui::Color system_color(int index)
{
    const COLORREF c = GetSysColor(index);
    return ui::Color{GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f, 1.0f};
}

// ---- AutoSuggestBox ----

// The AutoSuggestBox sample's list of cats.
constexpr const char* Cats[] = {
    "Abyssinian",
    "Aegean",
    "American Bobtail",
    "American Curl",
    "American Ringtail",
    "American Shorthair",
    "American Wirehair",
    "Aphrodite Giant",
    "Arabian Mau",
    "Asian cat",
    "Asian Semi-longhair",
    "Australian Mist",
    "Balinese",
    "Bambino",
    "Bengal",
    "Birman",
    "Brazilian Shorthair",
    "British Longhair",
    "British Shorthair",
    "Burmese",
    "Burmilla",
    "California Spangled",
    "Chantilly-Tiffany",
    "Chartreux",
    "Chausie",
    "Colorpoint Shorthair",
    "Cornish Rex",
    "Cymric",
    "Cyprus",
    "Devon Rex",
    "Donskoy",
    "Dragon Li",
    "Dwelf",
    "Egyptian Mau",
    "European Shorthair",
    "Exotic Shorthair",
    "Foldex",
    "German Rex",
    "Havana Brown",
    "Highlander",
    "Himalayan",
    "Japanese Bobtail",
    "Javanese",
    "Kanaani",
    "Khao Manee",
    "Kinkalow",
    "Korat",
    "Korean Bobtail",
    "Korn Ja",
    "Kurilian Bobtail",
    "Lambkin",
    "LaPerm",
    "Lykoi",
    "Maine Coon",
    "Manx",
    "Mekong Bobtail",
    "Minskin",
    "Napoleon",
    "Munchkin",
    "Nebelung",
    "Norwegian Forest Cat",
    "Ocicat",
    "Ojos Azules",
    "Oregon Rex",
    "Persian (modern)",
    "Persian (traditional)",
    "Peterbald",
    "Pixie-bob",
    "Ragamuffin",
    "Ragdoll",
    "Raas",
    "Russian Blue",
    "Russian White",
    "Sam Sawet",
    "Savannah",
    "Scottish Fold",
    "Selkirk Rex",
    "Serengeti",
    "Serrade Petit",
    "Siamese",
    "Siberian or\u00B4Siberian Forest Cat",
    "Singapura",
    "Snowshoe",
    "Sokoke",
    "Somali",
    "Sphynx",
    "Suphalak",
    "Thai",
    "Thai Lilac",
    "Tonkinese",
    "Toyger",
    "Turkish Angora",
    "Turkish Van",
    "Turkish Vankedisi",
    "Ukrainian Levkoy",
    "Wila Krungthep",
    "York Chocolate",
};

struct AutoSuggestState
{
    ui::Control<ui::AutoSuggestBox> basic;
    ui::Property<std::string>       basic_output;

    ui::Control<ui::AutoSuggestBox> search;
    std::vector<const Page*>        matches;
    std::string                     typed;
    ui::Property<bool>              details_shown{false};
    ui::Property<std::string>       details_title;
    ui::Property<std::string>       details_subtitle;
};

// Every space-separated token must occur in the page's name
// (ignoring case); names starting with the whole query first, then alphabetical.
std::vector<const Page*> search_controls(const std::string& query)
{
    std::vector<const Page*> found;
    const auto               tokens = split(lower(query));
    for (const Group& group : groups())
    {
        for (const Page& page : group.pages)
        {
            const std::string name = lower(page.name);
            if (std::all_of(tokens.begin(),
                            tokens.end(),
                            [&](const std::string& token) { return name.find(token) != std::string::npos; }))
            {
                found.push_back(&page);
            }
        }
    }
    const std::string q = lower(query);
    std::stable_sort(found.begin(),
                     found.end(),
                     [&](const Page* a, const Page* b)
                     {
                         const bool sa = lower(a->name).starts_with(q);
                         const bool sb = lower(b->name).starts_with(q);
                         if (sa != sb)
                         {
                             return sa;
                         }
                         return std::string_view{a->name} < std::string_view{b->name};
                     });
    return found;
}

void show_control(AutoSuggestState& s, const Page& page)
{
    s.details_title    = std::string{page.name};
    s.details_subtitle = std::string{page.description};
    s.details_shown    = true;
}

ui::Spec auto_suggest_box_page(Context&)
{
    return with_state<AutoSuggestState>(
        [](AutoSuggestState& s)
        {
            return examples({
                example({
                    .title = "A basic autosuggest box.",
                    .code  = R"cpp(
                        ui::Control<ui::AutoSuggestBox> box;
                        ui::Property<std::string>       chosen;

                        ui::HorizontalStack{
                            .content = {
                                ui::AutoSuggestBox{
                                    .width = 300,
                                    .on_text_changed =
                                        [&](const ui::AutoSuggestBoxTextChangedArgs& args)
                                    {
                                        // Choosing a suggestion changes the args.text too; only typing refilters.
                                        if (args.reason != ui::AutoSuggestBoxTextChangeReason::UserInput)
                                        {
                                            return;
                                        }
                                        std::vector<std::string> suitable;
                                        // ... collect the cats whose name contains every typed word ...
                                        if (suitable.empty())
                                        {
                                            suitable.emplace_back("No results found");
                                        }
                                        box->set_suggestions(std::move(suitable));
                                    },
                                    .on_suggestion_chosen = [&](std::size_t index) { chosen = box->suggestions()[index]; },
                                    .control              = box,
                                },
                                ui::Text{.text = chosen},
                            },
                        }
                    )cpp",
                    .controls =
                        {
                            ui::AutoSuggestBox{.width         = 300.0f,
                                               .accessibility = {.name = "Basic AutoSuggestBox"},
                                               .on_text_changed =
                                                   [&s](const ui::AutoSuggestBoxTextChangedArgs& args)
                                               {
                                                   // Only the user's own typing refilters; choosing a suggestion
                                                   // changes the args.text too.
                                                   if (args.reason != ui::AutoSuggestBoxTextChangeReason::UserInput)
                                                   {
                                                       return;
                                                   }
                                                   const auto               tokens = split(lower(args.text));
                                                   std::vector<std::string> suitable;
                                                   for (const char* cat : Cats)
                                                   {
                                                       const std::string name = lower(cat);
                                                       if (std::all_of(tokens.begin(),
                                                                       tokens.end(),
                                                                       [&](const std::string& t)
                                                                       { return name.find(t) != std::string::npos; }))
                                                       {
                                                           suitable.emplace_back(cat);
                                                       }
                                                   }
                                                   if (suitable.empty())
                                                   {
                                                       suitable.emplace_back("No results found");
                                                   }
                                                   s.basic->set_suggestions(std::move(suitable));
                                               },
                                               .on_suggestion_chosen = [&s](std::size_t index)
                                               { s.basic_output = s.basic->suggestions()[index]; },
                                               .control = s.basic},
                            ui::Text{.text               = s.basic_output,
                                     .margin             = ui::Edges{.left = 8.0f},
                                     .vertical_alignment = ui::Alignment::Center},
                        },
                }),
                example({
                    .title   = "An AutoSuggestBox as a search box",
                    .code    = R"cpp(
                        ui::Control<ui::AutoSuggestBox> search;
                        std::vector<const Page*>        matches;

                        ui::AutoSuggestBox{
                            .placeholder_text = "Type a control name",
                            .query_icon       = ui::symbol_glyph(ui::Symbol::Find),
                            .width            = 300,
                            .on_text_changed =
                                [&](const ui::AutoSuggestBoxTextChangedArgs& args)
                            {
                                if (args.reason != ui::AutoSuggestBoxTextChangeReason::UserInput)
                                {
                                    return;
                                }
                                matches = search_controls(args.text);
                                std::vector<std::string> names;
                                for (const Page* page : matches)
                                {
                                    names.emplace_back(page->name);
                                }
                                if (names.empty())
                                {
                                    names.emplace_back("No results found");
                                }
                                search->set_suggestions(std::move(names));
                            },
                            .on_query_submitted =
                                [&](const ui::AutoSuggestBoxQuerySubmittedArgs& args)
                            {
                                // args.chosen_suggestion is None when Enter or the args.query_text icon submitted the typed text.
                                if (args.chosen_suggestion != std::nullopt && args.chosen_suggestion < matches.size())
                                {
                                    show_control(*matches[args.chosen_suggestion]);
                                }
                                else if (!args.query_text.empty())
                                {
                                    // ... search again and show the best match ...
                                }
                            },
                            .control = search,
                        }
                    )cpp",
                    .stacked = true,
                    .controls =
                        {
                            ui::AutoSuggestBox{.placeholder_text     = "Type a control name",
                                               .query_icon           = ui::symbol_glyph(ui::Symbol::Find),
                                               .width                = 300.0f,
                                               .horizontal_alignment = ui::Alignment::Leading,
                                               .on_text_changed =
                                                   [&s](const ui::AutoSuggestBoxTextChangedArgs& args)
                                               {
                                                   // Don't autocomplete the box with "No results found".
                                                   if (args.reason == ui::AutoSuggestBoxTextChangeReason::Suggestion &&
                                                       s.matches.empty())
                                                   {
                                                       s.search->set_text(s.typed);
                                                   }
                                                   if (args.reason != ui::AutoSuggestBoxTextChangeReason::UserInput)
                                                   {
                                                       return;
                                                   }
                                                   s.typed   = args.text;
                                                   s.matches = search_controls(args.text);
                                                   std::vector<std::string> names;
                                                   names.reserve(s.matches.size());
                                                   for (const Page* page : s.matches)
                                                   {
                                                       names.emplace_back(page->name);
                                                   }
                                                   if (names.empty())
                                                   {
                                                       names.emplace_back("No results found");
                                                   }
                                                   s.search->set_suggestions(std::move(names));
                                               },
                                               .on_query_submitted =
                                                   [&s](const ui::AutoSuggestBoxQuerySubmittedArgs& args)
                                               {
                                                   if (args.chosen_suggestion && *args.chosen_suggestion < s.matches.size())
                                                   {
                                                       show_control(s, *s.matches[*args.chosen_suggestion]);
                                                   }
                                                   else if (!args.query_text.empty())
                                                   {
                                                       const auto found = search_controls(args.query_text);
                                                       if (!found.empty())
                                                       {
                                                           show_control(s, *found.front());
                                                       }
                                                   }
                                               },
                                               .control = s.search},
                            ui::Match<bool>{.value   = s.details_shown,
                                            .content = [&s](const bool& shown) -> ui::Spec
                                            {
                                                if (!shown)
                                                {
                                                    return {};
                                                }
                                                return ui::VerticalStack{.margin               = ui::Edges{.top = 8.0f},
                                                                         .horizontal_alignment = ui::Alignment::Leading,
                                                                         .content              = {
                                                                             ui::Text{.text = s.details_title,
                                                                                      .role = ui::TextRole::BodyStrong},
                                                                             ui::Text{.text = s.details_subtitle,
                                                                                      .wrapping = ui::TextWrapping::WrapWholeWords},
                                                                         }};
                                            }},
                        },
                }),
            });
        });
}

// ---- NumberBox ----

struct NumberBoxState
{
    ui::Control<ui::NumberBox> spin;
};

ui::Spec number_box_page(Context&)
{
    return with_state<NumberBoxState>(
        [](NumberBoxState& s)
        {
            return examples({
                example({
                    .title = "A NumberBox that evaluates expressions.",
                    .code  = R"cpp(
                        ui::NumberBox{
                            .header             = "Enter an expression:",
                            .placeholder_text   = "1 + 2^2",
                            .accepts_expression = true,
                        }
                    )cpp",
                    .controls =
                        {
                            ui::NumberBox{.header = "Enter an expression:", .placeholder_text = "1 + 2^2", .accepts_expression = true},
                        },
                }),
                example({
                    .title   = "A NumberBox with a spin button.",
                    .code    = R"cpp(
                        ui::Control<ui::NumberBox> spin;

                        ui::NumberBox{
                            .header                = "Enter an integer:",
                            .value                 = 10.0,
                            .step                  = 10.0,
                            .large_step            = 100.0,
                            .spin_button_placement = ui::NumberBoxSpinButtonPlacementMode::Inline,
                            .control               = spin,
                        }

                        // Later, through the handle:
                        spin->set_spin_button_placement(ui::NumberBoxSpinButtonPlacementMode::Compact);
                    )cpp",
                    .options = ui::RadioButtons{.header         = "SpinButton placement",
                                                .items          = {"Inline", "Compact"},
                                                .selected_index = 0,
                                                .on_selection_changed =
                                                    [&s](std::optional<std::size_t> index)
                                                {
                                                    if (s.spin != nullptr)
                                                    {
                                                        s.spin->set_spin_button_placement(
                                                            index == 1 ? ui::NumberBoxSpinButtonPlacementMode::Compact
                                                                       : ui::NumberBoxSpinButtonPlacementMode::Inline);
                                                    }
                                                }},
                    .controls =
                        {
                            ui::NumberBox{.header                = "Enter an integer:",
                                          .value                 = 10.0,
                                          .step                  = 10.0,
                                          .large_step            = 100.0,
                                          .spin_button_placement = ui::NumberBoxSpinButtonPlacementMode::Inline,
                                          .vertical_alignment    = ui::Alignment::Leading,
                                          .accessibility         = {.name = "NumberBox with spin button"},
                                          .control               = s.spin},
                        },
                }),
                example({
                    .title = "A formatted NumberBox that rounds to the nearest 0.25.",
                    .code  = R"cpp(
                        ui::NumberBox{
                            .header           = "Enter a dollar amount:",
                            .placeholder_text = "0.00",
                            .number_formatter = ui::DecimalFormatter{
                                .integer_digits  = 1,
                                .fraction_digits = 2,
                                .rounder = ui::IncrementNumberRounder{0.25, ui::RoundingAlgorithm::RoundHalfUp},
                            },
                        }
                    )cpp",
                    .controls =
                        {
                            ui::NumberBox{.header           = "Enter a dollar amount:",
                                          .placeholder_text = "0.00",
                                          .number_formatter = ui::
                                              DecimalFormatter{.integer_digits  = 1,
                                                               .fraction_digits = 2,
                                                               .rounder = ui::IncrementNumberRounder{0.25, ui::RoundingAlgorithm::RoundHalfUp}}},
                        },
                }),
            });
        });
}

// ---- PasswordBox ----

struct PasswordBoxState
{
    ui::Control<ui::PasswordBox> simple;
    ui::Property<bool>           not_allowed{false};
    ui::Property<bool>           reveal{false};
};

ui::Spec password_box_page(Context&)
{
    return with_state<PasswordBoxState>(
        [](PasswordBoxState& s)
        {
            return examples({
                example({
                    .title   = "A simple PasswordBox.",
                    .code    = R"cpp(
                        ui::Control<ui::PasswordBox> box;
                        ui::Property<bool>           not_allowed{false};

                        ui::VerticalStack{
                            .content = {
                                ui::PasswordBox{
                                    .width = 300,
                                    .on_password_changed =
                                        [&](const std::string& password)
                                    {
                                        not_allowed = password.empty() || password == "Password";
                                        if (not_allowed.get())
                                        {
                                            box->set_password("");
                                        }
                                    },
                                    .control = box,
                                },
                                ui::Match<bool>{
                                    .value   = not_allowed,
                                    .content = [](const bool& shown) -> ui::Spec
                                    {
                                        if (!shown)
                                        {
                                            return {};
                                        }
                                        return ui::Text{.text = "'Password' is not allowed."};
                                    },
                                },
                            },
                        }
                    )cpp",
                    .stacked = true,
                    .controls =
                        {
                            ui::PasswordBox{.width                = 300.0f,
                                            .horizontal_alignment = ui::Alignment::Leading,
                                            .accessibility        = {.name = "Simple PasswordBox"},
                                            .on_password_changed =
                                                [&s](const std::string& password)
                                            {
                                                if (password.empty() || password == "Password")
                                                {
                                                    s.not_allowed = true;
                                                    s.simple->set_password("");
                                                }
                                                else
                                                {
                                                    s.not_allowed = false;
                                                }
                                            },
                                            .control = s.simple},
                            ui::Match<bool>{.value   = s.not_allowed,
                                            .content = [](const bool& shown) -> ui::Spec
                                            {
                                                if (!shown)
                                                {
                                                    return {};
                                                }
                                                return ui::Text{.text   = "'Password' is not allowed.",
                                                                .margin = ui::Edges{.top = 8.0f}};
                                            }},
                        },
                }),
                example({
                    .title = "A PasswordBox with header, placeholder text and custom character.",
                    .code  = R"cpp(
                        ui::PasswordBox{
                            .header           = "Password",
                            .placeholder_text = "Enter your password",
                            .password_char    = '#',
                            .width            = 300,
                        }
                    )cpp",
                    .controls =
                        {
                            ui::PasswordBox{.header           = "Password",
                                            .placeholder_text = "Enter your password",
                                            .password_char    = '#',
                                            .width            = 300.0f},
                        },
                }),
                example({
                    .title = "A PasswordBox with reveal mode.",
                    .code  = R"cpp(
                        ui::Property<bool> reveal{false};

                        ui::HorizontalStack{
                            .spacing = 8,
                            .content = {
                                ui::PasswordBox{
                                    .reveal_mode = ui::bind(reveal,
                                                            [](bool on)
                                                            {
                                                                return on ? ui::PasswordBoxRevealMode::Visible
                                                                          : ui::PasswordBoxRevealMode::Hidden;
                                                            }),
                                    .width       = 250,
                                },
                                ui::CheckBox{
                                    .text               = "Show password",
                                    .on_checked_changed = [&](std::optional<bool> on) { reveal = on == true; },
                                },
                            },
                        }
                    )cpp",
                    .controls =
                        {
                            ui::PasswordBox{.reveal_mode   = ui::bind(s.reveal,
                                                                      [](bool on)
                                                                      {
                                                                        return on ? ui::PasswordBoxRevealMode::Visible
                                                                                  : ui::PasswordBoxRevealMode::Hidden;
                                                                      }),
                                            .margin        = ui::Edges{.right = 8.0f},
                                            .width         = 250.0f,
                                            .accessibility = {.name = "Sample password box"}},
                            ui::CheckBox{.text               = "Show password",
                                         .on_checked_changed = [&s](std::optional<bool> on) { s.reveal = on == true; }},
                        },
                }),
            });
        });
}

// ---- RichEditBox ----

// The custom editor's font colors, by name.
struct NamedColor
{
    const char* name;
    ui::Color   color;
};
const std::array<NamedColor, 8> FontColors{{
    {"Red", ui::Color::from_argb(0xFFFF0000)},
    {"Orange", ui::Color::from_argb(0xFFFFA500)},
    {"Yellow", ui::Color::from_argb(0xFFFFFF00)},
    {"Green", ui::Color::from_argb(0xFF008000)},
    {"Blue", ui::Color::from_argb(0xFF0000FF)},
    {"Indigo", ui::Color::from_argb(0xFF4B0082)},
    {"Violet", ui::Color::from_argb(0xFFEE82EE)},
    {"Gray", ui::Color::from_argb(0xFF808080)},
}};

constexpr ui::IconGlyph OpenFileGlyph{0xE8E5, "Segoe Fluent Icons", 16.0f};
constexpr ui::IconGlyph SaveGlyph{0xE74E, "Segoe Fluent Icons", 16.0f};
constexpr ui::IconGlyph BoldGlyph{0xE8DD, "Segoe Fluent Icons", 16.0f};
constexpr ui::IconGlyph ItalicGlyph{0xE8DB, "Segoe Fluent Icons", 16.0f};

struct RichEditState
{
    ui::Control<ui::RichEditBox> editor;
    ui::Control<ui::Flyout>      color_flyout;
    ui::Control<ui::TextBox>     find_box;
    // The color last picked from the flyout; none until one is. (The sample starts on
    // Colors.Green, which recolors everything typed after the first letter without a pick.)
    std::optional<ui::Color> current;
    // A swatch was picked: focus goes back to the editor once the flyout has closed.
    bool refocus = false;
};

// Editor_TextChanged: typed text keeps the picked color. Also runs on a caret move, which drops a
// color set at the old position, or the first letter would come out in the wrong color.
void keep_color(RichEditState& s)
{
    if (s.current && !s.editor->has_selection() && s.editor->selection_format().foreground != *s.current)
    {
        s.editor->set_selection_format({.foreground = *s.current});
    }
}

// The toolbar's buttons: RelativePanel.Resources' Button style (no border, transparent,
// 8 DIPs apart) is the subtle button.
ui::Spec tool_button(const char* name, ui::IconGlyph glyph, ui::Callback<void()> on_click)
{
    return ui::Button{.variant       = ui::ButtonVariant::Subtle,
                      .margin        = ui::Edges{.right = 8.0f},
                      .tool_tip      = name,
                      .accessibility = {.name = name},
                      .on_click      = std::move(on_click),
                      .content       = ui::Icon{.source = glyph}};
}

// The whole document back to the editor's own colors.
void remove_highlights(RichEditState& s)
{
    if (s.editor != nullptr)
    {
        s.editor->set_range_format(0,
                                   s.editor->text().size(),
                                   {.foreground = ui::colors::Transparent, .background = ui::colors::Transparent});
    }
}

// Every match in the theme's selection color under its selection text color.
void highlight_matches(RichEditState& s)
{
    remove_highlights(s);
    if (s.editor == nullptr || s.find_box == nullptr)
    {
        return;
    }
    const ui::Color background = system_color(COLOR_HIGHLIGHT);
    const ui::Color foreground = system_color(COLOR_HIGHLIGHTTEXT);
    for (const auto& [start, end] : s.editor->find_all(s.find_box->text()))
    {
        s.editor->set_range_format(start, end, {.foreground = foreground, .background = background});
    }
}

std::optional<std::string> read_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool write_file(const std::filesystem::path& path, const std::string& bytes)
{
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

// Open and Save: an .rtf in, an .rtf out.
ui::Spec file_buttons(RichEditState& s, ui::Window& window, ui::Property<std::string>& status)
{
    return ui::HorizontalStack{
        .content = {
            tool_button("Open file",
                        OpenFileGlyph,
                        [&s, &window]
                        {
                            if (auto path = ui::pick_file(window,
                                                          {.title = "Open",
                                                           .file_types = {{.name = "Rich Text", .extensions = {".rtf"}}}}))
                            {
                                if (auto rtf = read_file(*path))
                                {
                                    s.editor->load_rtf(*rtf);
                                }
                            }
                        }),
            tool_button("Save file",
                        SaveGlyph,
                        [&s, &window, &status]
                        {
                            auto path = ui::pick_save_file(window,
                                                           {.title = "Save",
                                                            .file_types = {{.name = "Rich Text", .extensions = {".rtf"}}},
                                                            .suggested_name = "New Document.rtf"});
                            if (!path)
                            {
                                return;
                            }
                            if (path->extension() != ".rtf")
                            {
                                *path += ".rtf";
                            }
                            if (!write_file(*path, s.editor->save_rtf()))
                            {
                                status = "RichEditBox: the file couldn't be saved.";
                            }
                        }),
        }};
}

// The font color flyout: three to a row, 32x32 swatches in 6 DIPs of margin. A pick colors
// the selection, or what is typed next, and hands focus back to the editor.
ui::Spec color_flyout(RichEditState& s, ui::Window& window)
{
    ui::CellList cells;
    for (std::size_t i = 0; i < FontColors.size(); ++i)
    {
        const NamedColor named = FontColors[i];
        cells.cells.push_back(
            {i / 3,
             i % 3,
             1,
             1,
             ui::build(ui::Button{.padding       = ui::Thickness{0.0f},
                                  .margin        = ui::Thickness{6.0f},
                                  .accessibility = {.name = named.name},
                                  .on_click =
                                      [&s, named]
                                  {
                                      s.editor->set_selection_format({.foreground = named.color});
                                      s.current = named.color;
                                      // editor.Focus() after Flyout.Hide(): focus stays inside an
                                      // open flyout, so the editor takes it back once it has closed.
                                      s.refocus = true;
                                      if (s.color_flyout != nullptr)
                                      {
                                          s.color_flyout->hide();
                                      }
                                  },
                                  .content = Swatch{.fill = named.color, .width = 32.0f, .height = 32.0f}})});
    }
    // The font color button: a subtle DropdownButton with its own tool_tip. A Flyout opens only from a button
    // that is its direct anchor; a ToolTip element wrapped around it would hide the Flyout.
    ui::Spec button = ui::DropdownButton{.variant       = ui::ButtonVariant::Subtle,
                                         .tool_tip      = "Font color",
                                         .accessibility = {.name = "Font color"},
                                         .content       = ui::Icon{.source = ui::symbol_glyph(ui::Symbol::FontColor)}};
    ui::Spec flyout = ui::Flyout{.placement = ui::PopupPlacement::Below,
                                 .control   = s.color_flyout,
                                 .anchor    = std::move(button),
                                 .content   = ui::Grid{.columns = {ui::AutoSize, ui::AutoSize, ui::AutoSize},
                                                       .rows    = {ui::AutoSize, ui::AutoSize, ui::AutoSize},
                                                       .content = std::move(cells)}};
    s.color_flyout->on_closed(
        [&s, &window](ui::PopupCloseReason)
        {
            if (std::exchange(s.refocus, false))
            {
                window.focus_element(s.editor);
            }
        });
    return flyout;
}

ui::Spec rich_edit_box_page(Context& ctx)
{
    ui::Window& window = ctx.window;
    auto&       status = ctx.status;
    return with_state<RichEditState>(
        [&window, &status](RichEditState& s)
        {
            // "Share" added to the end of the context menu.
            ui::Control<ui::RichEditBox> custom_menu;
            std::unique_ptr<ui::Element> custom = ui::build(
                ui::RichEditBox{.width         = 800.0f,
                                .height        = 200.0f,
                                .accessibility = {.name = "editor with custom menu"},
                                .control       = custom_menu});
            custom_menu->add_context_menu_item("Share", [&status] { status = "RichEditBox: Share"; });

            ui::Spec formatting = ui::HorizontalStack{
                .content = {
                    tool_button("Bold",
                                BoldGlyph,
                                [&s, &window]
                                {
                                    s.editor->toggle_bold();
                                    window.focus_element(s.editor);
                                }),
                    tool_button("Italic",
                                ItalicGlyph,
                                [&s, &window]
                                {
                                    s.editor->toggle_italic();
                                    window.focus_element(s.editor);
                                }),
                    color_flyout(s, window),
                }};
            ui::Spec editor = ui::RichEditBox{
                .height               = 200.0f,
                .min_width            = 300.0f,
                .accessibility        = {.name = "Custom editor"},
                .on_text_changed      = [&s] { keep_color(s); },
                .on_selection_changed = [&s] { keep_color(s); },
                .control              = s.editor,
            };
            ui::Spec find = ui::HorizontalStack{.margin  = ui::Edges{.top = 10.0f},
                                                .content = {
                                                    ui::Text{.text               = "Find:",
                                                             .margin             = ui::Edges{.bottom = 4.0f},
                                                             .vertical_alignment = ui::Alignment::Center},
                                                    ui::TextBox{.placeholder_text = "Enter search text",
                                                                .margin           = ui::Edges{.left = 10.0f},
                                                                .width            = 224.0f,
                                                                .on_text_changed  = [&s](const std::string&)
                                                                { highlight_matches(s); },
                                                                .on_got_focus  = [&s] { highlight_matches(s); },
                                                                .on_lost_focus = [&s] { remove_highlights(s); },
                                                                .control       = s.find_box},
                                                }};

            return examples({
                example({
                    .title    = "A simple text editor using RichEditBox.",
                    .code     = R"cpp(
                        ui::RichEditBox{.accessibility = {.name = "simple text editor"}}
                    )cpp",
                    .stretch  = true,
                    .controls = {ui::RichEditBox{.accessibility = {.name = "simple text editor"}}},
                }),
                example({
                    .title    = "Customizing RichEditBox's CommandBarFlyout - Adding 'Share'",
                    .code     = R"cpp(
                        ui::Control<ui::RichEditBox> editor;

                        std::unique_ptr<ui::Element> built = ui::build(ui::RichEditBox{
                            .width   = 800,
                            .height  = 200,
                            .control = editor,
                        });

                        // Added to the end of the editor's context menu.
                        editor->add_context_menu_item("Share", [&] { status = "RichEditBox: Share"; });
                    )cpp",
                    .controls = {std::move(custom)},
                }),
                example({
                    .title   = "A custom editor with RichEditBox.",
                    .code    = R"cpp(
                        using ui::operator""_fr;

                        ui::Control<ui::RichEditBox> editor;

                        ui::Grid{
                            .columns = {1_fr, ui::AutoSize},
                            .rows    = {ui::AutoSize, ui::AutoSize, ui::AutoSize},
                            .content   = {
                                // ... row 0, column 0: open and save buttons, which call
                                // editor->load_rtf(rtf) and editor->save_rtf() ...
                                ui::Cell{.row     = 0,
                                 .column  = 1,
                                 .content = ui::HorizontalStack{.content = {
                                     ui::Button{.variant  = ui::ButtonVariant::Subtle,
                                                .tool_tip = "Bold",
                                                .on_click = [&]
                                                {
                                                    editor->toggle_bold();
                                                    window.focus_element(editor);
                                                },
                                                .content = ui::Icon{.source = bold_glyph}},
                                     // ... Italic calls editor->toggle_italic(); a color swatch calls
                                     // editor->set_selection_format({.foreground = color}) ...
                                 }}},
                                ui::Cell{.row         = 1,
                                 .column      = 0,
                                 .column_span = 2,
                                 .content     = ui::RichEditBox{.height = 200, .min_width = 300, .control = editor}},
                                ui::Cell{.row         = 2,
                                 .column      = 0,
                                 .column_span = 2,
                                 .content     = ui::TextBox{
                                     .placeholder_text = "Enter search text",
                                     .width            = 224,
                                     .on_text_changed  = [&](const std::string& text)
                                     {
                                         for (const auto& [start, end] : editor->find_all(text))
                                         {
                                             editor->set_range_format(
                                                 start, end, {.foreground = highlight_text, .background = highlight});
                                         }
                                     },
                                 }},
                            },
                        }
                    )cpp",
                    .stretch = true,
                    .controls =
                        {ui::Grid{.columns = {1_fr, ui::AutoSize},
                                  .rows    = {ui::AutoSize, ui::AutoSize, ui::AutoSize},
                                  .margin  = ui::Edges{.bottom = 20.0f},
                                  .content =
                                      {
                                          ui::Cell{.row = 0, .column = 0, .content = file_buttons(s, window, status)},
                                          ui::Cell{.row = 0, .column = 1, .content = std::move(formatting)},
                                          ui::Cell{.row = 1, .column = 0, .column_span = 2, .content = std::move(editor)},
                                          ui::Cell{.row = 2, .column = 0, .column_span = 2, .content = std::move(find)},
                                      }}},
                }),
            });
        });
}

// ---- RichText ----

constexpr const char* LinkedIntro =
    "Linked text containers allow text which does not fit in one element to overflow into a different element on "
    "the page. Creative use of linked text containers enables basic multicolumn support and other advanced page "
    "layouts.";
constexpr const char* LinkedLorem =
    "Duis sed nulla metus, id hendrerit velit. Curabitur dolor purus, bibendum eu cursus lacinia, interdum vel "
    "augue. Aenean euismod eros et sapien vehicula dictum. Duis ullamcorper, turpis nec feugiat tincidunt, dui erat "
    "luctus risus, aliquam accumsan lacus est vel quam. Nunc lacus massa, varius eget accumsan id, congue sed orci. "
    "Duis dignissim hendrerit egestas. Proin ut turpis magna, sit amet porta erat. Nunc semper metus nec magna "
    "imperdiet nec vestibulum dui fringilla. Sed sed ante libero, nec porttitor mi. Ut luctus, neque vitae placerat "
    "egestas, urna leo auctor magna, sit amet ultricies ipsum felis quis sapien. Proin eleifend varius dui, at "
    "vestibulum nunc consectetur nec. Mauris nulla elit, ultrices a sodales non, aliquam ac est. Quisque sit amet "
    "risus nulla. Quisque vestibulum posuere velit, vitae vestibulum eros scelerisque sit amet. In in risus est, at "
    "laoreet dolor. Nullam aliquet pellentesque convallis. Ut vel tincidunt nulla. Mauris auctor tincidunt auctor. "
    "Aenean orci ante, vulputate ac sagittis sit amet, consequat at mi. Morbi elementum purus consectetur nisi "
    "adipiscing vitae blandit sapien placerat. Aliquam adipiscing tortor non sem lobortis consectetur mattis felis "
    "rhoncus. Nunc eu nunc rhoncus arcu sollicitudin ultrices. In vulputate eros in mauris aliquam id dignissim nisl "
    "laoreet.";

struct RichTextState
{
    ui::Control<ui::RichText>         linked;
    ui::Control<ui::RichTextOverflow> first;
    ui::Control<ui::RichTextOverflow> second;
    ui::Property<ui::Color>           highlight{ui::Color::from_argb(0xFFFFFF00)};
};

ui::Spec rich_text_page(Context&)
{
    return with_state<RichTextState>(
        [](RichTextState& s)
        {
            std::unique_ptr<ui::Element> page = ui::build(examples({
                example({
                    .title    = "A simple RichText.",
                    .code     = R"cpp(
                        ui::RichText{.blocks = {{.inlines = {{.text = "I am a RichText."}}}}}
                    )cpp",
                    .controls = {ui::RichText{.blocks = {{.inlines = {{.text = "I am a RichText."}}}}}},
                }),
                example({
                    .title = "A RichText with a custom selection highlight color.",
                    .code  = R"cpp(
                        ui::RichText{
                            .blocks = {
                                {.inlines = {
                                     {.text = "RichText provides a rich text display container that supports "},
                                     {.text = "formatted text", .weight = ui::FontWeight::Bold, .italic = true},
                                     {.text = ", "},
                                     {.text = "hyperlinks", .navigate_uri = "https://learn.microsoft.com/"},
                                     {.text = ", inline images, and other rich content."},
                                 }},
                                {.inlines = {{.text = "RichText also supports a built-in overflow model."}}},
                            },
                            .selection_highlight_color = ui::Color::from_argb(0xFF008000),
                        }
                    )cpp",
                    .controls =
                        {
                            ui::RichText{
                                .blocks =
                                    {
                                        {.inlines =
                                             {
                                                 {.text = "RichText provides a rich text display container that supports "},
                                                 {.text = "formatted text", .weight = ui::FontWeight::Bold, .italic = true},
                                                 {.text = ", "},
                                                 {.text         = "hyperlinks",
                                                  .navigate_uri = "https://example.com"},
                                                 {.text = ", inline images, and other rich content."},
                                             }},
                                        {.inlines = {{.text = "RichText also supports a built-in overflow model."}}},
                                    },
                                .selection_highlight_color = ui::Color::from_argb(0xFF008000)},
                        },
                }),
                example({
                    .title   = "A RichText with overflow.",
                    .code    = R"cpp(
                        using ui::operator""_fr;

                        ui::Control<ui::RichText>         text;
                        ui::Control<ui::RichTextOverflow> first;
                        ui::Control<ui::RichTextOverflow> second;

                        window.set_content(ui::Grid{
                            .columns = {1_fr, 1_fr, 1_fr},
                            .rows    = {1_fr},
                            .height  = 300,
                            .content   = {
                                ui::Cell{.row     = 0,
                                 .column  = 0,
                                 .content = ui::RichText{.blocks         = {{.inlines = {{.text = intro}}},
                                                                            {.inlines = {{.text = lorem}}}},
                                                         .overflow       = first,
                                                         .text_alignment = ui::TextAlignment::Justify, // what does not fit flows on
                                                         .control        = text}},
                                ui::Cell{.row = 0, .column = 1, .content = ui::RichTextOverflow{.overflow = second, .control = first}},
                                ui::Cell{.row = 0, .column = 2, .content = ui::RichTextOverflow{.control = second}},
                            },
                        });
                    )cpp",
                    .stretch = true,
                    .controls =
                        {
                            ui::Grid{.columns = {1_fr, 1_fr, 1_fr},
                                     .rows    = {1_fr},
                                     .height  = 300.0f,
                                     .content   = {
                                         ui::Cell{.row     = 0,
                                          .column  = 0,
                                          .content = ui::RichText{.blocks         = {{.inlines = {{.text = LinkedIntro}}},
                                                                                     {.inlines = {{.text = LinkedLorem}}}},
                                                                  .overflow       = s.first,
                                                                  .text_alignment = ui::TextAlignment::Justify,
                                                                  .margin         = ui::Symmetric{.horizontal = 12.0f, .vertical = 0.0f},
                                                                  .control            = s.linked}},
                                         ui::Cell{.row     = 0,
                                          .column  = 1,
                                          .content = ui::RichTextOverflow{.overflow      = s.second,
                                                                          .margin        = ui::Symmetric{.horizontal = 12.0f, .vertical = 0.0f},
                                                                          .accessibility = {.name = "First overflow container"},
                                                                          .control           = s.first}},
                                         ui::Cell{.row     = 0,
                                          .column  = 2,
                                          .content = ui::RichTextOverflow{.margin        = ui::Symmetric{.horizontal = 12.0f, .vertical = 0.0f},
                                                                          .accessibility = {.name = "Second overflow container"},
                                                                          .control           = s.second}},
                                     }},
                        },
                }),
                example({
                    .title   = "RichText with custom text highlighters",
                    .code    = R"cpp(
                        ui::Property<ui::Color> highlight{ui::Color::from_argb(0xFFFFFF00)};

                        ui::RichText{
                            .blocks = {{.inlines = {{.text = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, "
                                                             "sed do eiusmod tempor incididunt ut labore et dolore "
                                                             "magna aliqua"}}}},
                            .text_highlighters = ui::bind(highlight,
                                                          [](const ui::Color& color)
                                                          {
                                                              // Start 28, length 11: "consectetur".
                                                              return std::vector<ui::TextHighlighter>{
                                                                  ui::TextHighlighter{.ranges = {{28, 11}}, .background = color, .foreground = std::nullopt}};
                                                          }),
                        }

                        // Picking another color repaints the range:
                        highlight = ui::Color::from_argb(0xFFFF0000);
                    )cpp",
                    .options = ui::ComboBox{.header         = "Text highlighting color",
                                            .items          = {"Yellow", "Red", "Blue"},
                                            .selected_index = 0,
                                            .on_selection_changed =
                                                [&s](std::optional<std::size_t> index)
                                            {
                                                constexpr std::array<std::uint32_t, 3> Colors{0xFFFFFF00, 0xFFFF0000, 0xFF0000FF};
                                                s.highlight = ui::Color::from_argb(Colors[std::min<std::size_t>(index.value_or(0), 2)]);
                                            }},
                    .controls =
                        {
                            ui::RichText{
                                .blocks = {{.inlines = {{.text = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do "
                                                                 "eiusmod tempor incididunt ut labore et dolore magna aliqua"}}}},
                                .text_highlighters = ui::bind(s.highlight,
                                                          [](const ui::Color& color)
                                                          {
                                                              // From character 28, 11 long: "consectetur".
                                                              return std::vector<ui::TextHighlighter>{
                                                                  ui::TextHighlighter{.ranges = {{28, 11}}, .background = color, .foreground = std::nullopt}};
                                                          })},
                        },
                }),
            }));
            return page;
        });
}

// ---- Text ----

struct TextState
{
    ui::Property<bool> selectable{false};
};

ui::Spec text_page(Context&)
{
    return with_state<TextState>(
        [](TextState& s)
        {
            // A custom text style: Comic Sans MS, italic.
            ui::TextStyle comic{14.0f, ui::TextAlign::Leading, ui::TextAlign::Center, "Comic Sans MS"};
            comic.italic = true;
            ui::TextStyle arial{24.0f, ui::TextAlign::Leading, ui::TextAlign::Center, "Arial"};
            arial.italic = true;
            return examples({
                example({
                    .title    = "A simple Text.",
                    .code     = R"cpp(
                        ui::Text{.text = "I am a Text."}
                    )cpp",
                    .controls = {ui::Text{.text = "I am a Text."}},
                }),
                example({
                    .title    = "A Text with a style applied.",
                    .code     = R"cpp(
                        ui::TextStyle comic{14.0f, ui::TextAlign::Leading, ui::TextAlign::Center, "Comic Sans MS"};
                        comic.italic = true;

                        ui::Text{.text = "I am a styled Text.", .text_style = comic}
                    )cpp",
                    .controls = {ui::Text{.text = "I am a styled Text.", .text_style = comic}},
                }),
                example({
                    .title = "A Text with various properties set.",
                    .code  = R"cpp(
                        ui::TextStyle arial{24.0f, ui::TextAlign::Leading, ui::TextAlign::Center, "Arial"};
                        arial.italic = true;

                        ui::Text{
                            .text              = "I am super excited to be here!",
                            .text_style        = arial,
                            .wrapping          = ui::TextWrapping::WrapWholeWords,
                            .character_spacing = 200,
                            .foreground        = ui::Color::from_argb(0xFF6495ED),
                        }
                    )cpp",
                    .controls =
                        {
                            ui::Text{.text              = "I am super excited to be here!",
                                     .text_style        = arial,
                                     .wrapping          = ui::TextWrapping::WrapWholeWords,
                                     .character_spacing = 200,
                                     .foreground        = ui::Color::from_argb(0xFF6495ED)}, // CornflowerBlue
                        },
                }),
                example({
                    .title = "A Text with inline text elements.",
                    .code  = R"cpp(
                        ui::Text{
                            .inlines = {
                                {.text        = "The text of a Text does not have to be a simple string.",
                                 .font_family = "Times New Roman",
                                 .color       = ui::Color::from_argb(0xFFA9A9A9)},
                                ui::line_break(),
                                {.text = "Text can be "},
                                {.text = "bold", .weight = ui::FontWeight::Bold},
                                {.text = ", "},
                                {.text = "italic", .italic = true},
                                {.text = ", or "},
                                {.text = "underlined", .underline = true},
                                {.text = "."},
                            },
                        }
                    )cpp",
                    .controls =
                        {
                            ui::Text{.inlines =
                                         {
                                             {.text        = "The text of a Text does not have to be a simple string.",
                                              .font_family = "Times New Roman",
                                              .color       = ui::Color::from_argb(0xFFA9A9A9)}, // DarkGray
                                             ui::line_break(),
                                             {.text = "Text can be "},
                                             {.text = "bold", .weight = ui::FontWeight::Bold},
                                             {.text = ", "},
                                             {.text = "italic", .italic = true},
                                             {.text = ", or "},
                                             {.text = "underlined", .underline = true},
                                             {.text = "."},
                                         }},
                        },
                }),
                example({
                    .title   = "A selectable Text",
                    .code    = R"cpp(
                        ui::Property<bool> selectable{false};

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {
                                ui::Text{
                                    .text                      = "I am a selectable Text with a custom selection highlight color.",
                                    .selection_enabled         = selectable,
                                    .selection_highlight_color = ui::Color::from_argb(0xFFFF8C00),
                                },
                                ui::ToggleSwitch{
                                    .header     = "Allow selection",
                                    .on_checked_changed = [&](bool on) { selectable = on; },
                                },
                            },
                        }
                    )cpp",
                    .options = ui::ToggleSwitch{.header             = "Allow selection",
                                                .on_checked_changed = [&s](bool on) { s.selectable = on; }},
                    .controls =
                        {
                            ui::Text{.text = "I am a selectable Text with a custom selection highlight color.",
                                     .selection_enabled         = s.selectable,
                                     .selection_highlight_color = ui::Color::from_argb(0xFFFF8C00)}, // DarkOrange
                        },
                }),
            });
        });
}

// ---- TextBox ----

struct TextBoxPageState
{
    ui::Property<ui::CharacterCasing> casing{ui::CharacterCasing::Upper};
    ui::Property<std::string>         email{"ada.example.com"};
};

ui::Spec text_box_page(Context&)
{
    return with_state<TextBoxPageState>(
        [](TextBoxPageState& s)
        {
            return examples({
                example({
                    .title    = "A simple TextBox.",
                    .code     = R"cpp(
                        ui::TextBox{.width = 300, .accessibility = {.name = "simple TextBox"}}
                    )cpp",
                    .controls = {ui::TextBox{.width = 300.0f, .accessibility = {.name = "simple TextBox"}}},
                }),
                example({
                    .title    = "A TextBox with a header and placeholder text.",
                    .code     = R"cpp(
                        ui::TextBox{.header = "Enter your name:", .placeholder_text = "Name"}
                    )cpp",
                    .controls = {ui::TextBox{.header = "Enter your name:", .placeholder_text = "Name"}},
                }),
                example({
                    .title    = "A TextBox with a description.",
                    .code     = R"cpp(
                        ui::TextBox{
                            .header           = "Email address",
                            .description      = "We send the receipt to this address.",
                            .placeholder_text = "name@example.com",
                            .width            = 300,
                        }
                    )cpp",
                    .controls = {ui::TextBox{.header           = "Email address",
                                             .description      = "We send the receipt to this address.",
                                             .placeholder_text = "name@example.com",
                                             .width            = 300.0f}},
                }),
                example({
                    .title    = "A TextBox with an error.",
                    .code     = R"cpp(
                        ui::Property<std::string> email{"ada.example.com"};

                        ui::TextBox{
                            .header      = "Email address",
                            .description = "We send the receipt to this address.",
                            .error       = ui::bind(email, [](const std::string& e) {
                                return e.find('@') == std::string::npos ? "Enter an email address." : "";
                            }),
                            .text        = email,
                            .width       = 300,
                        }
                    )cpp",
                    .controls = {ui::TextBox{
                        .header      = "Email address",
                        .description = "We send the receipt to this address.",
                        .error = ui::bind(s.email,
                                          [](const std::string& e)
                                          { return e.find('@') == std::string::npos ? "Enter an email address." : ""; }),
                        .text  = s.email,
                        .width = 300.0f,
                    }},
                }),
                example({
                    .title = "A read-only TextBox with various properties set.",
                    .code  = R"cpp(
                        ui::TextBox{
                            .text              = "I am super excited to be here!",
                            .read_only         = true,
                            .font_family       = "Arial",
                            .font_size         = 24,
                            .italic            = true,
                            .character_spacing = 200,
                            .foreground        = ui::Color::from_argb(0xFF5178BE),
                        }
                    )cpp",
                    .controls =
                        {
                            ui::TextBox{.text              = "I am super excited to be here!",
                                        .read_only         = true,
                                        .font_family       = "Arial",
                                        .font_size         = 24.0f,
                                        .italic            = true,
                                        .character_spacing = 200,
                                        .foreground        = ui::Color::from_argb(0xFF5178BE),
                                        .accessibility     = {.name = "customized TextBox"}},
                        },
                }),
                example({
                    .title = "A multi-line TextBox with spell checking and custom selection highlight color.",
                    .code  = R"cpp(
                        ui::TextBox{
                            .accepts_return            = true,
                            .wrapping                  = ui::TextWrapping::Wrap,
                            .selection_highlight_color = ui::Color::from_argb(0xFF008000),
                            .min_width                 = 400,
                        }
                    )cpp",
                    .controls =
                        {
                            ui::TextBox{.accepts_return            = true,
                                        .wrapping                  = ui::TextWrapping::Wrap,
                                        .selection_highlight_color = ui::Color::from_argb(0xFF008000),
                                        .min_width                 = 400.0f,
                                        .accessibility             = {.name = "multi-line TextBox"}},
                        },
                }),
                example({
                    .title    = "A TextBox that changes the case of what is typed.",
                    .code     = R"cpp(
                        ui::Property<ui::CharacterCasing> casing{ui::CharacterCasing::Upper};

                        ui::TextBox{
                            .placeholder_text = "Type, or paste, some text",
                            .character_casing = casing,
                            .width            = 300,
                        }
                    )cpp",
                    .options  = ui::RadioButtons{.header         = "Character casing",
                                                 .items          = {"Normal", "Lower", "Upper"},
                                                 .selected_index = ui::bind_choice(s.casing,
                                                                                   {ui::CharacterCasing::Normal,
                                                                                    ui::CharacterCasing::Lower,
                                                                                    ui::CharacterCasing::Upper})},
                    .controls = {ui::TextBox{.placeholder_text = "Type, or paste, some text",
                                             .character_casing = s.casing,
                                             .width            = 300.0f,
                                             .accessibility    = {.name = "TextBox with character casing"}}},
                }),
            });
        });
}

} // namespace

std::span<const Page> text_pages()
{
    static const auto Pages = std::to_array<Page>({
        {"AutoSuggestBox", "A control to provide suggestions as a user is typing.", auto_suggest_box_page},
        {"NumberBox", "A text control used for numeric input and evaluation of algebraic equations.", number_box_page},
        {"PasswordBox", "A control for entering passwords.", password_box_page},
        {"RichEditBox",
         "A rich text editing control that supports formatted text, hyperlinks, and other rich content.",
         rich_edit_box_page},
        {"RichText", "A control that displays formatted text, hyperlinks, inline images, and other rich content.", rich_text_page},
        {"Text", "A lightweight control for displaying small amounts of text.", text_page},
        {"TextBox", "A single-line or multi-line plain text field.", text_box_page},
    });
    return Pages;
}

} // namespace gallery
