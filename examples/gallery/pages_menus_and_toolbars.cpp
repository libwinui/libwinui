// SPDX-License-Identifier: MIT
#include "gallery.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <numbers>
#include <optional>
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
// ---- stand-ins for the samples' assets ----

// The sample's Slices2 image: a 64x64 pie of four slices on a transparent ground, so a
// monochrome bitmap icon shows a disc.
const ui::Bitmap& slices2_image()
{
    static const ui::Bitmap image = []
    {
        constexpr int                      Size = 64;
        constexpr std::array<ui::Color, 4> Colors{ui::Color::from_argb(0xFF0078D4),
                                                  ui::Color::from_argb(0xFF00B294),
                                                  ui::Color::from_argb(0xFFFFB900),
                                                  ui::Color::from_argb(0xFFE81123)};
        ui::Bitmap                         img(Size, Size, ui::colors::Transparent);
        const float                        c = (Size - 1) * 0.5f;
        for (int y = 0; y < Size; ++y)
        {
            for (int x = 0; x < Size; ++x)
            {
                const float dx = static_cast<float>(x) - c;
                const float dy = static_cast<float>(y) - c;
                if (dx * dx + dy * dy > c * c)
                {
                    continue;
                }
                const float angle = std::atan2(dy, dx) + std::numbers::pi_v<float>;
                const auto  slice = static_cast<std::size_t>(angle / (2.0f * std::numbers::pi_v<float>)*Colors.size()) %
                                    Colors.size();
                img.set(x, y, Colors[slice]);
            }
        }
        return img;
    }();
    return image;
}

// The sample's coffee cup image: a 32x32 cup with a handle and a saucer, in color (the
// SwipeItem's bitmap icon shows it monochrome).
const ui::Bitmap& coffee_cup_image()
{
    static const ui::Bitmap image = []
    {
        constexpr int   Size = 32;
        ui::Bitmap      img(Size, Size, ui::colors::Transparent);
        const ui::Color cup    = ui::Color::from_argb(0xFF6F4E37);
        const ui::Color saucer = ui::Color::from_argb(0xFF8B6B4F);
        for (int y = 8; y < 24; ++y)
        {
            const int inset = (y - 8) / 4; // the cup narrows toward its foot
            for (int x = 6 + inset; x < 22 - inset; ++x)
            {
                img.set(x, y, cup);
            }
        }
        for (int y = 11; y < 19; ++y) // the handle
        {
            for (int x = 22; x < 27; ++x)
            {
                const bool ring = (x == 22 || x == 26 || y == 11 || y == 18);
                if (ring)
                {
                    img.set(x, y, cup);
                }
            }
        }
        for (int x = 3; x < 29; ++x) // the saucer
        {
            img.set(x, 25, saucer);
            img.set(x, 26, saucer);
        }
        return img;
    }();
    return image;
}

ui::IconSource candara_sigma()
{
    return ui::IconGlyph{0x03A3, "Candara", 20.0f}; // a glyph from Candara
}

ui::IconSource sample_path_icon()
{
    return ui::IconSource::path(*ui::PathGeometry::parse("F1 M 20,20L 24,10L 24,24L 5,24"));
}

// The samples' text 8 to the right of the control, centered vertically.
ui::Spec side_output(ui::Property<std::string>& text)
{
    return ui::Text{.text = text, .margin = ui::Edges{.left = 8.0f}, .vertical_alignment = ui::Alignment::Center};
}

ui::Spec paragraph(const char* text, ui::Thickness margin = {})
{
    return ui::Text{.text                 = text,
                    .wrapping             = ui::TextWrapping::WrapWholeWords,
                    .margin               = margin,
                    .horizontal_alignment = ui::Alignment::Leading};
}

// The checked state as text: indeterminate prints nothing after the "=".
std::string is_checked_text(std::optional<bool> checked)
{
    return "checked = " + std::string(!checked ? "" : (*checked ? "true" : "false"));
}

ui::MenuItem glyph_item(std::string text, char32_t glyph)
{
    ui::MenuItem item{.text = std::move(text)};
    item.icon = ui::IconGlyph{glyph, "Segoe Fluent Icons", 20.0f};
    return item;
}

// ---- AppBarButton ----

struct AppBarButtonState
{
    std::array<ui::Property<std::string>, 5> output{};
};

ui::Spec app_bar_button_page(Context&)
{
    return with_state<AppBarButtonState>(
        [](AppBarButtonState& s)
        {
            auto clicked = [&s](int n)
            { return [&s, n] { s.output[n - 1] = "You clicked: Button" + std::to_string(n); }; };
            return examples({
                example({.title = "An AppBarButton with a symbol icon.",
                         .code  = R"cpp(
                         ui::Property<std::string> output;

                         ui::AppBarButton{
                             .text    = "Symbol",
                             .icon     = ui::Symbol::Like,
                             .on_click = [&] { output = "You clicked: Button1"; },
                         }
                     )cpp",
                         .controls = {ui::AppBarButton{.text = "Symbol", .icon = ui::Symbol::Like, .on_click = clicked(1)},
                                      side_output(s.output[0])}}),
                example({.title    = "An AppBarButton with a bitmap icon.",
                         .code     = R"cpp(
                             ui::Property<std::string> output;
                             ui::Bitmap                image{64, 64, ui::colors::Transparent};
                             // ... draw or load the picture ...

                             ui::AppBarButton{
                                 .text    = "Bitmap",
                                 .icon     = ui::IconSource::bitmap(image),
                                 .on_click = [&] { output = "You clicked: Button2"; },
                             }
                         )cpp",
                         .controls = {ui::AppBarButton{.text     = "Bitmap",
                                                       .icon     = ui::IconSource::bitmap(slices2_image()),
                                                       .on_click = clicked(2)},
                                      side_output(s.output[1])}}),
                example({.title = "An AppBarButton with a font icon.",
                         .code  = R"cpp(
                             ui::Property<std::string> output;

                             ui::AppBarButton{
                                 .text    = "Glyph",
                                 .icon     = ui::IconGlyph{0x03A3, "Candara", 20.0f},
                                 .on_click = [&] { output = "You clicked: Button3"; },
                             }
                         )cpp",
                         .controls = {ui::AppBarButton{.text = "Glyph", .icon = candara_sigma(), .on_click = clicked(3)},
                                      side_output(s.output[2])}}),
                example({.title = "An AppBarButton with a path icon.",
                         .code  = R"cpp(
                         ui::Property<std::string> output;

                         ui::AppBarButton{
                             .text    = "Path",
                             .icon     = ui::IconSource::path(*ui::PathGeometry::parse("F1 M 20,20L 24,10L 24,24L 5,24")),
                             .on_click = [&] { output = "You clicked: Button4"; },
                         }
                     )cpp",
                         .controls = {ui::AppBarButton{.text = "Path", .icon = sample_path_icon(), .on_click = clicked(4)},
                                      side_output(s.output[3])}}),
                example({.title    = "An AppBarButton with a Shortcut",
                         .code     = R"cpp(
                             ui::Property<std::string> output;

                             // Ctrl+S clicks the button.
                             ui::AppBarButton{
                                 .text       = "Save",
                                 .icon        = ui::Symbol::Save,
                                 .shortcut = ui::Ctrl + ui::Key::S,
                                 .on_click    = [&] { output = "You clicked: Button5"; },
                             }
                         )cpp",
                         .controls = {ui::AppBarButton{.text     = "Save",
                                                       .icon     = ui::Symbol::Save,
                                                       .shortcut = ui::Ctrl + ui::Key::S,
                                                       .on_click = clicked(5)},
                                      side_output(s.output[4])}}),
                example({.title    = "An AppBarButton that opens a Flyout containing an input control.",
                         .code     = R"cpp(
                             ui::Flyout{
                                 .anchor  = ui::AppBarButton{.text = "Edit", .icon = ui::Symbol::Edit},
                                 .content = ui::TextBox{
                                     .placeholder_text = "Input text here",
                                     .min_width        = 240,
                                 },
                             }
                         )cpp",
                         .controls = {ui::Flyout{.anchor  = ui::AppBarButton{.text = "Edit", .icon = ui::Symbol::Edit},
                                                 .content = ui::TextBox{.placeholder_text = "Input text here",
                                                                        .min_width        = 240.0f}}}}),
            });
        });
}

// ---- AppBarSeparator ----

