// SPDX-License-Identifier: MIT
#include "gallery.hpp"

#include <array>
#include <memory>
#include <string>


namespace gallery
{
namespace
{
// Symbol-enum codepoints the samples name, which render/IconGlyph.hpp has no entry for.
constexpr ui::IconGlyph RefreshGlyph{0xE72C, "Segoe Fluent Icons", 20.0f}; // Symbol.Refresh, a Symbol's 20px

ui::Spec options_panel(float width, ui::SpecList children)
{
    return ui::VerticalStack{
        .spacing         = 8.0f,
        .child_alignment = ui::Alignment::Stretch,
        .width           = width,
        .content         = std::move(children),
    };
}

// Two things in one Grid cell, such as a button and the popup it opens: the popup
// (or tip) takes no room, and a Popup is placed from the cell's top-left corner.
ui::Spec overlaid(ui::Spec first, ui::Spec second)
{
    return ui::Grid{.columns              = {ui::AutoSize},
                    .rows                 = {ui::AutoSize},
                    .horizontal_alignment = ui::Alignment::Leading,
                    .vertical_alignment   = ui::Alignment::Leading,
                    .content              = {ui::Cell{.row = 0, .column = 0, .content = std::move(first)},
                                             ui::Cell{.row = 0, .column = 0, .content = std::move(second)}}};
}

// ---- Dialog ----

// The Dialog sample's content: a wrapping line of text over a check box.
ui::Spec dialog_content()
{
    return ui::VerticalStack{.child_alignment = ui::Alignment::Stretch,
                             .content         = {ui::Text{.text     = "Lorem ipsum dolor sit amet, adipisicing elit.",
                                                          .wrapping = ui::TextWrapping::Wrap},
                                                 ui::CheckBox{.text = "Upload your content to the cloud."}}};
}

struct DialogTexts
{
    const char*             title;
    const char*             primary;
    const char*             secondary;
    ui::DialogDefaultButton default_button;
    // What the output says for Primary, Secondary and Close.
    std::array<const char*, 3> results;
};

constexpr DialogTexts SaveDialog{"Save your work?",
                                 "Save",
                                 "Don't Save",
                                 ui::DialogDefaultButton::Primary,
                                 {"User saved their work", "User did not save their work", "User canceled the dialog"}};

constexpr DialogTexts ReplaceDialog{"Replace file?",
                                    "Replace",
                                    "Keep",
                                    ui::DialogDefaultButton::None,
                                    {"User replaced the file", "User kept the file", "User canceled the dialog"}};

// A fresh dialog on every click, since show_modal() takes ownership and the dialog is destroyed
// once it closes.
void show_sample_dialog(ui::Window& window, ui::Property<std::string>& output, const DialogTexts& texts)
{
    window.show_modal(ui::Dialog{
        .title          = texts.title,
        .default_button = texts.default_button,
        .primary        = {.text = texts.primary},
        .secondary      = {.text = texts.secondary},
        .close          = {.text = "Cancel"},
        .on_closed =
            [&output, &texts](ui::DialogResult result)
        {
            output = result == ui::DialogResult::Primary     ? texts.results[0]
                     : result == ui::DialogResult::Secondary ? texts.results[1]
                                                             : texts.results[2];
        },
        .content = dialog_content(),
    });
}

struct DialogState
{
    ui::Property<std::string> result;
    ui::Property<std::string> result_no_default;
    ui::Property<std::string> result_awaited;
};

// Two questions in a row, each awaited where it is asked.
ui::FireAndForget ask_before_closing(ui::Window& window, ui::Property<std::string>& output)
{
    const ui::DialogResult save = co_await ui::show_dialog(window,
                                                           ui::Dialog{
                                                               .title     = "Save your work?",
                                                               .primary   = {.text = "Save"},
                                                               .secondary = {.text = "Don't Save"},
                                                               .close     = {.text = "Cancel"},
                                                           });
    if (save == ui::DialogResult::Close)
    {
        output = "User canceled the first dialog";
        co_return;
    }
    const ui::DialogResult close = co_await ui::show_dialog(window,
                                                            ui::Dialog{
                                                                .title   = "Close the document?",
                                                                .primary = {.text = "Close"},
                                                                .close   = {.text = "Keep open"},
                                                            });
    output                       = std::string{save == ui::DialogResult::Primary ? "Saved" : "Not saved"} +
                                   (close == ui::DialogResult::Primary ? " and closed" : " and kept open");
}

ui::Spec dialog_page(Context& ctx)
{
    ui::Window& window = ctx.window;
    return with_state<DialogState>(
        [&window](DialogState& s)
        {
            return examples({
                example({
                    .title    = "A basic content dialog with content.",
                    .code     = R"cpp(
                        ui::Property<std::string> result;

                        ui::Button{
                            .text     = "Show dialog",
                            .on_click = [&]
                            {
                                // show_modal() takes ownership, so every click makes a fresh dialog.
                                window.show_modal(ui::Dialog{
                                    .title          = "Save your work?",
                                    .default_button = ui::DialogDefaultButton::Primary,
                                    .primary        = {.text = "Save"},
                                    .secondary      = {.text = "Don't Save"},
                                    .close          = {.text = "Cancel"},
                                    .on_closed      = [&](ui::DialogResult r)
                                    {
                                        result = r == ui::DialogResult::Primary     ? "User saved their work"
                                                 : r == ui::DialogResult::Secondary ? "User did not save their work"
                                                                                    : "User canceled the dialog";
                                    },
                                    .content        = ui::VerticalStack{
                                        .child_alignment = ui::Alignment::Stretch,
                                        .content         = {
                                            ui::Text{.text     = "Lorem ipsum dolor sit amet, adipisicing elit.",
                                                     .wrapping = ui::TextWrapping::Wrap},
                                            ui::CheckBox{.text = "Upload your content to the cloud."},
                                        },
                                    },
                                });
                            },
                        }
                    )cpp",
                    .output   = ui::Text{.text = s.result},
                    .controls = {ui::Button{.text     = "Show dialog",
                                            .on_click = [&window, &s]
                                            { show_sample_dialog(window, s.result, SaveDialog); }}},
                }),
                example({
                    .title    = "A content dialog without a default button.",
                    .code     = R"cpp(
                        ui::Property<std::string> result;

                        ui::Button{
                            .text     = "Show dialog without a default action",
                            .on_click = [&]
                            {
                                window.show_modal(ui::Dialog{
                                    .title          = "Replace file?",
                                    .default_button = ui::DialogDefaultButton::None,
                                    .primary        = {.text = "Replace"},
                                    .secondary      = {.text = "Keep"},
                                    .close          = {.text = "Cancel"},
                                    .on_closed      = [&](ui::DialogResult r)
                                    {
                                        result = r == ui::DialogResult::Primary     ? "User replaced the file"
                                                 : r == ui::DialogResult::Secondary ? "User kept the file"
                                                                                    : "User canceled the dialog";
                                    },
                                    .content        = ui::VerticalStack{
                                        .child_alignment = ui::Alignment::Stretch,
                                        .content         = {
                                            ui::Text{.text     = "Lorem ipsum dolor sit amet, adipisicing elit.",
                                                     .wrapping = ui::TextWrapping::Wrap},
                                            ui::CheckBox{.text = "Upload your content to the cloud."},
                                        },
                                    },
                                });
                            },
                        }
                    )cpp",
                    .output   = ui::Text{.text = s.result_no_default},
                    .controls = {ui::Button{.text     = "Show dialog without a default action",
                                            .on_click = [&window, &s]
                                            { show_sample_dialog(window, s.result_no_default, ReplaceDialog); }}},
                }),
                example({
                    .title  = "Waiting for the answer in a coroutine.",
                    .code   = R"cpp(
                        ui::Property<std::string> result;

                        ui::FireAndForget ask_before_closing(ui::Window& window, ui::Property<std::string>& result)
                        {
                            const ui::DialogResult save = co_await ui::show_dialog(window, ui::Dialog{
                                .title          = "Save your work?",
                                .primary        = {.text = "Save"},
                                .secondary      = {.text = "Don't Save"},
                                .close          = {.text = "Cancel"},
                            });
                            if (save == ui::DialogResult::Close)
                            {
                                result = "User canceled the first dialog";
                                co_return;
                            }
                            const ui::DialogResult close = co_await ui::show_dialog(window, ui::Dialog{
                                .title          = "Close the document?",
                                .primary        = {.text = "Close"},
                                .close          = {.text = "Keep open"},
                            });
                            result = std::string{save == ui::DialogResult::Primary ? "Saved" : "Not saved"} +
                                     (close == ui::DialogResult::Primary ? " and closed" : " and kept open");
                        }

                        ui::Button{.text = "Ask two questions", .on_click = [&] { ask_before_closing(window, result); }}
                    )cpp",
                    .output = ui::Text{.text = s.result_awaited},
                    .controls = {ui::Button{.text = "Ask two questions",
                                            .on_click = [&window, &s] { ask_before_closing(window, s.result_awaited); }}},
                }),
            });
        });
}

