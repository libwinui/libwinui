// SPDX-License-Identifier: MIT
#include "gallery.hpp"

#include <array>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <cmath>
#include <cstddef>

using ui::operator""_dip;
using ui::operator""_fr;

namespace gallery
{
namespace
{
// Symbol-enum codepoints the samples name, which render/IconGlyph.hpp has no entry for.
constexpr ui::IconGlyph HomeGlyph{0xE80F};                              // Symbol.Home
constexpr ui::IconGlyph MailGlyph{0xE715};                              // Symbol.Mail
constexpr ui::IconGlyph SyncGlyph{0xE117, "Segoe Fluent Icons", 20.0f}; // Symbol.Sync, a Symbol's 20px

ui::Spec options_panel(std::optional<float> width, ui::SpecList children)
{
    return ui::VerticalStack{
        .spacing         = 8.0f,
        .child_alignment = ui::Alignment::Stretch,
        .width           = width,
        .content         = std::move(children),
    };
}

// The "Is Open" check box every InfoBar card carries, two-way with the bar: ticking it opens the
// bar, and the bar's close button clears it (through its on_closed, below).
ui::Spec open_box(ui::Property<bool>& open)
{
    return ui::CheckBox{.text               = "Is Open",
                        .checked            = ui::bind_one_way(open),
                        .on_checked_changed = [&open](std::optional<bool> on) { open = on == true; }};
}

ui::Spec flag_box(const char* text, ui::Property<bool>& flag)
{
    return ui::CheckBox{.text               = text,
                        .checked            = ui::bind_one_way(flag),
                        .on_checked_changed = [&flag](std::optional<bool> on) { flag = on == true; }};
}

// ---- InfoBadge ----

constexpr std::array<ui::InfoBadgeSeverity, 4> BadgeStyles{ui::InfoBadgeSeverity::Attention,
                                                           ui::InfoBadgeSeverity::Informational,
                                                           ui::InfoBadgeSeverity::Success,
                                                           ui::InfoBadgeSeverity::Critical};

constexpr std::size_t InboxItem = 2; // pre-order index of "Inbox" in the card's NavigationShell

struct InfoBadgeState
{
    ui::Control<ui::NavigationShell>    nav;
    ui::Property<ui::InfoBadgeSeverity> style{ui::InfoBadgeSeverity::Attention};
    ui::Property<int>                   dynamic_value{1};
};

std::unique_ptr<ui::InfoBadgeControl> inbox_badge()
{
    return std::make_unique<ui::InfoBadgeControl>(5);
}

// The third card: a 200x60 Button with no padding, whose content Grid overlays a centered
// Symbol.Sync and a red icon badge in the top-right corner.
ui::Spec refresh_button()
{
    ui::Spec sync  = ui::Icon{.source               = SyncGlyph,
                              .horizontal_alignment = ui::Alignment::Center,
                              .vertical_alignment   = ui::Alignment::Center};
    ui::Spec badge = ui::InfoBadge{.icon                 = ui::IconGlyph{0xF13C},
                                   .background           = ui::Color::from_argb(0xFFC42B1C),
                                   .horizontal_alignment = ui::Alignment::Trailing,
                                   .vertical_alignment   = ui::Alignment::Leading};
    return ui::Button{.padding       = ui::Thickness{0.0f},
                      .width         = 200.0f,
                      .height        = 60.0f,
                      .accessibility = {.name = "Example3Button"},
                      .content = ui::Grid{.columns = {1_fr},
                                          .rows    = {1_fr},
                                          .content = {ui::Cell{.row = 0, .column = 0, .content = std::move(sync)},
                                                      ui::Cell{.row = 0, .column = 0, .content = std::move(badge)}}}};
}

// "InfoBadge Opacity" takes the badge out and puts it back, which is what opacity 0 and 1 look like.
ui::Spec info_badge_page(Context&)
{
    return with_state<InfoBadgeState>(
        [](InfoBadgeState& s)
        {
            return examples({
                example({
                    .title   = "InfoBadge embedded in NavigationShell",
                    .code    = R"cpp(
                        ui::Control<ui::NavigationShell> nav;

                        ui::NavigationShell{
                            .pane_width        = 320,
                            .item_row_height   = 40,
                            .pane_display_mode = ui::NavigationShellPaneDisplayMode::Left,
                            .height            = 300,
                            .control           = nav,
                            .content           = ui::VerticalStack{},
                            .items             = {
                                ui::NavItem{.text = "Home", .icon = ui::IconGlyph{0xE80F}},
                                ui::NavItem{.text = "Account", .icon = ui::glyphs::Contact},
                                ui::NavItem{.text      = "Inbox",
                                            .icon       = ui::IconGlyph{0xE715},
                                            .info_badge = ui::InfoBadge{.value = 5}},
                            },
                        }

                        // Later: take the badge off the third item, or put one back.
                        nav->set_info_badge(2, nullptr);
                        nav->set_info_badge(2, std::make_unique<ui::InfoBadgeControl>(5));

                        // The pane's other modes show the badge too.
                        nav->set_pane_display_mode(ui::NavigationShellPaneDisplayMode::LeftCompact);
                    )cpp",
                    .stretch = true,
                    .options = options_panel(160.0f,
                                             {
                                                 ui::ToggleSwitch{.header = "InfoBadge Opacity",
                                                                  .checked     = true,
                                                                  .on_checked_changed =
                                                                      [&s](bool on)
                                                                  {
                                                                      if (s.nav != nullptr)
                                                                      {
                                                                          s.nav->set_info_badge(InboxItem,
                                                                                                on ? inbox_badge() : nullptr);
                                                                      }
                                                                  }},
                                                 ui::ComboBox{.header = "Display Mode",
                                                              .items  = {"LeftExpanded", "LeftCompact", "Top"},
                                                              .selected_index = 0,
                                                              .on_selection_changed =
                                                                  [&s](std::optional<std::size_t> picked)
                                                              {
                                                                  if (s.nav == nullptr || !picked)
                                                                  {
                                                                      return;
                                                                  }
                                                                  const std::size_t index = *picked;
                                                                  constexpr std::array<ui::NavigationShellPaneDisplayMode, 3>
                                                                      Modes{ui::NavigationShellPaneDisplayMode::Left,
                                                                             ui::NavigationShellPaneDisplayMode::LeftCompact,
                                                                             ui::NavigationShellPaneDisplayMode::Top};
                                                                  s.nav->set_pane_display_mode(Modes[index]);
                                                                  // LeftCompact starts closed; the other two open.
                                                                  s.nav->set_pane_open(index != 1);
                                                              }},
                                             }),
                    .controls =
                        {
                            ui::NavigationShell{
                                .pane_width        = 320.0f,
                                .item_row_height   = 40.0f,
                                .pane_display_mode = ui::NavigationShellPaneDisplayMode::Left,
                                .height            = 300.0f,
                                .control           = s.nav,
                                .content           = ui::VerticalStack{},
                                .items =
                                    {
                                        ui::NavItem{.text = "Home", .icon = HomeGlyph},
                                        ui::NavItem{.text = "Account", .icon = ui::glyphs::Contact},
                                        ui::NavItem{.text = "Inbox", .icon = MailGlyph, .info_badge = ui::InfoBadge{.value = 5}},
                                    },
                            },
                        },
                }),
                // The picker leaves Caution out; the control has it.
                example({
                    .title   = "Different InfoBadge Styles",
                    .code    = R"cpp(
                        ui::Property<ui::InfoBadgeSeverity> style{ui::InfoBadgeSeverity::Attention};

                        ui::HorizontalStack{
                            .spacing         = 20,
                            .child_alignment = ui::Alignment::Center,
                            .content         = {
                                // An icon badge, with the glyph that goes with the severity.
                                ui::InfoBadge{
                                    .severity = style,
                                    .icon     = ui::bind(style,
                                                         [](ui::InfoBadgeSeverity s)
                                                         {
                                                             return ui::IconSource{
                                                                 ui::InfoBadgeControl::severity_glyph(s)};
                                                         }),
                                },
                                // A number badge.
                                ui::InfoBadge{.value = 10, .severity = style},
                                // A dot badge.
                                ui::InfoBadge{.severity = style},
                            },
                        }

                        style = ui::InfoBadgeSeverity::Critical; // all three follow
                    )cpp",
                    .options = options_panel(160.0f,
                                             {
                                                 ui::ComboBox{.header = "Styles",
                                                              .items =
                                                                  {"Attention", "Informational", "Success", "Critical"},
                                                              .selected_index = 0,
                                                              .on_selection_changed = [&s](std::optional<std::size_t> index)
                                                              { s.style = BadgeStyles[index.value_or(0)]; }},
                                             }),
                    .controls =
                        {
                            ui::HorizontalStack{
                                .spacing              = 20.0f,
                                .child_alignment      = ui::Alignment::Center,
                                .horizontal_alignment = ui::Alignment::Center,
                                .content =
                                    {
                                        ui::InfoBadge{.severity = s.style,
                                                      .icon     = ui::bind(s.style,
                                                                           [](ui::InfoBadgeSeverity style)
                                                                           {
                                                                           return ui::IconSource{
                                                                               ui::InfoBadgeControl::severity_glyph(style)};
                                                                           })},
                                        ui::InfoBadge{.value = 10, .severity = s.style},
                                        ui::InfoBadge{.severity = s.style},
                                    },
                            },
                        },
                }),
                example(
                    {
                        .title = "Placing an InfoBadge Inside Another Control",
                        .code  = R"cpp(
                            using ui::operator""_fr;

                            // A button whose content overlays an icon and a badge in one Grid cell.
                            ui::Button{
                                .padding = ui::Thickness{0},
                                .width   = 200,
                                .height  = 60,
                                .content = ui::Grid{
                                    .columns = {1_fr},
                                    .rows    = {1_fr},
                                    .content   = {
                                        ui::Cell{.row     = 0,
                                         .column  = 0,
                                         .content = ui::Icon{.source = ui::IconGlyph{0xE117, "Segoe Fluent Icons", 20.0f},
                                                             .horizontal_alignment = ui::Alignment::Center,
                                                             .vertical_alignment   = ui::Alignment::Center}},
                                        ui::Cell{.row     = 0,
                                         .column  = 0,
                                         .content = ui::InfoBadge{.icon       = ui::IconGlyph{0xF13C},
                                                                  .background = ui::Color::from_argb(0xFFC42B1C),
                                                                  .horizontal_alignment = ui::Alignment::Trailing,
                                                                  .vertical_alignment   = ui::Alignment::Leading}},
                                    },
                                },
                            }
                        )cpp",
                        .controls =
                            {
                                ui::ToolTip{.horizontal_alignment = ui::Alignment::Center,
                                            .anchor               = refresh_button(),
                                            .content              = ui::Text{.text = "Refresh required"}},
                            },
                    }),
                example(
                    {
                        .title = "InfoBadge with Dynamic Value",
                        .code  = R"cpp(
                            ui::Property<int> count{1};

                            ui::InfoBadge{.value = count}

                            ui::NumberBox{
                                .header           = "InfoBadge Value",
                                .value            = 1.0,
                                .min              = -1.0,
                                .on_value_changed = [&](std::optional<double> value)
                                {
                                    if (value)
                                    {
                                        count = static_cast<int>(*value);
                                    }
                                },
                            }
                        )cpp",
                        .options =
                            options_panel(
                                160.0f,
                                {
                                    ui::NumberBox{
                                        .header =
                                            "InfoBadge Value",
                                        .value = 1.0,
                                        .min   = -1.0,
                                        .on_value_changed =
                                            [&s](std::optional<double> value)
                                        {
                                            if (value && *value >= -1.0)
                                            {
                                                s.dynamic_value = static_cast<int>(*value);
                                            }
                                        }},
                                }),
                        .controls =
                            {
                                ui::InfoBadge{.value = s.dynamic_value, .horizontal_alignment = ui::Alignment::Center},
                            },
                    }),
            });
        });
}