ui::Spec app_bar_separator_page(Context&)
{
    // The sample's CommandBar sits in a Scroll that scrolls sideways (bars hidden), so a
    // narrow window scrolls it rather than moving commands into its overflow.
    return example({
        .title   = "AppBarButtons separated by AppBarSeparators.",
        .code    = R"cpp(
            ui::CommandBar{
                .primary = {
                    ui::AppBarButton{.text = "Attach Camera", .icon = ui::Symbol::AttachCamera},
                    ui::AppBarSeparator{},
                    ui::AppBarButton{.text = "Like", .icon = ui::Symbol::Like},
                    ui::AppBarButton{.text = "Dislike", .icon = ui::Symbol::Dislike},
                    ui::AppBarSeparator{},
                    ui::AppBarButton{.text = "Orientation", .icon = ui::Symbol::Orientation},
                },
            }
        )cpp",
        .stretch = true,
        .controls =
            {
                ui::Scroll{
                    .content_orientation              = ui::ScrollContentOrientation::Horizontal,
                    .vertical_scroll_mode             = ui::ScrollMode::Disabled,
                    .horizontal_scroll_bar_visibility = ui::ScrollBarVisibility::Hidden,
                    .vertical_scroll_bar_visibility   = ui::ScrollBarVisibility::Hidden,
                    .content = ui::CommandBar{.primary =
                                                  {
                                                      ui::AppBarButton{.text = "Attach Camera", .icon = ui::Symbol::AttachCamera},
                                                      ui::AppBarSeparator{},
                                                      ui::AppBarButton{.text = "Like", .icon = ui::Symbol::Like},
                                                      ui::AppBarButton{.text = "Dislike", .icon = ui::Symbol::Dislike},
                                                      ui::AppBarSeparator{},
                                                      ui::AppBarButton{.text = "Orientation", .icon = ui::Symbol::Orientation},
                                                  }},
                },
            },
    });
}

// ---- AppBarToggleButton ----

struct AppBarToggleButtonState
{
    std::array<ui::Property<std::string>, 4> output{};
};

ui::Spec app_bar_toggle_button_page(Context&)
{
    return with_state<AppBarToggleButtonState>(
        [](AppBarToggleButtonState& s)
        {
            auto report = [&s](int n)
            { return [&s, n](std::optional<bool> checked) { s.output[n] = is_checked_text(checked); }; };
            return examples({
                example({.title    = "An AppBarToggleButton with a symbol icon.",
                         .code     = R"cpp(
                             ui::Property<std::string> output;

                             ui::AppBarToggleButton{
                                 .text              = "Symbol",
                                 .icon               = ui::Symbol::Shuffle,
                                 .on_checked_changed = [&](std::optional<bool> checked)
                                 {
                                     output = *checked ? "checked = true" : "checked = false";
                                 },
                             }
                         )cpp",
                         .controls = {ui::AppBarToggleButton{.text               = "Symbol",
                                                             .icon               = ui::Symbol::Shuffle,
                                                             .on_checked_changed = report(0)},
                                      side_output(s.output[0])}}),
                example({.title    = "An AppBarToggleButton with a bitmap icon.",
                         .code     = R"cpp(
                             ui::Property<std::string> output;
                             ui::Bitmap                image{64, 64, ui::colors::Transparent};
                             // ... draw or load the picture ...

                             ui::AppBarToggleButton{
                                 .text              = "Bitmap",
                                 .icon               = ui::IconSource::bitmap(image),
                                 .on_checked_changed = [&](std::optional<bool> checked)
                                 {
                                     output = *checked ? "checked = true" : "checked = false";
                                 },
                             }
                         )cpp",
                         .controls = {ui::AppBarToggleButton{.text = "Bitmap",
                                                             .icon = ui::IconSource::bitmap(slices2_image()),
                                                             .on_checked_changed = report(1)},
                                      side_output(s.output[1])}}),
                example({.title    = "An AppBarToggleButton with a font icon.",
                         .code     = R"cpp(
                             ui::Property<std::string> output;

                             ui::AppBarToggleButton{
                                 .text              = "Glyph",
                                 .icon               = ui::IconGlyph{0x03A3, "Candara", 20.0f},
                                 .on_checked_changed = [&](std::optional<bool> checked)
                                 {
                                     output = *checked ? "checked = true" : "checked = false";
                                 },
                             }
                         )cpp",
                         .controls = {ui::AppBarToggleButton{.text               = "Glyph",
                                                             .icon               = candara_sigma(),
                                                             .on_checked_changed = report(2)},
                                      side_output(s.output[2])}}),
                example({.title    = "A three-state AppBarToggleButton with a path icon.",
                         .code     = R"cpp(
                             ui::Property<std::string> output;

                             // The third state is indeterminate: checked is empty.
                             ui::AppBarToggleButton{
                                 .text              = "Path",
                                 .three_state        = true,
                                 .icon               = ui::IconSource::path(*ui::PathGeometry::parse("F1 M 20,20L 24,10L 24,24L 5,24")),
                                 .on_checked_changed = [&](std::optional<bool> checked)
                                 {
                                     output = !checked ? "checked = " : *checked ? "checked = true" : "checked = false";
                                 },
                             }
                         )cpp",
                         .controls = {ui::AppBarToggleButton{.text               = "Path",
                                                             .three_state        = true,
                                                             .icon               = sample_path_icon(),
                                                             .on_checked_changed = report(3)},
                                      side_output(s.output[3])}}),
            });
        });
}

// ---- CommandBar ----

struct CommandBarState
{
    ui::Property<std::string>   selected{};
    ui::Control<ui::CommandBar> bar;
};

std::unique_ptr<ui::AppBarButtonControl> bar_button(std::string label, ui::Symbol icon, ui::KeyCombo shortcut)
{
    auto button = std::make_unique<ui::AppBarButtonControl>(std::move(label), icon);
    button->set_shortcut(shortcut);
    return button;
}

ui::Spec command_bar_page(Context&)
{
    return with_state<CommandBarState>(
        [](CommandBarState& s)
        {
            auto clicked = [&s](const char* label)
            { return [&s, label] { s.selected = std::string("You clicked: ") + label; }; };
            auto option = [](const char* text, ui::Action on_click)
            {
                return ui::Button{.text                 = text,
                                  .margin               = ui::Edges{.top = 12.0f},
                                  .horizontal_alignment = ui::Alignment::Leading,
                                  .on_click             = std::move(on_click)};
            };
            return example({
                .title   = "A command bar with text on the side free floating in a page",
                .code    = R"cpp(
                    ui::Property<std::string>   selected;
                    ui::Control<ui::CommandBar> bar;

                    ui::CommandBar{
                        .default_text_position = ui::CommandBarDefaultTextPosition::Right,
                        .control                = bar,
                        .primary = {
                            ui::AppBarButton{
                                .text       = "Add",
                                .icon        = ui::Symbol::Add,
                                .shortcut = ui::Ctrl + ui::Key::A,
                                .on_click    = [&] { selected = "You clicked: Add"; },
                            },
                            // ... Edit (Ctrl+E) and Share (F4) the same way ...
                        },
                        .secondary = {
                            ui::AppBarButton{
                                .text       = "Settings",
                                .icon        = ui::Symbol::Setting,
                                .shortcut = ui::Ctrl + ui::Key::I,
                                .on_click    = [&] { selected = "You clicked: Settings"; },
                            },
                        },
                    }

                    // Open the bar and keep it open:
                    bar->set_open(true);
                    bar->set_sticky(true);

                    // Add a secondary command later, and take the last one out again:
                    auto button = std::make_unique<ui::AppBarButtonControl>("Button 1", ui::Symbol::Add);
                    button->set_shortcut(ui::Ctrl + ui::Key::N);
                    bar->add_secondary(std::move(button));
                    bar->add_secondary(std::make_unique<ui::AppBarSeparatorControl>());
                    bar->remove_secondary(bar->secondary_count() - 1);
                )cpp",
                .stacked = true,
                .options =
                    ui::VerticalStack{
                        .content =
                            {
                                ui::Text{.text = "Show or hide", .horizontal_alignment = ui::Alignment::Leading},
                                option("Open command bar",
                                       [&s]
                                       {
                                           s.bar->set_open(true);
                                           s.bar->set_sticky(true);
                                       }),
                                option("Close command bar",
                                       [&s]
                                       {
                                           s.bar->set_open(false);
                                           s.bar->set_sticky(false);
                                       }),
                                ui::Text{.text                 = "Modify content",
                                         .margin               = ui::Edges{.top = 16.0f},
                                         .horizontal_alignment = ui::Alignment::Leading},
                                option("Add secondary commands",
                                       [&s]
                                       {
                                           // Commands for this page only, removed again on leaving it.
                                           if (s.bar->secondary_count() == 1)
                                           {
                                               s.bar->add_secondary(
                                                   bar_button("Button 1", ui::Symbol::Add, ui::Ctrl + ui::Key::N));
                                               s.bar->add_secondary(
                                                   bar_button("Button 2", ui::Symbol::Delete, ui::Key::Delete));
                                               s.bar->add_secondary(std::make_unique<ui::AppBarSeparatorControl>());
                                               s.bar->add_secondary(bar_button("Button 3",
                                                                               ui::Symbol::FontDecrease,
                                                                               ui::Ctrl + ui::Key::Subtract));
                                               s.bar->add_secondary(
                                                   bar_button("Button 4", ui::Symbol::FontIncrease, ui::Ctrl + ui::Key::Add));
                                           }
                                       }),
                                option("Remove secondary commands",
                                       [&s]
                                       {
                                           while (s.bar->secondary_count() > 1)
                                           {
                                               s.bar->remove_secondary(s.bar->secondary_count() - 1);
                                           }
                                       }),
                            },
                    },
                .controls =
                    {
                        ui::CommandBar{
                            .default_text_position = ui::CommandBarDefaultTextPosition::Right,
                            .control               = s.bar,
                            .primary =
                                {
                                    ui::AppBarButton{.text     = "Add",
                                                     .icon     = ui::Symbol::Add,
                                                     .shortcut = ui::Ctrl + ui::Key::A,
                                                     .on_click = clicked("Add")},
                                    ui::AppBarButton{.text     = "Edit",
                                                     .icon     = ui::Symbol::Edit,
                                                     .shortcut = ui::Ctrl + ui::Key::E,
                                                     .on_click = clicked("Edit")},
                                    ui::AppBarButton{.text     = "Share",
                                                     .icon     = ui::Symbol::Share,
                                                     .shortcut = ui::KeyCombo{ui::Key::F4},
                                                     .on_click = clicked("Share")},
                                },
                            .secondary = {ui::AppBarButton{.text     = "Settings",
                                                           .icon     = ui::Symbol::Setting,
                                                           .shortcut = ui::Ctrl + ui::Key::I,
                                                           .on_click = clicked("Settings")}},
                        },
                        ui::Text{.text                 = s.selected,
                                 .margin               = ui::Edges{.top = 8.0f},
                                 .horizontal_alignment = ui::Alignment::Leading},
                    },
            });
        });
}

