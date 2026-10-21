// SPDX-License-Identifier: MIT
#include "gallery.hpp"

#include <array>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>
#include <cwchar>


namespace gallery
{
namespace
{
ui::Spec output_text(ui::Property<std::string>& text)
{
    return ui::Text{.text = text, .horizontal_alignment = ui::Alignment::Leading};
}
ui::Spec output_text(const ui::Computed<std::string>& text)
{
    return ui::Text{.text = text, .horizontal_alignment = ui::Alignment::Leading};
}

std::string whole(double value)
{
    return std::format("{}", static_cast<int>(std::lround(value)));
}

// ---- Button ----

constexpr const char* ClippingWarning =
    "The following buttons' content may get clipped if we don't pay careful attention to their layout containers.";
constexpr const char* StackingHint =
    "One option to mitigate clipped content is to place Buttons underneath each "
    "other, allowing for more space to grow horizontally:";
constexpr const char* LongText1    = "This is some text that is too long and will get cut off";
constexpr const char* LongText2    = "This is another text that would result in being cut off";
constexpr const char* WrappedText1 = "This is some text that is too long and will get cut off without wrapping";
constexpr const char* WrappedText2 = "This is another text that would result in being cut off without wrapping";

ui::Spec wrapped_button(const char* text)
{
    return ui::Button{.max_width = 240.0f, .content = ui::Text{.text = text, .wrapping = ui::TextWrapping::WrapWholeWords}};
}

struct ButtonState
{
    ui::Property<bool>        disabled{false};
    ui::Property<std::string> simple_output{};
    ui::Property<std::string> image_output{};
};

ui::Spec button_page(Context&)
{
    return with_state<ButtonState>(
        [](ButtonState& s)
        {
            return examples({
                example({
                    .title   = "A simple Button with text content.",
                    .code    = R"cpp(
                        ui::Property<bool>        disabled{false};
                        ui::Property<std::string> output{};

                        ui::Button{
                            .text     = "Standard button",
                            .enabled  = ui::bind(disabled, [](bool off) { return !off; }),
                            .on_click = [&] { output = "You clicked: Button1"; },
                        }
                    )cpp",
                    .output  = output_text(s.simple_output),
                    .options = ui::CheckBox{.text               = "Disable button",
                                            .on_checked_changed = [&s](std::optional<bool> on)
                                            { s.disabled = on == true; }},
                    .controls =
                        {
                            ui::Button{.text     = "Standard button",
                                       .enabled  = ui::bind(s.disabled, [](bool off) { return !off; }),
                                       .on_click = [&s] { s.simple_output = "You clicked: Button1"; }},
                        },
                }),
                example({
                    .title  = "A Button with graphical content.",
                    .code   = R"cpp(
                        ui::Property<std::string> output{};
                        ui::Bitmap                photo = ...; // any loaded image

                        ui::Button{
                            .width         = 50.0f,
                            .height        = 50.0f,
                            .accessibility = {.name = "Pie"},
                            .on_click      = [&] { output = "You clicked: Button2"; },
                            .content       = ui::Image{.source = photo},
                        }
                    )cpp",
                    .output = output_text(s.image_output),
                    .controls =
                        {
                            ui::Button{.width         = 50.0f,
                                       .height        = 50.0f,
                                       .accessibility = {.name = "Pie"},
                                       .on_click      = [&s] { s.image_output = "You clicked: Button2"; },
                                       .content       = ui::Image{.source = placeholder_photo()}},
                        },
                }),
                example({
                    .title = "Built-in styles applied to Button.",
                    .code  = R"cpp(
                        ui::HorizontalStack{
                            .spacing = 8,
                            .content = {
                                ui::Button{.text = "Accent style button", .variant = ui::ButtonVariant::Accent},
                                ui::Button{.text = "Subtle style button", .variant = ui::ButtonVariant::Subtle},
                            },
                        }
                    )cpp",
                    .controls =
                        {
                            ui::Button{.text = "Accent style button", .variant = ui::ButtonVariant::Accent},
                            ui::Button{.text = "Subtle style button", .variant = ui::ButtonVariant::Subtle},
                        },
                }),
                example({
                    .title   = "Wrapping Buttons with large content",
                    .code    = R"cpp(
                        // A plain label stays on one line and is cut off where the room ends.
                        ui::Button{.text = "This is some text that is too long and will get cut off"}

                        // A wrapping Text as the content, and a width for it to wrap at.
                        ui::Button{
                            .max_width = 240.0f,
                            .content   = ui::Text{
                                .text     = "This is some text that is too long and will get cut off without wrapping",
                                .wrapping = ui::TextWrapping::WrapWholeWords,
                            },
                        }
                    )cpp",
                    .stacked = true,
                    .controls =
                        {
                            ui::Text{.text = ClippingWarning, .wrapping = ui::TextWrapping::Wrap},
                            ui::Text{.text = StackingHint, .wrapping = ui::TextWrapping::Wrap},
                            ui::Button{.text = LongText1},
                            ui::Button{.text = LongText2},
                            ui::Text{.text = "Another option is to explicitly wrap the Button's content"},
                            ui::HorizontalStack{
                                .spacing              = 8.0f,
                                .horizontal_alignment = ui::Alignment::Leading,
                                .content              = {wrapped_button(WrappedText1), wrapped_button(WrappedText2)},
                            },
                        },
                }),
            });
        });
}

// ---- ToggleButton ----

struct ToggleButtonState
{
    ui::Property<bool>        disabled{false};
    ui::Property<std::string> output{"Off"}; // the button starts unchecked
};

ui::Spec toggle_button_page(Context&)
{
    return with_state<ToggleButtonState>(
        [](ToggleButtonState& s)
        {
            return examples({
                example({
                    .title   = "A simple ToggleButton with text content.",
                    .code    = R"cpp(
                        ui::Property<bool>        disabled{false};
                        ui::Property<std::string> output{"Off"};

                        ui::ToggleButton{
                            .text               = "ToggleButton",
                            .enabled            = ui::bind(disabled, [](bool off) { return !off; }),
                            .on_checked_changed = [&](std::optional<bool> on) { output = on == true ? "On" : "Off"; },
                        }
                    )cpp",
                    .output  = output_text(s.output),
                    .options = ui::CheckBox{.text               = "Disable ToggleButton",
                                            .on_checked_changed = [&s](std::optional<bool> on)
                                            { s.disabled = on == true; }},
                    .controls =
                        {
                            ui::ToggleButton{.text               = "ToggleButton",
                                             .enabled            = ui::bind(s.disabled, [](bool off) { return !off; }),
                                             .on_checked_changed = [&s](std::optional<bool> on)
                                             { s.output = on == true ? "On" : "Off"; }},
                        },
                }),
            });
        });
}

// ---- CheckBox ----

// "Select all" is derived from the three options: all on, all off, or mixed.
std::optional<bool> all_of(bool a, bool b, bool c)
{
    if (a && b && c)
    {
        return true;
    }
    if (!a && !b && !c)
    {
        return false;
    }
    return std::nullopt;
}

std::string sync_text(const std::optional<bool>& photos, bool fallback)
{
    const bool on = photos.value_or(fallback);
    return std::string{on ? "Photos sync" : "Photos do not sync"} + (photos ? "." : ", as the default says.");
}

struct CheckBoxState
{
    ui::Property<std::string> two_state_output{};
    ui::Property<std::string> three_state_output{};
    ui::Property<bool>        disabled{false};

    ui::Property<bool>                option1{false};
    ui::Property<bool>                option2{true};
    ui::Property<bool>                option3{false};
    ui::Computed<std::optional<bool>> all{all_of, option1, option2, option3};

    ui::Property<bool>                sync_by_default{true};
    ui::Property<std::optional<bool>> sync_photos{std::nullopt}; // nullopt: follow the default
    ui::Computed<std::string>         sync_summary{sync_text, sync_photos, sync_by_default};
};

