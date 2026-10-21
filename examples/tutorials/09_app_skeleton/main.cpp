// SPDX-License-Identifier: MIT
// libwinui tutorial: an app skeleton.
// --8<-- [start:includes]
#include <winui/winui.hpp>

#include <format>
#include <string>

#include <cstddef>
// --8<-- [end:includes]


namespace
{
constexpr ui::IconGlyph AppGlyph{0xE8A5};

// --8<-- [start:pages]
ui::Spec home_page()
{
    return ui::VerticalStack{
        .spacing = 8,
        .margin  = ui::Edges{.left = 24, .right = 24, .bottom = 24},
        .content =
            {
                ui::Text{.text = "Welcome back."},
                ui::Text{.text = "Pick a page on the left.", .role = ui::TextRole::Caption},
            },
    };
}

ui::Spec document_editor(std::string text)
{
    return ui::TextBox{
        .placeholder_text = "Start typing",
        .text             = std::move(text),
        .accepts_return   = true,
        .wrapping         = ui::TextWrapping::Wrap,
        .margin           = 12,
    };
}

ui::Spec reports_page(int builds)
{
    return ui::Text{
        .text   = std::format("This page was built {} time(s). Switch away and back to rebuild it.", builds),
        .margin = ui::Edges{.left = 24, .right = 24, .bottom = 24},
    };
}

ui::Spec settings_page(ui::Property<bool>& compact)
{
    return ui::ToggleSwitch{
        .header  = "Compact navigation pane",
        .checked = compact,
        .margin  = ui::Edges{.left = 24, .right = 24, .bottom = 24},
    };
}
// --8<-- [end:pages]

} // namespace

int main()
{
    using ui::operator""_fr;

    // --8<-- [start:window]
    ui::Window window{{
        .title = "App skeleton",
        .size  = {1100, 720},
        .style = {.extend_content_into_title_bar = true, .title_bar_height = 48},
    }};
    // --8<-- [end:window]

    // --8<-- [start:state]
    ui::Property<bool>               compact{false};
    ui::Property<std::string>        header{"Home"};
    ui::Property<std::string>        page; // the selected item's key
    int                              report_builds = 0;
    int                              next_document = 3;
    ui::Control<ui::NavigationShell> nav;
    ui::Control<ui::Tabs>            documents;
    // --8<-- [end:state]

    // --8<-- [start:title_bar]
    ui::Spec title_bar = ui::TitleBar{
        .title                      = "App skeleton",
        .subtitle                   = "Tutorial",
        .icon                       = AppGlyph,
        .pane_toggle_button_visible = true,
        .on_pane_toggle_requested   = [&] { nav->set_pane_open(!nav->is_pane_open()); },
        .content                    = ui::AutoSuggestBox{.placeholder_text = "Search", .width = 320},
    };
    // --8<-- [end:title_bar]

    // --8<-- [start:tabs]
    ui::Spec documents_page = ui::Tabs{
        .on_add_tab_button_click =
            [&]
        {
            documents->add_tab(std::format("Document {}", next_document++), ui::build(document_editor("")));
            documents->set_selected_index(documents->tab_count() - 1);
        },
        .control = documents,
        .items =
            {
                ui::Tab{.header = "Document 1", .content = document_editor("Tabs keep their content while you switch.")},
                ui::Tab{.header = "Document 2", .content = document_editor("")},
            },
    };
    // --8<-- [end:tabs]

    // --8<-- [start:switch]
    ui::Spec shared_content = ui::Match<std::string>{
        .value   = page,
        .content = [&](const std::string& key) -> ui::Spec
        {
            if (key == ui::NavigationShellControl::SettingsKey)
            {
                return settings_page(compact);
            }
            if (key == "reports")
            {
                return reports_page(++report_builds);
            }
            return {};
        },
    };
    // --8<-- [end:switch]

    // --8<-- [start:frame]
    ui::Spec frame = ui::NavigationShell{
        .header                     = header,
        .selected_key               = page,
        .pane_display_mode          = ui::bind(compact,
                                               [](bool on)
                                               {
                                          return on ? ui::NavigationShellPaneDisplayMode::LeftCompact
                                                    : ui::NavigationShellPaneDisplayMode::Auto;
                                               }),
        .pane_toggle_button_visible = false,
        .on_selection_changed = [&](const std::string& key) { header = nav->text_at(nav->index_of(key).value_or(0)); },
        .control              = nav,
        .content              = std::move(shared_content),
        .items =
            {
                ui::NavItem{.text = "Home", .key = "home", .icon = ui::IconGlyph{0xE80F}, .content = home_page()},
                ui::NavItem{.text    = "Documents",
                            .key     = "documents",
                            .icon    = ui::IconGlyph{0xE8A5},
                            .content = std::move(documents_page)},
                ui::NavItem{.text = "Reports", .key = "reports", .icon = ui::IconGlyph{0xE9D2}},
            },
    };
    // --8<-- [end:frame]

    // --8<-- [start:layout]
    window.set_content(ui::Grid{
        .rows = {ui::AutoSize, 1_fr},
        .content = {ui::Cell{.row = 0, .content = std::move(title_bar)}, ui::Cell{.row = 1, .content = std::move(frame)}},
    });
    // --8<-- [end:layout]

    // --8<-- [start:run]
    return ui::run(window);
    // --8<-- [end:run]
}