// ---- CommandBarFlyout ----

struct CommandBarFlyoutState
{
    ui::Property<std::string>         selected{};
    ui::Control<ui::CommandBarFlyout> flyout;
};

ui::Spec command_bar_flyout_page(Context&)
{
    return with_state<CommandBarFlyoutState>(
        [](CommandBarFlyoutState& s)
        {
            auto clicked = [&s](const char* label)
            { return [&s, label] { s.selected = std::string("You clicked: ") + label; }; };
            auto primary = [&clicked](const char* label, ui::Symbol icon)
            { return ui::AppBarButton{.text = label, .icon = icon, .tool_tip = label, .on_click = clicked(label)}; };
            return example({
                .title   = "CommandBarFlyout for commands on an in-app object",
                .code    = R"cpp(
                    ui::Property<std::string>         selected;
                    ui::Control<ui::CommandBarFlyout> flyout;
                    ui::Bitmap                        photo = /* ... */;

                    // A click shows the flyout collapsed; a right click shows it with its menu open.
                    ui::CommandBarFlyout{
                        .placement = ui::PopupPlacement::Right,
                        .placement_alignment = ui::PopupPlacementAlignment::Leading,
                        .show_mode = ui::FlyoutShowMode::Transient,
                        .on_closed = [&](ui::PopupCloseReason) { flyout->set_show_mode(ui::FlyoutShowMode::Transient); },
                        .control   = flyout,
                        .anchor    = ui::Button{
                            .padding              = ui::Thickness{},
                            .accessibility        = {.name = "mountain"},
                            .on_context_requested = [&](std::optional<ui::Point>)
                            {
                                flyout->hide();
                                flyout->set_show_mode(ui::FlyoutShowMode::Standard);
                                flyout->show();
                            },
                            .content = ui::Image{.source = photo, .height = 300},
                        },
                        .primary = {
                            ui::AppBarButton{
                                .text    = "Share",
                                .icon     = ui::Symbol::Share,
                                .tool_tip = "Share",
                                .on_click = [&] { selected = "You clicked: Share"; },
                            },
                            // ... Save and Delete the same way ...
                        },
                        .secondary = {
                            ui::AppBarButton{.text = "Resize", .on_click = [&] { selected = "You clicked: Resize"; }},
                            ui::AppBarButton{.text = "Move", .on_click = [&] { selected = "You clicked: Move"; }},
                        },
                    }
                )cpp",
                .stacked = true,
                .controls =
                    {
                        ui::Text{.text                 = "Click or right click the image to open a CommandBarFlyout",
                                 .horizontal_alignment = ui::Alignment::Leading},
                        // A click shows it Transient (menu collapsed, focus left
                        // alone), a right click Standard (menu open, focus moved in).
                        ui::CommandBarFlyout{
                            .placement            = ui::PopupPlacement::Right,
                            .placement_alignment  = ui::PopupPlacementAlignment::Leading,
                            .show_mode            = ui::FlyoutShowMode::Transient,
                            .margin               = ui::Symmetric{.horizontal = 0.0f, .vertical = 12.0f},
                            .horizontal_alignment = ui::Alignment::Leading,
                            .on_closed            = [&s](ui::PopupCloseReason)
                            { s.flyout->set_show_mode(ui::FlyoutShowMode::Transient); },
                            .control   = s.flyout,
                            .anchor    = ui::Button{.padding       = ui::Thickness{},
                                                    .accessibility = {.name = "mountain"},
                                                    .on_context_requested =
                                                        [&s](std::optional<ui::Point>)
                                                    {
                                                     s.flyout->hide();
                                                     s.flyout->set_show_mode(ui::FlyoutShowMode::Standard);
                                                     s.flyout->show();
                                                    },
                                                    .content = ui::Image{.source = sample_photo(1), .height = 300.0f}},
                            .primary   = {primary("Share", ui::Symbol::Share),
                                          primary("Save", ui::Symbol::Save),
                                          primary("Delete", ui::Symbol::Delete)},
                            .secondary = {ui::AppBarButton{.text = "Resize", .on_click = clicked("Resize")},
                                          ui::AppBarButton{.text = "Move", .on_click = clicked("Move")}},
                        },
                        ui::Text{.text = s.selected, .horizontal_alignment = ui::Alignment::Leading},
                    },
            });
        });
}

// ---- Menu ----

struct MenuBarState
{
    std::array<ui::Property<std::string>, 3> selected{};
};

