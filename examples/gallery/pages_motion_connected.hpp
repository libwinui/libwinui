// SPDX-License-Identifier: MIT
// The Connected Animation page's state, shared by pages_motion_connected.cpp and
// pages_motion_connected_cards.cpp (the card page, split off for compile time).
#pragma once

#include "pages_motion.hpp"

#include <memory>
#include <vector>

#include <cstddef>

namespace gallery
{
// The ConnectedAnimation sample's CustomDataObject (gallery.hpp's data_objects()).
using Item = CustomDataObject;

// The acrylic's fallback color, a Border's background being a color.
inline ui::Color acrylic_fill(const ui::Theme& t)
{
    return has_fluent_palette(t) ? t.palette.acrylic_fallback_default : t.surfaces.menu;
}

struct ConnectedState
{
    std::vector<Item> items = data_objects(false);
    std::vector<Item> cards = data_objects(true);

    // A list page and a detail page in one frame.
    ui::Control<ui::NavigationFrame>    list_frame;
    std::vector<ui::Control<ui::Image>> list_images;
    std::size_t                         stored_item = 0;
    bool                                has_stored  = false;
    ui::Element*                        detail_page = nullptr;
    ui::Control<ui::Image>              detail_image;
    ui::Control<ui::VerticalStack>      detail_panel;
    ui::Control<ui::Button>             go_back;

    // A grid of cards and the card opened over it.
    std::vector<ui::Control<ui::Grid>> card_elements;
    std::size_t                        stored_card = 0;
    ui::Control<ui::Border>            smoke_grid;
    ui::Control<ui::Border>            destination;
    ui::Control<ui::Image>             card_image;
    ui::Control<ui::Text>              card_title;
    ui::Control<ui::Text>              card_description;

    // Two sample pages navigating to each other.
    ui::Control<ui::NavigationFrame>         simple_frame;
    int                                      simple_page = 1;
    SamplePageParts                          parts{};
    ui::Property<std::optional<std::size_t>> flight_path{0};
};

// The card page, in pages_motion_connected_cards.cpp.
std::unique_ptr<ui::Element> card_page(ConnectedState& s, ui::Window& window);

} // namespace gallery
