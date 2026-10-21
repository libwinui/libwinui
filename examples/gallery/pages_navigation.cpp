// SPDX-License-Identifier: MIT
#include "gallery.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

using ui::operator""_fr;

namespace gallery
{
namespace
{
// ---- Symbol-enum codepoints the samples name (a Symbol icon draws the enum value itself) ----

constexpr ui::IconGlyph Play{0xE102};        // Symbol.Play
constexpr ui::IconGlyph Save{0xE105};        // Symbol.Save
constexpr ui::IconGlyph Home{0xE10F};        // Symbol.Home
constexpr ui::IconGlyph Favorite{0xE113};    // Symbol.Favorite
constexpr ui::IconGlyph Download{0xE118};    // Symbol.Download
constexpr ui::IconGlyph Mail{0xE119};        // Symbol.Mail
constexpr ui::IconGlyph Help{0xE11B};        // Symbol.Help
constexpr ui::IconGlyph Clock{0xE121};       // Symbol.Clock
constexpr ui::IconGlyph Document{0xE130};    // Symbol.Document
constexpr ui::IconGlyph Contact{0xE13D};     // Symbol.Contact
constexpr ui::IconGlyph MusicInfo{0xE142};   // Symbol.MusicInfo
constexpr ui::IconGlyph Keyboard{0xE144};    // Symbol.Keyboard
constexpr ui::IconGlyph Refresh{0xE149};     // Symbol.Refresh
constexpr ui::IconGlyph Shop{0xE14D};        // Symbol.Shop
constexpr ui::IconGlyph Page2{0xE160};       // Symbol.Page2
constexpr ui::IconGlyph Calendar{0xE163};    // Symbol.Calendar
constexpr ui::IconGlyph Placeholder{0xE18A}; // Symbol.Placeholder
constexpr ui::IconGlyph OpenLocal{0xE197};   // Symbol.OpenLocal
constexpr ui::IconGlyph Map{0xE1C4};         // Symbol.Map
constexpr ui::IconGlyph Tag{0xE1CB};         // Symbol.Tag
constexpr ui::IconGlyph Library{0xE1D3};     // Symbol.Library
constexpr ui::IconGlyph NewFolder{0xE1DA};   // Symbol.NewFolder
constexpr ui::IconGlyph Share{0xE72D};       // Symbol.Share

constexpr const char* Lorem =
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et "
    "dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex "
    "ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat "
    "nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit "
    "anim id est laborum.";

// The page number a Frame shows for the settings page, and for no page at all.
constexpr int SettingsPage = -1;
constexpr int NoPage       = 0;

// ---- the sample pages ----

// A sample page tile: at least 150 tall with a margin of 5, or an ellipse. No fill means
// the theme's accent color, so it follows the theme.
class Tile final : public ui::Element
{
public:
    Tile(std::optional<ui::Color> fill, float diameter) : fill_(fill), diameter_(diameter)
    {
        if (diameter_ > 0.0f)
        {
            set_width(diameter_);
            set_height(diameter_);
        }
    }

protected:
    ui::Size measure_override(ui::Size, const ui::MeasureContext&) const override
    {
        return ui::Size{diameter_, diameter_};
    }

    void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
    {
        const ui::Theme& theme = context.theme;
        const ui::Color  color = fill_.value_or(theme.accent_ramp.base);
        if (diameter_ > 0.0f)
        {
            renderer.fill_ellipse(bounds().center(), bounds().width * 0.5f, bounds().height * 0.5f, color);
        }
        else
        {
            renderer.fill_rounded_rect(bounds(), 0.0f, color);
        }
    }


private:
    std::optional<ui::Color> fill_;
    float                    diameter_;
};

constexpr float TileHeight = 150.0f;

std::unique_ptr<ui::Element> tile(std::optional<ui::Color> fill, float margin = 5.0f)
{
    auto node = std::make_unique<Tile>(fill, 0.0f);
    node->set_min_height(TileHeight);
    node->set_margin(ui::Thickness{margin});
    return node;
}

std::unique_ptr<ui::Element> circle(ui::Color fill, float diameter)
{
    return std::make_unique<Tile>(fill, diameter);
}

ui::Cell cell(std::size_t row, std::size_t column, ui::Spec content, std::size_t row_span = 1, std::size_t column_span = 1)
{
    return ui::Cell{.row = row, .column = column, .row_span = row_span, .column_span = column_span, .content = std::move(content)};
}

ui::Spec lorem(ui::Thickness margin)
{
    return ui::Text{.text = Lorem, .wrapping = ui::TextWrapping::WrapWholeWords, .margin = margin};
}

namespace named
{
constexpr ui::Color DarkGray        = ui::Color::from_rgba(0xA9, 0xA9, 0xA9);
constexpr ui::Color LightGray       = ui::Color::from_rgba(0xD3, 0xD3, 0xD3);
constexpr ui::Color Gray            = ui::Color::from_rgba(0x80, 0x80, 0x80);
constexpr ui::Color DarkSalmon      = ui::Color::from_rgba(0xE9, 0x96, 0x7A);
constexpr ui::Color DarkRed         = ui::Color::from_rgba(0x8B, 0x00, 0x00);
constexpr ui::Color LightCoral      = ui::Color::from_rgba(0xF0, 0x80, 0x80);
constexpr ui::Color IndianRed       = ui::Color::from_rgba(0xCD, 0x5C, 0x5C);
constexpr ui::Color Khaki           = ui::Color::from_rgba(0xF0, 0xE6, 0x8C);
constexpr ui::Color DarkKhaki       = ui::Color::from_rgba(0xBD, 0xB7, 0x6B);
constexpr ui::Color DarkSeaGreen    = ui::Color::from_rgba(0x8F, 0xBC, 0x8F);
constexpr ui::Color MediumSeaGreen  = ui::Color::from_rgba(0x3C, 0xB3, 0x71);
constexpr ui::Color DarkOliveGreen  = ui::Color::from_rgba(0x55, 0x6B, 0x2F);
constexpr ui::Color MediumTurquoise = ui::Color::from_rgba(0x48, 0xD1, 0xCC);
constexpr ui::Color SteelBlue       = ui::Color::from_rgba(0x46, 0x82, 0xB4);
constexpr ui::Color SkyBlue         = ui::Color::from_rgba(0x87, 0xCE, 0xEB);
constexpr ui::Color PowderBlue      = ui::Color::from_rgba(0xB0, 0xE0, 0xE6);
constexpr ui::Color Pink            = ui::Color::from_rgba(0xFF, 0xC0, 0xCB);
constexpr ui::Color Plum            = ui::Color::from_rgba(0xDD, 0xA0, 0xDD);
} // namespace named

ui::Spec sample_grid(ui::Grid grid)
{
    return ui::Scroll{.content = std::move(grid)};
}

// Sample pages 1 to 6, and the settings page for SettingsPage. `parts` receives the
// elements the Motion group's connected animation card animates between.
ui::Spec sample_page(int page, SamplePageParts* parts = nullptr)
{
    using namespace named;
    const auto a = ui::AutoSize;
    switch (page)
    {
        case SettingsPage:
            return ui::Text{.text                 = "Sample Settings Page",
                            .role                 = ui::TextRole::Title,
                            .horizontal_alignment = ui::Alignment::Center,
                            .vertical_alignment   = ui::Alignment::Center};
        case 1:
        {
            auto accent = std::make_unique<Tile>(std::nullopt, 0.0f);
            accent->set_min_width(250.0f);
            accent->set_min_height(TileHeight);
            accent->set_margin(ui::Thickness{5.0f});
            if (parts != nullptr)
            {
                parts->accent = accent.get();
            }
            return sample_grid(
                ui::Grid{.columns = {a, 1_fr, 1_fr},
                         .rows    = {a, a, a, a},
                         .content = {cell(1, 1, tile(DarkGray, 6.0f)),
                                     cell(1, 2, tile(LightGray, 6.0f)),
                                     cell(2, 1, tile(LightGray, 6.0f)),
                                     cell(2, 2, tile(DarkGray, 6.0f)),
                                     cell(1, 0, std::move(accent), 2),
                                     cell(3, 0, lorem(ui::Symmetric{.horizontal = 6.0f, .vertical = 12.0f}), 1, 3)}});
        }
        case 2:
        {
            auto accent = std::make_unique<Tile>(std::nullopt, 0.0f);
            accent->set_width(150.0f);
            accent->set_height(200.0f);
            accent->set_margin(ui::Thickness{12.0f});
            accent->set_vertical_alignment(ui::Alignment::Leading);
            if (parts != nullptr)
            {
                parts->accent = accent.get();
            }
            return sample_grid(
                ui::Grid{.columns = {a, 1_fr},
                         .rows    = {a, a},
                         .content = {cell(1, 0, std::move(accent)),
                                     cell(1,
                                          1,
                                          ui::VerticalStack{.child_alignment = ui::Alignment::Stretch,
                                                            .margin          = ui::Thickness{12.0f},
                                                            .min_height      = 200.0f,
                                                            .control = parts != nullptr ? &parts->content_panel : nullptr,
                                                            .content = {ui::Text{.text = "Lorem ipsum dolor sit amet, "
                                                                                         "consectetur adipiscing elit",
                                                                                 .role = ui::TextRole::Title,
                                                                                 .wrapping = ui::TextWrapping::WrapWholeWords,
                                                                                 .margin = ui::Edges{.bottom = 12.0f}},
                                                                        lorem(ui::Thickness{})}})}});
        }
        case 3:
            return sample_grid(
                ui::Grid{.columns = {2_fr, 1_fr, 1_fr},
                         .rows    = {a, a, a, a},
                         .content = {cell(1, 0, tile(LightGray), 2),
                                     cell(1, 1, tile(DarkGray)),
                                     cell(2, 1, tile(Gray)),
                                     cell(1, 2, tile(LightGray)),
                                     cell(2, 2, tile(DarkGray)),
                                     cell(3, 0, lorem(ui::Thickness{5.0f}), 1, 3)}});
        case 4:
            return ui::Scroll{
                .content = ui::VerticalStack{.child_alignment = ui::Alignment::Stretch,
                                             .content = {ui::Grid{.columns = {2_fr, 1_fr, 1_fr},
                                                                  .rows    = {a},
                                                                  .content = {cell(0, 0, tile(DarkSalmon)),
                                                                              cell(0, 1, tile(DarkRed)),
                                                                              cell(0, 2, tile(LightCoral))}},
                                                         ui::Grid{.columns = {1_fr, 1_fr, 2_fr},
                                                                  .rows    = {a, a, a},
                                                                  .content = {cell(1, 1, tile(DarkRed)),
                                                                              cell(1, 0, tile(LightCoral)),
                                                                              cell(1, 2, tile(IndianRed)),
                                                                              cell(2, 0, lorem(ui::Thickness{5.0f}), 1, 3)}}}}};
        case 5:
            return sample_grid(
                ui::Grid{.columns = {1_fr, 1_fr, 4_fr},
                         .rows    = {a, a, a, a},
                         .content = {cell(0, 0, tile(Khaki)),
                                     cell(0, 1, tile(DarkKhaki)),
                                     cell(0, 2, circle(DarkSeaGreen, 150.0f)),
                                     cell(1, 0, circle(MediumSeaGreen, 75.0f), 1, 2),
                                     cell(1, 2, tile(DarkOliveGreen)),
                                     cell(3, 0, lorem(ui::Thickness{5.0f}), 1, 3)}});
        case 6:
            return sample_grid(
                ui::Grid{.columns = {1_fr, 1_fr, 4_fr},
                         .rows    = {a, a, a, a},
                         .content = {cell(0, 0, circle(MediumTurquoise, 20.0f)),
                                     cell(0, 1, circle(SteelBlue, 50.0f)),
                                     cell(0, 2, tile(SkyBlue)),
                                     cell(1, 0, circle(PowderBlue, 50.0f)),
                                     cell(1, 1, circle(MediumTurquoise, 20.0f)),
                                     cell(1, 2, tile(SteelBlue)),
                                     cell(3, 0, lorem(ui::Thickness{5.0f}), 1, 3)}});
        default:
            return ui::Spec{};
    }
}

// A Frame: whichever sample page `page` names.
ui::Spec frame(ui::Property<int>& page)
{
    return ui::Match<int>{.value = page, .content = [](const int& p) { return sample_page(p); }};
}

ui::Spec paragraph(const char* text)
{
    return ui::Text{.text = text, .wrapping = ui::TextWrapping::WrapWholeWords};
}

ui::Spec options_panel(ui::SpecList children)
{
    return ui::VerticalStack{
        .spacing         = 4.0f,
        .child_alignment = ui::Alignment::Stretch,
        .content         = std::move(children),
    };
}

// The pager samples' options column, 8 apart and at least `min_width` wide.
ui::Spec options_panel(float min_width, ui::SpecList children)
{
    return ui::VerticalStack{
        .spacing         = 8.0f,
        .child_alignment = ui::Alignment::Stretch,
        .min_width       = min_width,
        .content         = std::move(children),
    };
}

// The Carousel samples' photos: sample_photo() at a size and stretch.
// A sample photo. A width or height of 0 leaves that side to the layout.
ui::Spec photo(std::size_t index, float width, float height, ui::Stretch stretch)
{
    const auto size = [](float v) { return v > 0.0f ? std::optional<float>{v} : std::nullopt; };
    return ui::Image{.source  = ui::ImageSource{sample_photo(index)},
                     .stretch = stretch,
                     .width   = size(width),
                     .height  = size(height)};
}

ui::Spec check(const char* text, bool initial, ui::Callback<void(bool)> changed)
{
    return ui::CheckBox{.text               = text,
                        .checked            = std::optional<bool>{initial},
                        .on_checked_changed = [changed = std::move(changed)](std::optional<bool> on) mutable
                        { changed(on == true); }};
}

// ---- Breadcrumb ----

constexpr std::array<const char*, 8>
    FoldersString{"Home", "Documents", "Design", "Northwind", "Images", "Folder1", "Folder2", "Folder3"};
constexpr std::array<const char*, 4> DefaultFolders{"Home", "Folder1", "Folder2", "Folder3"};

std::vector<std::string> default_folders()
{
    return std::vector<std::string>(DefaultFolders.begin(), DefaultFolders.end());
}

struct BreadcrumbState
{
    ui::Property<std::vector<std::string>> folders{default_folders()};
};

ui::Spec breadcrumb_page(Context&)
{
    return with_state<BreadcrumbState>(
        [](BreadcrumbState& s)
        {
            return examples({
                example({
                    .title = "A Breadcrumb control",
                    .code  = R"cpp(
                        ui::Breadcrumb{
                            .items = {"Home", "Documents", "Design", "Northwind", "Images", "Folder1", "Folder2", "Folder3"},
                        }
                    )cpp",
                    .controls = {ui::Breadcrumb{.items = std::vector<std::string>(FoldersString.begin(), FoldersString.end())}},
                }),
                // Clicking an item drops everything after it, as the sample's click handler
                // does to its collection; "Reset sample" puts the missing folders back.
                example({
                    .title   = "Breadcrumb bound to a list of folders",
                    .code    = R"cpp(
                        ui::Property<std::vector<std::string>> folders{{"Home", "Folder1", "Folder2", "Folder3"}};

                        ui::Breadcrumb{
                            .items           = folders,
                            // Clicking an item drops everything after it.
                            .on_item_click = [&](std::size_t index)
                            { folders.update([&](std::vector<std::string>& v) { v.resize(index + 1); }); },
                        }

                        // "Reset sample" puts the folders back.
                        ui::Button{
                            .text     = "Reset sample",
                            .on_click = [&] { folders = {"Home", "Folder1", "Folder2", "Folder3"}; },
                        }
                    )cpp",
                    .options = ui::Button{.text = "Reset sample", .on_click = [&s] { s.folders = default_folders(); }},
                    .controls =
                        {
                            ui::Breadcrumb{.items = s.folders,
                                           .on_item_click =
                                               [&s](std::size_t index)
                                           {
                                               s.folders.update([&](std::vector<std::string>& v) { v.resize(index + 1); });
                                           }},
                        },
                }),
            });
        });
}