ui::Spec menu_page(Context&)
{
    return with_state<MenuBarState>(
        [](MenuBarState& s)
        {
            // A click reports "You clicked: " and the item's text, per example.
            auto item = [&s](int example, const char* text, ui::KeyCombo shortcut = {})
            {
                ui::MenuItem i{.text     = text,
                               .on_click = [&s, example, text]
                               { s.selected[example] = std::string("You clicked: ") + text; }};
                i.shortcut = shortcut;
                return i;
            };
            auto radio = [&s](const char* text, bool checked)
            {
                ui::MenuItem i = ui::MenuItem::radio(text, checked);
                i.on_click     = [&s, text] { s.selected[2] = std::string("You clicked: ") + text; };
                return i;
            };
            auto card = [&s](int example, const char* title, std::string code, std::vector<ui::MenuBarItem> menus)
            {
                return gallery::example({
                    .title = title,
                    .code  = std::move(code),
                    .output = ui::Text{.text = ui::bind(s.selected[example]), .horizontal_alignment = ui::Alignment::Leading},
                    .controls = {ui::MenuBar{.items = std::move(menus), .horizontal_alignment = ui::Alignment::Leading}},
                });
            };
            return examples({
                card(0,
                     "A simple MenuBar",
                     R"cpp(
                         ui::Property<std::string> selected;

                         auto item = [&selected](const char* text)
                         {
                             return ui::MenuItem{.text = text, .on_click = [&selected, text] { selected = std::string("You clicked: ") + text; }};
                         };

                         ui::MenuBar{
                             .items = {
                                 ui::MenuBarItem{.text = "File", .items = {item("New"), item("Open"), item("Save"), item("Exit")}},
                                 ui::MenuBarItem{.text = "Edit", .items = {item("Undo"), item("Cut"), item("Copy"), item("Paste")}},
                                 ui::MenuBarItem{.text = "Help", .items = {item("About")}},
                             },
                         }
                     )cpp",
                     {
                         ui::MenuBarItem{.text  = "File",
                                         .items = {item(0, "New"), item(0, "Open"), item(0, "Save"), item(0, "Exit")}},
                         ui::MenuBarItem{.text  = "Edit",
                                         .items = {item(0, "Undo"), item(0, "Cut"), item(0, "Copy"), item(0, "Paste")}},
                         ui::MenuBarItem{.text = "Help", .items = {item(0, "About")}},
                     }),
                card(1,
                     "MenuBar with shortcuts",
                     R"cpp(
                         ui::Property<std::string> selected;

                         // The chord shows beside the item and invokes it.
                         auto item = [&selected](const char* text, ui::KeyCombo shortcut)
                         {
                             ui::MenuItem i{.text = text, .kind = [&selected, text] { selected = std::string("You clicked: ") + text; }};
                             i.shortcut = shortcut;
                             return i;
                         };

                         ui::MenuBar{
                             .items = {
                                 ui::MenuBarItem{.text = "File", .items = {item("New", ui::Ctrl + ui::Key::N),
                                               item("Open", ui::Ctrl + ui::Key::O),
                                               item("Save", ui::Ctrl + ui::Key::S),
                                               item("Exit", ui::Ctrl + ui::Key::E)}},
                                 ui::MenuBarItem{.text = "Edit", .items = {item("Undo", ui::Ctrl + ui::Key::Z),
                                               item("Cut", ui::Ctrl + ui::Key::X),
                                               item("Copy", ui::Ctrl + ui::Key::C),
                                               item("Paste", ui::Ctrl + ui::Key::V)}},
                                 ui::MenuBarItem{.text = "Help", .items = {item("About", ui::Ctrl + ui::Key::I)}},
                             },
                         }
                     )cpp",
                     {
                         ui::MenuBarItem{.text  = "File",
                                         .items = {item(1, "New", ui::Ctrl + ui::Key::N),
                                                   item(1, "Open", ui::Ctrl + ui::Key::O),
                                                   item(1, "Save", ui::Ctrl + ui::Key::S),
                                                   item(1, "Exit", ui::Ctrl + ui::Key::E)}},
                         ui::MenuBarItem{.text  = "Edit",
                                         .items = {item(1, "Undo", ui::Ctrl + ui::Key::Z),
                                                   item(1, "Cut", ui::Ctrl + ui::Key::X),
                                                   item(1, "Copy", ui::Ctrl + ui::Key::C),
                                                   item(1, "Paste", ui::Ctrl + ui::Key::V)}},
                         ui::MenuBarItem{.text = "Help", .items = {item(1, "About", ui::Ctrl + ui::Key::I)}},
                     }),
                card(2,
                     "MenuBar with submenus, separators, and radio items",
                     R"cpp(
                         ui::MenuBar{
                             .items = {
                                 ui::MenuBarItem{.text = "File", .items = {ui::MenuItem::sub_item("New",
                                                                            {ui::MenuItem{.text = "Plain Text Document"},
                                                                             ui::MenuItem{.text = "Rich Text Document"},
                                                                             ui::MenuItem{.text = "Other Formats"}}),
                                               ui::MenuItem{.text = "Open"},
                                               ui::MenuItem{.text = "Save"},
                                               ui::MenuItem::separator(),
                                               ui::MenuItem{.text = "Exit"}}},
                                 // Radio items next to each other exclude each other; the separator starts a new group.
                                 ui::MenuBarItem{.text = "View", .items = {ui::MenuItem{.text = "Output"},
                                               ui::MenuItem::separator(),
                                               ui::MenuItem::radio("Landscape", false),
                                               ui::MenuItem::radio("Portrait", true),
                                               ui::MenuItem::separator(),
                                               ui::MenuItem::radio("Small icons", false),
                                               ui::MenuItem::radio("Medium icons", true),
                                               ui::MenuItem::radio("Large icons", false)}},
                                 // ... Edit and Help ...
                             },
                         }
                     )cpp",
                     {
                         ui::MenuBarItem{.text  = "File",
                                         .items = {ui::MenuItem::sub_item("New",
                                                                          {item(2, "Plain Text Document"),
                                                                           item(2, "Rich Text Document"),
                                                                           item(2, "Other Formats")}),
                                                   item(2, "Open"),
                                                   item(2, "Save"),
                                                   ui::MenuItem::separator(),
                                                   item(2, "Exit")}},
                         ui::MenuBarItem{.text  = "Edit",
                                         .items = {item(2, "Undo"), item(2, "Cut"), item(2, "Copy"), item(2, "Paste")}},
                         ui::MenuBarItem{.text  = "View",
                                         .items = {item(2, "Output"),
                                                   ui::MenuItem::separator(),
                                                   radio("Landscape", false),
                                                   radio("Portrait", true),
                                                   ui::MenuItem::separator(),
                                                   radio("Small icons", false),
                                                   radio("Medium icons", true),
                                                   radio("Large icons", false)}},
                         ui::MenuBarItem{.text = "Help", .items = {item(2, "About")}},
                     }),
            });
        });
}

// ---- MenuFlyout ----

struct MenuFlyoutState
{
    ui::Property<std::string> sort_output{};
    ui::Property<std::string> split_output{};
    ui::Property<std::string> context_output{};
    ui::Property<bool>        shuffle{true};
};

// The context menu card's target: a framed area with no control in it.
ui::Spec context_menu_card(MenuFlyoutState& s, ui::MenuItem copy, ui::MenuItem del)
{
    copy.on_click = [&s] { s.context_output = "Copied"; };
    del.on_click  = [&s] { s.context_output = "Deleted"; };
    ui::Spec area = ui::Border{
        .border_brush     = [](const ui::Theme& t) { return t.palette.text_fill_secondary; },
        .border_thickness = ui::Thickness{1.0f},
        .corner_radius    = 4.0f,
        .padding          = ui::Thickness{24.0f},
        .width            = 300.0f,
        .content          = ui::Text{.text = "Right-click anywhere in this box.", .wrapping = ui::TextWrapping::Wrap},
    };
    return ui::ContextMenu{
        .items   = {std::move(copy), ui::MenuItem::separator(), std::move(del)},
        .content = std::move(area),
    };
}