// ---- InfoBar ----

constexpr const char*
    EssentialMessage = "Essential app message for your users to be informed of, acknowledge, or take action on.";
constexpr const char* ShortMessage = "A short essential app message.";
constexpr const char* LongMessage =
    "A long essential app message for your users to be informed of, acknowledge, or take action on. Lorem "
    "ipsum dolor sit amet, consectetur adipiscing elit. Proin dapibus dolor vitae justo rutrum, ut lobortis "
    "nibh mattis. Aenean id elit commodo, semper felis nec.";

constexpr std::array<ui::InfoBarSeverity, 4> Severities{ui::InfoBarSeverity::Informational,
                                                        ui::InfoBarSeverity::Success,
                                                        ui::InfoBarSeverity::Warning,
                                                        ui::InfoBarSeverity::Error};

struct InfoBarState
{
    ui::Property<bool>                open1{true};
    ui::Property<ui::InfoBarSeverity> severity{ui::InfoBarSeverity::Informational};

    ui::Property<bool>        open2{true};
    ui::Property<std::string> message{LongMessage};
    ui::Control<ui::InfoBar>  bar2;

    ui::Property<bool> open3{true};
    ui::Property<bool> icon_visible{true};
    ui::Property<bool> closable{true};

    ui::Property<bool> open4{true};
};

