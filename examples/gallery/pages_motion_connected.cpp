// SPDX-License-Identifier: MIT
#include "pages_motion_connected.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <cstddef>

using ui::operator""_fr;

namespace gallery
{
namespace
{
// ---- Connected Animation ----

// "Label: value" in `role`, the label bold, like the samples' two pieces of text side by side.
ui::Spec stat(const char* label, const std::string& value, ui::TextRole role, const ui::Theme& theme)
{
    ui::TextStyle bold = role == ui::TextRole::Caption ? theme.type.caption : theme.type.subtitle;
    bold.weight        = ui::FontWeight::Bold;
    return ui::HorizontalStack{.content = {ui::Text{.text = label, .role = role, .text_style = bold},
                                           ui::Text{.text = value, .role = role, .margin = ui::Edges{.left = 5.0f}}}};
}

std::optional<ui::FlightPath> flight_path_of(std::size_t index)
{
    switch (index)
    {
        case 1:
            return ui::ArcFlightPath{};
        case 2:
            return ui::ShortFlightPath{};
        case 3:
            return ui::StraightFlightPath{};
        default:
            return std::nullopt; // Default: the service's, which is the arc
    }
}

std::unique_ptr<ui::Element> detail_page(ConnectedState& s, ui::Window& window);

// The list page: the items as a list, each row's photo the element to connect. The ListView's
// item template is a column of subtle buttons here (ListView has no item templates).
std::unique_ptr<ui::Element> collection_page(ConnectedState& s, ui::Window& window)
{
    const ui::Theme& theme  = window.theme();
    ui::TextStyle    italic = theme.type.body;
    italic.italic           = true;
    s.list_images.assign(s.items.size(), {});
    ui::SpecList rows;
    for (std::size_t i = 0; i < s.items.size(); ++i)
    {
        const Item& item = s.items[i];
        rows.push_back(
            ui::Button{.variant              = ui::ButtonVariant::Subtle,
                       .horizontal_alignment = ui::Alignment::Stretch,
                       .accessibility        = {.name = item.title},
                       .on_click =
                           [&s, &window, i]
                       {
                           // collection_ItemClick: prepare from the clicked photo, then navigate with the
                           // page animation suppressed.
                           s.stored_item = i;
                           s.has_stored  = true;
                           window.connected_animation_service().prepare_to_animate("ForwardConnectedAnimation",
                                                                                   *s.list_images[i]);
                           s.list_frame->navigate([&s, &window] { return detail_page(s, window); }, ui::NoPageTransition{});
                       },
                       .content = ui::Grid{
                           .columns = {ui::AutoSize, 1_fr},
                           .rows    = {ui::AutoSize},
                           .margin  = ui::Symmetric{.horizontal = 0.0f, .vertical = 12.0f},
                           .content = {
                               ui::Cell{.column  = 0,
                                        .content = ui::Image{.source     = sample_photo(item.image),
                                                             .stretch    = ui::Stretch::Fill,
                                                             .min_width  = 150.0f,
                                                             .max_height = 100.0f,
                                                             .control    = s.list_images[i]}},
                               ui::Cell{.column  = 1,
                                        .content = ui::
                                            VerticalStack{.margin = ui::Edges{.left = 12.0f},
                                                          .content =
                                                              {
                                                                  ui::Text{.text   = item.title,
                                                                           .role   = ui::TextRole::Subtitle,
                                                                           .margin = ui::Edges{.bottom = 6.0f},
                                                                           .horizontal_alignment = ui::Alignment::Leading},
                                                                  stat("V"
                                                                       "i"
                                                                       "e"
                                                                       "w"
                                                                       "s"
                                                                       ":"
                                                                       " ",
                                                                       item.views,
                                                                       ui::TextRole::Caption,
                                                                       theme),
                                                                  stat("L"
                                                                       "i"
                                                                       "k"
                                                                       "e"
                                                                       "s"
                                                                       ":"
                                                                       " ",
                                                                       item.likes,
                                                                       ui::TextRole::Caption,
                                                                       theme),
                                                                  ui::Text{.text       = item.description,
                                                                           .text_style = italic,
                                                                           .margin     = ui::Edges{.top = 12.0f},
                                                                           .max_width  = 500.0f,
                                                                           .horizontal_alignment = ui::Alignment::Leading},
                                                              }}},
                           }}});
    }
    return ui::build(ui::Scroll{.content = ui::VerticalStack{.content = std::move(rows)}});
}

// The detail page: the photo large, its title and numbers beside it (the coordinated panel), the
// description under them, and a Go Back button on an acrylic header.
std::unique_ptr<ui::Element> detail_page(ConnectedState& s, ui::Window& window)
{
    const ui::Theme&        theme = window.theme();
    const Item&             item  = s.items[s.stored_item];
    ui::Control<ui::Border> header_background;
    auto                    page = ui::build(
        ui::Grid{.columns = {1_fr},
                 .rows    = {ui::AutoSize, 1_fr},
                 .content = {
                     ui::Cell{.row = 0, .column = 0, .content = ui::Border{.control = header_background}},
                     ui::Cell{.row     = 0,
                              .column  = 0,
                              .content = ui::Button{.text                 = "Go Back",
                                                    .variant              = ui::ButtonVariant::Accent,
                                                    .horizontal_alignment = ui::Alignment::Leading,
                                                    .vertical_alignment   = ui::Alignment::Leading,
                                                    .on_click             = [&s] { s.list_frame->go_back(); },
                                                    .control              = s.go_back}},
                     ui::Cell{.row     = 0,
                              .column  = 0,
                              .content = ui::
                                  Grid{.columns = {ui::AutoSize, 1_fr},
                                       .rows    = {ui::AutoSize},
                                       .margin = ui::Edges{.left = 20.0f, .top = 52.0f, .right = 20.0f, .bottom = 20.0f},
                                       .content =
                                           {
                                               ui::Cell{.column = 0,
                                                        .content = ui::Image{.source     = sample_photo(item.image),
                                                                             .stretch    = ui::Stretch::Uniform,
                                                                             .max_height = 400.0f,
                                                                             .vertical_alignment = ui::Alignment::Leading,
                                                                             .control = s.detail_image}},
                                               ui::Cell{.column = 1,
                                                        .content = ui::VerticalStack{.margin = ui::Symmetric{.horizontal = 20.0f,
                                                                                                             .vertical = 0.0f},
                                                                                     .vertical_alignment = ui::Alignment::Leading,
                                                                                     .control = s.detail_panel,
                                                                                     .content =
                                                                                         {
                                                                                             ui::Text{.text = item.title,
                                                                                                      .role = ui::TextRole::Subheader,
                                                                                                      .margin = ui::Edges{.bottom = 10.0f},
                                                                                                      .horizontal_alignment = ui::Alignment::Leading},
                                                                                             stat("Views: ", item.views, ui::TextRole::Subtitle, theme),
                                                                                             stat("Likes: ", item.likes, ui::TextRole::Subtitle, theme),
                                                                                         }}},
                                           }}},
                     ui::Cell{.row     = 1,
                              .column  = 0,
                              .content = ui::Text{.text     = item.description,
                                                  .role     = ui::TextRole::Subtitle,
                                                  .wrapping = ui::TextWrapping::WrapWholeWords,
                                                  .margin   = ui::Thickness{10.0f}}},
                 }});
    header_background->set_background(acrylic_fill);
    s.detail_page = page.get();
    return page;
}

} // namespace

ui::Spec connected_animation_page(Context& ctx)
{
    ui::Window& window = ctx.window;
    return with_state<ConnectedState>(
        [&window](ConnectedState& s)
        {
            ui::Control<ui::NavigationFrame> card_frame;
            std::unique_ptr<ui::Element> page       = ui::build(examples({
                example({
                    .title    = "A connected animation between a list page and a detail page",
                    .code     = R"cpp(
                        ui::Control<ui::NavigationFrame> frame;
                        ui::Control<ui::Image>           row_image;    // the clicked row's photo
                        ui::Control<ui::Image>           detail_image; // the large photo on the detail page
                        ui::Control<ui::VerticalStack>   detail_panel; // its title and numbers

                        ui::NavigationFrame{.height = 750, .control = frame}

                        ui::ConnectedAnimationService& service = window.connected_animation_service();

                        // A row's on_click: snapshot its photo, then navigate without a page animation.
                        service.prepare_to_animate("ForwardConnectedAnimation", *row_image);
                        frame->navigate([&] { return detail_page(); }, ui::NoPageTransition{});

                        // Leaving the detail page: snapshot the large photo for the way back.
                        frame->on_navigating_from([&](const ui::NavigatingFromEventArgs&) {
                            service.prepare_to_animate("BackConnectedAnimation", *detail_image);
                        });

                        // Arriving: fly the snapshot into its counterpart on the new page.
                        frame->on_navigated([&](const ui::NavigationEventArgs&) {
                            if (ui::ConnectedAnimation* a = service.animation("ForwardConnectedAnimation"))
                            {
                                a->try_start(*detail_image, {detail_panel}); // the panel comes in alongside
                            }
                            else if (ui::ConnectedAnimation* back = service.animation("BackConnectedAnimation"))
                            {
                                row_image->bring_into_view();
                                back->set_flight_path(ui::ShortFlightPath{});
                                back->try_start(*row_image);
                            }
                        });
                    )cpp",
                    .stretch  = true,
                    .controls = {ui::NavigationFrame{.height = 750.0f, .min_width = 500.0f, .control = s.list_frame}},
                }),
                example({
                    .title   = "A connected animation between elements on the same page",
                    .code    = R"cpp(
                        ui::Control<ui::Grid>   card;        // the clicked card in the grid
                        ui::Control<ui::Border> smoke;       // the dimming layer over the grid, collapsed at first
                        ui::Control<ui::Border> destination; // the opened card on the smoke layer

                        ui::ConnectedAnimationService& service = window.connected_animation_service();

                        // The card's on_click: snapshot it, show the detail card and fly into it.
                        ui::Button{
                            .variant  = ui::ButtonVariant::Subtle,
                            .on_click = [&] {
                                ui::ConnectedAnimation& animation = service.prepare_to_animate("forwardAnimation", *card);
                                smoke->set_visibility(ui::Visibility::Visible);
                                animation.try_start(*destination);
                            },
                            .content = ui::Grid{.columns = {1_fr}, .rows = {1_fr}, .width = 150, .height = 110, .control = card},
                        }

                        // The close button's on_click: fly straight back, then drop the smoke.
                        ui::Button{
                            .accessibility = {.name = "Close"},
                            .on_click      = [&] {
                                ui::ConnectedAnimation& animation = service.prepare_to_animate("backwardsAnimation", *destination);
                                destination->set_visibility(ui::Visibility::Collapsed);
                                animation.on_completed([&] {
                                    smoke->set_visibility(ui::Visibility::Collapsed);
                                    destination->set_visibility(ui::Visibility::Visible);
                                });
                                animation.set_flight_path(ui::ShortFlightPath{});
                                animation.try_start(*card);
                            },
                        }
                    )cpp",
                    .stretch = true,
                    .controls = {ui::NavigationFrame{.min_width = 500.0f, .min_height = 300.0f, .max_height = 700.0f, .control = card_frame}},
                }),
                example(
                    {
                        .title   = "A simple connected animation",
                        .code    = R"cpp(
                            ui::Control<ui::NavigationFrame> frame;
                            ui::Element*                     accent = nullptr; // the accent element of the page on screen

                            ui::NavigationFrame{.height = 500, .control = frame}

                            ui::ConnectedAnimationService& service = window.connected_animation_service();

                            // Navigate: the page on screen prepares its accent element, picks how it
                            // flies, and the frame moves on without an animation of its own.
                            ui::ConnectedAnimation& animation = service.prepare_to_animate("ForwardConnectedAnimation", *accent);
                            animation.set_flight_path(ui::ArcFlightPath{}); // or ShortFlightPath, StraightFlightPath
                            frame->navigate([&] { return second_page(accent); }, ui::NoPageTransition{});

                            // The new page starts it on its own accent element.
                            frame->on_navigated([&](const ui::NavigationEventArgs&) {
                                if (ui::ConnectedAnimation* a = service.animation("ForwardConnectedAnimation"))
                                {
                                    a->try_start(*accent);
                                }
                            });
                        )cpp",
                        .stretch = true,
                        .options = options_panel(
                            200.0f,
                            {
                                ui::Button{
                                    .text    = "Navigate",
                                    .horizontal_alignment = ui::
                                        Alignment::
                                            Stretch,
                                    .on_click =
                                        [&s, &window]
                                    {
                                        // The page on screen prepares its accent
                                        // element, and the frame moves on without its own animation.
                                        ui::ConnectedAnimationService& service = window.connected_animation_service();
                                        if (s.simple_page == 1)
                                        {
                                            ui::ConnectedAnimation&
                                                a = service.prepare_to_animate("ForwardConnectedAnimation", *s.parts.accent);
                                            a.set_flight_path(flight_path_of(s.flight_path.get().value_or(0)));
                                            s.simple_page = 2;
                                        }
                                        else
                                        {
                                            ui::ConnectedAnimation&
                                                a = service.prepare_to_animate("BackwardConnectedAnimation", *s.parts.accent);
                                            a.set_flight_path(flight_path_of(s.flight_path.get().value_or(0)));
                                            s.simple_page = 1;
                                        }
                                        const int target = s.simple_page;
                                        s.simple_frame
                                            ->navigate([&s, target]
                                                       { return ui::build(navigation_sample_page(target, &s.parts)); },
                                                       ui::NoPageTransition{});
                                    }},
                                ui::Text{.text   = "Flight paths",
                                         .role   = ui::TextRole::BodyStrong,
                                         .margin = ui::Symmetric{.horizontal = 0.0f, .vertical = 6.0f}},
                                ui::RadioButtons{.items                = {"Default", "Arc", "Short", "Straight"},
                                                 .selected_index       = s.flight_path},
                            }),
                        .controls = {ui::NavigationFrame{.height = 500.0f, .min_width = 500.0f, .min_height = 300.0f, .control = s.simple_frame}},
                    }),
            }));

