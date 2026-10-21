// SPDX-License-Identifier: MIT
#include "gallery.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <cstddef>

// Stock Win32 controls draw as comctl32 v6 only when the manifest asks for it. Without it the
// EDIT has no placeholder and the calendar and date picker look like Windows 2000.
#ifdef _MSC_VER
#pragma comment(linker,                                                                                          \
                "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' " \
                "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif


// Only the grid literals. `using namespace ui` would make every control name ambiguous
// with its spec (ui::Button vs ui::ButtonControl).
using ui::operator""_fr;

namespace
{
// Segoe Fluent Icons names. Both codepoints exist in Segoe MDL2 Assets too, so Windows 10 era
// variants draw the same symbols.
constexpr ui::IconGlyph LightGlyph{0xE706};
constexpr ui::IconGlyph DarkGlyph{0xE708};

constexpr ui::ThemeVariant ThemeVariants[] =
    {ui::ThemeVariant::Fluent2,
     ui::ThemeVariant::Fluent,
     ui::ThemeVariant::Mdl2,
     ui::ThemeVariant::Metro,
     ui::ThemeVariant::Aero,
     ui::ThemeVariant::Luna,
     ui::ThemeVariant::Windows9x,
     ui::ThemeVariant::Windows3x,
     ui::ThemeVariant::Windows1x};
constexpr const char* ThemeVariantNames[] =
    {"Fluent 2 - Windows 11",
     "Fluent - Windows 10",
     "MDL2 - Windows 10",
     "MDL - Windows 8 & 8.1",
     "Aero - Windows Vista & 7",
     "Luna - Windows XP",
     "Windows 9x",
     "Windows 3.x",
     "Windows 1.x & 2.x"};

// Every page in navigation order. The content Switch is keyed on an index into this vector.
std::vector<const gallery::Page*> flatten_pages()
{
    std::vector<const gallery::Page*> pages;
    for (const gallery::Group& group : gallery::groups())
    {
        for (const gallery::Page& page : group.pages)
        {
            pages.push_back(&page);
        }
    }
    return pages;
}

// One NavigationShell with a collapsible item per group.
// Group rows do not select, so pane item index and page index differ; NavMaps maps both ways.
struct NavMaps
{
    std::vector<std::optional<std::size_t>> page_of_item;  // pane item -> page; unset for a group heading
    std::vector<std::size_t>                item_of_page;  // page -> pane item
    std::vector<std::size_t>                group_of_page; // page -> its group heading's pane item
};

std::vector<ui::NavEntry> build_nav_items(NavMaps& maps)
{
    std::vector<ui::NavEntry> items;
    bool                      first = true;
    for (const gallery::Group& group : gallery::groups())
    {
        ui::NavEntry heading{};
        heading.text = group.title;
        heading.icon = gallery::group_icon(group.title);
        // Only the first group starts open, or the pane is one long wall of names again.
        heading.expanded               = first;
        heading.selects_on_invoke      = false;
        const std::size_t heading_item = maps.page_of_item.size();
        maps.page_of_item.push_back(std::nullopt);

        for (const gallery::Page& page : group.pages)
        {
            ui::NavEntry row{};
            row.text = page.name;
            row.icon = gallery::page_icon(page.name);
            heading.children.push_back(std::move(row));
            const std::size_t item = maps.page_of_item.size();
            maps.page_of_item.push_back(maps.item_of_page.size());
            maps.item_of_page.push_back(item);
            maps.group_of_page.push_back(heading_item);
        }

        items.push_back(std::move(heading));
        first = false;
    }
    return items;
}

std::unique_ptr<ui::Element> build_nav_pane(ui::Property<std::size_t>&   selected,
                                            ui::Property<int>&           revealed,
                                            std::unique_ptr<ui::Element> content)
{
    auto    nav  = std::make_unique<ui::NavigationShellControl>();
    NavMaps maps = {};
    nav->set_pane_width(280.0f);
    // No back button, no pane toggle. No Settings page either.
    nav->set_pane_display_mode(ui::NavigationShellPaneDisplayMode::Left);
    nav->set_back_button_visible(ui::AutoVisibility::Collapsed);
    nav->set_pane_toggle_button_visible(false);
    nav->set_settings_visible(false);
    nav->set_items(build_nav_items(maps));
    nav->set_content(std::move(content));

    ui::NavigationShellControl* view = nav.get();
    view->set_selected_index(maps.item_of_page[selected.get()]);

    // Installed after the seed, so priming the pane never reports a selection the user did not
    // make.
    view->on_selection_changed(
        [&selected, view, page_of_item = maps.page_of_item](const std::string&)
        {
            const std::optional<std::size_t> item = view->selected_index();
            if (item && *item < page_of_item.size() && page_of_item[*item])
            {
                selected = *page_of_item[*item];
            }
        });
    // A page reached from elsewhere (a search, a "see the X sample" link) opens its group first, so
    // the selection shows, and the pane scrolls to it. The search results page is no pane item, so
    // it clears the selection. `revealed` ticks on every such visit, the page already open included.
    const auto show = [view, item_of_page = maps.item_of_page, group_of_page = maps.group_of_page](std::size_t page)
    {
        if (page < item_of_page.size())
        {
            view->set_expanded(group_of_page[page], true);
            view->set_selected_index(item_of_page[page]);
        }
        else
        {
            view->set_selected_index(std::nullopt);
        }
    };
    view->add_subscription(ui::observe([show](std::size_t page, int) { show(page); }, selected, revealed));

    return nav;
}

// An item page: title, description, the page's paragraph, then its cards.
ui::Spec page_view(const gallery::Page& page, gallery::Context& ctx)
{
    const char* about = gallery::page_about(page.name);
    return ui::VerticalStack{
        .spacing         = 10.0f,
        .child_alignment = ui::Alignment::Stretch,
        .margin          = ui::Edges{.left = 28.0f, .top = 22.0f, .right = 28.0f, .bottom = 28.0f},
        .content =
            {
                ui::HorizontalStack{
                    .spacing         = 12.0f,
                    .child_alignment = ui::Alignment::Center,
                    .content         = {ui::Icon{.source = gallery::page_icon(page.name), .size = 28.0f},
                                        ui::Text{.text = page.name, .role = ui::TextRole::Title}},
                },
                ui::Text{.text = page.description, .role = ui::TextRole::Body, .horizontal_alignment = ui::Alignment::Leading},
                ui::when(about != nullptr,
                         ui::Text{.text                 = about != nullptr ? about : "",
                                  .wrapping             = ui::TextWrapping::Wrap,
                                  .max_width            = 760.0f,
                                  .horizontal_alignment = ui::Alignment::Leading}),
                page.make(ctx),
            },
    };
}

} // namespace