// ---- NavigationShell ----

// The selection's page mapping, shared by every card: the Settings item shows the settings
// page, an item keyed "pageN" shows page N, and any other key no page.
int page_for(std::string_view key)
{
    if (key == ui::NavigationShellControl::SettingsKey)
    {
        return SettingsPage;
    }
    int page = NoPage;
    if (key.starts_with("page"))
    {
        (void)std::from_chars(key.data() + 4, key.data() + key.size(), page);
    }
    return page;
}

std::string page_header(int page)
{
    return std::format("Sample Page {}", page);
}

// The "switches pane orientation" card's width trigger: Top once the *window* is at least
// compact_mode_threshold_width() wide, the NavigationShell's own Auto below that.
class PaneOrientationTrigger final : public ui::ContentHost
{
public:
    PaneOrientationTrigger(const ui::Window& window, const ui::Control<ui::NavigationShell>* nav) :
        window_(window),
        nav_(nav)
    {
    }

    void arrange_override(const ui::Rect& final_rect, const ui::ArrangeContext& context) override
    {
        if (ui::NavigationShellControl* nav = *nav_)
        {
            const bool wide = window_.layout_size().width >= nav->compact_mode_threshold_width();
            const auto mode = wide ? ui::NavigationShellPaneDisplayMode::Top : ui::NavigationShellPaneDisplayMode::Auto;
            if (nav->pane_display_mode() != mode)
            {
                nav->set_pane_display_mode(mode);
            }
        }
        ui::ContentHost::arrange_override(final_rect, context);
    }

private:
    const ui::Window&                       window_;
    const ui::Control<ui::NavigationShell>* nav_;
};

struct NavigationShellState
{
    // The default pane display mode card
    ui::Property<int>         page5{1};
    ui::Property<std::string> header5{page_header(1)};
    // Top
    ui::Property<int> page6{1};
    // Switching orientation
    ui::Control<ui::NavigationShell> nav2;
    ui::Property<int>                page2{1};
    // Tabs pattern
    ui::Property<int> page7{1};
    // Data binding
    ui::Property<int>         page4{1};
    ui::Property<std::string> header4{page_header(1)};
    // Footer items
    ui::Control<ui::NavigationShell> nav9;
    ui::Property<int>                page9{1};
    // Hierarchical
    ui::Control<ui::NavigationShell> nav8;
    ui::Property<int>                page8{1};
    ui::Property<std::string>        header8{page_header(1)};
    // API in action
    ui::Control<ui::NavigationShell>                       nav;
    ui::Property<int>                                      page{NoPage};
    ui::Property<std::string>                              header{"Header"};
    ui::Property<std::string>                              pane_title{"Pane Title"};
    ui::Property<bool>                                     settings_visible{true};
    ui::Property<ui::AutoVisibility>                       back_visible{ui::AutoVisibility::Auto};
    ui::Property<bool>                                     back_enabled{false};
    ui::Property<bool>                                     always_show_header{true};
    ui::Property<ui::NavigationShellSelectionFollowsFocus> sff{ui::NavigationShellSelectionFollowsFocus::Disabled};
    bool                                                   custom_content      = false;
    bool                                                   pane_footer_content = false;
};

// The data-binding card's Category model.
struct Category
{
    const char*   name;
    ui::IconGlyph glyph;
    const char*   tool_tip;
};
constexpr std::array<Category, 4> Categories{{
    {"Category 1", Home, "This is category 1"},
    {"Category 2", Keyboard, "This is category 2"},
    {"Category 3", Library, "This is category 3"},
    {"Category 4", Mail, "This is category 4"},
}};

// The API card's pane footer: two icon-only items, stacked in Left mode and in a row in Top mode.
std::unique_ptr<ui::Element> api_pane_footer(bool top)
{
    auto item = [](ui::IconGlyph glyph, const char* name)
    {
        return ui::Button{.variant              = ui::ButtonVariant::Subtle,
                          .padding              = ui::Symmetric{.horizontal = 12.0f, .vertical = 10.0f},
                          .horizontal_alignment = ui::Alignment::Leading,
                          .accessibility        = {.name = name},
                          .content              = ui::Icon{.source = glyph}};
    };
    if (top)
    {
        return ui::build(ui::HorizontalStack{.content = {item(Download, "download"), item(Favorite, "favorite")}});
    }
    return ui::build(ui::VerticalStack{.content = {item(Download, "download"), item(Favorite, "favorite")}});
}

void set_pane_position(ui::NavigationShellControl* nav, ui::NavigationShellPaneDisplayMode mode)
{
    if (nav != nullptr)
    {
        nav->set_pane_display_mode(mode);
        nav->set_pane_open(mode == ui::NavigationShellPaneDisplayMode::Left);
    }
}