ui::Spec option_box(const char* text, ui::Property<bool>& option)
{
    return ui::CheckBox{.text = text, .checked = option, .margin = ui::Edges{.left = 24.0f}};
}

ui::Spec check_box_page(Context&)
{
    return with_state<CheckBoxState>(
        [](CheckBoxState& s)
        {
            return examples({
                example({
                    .title  = "A 2-state CheckBox.",
                    .code   = R"cpp(
                        ui::Property<std::string> output{};

                        ui::CheckBox{
                            .text               = "Two-state CheckBox",
                            .on_checked_changed = [&](std::optional<bool> on)
                            { output = on == true ? "You checked the box." : "You unchecked the box."; },
                        }
                    )cpp",
                    .output = output_text(s.two_state_output),
                    .controls =
                        {
                            ui::CheckBox{.text = "Two-state CheckBox",
                                         .on_checked_changed =
                                             [&s](std::optional<bool> on)
                                         {
                                             s.two_state_output = on == true ? "You checked the box."
                                                                             : "You unchecked the box.";
                                         }},
                        },
                }),
                example({
                    .title   = "A 3-state CheckBox.",
                    .code    = R"cpp(
                        ui::Property<bool>        disabled{false};
                        ui::Property<std::string> output{};

                        ui::CheckBox{
                            .text               = "Three-state CheckBox",
                            .three_state        = true,
                            .enabled            = ui::bind(disabled, [](bool off) { return !off; }),
                            .on_checked_changed = [&](std::optional<bool> on)
                            {
                                // An empty optional is the indeterminate state.
                                output = !on   ? "CheckBox state is indeterminate."
                                         : *on ? "CheckBox is checked."
                                               : "CheckBox is unchecked.";
                            },
                        }
                    )cpp",
                    .output  = output_text(s.three_state_output),
                    .options = ui::CheckBox{.text               = "Disable CheckBox",
                                            .on_checked_changed = [&s](std::optional<bool> on)
                                            { s.disabled = on == true; }},
                    .controls =
                        {
                            ui::CheckBox{.text        = "Three-state CheckBox",
                                         .three_state = true,
                                         .enabled     = ui::bind(s.disabled, [](bool off) { return !off; }),
                                         .on_checked_changed =
                                             [&s](std::optional<bool> on)
                                         {
                                             s.three_state_output = !on   ? "CheckBox state is indeterminate."
                                                                    : *on ? "CheckBox is checked."
                                                                          : "CheckBox is unchecked.";
                                         }},
                        },
                }),
                example({
                    .title   = "Using a CheckBox to select several options.",
                    .code    = R"cpp(
                        ui::Property<bool> option1{false}, option2{true}, option3{false};

                        // "Select all" is derived from the options: all on, all off, or mixed.
                        ui::Computed all{[](bool a, bool b, bool c) -> std::optional<bool> {
                            if (a && b && c) return true;
                            if (!a && !b && !c) return false;
                            return std::nullopt;
                        }, option1, option2, option3};

                        ui::VerticalStack{
                            .content = {
                                // A click on a mixed box checks it, so it selects every option.
                                ui::CheckBox{
                                    .text               = "Select all",
                                    .checked            = all,
                                    .on_checked_changed = [&](std::optional<bool> on)
                                    {
                                        option1 = on == true;
                                        option2 = on == true;
                                        option3 = on == true;
                                    },
                                },
                                ui::CheckBox{.text = "Option 1", .checked = option1, .margin = ui::Edges{.left = 24}},
                                ui::CheckBox{.text = "Option 2", .checked = option2, .margin = ui::Edges{.left = 24}},
                                ui::CheckBox{.text = "Option 3", .checked = option3, .margin = ui::Edges{.left = 24}},
                            },
                        }
                    )cpp",
                    .stacked = true,
                    .controls =
                        {
                            ui::CheckBox{.text    = "Select all",
                                         .checked = s.all,
                                         .on_checked_changed =
                                             [&s](std::optional<bool> on)
                                         {
                                             s.option1 = on == true;
                                             s.option2 = on == true;
                                             s.option3 = on == true;
                                         }},
                            option_box("Option 1", s.option1),
                            option_box("Option 2", s.option2),
                            option_box("Option 3", s.option3),
                        },
                }),
                example({
                    .title   = "A setting that can follow a default.",
                    .code    = R"cpp(
                        ui::Property<bool>                sync_by_default{true};
                        ui::Property<std::optional<bool>> sync_photos{std::nullopt}; // nullopt: follow the default

                        ui::VerticalStack{
                            .content = {
                                ui::CheckBox{.text = "Sync by default", .checked = sync_by_default},
                                // A click goes off, on, then back to following the default.
                                ui::CheckBox{.text = "Sync Photos", .checked = sync_photos, .three_state = true},
                                ui::Text{.text = ui::bind(
                                             [](const std::optional<bool>& photos, bool fallback)
                                             {
                                                 const bool on = photos.value_or(fallback);
                                                 return std::string{on ? "Photos sync" : "Photos do not sync"} +
                                                        (photos ? "." : ", as the default says.");
                                             },
                                             sync_photos, sync_by_default)},
                            },
                        }
                    )cpp",
                    .stacked = true,
                    .output  = output_text(s.sync_summary),
                    .controls =
                        {
                            ui::CheckBox{.text = "Sync by default", .checked = s.sync_by_default},
                            ui::CheckBox{.text = "Sync Photos", .checked = s.sync_photos, .three_state = true},
                        },
                }),
            });
        });
}

// ---- ColorPicker ----

struct ColorPickerState
{
    ui::Property<ui::Color> color{ui::Color::from_rgba(0, 120, 215)};
    ui::Property<bool>      more_button{false};
    ui::Property<bool>      color_slider{true};
    ui::Property<bool>      channel_input{true};
    ui::Property<bool>      hex_input{true};
    ui::Property<bool>      alpha{false};
    ui::Property<bool>      alpha_slider{true};
    ui::Property<bool>      alpha_input{true};
};

// One of the card's `*_visible` check boxes. `enabled_by` grays it while another option is off,
// as the two alpha visibility boxes follow the Alpha box.
ui::Spec flag_box(const char* text, ui::Property<bool>& flag, ui::Property<bool>* enabled_by = nullptr)
{
    return ui::CheckBox{.text               = text,
                        .checked            = std::optional<bool>{flag.get()},
                        .enabled            = enabled_by ? ui::bind(*enabled_by) : ui::Binding<bool>{true},
                        .on_checked_changed = [&flag](std::optional<bool> on) { flag = on == true; }};
}