// The action slot changes kind, not just text, so it is swapped on the control rather than bound.
void set_action(ui::InfoBarControl* bar, std::size_t kind)
{
    if (bar == nullptr)
    {
        return;
    }
    switch (kind)
    {
        case 1:
            bar->set_action_button_content(ui::build(ui::Button{.text = "Action"}));
            break;
        case 2:
            bar->set_action_button_content(ui::build(ui::Button{.text         = "Informational link",
                                                                .navigate_uri = "http://www.microsoft.com/",
                                                                .variant      = ui::ButtonVariant::Hyperlink}));
            break;
        default:
            bar->clear_action_button_content();
            break;
    }
}

ui::Spec info_bar_page(Context&)
{
    return with_state<InfoBarState>(
        [](InfoBarState& s)
        {
            return examples({
                example({
                    .title = "A closable InfoBar with options to change its Severity.",
                    .code  = R"cpp(
                        ui::Property<bool>                open{true};
                        ui::Property<ui::InfoBarSeverity> severity{ui::InfoBarSeverity::Informational};

                        ui::InfoBar{
                            .title     = "Title",
                            .message   = "Essential app message for your users to be informed of, "
                                         "acknowledge, or take action on.",
                            .is_open   = open,
                            .severity  = severity,
                            // The close button reports here; keep the property in step.
                            .on_closed = [&](ui::PopupCloseReason) { open = false; },
                        }

                        severity = ui::InfoBarSeverity::Warning;
                        open     = true; // shows the bar again
                    )cpp",
                    .options = options_panel(150.0f,
                                             {
                                                 open_box(s.open1),
                                                 ui::ComboBox{.header = "Severity",
                                                              .items = {"Informational", "Success", "Warning", "Error"},
                                                              .selected_index = 0,
                                                              .on_selection_changed = [&s](std::optional<std::size_t> index)
                                                              { s.severity = Severities[index.value_or(0)]; }},
                                             }),
                    .controls =
                        {
                            ui::InfoBar{.title     = "Title",
                                        .message   = EssentialMessage,
                                        .is_open   = s.open1,
                                        .severity  = s.severity,
                                        .on_closed = [&s](ui::PopupCloseReason) { s.open1 = false; }},
                        },
                }),
                example({
                    .title = "A closable InfoBar with a long or short message and various buttons",
                    .code  = R"cpp(
                        ui::Property<bool>        open{true};
                        ui::Property<std::string> message{"A short essential app message."};
                        ui::Control<ui::InfoBar>  bar;

                        ui::InfoBar{
                            .title     = "Title",
                            .message   = message,
                            .is_open   = open,
                            .on_closed = [&](ui::PopupCloseReason) { open = false; },
                            .control   = bar,
                        }

                        // The action slot takes any button, and is swapped on the control.
                        bar->set_action_button_content(ui::build(ui::Button{.text = "Action"}));
                        bar->set_action_button_content(ui::build(ui::Button{
                            .text         = "Informational link",
                            .navigate_uri = "http://www.microsoft.com/",
                            .variant      = ui::ButtonVariant::Hyperlink,
                        }));
                        bar->clear_action_button_content();
                    )cpp",
                    .options = options_panel(150.0f,
                                             {
                                                 open_box(s.open2),
                                                 ui::ComboBox{.header         = "Message Length",
                                                              .items          = {"Short", "Long"},
                                                              .selected_index = 1,
                                                              .on_selection_changed = [&s](std::optional<std::size_t> index)
                                                              { s.message = index == 0 ? ShortMessage : LongMessage; }},
                                                 ui::ComboBox{.header         = "Action Button",
                                                              .items          = {"None", "Button", "Hyperlink"},
                                                              .selected_index = 0,
                                                              .on_selection_changed = [&s](std::optional<std::size_t> index)
                                                              { set_action(s.bar2, index.value_or(0)); }},
                                             }),
                    .controls =
                        {
                            ui::InfoBar{.title     = "Title",
                                        .message   = s.message,
                                        .is_open   = s.open2,
                                        .on_closed = [&s](ui::PopupCloseReason) { s.open2 = false; },
                                        .control   = s.bar2},
                        },
                }),
                example({
                    .title   = "A closable InfoBar with options to display the close button and icon",
                    .code    = R"cpp(
                            ui::Property<bool> open{true};
                            ui::Property<bool> closable{true};
                            ui::Property<bool> icon_visible{true};

                            ui::InfoBar{
                                .title           = "Title",
                                .message         = "Essential app message for your users to be informed of, "
                                                   "acknowledge, or take action on.",
                                .is_open         = open,
                                .closable        = closable,
                                .icon_visible    = icon_visible,
                                .on_closed       = [&](ui::PopupCloseReason) { open = false; },
                            }
                        )cpp",
                    .options = options_panel(150.0f,
                                             {
                                                 open_box(s.open3),
                                                 flag_box("Is Icon Visible", s.icon_visible),
                                                 flag_box("Is Closable", s.closable),
                                             }),
                    .controls =
                        {
                            ui::InfoBar{.title        = "Title",
                                        .message      = EssentialMessage,
                                        .is_open      = s.open3,
                                        .closable     = s.closable,
                                        .icon_visible = s.icon_visible,
                                        .on_closed    = [&s](ui::PopupCloseReason) { s.open3 = false; }},
                        },
                }),
                example({
                    .title   = "An InfoBar with custom content",
                    .code    = R"cpp(
                            ui::Property<bool> open{true};

                            ui::InfoBar{
                                .title     = "Update available.",
                                .message   = "Restart the application to apply the latest update.",
                                .is_open   = open,
                                .on_closed = [&](ui::PopupCloseReason) { open = false; },
                                // Gets a row of its own under the title and message.
                                .content   = ui::ProgressBar{
                                    .margin               = ui::Edges{.bottom = 6},
                                    .max_width            = 200, // centered in the row, as a stretched bar is
                                },
                            }
                        )cpp",
                    .options = options_panel(150.0f, {open_box(s.open4)}),
                    .controls =
                        {
                            ui::InfoBar{.title     = "Update available.",
                                        .message   = "Restart the application to apply the latest update.",
                                        .is_open   = s.open4,
                                        .on_closed = [&s](ui::PopupCloseReason) { s.open4 = false; },
                                        .content = ui::ProgressBar{.margin = ui::Edges{.bottom = 6.0f}, .max_width = 200.0f}},
                        },
                }),
            });
        });
}