ui::Spec navigation_view_page(Context& ctx)
{
    ui::Window& window = ctx.window;
    return with_state<NavigationShellState>(
        [&window](NavigationShellState& s)
        {
            auto trigger = std::make_unique<PaneOrientationTrigger>(window, &s.nav2);
            trigger->set_child(ui::build(ui::NavigationShell{
                .height               = 460.0f,
                .on_selection_changed = [&s](const std::string& key) { s.page2 = page_for(key); },
                .control              = s.nav2,
                .content              = frame(s.page2),
                .items                = {ui::NavItem{.text = "Menu Item1", .key = "page1"},
                                         ui::NavItem{.text = "Menu Item2", .key = "page2"},
                                         ui::NavItem{.text = "Menu Item3", .key = "page3"},
                                         ui::NavItem{.text = "Menu Item4", .key = "page4"}},
            }));

            return examples({
                example({
                    .title   = "NavigationShell with the default pane display mode",
                    .code    = R"cpp(
                        ui::Property<std::string> page; // the selected item's key, "page1" at first
                        ui::Property<std::string> header{"Sample Page 1"};

                        ui::NavigationShell{
                            .header               = header,
                            .selected_key         = page,
                            .height               = 460,
                            .on_selection_changed = [&](const std::string& key)
                            {
                                if (key != ui::NavigationShellControl::SettingsKey)
                                {
                                    header = std::format("Sample Page {}", key.back()); // "page2": "Sample Page 2"
                                }
                            },
                            // One shared content area that follows `page`; make_page(key) is your own page factory.
                            .content = ui::Match<std::string>{.value   = page,
                                                              .content = [](const std::string& key) { return make_page(key); }},
                            .items   = {
                                ui::NavItem{.text = "Menu Item1", .key = "page1", .icon = ui::IconGlyph{0xE102}},
                                ui::NavItem{.text = "Menu Item2", .key = "page2", .icon = ui::IconGlyph{0xE105}},
                                ui::NavItem{.text = "Menu Item3", .key = "page3", .icon = ui::IconGlyph{0xE149}},
                                ui::NavItem{.text = "Menu Item4", .key = "page4", .icon = ui::IconGlyph{0xE118}},
                            },
                        }
                    )cpp",
                    .stacked = true,
                    .controls =
                        {
                            paragraph("If you have five or more equally important navigation categories that should prominently "
                                      "appear on larger window widths, consider using a left navigation pane."),
                            ui::NavigationShell{.header               = s.header5,
                                               .height               = 460.0f,
                                               .on_selection_changed = [&s](const std::string& key)
                                               {
                                                   s.page5 = page_for(key);
                                                   if (s.page5.get() > 0)
                                                   {
                                                       s.header5 = page_header(s.page5.get());
                                                   }
                                               },
                                               .content = frame(s.page5),
                                               .items   = {ui::NavItem{.text = "Menu Item1", .key = "page1", .icon = Play},
                                                           ui::NavItem{.text = "Menu Item2", .key = "page2", .icon = Save},
                                                           ui::NavItem{.text = "Menu Item3", .key = "page3", .icon = Refresh},
                                                           ui::NavItem{.text = "Menu Item4", .key = "page4", .icon = Download}}},
                        },
                }),
                example({
                    .title   = "NavigationShell with the pane display mode set to Top",
                    .code    = R"cpp(
                        ui::Property<std::string> page;

                        ui::NavigationShell{
                            .header            = std::string{"This is Header Text"},
                            .selected_key      = page,
                            .pane_display_mode = ui::NavigationShellPaneDisplayMode::Top,
                            .height            = 460,
                            // make_page(key) is your own page factory.
                            .content = ui::Match<std::string>{.value   = page,
                                                              .content = [](const std::string& key) { return make_page(key); }},
                            .items   = {
                                ui::NavItem{.text = "Menu Item1", .key = "page1"},
                                ui::NavItem{.text = "Menu Item2", .key = "page2"},
                                ui::NavItem{.text = "Menu Item3", .key = "page3"},
                                ui::NavItem{.text = "Menu Item4", .key = "page4"},
                            },
                        }
                    )cpp",
                    .stacked = true,
                    .controls =
                        {
                            paragraph("If you have equally important navigation categories that should be de-emphasized relative "
                                      "to the content of your app, consider using a top navigation pane."),
                            ui::NavigationShell{.header               = std::string{"This is Header Text"},
                                               .pane_display_mode    = ui::NavigationShellPaneDisplayMode::Top,
                                               .height               = 460.0f,
                                               .on_selection_changed = [&s](const std::string& key) { s.page6 = page_for(key); },
                                               .content              = frame(s.page6),
                                               .items                = {ui::NavItem{.text = "Menu Item1", .key = "page1"},
                                                                        ui::NavItem{.text = "Menu Item2", .key = "page2"},
                                                                        ui::NavItem{.text = "Menu Item3", .key = "page3"},
                                                                        ui::NavItem{.text = "Menu Item4", .key = "page3-more"}}},
                        },
                }),
                example({
                    .title   = "NavigationShell that switches pane orientation based on window width",
                    .code    = R"cpp(
                        ui::Control<ui::NavigationShell> nav;
                        ui::Property<std::string>       page;

                        ui::NavigationShell{
                            .selected_key = page,
                            .height       = 460,
                            .control      = nav,
                            // make_page(key) is your own page factory.
                            .content = ui::Match<std::string>{.value   = page,
                                                              .content = [](const std::string& key) { return make_page(key); }},
                            .items   = {
                                ui::NavItem{.text = "Menu Item1", .key = "page1"},
                                ui::NavItem{.text = "Menu Item2", .key = "page2"},
                                ui::NavItem{.text = "Menu Item3", .key = "page3"},
                                ui::NavItem{.text = "Menu Item4", .key = "page4"},
                            },
                        }

                        // Whenever the layout runs (here: in the arrange_override of a host around the view),
                        // a wide window gets the top pane, a narrow one the view's own Auto mode.
                        const bool wide = window.layout_size().width >= nav->compact_mode_threshold_width();
                        const auto mode = wide ? ui::NavigationShellPaneDisplayMode::Top
                                               : ui::NavigationShellPaneDisplayMode::Auto;
                        if (nav->pane_display_mode() != mode)
                        {
                            nav->set_pane_display_mode(mode);
                        }
                    )cpp",
                    .stacked = true,
                    .controls =
                        {
                            paragraph("If you have equally important navigation categories and limited app content space, "
                                      "consider using a top navigation pane on larger window widths and a minimal left "
                                      "navigation pane on smaller window widths."),
                            std::move(trigger),
                        },
                }),
                example({
                    .title   = "Tying selection and focus - Tabs",
                    .code    = R"cpp(
                        ui::Property<std::string> page;

                        ui::NavigationShell{
                            .selected_key            = page,
                            .pane_display_mode       = ui::NavigationShellPaneDisplayMode::Top,
                            .back_button_visible     = ui::AutoVisibility::Collapsed,
                            // The arrow keys select as they move.
                            .selection_follows_focus = ui::NavigationShellSelectionFollowsFocus::Enabled,
                            .height                  = 460,
                            // make_page(key) is your own page factory; an item without a key is keyed by its text.
                            .content = ui::Match<std::string>{.value   = page,
                                                              .content = [](const std::string& key) { return make_page(key); }},
                            .items   = {ui::NavItem{.text = "Item1"}, ui::NavItem{.text = "Item2"}, ui::NavItem{.text = "Item3"}, ui::NavItem{.text = "Item4"}},
                        }
                    )cpp",
                    .stacked = true,
                    .controls =
                        {
                            paragraph("For the tabs pattern, make selection follow focus. Moving between the items with the "
                                      "arrow keys then selects them, and the content follows."),
                            ui::NavigationShell{
                                .pane_display_mode       = ui::NavigationShellPaneDisplayMode::Top,
                                .back_button_visible     = ui::AutoVisibility::Collapsed,
                                .selection_follows_focus = ui::NavigationShellSelectionFollowsFocus::Enabled,
                                .height                  = 460.0f,
                                .on_selection_changed    = [&s](const std::string& key) { s.page7 = page_for(key); },
                                .content                 = frame(s.page7),
                                .items = {ui::NavItem{.text = "Item1", .key = "page1"}, ui::NavItem{.text = "Item2", .key = "page2"}, ui::NavItem{.text = "Item3", .key = "page3"}, ui::NavItem{.text = "Item4", .key = "page4"}}},
                        },
                }),
                example({
                    .title   = "Data binding",
                    .code    = R"cpp(
                        struct Category
                        {
                            std::string   name;
                            ui::IconGlyph glyph;
                            std::string   tool_tip;
                        };
                        std::vector<Category> categories{
                            {"Category 1", ui::IconGlyph{0xE10F}, "This is category 1"},
                            {"Category 2", ui::IconGlyph{0xE144}, "This is category 2"},
                            {"Category 3", ui::IconGlyph{0xE1D3}, "This is category 3"},
                            {"Category 4", ui::IconGlyph{0xE119}, "This is category 4"},
                        };
                        ui::Property<std::string> header{"Sample Page 1"};

                        // One entry per data item, keyed by its name.
                        ui::NavList items;
                        for (const Category& c : categories)
                        {
                            ui::NavList::Entry entry{.text = c.name, .key = c.name};
                            entry.icon    = c.glyph;
                            entry.tool_tip = c.tool_tip;
                            items.entries.push_back(std::move(entry));
                        }

                        ui::NavigationShell{
                            .header               = header,
                            .height               = 460,
                            // The selected key leads back to the data item.
                            .on_selection_changed = [&](const std::string& key)
                            {
                                if (key != ui::NavigationShellControl::SettingsKey)
                                {
                                    header = "Sample Page " + key.substr(key.size() - 1);
                                }
                            },
                            .content = ui::Text{.text = "The page"},
                            .items   = std::move(items),
                        }
                    )cpp",
                    .stacked = true,
                    .controls =
                        {
                            paragraph("When the items come from data, build the item list from that data: each entry carries "
                                      "its text, key, icon and tooltip, and the selection handler maps the selected key back "
                                      "to the data it came from."),
                            [&s]
                            {
                                ui::NavList items;
                                for (const Category& c : Categories)
                                {
                                    ui::NavList::Entry entry{.text = c.name, .key = c.name};
                                    entry.icon    = c.glyph;
                                    entry.tool_tip = c.tool_tip;
                                    items.entries.push_back(std::move(entry));
                                }
                                return ui::Spec{ui::NavigationShell{
                                    .header               = s.header4,
                                    .height               = 460.0f,
                                    .on_selection_changed = [&s](const std::string& key)
                                    {
                                        if (key == ui::NavigationShellControl::SettingsKey)
                                        {
                                            s.page4 = SettingsPage;
                                        }
                                        else if (!key.empty())
                                        {
                                            s.header4 = "Sample Page " + key.substr(key.size() - 1);
                                            s.page4   = 1;
                                        }
                                    },
                                    .content = frame(s.page4),
                                    .items   = std::move(items),
                                }};
                            }(),
                        },
                }),
                example({
                    .title   = "NavigationShell with Footer Menu Items",
                    .code    = R"cpp(
                        ui::Control<ui::NavigationShell> nav;
                        ui::Property<std::string>       page;

                        ui::NavigationShell{
                            .header            = std::string{"This is Header Text"},
                            .selected_key      = page,
                            .pane_display_mode = ui::NavigationShellPaneDisplayMode::Left,
                            .settings_visible  = false,
                            .height            = 460,
                            .control           = nav,
                            // The footer items select like the others; make_page(key) is your own page factory.
                            .content = ui::Match<std::string>{.value   = page,
                                                              .content = [](const std::string& key) { return make_page(key); }},
                            .items   = {
                                ui::NavItem{.text = "Browse", .key = "browse", .icon = ui::IconGlyph{0xE1D3}},
                                ui::NavItem{.text = "Track an Order", .key = "track", .icon = ui::IconGlyph{0xE1C4}},
                                ui::NavItem{.text = "Order History", .key = "history", .icon = ui::IconGlyph{0xE1CB}},
                            },
                            .footer_items = {
                                ui::NavItem{.text = "Account", .key = "account", .icon = ui::IconGlyph{0xE13D}},
                                ui::NavItem{.text = "Your Cart", .key = "cart", .icon = ui::IconGlyph{0xE14D}},
                                ui::NavItem{.text = "Help", .key = "help", .icon = ui::IconGlyph{0xE11B}},
                            },
                        }

                        // "Top mode": the footer items move to the right end of the bar.
                        nav->set_pane_display_mode(ui::NavigationShellPaneDisplayMode::Top);
                    )cpp",
                    .stacked = true,
                    .options = ui::RadioButtons{.header               = "Pane position:",
                                                .items                = {"Left mode", "Top mode"},
                                                .selected_index       = 0,
                                                .on_selection_changed = [&s](std::optional<std::size_t> i)
                                                {
                                                    set_pane_position(s.nav9,
                                                                      i == 0 ? ui::NavigationShellPaneDisplayMode::Left
                                                                             : ui::NavigationShellPaneDisplayMode::Top);
                                                }},
                    .controls =
                        {
                            paragraph("You can add clickable menu items to the footer of your NavigationShell that participate "
                                      "in the same selection model as items in the main menu. In Top mode, these "
                                      "items will appear aligned to the right of the NavigationShell. In Left mode, "
                                      "these items will appear aligned to the bottom of the NavigationShell."),
                            ui::NavigationShell{.header               = std::string{"This is Header Text"},
                                               .pane_display_mode    = ui::NavigationShellPaneDisplayMode::Left,
                                               .settings_visible     = false,
                                               .height               = 460.0f,
                                               .on_selection_changed = [&s](const std::string& key) { s.page9 = page_for(key); },
                                               .control                  = s.nav9,
                                               .content              = frame(s.page9),
                                               .items                = {ui::NavItem{.text = "Browse", .key = "page1", .icon = Library},
                                                                        ui::NavItem{.text = "Track an Order", .key = "page2", .icon = Map},
                                                                        ui::NavItem{.text = "Order History", .key = "page3", .icon = Tag}},
                                               .footer_items         = {ui::NavItem{.text = "Account", .key = "page4", .icon = Contact},
                                                                        ui::NavItem{.text = "Your Cart", .key = "page5", .icon = Shop},
                                                                        ui::NavItem{.text = "Help", .key = "page5-help", .icon = Help}}},
                        },
                }),
                example({
                    .title   = "Hierarchical NavigationShell",
                    .code    = R"cpp(
                        ui::Control<ui::NavigationShell> nav;
                        ui::Property<std::string>       header{"Sample Page 1"};

                        ui::NavigationShell{
                            .header               = header,
                            .pane_display_mode    = ui::NavigationShellPaneDisplayMode::Left,
                            .height               = 460,
                            // Nested items are keyed like any other; a key-less item is keyed by its text.
                            .on_selection_changed = [&](const std::string& key) { header = "Sample Page: " + key; },
                            .control              = nav,
                            .content              = ui::Text{.text = "The page"},
                            .items                = {
                                ui::NavItem{.text = "Home", .icon = ui::IconGlyph{0xE10F}, .tool_tip = "Home"},
                                ui::NavItem{.text    = "Account",
                                 .icon     = ui::IconGlyph{0xE13D},
                                 .tool_tip = "Account",
                                 .children = {ui::NavItem{.text = "Mail", .icon = ui::IconGlyph{0xE119}, .tool_tip = "Mail"},
                                              ui::NavItem{.text = "Calendar", .icon = ui::IconGlyph{0xE163}, .tool_tip = "Calendar"}}},
                                // Only opens its children, is never selected itself.
                                ui::NavItem{.text             = "Document options",
                                 .selects_on_invoke = false,
                                 .icon              = ui::IconGlyph{0xE160},
                                 .tool_tip          = "Document options",
                                 .children          = {ui::NavItem{.text = "Create new", .icon = ui::IconGlyph{0xE1DA}, .tool_tip = "Create new"},
                                                       ui::NavItem{.text = "Upload file", .icon = ui::IconGlyph{0xE197}, .tool_tip = "Upload file"}}},
                            },
                        }

                        // The pane position: Left, Top or LeftCompact.
                        nav->set_pane_display_mode(ui::NavigationShellPaneDisplayMode::LeftCompact);
                        nav->set_pane_open(false);
                    )cpp",
                    .stacked = true,
                    .options = ui::RadioButtons{.header               = "Pane position:",
                                                .items                = {"Left mode", "Top mode", "LeftCompact mode"},
                                                .selected_index       = 0,
                                                .on_selection_changed = [&s](std::optional<std::size_t> i)
                                                {
                                                    constexpr std::array<ui::NavigationShellPaneDisplayMode, 3> Modes{
                                                        ui::NavigationShellPaneDisplayMode::Left,
                                                        ui::NavigationShellPaneDisplayMode::Top,
                                                        ui::NavigationShellPaneDisplayMode::LeftCompact};
                                                    set_pane_position(s.nav8, Modes[static_cast<std::size_t>(i.value_or(0))]);
                                                }},
                    .controls =
                        {
                            paragraph("NavigationShell supports hierarchy in Left, LeftCompact, and Top display modes."),
                            paragraph("In the example below, the \"Account\" tab navigates to its own page while \"Document "
                                      "options\" only opens up its subtree of items. The Document options item "
                                      "doesn't select when it is invoked."),
                            paragraph("In both Top and Left modes, clicking the arrows on an item will expand or "
                                      "collapse the subtree. Clicking or tapping elsewhere on the item will "
                                      "collapse or expand the subtree."),
                            paragraph("Switch between the three pane display modes on the right."),
                            ui::NavigationShell{
                                .header               = s.header8,
                                .pane_display_mode    = ui::NavigationShellPaneDisplayMode::Left,
                                .height               = 460.0f,
                                .on_selection_changed = [&s](const std::string& key)
                                {
                                    s.page8 = page_for(key);
                                    if (s.page8.get() > 0)
                                    {
                                        s.header8 = page_header(s.page8.get());
                                    }
                                },
                                .control     = s.nav8,
                                .content = frame(s.page8),
                                .items =
                                    {
                                        ui::NavItem{.text = "Home", .key = "page1", .icon = Home, .tool_tip = "Home"},
                                        ui::NavItem{.text    = "Account",
                                         .key      = "page2",
                                         .icon     = Contact,
                                         .tool_tip = "Account",
                                         .children = {ui::NavItem{.text = "Mail", .key = "page3", .icon = Mail, .tool_tip = "Mail"},
                                                      ui::NavItem{.text = "Calendar", .key = "page4", .icon = Calendar, .tool_tip = "Calendar"}}},
                                        ui::NavItem{.text             = "Document options",
                                         .selects_on_invoke = false,
                                         .icon              = Page2,
                                         .tool_tip          = "Document options",
                                         .children          = {ui::NavItem{.text = "Create new", .key = "page5", .icon = NewFolder, .tool_tip = "Create new"},
                                                               ui::NavItem{.text = "Upload file", .key = "page6", .icon = OpenLocal, .tool_tip = "Upload file"}}},
                                    }},
                        },
                }),
                example({
                    .title   = "API in action",
                    .code    = R"cpp(
                        ui::Control<ui::NavigationShell> nav;
                        ui::Property<std::string>       header{"Header"};
                        ui::Property<std::string>       pane_title{"Pane Title"};
                        ui::Property<bool>              settings_visible{true};
                        ui::Property<bool>              back_enabled{false};
                        ui::Property<bool>              always_show_header{true};
                        ui::Property<ui::AutoVisibility> back_visible{
                            ui::AutoVisibility::Auto};
                        ui::Property<ui::NavigationShellSelectionFollowsFocus> selection_follows_focus{
                            ui::NavigationShellSelectionFollowsFocus::Disabled};

                        ui::NavigationShell{
                            .header                        = header,
                            .pane_title                    = pane_title,
                            .pane_display_mode             = ui::NavigationShellPaneDisplayMode::Left,
                            .always_show_header            = always_show_header,
                            .settings_visible              = settings_visible,
                            .back_button_visible           = back_visible,
                            .back_enabled                  = back_enabled,
                            .selection_follows_focus       = selection_follows_focus,
                            .expanded_mode_threshold_width = 500,
                            .height                        = 540,
                            .on_selection_changed = [&](const std::string& key) { header = key; },
                            .control              = nav,
                            .content              = ui::Text{.text = "The page"},
                            .items                = {
                                ui::NavItem{.text = "Menu Item1", .icon = ui::IconGlyph{0xE102}},
                                ui::NavItem{.text = "Actions", .kind = ui::NavEntryKind::Header},
                                ui::NavItem{.text = "Menu Item2", .icon = ui::IconGlyph{0xE105}},
                                ui::NavItem{.text = "Menu Item3", .icon = ui::IconGlyph{0xE149}},
                            },
                            .auto_suggest_box = ui::AutoSuggestBox{.query_icon = ui::symbol_glyph(ui::Symbol::Find)},
                        }

                        // The options write the bound properties ...
                        settings_visible = false;
                        back_visible     = ui::AutoVisibility::Collapsed;

                        // ... or go through the handle for the pane's parts and the items.
                        nav->set_auto_suggest_box(nullptr);
                        nav->set_pane_custom_content(
                            ui::build(ui::Button{.text = "More info", .variant = ui::ButtonVariant::Hyperlink}));
                        nav->set_pane_footer_content(ui::build(ui::Button{.text = "Download"}));
                        nav->set_pane_display_mode(ui::NavigationShellPaneDisplayMode::Top);
                        nav->set_selects_on_invoke(2, false); // Menu Item2 can no longer be selected
                    )cpp",
                    .stretch = true,
                    .options = options_panel({
                        check("Settings item visible", true, [&s](bool on) { s.settings_visible = on; }),
                        check("Back button visible",
                              true,
                              [&s](bool on)
                              {
                                  s.back_visible = on ? ui::AutoVisibility::Visible
                                                      : ui::AutoVisibility::Collapsed;
                              }),
                        check("Back button enabled", false, [&s](bool on) { s.back_enabled = on; }),
                        check("AutoSuggestBox visible",
                              true,
                              [&s](bool on)
                              {
                                  if (s.nav != nullptr)
                                  {
                                      s.nav->set_auto_suggest_box(
                                          on ? ui::build(ui::AutoSuggestBox{.query_icon    = ui::symbol_glyph(ui::Symbol::Find),
                                                                            .accessibility = {.name = "search"}})
                                             : nullptr);
                                  }
                              }),
                        ui::Text{.text = "Header:",  .margin = ui::Edges{.top = 12.0f}},
                        ui::TextBox{.text            = s.header,
                                    .accessibility   = {.name = "Header property"}},
                        check("Always show header", true, [&s](bool on) { s.always_show_header = on; }),
                        ui::Text{.text = "Pane title:", .margin = ui::Edges{.top = 12.0f}},
                        ui::TextBox{.text            = s.pane_title,
                                    .accessibility   = {.name = "Pane title"}},
                        check("Pane custom content visible",
                              false,
                              [&s](bool on)
                              {
                                  s.custom_content = on;
                                  if (s.nav != nullptr)
                                  {
                                      s.nav->set_pane_custom_content(
                                          on ? ui::build(ui::Button{.text   = "More info",
                                                                    .variant  = ui::ButtonVariant::Hyperlink,
                                                                    .margin = ui::Symmetric{.horizontal = 12.0f, .vertical = 0.0f}})
                                             : nullptr);
                                  }
                              }),
                        check("Pane footer visible",
                              false,
                              [&s](bool on)
                              {
                                  s.pane_footer_content = on;
                                  if (s.nav != nullptr)
                                  {
                                      const bool top = s.nav->pane_display_mode() == ui::NavigationShellPaneDisplayMode::Top;
                                      s.nav->set_pane_footer_content(on ? api_pane_footer(top) : nullptr);
                                  }
                              }),
                        ui::RadioButtons{.header               = "Pane position:",
                                         .items                = {"Left", "Top"},
                                         .selected_index       = 0,
                                         .margin               = ui::Symmetric{.horizontal = 0.0f, .vertical = 12.0f},
                                         .on_selection_changed = [&s](std::optional<std::size_t> i)
                                         {
                                             const bool top = i == 1;
                                             set_pane_position(s.nav,
                                                               top ? ui::NavigationShellPaneDisplayMode::Top
                                                                   : ui::NavigationShellPaneDisplayMode::Left);
                                             if (s.nav != nullptr && s.pane_footer_content)
                                             {
                                                 s.nav->set_pane_footer_content(api_pane_footer(top));
                                             }
                                         }},
                        check("Selection follows keyboard focus",
                              false,
                              [&s](bool on)
                              {
                                  s.sff = on ? ui::NavigationShellSelectionFollowsFocus::Enabled
                                             : ui::NavigationShellSelectionFollowsFocus::Disabled;
                              }),
                        check("Selection of Menu Item2 suppressed",
                              false,
                              [&s](bool on)
                              {
                                  if (s.nav != nullptr)
                                  {
                                      s.nav->set_selects_on_invoke(2, !on);
                                  }
                              }),
                    }),
                    .controls =
                        {
                            ui::NavigationShell{
                                .header                        = s.header,
                                .pane_title                    = s.pane_title,
                                .pane_display_mode             = ui::NavigationShellPaneDisplayMode::Left,
                                .always_show_header            = s.always_show_header,
                                .settings_visible              = s.settings_visible,
                                .back_button_visible           = s.back_visible,
                                .back_enabled                  = s.back_enabled,
                                .selection_follows_focus       = s.sff,
                                .expanded_mode_threshold_width = 500.0f,
                                .margin                        = ui::Edges{.top = 12.0f},
                                .height                        = 540.0f,
                                .on_selection_changed          = [&s](const std::string& key)
                                {
                                    s.page = page_for(key);
                                    if (s.page.get() > 0)
                                    {
                                        s.header = page_header(s.page.get());
                                    }
                                },
                                .control              = s.nav,
                                .content          = frame(s.page),
                                .items            = {ui::NavItem{.text = "Menu Item1", .key = "page1", .icon = Play},
                                                     ui::NavItem{.text = "Actions", .kind = ui::NavEntryKind::Header},
                                                     ui::NavItem{.text = "Menu Item2", .key = "page2", .icon =  Save},
                                                     ui::NavItem{.text = "Menu Item3", .key = "page3", .icon =  Refresh}},
                                .auto_suggest_box = ui::AutoSuggestBox{.query_icon    = ui::symbol_glyph(ui::Symbol::Find),
                                                                      .accessibility = {.name = "Search"}},
                            },
                        },
                }),
            });
        });
}