ui::Spec menu_flyout_page(Context&)
{
    return with_state<MenuFlyoutState>(
        [](MenuFlyoutState& s)
        {
            auto sort = [&s](const char* text, const char* tag)
            {
                return ui::MenuItem{.text     = text,
                                    .on_click = [&s, tag] { s.sort_output = std::string("Sort by: ") + tag; }};
            };
            auto split = [&s](const char* text)
            {
                return ui::MenuItem{.text     = text,
                                    .on_click = [&s, text] { s.split_output = std::string("Clicked: ") + text; }};
            };

            ui::MenuItem save_split = ui::MenuItem::split("Save",
                                                          [&s] { s.split_output = "Clicked: Save"; },
                                                          {split("Save as .docx"), split("Save as .pdf"), split("Save as .txt")});
            save_split.icon          = ui::IconGlyph{0xE74E, "Segoe Fluent Icons", 20.0f};
            ui::MenuItem share_split = ui::MenuItem::split("Share",
                                                           [&s] { s.split_output = "Clicked: Share"; },
                                                           {split("Share via email"), split("Share via link")});
            share_split.icon         = ui::Symbol::Share;

            auto with_shortcut = [](ui::MenuItem item, ui::KeyCombo chord)
            {
                item.shortcut = chord;
                return item;
            };
            ui::MenuItem copy{.text = "Copy"};
            copy.icon = ui::Symbol::Copy;
            ui::MenuItem del{.text = "Delete"};
            del.icon = ui::Symbol::Delete;

            return examples({
                example({.title    = "An AppBarButton with a  MenuFlyout.",
                         .code     = R"cpp(
                             ui::Property<std::string> output;

                             ui::MenuFlyout{
                                 .items = {
                                     ui::MenuItem{.text = "By rating", .on_click = [&] { output = "Sort by: rating"; }},
                                     ui::MenuItem{.text = "By match", .on_click = [&] { output = "Sort by: match"; }},
                                     ui::MenuItem{.text = "By distance", .on_click = [&] { output = "Sort by: distance"; }},
                                 },
                                 .anchor = ui::AppBarButton{
                                     .icon          = ui::Symbol::Sort,
                                     .compact       = true,
                                     .tool_tip      = "Sort",
                                     .accessibility = {.name = "Sort"},
                                 },
                             }
                         )cpp",
                         .controls = {ui::MenuFlyout{.items  = {sort("By rating", "rating"),
                                                                sort("By match", "match"),
                                                                sort("By distance", "distance")},
                                                     .anchor = ui::AppBarButton{.icon          = ui::Symbol::Sort,
                                                                                .compact       = true,
                                                                                .tool_tip      = "Sort",
                                                                                .accessibility = {.name = "Sort"}}},
                                      side_output(s.sort_output)}}),
                example({.title    = "A MenuFlyout with toggle items and a separator.",
                         .code     = R"cpp(
                             ui::MenuFlyout{
                                 .items = {
                                     ui::MenuItem{.text = "Reset"},
                                     ui::MenuItem::separator(),
                                     ui::MenuItem::toggle("Repeat", true),
                                     ui::MenuItem::toggle("Shuffle", true),
                                 },
                                 .anchor = ui::Button{.text = "Options"},
                             }
                         )cpp",
                         .controls = {ui::MenuFlyout{.items  = {ui::MenuItem{.text = "Reset"},
                                                                ui::MenuItem::separator(),
                                                                ui::MenuItem::toggle("Repeat", true),
                                                                ui::MenuItem::toggle("Shuffle", true)},
                                                     .anchor = ui::Button{.text = "Options"}}}}),
                example({.title    = "A toggle item and an AppBarToggleButton bound to one property.",
                         .code     = R"cpp(
                             ui::Property<bool> shuffle{true};

                             // Checking either one checks the other.
                             ui::MenuFlyout{
                                 .items   = {ui::MenuItem::toggle("Shuffle", shuffle)},
                                 .anchor  = ui::Button{.text = "Options"},
                             },
                             ui::AppBarToggleButton{
                                 .text              = "Shuffle",
                                 .checked            = shuffle,
                                 .icon               = ui::Symbol::Shuffle,
                                 .on_checked_changed = [&](std::optional<bool> on) { shuffle = on.value_or(false); },
                             }
                         )cpp",
                         .controls = {ui::MenuFlyout{.items  = {ui::MenuItem::toggle("Shuffle", s.shuffle)},
                                                     .anchor = ui::Button{.text = "Options"}},
                                      ui::AppBarToggleButton{.text               = "Shuffle",
                                                             .checked            = ui::bind_one_way(s.shuffle),
                                                             .icon               = ui::Symbol::Shuffle,
                                                             .on_checked_changed = [&s](std::optional<bool> on)
                                                             { s.shuffle = on.value_or(false); }}}}),
                example({.title    = "A MenuFlyout with cascading menus.",
                         .code     = R"cpp(
                         ui::MenuFlyout{
                             .items = {
                                 ui::MenuItem{.text = "Open"},
                                 ui::MenuItem::sub_item(
                                     "Send to",
                                     {ui::MenuItem{.text = "Bluetooth"},
                                      ui::MenuItem{.text = "Desktop (shortcut)"},
                                      ui::MenuItem::sub_item("Compressed file",
                                                                   {ui::MenuItem{.text = "Compress and email"},
                                                                    ui::MenuItem{.text = "Compress to .7z"},
                                                                    ui::MenuItem{.text = "Compress to .zip"}})}),
                             },
                             .anchor = ui::Button{.text = "File Options"},
                         }
                     )cpp",
                         .controls = {ui::MenuFlyout{
                             .items  = {ui::MenuItem{.text = "Open"},
                                        ui::MenuItem::sub_item("Send to",
                                                               {ui::MenuItem{.text = "Bluetooth"},
                                                                ui::MenuItem{.text = "Desktop (shortcut)"},
                                                                ui::MenuItem::sub_item("Compressed file",
                                                                                       {ui::MenuItem{.text = "Comp"
                                                                                                             "ress"
                                                                                                             " and"
                                                                                                             " ema"
                                                                                                             "il"},
                                                                                        ui::MenuItem{.text = "Comp"
                                                                                                             "ress"
                                                                                                             " to "
                                                                                                             ".7"
                                                                                                             "z"},
                                                                                        ui::MenuItem{.text = "Comp"
                                                                                                             "ress"
                                                                                                             " to "
                                                                                                             ".zi"
                                                                                                             "p"}})})},
                             .anchor = ui::Button{.text = "File Options"}}}}),
                example({.title    = "A MenuFlyout with split items.",
                         .code     = R"cpp(
                             ui::Property<std::string> output;

                             auto item = [&output](const char* text)
                             {
                                 return ui::MenuItem{.text = text, .on_click = [&output, text] { output = std::string("Clicked: ") + text; }};
                             };

                             // A split item runs its own action and opens a submenu from its arrow.
                             ui::MenuItem save = ui::MenuItem::split(
                                 "Save",
                                 [&] { output = "Clicked: Save"; },
                                 {item("Save as .docx"), item("Save as .pdf"), item("Save as .txt")});
                             save.icon = ui::IconGlyph{0xE74E, "Segoe Fluent Icons", 20.0f};

                             ui::MenuItem share = ui::MenuItem::split(
                                 "Share",
                                 [&] { output = "Clicked: Share"; },
                                 {item("Share via email"), item("Share via link")});
                             share.icon = ui::Symbol::Share;

                             ui::MenuFlyout{
                                 .items   = {save, share},
                                 .anchor  = ui::Button{.text = "File Options"},
                             }
                         )cpp",
                         .controls = {ui::MenuFlyout{.items  = {save_split, share_split},
                                                     .anchor = ui::Button{.text = "File Options"}},
                                      side_output(s.split_output)}}),
                example({.title    = "A MenuFlyout with icons.",
                         .code     = R"cpp(
                             ui::MenuItem share{.text = "Share"};
                             share.icon = ui::IconGlyph{0xE72D, "Segoe Fluent Icons", 20.0f};
                             ui::MenuItem copy{.text = "Copy"};
                             copy.icon = ui::Symbol::Copy;
                             ui::MenuItem del{.text = "Delete"};
                             del.icon = ui::Symbol::Delete;

                             ui::MenuFlyout{
                                 .items = {
                                     share,
                                     copy,
                                     del,
                                     ui::MenuItem::separator(),
                                     ui::MenuItem{.text = "Rename"},
                                     ui::MenuItem{.text = "Select"},
                                 },
                                 .anchor = ui::Button{.text = "Edit Options"},
                             }
                         )cpp",
                         .controls = {ui::MenuFlyout{.items  = {glyph_item("Share", 0xE72D),
                                                                copy,
                                                                del,
                                                                ui::MenuItem::separator(),
                                                                ui::MenuItem{.text = "Rename"},
                                                                ui::MenuItem{.text = "Select"}},
                                                     .anchor = ui::Button{.text = "Edit Options"}}}}),
                example({.title = "A MenuFlyout with icons and Keyboard Shortcuts.",
                         .code  = R"cpp(
                             ui::MenuItem share{.text = "Share"};
                             share.icon        = ui::IconGlyph{0xE72D, "Segoe Fluent Icons", 20.0f};
                             share.shortcut = ui::Ctrl + ui::Key::S;
                             ui::MenuItem copy{.text = "Copy"};
                             copy.icon        = ui::Symbol::Copy;
                             copy.shortcut = ui::Ctrl + ui::Key::C;
                             ui::MenuItem del{.text = "Delete"};
                             del.icon        = ui::Symbol::Delete;
                             del.shortcut = {ui::Key::Delete};

                             ui::MenuFlyout{
                                 .items = {
                                     share,
                                     copy,
                                     del,
                                     ui::MenuItem::separator(),
                                     ui::MenuItem{.text = "Rename"},
                                     ui::MenuItem{.text = "Select"},
                                 },
                                 .anchor = ui::Button{.text = "Edit Options"},
                             }
                         )cpp",
                         .controls = {ui::MenuFlyout{.items = {with_shortcut(glyph_item("Share", 0xE72D), ui::Ctrl + ui::Key::S),
                                                               with_shortcut(copy, ui::Ctrl + ui::Key::C),
                                                               with_shortcut(del, ui::Key::Delete),
                                                               ui::MenuItem::separator(),
                                                               ui::MenuItem{.text = "Rename"},
                                                               ui::MenuItem{.text = "Select"}},
                                                     .anchor = ui::Button{.text = "Edit Options"}}}}),
                example({.title    = "A context menu on any element.",
                         .code     = R"cpp(
                             ui::Property<std::string> output;

                             // A right-click anywhere on the content, or Shift+F10 or the Menu key while
                             // focus is inside it, opens the items at the pointer.
                             ui::ContextMenu{
                                 .items = {
                                     ui::MenuItem{.text = "Copy", .icon = ui::Symbol::Copy, .on_click = [&] { output = "Copied"; }},
                                     ui::MenuItem::separator(),
                                     ui::MenuItem{.text = "Delete", .icon = ui::Symbol::Delete, .on_click = [&] { output = "Deleted"; }},
                                 },
                                 .content = ui::Border{
                                     .border_thickness = 1,
                                     .padding          = 24,
                                     .content          = ui::Text{.text = "Right-click anywhere in this box."},
                                 },
                             }
                         )cpp",
                         .controls = {context_menu_card(s, copy, del), side_output(s.context_output)}}),
                example({.title    = "A MenuFlyout with radio items",
                         .code     = R"cpp(
                         // Radio items next to each other exclude each other; the separator starts a new group.
                         ui::MenuFlyout{
                             .items = {
                                 ui::MenuItem::radio("Landscape"),
                                 ui::MenuItem::radio("Portrait", true),
                                 ui::MenuItem::separator(),
                                 ui::MenuItem::radio("Small icons"),
                                 ui::MenuItem::radio("Medium icons", true),
                                 ui::MenuItem::radio("Large icons"),
                             },
                             .anchor = ui::Button{.text = "Options"},
                         }
                     )cpp",
                         .controls = {ui::MenuFlyout{.items  = {ui::MenuItem::radio("Landscape"),
                                                                ui::MenuItem::radio("Portrait", true),
                                                                ui::MenuItem::separator(),
                                                                ui::MenuItem::radio("Small icons"),
                                                                ui::MenuItem::radio("Medium icons", true),
                                                                ui::MenuItem::radio("Large icons")},
                                                     .anchor = ui::Button{.text = "Options"}}}}),
            });
        });
}

// ---- Swipe ----

// The samples' border in the standard button's fill.
ui::Color button_background(const ui::Theme& theme)
{
    return theme.colors().control_fill;
}

ui::IconSource glyph(char32_t codepoint)
{
    return ui::IconGlyph{codepoint, "Segoe Fluent Icons", 20.0f};
}

struct SwipeState
{
    bool                              accepted = false;
    bool                              flagged  = false;
    bool                              archived = false;
    ui::Property<std::string>         reveal_text{"Swipe Right"};
    ui::Property<std::string>         execute_text{"Swipe Left"};
    ui::StackControl*                 rows       = nullptr;
    std::shared_ptr<ui::ElementWatch> rows_watch = std::make_shared<ui::ElementWatch>();