// A "Colorspectrum shape" Box/Ring choice is not among the options: there is no
// spectrum to shape (the hue and saturation sliders stand in for it, ColorPicker.hpp).
ui::Spec color_picker_page(Context&)
{
    return with_state<ColorPickerState>(
        [](ColorPickerState& s)
        {
            return examples({
                example({
                    .title = "ColorPicker Properties.",
                    .code  = R"cpp(
                                ui::Property<ui::Color> color{ui::Color::from_rgba(0, 120, 215)};
                                ui::Property<bool>      more_button{false};
                                ui::Property<bool>      color_slider{true};
                                ui::Property<bool>      channel_input{true};
                                ui::Property<bool>      hex_input{true};
                                ui::Property<bool>      alpha{false};
                                ui::Property<bool>      alpha_slider{true};
                                ui::Property<bool>      alpha_input{true};

                                ui::ColorPicker{
                                    .color                            = color,
                                    .alpha_enabled                    = alpha,
                                    .color_slider_visible             = color_slider,
                                    .alpha_slider_visible             = alpha_slider,
                                    .more_button_visible              = more_button,
                                    .color_channel_text_input_visible = channel_input,
                                    .alpha_text_input_visible         = alpha_input,
                                    .hex_input_visible                = hex_input,
                                    .horizontal_alignment             = ui::Alignment::Leading,
                                    .on_color_changed                 = [&](ui::Color c) { color = c; },
                                }

                                // Each option is a check box that writes one of the flags.
                                ui::CheckBox{
                                    .text               = "More button",
                                    .on_checked_changed = [&](std::optional<bool> on) { more_button = on == true; },
                                }
                            )cpp",
                    .options =
                        ui::VerticalStack{
                            .spacing         = 8.0f,
                            .child_alignment = ui::Alignment::Stretch,
                            .width           = 250.0f,
                            .content =
                                {
                                    flag_box("More button", s.more_button),
                                    flag_box("Color slider", s.color_slider),
                                    flag_box("Color channel inputs", s.channel_input),
                                    flag_box("Hex input", s.hex_input),
                                    flag_box("Alpha", s.alpha),
                                    flag_box("Alpha slider", s.alpha_slider, &s.alpha),
                                    flag_box("Alpha input", s.alpha_input, &s.alpha),
                                    ui::Text{.text                 = "ColorPicker applied on a Rectangle",
                                             .margin               = ui::Edges{.top = 12.0f},
                                             .horizontal_alignment = ui::Alignment::Leading},
                                    Swatch{.fill             = s.color,
                                           .border           = ui::Color::from_rgba(128, 128, 128),
                                           .border_thickness = 1.0f,
                                           .height           = 100.0f},
                                },
                        },
                    .controls =
                        {
                            ui::ColorPicker{.color                            = s.color,
                                            .alpha_enabled                    = s.alpha,
                                            .color_slider_visible             = s.color_slider,
                                            .alpha_slider_visible             = s.alpha_slider,
                                            .more_button_visible              = s.more_button,
                                            .color_channel_text_input_visible = s.channel_input,
                                            .alpha_text_input_visible         = s.alpha_input,
                                            .hex_input_visible                = s.hex_input,
                                            .horizontal_alignment             = ui::Alignment::Leading,
                                            .on_color_changed                 = [&s](ui::Color c) { s.color = c; }},
                        },
                }),
            });
        });
}

// ---- ComboBox ----

struct NamedColor
{
    const char* name;
    ui::Color   color;
};

constexpr std::array<NamedColor, 4> ComboColors{{
    {"Blue", ui::Color::from_rgba(0, 0, 255)},
    {"Green", ui::Color::from_rgba(0, 128, 0)},
    {"Red", ui::Color::from_rgba(255, 0, 0)},
    {"Yellow", ui::Color::from_rgba(255, 255, 0)},
}};

constexpr std::array<const char*, 5> Fonts{"Arial", "Comic Sans MS", "Courier New", "Segoe UI", "Times New Roman"};

constexpr std::array<int, 14> FontSizes{8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 36, 48, 72};

ui::TextStyle sample_style(const char* family, float size)
{
    return ui::TextStyle{.font_size = size, .horizontal = ui::TextAlign::Leading, .font_family = family};
}

struct ComboBoxState
{
    ui::Property<ui::Color>   picked{ui::colors::Transparent};
    ui::Control<ui::Text>     font_sample;
    ui::Control<ui::Text>     size_sample;
    ui::Control<ui::ComboBox> size_box;
    float                     size = static_cast<float>(FontSizes[2]);
    ui::Property<std::string> size_error{};

    void apply_size(float value)
    {
        size = value;
        size_sample->set_text_style(sample_style("Segoe UI", value));
        size_error = "";
    }
};

ui::Spec combo_box_page(Context&)
{
    return with_state<ComboBoxState>(
        [](ComboBoxState& s)
        {
            std::vector<std::string> color_names;
            color_names.reserve(ComboColors.size());
            for (const NamedColor& c : ComboColors)
            {
                color_names.emplace_back(c.name);
            }
            std::vector<std::string> sizes;
            sizes.reserve(FontSizes.size());
            for (int size : FontSizes)
            {
                sizes.push_back(std::to_string(size));
            }

            return examples(
                {
                    example({
                        .title   = "A ComboBox with items defined inline and its width set.",
                        .code    = R"cpp(
                            const std::array<ui::Color, 4> colors{
                                ui::Color::from_rgba(0, 0, 255),
                                ui::Color::from_rgba(0, 128, 0),
                                ui::Color::from_rgba(255, 0, 0),
                                ui::Color::from_rgba(255, 255, 0),
                            };
                            ui::Property<ui::Color> picked{ui::colors::Transparent};

                            ui::ComboBox{
                                .header               = "Colors",
                                .placeholder_text     = "Pick a color",
                                .items                = {"Blue", "Green", "Red", "Yellow"},
                                .width                = 200.0f,
                                .horizontal_alignment = ui::Alignment::Leading,
                                .on_selection_changed = [&](std::optional<std::size_t> index)
                                { picked = colors[index.value_or(0)]; },
                            }
                        )cpp",
                        .stacked = true,
                        .controls =
                            {
                                ui::ComboBox{.header               = "Colors",
                                             .placeholder_text     = "Pick a color",
                                             .items                = std::move(color_names),
                                             .width                = 200.0f,
                                             .horizontal_alignment = ui::Alignment::Leading,
                                             .on_selection_changed = [&s](std::optional<std::size_t> index)
                                             { s.picked = ComboColors[index.value_or(0)].color; }},
                                Swatch{.fill = s.picked, .width = 100.0f, .height = 30.0f},
                            },
                    }),
                    example({
                        .title   = "A ComboBox with its items set.",
                        .code    = R"cpp(
                            const std::vector<std::string> fonts{"Arial", "Comic Sans MS", "Courier New", "Segoe UI",
                                                                 "Times New Roman"};
                            ui::Control<ui::Text> sample;

                            ui::VerticalStack{
                                .spacing = 8,
                                .content = {
                                    ui::ComboBox{
                                        .header               = "Font",
                                        .items                = fonts,
                                        .text                 = fonts[2],
                                        .min_width            = 200.0f,
                                        .horizontal_alignment = ui::Alignment::Leading,
                                        .on_selection_changed = [&](std::optional<std::size_t> index)
                                        { sample->set_text_style({.font_size = 14.0f, .font_family = fonts[index.value_or(2)]}); },
                                    },
                                    ui::Text{
                                        .text       = "You can set the font used for this text.",
                                        .text_style = ui::TextStyle{.font_size = 14.0f, .font_family = fonts[2]},
                                        .control    = sample,
                                    },
                                },
                            }
                        )cpp",
                        .stacked = true,
                        .controls =
                            {
                                ui::ComboBox{.header    = "Font",
                                             .items     = std::vector<std::string>(Fonts.begin(), Fonts.end()),
                                             .text      = Fonts[2],
                                             .min_width = 200.0f,
                                             .horizontal_alignment = ui::Alignment::Leading,
                                             .on_selection_changed = [&s](std::optional<std::size_t> index)
                                             { s.font_sample->set_text_style(sample_style(Fonts[index.value_or(2)], 14.0f)); }},
                                ui::Text{.text                 = "You can set the font used for this text.",
                                         .text_style           = sample_style(Fonts[2], 14.0f),
                                         .horizontal_alignment = ui::Alignment::Leading,
                                         .control              = s.font_sample},
                            },
                    }),
                    // Validated when the text is submitted. The message goes under the text instead
                    // of into a dialog: a modal opened from a focus-loss commit re-enters that focus change.
                    example(
                        {
                            .title   = "An editable ComboBox. Type the start of a size to have it completed.",
                            .code    = R"cpp(
                                const std::array<int, 14> font_sizes{8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 36, 48, 72};
                                std::vector<std::string>  sizes; // the same numbers, as text
                                float                     size = 10.0f;
                                ui::Property<std::string> error{};
                                ui::Control<ui::ComboBox> size_box;
                                ui::Control<ui::Text>     sample;

                                auto apply_size = [&](float value)
                                {
                                    size = value;
                                    sample->set_text_style({.font_size = value});
                                    error = "";
                                };

                                ui::ComboBox{
                                    .header               = "Font Size",
                                    .items                = std::move(sizes),
                                    .text                 = "10",
                                    .editable             = true,
                                    .width                = 200.0f,
                                    .horizontal_alignment = ui::Alignment::Leading,
                                    .on_item_chosen     = [&](std::size_t index)
                                    { apply_size(static_cast<float>(font_sizes[index])); },
                                    // Typed text that is not in the list. Return ui::Cancel to reject it.
                                    .on_text_submitted = [&](const ui::ComboBoxTextSubmittedArgs& args)
                                    {
                                        const std::string& text   = args.text;
                                        double             typed  = 0.0;
                                        const auto         parsed = std::from_chars(text.data(), text.data() + text.size(), typed);
                                        if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
                                            typed > 8.0 && typed < 100.0)
                                        {
                                            apply_size(static_cast<float>(typed));
                                            return ui::Proceed;
                                        }
                                        size_box->set_text(std::format("{}", size)); // back to the last valid size
                                        error = "The font size must be a number between 8 and 100.";
                                        return ui::Cancel;
                                    },
                                    .control = size_box,
                                }
                            )cpp",
                            .stacked = true,
                            .controls =
                                {
                                    ui::ComboBox{.header               = "Font Size",
                                                 .items                = std::move(sizes),
                                                 .text                 = std::to_string(FontSizes[2]),
                                                 .editable             = true,
                                                 .width                = 200.0f,
                                                 .horizontal_alignment = ui::Alignment::Leading,
                                                 .on_item_chosen       = [&s](std::size_t index)
                                                 { s.apply_size(static_cast<float>(FontSizes[index])); },
                                                 .on_text_submitted =
                                                     [&s](const ui::ComboBoxTextSubmittedArgs& args)
                                                 {
                                                     const std::string& text = args.text;
                                                     // A size in the list is picked as an item before it
                                                     // gets here, so only a custom one is checked.
                                                     double     size   = 0.0;
                                                     const auto parsed = std::from_chars(text.data(),
                                                                                         text.data() + text.size(),
                                                                                         size);
                                                     if (parsed.ec == std::errc{} &&
                                                         parsed.ptr == text.data() + text.size() && size > 8.0 &&
                                                         size < 100.0)
                                                     {
                                                         s.apply_size(static_cast<float>(size));
                                                         return ui::Proceed;
                                                     }
                                                     s.size_box->set_text(std::format("{}", s.size));
                                                     s.size_error =
                                                         "The font size must be a number between 8 and "
                                                         "100.";
                                                     return ui::Cancel;
                                                 },
                                                 .control = s.size_box},
                                    ui::Text{.text       = "You can set the font size used for this text.",
                                             .text_style = sample_style("Segoe UI", static_cast<float>(FontSizes[2])),
                                             .horizontal_alignment = ui::Alignment::Leading,
                                             .control              = s.size_sample},
                                    ui::Text{.text = s.size_error, .horizontal_alignment = ui::Alignment::Leading},
                                },
                        }),
                });
        });
}

