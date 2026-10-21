// SPDX-License-Identifier: MIT
#include "pages_motion.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <cstdint>

using ui::operator""_fr;

namespace gallery
{
namespace
{
// ---- Element Transitions ----

struct ElementTransitionState
{
    ui::Control<ui::HorizontalStack>       entrance;
    ui::Control<ui::Rectangle>             middle;
    ui::Control<ui::ListView<std::string>> content_list;
    ui::Control<ui::ListView<std::string>> add_remove;
    int                                    item_count = 10;
    ui::Control<ui::Popup>                 popup;
    ui::Control<ui::Button>                show_popup;
    ui::Control<ui::Button>                close_popup;
};

std::vector<std::string> numbered(const char* prefix, int count)
{
    std::vector<std::string> items;
    for (int i = 0; i < count; ++i)
    {
        items.push_back(std::format("{}{}", prefix, i));
    }
    return items;
}

std::unique_ptr<ui::Element> light_blue_square()
{
    return ui::build(rectangle(LightBlue, 50.0f, 50.0f, ui::Thickness{5.0f}));
}

} // namespace

ui::Spec element_transitions_page(Context& ctx)
{
    ui::Window& window = ctx.window;
    return with_state<ElementTransitionState>(
        [&window](ElementTransitionState& s)
        {
            ui::SpecList squares;
            for (int i = 0; i < 5; ++i)
            {
                squares.nodes.push_back(light_blue_square());
            }
            ui::Control<ui::Rectangle> right;
            auto popup_shape = std::make_unique<ThemedShape>(ThemedShape::Kind::Ellipse, flyout_background, 200.0f, 200.0f);
            popup_shape->set_stroke(chrome_gray, 2.0f);

            // Built on its own: nested in the card's Grid below, MSVC's time and memory for the
            // expression grow with every level of Grid cells (clang is unaffected).
            ui::Spec popup_content = ui::Grid{
                                         .columns = {ui::AutoSize},
                                         .rows    = {ui::AutoSize},
                                         .content =
                                             {
                                                 ui::Cell{.content = std::move(popup_shape)},
                                                 ui::Cell{.content = ui::VerticalStack{
                                                      .margin  = ui::Thickness{24.0f},
                                                      .width   = 200.0f,
                                                      .vertical_alignment = ui::Alignment::Center,
                                                      .content =
                                                          {
                                                              ui::Text{.text = "This is a popup using PopupTransition",
                                                                       .wrapping       = ui::TextWrapping::WrapWholeWords,
                                                                       .text_alignment = ui::TextAlignment::Center,
                                                                       .margin         = ui::Thickness{12.0f},
                                                                       .horizontal_alignment        = ui::Alignment::Center},
                                                              ui::Button{.text     = "Close Popup",
                                                                         .horizontal_alignment  = ui::Alignment::Center,
                                                                         .on_click =
                                                                             [&s, &window]
                                                                             {
                                                                                 s.popup->set_open(false);
                                                                                 window.focus_element(s.show_popup);
                                                                             },
                                                                         .control = s.close_popup},
                                                          }}},
                                             }};

            std::unique_ptr<ui::Element> page = ui::build(examples({
                example({
                    .title   = "Use the EntranceTransition when adding items to your page.",
                    .code    = R"cpp(
                        ui::Control<ui::HorizontalStack> panel;

                        const auto square = [] {
                            return ui::build(ui::Rectangle{
                                .fill   = ui::Color::from_argb(0xFFADD8E6),
                                .margin = ui::Thickness{5},
                                .width  = 50,
                                .height = 50,
                            });
                        };

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {
                                // Every child slides and fades in as it enters; the ones entering together stagger.
                                ui::HorizontalStack{
                                    .children_transitions = {ui::EntranceTransition{.stagger = true}},
                                    .control              = panel,
                                    .content              = {square(), square(), square()},
                                },
                                ui::Button{.text = "Add one", .on_click = [&] { panel->add(square()); }},
                                ui::Button{.text = "Clear all", .on_click = [&] { panel->clear(); }},
                            },
                        }
                    )cpp",
                    .options = ui::VerticalStack{
                        .child_alignment = ui::Alignment::Stretch,
                        .width       = 160.0f,
                        .content =
                            {
                                ui::Button{.text     = "Add one",
                                           .horizontal_alignment  = ui::Alignment::Stretch,
                                           .on_click = [&s] { s.entrance->add(light_blue_square()); }},
                                ui::Button{.text     = "Add five",
                                           .horizontal_alignment  = ui::Alignment::Stretch,
                                           .on_click =
                                               [&s]
                                               {
                                                   for (int i = 0; i < 5; ++i)
                                                   {
                                                       s.entrance->add(light_blue_square());
                                                   }
                                               }},
                                ui::Button{.text     = "Clear all",
                                           .horizontal_alignment  = ui::Alignment::Stretch,
                                           .on_click = [&s] { s.entrance->clear(); }},
                            }},
                    .controls = {ui::HorizontalStack{.children_transitions = {ui::EntranceTransition{.stagger = true}},
                                                .control                  = s.entrance,
                                                .content = std::move(squares)}},
                }),
                example({
                    .title    = "Use the RepositionTransition to react to layout changes.",
                    .code     = R"cpp(
                        ui::Control<ui::Rectangle> middle;
                        ui::Control<ui::Rectangle> right;

                        const auto square = [](std::uint32_t argb, ui::Control<ui::Rectangle>* out = nullptr) {
                            return ui::Rectangle{
                                .fill    = ui::Color::from_argb(argb),
                                .margin  = ui::Thickness{5},
                                .width   = 75,
                                .height  = 75,
                                .control = out,
                            };
                        };

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {
                                ui::HorizontalStack{
                                    .content = {square(0xFFFF0000), square(0xFF008000, &middle), square(0xFF0000FF, &right)},
                                },
                                ui::Button{
                                    .text     = "Reposition",
                                    .on_click = [&] {
                                        middle->set_visibility(middle->visibility() == ui::Visibility::Visible
                                                                   ? ui::Visibility::Collapsed
                                                                   : ui::Visibility::Visible);
                                    },
                                },
                            },
                        }

                        // Once built: when layout moves the right square, it glides to its new place.
                        right->set_transitions({ui::RepositionTransition{}});
                    )cpp",
                    .options  = ui::Button{.text     = "Reposition",
                                          .on_click =
                                              [&s]
                                              {
                                                  s.middle->set_visibility(s.middle->visibility() == ui::Visibility::Visible
                                                                               ? ui::Visibility::Collapsed
                                                                               : ui::Visibility::Visible);
                                              }},
                    .controls = {ui::Grid{.columns = {ui::AutoSize, ui::AutoSize, ui::AutoSize},
                                          .rows    = {ui::AutoSize},
                                          .content   = {ui::Cell{.column = 0, .content = rectangle(Red, 75.0f, 75.0f, ui::Thickness{5.0f})},
                                                      ui::Cell{.column  = 1,
                                                       .content = rectangle(Green, 75.0f, 75.0f, ui::Thickness{5.0f}, &s.middle)},
                                                      ui::Cell{.column  = 2,
                                                       .content = rectangle(Blue, 75.0f, 75.0f, ui::Thickness{5.0f}, &right)}}}},
                }),
                example({
                    .title    = "Use ContentTransition to animate content refreshes.",
                    .code     = R"cpp(
                        ui::Control<ui::ListView<std::string>> list;

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {
                                // Replaced rows fade out and the new ones slide in.
                                ui::ListView<std::string>{
                                    .items                      = {"Item 0", "Item 1", "Item 2", "Item 3", "Item 4"},
                                    .item_transitions = {ui::ContentTransition{}},
                                    .control                    = list,
                                },
                                ui::Button{
                                    .text     = "Refresh data",
                                    .on_click = [&] { list->set_items({"Updated content 0", "Updated content 1", "Updated content 2"}); },
                                },
                            },
                        }
                    )cpp",
                    .options  = ui::Button{.text     = "Refresh data",
                                          .on_click = [&s] { s.content_list->set_items(numbered("Updated content ", 5)); }},
                    .controls = {ui::ListView<std::string>{.items                      = numbered("Item ", 5),
                                              .item_transitions = {ui::ContentTransition{}},
                                              .control                        = s.content_list}},
                }),
                example({
                    .title   = "Use AddRemoveTransition to animate adding and removing items from a collection.",
                    .code    = R"cpp(
                        ui::Control<ui::ListView<std::string>> list;
                        int                                    next = 3;

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {
                                // An added row grows in once the others have made room; a deleted one
                                // shrinks out and the rest close the gap.
                                ui::ListView<std::string>{
                                    .items                      = {"Item 0", "Item 1", "Item 2"},
                                    .item_transitions = {ui::AddRemoveTransition{}},
                                    .control                    = list,
                                },
                                ui::Button{
                                    .text     = "Add",
                                    .on_click = [&] { list->add_item(std::format("New Item {}", next++)); },
                                },
                                ui::Button{
                                    .text     = "Delete",
                                    .on_click = [&] {
                                        if (!list->items().empty())
                                        {
                                            list->remove_item(0);
                                        }
                                    },
                                },
                            },
                        }
                    )cpp",
                    .options = ui::VerticalStack{
                        .child_alignment = ui::Alignment::Stretch,
                        .width       = 160.0f,
                        .content =
                            {
                                ui::Button{.text     = "Add",
                                           .horizontal_alignment  = ui::Alignment::Stretch,
                                           .on_click = [&s] { s.add_remove->add_item(std::format("New Item {}", s.item_count++)); }},
                                ui::Button{.text     = "Delete",
                                           .horizontal_alignment  = ui::Alignment::Stretch,
                                           .on_click =
                                               [&s]
                                               {
                                                   if (!s.add_remove->items().empty())
                                                   {
                                                       s.add_remove->remove_item(0);
                                                   }
                                               }},
                                ui::Button{.text     = "Add and Del",
                                           .horizontal_alignment  = ui::Alignment::Stretch,
                                           .on_click =
                                               [&s]
                                               {
                                                   s.add_remove->add_item(std::format("New Item {}", s.item_count++));
                                                   if (!s.add_remove->items().empty())
                                                   {
                                                       s.add_remove->remove_item(0);
                                                   }
                                               }},
                            }},
                    .controls = {ui::ListView<std::string>{.items                      = numbered("Item ", 10),
                                              .item_transitions = {ui::AddRemoveTransition{}},
                                              .control                        = s.add_remove}},
                }),
                example({
                    .title    = "Use PopupTransition to animate opening and closing a popup.",
                    .code     = R"cpp(
                        ui::Control<ui::Popup> popup;

                        ui::Grid{
                            .columns = {ui::AutoSize},
                            .rows    = {ui::AutoSize},
                            .content   = {
                                ui::Cell{.content = ui::Button{.text = "Show Popup", .on_click = [&] { popup->set_open(true); }}},
                                // The child slides and fades in on opening and fades out on closing.
                                ui::Cell{.content = ui::Popup{
                                     .child_transitions = {ui::PopupTransition{}},
                                     .control           = popup,
                                     .content           = ui::VerticalStack{
                                         .margin  = ui::Thickness{24},
                                         .width   = 200,
                                         .content = {
                                             ui::Text{
                                                 .text     = "This is a popup using PopupTransition",
                                                 .wrapping = ui::TextWrapping::WrapWholeWords,
                                             },
                                             ui::Button{.text = "Close Popup", .on_click = [&] { popup->set_open(false); }},
                                         },
                                     },
                                 }},
                            },
                        }
                    )cpp",
                    .controls = {ui::Grid{
                        .columns = {ui::AutoSize},
                        .rows    = {ui::AutoSize},
                        .content =
                            {
                                ui::Cell{.content = ui::Button{.text     = "Show Popup",
                                                       .horizontal_alignment  = ui::Alignment::Leading,
                                                       .vertical_alignment  = ui::Alignment::Leading,
                                                       .on_click =
                                                           [&s, &window]
                                                           {
                                                               s.popup->set_open(true);
                                                               window.focus_element(s.close_popup);
                                                           },
                                                       .control = s.show_popup}},
                                ui::Cell{.content = ui::Popup{
                                     .child_transitions = {ui::PopupTransition{}},
                                     .margin            = ui::Thickness{-75.0f},
                                     .control               = s.popup,
                                     .content           = std::move(popup_content)}},
                            }}},
                }),
            }));
            right->set_transitions({ui::RepositionTransition{}});
            return ui::Spec{std::move(page)};
        });
}