// ---- Flyout ----

struct FlyoutState
{
    ui::Control<ui::Flyout>   flyout;
    ui::Control<ui::Flyout>   sticky;
    ui::Property<std::string> closed{"Not closed yet"};
};

// The text a flyout's on_closed reports for each reason.
std::string close_reason_text(ui::PopupCloseReason reason)
{
    switch (reason)
    {
        case ui::PopupCloseReason::Programmatic:
            break;
        case ui::PopupCloseReason::LightDismiss:
            return "Closed by a click outside or Escape";
        case ui::PopupCloseReason::Invoked:
            return "Closed by a command";
        case ui::PopupCloseReason::CloseButton:
            return "Closed by its close button";
    }
    return "Closed from code";
}

ui::Spec flyout_page(Context&)
{
    return with_state<FlyoutState>(
        [](FlyoutState& s)
        {
            return examples({
                example({
                    .title = "A button with a flyout",
                    .code  = R"cpp(
                        ui::Control<ui::Flyout> flyout;

                        // The anchor button opens the flyout by itself.
                        ui::Flyout{
                            .control = flyout,
                            .anchor  = ui::Button{.text = "Empty cart"},
                            .content = ui::VerticalStack{
                                .child_alignment = ui::Alignment::Stretch,
                                .content         = {
                                    ui::Text{.text   = "All items will be removed. Do you want to continue?",
                                             .role   = ui::TextRole::BodyStrong,
                                             .margin = ui::Edges{.bottom = 12}},
                                    ui::Button{.text                 = "Yes, empty my cart",
                                               .horizontal_alignment = ui::Alignment::Leading,
                                               .on_click             = [&] { flyout->hide(); }},
                                },
                            },
                        }
                    )cpp",
                    .controls =
                        {
                            // Button.Flyout: the Button anchor opens it by itself.
                            ui::Flyout{
                                .control     = s.flyout,
                                .anchor = ui::Button{.text = "Empty cart"},
                                .content =
                                    ui::VerticalStack{
                                        .child_alignment = ui::Alignment::Stretch,
                                        .content =
                                            {
                                                ui::Text{.text = "All items will be removed. Do you want to continue?",
                                                         .role   = ui::TextRole::BodyStrong,
                                                         .margin = ui::Edges{.bottom = 12.0f}},
                                                ui::Button{.text    = "Yes, empty my cart",
                                                           .horizontal_alignment = ui::Alignment::Leading,
                                                           .on_click =
                                                               [&s]
                                                           {
                                                               if (s.flyout != nullptr)
                                                               {
                                                                   s.flyout->hide();
                                                               }
                                                           }},
                                            },
                                    },
                            },
                        },
                }),
                example({
                    .title = "A flyout that stays open until it is closed",
                    .code  = R"cpp(
                        ui::Control<ui::Flyout>   flyout;
                        ui::Property<std::string> closed{"Not closed yet"};

                        ui::Flyout{
                            .light_dismiss = false,
                            .on_closed     = [&](ui::PopupCloseReason reason) { closed = close_reason_text(reason); },
                            .control       = flyout,
                            .anchor        = ui::Button{.text = "Show options"},
                            .content       = ui::VerticalStack{
                                .spacing = 12,
                                .content = {
                                    ui::Text{.text = "Clicks outside and Escape leave this open."},
                                    ui::Button{.text = "Done", .on_click = [&] { flyout->hide(); }},
                                },
                            },
                        }
                    )cpp",
                    .output = ui::Text{.text = s.closed},
                    .controls =
                        {
                            ui::Flyout{
                                .light_dismiss = false,
                                .on_closed     = [&s](ui::PopupCloseReason reason) { s.closed = close_reason_text(reason); },
                                .control       = s.sticky,
                                .anchor        = ui::Button{.text = "Show options"},
                                .content       = ui::VerticalStack{
                                    .spacing = 12.0f,
                                    .content =
                                        {
                                            ui::Text{.text = "Clicks outside and Escape leave this open."},
                                            ui::Button{.text     = "Done",
                                                       .on_click = [&s]
                                                       {
                                                           if (s.sticky != nullptr)
                                                           {
                                                               s.sticky->hide();
                                                           }
                                                       }},
                                        },
                                },
                            },
                        },
                }),
            });
        });
}