// ---- Sections ----

ui::Spec sections_page(Context&)
{
    return examples({
        example({
            .title   = "A basic sections.",
            .code    = R"cpp(
                ui::Sections{
                    .title      = std::string{"EMAIL"},
                    .min_height = 400,
                    .items      = {
                        ui::Section{.header = "All", .content = ui::Text{.text = "all emails go here."}},
                        ui::Section{.header = "Unread", .content = ui::Text{.text = "unread emails go here."}},
                        ui::Section{.header = "Flagged", .content = ui::Text{.text = "flagged emails go here."}},
                        ui::Section{.header = "Urgent", .content = ui::Text{.text = "urgent emails go here."}},
                    },
                }
            )cpp",
            .stretch = true,
            .controls =
                {ui::Sections{.title      = std::string{"EMAIL"},
                              .min_height = 400.0f,
                              .items =
                                  {ui::Section{.header = "All", .content = ui::Text{.text = "all emails go here."}},
                                   ui::Section{.header = "Unread", .content = ui::Text{.text = "unread emails go here."}},
                                   ui::Section{.header = "Flagged", .content = ui::Text{.text = "flagged emails go here."}},
                                   ui::Section{.header = "Urgent", .content = ui::Text{.text = "urgent emails go here."}}}}},
        }),
    });
}