// ---- RadioButton ----

constexpr std::array<const char*, 3> RadioColorNames{"Green", "Yellow", "White"};
constexpr std::array<ui::Color, 3>   BackgroundColors{
    ui::Color::from_rgba(0, 128, 0), // Colors.Green
    ui::Color::from_rgba(255, 255, 0),
    ui::Color::from_rgba(255, 255, 255),
};
// The border color choice uses its own shades: dark green and gold for Green and Yellow.
constexpr std::array<ui::Color, 3> BorderColors{
    ui::Color::from_rgba(0, 100, 0),
    ui::Color::from_rgba(255, 215, 0),
    ui::Color::from_rgba(255, 255, 255),
};

enum class ShippingSpeed
{
    Standard,
    Express,
    Overnight,
};

std::string shipping_text(ShippingSpeed speed)
{
    switch (speed)
    {
        case ShippingSpeed::Standard:
            return "Arrives in 5 to 7 days.";
        case ShippingSpeed::Express:
            return "Arrives in 2 days.";
        case ShippingSpeed::Overnight:
            return "Arrives tomorrow.";
    }
    return {};
}

struct RadioButtonState
{
    ui::Property<ShippingSpeed> speed{ShippingSpeed::Standard};
    ui::Computed<std::string>   arrival{shipping_text, speed};
    ui::Property<std::string>   selected{"Select an option."};
    ui::Property<ui::Color>     background_color{BackgroundColors[0]};
    ui::Property<ui::Color>     border_color{BorderColors[1]};
};

ui::Spec color_choice(const char* title, int initial, ui::Property<ui::Color>& target, const std::array<ui::Color, 3>& colors)
{
    return ui::RadioButtons{.header         = title,
                            .items          = std::vector<std::string>(RadioColorNames.begin(), RadioColorNames.end()),
                            .selected_index = initial,
                            .max_columns    = 3,
                            .on_selection_changed = [&target, &colors](std::optional<std::size_t> index)
                            {
                                if (index)
                                {
                                    target = colors[*index];
                                }
                            }};
}

ui::Spec radio_button_page(Context&)
{
    return with_state<RadioButtonState>(
        [](RadioButtonState& s)
        {
            return examples({
                example({
                    .title  = "A group of RadioButton controls in a RadioButtons group.",
                    .code   = R"cpp(
                        ui::Property<std::string> selected{"Select an option."};

                        ui::RadioButtons{
                            .header               = "Options:",
                            .items                = {"Option 1", "Option 2", "Option 3"},
                            .on_selection_changed = [&](std::optional<std::size_t> index)
                            { selected = std::format("You selected Option {}", index.value_or(0) + 1); },
                        }
                    )cpp",
                    .output = output_text(s.selected),
                    .controls =
                        {
                            ui::RadioButtons{.header = "Options:",
                                             .items  = {"Option 1", "Option 2", "Option 3"},
                                             .on_selection_changed =
                                                 [&s](std::optional<std::size_t> index)
                                             {
                                                 s.selected = std::format("You selected Option {}", index.value_or(0) + 1);
                                             }},
                        },
                }),
                example({
                    .title   = "RadioButtons anywhere in a layout, choosing one value.",
                    .code    = R"cpp(
                        enum class ShippingSpeed { Standard, Express, Overnight };
                        ui::Property<ShippingSpeed> speed{ShippingSpeed::Standard};

                        // Every button bound to `speed` is one group: Tab stops on the checked one,
                        // the arrow keys move between them.
                        ui::VerticalStack{
                            .content = {
                                ui::RadioButton{.text = "Standard", .checked = ui::selects(speed, ShippingSpeed::Standard)},
                                ui::RadioButton{.text = "Express", .checked = ui::selects(speed, ShippingSpeed::Express)},
                                ui::Text{.text = "Express and Overnight include tracking.", .margin = ui::Edges{.left = 28, .bottom = 8}},
                                ui::RadioButton{.text = "Overnight", .checked = ui::selects(speed, ShippingSpeed::Overnight)},
                            },
                        }
                    )cpp",
                    .stacked = true,
                    .output  = output_text(s.arrival),
                    .controls =
                        {
                            ui::RadioButton{.text = "Standard", .checked = ui::selects(s.speed, ShippingSpeed::Standard)},
                            ui::RadioButton{.text = "Express", .checked = ui::selects(s.speed, ShippingSpeed::Express)},
                            ui::Text{.text   = "Express and Overnight include tracking.",
                                     .margin = ui::Edges{.left = 28.0f, .bottom = 8.0f}},
                            ui::RadioButton{.text = "Overnight", .checked = ui::selects(s.speed, ShippingSpeed::Overnight)},
                        },
                }),
                example({
                    .title   = "Two RadioButtons controls with strings as options.",
                    .code    = R"cpp(
                        const std::array<ui::Color, 3> colors{
                            ui::Color::from_rgba(0, 128, 0),
                            ui::Color::from_rgba(255, 255, 0),
                            ui::Color::from_rgba(255, 255, 255),
                        };
                        ui::Property<ui::Color> background{colors[0]};

                        ui::RadioButtons{
                            .header               = "Background",
                            .items                = {"Green", "Yellow", "White"},
                            .selected_index       = 0,
                            .max_columns          = 3,
                            .on_selection_changed = [&](std::optional<std::size_t> index)
                            {
                                if (index)
                                {
                                    background = colors[*index];
                                }
                            },
                        }

                        // ... "Border" is a second group of the same shape, starting at index 1 ...
                    )cpp",
                    .stacked = true,
                    .controls =
                        {
                            color_choice("Background", 0, s.background_color, BackgroundColors),
                            color_choice("Border", 1, s.border_color, BorderColors),
                            Swatch{.fill = s.background_color, .border = s.border_color, .border_thickness = 10.0f, .height = 50.0f},
                        },
                }),
            });
        });
}