int main()
{
    // The header's theme picker offers every period pack.
    ui::enable_era_themes();

    // Frame::Themed: the era packs draw their own caption and border, so picking a theme in the header
    // switches the window frame too. Fluent keeps the system frame and Mica.
    ui::Window  window{{.title    = "libwinui Control Gallery",
                        .size     = {1180, 820},
                        .backdrop = ui::Backdrop::Mica,
                        .style    = {.frame = ui::Frame::Themed}}};
    ui::Window* window_ptr = &window;

    // Every page writes to it, only the status strip reads it, so it survives navigation.
    ui::Property<std::string> status{"Pick a control on the left."};

    // A task from the jump list the Single Instance page sets starts the gallery again with
    // --gallery-task. That start hands its arguments to the running gallery and ends.
    if (const std::vector<std::string> arguments = ui::command_line();
        std::ranges::find(arguments, "--gallery-task") != arguments.end() &&
        !ui::claim_single_instance("libwinui.gallery", gallery::report_handover(status, window)))
    {
        return 0;
    }

    const std::vector<const gallery::Page*> pages = flatten_pages();
    ui::Property<std::size_t>               selected{0};
    ui::Property<int>                       revealed{0}; // ticks when a page is reached from elsewhere

    ui::ThemeRequest theme_request{};

    // Outlives the window, so page handlers may capture references into it.
    gallery::Context ctx{status,
                         *window_ptr,
                         theme_request,
                         [&pages, &selected, &revealed](std::string_view name)
                         {
                             for (std::size_t i = 0; i < pages.size(); ++i)
                             {
                                 if (name == pages[i]->name)
                                 {
                                     selected = i;
                                     ++revealed; // scrolls to it although it was open
                                     return;
                                 }
                             }
                         }};

    // The title bar's search. One past the last page is the results page for a submitted query.
    const std::size_t               search_results_index = pages.size();
    ui::Property<std::string>       search_query{};
    std::vector<gallery::SearchHit> suggestions; // what the box lists, in its order
    ui::Control<ui::AutoSuggestBox> search_box;

    ui::Control<ui::ComboBox>     theme_picker;
    ui::Control<ui::ToggleSwitch> dark_mode_toggle;
    ui::Control<ui::CheckBox>     high_contrast_box;
    ui::Control<ui::CheckBox>     animations_box;

    // The toggles start from the OS state; the variant always starts on Fluent 2, which the OS
    // has no setting for.
    const ui::OsThemeSnapshot initial_os_snapshot = ui::os_theme();

    // What the user last asked for. Light-only variants force the toggle off, and this keeps the
    // Dark choice so switching to Luna and back does not lose it.
    bool dark_preference = initial_os_snapshot.dark;
    // set_checked() fires on_checked_changed, which calls back into apply_theme(); this lets the handler skip it.
    bool syncing_dark_toggle = false;

    // High contrast replaces every color the variant and Dark/Light would pick, but the variant
    // still decides which era's high-contrast look that is.
    auto apply_theme = [&]
    {
        const ui::ThemeVariant variant       = ThemeVariants[theme_picker->selected_index().value_or(0)];
        const bool             supports_dark = ui::theme_variant_supports_dark_mode(variant);
        const bool             high_contrast = high_contrast_box->checked() == true;

        syncing_dark_toggle = true;
        dark_mode_toggle->set_checked(supports_dark && dark_preference);
        syncing_dark_toggle = false;
        dark_mode_toggle->set_enabled(supports_dark && !high_contrast);

        const ui::ThemeMode mode = high_contrast
                                       ? ui::ThemeMode::HighContrast
                                       : (dark_mode_toggle->checked() ? ui::ThemeMode::Dark : ui::ThemeMode::Light);
        // Starts from the OS "Animation effects" setting; unticking it is the app's own "Reduce
        // motion", whatever the OS says.
        const ui::AnimationPreference animations = animations_box->checked() == true ? ui::AnimationPreference::On
                                                                                     : ui::AnimationPreference::Off;
        theme_request                            = {.mode = mode, .variant = variant, .animations = animations};
        window_ptr->set_theme(theme_request); // keeps following the accent color
    };

    // Every word typed must be in a page's name, description or group. The header is built in
    // named parts, which keeps each expression shallow enough for MSVC.
    ui::Spec search = ui::AutoSuggestBox{
        .placeholder_text   = "Search controls and samples...",
        .query_icon         = ui::symbol_glyph(ui::Symbol::Find),
        .max_width          = 580.0f,
        .vertical_alignment = ui::Alignment::Center,
        .accessibility      = {.name = "Search controls and samples"},
        .on_text_changed =
            [&](const ui::AutoSuggestBoxTextChangedArgs& args)
        {
            // Choosing a suggestion changes the args.text too; only typing searches.
            if (args.reason != ui::AutoSuggestBoxTextChangeReason::UserInput)
            {
                return;
            }
            suggestions = gallery::search_suggestions(args.text);
            std::vector<std::string> names;
            for (const gallery::SearchHit& hit : suggestions)
            {
                names.emplace_back(hit.page->name);
            }
            if (names.empty() && args.text.find_first_not_of(' ') != std::string::npos)
            {
                names.emplace_back("No results found");
            }
            search_box->set_suggestions(std::move(names));
        },
        .on_query_submitted =
            [&](const ui::AutoSuggestBoxQuerySubmittedArgs& args)
        {
            // A chosen suggestion goes to its page; typed text to the results page.
            // "No results found" is a suggestion with no hit behind it.
            if (args.chosen_suggestion)
            {
                if (*args.chosen_suggestion < suggestions.size())
                {
                    ctx.navigate(suggestions[*args.chosen_suggestion].page->name);
                }
                return;
            }
            if (args.query_text.find_first_not_of(' ') == std::string::npos)
            {
                return;
            }
            search_query = args.query_text;
            selected     = search_results_index;
            status       = "Search: " + args.query_text;
        },
        .control = search_box,
    };

    ui::Spec theme_controls = ui::HorizontalStack{
        .spacing         = 14.0f,
        .child_alignment = ui::Alignment::Center,
        .content =
            {
                ui::ComboBox{.items = std::vector<std::string>(std::begin(ThemeVariantNames),
                                                               std::end(ThemeVariantNames)),
                             .selected_index = 0,
                             .on_selection_changed =
                                 [&](std::optional<std::size_t>)
                             {
                                 apply_theme();
                                 status = "Theme: variant changed to " + theme_picker->text();
                             },
                             .control = theme_picker},
                // The icon follows the state, so it always shows the theme in effect.
                // Screen readers still hear "Light"/"Dark".
                ui::ToggleSwitch{.checked       = initial_os_snapshot.dark,
                                 .checked_text  = "Dark",
                                 .unchecked_text     = "Light",
                                 .checked_icon       = DarkGlyph,
                                 .unchecked_icon      = LightGlyph,
                                 .min_width     = 0.0f, // no gap after the icon (the theme's minimum is 154)
                                 .accessibility = {.name = "Dark mode"},
                                 .on_checked_changed =
                                     [&](bool on)
                                 {
                                     if (syncing_dark_toggle)
                                     {
                                         return;
                                     }
                                     dark_preference = on;
                                     apply_theme();
                                     status = on ? "Theme: Dark mode on" : "Theme: Dark mode off";
                                 },
                                 .control = dark_mode_toggle},
                ui::CheckBox{.text    = "High contrast",
                             .checked = std::optional<bool>{initial_os_snapshot.high_contrast},
                             .on_checked_changed =
                                 [&](std::optional<bool> on)
                             {
                                 apply_theme();
                                 status = on == true ? "Theme: High contrast on" : "Theme: High contrast off";
                             },
                             .control = high_contrast_box},
                ui::CheckBox{.text    = "Animations",
                             .checked = std::optional<bool>{initial_os_snapshot.animations_enabled},
                             .on_checked_changed =
                                 [&](std::optional<bool> on)
                             {
                                 apply_theme();
                                 status = on == true ? "Theme: Animations on" : "Theme: Animations off";
                             },
                             .control = animations_box},
            },
    };

    // ---- header: the app's name, the search box and the theme controls ----
    ui::Spec header = ui::Grid{.columns        = {ui::AutoSize, 1_fr, ui::AutoSize},
                               .rows           = {ui::AutoSize},
                               .column_spacing = 16.0f,
                               .margin  = ui::Edges{.left = 20.0f, .top = 14.0f, .right = 20.0f, .bottom = 10.0f},
                               .content = {
                                   ui::Cell{.row     = 0,
                                            .column  = 0,
                                            .content = ui::Text{.text                 = "libwinui Gallery",
                                                                .role                 = ui::TextRole::Subtitle,
                                                                .horizontal_alignment = ui::Alignment::Leading,
                                                                .vertical_alignment   = ui::Alignment::Center}},
                                   ui::Cell{.row = 0, .column = 1, .content = std::move(search)},
                                   ui::Cell{.row = 0, .column = 2, .content = std::move(theme_controls)},
                               }};

    // ---- body: the navigation pane and the page it selects ----
    // Outside the Scroll, so the pane background stays put while its content scrolls.
    ui::Spec pages_view = ui::Match<std::size_t>{
        .value   = selected,
        .content = [&pages, &ctx, &search_query](const std::size_t& index) -> ui::Spec
        {
            if (index < pages.size())
            {
                return page_view(*pages[index], ctx);
            }
            // A new query while the results show rebuilds just them.
            return ui::Match<std::string>{
                .value   = search_query,
                .content = [&ctx](const std::string& query) { return gallery::search_results_page(query, ctx); },
            };
        },
    };
    ui::Spec body = build_nav_pane(selected, revealed, gallery::content_surface(ui::Scroll{.content = std::move(pages_view)}));

    ui::Element* root = window.set_content(
        ui::Grid{.columns = {1_fr},
                 .rows    = {ui::AutoSize, 1_fr, ui::AutoSize},
                 .content = {
                     ui::Cell{.row = 0, .column = 0, .content = std::move(header)},
                     ui::Cell{.row = 1, .column = 0, .content = std::move(body)},
                     // ---- status strip ----
                     ui::Cell{.row    = 2,
                              .column = 0,
                              .content = ui::Text{.text = status,
                                                  .role = ui::TextRole::Caption,
                                                  .margin = ui::Edges{.left = 20.0f, .top = 8.0f, .right = 20.0f, .bottom = 12.0f},
                                                  .horizontal_alignment = ui::Alignment::Leading}},
                 }});

    // Ctrl+F jumps to the search box from anywhere in the window. A page's own Ctrl+F, on an
    // element inside it, still comes first.
    root->add_shortcut({.keys       = ui::Ctrl + ui::Key::F,
                        .on_pressed = [&window, &search_box] { (void)window.focus_element(search_box.get()); }});

    // The default ThemeRequest follows live OS theme changes. The Mica backdrop falls back to an
    // opaque fill before Windows 11 22H2.
    return ui::run(window);
}