// ---- SelectorBar ----

struct SelectorBarState
{
    ui::Property<int> page{1};
    ui::Property<int> colors{0};
};

ui::Spec color_collection(int which)
{
    constexpr std::array<std::pair<ui::Color, int>, 3> Collections{{
        {named::Pink, 5},
        {named::Plum, 7},
        {named::PowderBlue, 4},
    }};
    const auto [color, count] = Collections[static_cast<std::size_t>(which)];
    ui::SpecList swatches;
    for (int i = 0; i < count; ++i)
    {
        auto swatch = ui::build(Swatch{.fill = color, .width = 112.0f, .height = 82.0f});
        swatch->set_margin(ui::Thickness{4.0f});
        swatches.nodes.push_back(std::move(swatch));
    }
    return ui::Scroll{.content_orientation    = ui::ScrollContentOrientation::Horizontal,
                      .horizontal_scroll_mode = ui::ScrollMode::Enabled,
                      .vertical_scroll_mode   = ui::ScrollMode::Disabled,
                      .content                = ui::HorizontalStack{.content = std::move(swatches)}};
}

ui::Spec selector_bar_page(Context&)
{
    return with_state<SelectorBarState>(
        [](SelectorBarState& s)
        {
            return examples({
                example({
                    .title = "A Basic SelectorBar",
                    .code  = R"cpp(
                        ui::SelectorBar{
                            .items = {{"Recent", ui::IconGlyph{0xE121}},
                                      {"Shared", ui::IconGlyph{0xE72D}},
                                      {"Favorites", ui::IconGlyph{0xE113}}},
                        }
                    )cpp",
                    .controls = {ui::SelectorBar{.items = {{"Recent", Clock}, {"Shared", Share}, {"Favorites", Favorite}}}},
                }),
                example({
                    .title   = "SelectorBar with Frame Slide Transitions",
                    .code    = R"cpp(
                        ui::Property<int> page{1};

                        ui::VerticalStack{
                            .content = {
                                ui::SelectorBar{
                                    .items          = {"Page1", "Page2", "Page3", "Page4", "Page5"},
                                    .selected_index = ui::bind_choice(page, {1, 2, 3, 4, 5}),
                                },
                                // Rebuilt whenever `page` changes; make_page(n) is your own page factory.
                                ui::Match<int>{
                                    .value   = page,
                                    .content = [](const int& n) { return make_page(n); },
                                },
                            },
                        }
                    )cpp",
                    .stacked = true,
                    .controls =
                        {
                            ui::SelectorBar{.items          = {"Page1", "Page2", "Page3", "Page4", "Page5"},
                                            .selected_index = ui::bind_choice(s.page, {1, 2, 3, 4, 5})},
                            frame(s.page),
                        },
                }),
                example({
                    .title   = "SelectorBar Displaying Different Collections",
                    .code    = R"cpp(
                        ui::Property<int> collection{0};

                        ui::VerticalStack{
                            .content = {
                                ui::SelectorBar{
                                    .items                = {"Pink", "Plum", "PowderBlue"},
                                    .on_selection_changed = [&](std::size_t i) { collection = static_cast<int>(i); },
                                },
                                ui::Match<int>{
                                    .value   = collection,
                                    .content = [](const int& which)
                                    {
                                        ui::SpecList swatches;
                                        // ... one colored rectangle per item of collection `which` ...
                                        return ui::Scroll{
                                            .content_orientation    = ui::ScrollContentOrientation::Horizontal,
                                            .horizontal_scroll_mode = ui::ScrollMode::Enabled,
                                            .vertical_scroll_mode   = ui::ScrollMode::Disabled,
                                            .content                = ui::HorizontalStack{.content = std::move(swatches)},
                                        };
                                    },
                                },
                            },
                        }
                    )cpp",
                    .stacked = true,
                    .controls =
                        {
                            ui::SelectorBar{.items                = {"Pink", "Plum", "PowderBlue"},
                                            .on_selection_changed = [&s](std::size_t i)
                                            { s.colors = static_cast<int>(i); }},
                            ui::Match<int>{.value   = s.colors,
                                           .content = [](const int& which) { return color_collection(which); }},
                        },
                }),
            });
        });
}

// ---- Tabs ----

ui::Tab document_tab(int index, ui::IconGlyph icon = Document, const std::string& prefix = "Document ")
{
    return ui::Tab{.header  = prefix + std::to_string(index),
                   .icon    = icon,
                   .content = std::move(sample_page(index % 3 + 1)).build()};
}

// Tabs_Loaded: three "Document n" tabs.
ui::TabList document_tabs(int count, ui::IconGlyph icon = Document, const std::string& prefix = "Document ")
{
    ui::TabList list;
    for (int i = 0; i < count; ++i)
    {
        list.add(document_tab(i, icon, prefix));
    }
    return list;
}

// Three fixed tabs with sample page 1 to 3 content, as the width-mode and close-button cards declare.
ui::TabList home_tabs(bool closable)
{
    constexpr std::array<std::pair<const char*, ui::IconGlyph>, 3> Tabs{{
        {"Home", Home},
        {"Tab 2 Has Longer Text", MusicInfo},
        {"Third Tab", Placeholder},
    }};
    ui::TabList                                                    list;
    for (std::size_t i = 0; i < Tabs.size(); ++i)
    {
        list.entries.push_back(
            ui::TabList::Entry{Tabs[i].first, std::move(sample_page(static_cast<int>(i) + 1)).build(), Tabs[i].second, closable});
    }
    return list;
}

// The color-icon card's console, PowerShell and Linux icons, drawn rather than shipped.
ui::Bitmap console_icon(int kind)
{
    constexpr int Size = 32;
    ui::Bitmap    img(Size, Size);
    const auto    inside_rounded = [](int x, int y, float r)
    {
        const float cx = std::clamp(static_cast<float>(x) + 0.5f, r, Size - r);
        const float cy = std::clamp(static_cast<float>(y) + 0.5f, r, Size - r);
        const float dx = static_cast<float>(x) + 0.5f - cx;
        const float dy = static_cast<float>(y) + 0.5f - cy;
        return dx * dx + dy * dy <= r * r;
    };
    for (int y = 0; y < Size; ++y)
    {
        for (int x = 0; x < Size; ++x)
        {
            const float fx = static_cast<float>(x) + 0.5f;
            const float fy = static_cast<float>(y) + 0.5f;
            ui::Color   c  = ui::colors::Transparent;
            if (kind == 0) // Command Prompt: a black console under a gray title bar, with a white ">_"
            {
                if (inside_rounded(x, y, 3.0f))
                {
                    c = y < 7 ? ui::Color::from_rgba(0x9A, 0x9A, 0x9A) : ui::Color::from_rgba(0x1E, 0x1E, 0x1E);
                    const bool chevron = fy > 11.0f && fy < 23.0f &&
                                         std::abs(fx - 6.0f - (6.0f - std::abs(fy - 17.0f))) < 1.3f;
                    const bool cursor  = fy > 20.5f && fy < 22.5f && fx > 13.0f && fx < 20.0f;
                    if (chevron || cursor)
                    {
                        c = ui::colors::White;
                    }
                }
            }
            else if (kind == 1) // PowerShell: a navy tile with a white ">" and "_"
            {
                if (inside_rounded(x, y, 4.0f))
                {
                    c                  = ui::Color::from_rgba(0x01, 0x24, 0x56);
                    const bool chevron = fy > 9.0f && fy < 23.0f &&
                                         std::abs(fx - 8.0f - (7.0f - std::abs(fy - 16.0f))) < 1.6f;
                    const bool cursor  = fy > 20.5f && fy < 23.0f && fx > 16.0f && fx < 24.0f;
                    if (chevron || cursor)
                    {
                        c = ui::colors::White;
                    }
                }
            }
            else // Linux: a penguin with a black body, white belly, orange beak and feet
            {
                const auto in_ellipse = [&](float cx, float cy, float rx, float ry)
                {
                    const float dx = (fx - cx) / rx;
                    const float dy = (fy - cy) / ry;
                    return dx * dx + dy * dy <= 1.0f;
                };
                if (in_ellipse(16.0f, 17.0f, 10.0f, 13.0f))
                {
                    c = ui::Color::from_rgba(0x20, 0x20, 0x20);
                }
                if (in_ellipse(16.0f, 21.0f, 6.5f, 8.0f))
                {
                    c = ui::Color::from_rgba(0xF4, 0xF4, 0xF4);
                }
                if (in_ellipse(13.0f, 10.0f, 1.6f, 2.0f) || in_ellipse(19.0f, 10.0f, 1.6f, 2.0f))
                {
                    c = ui::colors::White;
                }
                if (in_ellipse(16.0f, 13.5f, 3.0f, 1.6f) || in_ellipse(10.0f, 29.5f, 4.0f, 2.0f) ||
                    in_ellipse(22.0f, 29.5f, 4.0f, 2.0f))
                {
                    c = ui::Color::from_rgba(0xF5, 0xA6, 0x23);
                }
            }
            img.set(x, y, c);
        }
    }
    return img;
}

// The accent-strip card's strip background: the accent ramp's light2 in Light, dark2 in Dark,
// re-read from the theme on every frame so a theme or accent change repaints it.
class AccentStrip final : public ui::ContentHost
{
public:
    explicit AccentStrip(const ui::Control<ui::Tabs>* tabs) : tabs_(tabs)
    {
    }

    void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
    {
        const ui::Theme& theme = context.theme;
        if (const ui::TabsControl* tabs = *tabs_)
        {
            const ui::Color strip = theme.dark ? theme.accent_ramp.dark2 : theme.accent_ramp.light2;
            renderer.fill_rounded_rect(tabs->strip_bounds(), 0.0f, strip);
        }
        ui::ContentHost::render_override(renderer, context);
    }

private:
    const ui::Control<ui::Tabs>* tabs_;
};

struct TabsState
{
    ui::Control<ui::Tabs>                        tabs1; // adding, closing, rearranging
    ui::Control<ui::Tabs>                        tabs2; // declared up front
    ui::Control<ui::Tabs>                        tabs3; // bound to MyData
    ui::Control<ui::Tabs>                        tabs4; // keyboarding
    ui::Control<ui::Tabs>                        tabs5; // strip header/footer
    ui::Control<ui::Tabs>                        accent;
    int                                          my_data = 3; // the MyData collection's size
    ui::Property<ui::TabsWidthMode>              width_mode{ui::TabsWidthMode::SizeToContent};
    ui::Property<ui::TabsCloseButtonOverlayMode> overlay_mode{ui::TabsCloseButtonOverlayMode::Always};
};