// ---- Rating ----

struct RatingState
{
    ui::Property<std::string> value{};
    ui::Property<bool>        clear_enabled{false};
    ui::Property<bool>        read_only{false};
    ui::Property<double>      placeholder{0.0};
};

ui::Spec rating_page(Context&)
{
    return with_state<RatingState>(
        [](RatingState& s)
        {
            return examples(
                {
                    example(
                        {
                            .title  = "A simple Rating",
                            .code   = R"cpp(
                                ui::Property<std::string> output{};
                                ui::Property<bool>        clear_enabled{false};
                                ui::Property<bool>        read_only{false};

                                ui::Rating{
                                    .clear_enabled        = clear_enabled,
                                    .caption              = "312 ratings",
                                    .read_only            = read_only,
                                    .horizontal_alignment = ui::Alignment::Leading,
                                    .accessibility        = {.name = "Simple Rating"},
                                    // An unset value is a cleared rating.
                                    .on_value_changed = [&](std::optional<double> value)
                                    {
                                        output = value ? std::format("{:.0f}", *value) : std::string{};
                                    },
                                }
                            )cpp",
                            .output = output_text(s.value),
                            .options =
                                ui::
                                    VerticalStack{
                                        .spacing         = 4.0f,
                                        .child_alignment = ui::Alignment::Stretch,
                                        .width           = 220.0f,
                                        .content =
                                            {
                                                ui::CheckBox{.text                 = "Allow clearing",
                                                             .horizontal_alignment = ui::Alignment::Leading,
                                                             .on_checked_changed   = [&s](std::optional<bool> on)
                                                             { s.clear_enabled = on == true; }},
                                                ui::Text{.text     = "Drag left or click again to clear your rating.",
                                                         .wrapping = ui::TextWrapping::WrapWholeWords},
                                                ui::CheckBox{.text   = "Read only",
                                                             .margin = ui::Edges{.top = 12.0f},
                                                             .horizontal_alignment = ui::Alignment::Leading,
                                                             .on_checked_changed   = [&s](std::optional<bool> on)
                                                             { s.read_only = on == true; }},
                                            },
                                    },
                            .controls =
                                {
                                    ui::Rating{.clear_enabled        = s.clear_enabled,
                                               .caption              = "312 ratings",
                                               .read_only            = s.read_only,
                                               .horizontal_alignment = ui::Alignment::Leading,
                                               .accessibility        = {.name = "Simple Rating"},
                                               .on_value_changed     = [&s](std::optional<double> value)
                                               { s.value = value ? whole(*value) : std::string{}; }},
                                },
                        }),
                    example({
                        .title   = "A Rating with a placeholder value",
                        .code    = R"cpp(
                                ui::Property<double> placeholder{0.0};

                                ui::Rating{
                                    .placeholder_value    = placeholder,
                                    .horizontal_alignment = ui::Alignment::Leading,
                                    .accessibility        = {.name = "Rating with placeholder"},
                                }

                                ui::Slider{
                                    .header           = "Placeholder value",
                                    .value            = 0.0f,
                                    .max              = 5.0f,
                                    .step             = 0.5f,
                                    .step_frequency   = 0.5f,
                                    .min_width        = 200.0f,
                                    .on_value_changed = [&](double value) { placeholder = value; },
                                }
                            )cpp",
                        .options = ui::Slider{.header           = "Placeholder value",
                                              .value            = 0.0f,
                                              .max              = 5.0f,
                                              .step             = 0.5f,
                                              .step_frequency   = 0.5f,
                                              .min_width        = 200.0f,
                                              .on_value_changed = [&s](double value) { s.placeholder = value; }},
                        .controls =
                            {
                                ui::Rating{.placeholder_value    = s.placeholder,
                                           .horizontal_alignment = ui::Alignment::Leading,
                                           .accessibility        = {.name = "Rating with placeholder"}},
                            },
                    }),
                });
        });
}

// ---- Slider ----

struct SliderState
{
    ui::Property<std::string>       simple{"0"};
    ui::Property<std::string>       range{"800"};
    ui::Property<std::string>       ticks{"0"};
    ui::Property<std::string>       vertical{"0"};
    ui::Property<ui::SliderSnapsTo> snaps_to{ui::SliderSnapsTo::StepValues};
    ui::Control<ui::Slider>         range_slider;
};