    // The reveal text for the accepted and flagged states.
    void update_reveal_text()
    {
        if (accepted && !flagged)
        {
            reveal_text = "Swipe Right - Accepted";
        }
        else if (accepted && flagged)
        {
            reveal_text = "Swipe Right - Accepted & Flagged";
        }
        else if (!accepted && flagged)
        {
            reveal_text = "Swipe Right - Flagged";
        }
        else
        {
            reveal_text = "Swipe Right";
        }
    }
};

ui::Spec swipe_row(ui::Spec                   content,
                   std::vector<ui::SwipeItem> left,
                   std::vector<ui::SwipeItem> right,
                   ui::SwipeMode              right_mode = ui::SwipeMode::Reveal,
                   ui::Thickness              border     = ui::Thickness{1.0f})
{
    return ui::Swipe{
        .left_items       = std::move(left),
        .right_items      = std::move(right),
        .right_mode       = right_mode,
        .border_brush     = button_background,
        .border_thickness = border,
        .content          = std::move(content),
    };
}

std::unique_ptr<ui::Element> swipe_list_row(SwipeState& s, Context& ctx, const std::string& text)
{
    // One row of the sample's ListView: its item template, a SwipeControl 68 tall.
    ui::SwipeItem reply{.text       = "Reply All",
                        .icon       = glyph(0xE8C2),
                        .background = ui::Color::from_argb(0xFF3E6FA7),
                        .foreground = ui::colors::White};
    ui::SwipeItem open{.text       = "Open",
                       .icon       = glyph(0xE8C3),
                       .background = ui::Color::from_argb(0xFFFF9501),
                       .foreground = ui::colors::White};
    ui::SwipeItem remove{.text       = "Delete",
                         .icon       = glyph(0xE74D),
                         .background = ui::theme_colors::Critical,
                         .on_invoked = [&s, &ctx, text]
                         {
                             // DeleteItem_ItemInvoked: the row leaves the list. Posted, since the row
                             // invoking this is itself mid-update.
                             std::shared_ptr<ui::ElementWatch> watch = s.rows_watch;
                             ctx.window.post(
                                 [&s, watch, text]
                                 {
                                     if (watch->get() == nullptr)
                                     {
                                         return; // the page is gone
                                     }
                                     for (const auto& row : s.rows->children())
                                     {
                                         if (row->accessibility().name == text)
                                         {
                                             s.rows->remove(row.get());
                                             return;
                                         }
                                     }
                                 });
                         }};
    ui::Spec      row = ui::Swipe{
        .left_items       = {reply, open},
        .right_items      = {remove},
        .right_mode       = ui::SwipeMode::Execute,
        .border_brush     = button_background,
        .border_thickness = ui::Edges{.top = 1.0f},
        .height           = 68.0f,
        .min_width        = 200.0f,
        .accessibility    = {.name = text},
        .content          = ui::Text{.text               = text,
                                     .text_style         = ui::TextStyle{.font_size = 24.0f},
                                     .margin             = ui::Thickness{12.0f},
                                     .vertical_alignment = ui::Alignment::Center},
    };
    return std::move(row).build();
}

ui::Spec swipe_page(Context& ctx)
{
    return with_state<SwipeState>(
        [&ctx](SwipeState& s)
        {
            // Accept_ItemInvoked / Flag_ItemInvoked: each item flips its own text and icon.
            ui::SwipeItem                 accept{.text       = "Accept",
                                                 .icon       = glyph(0xE8FB),
                                                 .on_invoked = [&s](ui::SwipeItemInvokedEventArgs& args)
                                                 {
                                     s.accepted     = !s.accepted;
                                     args.item.icon = glyph(s.accepted ? 0xE711 : 0xE10B);
                                     args.item.text = s.accepted ? "Cancel" : "Accept";
                                     s.update_reveal_text();
                                                 }};
            ui::SwipeItem                 flag{.text       = "Flag",
                                               .icon       = glyph(0xE7C1),
                                               .on_invoked = [&s](ui::SwipeItemInvokedEventArgs& args)
                                               {
                                   s.flagged      = !s.flagged;
                                   args.item.icon = glyph(s.flagged ? 0xEB4B : 0xE129);
                                   args.item.text = s.flagged ? "Unmark" : "Flag";
                                   s.update_reveal_text();
                                               }};
            ui::SwipeItem                 archive{.text                = "Archive",
                                                  .icon                = glyph(0xE7B8),
                                                  .behavior_on_invoked = ui::SwipeBehaviorOnInvoked::Close,
                                                  .on_invoked          = [&s]
                                                  {
                                      s.archived     = !s.archived;
                                      s.execute_text = s.archived ? "Archived - Swipe Left" : "Swipe Left";
                                                  }};
            const ui::LinearGradientBrush purple{{{0.0f, ui::Color::from_argb(0xFF8990F9)},
                                                  {0.5f, ui::Color::from_argb(0xFF5B66FB)},
                                                  {1.0f, ui::Color::from_argb(0xFF5C1DF4)}},
                                                 ui::GradientAxis::Horizontal};
            ui::SwipeItem                 lock{.text                = "Lock",
                                               .icon                = glyph(0xE72E),
                                               .background          = purple,
                                               .behavior_on_invoked = ui::SwipeBehaviorOnInvoked::Close};
            ui::SwipeItem                 coffee{.text = "Coffee", .icon = ui::IconSource::bitmap(coffee_cup_image())};

            auto label = [](ui::Binding<std::string> text)
            {
                return ui::Text{.text                 = std::move(text),
                                .margin               = ui::Thickness{12.0f},
                                .horizontal_alignment = ui::Alignment::Center,
                                .vertical_alignment   = ui::Alignment::Center};
            };

            auto list = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
            s.rows    = list.get();
            s.rows_watch->reset(s.rows);
            for (int i = 1; i <= 4; ++i)
            {
                s.rows->add(swipe_list_row(s, ctx, "Swipe Item " + std::to_string(i)));
            }

            auto sized = [](ui::Spec row)
            {
                // The samples' Width="500" Height="68" Margin="12".
                return ui::Border{.margin               = ui::Thickness{12.0f},
                                  .width                = 500.0f,
                                  .height               = 68.0f,
                                  .horizontal_alignment = ui::Alignment::Leading,
                                  .content              = std::move(row)};
            };
            return examples({
                example({.title    = "Swipe right to reveal actions",
                         .code     = R"cpp(
                             ui::Property<std::string> text{"Swipe Right"};
                             bool                      accepted = false;

                             // An invoked item can change its own text and icon.
                             ui::SwipeItem accept{
                                 .text       = "Accept",
                                 .icon       = ui::IconGlyph{0xE8FB, "Segoe Fluent Icons", 20.0f},
                                 .on_invoked = [&](ui::SwipeItemInvokedEventArgs& args)
                                 {
                                     accepted       = !accepted;
                                     args.item.text = accepted ? "Cancel" : "Accept";
                                     args.item.icon = ui::IconGlyph{accepted ? 0xE711 : 0xE10B, "Segoe Fluent Icons", 20.0f};
                                     text           = accepted ? "Swipe Right - Accepted" : "Swipe Right";
                                 },
                             };
                             ui::SwipeItem flag{/* ... the same way ... */};

                             ui::Swipe{
                                 .left_items            = {accept, flag},
                                 .border_brush = [](const ui::Theme& theme) { return theme.colors().control_fill; },
                                 .border_thickness      = ui::Thickness{1},
                                 .content               = ui::Text{.text = text, .margin = ui::Thickness{12}},
                             }
                         )cpp",
                         .controls = {sized(swipe_row(label(s.reveal_text), {accept, flag}, {}))}}),
                example({.title    = "Swipe left to invoke an execute",
                         .code     = R"cpp(
                             ui::Property<std::string> text{"Swipe Left"};
                             bool                      archived = false;

                             // Execute mode: a full swipe invokes the one item.
                             ui::Swipe{
                                 .right_items = {
                                     ui::SwipeItem{
                                         .text                = "Archive",
                                         .icon                = ui::IconGlyph{0xE7B8, "Segoe Fluent Icons", 20.0f},
                                         .behavior_on_invoked = ui::SwipeBehaviorOnInvoked::Close,
                                         .on_invoked          = [&]
                                         {
                                             archived = !archived;
                                             text     = archived ? "Archived - Swipe Left" : "Swipe Left";
                                         },
                                     },
                                 },
                                 .right_mode = ui::SwipeMode::Execute,
                                 .content    = ui::Text{.text = text, .margin = ui::Thickness{12}},
                             }
                         )cpp",
                         .controls = {sized(swipe_row(label(s.execute_text), {}, {archive}, ui::SwipeMode::Execute))}}),
                example({.title    = "Custom Swipe in a ListView",
                         .code     = R"cpp(
                             ui::StackControl* rows = nullptr; // the list the rows are added to

                             // One row: two revealed items on the left, a delete executed from the right.
                             auto row = [&](std::string text) -> ui::Spec
                             {
                                 return ui::Swipe{
                                     .left_items = {
                                         ui::SwipeItem{.text       = "Reply All",
                                                       .icon       = ui::IconGlyph{0xE8C2, "Segoe Fluent Icons", 20.0f},
                                                       .background = ui::Color::from_argb(0xFF3E6FA7),
                                                       .foreground = ui::colors::White},
                                         ui::SwipeItem{.text       = "Open",
                                                       .icon       = ui::IconGlyph{0xE8C3, "Segoe Fluent Icons", 20.0f},
                                                       .background = ui::Color::from_argb(0xFFFF9501),
                                                       .foreground = ui::colors::White},
                                     },
                                     .right_items = {
                                         ui::SwipeItem{.text       = "Delete",
                                                       .icon       = ui::IconGlyph{0xE74D, "Segoe Fluent Icons", 20.0f},
                                                       .background = ui::theme_colors::Critical,
                                                       .on_invoked = [&, text]
                                                       {
                                                           // Posted, since the row is still handling the swipe.
                                                           window.post([&, text] { /* ... rows->remove(the row named text) ... */ });
                                                       }},
                                     },
                                     .right_mode    = ui::SwipeMode::Execute,
                                     .height        = 68,
                                     .accessibility = {.name = text},
                                     .content       = ui::Text{.text = text, .text_style = ui::TextStyle{.font_size = 24}},
                                 };
                             };

                             auto list = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
                             rows      = list.get();
                             for (int i = 1; i <= 4; ++i)
                             {
                                 rows->add(row("Swipe Item " + std::to_string(i)).build());
                             }

                             ui::Scroll{.width = 800, .height = 300, .content = std::move(list)}
                         )cpp",
                         .controls = {ui::Scroll{.margin    = ui::Thickness{12.0f},
                                                 .width     = 800.0f,
                                                 .height    = 300.0f,
                                                 .min_width = 200.0f,
                                                 .content   = std::move(list)}}}),
                example(
                    {.title = "Gradient Background",
                     .code  = R"cpp(
                         const ui::LinearGradientBrush purple{{{0.0f, ui::Color::from_argb(0xFF8990F9)},
                                                               {0.5f, ui::Color::from_argb(0xFF5B66FB)},
                                                               {1.0f, ui::Color::from_argb(0xFF5C1DF4)}},
                                                              ui::GradientAxis::Horizontal};

                         ui::Swipe{
                             .right_items = {
                                 ui::SwipeItem{
                                     .text                = "Lock",
                                     .icon                = ui::IconGlyph{0xE72E, "Segoe Fluent Icons", 20.0f},
                                     .background          = purple,
                                     .behavior_on_invoked = ui::SwipeBehaviorOnInvoked::Close,
                                 },
                             },
                             .right_mode = ui::SwipeMode::Execute,
                             .content    = ui::Text{.text = "Swipe Left", .margin = ui::Thickness{12}},
                         }
                     )cpp",
                     .controls = {sized(swipe_row(label(std::string("Swipe Left")), {}, {lock}, ui::SwipeMode::Execute))}}),
                example({.title    = "Custom icons",
                         .code     = R"cpp(
                             ui::Bitmap cup{32, 32, ui::colors::Transparent};
                             // ... draw or load the picture ...

                             // A bitmap icon is drawn in one color, from the picture's alpha.
                             ui::Swipe{
                                 .left_items = {ui::SwipeItem{.text = "Coffee", .icon = ui::IconSource::bitmap(cup)}},
                                 .content    = ui::Text{.text = "Swipe Right", .margin = ui::Thickness{12}},
                             }
                         )cpp",
                         .controls = {sized(swipe_row(label(std::string("Swipe Right")), {coffee}, {}))}}),
            });
        });
}