// Tabs_AddButtonClick / Tabs_TabCloseRequested, shared by every card that has them.
ui::Callback<void()> add_document(ui::Control<ui::Tabs>& tabs)
{
    return [&tabs]
    {
        if (tabs != nullptr)
        {
            tabs->add_tab(document_tab(static_cast<int>(tabs->tab_count())));
        }
    };
}

std::unique_ptr<ui::Element> keyboarding_tabs(TabsState& s)
{
    std::unique_ptr<ui::Element> node = ui::build(
        ui::Tabs{.min_height              = 475.0f,
                 .on_add_tab_button_click = add_document(s.tabs4),
                 .control                 = s.tabs4,
                 .items                   = document_tabs(3)});
    ui::TabsControl* tabs = s.tabs4;
    tabs->add_shortcut({.keys       = ui::Ctrl + ui::Key::T,
                        .on_pressed = [tabs] { tabs->add_tab(document_tab(static_cast<int>(tabs->tab_count()))); }});
    tabs->add_shortcut({.keys       = ui::Ctrl + ui::Key::W,
                        .on_pressed = [tabs]
                        {
                            const std::optional<std::size_t> selected = tabs->selected_index();
                            if (selected && tabs->closable(*selected))
                            {
                                tabs->remove_tab(*selected);
                            }
                        }});
    constexpr std::array<ui::Key, 9> Numbers{ui::Key::Number1,
                                             ui::Key::Number2,
                                             ui::Key::Number3,
                                             ui::Key::Number4,
                                             ui::Key::Number5,
                                             ui::Key::Number6,
                                             ui::Key::Number7,
                                             ui::Key::Number8,
                                             ui::Key::Number9};
    for (std::size_t n = 0; n < Numbers.size(); ++n)
    {
        tabs->add_shortcut({.keys       = ui::Ctrl + Numbers[n],
                            .on_pressed = [tabs, n]
                            {
                                // Ctrl+9 is the last tab, however many there are.
                                const std::size_t target = n == 8 ? tabs->tab_count() - 1 : n;
                                if (target < tabs->tab_count())
                                {
                                    tabs->set_selected_index(target);
                                }
                            }});
    }
    return node;
}

ui::Spec tabs_page(Context&)
{
    return with_state<TabsState>(
        [](TabsState& s)
        {
            auto accent = std::make_unique<AccentStrip>(&s.accent);
            accent->set_child(ui::build(ui::Tabs{.min_height              = 475.0f,
                                                 .on_add_tab_button_click = add_document(s.accent),
                                                 .control                 = s.accent,
                                                 .items                   = document_tabs(3)}));

            return examples({
                example({
                    .title    = "A Tabs control with support for adding, closing, and rearranging tabs",
                    .code     = R"cpp(
                        ui::Control<ui::Tabs> tabs;

                        // A new "Document n" tab.
                        auto document = [](std::size_t n)
                        {
                            return ui::Tab{.header  = std::format("Document {}", n),
                                           .icon    = ui::IconGlyph{0xE130},
                                           .content = ui::Text{.text = "The document"}};
                        };

                        ui::Tabs{
                            .min_height              = 475,
                            // A close button closes its tab; the "+" shows because there is an add handler.
                            .on_add_tab_button_click = [&] { tabs->add_tab(document(tabs->tab_count())); },
                            .control                 = tabs,
                            .items                    = {
                                ui::Tab{.header = "Document 0", .icon = ui::IconGlyph{0xE130}, .content = ui::Text{.text = "The document"}},
                                ui::Tab{.header = "Document 1", .icon = ui::IconGlyph{0xE130}, .content = ui::Text{.text = "The document"}},
                                ui::Tab{.header = "Document 2", .icon = ui::IconGlyph{0xE130}, .content = ui::Text{.text = "The document"}},
                            },
                        }
                        // Dragging a tab reorders the strip (can_reorder_tabs is on by default).
                    )cpp",
                    .stretch  = true,
                    .controls = {ui::Tabs{.min_height              = 475.0f,
                                             .on_add_tab_button_click = add_document(s.tabs1),
                                             .control                     = s.tabs1,
                                             .items                    = document_tabs(3)}},
                }),
                example({
                    .title    = "A Tabs control with its tabs declared up front",
                    .code     = R"cpp(
                        ui::Control<ui::Tabs> tabs;

                        ui::Tabs{
                            .min_height              = 475,
                            .on_add_tab_button_click = [&]
                            {
                                tabs->add_tab(ui::Tab{.header  = std::format("Document {}", tabs->tab_count()),
                                                      .icon    = ui::IconGlyph{0xE130},
                                                      .content = ui::Text{.text = "A new document"}});
                            },
                            .control = tabs,
                            .items    = {
                                ui::Tab{.header  = "Document 0",
                                        .icon    = ui::IconGlyph{0xE18A},
                                        .content = ui::Text{.text = "The first document"}},
                                ui::Tab{.header  = "Document 1",
                                        .icon    = ui::IconGlyph{0xE18A},
                                        .content = ui::Text{.text = "The second document"}},
                                ui::Tab{.header  = "Document 2",
                                        .icon    = ui::IconGlyph{0xE18A},
                                        .content = ui::Text{.text = "The third document"}},
                            },
                        }
                    )cpp",
                    .stretch  = true,
                    .controls = {ui::Tabs{.min_height              = 475.0f,
                                             .on_add_tab_button_click = add_document(s.tabs2),
                                             .control                     = s.tabs2,
                                             .items                    = document_tabs(3, Placeholder)}},
                }),
                // Bound tabs: the tabs mirror a MyData collection, which grows on "+" and
                // shrinks on close.
                example({
                    .title    = "A Tabs control bound to a collection of MyData objects",
                    .code     = R"cpp(
                        ui::Control<ui::Tabs> tabs;
                        int                      my_data = 3; // how many data objects there are

                        auto data_tab = [](int n)
                        {
                            return ui::Tab{.header  = std::format("MyData Doc {}", n),
                                           .icon    = ui::IconGlyph{0xE18A},
                                           .content = ui::Text{.text = "The data object's page"}};
                        };

                        // One tab per data object.
                        ui::TabList list;
                        for (int n = 0; n < my_data; ++n)
                        {
                            list.add(data_tab(n));
                        }

                        ui::Tabs{
                            .min_height = 475,
                            // The collection and the strip change together.
                            .on_tab_close_requested = [&](const std::string& key)
                            {
                                --my_data;
                                tabs->remove_tab(key);
                            },
                            .on_add_tab_button_click = [&] { tabs->add_tab(data_tab(my_data++)); },
                            .control                 = tabs,
                            .items                    = std::move(list),
                        }
                    )cpp",
                    .stretch  = true,
                    .controls = {ui::Tabs{.min_height             = 475.0f,
                                             .on_tab_close_requested = [&s](const std::string& key)
                                             {
                                                 --s.my_data;
                                                 s.tabs3->remove_tab(key);
                                             },
                                             .on_add_tab_button_click = [&s]
                                             { s.tabs3->add_tab(document_tab(s.my_data++, Placeholder, "MyData Doc ")); },
                                             .control  = s.tabs3,
                                             .items = document_tabs(3, Placeholder, "MyData Doc ")}},
                }),
                example({
                    .title   = "A Tabs control with keyboarding support",
                    .code    = R"cpp(
                        ui::Control<ui::Tabs> handle;

                        std::unique_ptr<ui::Element> node = ui::build(ui::Tabs{
                            .min_height             = 475,
                            .control                = handle,
                            .items                   = {ui::Tab{.header = "Document 0", .content = ui::Text{.text = "The document"}}},
                        });
                        ui::TabsControl* tabs = handle;

                        // Ctrl+T opens a new tab.
                        tabs->add_shortcut({.keys     = ui::Ctrl + ui::Key::T,
                                                        .on_pressed = [tabs]
                                                        {
                                                            tabs->add_tab(ui::Tab{
                                                                .header  = std::format("Document {}", tabs->tab_count()),
                                                                .content = ui::Text{.text = "A new document"}});
                                                            return true;
                                                        }});
                        // Ctrl+W closes the selected tab.
                        tabs->add_shortcut({.keys     = ui::Ctrl + ui::Key::W,
                                                        .on_pressed = [tabs]
                                                        {
                                                            const std::size_t selected = tabs->selected_index();
                                                            if (selected < tabs->tab_count() && tabs->closable(selected))
                                                            {
                                                                tabs->remove_tab(selected);
                                                            }
                                                            return true;
                                                        }});
                        // Ctrl+1 selects the first tab; the same for 2 to 8, and Ctrl+9 for the last tab.
                        tabs->add_shortcut({.keys     = ui::Ctrl + ui::Key::Number1,
                                                        .on_pressed = [tabs]
                                                        {
                                                            tabs->set_selected_index(0);
                                                            return true;
                                                        }});
                    )cpp",
                    .stacked = true,
                    .controls =
                        {
                            ui::VerticalStack{.child_alignment = ui::Alignment::Stretch,
                                         .content = {paragraph("- Ctrl+T opens a new tab"),
                                                         paragraph("- Ctrl+W closes the selected tab"),
                                                         paragraph("- Ctrl+1 to Ctrl+8 selects that number tab"),
                                                         paragraph("- Ctrl+9 selects the last tab (regardless of the number of tabs)")}},
                            keyboarding_tabs(s),
                        },
                }),
                example({
                    .title   = "You can put custom content in the tab strip header and footer",
                    .code    = R"cpp(
                        ui::Control<ui::Tabs> tabs;

                        ui::Tabs{
                            .tab_width_mode         = ui::TabsWidthMode::SizeToContent,
                            .min_height             = 475,
                            .control                = tabs,
                            .items                   = {
                                ui::Tab{.header = "Document 0", .content = ui::Text{.text = "The first document"}},
                                ui::Tab{.header = "Document 1", .content = ui::Text{.text = "The second document"}},
                            },
                            // Left of the tabs.
                            .tab_strip_header_content = ui::Text{.text               = "Tab strip header content",
                                                         .role               = ui::TextRole::BodyStrong,
                                                         .margin             = ui::Symmetric{.horizontal = 8, .vertical = 6},
                                                         .vertical_alignment = ui::Alignment::Center},
                            // In the space right of the add button.
                            .tab_strip_footer_content = ui::Text{.text                 = "Tab strip footer content",
                                                         .role                 = ui::TextRole::BodyStrong,
                                                         .margin               = ui::Thickness{6},
                                                         .horizontal_alignment = ui::Alignment::Trailing,
                                                         .vertical_alignment   = ui::Alignment::Center},
                        }
                    )cpp",
                    .stacked = true,
                    .controls =
                        {
                            paragraph("You can put any content in the tab strip header and footer areas"),
                            ui::Tabs{.tab_width_mode          = ui::TabsWidthMode::SizeToContent,
                                        .min_height              = 475.0f,
                                        .on_add_tab_button_click = add_document(s.tabs5),
                                        .control                     = s.tabs5,
                                        .items                    = document_tabs(3),
                                        .tab_strip_header_content        = ui::Text{.text    = "Tab strip header content",
                                                                            .role    = ui::TextRole::BodyStrong,
                                                                            .margin  = ui::Symmetric{.horizontal = 8.0f, .vertical = 6.0f},
                                                                            .vertical_alignment = ui::Alignment::Center},
                                        .tab_strip_footer_content        = ui::Text{.text    = "Tab strip footer content",
                                                                            .role    = ui::TextRole::BodyStrong,
                                                                            .margin  = ui::Thickness{6.0f},
                                                                            .horizontal_alignment = ui::Alignment::Trailing,
                                                                            .vertical_alignment = ui::Alignment::Center}},
                        },
                }),
                example({
                    .title   = "Tab widths can either be equally sized, sized to the content of the tab, or sized to only "
                               "show the icon when unselected",
                    .code    = R"cpp(
                        ui::Property<ui::TabsWidthMode> width_mode{ui::TabsWidthMode::SizeToContent};

                        ui::Tabs{
                            .tab_width_mode            = width_mode,
                            .min_height                = 475,
                            .items                      = {
                                ui::Tab{.header   = "Home",
                                        .icon     = ui::IconGlyph{0xE10F},
                                        .closable = false,
                                        .content  = ui::Text{.text = "Home"}},
                                ui::Tab{.header   = "Tab 2 Has Longer Text",
                                        .icon     = ui::IconGlyph{0xE142},
                                        .closable = false,
                                        .content  = ui::Text{.text = "The second tab"}},
                                ui::Tab{.header   = "Third Tab",
                                        .icon     = ui::IconGlyph{0xE18A},
                                        .closable = false,
                                        .content  = ui::Text{.text = "The third tab"}},
                            },
                        }

                        // SizeToContent, Equal, or Compact (unselected tabs show only their icon).
                        width_mode = ui::TabsWidthMode::Compact;
                    )cpp",
                    .stretch = true,
                    .options = ui::ComboBox{.header           = "Tab width mode",
                                            .items            = {"SizeToContent", "Equal", "Compact"},
                                            .selected_index   = 0,
                                            .width            = 150.0f,
                                            .vertical_alignment          = ui::Alignment::Leading,
                                            .on_selection_changed = [&s](std::optional<std::size_t> i)
                                            {
                                                constexpr std::array<ui::TabsWidthMode, 3> Modes{
                                                    ui::TabsWidthMode::SizeToContent, ui::TabsWidthMode::Equal, ui::TabsWidthMode::Compact};
                                                s.width_mode = Modes[i.value_or(0)];
                                            }},
                    .controls = {ui::Tabs{.tab_width_mode            = s.width_mode,
                                             .min_height                = 475.0f,
                                             .items                      = home_tabs(false)}},
                }),
                example({
                    .title   = "The close button can be persistent or only visible on hover",
                    .code    = R"cpp(
                        ui::Property<ui::TabsCloseButtonOverlayMode> overlay_mode{
                            ui::TabsCloseButtonOverlayMode::Always};

                        ui::Tabs{
                            .close_button_overlay_mode = overlay_mode,
                            .min_height                = 475,
                            .items                      = {
                                ui::Tab{.header = "Home", .icon = ui::IconGlyph{0xE10F}, .content = ui::Text{.text = "Home"}},
                                ui::Tab{.header  = "Tab 2 Has Longer Text",
                                        .icon    = ui::IconGlyph{0xE142},
                                        .content = ui::Text{.text = "The second tab"}},
                                ui::Tab{.header  = "Third Tab",
                                        .icon    = ui::IconGlyph{0xE18A},
                                        .content = ui::Text{.text = "The third tab"}},
                            },
                        }

                        // Auto, Always, or OnPointerOver (only while the pointer is over the tab).
                        overlay_mode = ui::TabsCloseButtonOverlayMode::OnPointerOver;
                    )cpp",
                    .stretch = true,
                    .options = ui::ComboBox{.header           = "Close button overlay mode",
                                            .items            = {"Auto", "Always", "OnPointerOver"},
                                            .selected_index   = 1,
                                            .width            = 150.0f,
                                            .vertical_alignment          = ui::Alignment::Leading,
                                            .on_selection_changed = [&s](std::optional<std::size_t> i)
                                            {
                                                constexpr std::array<ui::TabsCloseButtonOverlayMode, 3> Modes{
                                                    ui::TabsCloseButtonOverlayMode::Auto,
                                                    ui::TabsCloseButtonOverlayMode::Always,
                                                    ui::TabsCloseButtonOverlayMode::OnPointerOver};
                                                s.overlay_mode = Modes[i.value_or(1)];
                                            }},
                    .controls = {ui::Tabs{.close_button_overlay_mode = s.overlay_mode,
                                             .min_height                = 475.0f,
                                             .items                      = home_tabs(true)}},
                }),
                example({
                    .title   = "Tabs with color tab icons",
                    .code    = R"cpp(
                        // Three ui::Bitmap pictures, loaded or drawn by the app.
                        ui::Bitmap cmd_icon, powershell_icon, linux_icon;

                        ui::Tabs{
                            .tab_width_mode            = ui::TabsWidthMode::SizeToContent,
                            .min_width                 = 490,
                            // A picture icon keeps its own colors.
                            .items = {
                                ui::Tab{.header = "CMD Prompt", .icon = cmd_icon, .closable = false},
                                ui::Tab{.header = "PowerShell", .icon = powershell_icon, .closable = false},
                                ui::Tab{.header = "Windows Subsystem for Linux", .icon = linux_icon, .closable = false},
                            },
                        }
                    )cpp",
                    .stacked = true,
                    .controls =
                        {
                            paragraph("Give a tab an image instead of a glyph to display a full color icon in the Tab"),
                            [] {
                                constexpr std::array<const char*, 3> Shells{
                                    "CMD Prompt", "PowerShell", "Windows Subsystem for Linux"};
                                ui::TabList list;
                                for (int i = 0; i < 3; ++i)
                                {
                                    list.entries.push_back(ui::TabList::Entry{
                                        Shells[static_cast<std::size_t>(i)], nullptr, console_icon(i), false});
                                }
                                return ui::Spec{ui::Tabs{.tab_width_mode            = ui::TabsWidthMode::SizeToContent,
                                                            .min_width                 = 490.0f,
                                                            .items                      = std::move(list)}};
                            }(),
                        },
                }),
                example({
                    .title    = "A Tabs control with an accent colored tab strip background",
                    .code     = R"cpp(
                        // A host that paints the accent color behind the strip of the Tabs inside it.
                        // It reads the theme on every frame, so a theme or accent change repaints it.
                        class AccentStrip final : public ui::ContentHost
                        {
                        public:
                            explicit AccentStrip(const ui::Control<ui::Tabs>* tabs) : tabs_(tabs) {}

                            void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
                            {
                                const ui::Theme& theme = context.theme;
                                if (const ui::TabsControl* tabs = *tabs_)
                                {
                                    const ui::Color strip = theme.dark ? theme.accent_ramp.dark2
                                                                               : theme.accent_ramp.light2;
                                    renderer.fill_rounded_rect(tabs->strip_bounds(), 0.0f, strip);
                                }
                                ui::ContentHost::render_override(renderer, context);
                            }

                        private:
                            const ui::Control<ui::Tabs>* tabs_;
                        };

                        ui::Control<ui::Tabs> tabs;

                        auto host = std::make_unique<AccentStrip>(&tabs);
                        host->set_child(ui::build(ui::Tabs{
                            .min_height             = 475,
                            .control                = tabs,
                            .items                   = {ui::Tab{.header = "Document 0", .content = ui::Text{.text = "The document"}}},
                        }));
                    )cpp",
                    .stretch  = true,
                    .controls = {std::move(accent)},
                }),
            });
        });
}