// ---- ProgressBar ----

struct ProgressBarState
{
    ui::Property<int>    state{0}; // Running, Paused, Error
    ui::Property<double> value{50.0};
};

ui::Spec progress_bar_page(Context&)
{
    return with_state<ProgressBarState>(
        [](ProgressBarState& s)
        {
            return examples(
                {
                    example({
                        .title   = "An indeterminate progress bar.",
                        .code    = R"cpp(
                            ui::Property<std::optional<std::size_t>> state{0}; // 0 running, 1 paused, 2 error

                            ui::ProgressBar{
                                .show_paused   = ui::bind(state, [](std::optional<std::size_t> v) { return v == 1; }),
                                .show_error    = ui::bind(state, [](std::optional<std::size_t> v) { return v == 2; }),
                                .width         = 130,
                            }

                            ui::RadioButtons{
                                .header               = "Progress state",
                                .items                = {"Running", "Paused", "Error"},
                                .selected_index = state,
                            }
                        )cpp",
                        .options = ui::RadioButtons{.header               = "Progress state",
                                                    .items                = {"Running", "Paused", "Error"},
                                                    .selected_index       = 0,
                                                    .on_selection_changed = [&s](std::optional<std::size_t> index)
                                                    { s.state = static_cast<int>(index.value_or(0)); }},
                        .controls =
                            {
                                ui::ProgressBar{.show_paused = ui::bind(s.state, [](int v) { return v == 1; }),
                                                .show_error  = ui::bind(s.state, [](int v) { return v == 2; }),
                                                .margin      = ui::Edges{.left = 10.0f, .top = 10.0f},
                                                .width       = 130.0f},
                            },
                    }),
                    // The sample has a 60px output text between bar and label that nothing
                    // writes to; it is a gap here.
                    example(
                        {
                            .title = "A determinate progress bar.",
                            .code  = R"cpp(
                                ui::Property<double> progress{50.0};

                                ui::HorizontalStack{
                                    .child_alignment = ui::Alignment::Center,
                                    .content         = {
                                        ui::ProgressBar{.value = progress, .width = 130},
                                        ui::Text{.text = "Progress", .margin = ui::Edges{.left = 60, .right = 10}},
                                        ui::NumberBox{
                                            .value            = 50.0,
                                            .min              = 0.0,
                                            .max              = 100.0,
                                            .on_value_changed = [&](std::optional<double> value)
                                            { progress = value.value_or(0.0); },
                                        },
                                    },
                                }
                            )cpp",
                            .controls =
                                {
                                    ui::HorizontalStack{
                                        .child_alignment = ui::Alignment::Center,
                                        .content =
                                            {
                                                ui::ProgressBar{.value         = s.value,
                                                                .width         = 130.0f,
                                                                .accessibility = {.name = "Determinate progress "
                                                                                          "example"}},
                                                ui::Text{.text = "Progress", .margin = ui::Edges{.left = 60.0f, .right = 10.0f}},
                                                ui::NumberBox{.value            = 50.0,
                                                              .min              = 0.0,
                                                              .max              = 100.0,
                                                              .accessibility    = {.name = "NumberBox controlling "
                                                                                           "ProgressBar2 value"},
                                                              .on_value_changed = [&s](std::optional<double> value)
                                                              { s.value = value.value_or(0.0); }},
                                            },
                                    },
                                },
                        }),
                });
        });
}