ui::Spec slider_page(Context&)
{
    return with_state<SliderState>(
        [](SliderState& s)
        {
            return examples({
                example({
                    .title  = "A simple Slider.",
                    .code   = R"cpp(
                        ui::Property<std::string> output{"0"};

                        ui::Slider{
                            .width            = 200.0f,
                            .accessibility    = {.name = "Simple example"},
                            .on_value_changed = [&](double value) { output = std::format("{:.0f}", value); },
                        }
                    )cpp",
                    .output = output_text(s.simple),
                    .controls =
                        {
                            ui::Slider{.width            = 200.0f,
                                       .accessibility    = {.name = "Simple example"},
                                       .on_value_changed = [&s](double value) { s.simple = whole(value); }},
                        },
                }),
                // `step` is the keyboard increment; `step_frequency` is
                // what a drag snaps to.
                example({
                    .title  = "A Slider with range and steps specified.",
                    .code   = R"cpp(
                        ui::Property<std::string> output{"800"};
                        ui::Control<ui::Slider>   slider;

                        // step is the arrow-key increment, step_frequency is what a drag snaps to.
                        ui::Slider{
                            .header           = "Control header",
                            .value            = 800.0f,
                            .min              = 500.0f,
                            .max              = 1000.0f,
                            .step             = 10.0f,
                            .step_frequency   = 10.0f,
                            .width            = 200.0f,
                            .on_value_changed = [&](double value) { output = std::format("{:.0f}", value); },
                            .control          = slider,
                        }

                        // The options change the built slider through its handle.
                        ui::NumberBox{
                            .value                 = 500.0,
                            .spin_button_placement = ui::NumberBoxSpinButtonPlacementMode::Compact,
                            .on_value_changed      = [&](std::optional<double> minimum)
                            {
                                if (minimum && *minimum < slider->max())
                                {
                                    slider->set_range(static_cast<float>(*minimum), slider->max());
                                }
                            },
                        }
                        // ... and likewise:
                        slider->set_step_frequency(10.0f);
                        slider->set_step(10.0f);
                    )cpp",
                    .output = output_text(s.range),
                    .options =
                        ui::Grid{
                            .columns        = {ui::AutoSize, ui::AutoSize},
                            .rows           = {ui::AutoSize, ui::AutoSize, ui::AutoSize, ui::AutoSize},
                            .row_spacing    = 8.0f,
                            .column_spacing = 10.0f,
                            .content =
                                {
                                    ui::Cell{.row = 0, .column = 0, .content = ui::Text{.text = "Minimum:", .vertical_alignment = ui::Alignment::Center}},
                                    ui::Cell{.row     = 0,
                                     .column  = 1,
                                     .content = ui::NumberBox{.value            = 500.0,
                                                              .spin_button_placement = ui::NumberBoxSpinButtonPlacementMode::Compact,
                                                              .on_value_changed = [&s](std::optional<double> value)
                                                              {
                                                                  if (value && *value < s.range_slider->max())
                                                                  {
                                                                      s.range_slider->set_range(static_cast<float>(*value),
                                                                                                s.range_slider->max());
                                                                  }
                                                              }}},
                                    ui::Cell{.row = 1, .column = 0, .content = ui::Text{.text = "Maximum:", .vertical_alignment = ui::Alignment::Center}},
                                    ui::Cell{.row     = 1,
                                     .column  = 1,
                                     .content = ui::NumberBox{.value            = 1000.0,
                                                              .spin_button_placement = ui::NumberBoxSpinButtonPlacementMode::Compact,
                                                              .on_value_changed = [&s](std::optional<double> value)
                                                              {
                                                                  if (value && *value > s.range_slider->min())
                                                                  {
                                                                      s.range_slider->set_range(s.range_slider->min(),
                                                                                                static_cast<float>(*value));
                                                                  }
                                                              }}},
                                    ui::Cell{.row = 2, .column = 0, .content = ui::Text{.text = "Step frequency:", .vertical_alignment = ui::Alignment::Center}},
                                    ui::Cell{.row     = 2,
                                     .column  = 1,
                                     .content = ui::NumberBox{.value            = 10.0,
                                                              .min              = 1.0,
                                                              .spin_button_placement = ui::NumberBoxSpinButtonPlacementMode::Compact,
                                                              .on_value_changed = [&s](std::optional<double> value)
                                                              { s.range_slider->set_step_frequency(static_cast<float>(value.value_or(0.0))); }}},
                                    ui::Cell{.row = 3, .column = 0, .content = ui::Text{.text = "Step:", .vertical_alignment = ui::Alignment::Center}},
                                    ui::Cell{.row     = 3,
                                     .column  = 1,
                                     .content = ui::NumberBox{.value            = 10.0,
                                                              .min                = 0.0,
                                                              .spin_button_placement = ui::NumberBoxSpinButtonPlacementMode::Compact,
                                                              .on_value_changed = [&s](std::optional<double> value)
                                                              { s.range_slider->set_step(static_cast<float>(value.value_or(0.0))); }}},
                                },
                        },
                    .controls =
                        {
                            ui::Slider{.header           = "Control header",
                                       .value            = 800.0f,
                                       .min              = 500.0f,
                                       .max              = 1000.0f,
                                       .step             = 10.0f,
                                       .step_frequency   = 10.0f,
                                       .width            = 200.0f,
                                       .on_value_changed = [&s](double value) { s.range = whole(value); },
                                       .control              = s.range_slider},
                        },
                }),
                example({
                    .title   = "A Slider with tick marks.",
                    .code    = R"cpp(
                        ui::Property<std::string>       output{"0"};
                        ui::Property<ui::SliderSnapsTo> snaps_to{ui::SliderSnapsTo::StepValues};

                        ui::Slider{
                            .tick_frequency   = 20.0f,
                            .tick_placement   = ui::SliderTickPlacement::Outside,
                            .snaps_to         = snaps_to,
                            .width            = 290.0f,
                            .accessibility    = {.name = "Example with ticks"},
                            .on_value_changed = [&](double value) { output = std::format("{:.0f}", value); },
                        }

                        ui::RadioButtons{
                            .header               = "Snaps to:",
                            .items                = {"StepValues", "Ticks"},
                            .selected_index       = 0,
                            .on_selection_changed = [&](std::optional<std::size_t> index)
                            { snaps_to = index == 1 ? ui::SliderSnapsTo::Ticks : ui::SliderSnapsTo::StepValues; },
                        }
                    )cpp",
                    .output  = output_text(s.ticks),
                    .options = ui::RadioButtons{.header               = "Snaps to:",
                                                .items                = {"StepValues", "Ticks"},
                                                .selected_index       = 0,
                                                .on_selection_changed = [&s](std::optional<std::size_t> index)
                                                {
                                                    s.snaps_to = index == 1 ? ui::SliderSnapsTo::Ticks
                                                                            : ui::SliderSnapsTo::StepValues;
                                                }},
                    .controls =
                        {
                            ui::Slider{.tick_frequency   = 20.0f,
                                       .tick_placement   = ui::SliderTickPlacement::Outside,
                                       .snaps_to         = s.snaps_to,
                                       .width            = 290.0f,
                                       .accessibility    = {.name = "Example with ticks"},
                                       .on_value_changed = [&s](double value) { s.ticks = whole(value); }},
                        },
                }),
                example({
                    .title  = "A vertical slider with range and tick marks specified.",
                    .code   = R"cpp(
                        ui::Property<std::string> output{"0"};

                        ui::Slider{
                            .min              = -50.0f,
                            .max              = 50.0f,
                            .tick_frequency   = 10.0f,
                            .tick_placement   = ui::SliderTickPlacement::Outside,
                            .orientation      = ui::Orientation::Vertical,
                            .width            = 100.0f,
                            .height           = 100.0f,
                            .accessibility    = {.name = "Vertical example"},
                            .on_value_changed = [&](double value) { output = std::format("{:.0f}", value); },
                        }
                    )cpp",
                    .output = output_text(s.vertical),
                    .controls =
                        {
                            ui::Slider{.min              = -50.0f,
                                       .max              = 50.0f,
                                       .tick_frequency   = 10.0f,
                                       .tick_placement   = ui::SliderTickPlacement::Outside,
                                       .orientation      = ui::Orientation::Vertical,
                                       .width            = 100.0f,
                                       .height           = 100.0f,
                                       .accessibility    = {.name = "Vertical example"},
                                       .on_value_changed = [&s](double value) { s.vertical = whole(value); }},
                        },
                }),
            });
        });
}

// ---- ToggleSwitch ----

struct ToggleSwitchState
{
    ui::Property<bool> working{true};
};

ui::Spec toggle_switch_page(Context&)
{
    return with_state<ToggleSwitchState>(
        [](ToggleSwitchState& s)
        {
            return examples({
                example({
                    .title    = "A simple ToggleSwitch.",
                    .code     = R"cpp(
                        ui::ToggleSwitch{.accessibility = {.name = "simple ToggleSwitch"}}
                    )cpp",
                    .controls = {ui::ToggleSwitch{.accessibility = {.name = "simple ToggleSwitch"}}},
                }),
                example({
                    .title = "A ToggleSwitch with custom header and content.",
                    .code  = R"cpp(
                        ui::Property<bool> working{true};

                        ui::HorizontalStack{
                            .spacing = 8,
                            .content = {
                                ui::ToggleSwitch{
                                    .header            = "Toggle work",
                                    .checked           = working,
                                    .checked_text      = "Working",
                                    .unchecked_text    = "Do work",
                                },
                                ui::ProgressRing{.active = working, .width = 32.0f},
                            },
                        }
                    )cpp",
                    .controls =
                        {
                            ui::ToggleSwitch{.header         = "Toggle work",
                                             .checked        = s.working,
                                             .checked_text   = "Working",
                                             .unchecked_text = "Do work"},
                            ui::ProgressRing{.active = s.working, .width = 32.0f},
                        },
                }),
            });
        });
}

// ---- DropdownButton ----

// A MenuFlyout below the button, left edges aligned, with Send, Reply and Reply All, plus their
// glyph icons in the second card.
ui::Spec email_menu(ui::Spec button, bool icons)
{
    std::vector<ui::MenuItem> items{{.text = "Send"}, {.text = "Reply"}, {.text = "Reply All"}};
    if (icons)
    {
        items[0].icon = ui::IconGlyph{0xE725, "Segoe Fluent Icons", 16.0f};
        items[1].icon = ui::IconGlyph{0xE8CA, "Segoe Fluent Icons", 16.0f};
        items[2].icon = ui::IconGlyph{0xE8C2, "Segoe Fluent Icons", 16.0f};
    }
    return ui::MenuFlyout{.items               = std::move(items),
                          .placement           = ui::PopupPlacement::Below,
                          .placement_alignment = ui::PopupPlacementAlignment::Leading,
                          .anchor              = std::move(button)};
}