// ---- Carousel ------------------------------------------------------------------------------------

ui::Spec flip_photos(ui::Orientation orientation)
{
    ui::SpecList photos;
    // cliff, grapes, rainier, sunset, valley.
    for (std::size_t i = 0; i < 5; ++i)
    {
        photos.push_back(photo(i, 0.0f, 0.0f, ui::Stretch::Uniform));
    }
    return ui::Carousel{.orientation = orientation, .height = 270.0f, .max_width = 400.0f, .content = std::move(photos)};
}

ui::Spec flip_view_page(Context&)
{
    // The Carousel sample's items: the controls of the gallery's first three groups.
    ui::SpecList bound;
    std::size_t  group_index = 0;
    for (const Group& group : groups())
    {
        if (group_index++ >= 3)
        {
            break;
        }
        const ui::IconGlyph glyph = group_index == 1   ? ui::symbol_glyph(ui::Symbol::More)
                                    : group_index == 2 ? ui::symbol_glyph(ui::Symbol::List)
                                                       : ui::symbol_glyph(ui::Symbol::Edit);
        for (const Page& page : group.pages)
        {
            bound.nodes
                .push_back(
                    ui::Spec(
                        ui::Grid{
                            .rows = {1_fr, ui::AutoSize},
                            .content =
                                {
                                    ui::Cell{.row     = 0,
                                             .column  = 0,
                                             .content = ui::Icon{.source             = glyph,
                                                                 .size               = 36.0f,
                                                                 .vertical_alignment = ui::Alignment::Center}},
                                    ui::Cell{.row    = 1,
                                             .column = 0,
                                             .content = ui::Border{.background = ui::Color::from_argb(0xA5FFFFFF),
                                                                   .height     = 60.0f,
                                                                   .content = ui::Text{.text = page.name,
                                                                                       .role = ui::TextRole::Title,
                                                                                       .foreground = ui::colors::Black,
                                                                                       .margin = ui::Thickness{12.0f},
                                                                                       .horizontal_alignment = ui::Alignment::Center}}},
                                },
                        })
                        .take());
        }
    }
    return examples({
        example({.title    = "A simple Carousel with items declared inline.",
                 .code     = R"cpp(
                     ui::Carousel{
                         .height    = 270,
                         .max_width = 400,
                         .content     = {
                             ui::Image{.source = ui::Bitmap::read("cliff.jpg")},
                             ui::Image{.source = ui::Bitmap::read("grapes.jpg")},
                             ui::Image{.source = ui::Bitmap::read("rainier.jpg")},
                             ui::Image{.source = ui::Bitmap::read("sunset.jpg")},
                             ui::Image{.source = ui::Bitmap::read("valley.jpg")},
                         },
                     }
                 )cpp",
                 .stretch  = true,
                 .controls = {flip_photos(ui::Orientation::Horizontal)}}),
        example({.title    = "A Carousel showing bound data with an item template.",
                 .code     = R"cpp(
                     // The pages are children, so one is built per item of the data.
                     ui::SpecList pages;
                     for (const ControlInfo& info : controls)
                     {
                         pages.push_back(ui::Grid{
                                 .rows  = {1_fr, ui::AutoSize},
                                 .content = {
                                     ui::Cell{.row = 0, .column = 0, .content = ui::Icon{.source = info.glyph, .size = 36}},
                                     ui::Cell{.row     = 1,
                                      .column  = 0,
                                      .content = ui::Border{
                                          .background = ui::Color::from_argb(0xA5FFFFFF),
                                          .height     = 60,
                                          .content    = ui::Text{.text = info.name, .role = ui::TextRole::Title},
                                      }},
                                 },
                             });
                     }

                     ui::Carousel{
                         .border_brush     = ui::colors::Black,
                         .border_thickness = ui::Thickness{1.0f},
                         .height           = 180,
                         .max_width        = 400,
                         .content            = std::move(pages),
                     }
                 )cpp",
                 .stretch  = true,
                 .controls = {ui::Carousel{.border_brush     = ui::colors::Black,
                                           .border_thickness = ui::Thickness{1.0f},
                                           .height           = 180.0f,
                                           .max_width        = 400.0f,
                                           .content          = std::move(bound)}}}),
        example({.title    = "Vertical Carousel",
                 .code     = R"cpp(
                     ui::Carousel{
                         .orientation = ui::Orientation::Vertical,
                         .height      = 270,
                         .max_width   = 400,
                         .content       = {
                             ui::Image{.source = ui::Bitmap::read("cliff.jpg")},
                             ui::Image{.source = ui::Bitmap::read("grapes.jpg")},
                             ui::Image{.source = ui::Bitmap::read("rainier.jpg")},
                         },
                     }
                 )cpp",
                 .stretch  = true,
                 .controls = {flip_photos(ui::Orientation::Vertical)}}),
    });
}