// ---- Popup ----

struct PopupState
{
    ui::Property<bool>  open{false};
    ui::Property<bool>  light_dismiss{true};
    ui::Property<float> vertical{0.0f};
    ui::Property<float> horizontal{200.0f};
};

ui::Spec popup_page(Context&)
{
    return with_state<PopupState>(
        [](PopupState& s)
        {
            return examples({
                example({
                    .title = "Popup with Offset Positioning",
                    .code  = R"cpp(
                        ui::Property<bool>  open{false};
                        ui::Property<bool>  light_dismiss{true};
                        ui::Property<float> horizontal{200.0f};
                        ui::Property<float> vertical{0.0f};

                        // Button and popup share one Grid cell: the popup takes no room and is
                        // placed from the cell's top-left corner.
                        ui::Grid{
                            .columns = {ui::AutoSize},
                            .rows    = {ui::AutoSize},
                            .content   = {
                                ui::Cell{.row     = 0,
                                 .column  = 0,
                                 .content = ui::Button{.text     = "Show Popup (using Offset)",
                                                       .on_click = [&] { open = true; }}},
                                ui::Cell{.row     = 0,
                                 .column  = 0,
                                 .content = ui::Popup{
                                     .is_open           = open,
                                     .horizontal_offset = horizontal,
                                     .vertical_offset   = vertical,
                                     .light_dismiss     = light_dismiss,
                                     .on_closed         = [&] { open = false; },
                                     // The page wraps this stack in a popup-colored card.
                                     .content           = ui::VerticalStack{
                                         .spacing = 8,
                                         .content = {
                                             ui::Text{.text = "Simple Popup"},
                                             ui::Button{.text = "Close", .on_click = [&] { open = false; }},
                                         },
                                     },
                                 }},
                            },
                        }
                    )cpp",
                    // The toggle is disabled while the popup is open, as the sample's handlers do.
                    .options = options_panel(
                        200.0f,
                        {
                            ui::ToggleSwitch{.header             = "Light dismiss",
                                             .checked            = true,
                                             .checked_text       = "True",
                                             .unchecked_text     = "False",
                                             .enabled            = ui::bind(s.open, [](bool open) { return !open; }),
                                             .on_checked_changed = [&s](bool on) { s.light_dismiss = on; }},
                            ui::NumberBox{.header                = "Vertical offset",
                                          .value                 = 0.0,
                                          .min                   = -100.0,
                                          .max                   = 100.0,
                                          .step                  = 10.0,
                                          .large_step            = 100.0,
                                          .spin_button_placement = ui::NumberBoxSpinButtonPlacementMode::Inline,
                                          .on_value_changed = [&s](std::optional<double> v) { s.vertical = static_cast<float>(v.value_or(0.0)); }},
                            ui::NumberBox{.header                = "Horizontal offset",
                                          .value                 = 200.0,
                                          .min                   = -100.0,
                                          .max                   = 500.0,
                                          .step                  = 10.0,
                                          .large_step            = 100.0,
                                          .spin_button_placement = ui::NumberBoxSpinButtonPlacementMode::Inline,
                                          .on_value_changed = [&s](std::optional<double> v) { s.horizontal = static_cast<float>(v.value_or(0.0)); }},
                        }),
                    .controls =
                        {
                            overlaid(ui::Button{.text = "Show Popup (using Offset)", .on_click = [&s] { s.open = true; }},
                                     ui::Popup{
                                         .is_open           = s.open,
                                         .horizontal_offset = s.horizontal,
                                         .vertical_offset   = s.vertical,
                                         .light_dismiss     = s.light_dismiss,
                                         .on_closed         = [&s] { s.open = false; },
                                         .content           = overlay_card(
                                             ui::VerticalStack{
                                                 .spacing         = 8.0f,
                                                 .child_alignment = ui::Alignment::Stretch,
                                                 .min_width       = 240.0f - 32.0f,
                                                 .content =
                                                     {
                                                         ui::Text{.text = "Simple Popup",
                                                                  .text_style = ui::TextStyle{.font_size = 16.0f,
                                                                                              .horizontal = ui::TextAlign::Leading}},
                                                         ui::Button{.text                 = "Close",
                                                                    .horizontal_alignment = ui::Alignment::Leading,
                                                                    .on_click             = [&s] { s.open = false; }},
                                                     },
                                             },
                                             ui::Thickness{16.0f}),
                                     }),
                        },
                }),
            });
        });
}