// ---- ProgressRing ----

constexpr std::array<ui::Color, 2> RingBackgrounds{ui::colors::Transparent, ui::Color::from_argb(0xFFD3D3D3)}; // Transparent, LightGray

struct ProgressRingState
{
    ui::Property<bool>                     active{true};
    ui::Property<std::optional<ui::Color>> background1{};
    ui::Property<double>                   value{50.0};
    ui::Property<std::optional<ui::Color>> background2{};
};

ui::Spec background_picker(ui::Property<std::optional<ui::Color>>& target)
{
    return ui::ComboBox{.header               = "Background color",
                        .placeholder_text     = "Pick a color",
                        .items                = {"Transparent", "LightGray"},
                        .selected_index       = std::nullopt,
                        .width                = 200.0f,
                        .on_selection_changed = [&target](std::optional<std::size_t> index)
                        { target = std::optional<ui::Color>{RingBackgrounds[index.value_or(0)]}; }};
}

ui::Spec progress_ring_page(Context&)
{
    return with_state<ProgressRingState>(
        [](ProgressRingState& s)
        {
            return examples({
                example({
                    .title = "An indeterminate progress ring.",
                    .code  = R"cpp(
                            ui::Property<bool>                     active{true};
                            ui::Property<std::optional<ui::Color>> background{};

                            ui::ProgressRing{
                                .active     = active,
                                .background = background,
                                .width      = 60,
                                .height     = 60,
                            }

                            ui::ToggleSwitch{
                                .header             = "Progress Options",
                                .checked            = true,
                                .checked_text       = "Working",
                                .unchecked_text     = "Do work",
                                .on_checked_changed = [&](bool on) { active = on; },
                            }

                            background = ui::Color::from_argb(0xFFD3D3D3); // light gray track
                        )cpp",
                    .options = options_panel(std::nullopt,
                                             {
                                                 ui::ToggleSwitch{.header         = "Progress Options",
                                                                  .checked        = true,
                                                                  .checked_text   = "Working",
                                                                  .unchecked_text = "Do work",
                                                                  .accessibility  = {.name = "Progress Options"},
                                                                  .on_checked_changed = [&s](bool on) { s.active = on; }},
                                                 background_picker(s.background1),
                                             }),
                    .controls =
                        {
                            ui::ProgressRing{.active               = s.active,
                                             .background           = s.background1,
                                             .margin               = ui::Edges{.left = 10.0f, .top = 10.0f},
                                             .width                = 60.0f,
                                             .height               = 60.0f,
                                             .horizontal_alignment = ui::Alignment::Leading,
                                             .vertical_alignment   = ui::Alignment::Leading,
                                             .accessibility        = {.name = "Progress image"}},
                        },
                }),
                example(
                    {
                        .title   = "A determinate progress ring.",
                        .code    = R"cpp(
                                ui::Property<double>                   progress{50.0};
                                ui::Property<std::optional<ui::Color>> background{};

                                ui::HorizontalStack{
                                    .child_alignment = ui::Alignment::Center,
                                    .content         = {
                                        ui::ProgressRing{
                                            .value         = progress,
                                            .background    = background,
                                            .margin        = ui::Edges{.right = 60},
                                            .width         = 60,
                                            .height        = 60,
                                        },
                                        ui::NumberBox{
                                            .header           = "Progress",
                                            .value            = 50.0,
                                            .min              = 0.0,
                                            .max              = 100.0,
                                            .on_value_changed = [&](std::optional<double> value)
                                            { progress = value.value_or(0.0); },
                                        },
                                    },
                                }
                            )cpp",
                        .options = options_panel(std::nullopt, {background_picker(s.background2)}),
                        .controls =
                            {
                                ui::HorizontalStack{
                                    .child_alignment = ui::Alignment::Center,
                                    .content =
                                        {
                                            ui::ProgressRing{.value         = s.value,
                                                             .background    = s.background2,
                                                             .margin        = ui::Edges{.right = 60.0f},
                                                             .width         = 60.0f,
                                                             .height        = 60.0f,
                                                             .accessibility = {.name = "Progress image"}},
                                            ui::NumberBox{.header           = "Progress",
                                                          .value            = 50.0,
                                                          .min              = 0.0,
                                                          .max              = 100.0,
                                                          .min_width        = 120.0f,
                                                          .accessibility    = {.name = "Progress amount"},
                                                          .on_value_changed = [&s](std::optional<double> value)
                                                          { s.value = value.value_or(0.0); }},
                                        },
                                },
                            },
                    }),
            });
        });
}

