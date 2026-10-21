// SPDX-License-Identifier: MIT
#include "pages_motion.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <numbers>
#include <optional>

#include <cmath>
#include <cstddef>

using ui::operator""_fr;

namespace gallery
{
namespace
{
// ---- Animation Interop ----

constexpr std::array<float, 4> DampingRatios{0.2f, 0.4f, 0.6f, 0.8f};

struct InteropState
{
    ui::Property<std::optional<std::size_t>> damping{2}; // the 0.6 radio button
    ui::Property<float>                      period{50.0f};
    ui::Property<float>                      radius{200.0f};
    ui::Property<float>                      font_size{12.0f};
    ui::Property<float>                      text_margin{0.0f};
};

// A spring animation on Scale toward `final_value` on all three axes.
ui::SpringAnimation spring_to(const InteropState& s, float final_value)
{
    return ui::SpringAnimation{.final_value   = ui::Vector3{final_value, final_value, final_value},
                               .damping_ratio = DampingRatios[std::min<std::size_t>(s.damping.get().value_or(0), 3)],
                               .period        = ui::Duration{s.period.get()}};
}

// element_PointerEntered / element_PointerExited: scale up to 1.5 on hover, back to 1 on leaving.
void spring_on_hover(ui::Element& element, const InteropState& s)
{
    element.on_pointer_entered(
        [&element, &s] { element.start_animation(ui::AnimatedProperty::Scale, spring_to(s, 1.5f)); });
    element.on_pointer_exited(
        [&element, &s] { element.start_animation(ui::AnimatedProperty::Scale, spring_to(s, 1.0f)); });
}

} // namespace

ui::Spec animation_interop_page(Context& ctx)
{
    const ui::TextStyle body = ctx.window.theme().type.body;
    return with_state<InteropState>(
        [body](InteropState& s)
        {
            ui::Control<ui::Button>                hover_button;
            ThemedShape*                           square = nullptr;
            ThemedShape*                           circle = nullptr;
            std::array<ui::Control<ui::Button>, 4> stacked{};
            ui::Control<ui::Grid>                  layout_panel;
            ui::Control<ui::Text>                  target;
            ui::Control<ui::Border>                popup_card;

            auto ellipse = std::make_unique<ThemedShape>(ThemedShape::Kind::Ellipse, system_accent, 50.0f, 50.0f);
            ellipse->set_margin(ui::Symmetric{.horizontal = 55.0f, .vertical = 0.0f});
            circle = ellipse.get();

            ui::SpecList stacked_buttons;
            stacked_buttons.push_back(
                paragraph("Hover over any button to animate its scale. Notice "
                          "that the other buttons move out of the way.",
                          ui::Thickness{}));
            stacked_buttons.push_back(ui::Text{.text   = "Each button animates as a function of the "
                                                         "previous button's scale and translation.",
                                               .margin = ui::Edges{.bottom = 12.0f}});
            for (std::size_t i = 0; i < stacked.size(); ++i)
            {
                stacked_buttons.push_back(
                    ui::Button{.text                 = std::format("Item {}", i + 1),
                               .margin               = ui::Thickness{5.0f},
                               .width                = 100.0f,
                               .height               = 50.0f,
                               .horizontal_alignment = ui::Alignment::Leading,
                               .control              = stacked[i]});
            }

            // Named, not nested: a Border deep inside a Grid is expensive for MSVC to compile. It sits in
            // the page like any other element, so it scrolls and clips with the card.
            ui::Spec popup_spec = ui::Border{.border_thickness     = ui::Thickness{2.0f},
                                             .margin               = ui::Thickness{5.0f},
                                             .min_width            = 50.0f,
                                             .min_height           = 50.0f,
                                             .max_width            = 200.0f,
                                             .horizontal_alignment = ui::Alignment::Leading,
                                             .vertical_alignment   = ui::Alignment::Leading,
                                             .control              = popup_card,
                                             .content = ui::Text{.text     = "I am always right aligned center to "
                                                                             "the target.",
                                                                 .role     = ui::TextRole::Caption,
                                                                 .wrapping = ui::TextWrapping::WrapWholeWords,
                                                                 .margin   = ui::Thickness{6.0f},
                                                                 .vertical_alignment = ui::Alignment::Center}};
            std::unique_ptr<ui::Element> page = ui::build(examples({
                example({
                    .title    = "Use a natural motion composition animation on a UIElement",
                    .code     = R"cpp(
                                ui::Property<std::optional<std::size_t>>   damping{2}; // index into the ratios below
                                ui::Property<float>     period{50.0f};
                                ui::Control<ui::Button> button;

                                ui::VerticalStack{
                                    .spacing = 8,
                                    .content = {
                                        ui::Button{.text = "Item", .width = 100, .height = 50, .control = button},
                                        ui::RadioButtons{
                                            .header         = "Damping Ratio",
                                            .items          = {"0.2", "0.4", "0.6", "0.8"},
                                            .selected_index = damping,
                                        },
                                        ui::Slider{.header = "Period (in ms)", .value = period, .min = 25, .max = 200},
                                    },
                                }

                                // Once built: a spring on the scale, up on hover and back on leaving.
                                const auto spring_to = [&](float scale) {
                                    constexpr std::array Ratios{0.2f, 0.4f, 0.6f, 0.8f};
                                    return ui::SpringAnimation{
                                        .final_value   = ui::Vector3{scale, scale, scale},
                                        .damping_ratio = Ratios[damping.get().value_or(0)],
                                        .period        = ui::Duration{period.get()},
                                    };
                                };
                                button->on_pointer_entered(
                                    [&] { button->start_animation(ui::AnimatedProperty::Scale, spring_to(1.5f)); });
                                button->on_pointer_exited(
                                    [&] { button->start_animation(ui::AnimatedProperty::Scale, spring_to(1.0f)); });
                            )cpp",
                    .stacked  = true,
                    .options  = options_panel(200.0f,
                                              {
                                                  ui::RadioButtons{.header         = "Damping Ratio",
                                                                   .items          = {"0.2", "0.4", "0.6", "0.8"},
                                                                   .selected_index = s.damping},
                                                  ui::Slider{.header         = "Period (in ms)",
                                                             .value          = s.period,
                                                             .min            = 25.0f,
                                                             .max            = 200.0f,
                                                             .tick_frequency = 25.0f,
                                                             .step_frequency = 25.0f},
                                              }),
                    .controls = {paragraph("Hover over the button to animate its scale."),
                                 ui::Button{.text                 = "Item",
                                            .width                = 100.0f,
                                            .height               = 50.0f,
                                            .horizontal_alignment = ui::Alignment::Leading,
                                            .control              = hover_button}},
                }),
                example({
                    .title   = "ExpressionAnimation on an Ellipse element",
                    .code    = R"cpp(
                                    ui::Control<ui::Rectangle> square;
                                    ui::Control<ui::Ellipse>   circle;
                                    const ui::Color            accent = window.theme().accent_ramp.base;

                                    ui::HorizontalStack{
                                        .spacing = 110,
                                        .content = {
                                            ui::Rectangle{
                                                .fill    = accent,
                                                .width   = 50,
                                                .height  = 50,
                                                .control = square,
                                            },
                                            ui::Ellipse{
                                                .fill    = accent,
                                                .width   = 50,
                                                .height  = 50,
                                                .control = circle,
                                            },
                                        },
                                    }

                                    // The square springs on hover ...
                                    square->on_pointer_entered([&] {
                                        square->start_animation(ui::AnimatedProperty::Scale,
                                                                ui::SpringAnimation{.final_value = ui::Vector3{1.5f, 1.5f, 1.5f}});
                                    });
                                    square->on_pointer_exited([&] {
                                        square->start_animation(ui::AnimatedProperty::Scale,
                                                                ui::SpringAnimation{.final_value = ui::Vector3{1.0f, 1.0f, 1.0f}});
                                    });

                                    // ... and the circle's scale is the inverse of the square's, every frame.
                                    circle->start_animation(ui::AnimatedProperty::Scale,
                                                            ui::ExpressionAnimation{ui::Callback<ui::Vector3()>{[&] {
                                                                const ui::Vector3 s = square->current_scale();
                                                                return ui::Vector3{1.0f / s.x, 1.0f / s.y, 1.0f};
                                                            }}});
                                )cpp",
                    .stretch = true,
                    .controls = {ui::VerticalStack{.height = 200.0f,
                                                   .content =
                                                       {
                                                           paragraph("Hover over the square to animate its "
                                                                     "scale. Notice that the ellipse also "
                                                                     "animates.",
                                                                     ui::Thickness{}),
                                                           ui::Text{.text   = "The scale of the circle is "
                                                                              "inversely related to the "
                                                                              "scale of the "
                                                                              "square.",
                                                                    .margin = ui::Edges{.bottom = 12.0f}},
                                                           ui::Grid{.columns            = {1_fr, 1_fr},
                                                                    .rows               = {ui::AutoSize},
                                                                    .vertical_alignment = ui::Alignment::Leading,
                                                                    .content = {ui::Cell{.row    = 0,
                                                                                         .column = 0,
                                                                                         .content = accent_rectangle(50.0f, &square)},
                                                                                ui::Cell{.row    = 0,
                                                                                         .column = 1,
                                                                                         .content = std::move(ellipse)}}},
                                                       }}},
                }),
                example({
                    .title    = "Driving several related animations together using ExpressionAnimation",
                    .code     = R"cpp(
                                std::array<ui::Control<ui::Button>, 4> buttons{};

                                ui::VerticalStack{
                                    .content = {
                                        ui::Button{.text = "Item 1", .margin = ui::Thickness{5}, .width = 100, .height = 50, .control = buttons[0]},
                                        ui::Button{.text = "Item 2", .margin = ui::Thickness{5}, .width = 100, .height = 50, .control = buttons[1]},
                                        ui::Button{.text = "Item 3", .margin = ui::Thickness{5}, .width = 100, .height = 50, .control = buttons[2]},
                                        ui::Button{.text = "Item 4", .margin = ui::Thickness{5}, .width = 100, .height = 50, .control = buttons[3]},
                                    },
                                }

                                // Every button springs its scale on hover, as in the first sample.
                                // Each one's y translation then follows the button above it.
                                for (std::size_t i = 1; i < buttons.size(); ++i)
                                {
                                    ui::ButtonControl* above = buttons[i - 1];
                                    const float        index = static_cast<float>(i);
                                    buttons[i]->start_animation(ui::AnimatedProperty::TranslationY,
                                                                ui::ExpressionAnimation{ui::Callback<float()>{[above, index] {
                                                                    return (above->current_scale().y - 1.0f) * 50.0f +
                                                                           std::fmod(above->current_translation().y, 50.0f * index);
                                                                }}});
                                }
                            )cpp",
                    .controls = {ui::VerticalStack{.margin  = ui::Edges{.bottom = 50.0f},
                                                   .content = std::move(stacked_buttons)}},
                }),
                example({
                    .title    = "Use an element's actual size in an expression animation",
                    .code     = R"cpp(
                                ui::Property<float>   radius{200.0f};
                                ui::Control<ui::Grid> panel;

                                ui::VerticalStack{
                                    .content = {
                                        ui::Grid{.columns = {1_fr}, .rows = {1_fr}, .width = radius, .height = radius, .control = panel},
                                        ui::Slider{.header = "Change radius", .value = radius, .min = 200, .max = 400},
                                    },
                                }

                                // Eight buttons in one cell, each translated onto a circle half the panel wide.
                                constexpr int Total = 8;
                                for (int i = 0; i < Total; ++i)
                                {
                                    ui::ButtonControl* b     = panel->add(std::make_unique<ui::ButtonControl>("Button"), 0, 0);
                                    const float        index = static_cast<float>(i + 1);
                                    b->start_animation(ui::AnimatedProperty::Translation,
                                                       ui::ExpressionAnimation{ui::Callback<ui::Vector3()>{[panel, index] {
                                                           const float r     = panel->actual_size().width / 2.0f;
                                                           const float theta = 0.02f * r + (2.0f * std::numbers::pi_v<float> / Total) * index;
                                                           return ui::Vector3{r * std::cos(theta) + r, r * std::sin(theta), 0.0f};
                                                       }}});
                                }
                            )cpp",
                    .options  = options_panel(200.0f,
                                              {ui::Slider{.header    = "Change radius",
                                                          .value     = s.radius,
                                                          .min       = 200.0f,
                                                          .max       = 400.0f,
                                                          .min_width = 150.0f}}),
                    .controls = {ui::Grid{.columns = {1_fr},
                                          .rows    = {1_fr},
                                          .margin  = ui::Thickness{12.0f},
                                          .width   = s.radius,
                                          .height  = s.radius,
                                          .control = layout_panel}},
                }),
                example({
                    .title = "Position one element next to another by its actual offset and size",
                    .code  = R"cpp(
                                    ui::Property<float>     text_margin{0.0f};
                                    ui::Control<ui::Text>   target;
                                    ui::Control<ui::Border> box;

                                    // The box is a sibling of the text, in the same grid cell.
                                    ui::Grid{
                                        .columns = {1_fr},
                                        .rows    = {ui::AutoSize},
                                        .content   = {
                                            ui::Cell{.content = ui::Text{
                                                 .text                 = "Lorem ipsum dolor sit amet, ...",
                                                 .wrapping             = ui::TextWrapping::WrapWholeWords,
                                                 .margin               = ui::bind(text_margin, [](float m) { return ui::Thickness{m}; }),
                                                 .width                = 300,
                                                 .horizontal_alignment = ui::Alignment::Leading,
                                                 .vertical_alignment   = ui::Alignment::Leading,
                                                 .control              = target,
                                             }},
                                            ui::Cell{.content = ui::Border{
                                                 .horizontal_alignment = ui::Alignment::Leading,
                                                 .vertical_alignment   = ui::Alignment::Leading,
                                                 .control              = box,
                                                 .content = ui::Text{.text = "I am always right aligned center to the target."},
                                             }},
                                        },
                                    }

                                    // Moving or resizing the text moves the box: it stays right of the text, centered on it.
                                    box->start_animation(ui::AnimatedProperty::Translation,
                                                           ui::ExpressionAnimation{ui::Callback<ui::Vector3()>{[target] {
                                                               const ui::Point offset = target->actual_offset();
                                                               const ui::Size  size   = target->actual_size();
                                                               return ui::Vector3{offset.x + size.width,
                                                                                  offset.y + size.height / 2.0f - 25.0f,
                                                                                  0.0f};
                                                           }}});
                                )cpp",
                    .options = options_panel(200.0f,
                                             {
                                                 ui::Slider{.header = "Change font size",
                                                            .value  = s.font_size,
                                                            .min    = 12.0f,
                                                            .max    = 24.0f,
                                                            // It snaps to whole sizes, so it shows them.
                                                            .tick_frequency = 1.0f},
                                                 ui::Slider{.header = "Change text margin", .value = s.text_margin, .min = 0.0f, .max = 100.0f},
                                             }),
                    .controls = {ui::VerticalStack{.content =
                                                       {
                                                           paragraph("This sample positions a box next to a block "
                                                                     "of text whose size depends on its font size. "
                                                                     "Use the sliders to move and resize the text.",
                                                                     ui::Thickness{}),
                                                           ui::Grid{.columns = {1_fr},
                                                                    .rows    = {ui::AutoSize},
                                                                    .margin  = ui::Edges{.top = 12.0f},
                                                                    .content =
                                                                        {
                                                                            ui::Cell{.content = ui::Text{.text = Lorem,
                                                                                                         .wrapping = ui::TextWrapping::WrapWholeWords,
                                                                                                         .margin = ui::bind(s.text_margin, [](float m) { return ui::Thickness{m}; }),
                                                                                                         .width = 300.0f,
                                                                                                         .horizontal_alignment = ui::Alignment::Leading,
                                                                                                         .vertical_alignment = ui::Alignment::Leading,
                                                                                                         .control = target}},
                                                                            // The box is a sibling of the text, not its child.
                                                                            ui::Cell{.content = std::move(popup_spec)},
                                                                        }},
                                                       }}},
                }),
            }));

            // The button springs on hover with the options' damping and period.
            spring_on_hover(*hover_button, s);

            // The ellipse's scale is the inverse of the square's.
            spring_on_hover(*square, s);
            circle->start_animation(ui::AnimatedProperty::Scale,
                                    ui::ExpressionAnimation{ui::Callback<ui::Vector3()>{
                                        [square]
                                        {
                                            const ui::Vector3 sc = square->current_scale();
                                            return ui::Vector3{1.0f / sc.x, 1.0f / sc.y, 1.0f};
                                        }}});

            // Each button's vertical translation follows the one above it,
            // "(above.Scale.Y - 1) * 50 + above.Translation.Y % (50 * index)".
            for (ui::ButtonControl* b : stacked)
            {
                spring_on_hover(*b, s);
            }
            for (std::size_t i = 1; i < stacked.size(); ++i)
            {
                ui::ButtonControl* above = stacked[i - 1];
                const float        index = static_cast<float>(i);
                stacked[i]->start_animation(ui::AnimatedProperty::TranslationY,
                                            ui::ExpressionAnimation{ui::Callback<float()>{
                                                [above, index]
                                                {
                                                    return (above->current_scale().y - 1.0f) * 50.0f +
                                                           std::fmod(above->current_translation().y, 50.0f * index);
                                                }}});
            }

            // Eight buttons around a circle whose radius is half the panel.
            constexpr int Total = 8;
            for (int i = 0; i < Total; ++i)
            {
                auto button = std::make_unique<ui::ButtonControl>("Button");
                // The sample Button's alignment (Left, Center), which a Grid cell honors.
                button->set_horizontal_alignment(ui::Alignment::Leading);
                button->set_vertical_alignment(ui::Alignment::Center);
                button->set_accessibility(ui::Accessibility{.name = std::format("Button {}", i)});
                ui::ButtonControl* b     = layout_panel->add(std::move(button), 0, 0);
                const float        index = static_cast<float>(i + 1);
                b->start_animation(ui::AnimatedProperty::Translation,
                                   ui::ExpressionAnimation{ui::Callback<ui::Vector3()>{
                                       [layout_panel, index]
                                       {
                                           const float radius = layout_panel->actual_size().width / 2.0f;
                                           const float theta  = 0.02f * radius +
                                                                ((2.0f * std::numbers::pi_v<float>) / Total) * index;
                                           return ui::Vector3{radius * std::cos(theta) + radius,
                                                              radius * std::sin(theta) + 0.0f,
                                                              0.0f};
                                       }}});
            }

            // The box sits right of the text, centered on it, whatever size and margin the text has.
            popup_card->set_background(flyout_background);
            popup_card->set_border_brush(ChromeGray);
            target->add_subscription(s.font_size.observe(
                [target, body](const float& size)
                {
                    ui::TextStyle style = body;
                    style.font_size     = size;
                    target->set_text_style(style);
                }));
            popup_card->start_animation(ui::AnimatedProperty::Translation,
                                        ui::ExpressionAnimation{ui::Callback<ui::Vector3()>{
                                            [target]
                                            {
                                                const ui::Point offset = target->actual_offset();
                                                const ui::Size  size   = target->actual_size();
                                                return ui::Vector3{offset.x + size.width,
                                                                   offset.y + size.height / 2.0f - 25.0f,
                                                                   0.0f};
                                            }}});
            return ui::Spec{std::move(page)};
        });
}

std::span<const Page> motion_pages()
{
    static const auto Pages = std::to_array<Page>({
        {"Animation Interop",
         "Composition interop allows you to animate elements using expressions, natural animations, and more.",
         animation_interop_page},
        {"Connected Animation",
         "Connected animations continue elements during page navigation and help the user maintain their context "
         "between views.",
         connected_animation_page},
        {"Easing Functions", "Easing is a way to manipulate the velocity of an object as it animates.", easing_functions_page},
        {"Implicit Transitions",
         "Use Implicit Transitions to automatically animate changes to properties.",
         implicit_transitions_page},
        {"Page Transitions",
         "Page transitions provide visual feedback about the relationship between pages.",
         page_transitions_page},
        {"Element Transitions", "Animations that play when an element enters, moves or leaves.", element_transitions_page},
        {"Parallax", "A container control that provides the parallax effect when scrolling.", parallax_view_page},
    });
    return Pages;
}

} // namespace gallery