            // First sample: the list, cached so it comes back scrolled where it was.
            s.list_frame->on_navigating_from(
                [&s, &window](const ui::NavigatingFromEventArgs& args)
                {
                    // Leaving the detail page: prepare the way back from the large photo.
                    if (&args.page == s.detail_page && s.detail_image != nullptr)
                    {
                        window.connected_animation_service().prepare_to_animate("BackConnectedAnimation", *s.detail_image);
                    }
                });
            s.list_frame->on_navigated(
                [&s, &window](const ui::NavigationEventArgs& e)
                {
                    ui::ConnectedAnimationService& service = window.connected_animation_service();
                    if (e.content == s.detail_page)
                    {
                        // Arriving on the detail page: the photo, with the title panel coordinated.
                        if (ui::ConnectedAnimation* a = service.animation("ForwardConnectedAnimation"))
                        {
                            a->try_start(*s.detail_image, {s.detail_panel});
                        }
                        window.focus_element(s.go_back);
                        return;
                    }
                    // Back on the list page: bring the item back into view and fly into it.
                    s.detail_page = nullptr;
                    if (!s.has_stored)
                    {
                        return;
                    }
                    ui::ImageControl* image = s.list_images[s.stored_item];
                    image->bring_into_view();
                    if (ui::ConnectedAnimation* a = service.animation("BackConnectedAnimation"))
                    {
                        a->set_flight_path(ui::ShortFlightPath{});
                        a->try_start(*image);
                    }
                });
            s.list_frame->navigate([&s, &window] { return collection_page(s, window); },
                                   std::nullopt,
                                   ui::NavigationCacheMode::Enabled);

            // Second sample: one page, the card grid.
            card_frame->navigate([&s, &window] { return card_page(s, window); });

            // Third sample: two sample pages, each starting the other's animation.
            s.simple_frame->on_navigated(
                [&s, &window](const ui::NavigationEventArgs&)
                {
                    ui::ConnectedAnimationService& service = window.connected_animation_service();
                    if (s.simple_page == 2)
                    {
                        // Arriving on the second page: the text panel enters as the tile lands.
                        if (ui::ConnectedAnimation* a = service.animation("ForwardConnectedAnimation"))
                        {
                            if (s.parts.content_panel != nullptr)
                            {
                                s.parts.content_panel->set_transitions({ui::EntranceTransition{}});
                            }
                            a->try_start(*s.parts.accent);
                        }
                    }
                    else if (ui::ConnectedAnimation* a = service.animation("BackwardConnectedAnimation"))
                    {
                        a->try_start(*s.parts.accent);
                    }
                });
            s.simple_frame->navigate([&s] { return ui::build(navigation_sample_page(1, &s.parts)); });
            return ui::Spec{std::move(page)};
        });
}

} // namespace gallery