// ---- ToolTip ----

ui::Spec tool_tip_page(Context&)
{
    return examples({
        example({
            .title = "A button with a simple ToolTip.",
            .code  = R"cpp(
                ui::ToolTip{
                    .anchor  = ui::Button{.text = "Show a simple ToolTip."},
                    .content = ui::Text{.text = "Simple ToolTip"},
                }
            )cpp",
            .controls =
                {
                    ui::ToolTip{.horizontal_alignment = ui::Alignment::Leading,
                                .anchor               = ui::Button{.text = "Show a simple ToolTip."},
                                .content              = ui::Text{.text = "Simple ToolTip"}},
                },
        }),
        example({
            .title = "A Text with an offset ToolTip.",
            .code  = R"cpp(
                ui::ToolTip{
                    .vertical_offset = -80,
                    .anchor          = ui::Text{.text = "Text with an offset ToolTip."},
                    .content         = ui::Text{.text = "Offset information."},
                }
            )cpp",
            .controls =
                {
                    ui::ToolTip{.vertical_offset      = -80.0f,
                                .horizontal_alignment = ui::Alignment::Leading,
                                .anchor               = ui::Text{.text = "Text with an offset ToolTip."},
                                .content              = ui::Text{.text = "Offset information."}},
                },
        }),
        example({
            .title = "An Image with a ToolTip using PlacementRect.",
            .code  = R"cpp(
                ui::Bitmap photo; // loaded elsewhere

                // The tip goes to the right of the rectangle, so it never covers the picture.
                ui::ToolTip{
                    .placement      = ui::PopupPlacement::Right,
                    .placement_rect = ui::Rect{0, 0, 400, 266},
                    .anchor         = ui::Image{.source  = photo,
                                                .stretch = ui::Stretch::UniformToFill,
                                                .width   = 400,
                                                .height  = 266},
                    .content        = ui::Text{.text = "Non-occluding information."},
                }
            )cpp",
            .controls =
                {
                    ui::ToolTip{.placement            = ui::PopupPlacement::Right,
                                .placement_rect       = ui::Rect{0.0f, 0.0f, 400.0f, 266.0f},
                                .horizontal_alignment = ui::Alignment::Leading,
                                .accessibility        = {.description = "Non-occluding tooltip"},
                                .anchor               = ui::Image{.source  = placeholder_photo(),
                                                                  .stretch = ui::Stretch::UniformToFill,
                                                                  .width   = 400.0f,
                                                                  .height  = 266.0f},
                                .content              = ui::Text{.text = "Non-occluding information."}},
                },
        }),
    });
}

} // namespace

std::span<const Page> status_and_info_pages()
{
    static const auto Pages = std::to_array<Page>({
        {"InfoBadge", "A non-intrusive UI to display notifications or bring focus to an area.", info_badge_page},
        {"InfoBar", "An inline message to display app-wide status change information.", info_bar_page},
        {"ProgressBar",
         "Shows the apps progress on a task, or that the app is performing ongoing work that doesn't block user "
         "interaction.",
         progress_bar_page},
        {"ProgressRing",
         "Shows the apps progress on a task, or that the app is performing ongoing work that does block user "
         "interaction.",
         progress_ring_page},
        {"ToolTip", "Displays information for an element in a pop-up window.", tool_tip_page},
    });
    return Pages;
}

} // namespace gallery
