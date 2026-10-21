// SPDX-License-Identifier: MIT
#include "pages_motion.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <cmath>
#include <cstddef>

using ui::operator""_fr;

namespace gallery
{
namespace
{
// ---- Easing Functions ----

struct EasingState
{
    ui::Property<std::optional<double>>      accelerate_exponent{4.5};
    ui::Property<std::optional<double>>      decelerate_exponent{7.0};
    ui::Property<std::optional<std::size_t>> function{0};
    ui::Property<std::optional<std::size_t>> mode{0}; // EaseOut, EaseIn, EaseInOut
    ThemedShape*                             standard   = nullptr;
    ThemedShape*                             accelerate = nullptr;
    ThemedShape*                             decelerate = nullptr;
    ThemedShape*                             other      = nullptr;
};

// The named easing functions of the EasingFunction sample, in its order.
constexpr std::array<const char*, 11>
    EasingNames{"BackEase",
                "BounceEase",
                "CircleEase",
                "CubicEase",
                "ElasticEase",
                "ExponentialEase",
                "PowerEase",
                "QuadraticEase",
                "QuarticEase",
                "QuinticEase",
                "SineEase"};

ui::EasingFunction named_easing(std::size_t index, ui::EasingMode mode)
{
    switch (index)
    {
        case 0:
            return ui::BackEase{.mode = mode};
        case 1:
            return ui::BounceEase{.mode = mode};
        case 2:
            return ui::CircleEase{mode};
        case 3:
            return ui::CubicEase{mode};
        case 4:
            return ui::ElasticEase{.mode = mode};
        case 5:
            return ui::ExponentialEase{.mode = mode};
        case 6:
            return ui::PowerEase{.mode = mode};
        case 7:
            return ui::QuadraticEase{mode};
        case 8:
            return ui::QuarticEase{mode};
        case 9:
            return ui::QuinticEase{mode};
        default:
            return ui::SineEase{mode};
    }
}

// Button*_Click: from where the rectangle is to 200, or back to 0 from anywhere past it.
void slide(ui::Element* rect, ui::Duration duration, ui::EasingFunction easing)
{
    const float x = rect->current_translation().x;
    rect->start_animation(ui::AnimatedProperty::TranslationX,
                          ui::DoubleAnimation{.from = x, .to = x > 0.0f ? 0.0f : 200.0f, .duration = duration, .easing = easing});
}

// A card's content: the Animate button and the rectangle it moves, in a Grid of an Auto and a
// star column of at least 300.
ui::Spec animate_row(const char* name, ThemedShape** rect, ui::Callback<void()> animate)
{
    return ui::Grid{.columns = {ui::AutoSize, 1_fr},
                    .rows    = {ui::AutoSize},
                    .content =
                        {ui::Cell{.column  = 0,
                                  .content = ui::Button{.text                 = "Animate",
                                                        .horizontal_alignment = ui::Alignment::Leading,
                                                        .vertical_alignment   = ui::Alignment::Center,
                                                        .accessibility        = {.name = name},
                                                        .on_click             = std::move(animate)}},
                         // The column's minimum width of 300: a star column has no minimum here,
                         // so the cell's content carries it.
                         ui::Cell{.column = 1,
                                  .content = ui::Grid{.columns   = {1_fr},
                                                      .rows      = {ui::AutoSize},
                                                      .min_width = 300.0f,
                                                      .content = {ui::Cell{.content = accent_rectangle(50.0f, rect)}}}}}};
}

} // namespace

ui::Spec easing_functions_page(Context&)
{
    return with_state<EasingState>(
        [](EasingState& s)
        {
            std::unique_ptr<ui::Element> page = ui::build(
                ui::VerticalStack{
                    .spacing = 10.0f,
                    .content =
                        {
                            ui::VerticalStack{.content =
                                                  {
                                                      paragraph("- Use the Standard easing function for animating "
                                                                "general property changes.",
                                                                ui::Thickness{}),
                                                      paragraph("- Use the Accelerate easing function to animate "
                                                                "objects that are exiting "
                                                                "the scene.",
                                                                ui::Thickness{}),
                                                      paragraph("- Use the Decelerate easing function to animate "
                                                                "objects that are entering "
                                                                "the scene.",
                                                                ui::Thickness{}),
                                                  }},
                            examples(
                                {
                                    example(
                                        {
                                            .title    = "Standard Easing Function",
                                            .code     = R"cpp(
                                                ui::Control<ui::Rectangle> rect;
                                                const ui::Color            accent = window.theme().accent_ramp.base;

                                                ui::HorizontalStack{
                                                    .spacing = 12,
                                                    .content = {
                                                        ui::Button{
                                                            .text     = "Animate",
                                                            .on_click = [&] {
                                                                // To 200, or back to 0 when it is already out there.
                                                                const float x = rect->current_translation().x;
                                                                rect->start_animation(ui::AnimatedProperty::TranslationX,
                                                                                      ui::DoubleAnimation{
                                                                                          .from        = x,
                                                                                          .to          = x > 0.0f ? 0.0f : 200.0f,
                                                                                          .duration    = 500ms,
                                                                                          .easing      = ui::CircleEase{ui::EasingMode::EaseInOut},
                                                                                      });
                                                            },
                                                        },
                                                        ui::Rectangle{
                                                            .fill    = accent,
                                                            .width   = 50,
                                                            .height  = 50,
                                                            .control = rect,
                                                        },
                                                    },
                                                }
                                            )cpp",
                                            .stretch  = true,
                                            .controls = {animate_row("Animate rectangle using Standard Easing Function",
                                                                     &s.standard,
                                                                     [&s]
                                                                     {
                                                                         slide(s.standard,
                                                                               ui::Duration{500.0},
                                                                               ui::CircleEase{ui::EasingMode::EaseInOut});
                                                                     })},
                                        }),
                                    example(
                                        {
                                            .title   = "Accelerate Easing Function",
                                            .code    = R"cpp(
                                                ui::Property<std::optional<double>> exponent{4.5};
                                                ui::Control<ui::Rectangle> rect;
                                                const ui::Color            accent = window.theme().accent_ramp.base;

                                                ui::HorizontalStack{
                                                    .spacing = 12,
                                                    .content = {
                                                        ui::Button{
                                                            .text     = "Animate",
                                                            .on_click = [&] {
                                                                // EaseIn speeds up toward the end: for what leaves the scene.
                                                                const float x = rect->current_translation().x;
                                                                rect->start_animation(ui::AnimatedProperty::TranslationX,
                                                                                      ui::DoubleAnimation{
                                                                                          .from        = x,
                                                                                          .to          = x > 0.0f ? 0.0f : 200.0f,
                                                                                          .duration    = 150ms,
                                                                                          .easing      = ui::ExponentialEase{
                                                                                              .exponent = static_cast<float>(exponent.get().value_or(0.0)),
                                                                                              .mode     = ui::EasingMode::EaseIn,
                                                                                          },
                                                                                      });
                                                            },
                                                        },
                                                        ui::Rectangle{
                                                            .fill    = accent,
                                                            .width   = 50,
                                                            .height  = 50,
                                                            .control = rect,
                                                        },
                                                        ui::NumberBox{.header = "Exponent", .value = exponent},
                                                    },
                                                }
                                            )cpp",
                                            .stretch = true,
                                            .options = options_panel(200.0f,
                                                                     {ui::NumberBox{.header = "Exponent",
                                                                                    .value = s.accelerate_exponent,
                                                                                    .accessibility =
                                                                                        {.name = "Accelerate easing "
                                                                                                 "exponent"}}}),
                                            .controls = {animate_row("Animate rectangle using Accelerate Easing "
                                                                     "Function",
                                                                     &s.accelerate,
                                                                     [&s]
                                                                     {
                                                                         const double e = s.accelerate_exponent.get().value_or(0.0);
                                                                         slide(s.accelerate,
                                                                               ui::Duration{150.0},
                                                                               ui::ExponentialEase{static_cast<float>(e),
                                                                                                   ui::EasingMode::EaseIn});
                                                                     })},
                                        }),
                                    example(
                                        {
                                            .title   = "Decelerate Easing Function",
                                            .code    = R"cpp(
                                                ui::Property<std::optional<double>> exponent{7.0};
                                                ui::Control<ui::Rectangle> rect;
                                                const ui::Color            accent = window.theme().accent_ramp.base;

                                                ui::HorizontalStack{
                                                    .spacing = 12,
                                                    .content = {
                                                        ui::Button{
                                                            .text     = "Animate",
                                                            .on_click = [&] {
                                                                // EaseOut slows down into the end: for what enters the scene.
                                                                const float x = rect->current_translation().x;
                                                                rect->start_animation(ui::AnimatedProperty::TranslationX,
                                                                                      ui::DoubleAnimation{
                                                                                          .from        = x,
                                                                                          .to          = x > 0.0f ? 0.0f : 200.0f,
                                                                                          .duration    = 300ms,
                                                                                          .easing      = ui::ExponentialEase{
                                                                                              .exponent = static_cast<float>(exponent.get().value_or(0.0)),
                                                                                              .mode     = ui::EasingMode::EaseOut,
                                                                                          },
                                                                                      });
                                                            },
                                                        },
                                                        ui::Rectangle{
                                                            .fill    = accent,
                                                            .width   = 50,
                                                            .height  = 50,
                                                            .control = rect,
                                                        },
                                                        ui::NumberBox{.header = "Exponent", .value = exponent},
                                                    },
                                                }
                                            )cpp",
                                            .stretch = true,
                                            .options = options_panel(200.0f,
                                                                     {ui::NumberBox{.header = "Exponent",
                                                                                    .value = s.decelerate_exponent,
                                                                                    .accessibility =
                                                                                        {.name = "Decelerate easing "
                                                                                                 "exponent"}}}),
                                            .controls = {animate_row("Animate rectangle using Decelerate Easing "
                                                                     "Function",
                                                                     &s.decelerate,
                                                                     [&s]
                                                                     {
                                                                         const double e = s.decelerate_exponent.get().value_or(0.0);
                                                                         slide(s.decelerate,
                                                                               ui::Duration{300.0},
                                                                               ui::ExponentialEase{static_cast<float>(e),
                                                                                                   ui::EasingMode::EaseOut});
                                                                     })},
                                        }),
                                    example(
                                        {
                                            .title   = "Other Easing Functions",
                                            .code    = R"cpp(
                                                ui::Property<std::optional<std::size_t>>  function{0};
                                                ui::Property<std::optional<std::size_t>>  mode{0}; // EaseOut, EaseIn, EaseInOut
                                                ui::Control<ui::Rectangle> rect;
                                                const ui::Color            accent = window.theme().accent_ramp.base;

                                                // Any of the eleven easing structs is a ui::EasingFunction.
                                                const auto easing = [&]() -> ui::EasingFunction {
                                                    const auto m = static_cast<ui::EasingMode>(mode.get().value_or(0));
                                                    switch (function.get().value_or(0))
                                                    {
                                                        case 0:  return ui::BackEase{.mode = m};
                                                        case 1:  return ui::BounceEase{.mode = m};
                                                        case 2:  return ui::CircleEase{m};
                                                        // ... CubicEase, ElasticEase, ExponentialEase, PowerEase,
                                                        //     QuadraticEase, QuarticEase, QuinticEase ...
                                                        default: return ui::SineEase{m};
                                                    }
                                                };

                                                ui::VerticalStack{
                                                    .spacing = 8,
                                                    .content = {
                                                        ui::ComboBox{
                                                            .items          = {"BackEase", "BounceEase", "CircleEase", /* ... */ "SineEase"},
                                                            .selected_index = function,
                                                        },
                                                        ui::RadioButtons{
                                                            .items          = {"EaseOut", "EaseIn", "EaseInOut"},
                                                            .selected_index = mode,
                                                        },
                                                        ui::Button{
                                                            .text     = "Animate",
                                                            .on_click = [&] {
                                                                const float x = rect->current_translation().x;
                                                                rect->start_animation(ui::AnimatedProperty::TranslationX,
                                                                                      ui::DoubleAnimation{
                                                                                          .from        = x,
                                                                                          .to          = x > 0.0f ? 0.0f : 200.0f,
                                                                                          .duration    = 500ms,
                                                                                          .easing      = easing(),
                                                                                      });
                                                            },
                                                        },
                                                        ui::Rectangle{
                                                            .fill    = accent,
                                                            .width   = 50,
                                                            .height  = 50,
                                                            .control = rect,
                                                        },
                                                    },
                                                }
                                            )cpp",
                                            .stretch = true,
                                            .options = options_panel(200.0f,
                                                                     {
                                                                         ui::ComboBox{.items = std::vector<std::string>(EasingNames
                                                                                                                            .begin(),
                                                                                                                        EasingNames
                                                                                                                            .end()),
                                                                                      .selected_index = s.function,
                                                                                      .accessibility = {.name = "Easing"
                                                                                                                " typ"
                                                                                                                "e"}},
                                                                         ui::RadioButtons{.items = {"EaseOut",
                                                                                                    "EaseIn",
                                                                                                    "EaseInOut"},
                                                                                          .selected_index = s.mode},
                                                                     }),
                                            .controls = {animate_row("Animate rectangle using an Easing Function",
                                                                     &s.other,
                                                                     [&s]
                                                                     {
                                                                         static constexpr std::array<ui::EasingMode, 3>
                                                                             Modes{ui::EasingMode::EaseOut,
                                                                                    ui::EasingMode::EaseIn,
                                                                                    ui::EasingMode::EaseInOut};
                                                                         const auto mode = Modes[static_cast<std::size_t>(
                                                                             std::min<std::size_t>(s.mode.get().value_or(0), 2))];
                                                                         slide(s.other,
                                                                               ui::Duration{500.0},
                                                                               named_easing(s.function.get().value_or(0), mode));
                                                                     })},
                                        }),
                                }),
                        },
                });
            return ui::Spec{std::move(page)};
        });
}