// ---- PipsPager ----

struct PipsPagerState
{
    ui::Property<std::size_t>                   picture{0};
    ui::Property<ui::Orientation>               orientation{ui::Orientation::Horizontal};
    ui::Property<ui::PipsPagerButtonVisibility> previous{ui::PipsPagerButtonVisibility::Visible};
    ui::Property<ui::PipsPagerButtonVisibility> next{ui::PipsPagerButtonVisibility::Visible};
    ui::Property<ui::PipsPagerWrapMode>         wrap{ui::PipsPagerWrapMode::None};
};

// Eight landscape pictures.
ui::SpecList landscape_items()
{
    ui::SpecList items;
    for (std::size_t i = 0; i < 8; ++i)
    {
        items.push_back(ui::Image{.source        = sample_photo(i),
                                  .stretch       = ui::Stretch::Uniform,
                                  .accessibility = {.name = "Landscape " + std::to_string(i + 1)}});
    }
    return items;
}

ui::Spec button_visibility_picker(const char* header, ui::Property<ui::PipsPagerButtonVisibility>& target)
{
    return ui::ComboBox{.header         = header,
                        .items          = {"Visible", "VisibleOnPointerOver", "Collapsed"},
                        .selected_index = ui::bind_choice(target,
                                                          {ui::PipsPagerButtonVisibility::Visible,
                                                           ui::PipsPagerButtonVisibility::VisibleOnPointerOver,
                                                           ui::PipsPagerButtonVisibility::Collapsed})};
}

ui::Spec pips_pager_page(Context&)
{
    return with_state<PipsPagerState>(
        [](PipsPagerState& s)
        {
            return examples({
                example({
                    .title = "PipsPager integrated with a Carousel",
                    .code  = R"cpp(
                        ui::Property<std::size_t> picture{0};

                        // Both controls bind the same property two-way, so each follows the other.
                        ui::VerticalStack{
                            .child_alignment = ui::Alignment::Stretch,
                            .content         = {
                                ui::Carousel{
                                    .selected_index = picture,
                                    .height         = 270,
                                    .max_width      = 400,
                                    .content          = std::move(pictures), // eight ui::Image specs
                                },
                                ui::PipsPager{
                                    .selected_index       = picture,
                                    .number_of_pages      = 8,
                                    .margin               = ui::Edges{.top = 12},
                                    .horizontal_alignment = ui::Alignment::Center,
                                },
                            },
                        }
                    )cpp",
                    .controls =
                        {
                            ui::VerticalStack{
                                .child_alignment = ui::Alignment::Stretch,
                                .content =
                                    {
                                        // Both selected_index fields are bound two-way to the same property.
                                        ui::Carousel{.selected_index = s.picture,
                                                     .height         = 270.0f,
                                                     .max_width      = 400.0f,
                                                     .content        = landscape_items()},
                                        ui::PipsPager{.selected_index       = s.picture,
                                                      .number_of_pages      = 8,
                                                      .margin               = ui::Edges{.top = 12.0f},
                                                      .horizontal_alignment = ui::Alignment::Center},
                                    },
                            },
                        },
                }),
                example({
                    .title = "PipsPager with options to change its orientation, button visibility, and wrap mode.",
                    .code  = R"cpp(
                        ui::Property<ui::Orientation>               orientation{ui::Orientation::Horizontal};
                        ui::Property<ui::PipsPagerButtonVisibility> previous{ui::PipsPagerButtonVisibility::Visible};
                        ui::Property<ui::PipsPagerButtonVisibility> next{ui::PipsPagerButtonVisibility::Visible};
                        ui::Property<ui::PipsPagerWrapMode>         wrap{ui::PipsPagerWrapMode::None};

                        ui::PipsPager{
                            .number_of_pages            = 10,
                            .orientation                = orientation,
                            .previous_button_visibility = previous,
                            .next_button_visibility     = next,
                            .wrap_mode                  = wrap,
                        }

                        ui::ComboBox{
                            .header         = "Wrap Mode",
                            .items          = {"None", "Wrap"},
                            .selected_index = ui::bind_choice(wrap, {ui::PipsPagerWrapMode::None, ui::PipsPagerWrapMode::Wrap}),
                        }
                    )cpp",
                    .options = options_panel(0.0f,
                                             {
                                                 ui::ComboBox{.header = "Orientation",
                                                              .items  = {"Horizontal", "Vertical"},
                                                              .selected_index = ui::bind_choice(s.orientation,
                                                                                                {ui::Orientation::Horizontal,
                                                                                                 ui::Orientation::Vertical})},
                                                 button_visibility_picker("Previous Button Visibility", s.previous),
                                                 button_visibility_picker("Next Button Visibility", s.next),
                                                 ui::ComboBox{.header = "Wrap Mode",
                                                              .items  = {"None", "Wrap"},
                                                              .selected_index = ui::bind_choice(s.wrap,
                                                                                                {ui::PipsPagerWrapMode::None,
                                                                                                 ui::PipsPagerWrapMode::Wrap})},
                                             }),
                    .controls =
                        {
                            ui::PipsPager{.number_of_pages            = 10,
                                          .orientation                = s.orientation,
                                          .previous_button_visibility = s.previous,
                                          .next_button_visibility     = s.next,
                                          .wrap_mode                  = s.wrap},
                        },
                }),
            });
        });
}

// ---- Pager ----

// A BodyStrong caption over a pager, as the display-mode samples label each one.
ui::Spec labeled_pager(const char* label, ui::Pager pager)
{
    return ui::VerticalStack{
        .spacing         = 8.0f,
        .child_alignment = ui::Alignment::Stretch,
        .content = {ui::Text{.text = label, .role = ui::TextRole::BodyStrong, .horizontal_alignment = ui::Alignment::Leading},
                    std::move(pager)},
    };
}

ui::Spec pager_page(Context&)
{
    return examples(
        {
            example({
                .title    = "A basic pager",
                .code     = R"cpp(
                    ui::Pager{
                        .selected_index = 2,
                        .number_of_pages     = 12, // zero-based: page 3
                        .prefix_text         = "Page",
                        .suffix_text         = "of",
                    }
                )cpp",
                .controls = {ui::Pager{.selected_index  = 2,
                                       .number_of_pages = 12,
                                       .prefix_text     = "Page",
                                       .suffix_text     = "of",
                                       .accessibility   = {.name = "Page selector"}}},
            }),
            example(
                {
                    .title = "Pager display modes",
                    .code  = R"cpp(
                        ui::Pager{
                            .selected_index = 9,
                            .number_of_pages     = 50,
                            .display_mode        = ui::PagerDisplayMode::NumberBox,
                            .prefix_text         = "Page",
                            .suffix_text         = "of",
                        }

                        ui::Pager{
                            .selected_index        = 4,
                            .number_of_pages            = 20,
                            .display_mode               = ui::PagerDisplayMode::ButtonPanel,
                            .first_button_visibility    = ui::PagerButtonVisibility::Visible,
                            .previous_button_visibility = ui::PagerButtonVisibility::Visible,
                            .next_button_visibility     = ui::PagerButtonVisibility::Visible,
                            .last_button_visibility     = ui::PagerButtonVisibility::Visible,
                        }
                    )cpp",
                    .controls =
                        {
                            ui::VerticalStack{
                                .spacing         = 20.0f,
                                .child_alignment = ui::Alignment::Stretch,
                                .content =
                                    {
                                        labeled_pager("Number box",
                                                      ui::Pager{.selected_index  = 9,
                                                                .number_of_pages = 50,
                                                                .display_mode    = ui::PagerDisplayMode::NumberBox,
                                                                .prefix_text     = "Page",
                                                                .suffix_text     = "of",
                                                                .accessibility   = {.name = "Number box page "
                                                                                            "selector"}}),
                                        labeled_pager("Button panel",
                                                      ui::Pager{.selected_index  = 4,
                                                                .number_of_pages = 20,
                                                                .display_mode    = ui::PagerDisplayMode::ButtonPanel,
                                                                .first_button_visibility = ui::PagerButtonVisibility::Visible,
                                                                .previous_button_visibility = ui::PagerButtonVisibility::Visible,
                                                                .next_button_visibility = ui::PagerButtonVisibility::Visible,
                                                                .last_button_visibility = ui::PagerButtonVisibility::Visible,
                                                                .accessibility =
                                                                    {
                                                                        .name = "Button panel page selector"}}),
                                    },
                            }},
                }),
            example({
                .title = "Unbounded pagers",
                .code  = R"cpp(
                        // -1 pages: the last page is not known, so there is no total and no last button.
                        ui::Pager{
                            .selected_index = 99,
                            .number_of_pages     = std::nullopt,
                            .display_mode        = ui::PagerDisplayMode::NumberBox,
                            .prefix_text         = "Page",
                        }

                        ui::Pager{
                            .selected_index        = 99,
                            .number_of_pages            = std::nullopt,
                            .display_mode               = ui::PagerDisplayMode::ButtonPanel,
                            .first_button_visibility    = ui::PagerButtonVisibility::Visible,
                            .previous_button_visibility = ui::PagerButtonVisibility::Visible,
                            .next_button_visibility     = ui::PagerButtonVisibility::Visible,
                        }
                    )cpp",
                .controls =
                    {
                        ui::VerticalStack{
                            .spacing         = 20.0f,
                            .child_alignment = ui::Alignment::Stretch,
                            .content =
                                {
                                    ui::Text{.text     = "Leave the page count unset when the final page is not known. "
                                                         "Unbounded pagers omit "
                                                         "the total-page count and last-page navigation.",
                                             .wrapping = ui::TextWrapping::Wrap},
                                    labeled_pager("Number box",
                                                  ui::Pager{.selected_index  = 99,
                                                            .number_of_pages = std::nullopt,
                                                            .display_mode    = ui::PagerDisplayMode::NumberBox,
                                                            .prefix_text     = "Page",
                                                            .accessibility   = {.name = "Unbounded number box page "
                                                                                        "selector"}}),
                                    labeled_pager("Button panel",
                                                  ui::Pager{.selected_index  = 99,
                                                            .number_of_pages = std::nullopt,
                                                            .display_mode    = ui::PagerDisplayMode::ButtonPanel,
                                                            .first_button_visibility = ui::PagerButtonVisibility::Visible,
                                                            .previous_button_visibility = ui::PagerButtonVisibility::Visible,
                                                            .next_button_visibility = ui::PagerButtonVisibility::Visible,
                                                            .accessibility = {.name = "Unbounded button panel page "
                                                                                      "selector"}}),
                                },
                        }},
            }),
        });
}

} // namespace

ui::Spec navigation_sample_page(int page, SamplePageParts* parts)
{
    return sample_page(page, parts);
}

std::span<const Page> navigation_pages()
{
    static const auto Pages = std::to_array<Page>({
        {"Breadcrumb", "Shows the trail of navigation taken to the current location.", breadcrumb_page},
        {"Carousel", "Presents a collection of items that the user can flip through, one item at a time.", flip_view_page},
        {"NavigationShell",
         "Common vertical layout for top-level areas of your app via a collapsible navigation menu.",
         navigation_view_page},
        {"Pager", "A control that lets users navigate through a numbered collection of pages.", pager_page},
        {"PipsPager",
         "A control to let the user navigate through a paginated collection when the page numbers do not need to be "
         "visually known.",
         pips_pager_page},
        {"Sections", "Presents information from different sources in a tabbed view.", sections_page},
        {"SelectorBar",
         "Presents information from a small set of different sources. The user can pick one of them.",
         selector_bar_page},
        {"Tabs", "A control that displays a collection of tabs that can be used to display several documents.", tabs_page},
    });
    return Pages;
}

} // namespace gallery