ui::Spec dropdown_button_page(Context&)
{
    return examples({
        example({
            .title    = "Simple DropdownButton",
            .code     = R"cpp(
                // The button is the anchor of the flyout it opens.
                ui::MenuFlyout{
                    .items     = {{.text = "Send"}, {.text = "Reply"}, {.text = "Reply All"}},
                    .placement = ui::PopupPlacement::Below,
                    .placement_alignment = ui::PopupPlacementAlignment::Leading,
                    .anchor              = ui::DropdownButton{.text = "Email"},
                }
            )cpp",
            .controls = {email_menu(ui::DropdownButton{.text = "Email"}, false)},
        }),
        example({
            .title = "DropdownButton with Icons",
            .code  = R"cpp(
                ui::MenuFlyout{
                    .items = {
                        {.text = "Send", .icon = ui::IconGlyph{0xE725, "Segoe Fluent Icons", 16.0f}},
                        {.text = "Reply", .icon = ui::IconGlyph{0xE8CA, "Segoe Fluent Icons", 16.0f}},
                        {.text = "Reply All", .icon = ui::IconGlyph{0xE8C2, "Segoe Fluent Icons", 16.0f}},
                    },
                    .placement = ui::PopupPlacement::Below,
                    .placement_alignment = ui::PopupPlacementAlignment::Leading,
                    .anchor              = ui::DropdownButton{
                        .accessibility = {.name = "Email"},
                        .content       = ui::Icon{.source = ui::IconGlyph{0xE715, "Segoe Fluent Icons", 20.0f}},
                    },
                }
            )cpp",
            .controls =
                {email_menu(ui::DropdownButton{.accessibility = {.name = "Email"},
                                               .content = ui::Icon{.source = ui::IconGlyph{0xE715, "Segoe Fluent Icons", 20.0f}}},
                            true)},
        }),
    });
}

// ---- SplitButton ----

struct NamedSwatch
{
    const char* name;
    ui::Color   color;
};

// The named colors the samples' swatches use.
const std::array<NamedSwatch, 9> Swatches{{
    {"Red", ui::Color::from_rgba(255, 0, 0)},
    {"Orange", ui::Color::from_rgba(255, 165, 0)},
    {"Yellow", ui::Color::from_rgba(255, 255, 0)},
    {"Green", ui::Color::from_rgba(0, 128, 0)},
    {"Blue", ui::Color::from_rgba(0, 0, 255)},
    {"Indigo", ui::Color::from_rgba(75, 0, 130)},
    {"Violet", ui::Color::from_rgba(238, 130, 238)},
    {"Gray", ui::Color::from_rgba(128, 128, 128)},
    {"Black", ui::Color::from_rgba(0, 0, 0)},
}};

ui::Spec swatch_rectangle(const NamedSwatch& swatch)
{
    return ui::Rectangle{.fill          = swatch.color,
                         .radius_x      = 4.0f,
                         .radius_y      = 4.0f,
                         .width         = 32.0f,
                         .height        = 32.0f,
                         .accessibility = {.name = swatch.name}};
}

constexpr const char* Lorem =
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, "
    "sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Tempor commodo ullamcorper a lacus.";

struct SplitButtonState
{
    ui::Property<ui::Color>      current{ui::Color::from_rgba(0, 128, 0)}; // Colors.Green
    ui::Control<ui::RichEditBox> editor;
    ui::Control<ui::Flyout>      colors;
    ui::Control<ui::Flyout>      text_colors;
    bool                         refocus = false;
};

ui::Spec split_button_page(Context& ctx)
{
    ui::Window& window = ctx.window;
    return with_state<SplitButtonState>(
        [&window](SplitButtonState& s)
        {
            // The page's constructor: the editor's text, in the current color.
            std::unique_ptr<ui::Element> editor = ui::build(
                ui::RichEditBox{.placeholder_text = "Type something here",
                                .width            = 240.0f,
                                .min_height       = 96.0f,
                                .control          = s.editor});
            s.editor->set_text(Lorem);
            s.editor->select_all();
            s.editor->set_selection_format({.foreground = s.current.get()});

            // TileView_ItemClick: the swatch colors the selection and the button's face, the editor
            // takes focus back, and the flyout closes.
            std::unique_ptr<ui::Element> color_button = ui::build(ui::Flyout{
                .placement = ui::PopupPlacement::Below,
                .control   = s.colors,
                .anchor    = ui::SplitButton{.padding            = ui::Thickness{0.0f},
                                             .vertical_alignment = ui::Alignment::Leading,
                                             .accessibility      = {.name = "Font color"},
                                             // A click on the face applies its color again.
                                             .on_click = [&s]
                                             { s.editor->set_selection_format({.foreground = s.current.get()}); },
                                             .content = Swatch{.fill = s.current, .width = 32.0f, .height = 32.0f}},
                .content = ui::TileView<NamedSwatch>{.items = std::vector<NamedSwatch>(Swatches.begin(), Swatches.begin() + 8),
                                                     .maximum_rows_or_columns = 3,
                                                     .on_item_click =
                                                         [&s](const NamedSwatch& swatch)
                                                     {
                                                         s.editor->set_selection_format({.foreground = swatch.color});
                                                         s.current = swatch.color;
                                                         s.refocus = true;
                                                         s.colors->hide();
                                                     },
                                                     .item = [](const NamedSwatch& swatch) -> ui::Spec
                                                     { return swatch_rectangle(swatch); }},
            });
            // While the flyout is open focus stays inside it: the editor takes it once it closes.
            s.colors->on_closed(
                [&s, &window](ui::PopupCloseReason)
                {
                    if (std::exchange(s.refocus, false))
                    {
                        (void)window.focus_element(s.editor);
                    }
                });

            // Any swatch just closes the flyout.
            ui::CellList cells;
            for (std::size_t i = 0; i < Swatches.size(); ++i)
            {
                cells.cells.push_back(
                    {i / 3,
                     i % 3,
                     1,
                     1,
                     ui::build(ui::Button{.padding       = ui::Thickness{0.0f},
                                          .margin        = ui::Thickness{6.0f},
                                          .accessibility = {.name = Swatches[i].name},
                                          .on_click      = [&s] { s.text_colors->hide(); },
                                          .content       = swatch_rectangle(Swatches[i])})});
            }
            ui::Spec text_button = ui::Flyout{
                .placement = ui::PopupPlacement::Below,
                .control   = s.text_colors,
                .anchor    = ui::SplitButton{.text               = "Choose color",
                                             .padding            = ui::Thickness{5.0f},
                                             .vertical_alignment = ui::Alignment::Leading,
                                             .accessibility      = {.name = "Font color with text"}},
                .content   = ui::Grid{.columns = {ui::AutoSize, ui::AutoSize, ui::AutoSize},
                                      .rows    = {ui::AutoSize, ui::AutoSize, ui::AutoSize},
                                      .content = std::move(cells)},
            };

            return examples({
                example({
                    .title    = "A SplitButton controlling text color in a RichEditBox",
                    .code     = R"cpp(
                        struct NamedSwatch
                        {
                            const char* name;
                            ui::Color   color;
                        };
                        std::vector<NamedSwatch> swatches = ...; // Red, Orange, Yellow, ...

                        ui::Property<ui::Color>      current{ui::Color::from_rgba(0, 128, 0)};
                        ui::Control<ui::RichEditBox> editor;
                        ui::Control<ui::Flyout>      colors;

                        // The split button is the anchor: its chevron opens the flyout.
                        ui::Flyout{
                            .placement = ui::PopupPlacement::Below,
                            .control   = colors,
                            .anchor    = ui::SplitButton{
                                .padding       = ui::Thickness{0.0f},
                                .accessibility = {.name = "Font color"},
                                // The primary part applies the current color again.
                                .on_click = [&] { editor->set_selection_format({.foreground = current.get()}); },
                                .content  = ui::Rectangle{
                                    .fill   = current,
                                    .width  = 32.0f,
                                    .height = 32.0f,
                                },
                            },
                            .content = ui::TileView<NamedSwatch>{
                                .items = std::move(swatches),
                                .maximum_rows_or_columns = 3,
                                .on_item_click           = [&](const NamedSwatch& swatch)
                                {
                                    editor->set_selection_format({.foreground = swatch.color});
                                    current = swatch.color;
                                    colors->hide();
                                },
                                .item  = [](const NamedSwatch& swatch) -> ui::Spec
                                {
                                    return ui::Rectangle{.fill     = swatch.color,
                                                         .radius_x = 4.0f,
                                                         .radius_y = 4.0f,
                                                         .width    = 32.0f,
                                                         .height   = 32.0f};
                                },
                            },
                        }
                    )cpp",
                    .options  = std::move(editor),
                    .controls = {std::move(color_button)},
                }),
                example({
                    .title    = "A SplitButton with text",
                    .code     = R"cpp(
                        ui::Control<ui::Flyout> colors;

                        ui::Flyout{
                            .placement = ui::PopupPlacement::Below,
                            .control   = colors,
                            .anchor    = ui::SplitButton{
                                .text          = "Choose color",
                                .padding       = ui::Thickness{5.0f},
                                .accessibility = {.name = "Font color with text"},
                            },
                            // A 3x3 grid of swatch buttons. Each one closes the flyout.
                            .content = ui::Grid{
                                .columns = {ui::AutoSize, ui::AutoSize, ui::AutoSize},
                                .rows    = {ui::AutoSize, ui::AutoSize, ui::AutoSize},
                                .content   = {
                                    ui::Cell{.row     = 0,
                                     .column  = 0,
                                     .content = ui::Button{
                                         .padding       = ui::Thickness{0.0f},
                                         .margin        = ui::Thickness{6.0f},
                                         .accessibility = {.name = "Red"},
                                         .on_click      = [&] { colors->hide(); },
                                         .content       = ui::Rectangle{
                                             .fill     = ui::Color::from_rgba(255, 0, 0),
                                             .radius_x = 4.0f,
                                             .radius_y = 4.0f,
                                             .width    = 32.0f,
                                             .height   = 32.0f,
                                         },
                                     }},
                                    // ... one cell per color ...
                                },
                            },
                        }
                    )cpp",
                    .controls = {std::move(text_button)},
                }),
            });
        });
}