namespace
{
// ---- Parallax ----

// The parallax list's items: every control page, by title.
std::vector<std::string> all_titles()
{
    std::vector<std::string> titles;
    for (const Group& group : groups())
    {
        for (const Page& p : group.pages)
        {
            titles.emplace_back(p.name);
        }
    }
    std::sort(titles.begin(), titles.end());
    return titles;
}

// White in the light theme, black in the dark.
ui::Color page_100(const ui::Theme& t)
{
    return t.dark ? ui::colors::Black : ui::colors::White;
}

// The sample's cliff photo, drawn: a tall photo to parallax.
const ui::Bitmap& cliff_photo()
{
    return sample_photo(3);
}

} // namespace

ui::Spec parallax_view_page(Context&)
{
    ui::Control<ui::Parallax>              list_parallax;
    ui::Control<ui::Parallax>              scroll_parallax;
    ui::Control<ui::ListView<std::string>> list;
    ui::Control<ui::Scroll>                scroll;

    static constexpr std::array<ui::Color, 19>
                 Colors{ui::Color::from_argb(0xFFF0F8FF),
                        ui::Color::from_argb(0xFFFAEBD7),
                        ui::Color::from_argb(0xFF00FFFF),
                        ui::Color::from_argb(0xFF7FFFD4),
                        ui::Color::from_argb(0xFFF0FFFF),
                        ui::Color::from_argb(0xFFF5F5DC),
                        ui::Color::from_argb(0xFFFFE4C4),
                        ui::Color::from_argb(0xFFFFEBCD),
                        ui::Color::from_argb(0xFF8A2BE2),
                        ui::Color::from_argb(0xFFA52A2A),
                        ui::Color::from_argb(0xFFDEB887),
                        ui::Color::from_argb(0xFF5F9EA0),
                        ui::Color::from_argb(0xFF7FFF00),
                        ui::Color::from_argb(0xFFD2691E),
                        ui::Color::from_argb(0xFFFF7F50),
                        ui::Color::from_argb(0xFF6495ED),
                        ui::Color::from_argb(0xFFFFF8DC),
                        ui::Color::from_argb(0xFFDC143C),
                        ui::Color::from_argb(0xFF00FFFF)};
    ui::SpecList rectangles;
    for (const ui::Color c : Colors)
    {
        rectangles.push_back(ui::Rectangle{.fill = c, .height = 150.0f});
    }

    std::unique_ptr<ui::Element> page = ui::build(examples({
        example({
            .title   = "Parallax on a ListView",
            .code    = R"cpp(
                ui::Control<ui::Parallax>          parallax;
                ui::Control<ui::ListView<std::string>> list;

                // The picture sits behind the list, in the same grid cell. `photo` is a ui::Bitmap.
                ui::Grid{
                    .columns = {1_fr},
                    .rows    = {1_fr},
                    .height  = 700,
                    .content   = {
                        ui::Cell{.content = ui::Parallax{
                             .vertical_shift = 500,
                             .control        = parallax,
                             .content        = ui::Image{.source = photo},
                         }},
                        ui::Cell{.content = ui::ListView<std::string>{.items = titles, .control = list}},
                    },
                }

                // Once built: the picture shifts as the list scrolls.
                parallax->set_source(list);
            )cpp",
            .stretch = true,
            .controls =
                {ui::Grid{.columns = {1_fr},
                          .rows    = {1_fr},
                          .height  = 700.0f,
                          .content = {ui::Cell{.content = ui::Parallax{.vertical_shift       = 500.0f,
                                                                       .horizontal_alignment = ui::Alignment::Leading,
                                                                       .vertical_alignment   = ui::Alignment::Leading,
                                                                       .control              = list_parallax,
                                                                       .content = ui::Image{.source = cliff_photo()}}},
                                      ui::Cell{.content = ui::ListView<std::string>{.items = all_titles(),
                                                                                    .vertical_alignment = ui::Alignment::Leading,
                                                                                    .accessibility = {.name = "all "
                                                                                                              "sample"
                                                                                                              "s"},
                                                                                    .control       = list}}}}},
        }),
        example({
            .title   = "Parallax with a Scroll",
            .code    = R"cpp(
                ui::Control<ui::Parallax> parallax;
                ui::Control<ui::Scroll>   scroll;

                // The picture sits behind the scroller, in the same grid cell. `photo` is a ui::Bitmap.
                ui::Grid{
                    .columns = {1_fr},
                    .rows    = {1_fr},
                    .height  = 700,
                    .content   = {
                        ui::Cell{.content = ui::Parallax{
                             .vertical_shift = 500,
                             .control        = parallax,
                             .content        = ui::Image{.source = photo},
                         }},
                        ui::Cell{.content = ui::Scroll{
                             .width   = 150,
                             .control = scroll,
                             .content = ui::VerticalStack{
                                 .content = {
                                     ui::Rectangle{.fill = ui::Color::from_argb(0xFFF0F8FF), .height = 150},
                                     ui::Rectangle{.fill = ui::Color::from_argb(0xFFFAEBD7), .height = 150},
                                     // ... more rectangles, enough to scroll ...
                                 },
                             },
                         }},
                    },
                }

                // Once built: the picture shifts as the Scroll scrolls.
                parallax->set_source(scroll);
            )cpp",
            .stretch = true,
            .controls =
                {ui::Grid{.columns = {1_fr},
                          .rows    = {1_fr},
                          .height  = 700.0f,
                          .content =
                              {
                                  ui::Cell{.content = ui::Parallax{.vertical_shift       = 500.0f,
                                                                   .horizontal_alignment = ui::Alignment::Leading,
                                                                   .vertical_alignment   = ui::Alignment::Leading,
                                                                   .control              = scroll_parallax,
                                                                   .content = ui::Image{.source = cliff_photo()}}},
                                  ui::Cell{.content = ui::Text{.text = "Scroll the rectangles to see parallaxing of "
                                                                       "image",
                                                               .role = ui::TextRole::Title,
                                                               .wrapping             = ui::TextWrapping::WrapWholeWords,
                                                               .max_width            = 280.0f,
                                                               .horizontal_alignment = ui::Alignment::Center,
                                                               .vertical_alignment   = ui::Alignment::Leading}},
                                  ui::Cell{.content = ui::Scroll{.width                = 150.0f,
                                                                 .horizontal_alignment = ui::Alignment::Leading,
                                                                 .control              = scroll,
                                                                 .content = ui::VerticalStack{.content = std::move(rectangles)}}},
                              }}},
        }),
    }));

    // The list's dark wash, its white rows and its header (the sample's ListView.Background,
    // item template and ListView.Header).
    list->set_background(ui::Color::from_argb(0x80000000));
    list->set_foreground(page_100);
    auto header = std::make_unique<ui::TextControl>("Scroll the list to see parallaxing of image");
    header->set_role(ui::TextRole::Title);
    header->set_wrapping(ui::TextWrapping::WrapWholeWords);
    header->set_max_width(280.0f);
    header->set_horizontal_alignment(ui::Alignment::Center);
    header->set_vertical_alignment(ui::Alignment::Center);
    header->set_color(ui::colors::White);
    list->set_header(std::move(header));
    list_parallax->set_source(list);
    scroll_parallax->set_source(scroll);
    return ui::Spec{std::move(page)};
}

} // namespace gallery
