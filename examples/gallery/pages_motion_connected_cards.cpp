// SPDX-License-Identifier: MIT
#include "pages_motion_connected.hpp"

#include <memory>
#include <vector>

#include <cstddef>

using ui::operator""_fr;

namespace gallery
{
namespace
{
ui::Color card_background(const ui::Theme& t)
{
    return has_fluent_palette(t) ? t.palette.card_background_default : t.colors().field;
}
ui::Color smoke(const ui::Theme& t)
{
    return t.colors().scrim; // the dimming behind a modal
}

} // namespace

// CardPage: the photos as a grid of cards (a WrapPanel; there is no TileView), and the card
// opened over a smoke layer.
std::unique_ptr<ui::Element> card_page(ConnectedState& s, ui::Window& window)
{
    s.card_elements.assign(s.cards.size(), {});
    std::vector<ui::Control<ui::Border>> captions(s.cards.size());
    ui::SpecList                         cards;
    for (std::size_t i = 0; i < s.cards.size(); ++i)
    {
        const Item& item = s.cards[i];
        cards.push_back(
            ui::Button{.variant       = ui::ButtonVariant::Subtle,
                       .padding       = ui::Thickness{},
                       .margin        = ui::Thickness{4.0f},
                       .accessibility = {.name = item.title},
                       .on_click =
                           [&s, &window, i]
                       {
                           // TipsGrid_ItemClick: prepare from the card, fill the detail card in,
                           // show it over the smoke and fly into it.
                           s.stored_card                     = i;
                           ui::ConnectedAnimation& animation = window.connected_animation_service()
                                                                   .prepare_to_animate("forwardAnimation",
                                                                                       *s.card_elements[i]);
                           const Item&             picked    = s.cards[i];
                           s.card_image->set_source(ui::ImageSource{sample_photo(picked.image)});
                           s.card_title->set_text(picked.title);
                           s.card_description->set_text(picked.description);
                           s.smoke_grid->set_visibility(ui::Visibility::Visible);
                           animation.try_start(*s.destination);
                       },
                       .content = ui::
                           Grid{.columns = {1_fr},
                                .rows    = {1_fr},
                                .width   = 150.0f,
                                .height  = 110.0f,
                                .control = s.card_elements[i],
                                .content =
                                    {ui::Cell{.content = ui::Image{.source  = sample_photo(item.image),
                                                                   .stretch = ui::Stretch::UniformToFill}},
                                     ui::Cell{
                                         .content = ui::Border{.padding = ui::Symmetric{.horizontal = 8.0f, .vertical = 4.0f},
                                                               .vertical_alignment = ui::Alignment::Trailing,
                                                               .control            = captions[i],
                                                               .content = ui::Text{.text = item.title,
                                                                                   .role = ui::TextRole::Caption,
                                                                                   .horizontal_alignment = ui::Alignment::Leading}}}}}});
    }

    ui::Control<ui::Border> detail_body;
    // Built on its own: nested inside the page's Grid below, MSVC's time and memory for the
    // expression grow with every level of Grid cells (clang is unaffected).
    ui::Spec detail = ui::Grid{
        .columns = {1_fr},
        .rows    = {1_fr, ui::AutoSize},
        .content =
            {
                ui::Cell{.row = 0, .content = ui::Image{.stretch = ui::Stretch::UniformToFill, .control = s.card_image}},
                ui::Cell{.row     = 0,
                 .content = ui::ToolTip{
                     .anchor =
                         ui::Button{
                             .margin        = ui::Thickness{8.0f},
                             .width         = 36.0f,
                             .height        = 36.0f,
                             .horizontal_alignment       = ui::Alignment::Trailing,
                             .vertical_alignment       = ui::Alignment::Leading,
                             .accessibility = {.name = "Close"},
                             .on_click =
                                 [&s, &window]
                                 {
                                     // BackButton_Click: fly the open card straight back into its place,
                                     // then drop the smoke.
                                     ui::ConnectedAnimation& animation =
                                         window.connected_animation_service().prepare_to_animate(
                                             "backwardsAnimation", *s.destination);
                                     s.destination->set_visibility(ui::Visibility::Collapsed);
                                     animation.on_completed(
                                         [&s]
                                         {
                                             s.smoke_grid->set_visibility(ui::Visibility::Collapsed);
                                             s.destination->set_visibility(ui::Visibility::Visible);
                                         });
                                     animation.set_flight_path(ui::ShortFlightPath{});
                                     animation.try_start(*s.card_elements[s.stored_card]);
                                 },
                             .content = ui::Icon{.source = ui::IconGlyph{0xE711}, .size = 14.0f}},
                     .content = ui::Text{.text = "Close"}}},
                ui::Cell{.row     = 1,
                 .content = ui::Border{
                     .padding = ui::Symmetric{.horizontal = 16.0f, .vertical = 12.0f},
                     .control     = detail_body,
                     .content = ui::VerticalStack{
                         .content =
                             {
                                 ui::Text{.text = "", .role = ui::TextRole::Subtitle, .control = s.card_title},
                                 ui::Text{.text     = "",
                                          .wrapping = ui::TextWrapping::Wrap,
                                          .margin   = ui::Edges{.top = 4.0f},
                                          .control      = s.card_description},
                             }}}},
            }};

    auto page = ui::build(
        ui::Grid{.columns = {1_fr},
                 .rows    = {1_fr},
                 .content = {
                     ui::Cell{.content = ui::WrapPanel{.max_width            = 1400.0f,
                                                       .horizontal_alignment = ui::Alignment::Center,
                                                       .content              = std::move(cards)}},
                     ui::Cell{.content = ui::Border{.control = s.smoke_grid,
                                                    .content = ui::Border{.border_thickness     = ui::Thickness{1.0f},
                                                                          .corner_radius        = 8.0f,
                                                                          .width                = 400.0f,
                                                                          .height               = 320.0f,
                                                                          .horizontal_alignment = ui::Alignment::Center,
                                                                          .vertical_alignment   = ui::Alignment::Center,
                                                                          .control              = s.destination,
                                                                          .content              = std::move(detail)}}},
                 }});
    for (ui::BorderControl* caption : captions)
    {
        caption->set_background(acrylic_fill);
    }
    s.smoke_grid->set_background(smoke);
    s.smoke_grid->set_visibility(ui::Visibility::Collapsed);
    s.destination->set_border_brush(
        [](const ui::Theme& t)
        { return has_fluent_palette(t) ? t.palette.card_stroke_default : t.surfaces.field_border; }(window.theme()));
    detail_body->set_background(card_background);
    return page;
}

} // namespace gallery