// ---- StandardCommand ----

// One row of the sample's ListView: a list item's selection and hover look around the row's
// template, and its context menu. ListView has no item templates, so the rows are hand-built.
class CommandRow final : public ui::ContentHost
{
public:
    ui::Callback<void()>     on_selected;
    ui::AppBarButtonControl* hover_button = nullptr;

    void set_selected(bool selected)
    {
        selected_ = selected;
        mark_dirty(ui::DirtyFlags::Visual);
    }

    [[nodiscard]] bool focusable() const override
    {
        return true; // a list item is a tab stop; its hover button is not
    }
    void collect_focusables(std::vector<ui::Element*>& out) override
    {
        out.push_back(this);
    }
    bool pointer_override(const ui::PointerEvent& e) override
    {
        const bool inside = bounds().contains(e.position);
        if (e.action == ui::PointerAction::Move || e.action == ui::PointerAction::Leave)
        {
            const bool now = inside && e.action == ui::PointerAction::Move;
            if (now != hovered_)
            {
                hovered_ = now;
                // Show the hover button while the pointer is over the row, hide it otherwise.
                hover_button->set_visibility(hovered_ ? ui::Visibility::Visible : ui::Visibility::Collapsed);
                mark_dirty(ui::DirtyFlags::Layout | ui::DirtyFlags::Visual);
            }
        }
        if (e.action == ui::PointerAction::Down && inside && on_selected)
        {
            on_selected();
        }
        ui::ContentHost::pointer_override(e);
        return false;
    }
    void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
    {
        const ui::Theme& theme = context.theme;
        if (selected_ || hovered_)
        {
            const ui::ButtonVisual& fill = theme.subtle_for_state(
                selected_ ? ui::ButtonState::Pressed : ui::ButtonState::Hover);
            renderer.fill_rounded_rect(bounds(), theme.corner_radius, fill.background);
        }
        ui::ContentHost::render_override(renderer, context);
        // The flyout wrapped around the row holds its place in the tab ring, so it is the one Window
        // tells whether the keyboard put focus there.
        if (is_focused() && parent() != nullptr && parent()->focus_visible())
        {
            // Inside the row, as a row in a list is ringed.
            const ui::FocusRingStyle ring = theme.focus_ring_style();
            ui::draw_focus_ring(renderer, bounds().inset(ui::Thickness{ring.outward_extent()}), theme.corner_radius, ring);
        }
    }
    // The ring follows the list's focus-visible state, so the tile draws it itself.
    [[nodiscard]] bool draws_focus_ring() const override
    {
        return false;
    }

private:
    bool selected_ = false;
    bool hovered_  = false;
};

struct StandardCommandState
{
    ui::Command                       delete_command;
    std::vector<std::string>          items;
    std::size_t                       selected   = static_cast<std::size_t>(-1);
    ui::StackControl*                 rows       = nullptr;
    std::shared_ptr<ui::ElementWatch> rows_watch = std::make_shared<ui::ElementWatch>();
    std::vector<CommandRow*>          row_elements;

    void select(std::size_t index)
    {
        selected = index;
        for (std::size_t i = 0; i < row_elements.size(); ++i)
        {
            row_elements[i]->set_selected(i == index);
        }
    }
};

std::unique_ptr<ui::Element> standard_command_row(StandardCommandState& s, const std::string& text)
{
    auto row = std::make_unique<CommandRow>();

    // The row's template: a Swipe whose Execute item deletes the row, over the text and an
    // AppBarButton for the same command that shows while the pointer is on the row.
    ui::Control<ui::AppBarButton> hover;
    ui::Spec                      layout = ui::Grid{
        .columns            = {1_fr, ui::AutoSize},
        .rows               = {1_fr},
        .vertical_alignment = ui::Alignment::Center,
        .content =
            {
                ui::Cell{.row     = 0,
                         .column  = 0,
                         .content = ui::Text{.text                 = text,
                                             .text_style           = ui::TextStyle{.font_size = 18.0f},
                                             .margin               = ui::Thickness{10.0f},
                                             .horizontal_alignment = ui::Alignment::Leading,
                                             .vertical_alignment   = ui::Alignment::Center}},
                ui::Cell{.row    = 0,
                         .column = 1,
                         .content = ui::AppBarButton{.command = s.delete_command, .command_parameter = text, .control = hover}},
            },
    };
    ui::Spec swipe = ui::Swipe{
        .right_items = {ui::SwipeItem{.background        = ui::theme_colors::Critical,
                                      .command           = s.delete_command,
                                      .command_parameter = text}},
        .right_mode  = ui::SwipeMode::Execute,
        .content     = std::move(layout),
    };
    row->set_child(std::move(swipe).build());
    hover->set_visibility(ui::Visibility::Collapsed);
    row->hover_button = hover;
    row->set_height(60.0f); // the sample's list item height
    row->set_accessibility(ui::Accessibility{.name = text});

    // Its context menu: a MenuFlyout with one item for the command (no parameter: the selected
    // row), which selects the row as it opens.
    auto flyout = std::make_unique<ui::MenuFlyoutControl>();
    flyout->set_items({ui::MenuItem::from_command(s.delete_command)});
    ui::MenuFlyoutControl* menu = flyout.get();
    CommandRow*            raw  = flyout->set_anchor(std::move(row));
    raw->on_context_requested(
        [&s, menu, raw](std::optional<ui::Point> at)
        {
            const auto it = std::find(s.row_elements.begin(), s.row_elements.end(), raw);
            if (it != s.row_elements.end())
            {
                s.select(static_cast<std::size_t>(it - s.row_elements.begin()));
            }
            menu->show_at(at.value_or(raw->bounds().center()));
        });
    raw->on_selected = [&s, raw]
    {
        const auto it = std::find(s.row_elements.begin(), s.row_elements.end(), raw);
        if (it != s.row_elements.end())
        {
            s.select(static_cast<std::size_t>(it - s.row_elements.begin()));
        }
    };
    s.row_elements.push_back(raw);
    return flyout;
}