// ---- ToggleSplitButton ----

struct ToggleSplitButtonState
{
    ui::Property<bool>                 checked{false};
    ui::Property<std::string>          list_type{"List type: none"};
    bool                               roman = false; // bullets, or roman numerals
    ui::Control<ui::ToggleSplitButton> button;
    ui::Control<ui::Icon>              icon;
    ui::Control<ui::Flyout>            flyout;
};

std::string list_type_text(const ToggleSplitButtonState& s, bool on)
{
    return on ? (s.roman ? "List type: roman numerals" : "List type: bullets") : "List type: none";
}

ui::Spec toggle_split_button_page(Context&)
{
    return with_state<ToggleSplitButtonState>(
        [](ToggleSplitButtonState& s)
        {
            // A pick becomes the button's icon, name and list type, and checks it.
            auto pick = [&s](bool roman)
            {
                s.roman = roman;
                s.icon->set_source(ui::symbol_glyph(roman ? ui::Symbol::Bullets : ui::Symbol::List));
                ui::Accessibility name = s.button->accessibility();
                name.name              = roman ? "Roman Numerals" : "Bullets";
                s.button->set_accessibility(std::move(name));
                s.list_type = list_type_text(s, true);
                s.checked   = true;
                s.flyout->hide();
            };
            auto bullet_button = [pick](const char* name, ui::Symbol symbol, bool roman)
            {
                return ui::Button{.padding       = ui::Thickness{4.0f},
                                  .margin        = ui::Thickness{6.0f},
                                  .accessibility = {.name = name},
                                  .on_click      = [pick, roman] { pick(roman); },
                                  .content       = ui::Icon{.source = ui::symbol_glyph(symbol)}};
            };
            return examples({
                example({
                    .title    = "Using ToggleSplitButton to control bulleted list functionality in RichEditBox",
                    .code     = R"cpp(
                        ui::Property<bool>                 checked{false};
                        ui::Property<std::string>          list_type{"List type: none"};
                        bool                               roman = false;
                        ui::Control<ui::ToggleSplitButton> button;
                        ui::Control<ui::Icon>              icon;
                        ui::Control<ui::Flyout>            flyout;

                        // A pick in the flyout becomes the button's icon and checks it.
                        auto pick = [&](bool use_roman)
                        {
                            roman = use_roman;
                            icon->set_glyph(ui::symbol_glyph(roman ? ui::Symbol::Bullets : ui::Symbol::List));
                            list_type = roman ? "List type: roman numerals" : "List type: bullets";
                            checked   = true;
                            flyout->hide();
                        };

                        ui::Flyout{
                            .placement = ui::PopupPlacement::Below,
                            .control   = flyout,
                            .anchor    = ui::ToggleSplitButton{
                                .checked       = checked,
                                .accessibility = {.name = "Bullets"},
                                // checked is one-way, so the handler writes the property back.
                                .on_checked_changed = [&](bool on)
                                {
                                    checked   = on;
                                    list_type = !on ? "List type: none" : roman ? "List type: roman numerals" : "List type: bullets";
                                },
                                .control = button,
                                .content = ui::Icon{.source = ui::symbol_glyph(ui::Symbol::List), .control = icon},
                            },
                            .content = ui::HorizontalStack{
                                .content = {
                                    ui::Button{
                                        .accessibility = {.name = "Bulleted list"},
                                        .on_click      = [&] { pick(false); },
                                        .content       = ui::Icon{.source = ui::symbol_glyph(ui::Symbol::List)},
                                    },
                                    ui::Button{
                                        .accessibility = {.name = "Roman numerals list"},
                                        .on_click      = [&] { pick(true); },
                                        .content       = ui::Icon{.source = ui::symbol_glyph(ui::Symbol::Bullets)},
                                    },
                                },
                            },
                        }
                    )cpp",
                    .output   = output_text(s.list_type),
                    .controls = {ui::Flyout{
                        .placement = ui::PopupPlacement::Below,
                        .control   = s.flyout,
                        .anchor    = ui::ToggleSplitButton{.checked            = s.checked,
                                                           .vertical_alignment = ui::Alignment::Leading,
                                                           .accessibility      = {.name = "Bullets"},
                                                           .on_checked_changed =
                                                               [&s](bool on)
                                                           {
                                                            s.checked   = on;
                                                            s.list_type = list_type_text(s, on);
                                                           },
                                                           .control = s.button,
                                                           .content = ui::Icon{.source = ui::symbol_glyph(ui::Symbol::List),
                                                                               .control = s.icon}},
                        .content = ui::HorizontalStack{.content = {bullet_button("Bulleted list", ui::Symbol::List, false),
                                                                   bullet_button("Roman numerals list", ui::Symbol::Bullets, true)}},
                    }},
                }),
            });
        });
}

} // namespace

std::span<const Page> basic_input_pages()
{
    static const auto Pages = std::to_array<Page>({
        {"Button", "A control that responds to a click and raises an event.", button_page},
        {"DropdownButton", "A button that displays a flyout of choices when clicked.", dropdown_button_page},
        {"ToggleButton", "A button that can be switched between two states like a CheckBox.", toggle_button_page},
        {"SplitButton", "A two-part button that displays a flyout when its secondary part is clicked.", split_button_page},
        {"ToggleSplitButton", "A version of the SplitButton where the activation target toggles on/off.", toggle_split_button_page},
        {"CheckBox", "A box the user can select or clear.", check_box_page},
        {"ColorPicker", "A selectable color spectrum with channel sliders.", color_picker_page},
        {"ComboBox", "A drop-down list of items to pick from, or to type a value into when editable.", combo_box_page},
        {"RadioButton", "One selectable option out of a mutually exclusive set.", radio_button_page},
        {"Rating", "A one-to-five-star rating.", rating_page},
        {"Slider", "A value selected from a continuous range.", slider_page},
        {"ToggleSwitch", "A switch between two states.", toggle_switch_page},
    });
    return Pages;
}

} // namespace gallery