namespace
{
// ---- Page Transitions ----

struct PageTransitionState
{
    ui::Control<ui::NavigationFrame>         frame;
    ui::Property<std::optional<std::size_t>> mode{0};
    std::optional<ui::PageTransition>        info{};
};

// TransitionRadioButton_Checked's choices. "Common" (a 3D turnstile) is not among them.
std::optional<ui::PageTransition> transition_of(std::size_t mode)
{
    switch (mode)
    {
        case 1:
            return ui::RisePageTransition{};
        case 2:
            return ui::ZoomPageTransition{};
        case 3:
            return ui::NoPageTransition{};
        case 4:
            return ui::SlidePageTransition{ui::PageSlideDirection::FromRight};
        case 5:
            return ui::SlidePageTransition{ui::PageSlideDirection::FromLeft};
        case 6:
            return ui::FadeThroughPageTransition{};
        default:
            return std::nullopt; // Default: the frame's own NavigationTransition
    }
}

} // namespace

ui::Spec page_transitions_page(Context&)
{
    return with_state<PageTransitionState>(
        [](PageTransitionState& s)
        {
            std::unique_ptr<ui::Element> page = ui::build(example({
                .title   = "Page transitions",
                .code    = R"cpp(
                    ui::Control<ui::NavigationFrame> frame;

                    // The frame's own transition animates a navigation that names none.
                    ui::VerticalStack{
                        .spacing = 8,
                        .content = {
                            ui::NavigationFrame{
                                .content_transitions = {ui::NavigationTransition{}},
                                .min_height          = 600,
                                .control             = frame,
                            },
                            ui::Button{
                                .text     = "Navigate Forward",
                                .on_click = [&] {
                                    // Or Rise, Zoom, No or FadeThrough PageTransition; std::nullopt for the frame's default.
                                    frame->navigate(
                                        [] { return ui::build(ui::Text{.text = "Page 2"}); },
                                        ui::SlidePageTransition{.direction = ui::PageSlideDirection::FromRight});
                                },
                            },
                            ui::Button{.text = "Navigate Backward", .on_click = [&] { frame->go_back(); }},
                        },
                    }

                    frame->navigate([] { return ui::build(ui::Text{.text = "Page 1"}); });
                )cpp",
                .stretch = true,
                .options = ui::VerticalStack{
                    .child_alignment = ui::Alignment::Stretch,
                    .width       = 220.0f,
                    .content =
                        {
                            ui::RadioButtons{.header = "Transition modes",
                                             .items  = {"Default",
                                                       "Rise",
                                                       "Zoom",
                                                       "None",
                                                       "Slide from Right",
                                                       "Slide from Left",
                                                       "Fade through"},
                                             .selected_index       = s.mode},
                            ui::Text{.text = "Navigate", .margin = ui::Edges{.top = 12.0f, .bottom = 8.0f}, .horizontal_alignment = ui::Alignment::Leading},
                            ui::Button{.text     = "Navigate Forward",
                                       .margin   = ui::Edges{.bottom = 4.0f},
                                       .horizontal_alignment  = ui::Alignment::Stretch,
                                       .on_click =
                                           [&s]
                                           {
                                               // ForwardButton1_Click: page 1 and page 2 by turns.
                                               const int next = s.frame->back_stack_depth() % 2 == 1 ? 1 : 2;
                                               s.frame->navigate([next] { return ui::build(navigation_sample_page(next)); },
                                                                 transition_of(s.mode.get().value_or(0)));
                                           }},
                            ui::Button{.text     = "Navigate Backward",
                                       .horizontal_alignment  = ui::Alignment::Stretch,
                                       .on_click = [&s] { s.frame->go_back(); }},
                        }},
                .controls = {ui::NavigationFrame{.content_transitions = {ui::NavigationTransition{}},
                                       .min_height          = 600.0f,
                                       .horizontal_alignment             = ui::Alignment::Stretch,
                                       .control                 = s.frame}},
            }));
            s.frame->navigate([] { return ui::build(navigation_sample_page(1)); });
            return ui::Spec{std::move(page)};
        });
}

} // namespace gallery