// ---- TeachingTip ----

// The targets are wired when the buttons are clicked: a tip's target is a pointer to another
// element, which exists only once the page has been built.
struct TeachingTipState
{
    ui::Control<ui::Button>      button1;
    ui::Control<ui::Button>      button3;
    ui::Control<ui::TeachingTip> tip1;
    ui::Control<ui::TeachingTip> tip2;
    ui::Control<ui::TeachingTip> tip3;
};

void open_tip(ui::TeachingTipControl* tip, ui::Element* target)
{
    if (tip != nullptr)
    {
        tip->set_target(target);
        tip->set_open(true);
    }
}

ui::Spec teaching_tip_page(Context&)
{
    return with_state<TeachingTipState>(
        [](TeachingTipState& s)
        {
            return examples({
                example({
                    .title    = "Show a targeted TeachingTip on a button.",
                    .code     = R"cpp(
                        ui::Control<ui::Button>      button;
                        ui::Control<ui::TeachingTip> tip;

                        // The tip takes no room; put it in the same Grid cell as its button.
                        ui::Button{
                            .text     = "Show TeachingTip",
                            .on_click = [&]
                            {
                                // The target is set on click, once the button exists.
                                tip->set_target(button);
                                tip->set_open(true);
                            },
                            .control = button,
                        }

                        ui::TeachingTip{
                            .title    = "This is the title",
                            .subtitle = "And this is the subtitle",
                            .icon     = ui::IconGlyph{0xE72C, "Segoe Fluent Icons", 20.0f}, // Refresh
                            .control  = tip,
                        }
                    )cpp",
                    .controls = {overlaid(ui::Button{.text     = "Show TeachingTip",
                                                     .on_click = [&s] { open_tip(s.tip1, s.button1); },
                                                     .control  = s.button1},
                                          ui::TeachingTip{.title    = "This is the title",
                                                          .subtitle = "And this is the subtitle",
                                                          .icon     = RefreshGlyph,
                                                          .control  = s.tip1})},
                }),
                example({
                    .title    = "Show a non-targeted TeachingTip with buttons.",
                    .code     = R"cpp(
                        ui::Control<ui::TeachingTip> tip;

                        ui::Button{
                            .text     = "Show TeachingTip",
                            .on_click = [&]
                            {
                                // No target element.
                                tip->set_target(nullptr);
                                tip->set_open(true);
                            },
                        }

                        ui::TeachingTip{
                            .title                 = "This is the title",
                            .subtitle              = "And this is the subtitle",
                            .action_button_text = "Action button",
                            .close_button_text  = "Close button",
                            .light_dismiss         = true,
                            .placement_margin      = ui::Thickness{20},
                            .control               = tip,
                        }
                    )cpp",
                    .controls = {overlaid(ui::Button{.text     = "Show TeachingTip",
                                                     .on_click = [&s] { open_tip(s.tip2, nullptr); }},
                                          ui::TeachingTip{.title              = "This is the title",
                                                          .subtitle           = "And this is the subtitle",
                                                          .action_button_text = "Action button",
                                                          .close_button_text  = "Close button",
                                                          .light_dismiss      = true,
                                                          .placement_margin   = ui::Thickness{20.0f},
                                                          .control            = s.tip2})},
                }),
                // The sample's sunset is a photo; the gallery's generated stand-in fills the
                // same width at a 16:9-ish height.
                example({
                    .title = "Show a targeted TeachingTip with hero content on a button.",
                    .code  = R"cpp(
                        ui::Control<ui::Button>      button;
                        ui::Control<ui::TeachingTip> tip;
                        ui::Bitmap                   photo; // loaded elsewhere

                        ui::Button{
                            .text     = "Show TeachingTip",
                            .on_click = [&]
                            {
                                tip->set_target(button);
                                tip->set_open(true);
                            },
                            .control = button,
                        }

                        ui::TeachingTip{
                            .title               = "This is the title",
                            .subtitle            = "And this is the subtitle",
                            .placement           = ui::PopupPlacement::Below,
                            .control             = tip,
                            .content             = ui::Text{.text     = "Description can go here",
                                                            .wrapping = ui::TextWrapping::WrapWholeWords,
                                                            .margin   = ui::Edges{.top = 16}},
                            .hero_content        = ui::Image{.source  = photo,
                                                             .stretch = ui::Stretch::UniformToFill,
                                                             .height  = 180},
                        }
                    )cpp",
                    .controls = {overlaid(ui::Button{.text     = "Show TeachingTip",
                                                     .on_click = [&s] { open_tip(s.tip3, s.button3); },
                                                     .control  = s.button3},
                                          ui::TeachingTip{.title     = "This is the title",
                                                          .subtitle  = "And this is the subtitle",
                                                          .placement = ui::PopupPlacement::Below,
                                                          .control   = s.tip3,
                                                          .content = ui::Text{.text = "Description can go here",
                                                                              .wrapping = ui::TextWrapping::WrapWholeWords,
                                                                              .margin = ui::Edges{.top = 16.0f}},
                                                          .hero_content = ui::Image{.source = placeholder_photo(),
                                                                                    .stretch = ui::Stretch::UniformToFill,
                                                                                    .height        = 180.0f,
                                                                                    .accessibility = {.name = "Sun"
                                                                                                              "se"
                                                                                                              "t"}}})},
                }),
            });
        });
}

} // namespace

std::span<const Page> dialogs_and_flyouts_pages()
{
    static const auto Pages = std::to_array<Page>({
        {"Dialog", "A dialog box that can be customized to contain any content.", dialog_page},
        {"Flyout", "Shows contextual information and enables user interaction.", flyout_page},
        {"Popup", "A UI element displaying temporary content over existing interface.", popup_page},
        {"TeachingTip", "A content-rich flyout for guiding users and enabling teaching moments.", teaching_tip_page},
    });
    return Pages;
}

} // namespace gallery