ui::Spec standard_command_page(Context& ctx)
{
    return with_state<StandardCommandState>(
        [&ctx](StandardCommandState& s)
        {
            // The row the parameter names, else the selected row.
            s.delete_command = ui::standard_command(ui::StandardCommand::Delete,
                                                    [&s, &ctx](std::string_view parameter)
                                                    {
                                                        std::size_t index = s.selected;
                                                        if (!parameter.empty())
                                                        {
                                                            const auto it = std::find(s.items.begin(), s.items.end(), parameter);
                                                            index = it != s.items.end()
                                                                        ? static_cast<std::size_t>(it - s.items.begin())
                                                                        : static_cast<std::size_t>(-1);
                                                        }
                                                        if (index >= s.items.size())
                                                        {
                                                            return;
                                                        }
                                                        // Posted: the control running the command is inside the row it removes.
                                                        std::shared_ptr<ui::ElementWatch> watch = s.rows_watch;
                                                        ctx.window.post(
                                                            [&s, watch, index]
                                                            {
                                                                if (watch->get() == nullptr || index >= s.items.size())
                                                                {
                                                                    return;
                                                                }
                                                                s.items.erase(s.items.begin() +
                                                                              static_cast<std::ptrdiff_t>(index));
                                                                s.row_elements.erase(s.row_elements.begin() +
                                                                                     static_cast<std::ptrdiff_t>(index));
                                                                s.rows->remove(s.rows->children()[index].get());
                                                                s.select(static_cast<std::size_t>(-1));
                                                            });
                                                    });

            auto list = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
            s.rows    = list.get();
            s.rows_watch->reset(s.rows);
            for (int i = 0; i < 15; ++i)
            {
                s.items.push_back("List item " + std::to_string(i));
                s.rows->add(standard_command_row(s, s.items.back()));
            }

            return example({
                .title   = "Exposing a command in multiple controls using StandardCommand",
                .code    = R"cpp(
                    // The command brings its text, icon, shortcut and description with it.
                    ui::Command delete_command = ui::standard_command(
                        ui::StandardCommand::Delete,
                        [&](std::string_view parameter)
                        {
                            // ... remove the row the parameter names, or the selected row if it is empty ...
                        });

                    // In a menu:
                    ui::MenuBar{
                        .items = {
                            ui::MenuBarItem{.text = "Edit", .items = {ui::MenuItem::from_command(delete_command)}},
                        },
                    }

                    // In each row, as a button and as a swipe action; the parameter says which row:
                    ui::Swipe{
                        .right_items = {
                            ui::SwipeItem{
                                .background        = ui::theme_colors::Critical,
                                .command           = delete_command,
                                .command_parameter = text,
                            },
                        },
                        .right_mode = ui::SwipeMode::Execute,
                        .content    = ui::HorizontalStack{
                            .content = {
                                ui::Text{.text = text},
                                ui::AppBarButton{.command = delete_command, .command_parameter = text},
                            },
                        },
                    }

                    // In the row's context menu:
                    auto flyout = std::make_unique<ui::MenuFlyoutControl>();
                    flyout->set_items({ui::MenuItem::from_command(delete_command)});
                )cpp",
                .stacked = true,
                .stretch = true,
                .controls =
                    {
                        paragraph("A standard command comes with the text, icon, keyboard shortcut and "
                                  "description of a common action. Here the Delete command sits in the menu, on "
                                  "every row's button and swipe action, and in every row's context menu, and the "
                                  "app only says what deleting does.",
                                  ui::Edges{.bottom = 12.0f}),
                        ui::MenuBar{.items = {ui::MenuBarItem{.text  = "File",
                                                              .items = {ui::MenuItem{.text = "New"},
                                                                        ui::MenuItem{.text = "Open..."},
                                                                        ui::MenuItem{.text = "Save"},
                                                                        ui::MenuItem{.text = "Exit"}}},
                                              ui::MenuBarItem{.text  = "Edit",
                                                              .items = {ui::MenuItem::from_command(s.delete_command)}},
                                              ui::MenuBarItem{.text = "Help", .items = {ui::MenuItem{.text = "About"}}}},
                                    .horizontal_alignment = ui::Alignment::Leading},
                        ui::Scroll{.height = 500.0f, .content = std::move(list)},
                    },
            });
        });
}

// ---- Command ----

struct CommandState
{
    ui::Property<bool>        enabled{true};
    ui::Property<std::string> output{};
    ui::Command               custom;
};

ui::Spec command_page(Context&)
{
    return with_state<CommandState>(
        [](CommandState& s)
        {
            s.custom = ui::Command{
                .text        = "Custom command",
                .icon        = ui::Symbol::Favorite,
                .shortcut    = ui::Ctrl + ui::Key::D,
                .description = "This is a custom command",
                .enabled     = s.enabled,
                .execute     = [&s] { s.output = "You fired the custom command"; },
            };
            return example({
                .title   = "Creating a reusable command",
                .code    = R"cpp(
                    ui::Property<bool>        enabled{true};
                    ui::Property<std::string> output;

                    // Defined once; it has to outlive the controls that point at it.
                    ui::Command custom{
                        .text       = "Custom command",
                        .icon        = ui::Symbol::Favorite,
                        .shortcut = ui::Ctrl + ui::Key::D,
                        .description = "This is a custom command",
                        .enabled     = enabled,
                        .execute     = [&] { output = "You fired the custom command"; },
                    };

                    // The button takes its text, icon, shortcut and enabled state from the command.
                    ui::AppBarButton{.command = custom}
                    ui::CheckBox{.text = "Enable the command", .checked = enabled}
                )cpp",
                .stacked = true,
                .stretch = true,
                .controls =
                    {
                        paragraph("A command carries its text, icon, shortcut, description and enabled state. It's "
                                  "defined once and can be used in many controls, like this AppBarButton, which "
                                  "takes all of them from the command and follows them as they change.",
                                  ui::Edges{.bottom = 12.0f}),
                        ui::HorizontalStack{.content = {ui::AppBarButton{.command = s.custom}, side_output(s.output)}},
                        ui::CheckBox{.text = "Enable the command", .checked = s.enabled},
                    },
            });
        });
}

// ---- Toolbar (libwinui's own) ----

ui::Spec toolbar_page(Context& ctx)
{
    auto& status = ctx.status;
    return example("A row of commands, with a separator",
                   {
                       ui::Toolbar{
                           .content =
                               {
                                   ui::Button{.text = "New", .on_click = [&status] { status = "Toolbar: New clicked"; }},
                                   ui::Button{.text = "Open", .on_click = [&status] { status = "Toolbar: Open clicked"; }},
                                   ui::Separator{},
                                   ui::Button{.text = "Save", .on_click = [&status] { status = "Toolbar: Save clicked"; }},
                               }},
                   },
                   false,
                   R"cpp(
                       ui::Property<std::string> status;

                       ui::Toolbar{
                           .content = {
                               ui::Button{.text = "New", .on_click = [&] { status = "Toolbar: New clicked"; }},
                               ui::Button{.text = "Open", .on_click = [&] { status = "Toolbar: Open clicked"; }},
                               ui::Separator{},
                               ui::Button{.text = "Save", .on_click = [&] { status = "Toolbar: Save clicked"; }},
                           },
                       }
                   )cpp");
}

} // namespace

std::span<const Page> menus_and_toolbars_pages()
{
    static const auto Pages = std::to_array<Page>({
        {"AppBarButton", "A button that's styled for use in a CommandBar.", app_bar_button_page},
        {"AppBarSeparator",
         "A vertical line that's used to visually separate groups of commands in a CommandBar.",
         app_bar_separator_page},
        {"AppBarToggleButton",
         "A button that can be on, off, or indeterminate like a CheckBox, and is styled for use in a CommandBar.",
         app_bar_toggle_button_page},
        {"CommandBar",
         "A toolbar for displaying application-specific commands that handles layout and resizing of its contents.",
         command_bar_page},
        {"CommandBarFlyout",
         "A mini-toolbar displaying proactive commands, and an optional menu of commands.",
         command_bar_flyout_page},
        {"MenuBar", "A row of headers, each dropping down a menu of MenuItems.", menu_page},
        {"MenuFlyout", "Shows a contextual list of simple commands or options.", menu_flyout_page},
        {"Swipe", "Touch gesture for quick menu actions on items.", swipe_page},
        {"StandardCommand", "A built-in Command which represents a commonly used command, e.g. 'Save'.", standard_command_page},
        {"Command", "An object which is used to define the look and feel of a given command.", command_page},
        {"Toolbar", "A plain row of controls with separators between groups.", toolbar_page},
    });
    return Pages;
}

} // namespace gallery
