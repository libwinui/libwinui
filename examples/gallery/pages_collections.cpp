// SPDX-License-Identifier: MIT
#include "gallery.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

#include <cmath>
#include <cstddef>
#include <cstdint>


using ui::operator""_dip;
using ui::operator""_fr;

namespace gallery
{
namespace
{
// ---- shared pieces ----------------------------------------------------------------------------

// 0, 1, ... count - 1: the items of a collection that is only numbers.
std::vector<std::size_t> indices(std::size_t count)
{
    std::vector<std::size_t> all(count);
    std::iota(all.begin(), all.end(), std::size_t{0});
    return all;
}

ui::Spec options_panel(float width, ui::SpecList children, float spacing = 8.0f)
{
    return ui::VerticalStack{
        .spacing         = spacing,
        .child_alignment = ui::Alignment::Stretch,
        .width           = width,
        .content         = std::move(children),
    };
}

// The theme resources the samples fill and stroke with, as a ui::ThemeColor reads them.
ui::Color chrome_faint(const ui::Theme& t) // the split pane's light chrome
{
    return t.surfaces.split_view_pane.value_or(t.dark ? ui::Color::from_argb(0xFF171717) : ui::Color::from_argb(0xFFF2F2F2));
}
ui::Color chrome_mid(const ui::Theme& t) // a medium chrome gray
{
    return t.dark ? ui::Color::from_argb(0xFF1F1F1F) : ui::Color::from_argb(0xFFE6E6E6);
}
ui::Color text_60(const ui::Theme& t) // the text color at 60%
{
    return t.dark ? ui::Color::from_argb(0x99FFFFFF) : ui::Color::from_argb(0x99000000);
}
ui::Color text_20(const ui::Theme& t) // the text color at 20%
{
    return t.dark ? ui::Color::from_argb(0x33FFFFFF) : ui::Color::from_argb(0x33000000);
}
ui::Color page_100(const ui::Theme& t) // the page color: white in light, black in dark
{
    return t.dark ? ui::colors::Black : ui::colors::White;
}
ui::Color system_accent(const ui::Theme& t) // the accent color itself
{
    return t.accent_ramp.base;
}
ui::Color on_accent(const ui::Theme&) // white text on the accent
{
    return ui::colors::White;
}
ui::Color strong_stroke(const ui::Theme& t) // the strong control stroke
{
    return t.colors().control_stroke_strong;
}
ui::Color text_control_border(const ui::Theme& t) // a text field's border
{
    return t.colors().control_stroke_strong;
}
ui::Color highlight(const ui::Theme& t) // the selection color
{
    return t.colors().selection;
}
ui::Color highlight_text(const ui::Theme& t) // the selected text color
{
    return t.colors().selection_text;
}
ui::Color text_inverse(const ui::Theme& t) // text on an inverted background
{
    return t.dark ? ui::Color::from_argb(0xE4000000) : ui::colors::White;
}
ui::Color text_secondary(const ui::Theme& t) // the secondary text color
{
    return t.colors().text_secondary;
}

// A rectangle or ellipse filled with a theme resource, like the samples' shapes and Borders filled
// from the theme.
class ThemedShape final : public ui::Element
{
public:
    ThemedShape(ui::ThemeColor fill, bool ellipse, float corner = 0.0f) :
        fill_(fill),
        ellipse_(ellipse),
        corner_(corner)
    {
    }

protected:
    [[nodiscard]] ui::Size measure_override(ui::Size, const ui::MeasureContext&) const override
    {
        return ui::Size{};
    }
    void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
    {
        const ui::Theme& theme = context.theme;
        const ui::Color  c     = fill_(theme);
        if (ellipse_)
        {
            renderer.fill_ellipse(bounds().center(), bounds().width * 0.5f, bounds().height * 0.5f, c);
        }
        else
        {
            renderer.fill_rounded_rect(bounds(), corner_, c);
        }
    }


private:
    ui::ThemeColor fill_;
    bool           ellipse_;
    float          corner_;
};

std::unique_ptr<ui::Element> shape(
    ui::ThemeColor fill,
    bool           ellipse,
    float          width,
    float          height,
    ui::Alignment  h      = ui::Alignment::Center,
    ui::Alignment  v      = ui::Alignment::Center,
    float          corner = 0.0f)
{
    auto s = std::make_unique<ThemedShape>(fill, ellipse, corner);
    if (width > 0.0f)
    {
        s->set_width(width);
    }
    if (height > 0.0f)
    {
        s->set_height(height);
    }
    s->set_horizontal_alignment(h);
    s->set_vertical_alignment(v);
    return s;
}

// A Stack or Grid built from a spec, given a theme-resource background afterwards.
template <typename Panel>
std::unique_ptr<ui::Element> with_background(ui::Spec view, ui::ThemeColor background)
{
    std::unique_ptr<ui::Element> node = std::move(view).build();
    if (auto* panel = dynamic_cast<ui::Panel*>(node.get()))
    {
        panel->set_background(background);
    }
    return node;
}

// The samples' photos: sample_photo() at a size and stretch.
ui::Spec photo(std::size_t index, float width, float height, ui::Stretch stretch, ui::Alignment v = ui::Alignment::Stretch)
{
    return ui::Image{.source             = ui::ImageSource{sample_photo(index)},
                     .stretch            = stretch,
                     .width              = width,
                     .height             = height,
                     .vertical_alignment = v};
}

// ---- the contacts ----

struct Contact
{
    std::string               first;
    std::string               last;
    std::string               company;
    [[nodiscard]] std::string name() const
    {
        return first + " " + last;
    }
};

std::vector<Contact> contacts()
{
    return {
        {"Kendall", "Collins", "Adatum Corporation"},
        {"Henry", "Ross", "Adventure Works Cycles"},
        {"Vance", "DeLeon", "Alpine Ski House"},
        {"Victoria", "Burke", "Bellows College"},
        {"Amber", "Rodriguez", "Best For You Organics Company"},
        {"Amari", "Rivera", "Contoso, Ltd."},
        {"Jessie", "Irwin", "Contoso Pharmaceuticals"},
        {"Quinn", "Campbell", "Contoso Suites"},
        {"Olivia", "Wilson", "Consolidated Messenger"},
        {"Ana", "Bowman", "Fabrikam, Inc."},
        {"Shawn", "Hughes", "Fabrikam Residences"},
        {"Oscar", "Ward", "First Up Consultants"},
        {"Madison", "Butler", "Fourth Coffee"},
        {"Graham", "Barnes", "Graphic Design Institute"},
        {"Anthony", "Ivanov", "Humongous Insurance"},
        {"Michael", "Peltier", "Lamna Healthcare Company"},
        {"Morgan", "Connors", "Liberty's Delightful Sinful Bakery & Cafe"},
        {"Andre", "Lawson", "Lucerne Publishing"},
        {"Preston", "Morales", "Margie's Travel"},
        {"Briana", "Hernandez", "Nod Publishers"},
        {"Nicole", "Wagner", "Northwind Traders"},
        {"Mario", "Rogers", "Proseware, Inc."},
        {"Eugenia", "Lopez", "Relecloud"},
        {"Nathan", "Rigby", "School of Fine Art"},
        {"Ellis", "Turner", "Southridge Video"},
        {"Miguel", "Reyes", "Tailspin Toys"},
        {"Hayden", "Cook", "Tailwind Traders"},
    };
}

// A contact row: a 32px circle, the name over the company. The circle's strong fill has the same
// values as the strong control stroke (#72000000 / #8BFFFFFF).
ui::Spec contact_template(const Contact& c)
{
    return ui::Grid{
        .columns = {ui::AutoSize, 1_fr},
        .rows    = {1_fr, 1_fr},
        .content =
            {
                ui::Cell{.row      = 0,
                         .column   = 0,
                         .row_span = 2,
                         .content =
                             []
                         {
                             auto circle = shape(strong_stroke, true, 32.0f, 32.0f);
                             circle->set_margin(ui::Thickness{6.0f});
                             return circle;
                         }()},
                ui::Cell{.row     = 0,
                         .column  = 1,
                         .content = ui::Text{.text   = c.name(),
                                             .role   = ui::TextRole::BodyStrong,
                                             .margin = ui::Edges{.left = 12.0f, .top = 6.0f}}},
                ui::Cell{.row     = 1,
                         .column  = 1,
                         .content = ui::Text{.text = c.company, .margin = ui::Edges{.left = 12.0f, .bottom = 6.0f}}},
            },
    };
}

ui::ThemeColor list_border()
{
    return strong_stroke;
}

// ---- TileView ------------------------------------------------------------------------------------

// The TileView page's item templates over CustomDataObject.
ui::Spec image_template(const CustomDataObject& o)
{
    return photo(o.image, 190.0f, 130.0f, ui::Stretch::UniformToFill);
}

ui::Spec icon_text_template(const CustomDataObject& o)
{
    return ui::RelativePanel{
        .width      = 280.0f,
        .min_height = 160.0f,
        .content =
            {
                ui::RelativeItem{.name                  = "image",
                                 .align_left_with_panel = true,
                                 .align_top_with_panel  = true,
                                 .content               = ui::Image{.source  = ui::ImageSource{sample_photo(o.image)},
                                                                    .stretch = ui::Stretch::Uniform,
                                                                    .margin  = ui::Edges{.top = 4.0f},
                                                                    .width   = 18.0f}},
                ui::RelativeItem{.name                 = "title",
                                 .right_of             = "image",
                                 .align_top_with_panel = true,
                                 .content              = ui::Text{.text   = o.title,
                                                                  .role   = ui::TextRole::BodyStrong,
                                                                  .margin = ui::Edges{.left = 8.0f}}},
                ui::RelativeItem{.below   = "title",
                                 .content = ui::Text{.text     = o.description,
                                                     .role     = ui::TextRole::Caption,
                                                     .wrapping = ui::TextWrapping::Wrap,
                                                     .trimming = ui::TextTrimming::WordEllipsis,
                                                     .margin   = ui::Edges{.top = 4.0f, .right = 8.0f}}},
            },
    };
}

ui::Spec image_text_template(const CustomDataObject& o)
{
    return ui::Grid{
        .columns = {ui::AutoSize, 1_fr},
        .width   = 280.0f,
        .content =
            {
                ui::Cell{.row     = 0,
                         .column  = 0,
                         .content = photo(o.image, 0.0f, 100.0f, ui::Stretch::Fill, ui::Alignment::Leading)},
                ui::Cell{.row     = 0,
                         .column  = 1,
                         .content = ui::VerticalStack{.margin  = ui::Edges{.left = 8.0f, .bottom = 8.0f},
                                                      .content = {ui::Text{.text   = o.title,
                                                                           .role   = ui::TextRole::Subtitle,
                                                                           .margin = ui::Edges{.bottom = 8.0f}},
                                                                  ui::Text{.text = o.views + " Views ",
                                                                           .role = ui::TextRole::Caption},
                                                                  ui::Text{.text = o.likes + " Likes",
                                                                           .role = ui::TextRole::Caption}}}},
            },
    };
}

ui::Spec text_template(const CustomDataObject& o)
{
    return ui::HorizontalStack{.width   = 240.0f,
                               .content = {
                                   ui::Text{.text = o.title, .role = ui::TextRole::Title, .margin = ui::Edges{.left = 8.0f}}}};
}

// The image overlay template (and the TileView layout cards' overlay templates): the photo, and a
// 40-tall strip at its foot in text_60() at 0.75 with the title and likes in page_100().
ui::Spec overlay_template(const CustomDataObject& o, float width, float min_image_width = 0.0f)
{
    auto strip = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
    strip->set_background(text_60);
    strip->set_height(40.0f);
    strip->set_vertical_alignment(ui::Alignment::Trailing);
    strip->set_opacity(0.75f);
    strip->set_margin(ui::Thickness{});
    strip->add(
        ui::Text{.text = o.title, .foreground = page_100, .margin = ui::Edges{.left = 5.0f, .top = 1.0f, .right = 5.0f}});
    strip->add(ui::Text{.text       = o.likes + " Likes",
                        .role       = ui::TextRole::Caption,
                        .foreground = page_100,
                        .margin     = ui::Edges{.left = 5.0f, .right = 5.0f, .bottom = 1.0f}});
    return ui::Grid{
        .width = width,
        .content =
            {
                ui::Cell{.row     = 0,
                         .column  = 0,
                         .content = ui::Image{.source               = ui::ImageSource{sample_photo(o.image)},
                                              .stretch              = ui::Stretch::UniformToFill,
                                              .min_width            = min_image_width,
                                              .horizontal_alignment = ui::Alignment::Center,
                                              .vertical_alignment   = ui::Alignment::Center}},
                ui::Cell{.row = 0, .column = 0, .content = std::move(strip)},
            },
    };
}

struct TileViewState
{
    // One collection behind all three views. Reordering the last one moves it here.
    ui::ObservableVector<CustomDataObject> items;
    ui::Property<std::string>              click_output0;
    // Layout customization.
    ui::Control<ui::TileView<CustomDataObject>> styled;
    ui::Property<float>                         column_space{5.0f};
    ui::Property<float>                         row_space{5.0f};
    // Content inside a TileView.
    ui::Control<ui::TileView<CustomDataObject>> content;
    ui::Property<std::string>                   click_output;
    ui::Property<std::string>                   selection_output;
    ui::Property<bool>                          item_click{false};
    ui::Property<bool>                          can_drag{false};
    ui::Property<bool>                          can_reorder{false};
    ui::Property<bool>                          allow_drop{false};
    ui::Property<ui::SelectionMode>             selection_mode{ui::SelectionMode::Single};

    TileViewState()
    {
        items.assign(data_objects());
    }
};

ui::Spec tile_view_basics(Context&)
{
    return with_state<TileViewState>(
        [](TileViewState& s)
        {
            const auto margin_of = [&s]
            { return ui::Symmetric{.horizontal = s.column_space.get(), .vertical = s.row_space.get()}; };
            const auto number_box =
                [](const char* header, double value, double max, double min, ui::Callback<void(double)> changed)
            {
                return ui::NumberBox{.header                = header,
                                     .value                 = value,
                                     .min                   = min,
                                     .max                   = max,
                                     .spin_button_placement = ui::NumberBoxSpinButtonPlacementMode::Inline,
                                     .margin                = ui::Edges{.bottom = 16.0f},
                                     .max_width             = 250.0f,
                                     .on_value_changed = [changed = std::move(changed)](std::optional<double> v) mutable
                                     {
                                         if (v)
                                         {
                                             changed(*v);
                                         }
                                     }};
            };
            return examples({
                example({
                    .title    = "Basic TileView with a simple item template",
                    .code     = R"cpp(
                        struct Photo
                        {
                            std::string     title;
                            ui::ImageSource image;
                        };
                        ui::ObservableVector<Photo> photos; // filled by the app
                        ui::Property<std::string>   output;

                        ui::VerticalStack{
                            .content = {
                                ui::TileView<Photo>{
                                    .items = photos,
                                    .on_item_click = [&](const Photo& photo) { output = "You clicked " + photo.title + "."; },
                                    .item  = [](const Photo& photo) -> ui::Spec
                                    {
                                        return ui::Image{
                                            .source  = photo.image,
                                            .stretch = ui::Stretch::UniformToFill,
                                            .width   = 190,
                                            .height  = 130,
                                        };
                                    },
                                },
                                ui::Text{.text = output},
                            },
                        }
                    )cpp",
                    .stacked  = true,
                    .output   = ui::Text{.text = s.click_output0},
                    .controls = {ui::Text{.text = "This is a basic TileView that has the full source code below.\nOther samples on this page display "
                                                  "only the additional code needed to customize the specific TileView.",
                                          .wrapping = ui::TextWrapping::Wrap,
                                          .margin   = ui::Edges{.bottom = 15.0f}},
                                 ui::TileView<CustomDataObject>{.items                 = s.items,
                                              .on_item_click         = [&s](const CustomDataObject& o) { s.click_output0 = "You clicked " + o.title + "."; },
                                              .item                  = [](const CustomDataObject& o) -> ui::Spec { return image_template(o); }}},
                }),
                example({
                    .title    = "TileView with Layout Customization",
                    .code     = R"cpp(
                        ui::Control<ui::TileView<Photo>> tiles;

                        ui::TileView<Photo>{
                            .items = photos,
                            .maximum_rows_or_columns = 3,
                            .item_container_margin   = std::optional<ui::Thickness>{ui::Thickness{5.0f}},
                            .control                 = tiles,
                            .item  = [](const Photo& photo) -> ui::Spec
                            {
                                // ... the photo, 100 wide, with its title on a strip at the foot ...
                                return ui::Image{.source = photo.image, .stretch = ui::Stretch::UniformToFill, .width = 100};
                            },
                        }

                        // The number boxes change the view through its handle.
                        tiles->set_item_container_margin(ui::Symmetric{.horizontal = column_space, .vertical = row_space});
                        tiles->set_maximum_rows_or_columns(4);
                    )cpp",
                    .stacked  = true,
                    .options  = options_panel(250.0f,
                                              {
                                                 number_box("Space between columns", 5.0, 100.0, 0.0,
                                                            [&s, margin_of](double v)
                                                            {
                                                                s.column_space = static_cast<float>(v);
                                                                if (s.styled != nullptr)
                                                                {
                                                                    s.styled->set_item_container_margin(margin_of());
                                                                }
                                                            }),
                                                 number_box("Space between rows", 5.0, 100.0, 0.0,
                                                            [&s, margin_of](double v)
                                                            {
                                                                s.row_space = static_cast<float>(v);
                                                                if (s.styled != nullptr)
                                                                {
                                                                    s.styled->set_item_container_margin(margin_of());
                                                                }
                                                            }),
                                                 number_box("Maximum number of items before wrapping", 3.0, 8.0, 1.0,
                                                            [&s](double v)
                                                            {
                                                                if (s.styled != nullptr && std::isfinite(v))
                                                                {
                                                                    s.styled->set_maximum_rows_or_columns(static_cast<std::size_t>(v));
                                                                }
                                                            }),
                                             }),
                    .controls = {ui::Text{.text     = "Use the options on the right to control different layout customizations to the TileView below.",
                                          .wrapping = ui::TextWrapping::Wrap,
                                          .margin   = ui::Edges{.bottom = 15.0f}},
                                 ui::TileView<CustomDataObject>{.items                   = s.items,
                                              .maximum_rows_or_columns = 3,
                                              .item_container_margin   = std::optional<ui::Thickness>{ui::Thickness{5.0f}},
                                              .control                     = s.styled,
                                              .item                    = [](const CustomDataObject& o) -> ui::Spec { return overlay_template(o, 100.0f); }}},
                }),
                example({
                    .title    = "Content inside of a TileView.",
                    .code     = R"cpp(
                        ui::Property<ui::SelectionMode> mode{ui::SelectionMode::Single};
                        ui::Property<bool>                      item_click{false};
                        ui::Property<bool>                      can_drag{false};
                        ui::Property<bool>                      can_reorder{false};
                        ui::Property<bool>                      allow_drop{false};
                        ui::Property<std::string>               click_output;
                        ui::Property<std::string>               selection_output;
                        ui::Control<ui::TileView<Photo>>        tiles;

                        ui::TileView<Photo>{
                            .items = photos,
                            .selection_mode        = mode,
                            .item_click_enabled    = item_click,
                            .can_drag_items        = can_drag,
                            .can_reorder_items     = can_reorder,
                            .allow_drop            = allow_drop,
                            .on_selection_changed  = [&]
                            {
                                selection_output = std::format("You have selected {} item(s).", tiles->selected_items().size());
                            },
                            // A reorder needs no handler: the view moves the item in `photos` itself.
                            .on_item_click = [&](const Photo& photo) { click_output = "You clicked " + photo.title + "."; },
                            .control       = tiles,
                            .item  = [](const Photo& photo) -> ui::Spec
                            { return ui::Image{.source = photo.image, .width = 190, .height = 130}; },
                        }

                        // Another item template, by position in the collection.
                        tiles->set_item_template([&](std::size_t index) -> std::unique_ptr<ui::Element>
                                                 { return ui::build(ui::Text{.text = photos[index].title}); });
                    )cpp",
                    .stacked  = true,
                    .output   = ui::VerticalStack{.content = {ui::Text{.text = s.click_output}, ui::Text{.text = s.selection_output}}},
                    .options  = options_panel(
                        180.0f,
                        {
                            ui::RadioButtons{.header         = "Item template",
                                             .items          = {"Image", "Icon/Text", "Image/Text", "Text"},
                                             .selected_index = 0,
                                             .on_selection_changed =
                                                 [&s](std::optional<std::size_t> i)
                                             {
                                                 if (s.content == nullptr)
                                                 {
                                                     return;
                                                 }
                                                 s.content->set_item_template(
                                                     [&s, i = i.value_or(0)](std::size_t k) -> std::unique_ptr<ui::Element>
                                                     {
                                                         const CustomDataObject& o = s.items[k];
                                                         switch (i)
                                                         {
                                                             case 1:
                                                                 return icon_text_template(o).build();
                                                             case 2:
                                                                 return image_text_template(o).build();
                                                             case 3:
                                                                 return text_template(o).build();
                                                             default:
                                                                 return image_template(o).build();
                                                         }
                                                     });
                                             }},
                            ui::Text{.text = "TileView Properties", .margin = ui::Edges{.top = 18.0f, .bottom = 10.0f}},
                            ui::Text{.text     = "In order to drag, drop, and reorder items within the TileView, make sure the last three boxes are checked below.",
                                     .text_style    = ui::TextStyle{.font_size = 13.0f, .horizontal = ui::TextAlign::Leading},
                                     .wrapping = ui::TextWrapping::Wrap,
                                     .max_width = 150.0f},
                            ui::Text{.text = "With item click on, clicking an item prints it below the TileView, whatever the "
                                             "selection mode.",
                                     .text_style     = ui::TextStyle{.font_size = 13.0f, .horizontal = ui::TextAlign::Leading},
                                     .wrapping  = ui::TextWrapping::Wrap,
                                     .max_width = 150.0f},
                            ui::CheckBox{.text               = "Item click",
                                         .on_checked_changed = [&s](std::optional<bool> v)
                                         {
                                             s.item_click   = v.value_or(false);
                                             s.click_output = "";
                                         }},
                            ui::CheckBox{.text = "Drag items", .on_checked_changed = [&s](std::optional<bool> v) { s.can_drag = v.value_or(false); }},
                            ui::CheckBox{.text = "Reorder items", .on_checked_changed = [&s](std::optional<bool> v) { s.can_reorder = v.value_or(false); }},
                            ui::CheckBox{.text = "Allow drop",  .on_checked_changed = [&s](std::optional<bool> v) { s.allow_drop = v.value_or(false); }},
                            ui::ComboBox{.header               = "Selection mode",
                                         .items                = {"None", "Single", "Multiple", "Extended"},
                                         .selected_index       = 1,
                                         .margin               = ui::Edges{.top = 12.0f},
                                         .on_selection_changed = [&s](std::optional<std::size_t> i)
                                         {
                                             const auto mode = static_cast<ui::SelectionMode>(i.value_or(0));
                                             // On the control first: a mode that drops items from the
                                             // selection then runs its handler, which a bound write doesn't.
                                             if (s.content != nullptr)
                                             {
                                                 s.content->set_selection_mode(mode);
                                             }
                                             s.selection_mode = mode;
                                             if (i == 0)
                                             {
                                                 s.selection_output = "";
                                             }
                                         }},
                        }),
                    .controls = {ui::TileView<CustomDataObject>{
                        .items                 = s.items,
                        .selection_mode        = s.selection_mode,
                        .item_click_enabled    = s.item_click,
                        .can_drag_items        = s.can_drag,
                        .can_reorder_items     = s.can_reorder,
                        .allow_drop            = s.allow_drop,
                        .on_selection_changed  = [&s]
                        {
                            if (s.content != nullptr)
                            {
                                s.selection_output = std::format("You have selected {} item(s).", s.content->selected_items().size());
                            }
                        },
                        // A reorder needs no handler: the view moves the item in `s.items` itself.
                        .on_item_click = [&s](const CustomDataObject& o) { s.click_output = "You clicked " + o.title + "."; },
                        .control       = s.content,
                        .item                  = [](const CustomDataObject& o) -> ui::Spec { return image_template(o); }}},
                }),
            });
        });
}

// ---- Item Layouts ----------------------------------------------------------------------------------

// Bar, from the Item Layouts sample: a length of at most 425, its height a quarter of it and its
// diameter a sixth. The bar templates are Borders, which size to the bar: a Grid fills the width it
// is offered, the horizontal stack's whole 6000.
struct Bar
{
    float length = 0.0f;
    float max    = 425.0f;
};

enum class BarLayout
{
    Vertical,
    Horizontal,
    Grid,
};

std::unique_ptr<ui::Element> bar_template(const Bar& bar, BarLayout layout)
{
    switch (layout)
    {
        case BarLayout::Vertical: // a horizontal bar
        {
            auto border = std::make_unique<ui::BorderControl>();
            border->set_background(chrome_faint);
            border->set_width(bar.max);
            border->set_child(shape(system_accent, false, bar.length, 24.0f, ui::Alignment::Leading));
            return border;
        }
        case BarLayout::Horizontal: // a vertical bar
        {
            auto border = std::make_unique<ui::BorderControl>();
            border->set_background(chrome_faint);
            border->set_height(bar.max / 4.0f);
            border->set_child(
                shape(system_accent, false, 48.0f, bar.length / 4.0f, ui::Alignment::Center, ui::Alignment::Leading));
            return border;
        }
        case BarLayout::Grid: // a circle
        {
            auto grid = std::make_unique<ui::GridControl>();
            grid->add(shape(chrome_faint, true, bar.max / 6.0f, bar.max / 6.0f), 0, 0);
            grid->add(shape(system_accent, true, bar.length / 6.0f, bar.length / 6.0f), 0, 0);
            return grid;
        }
    }
    return nullptr;
}

// ActivityFeedLayout: rows of three, a narrow pair and a wide tile swapping sides from row to row,
// all as tall as the first item.
class ActivityFeedLayout final : public ui::VirtualizingLayout
{
public:
    ActivityFeedLayout(float row_spacing, float column_spacing, ui::Size min_item_size) :
        row_spacing_(row_spacing),
        column_spacing_(column_spacing),
        min_item_size_(min_item_size)
    {
    }

    ui::Size measure(ui::VirtualizingLayoutContext& context, ui::Size available) override
    {
        const std::size_t count = context.item_count();
        if (count == 0)
        {
            return ui::Size{};
        }
        if (min_item_size_.empty())
        {
            ui::Element& first = context.get_or_create_element_at(0);
            min_item_size_     = context.measure(first, ui::Size{ui::Unbounded, ui::Unbounded});
        }
        // The rows the realization rect touches, one either side.
        const ui::Rect    window = context.realization_rect();
        const float       stride = min_item_size_.height + row_spacing_;
        const std::size_t rows   = (count + 2) / 3;
        const float       top    = std::isfinite(window.y) ? std::max(0.0f, window.y / stride) : 0.0f;
        const float       bottom = std::isfinite(window.bottom()) ? std::max(0.0f, window.bottom() / stride)
                                                                  : static_cast<float>(rows);
        const auto  first = std::min(static_cast<std::size_t>(top) > 0 ? static_cast<std::size_t>(top) - 1 : 0, rows);
        const auto  last  = std::min(static_cast<std::size_t>(std::min(bottom, static_cast<float>(rows))) + 1, rows);
        const float width = std::max(min_item_size_.width, (available.width - column_spacing_ * 3.0f) / 4.0f);
        rects_.clear();
        first_index_ = first * 3;
        for (std::size_t row = first; row < last; ++row)
        {
            const std::array<ui::Rect, 3> bounds = row_bounds(row, width);
            for (std::size_t column = 0; column < 3; ++column)
            {
                const std::size_t index = row * 3 + column;
                if (index >= count)
                {
                    break;
                }
                ui::Element& element = context.get_or_create_element_at(index);
                (void)context.measure(element, bounds[column].size());
                rects_.push_back(bounds[column]);
            }
        }
        const float extent = (static_cast<float>(rows) - 1.0f) * stride + min_item_size_.height;
        return ui::Size{width * 4.0f + column_spacing_ * 2.0f, std::max(0.0f, extent)};
    }

    ui::Size arrange(ui::VirtualizingLayoutContext& context, ui::Size final_size) override
    {
        std::size_t index = first_index_;
        for (const ui::Rect& r : rects_)
        {
            context.arrange(context.get_or_create_element_at(index++), r);
        }
        return final_size;
    }

private:
    [[nodiscard]] std::array<ui::Rect, 3> row_bounds(std::size_t row, float width) const
    {
        const float y = static_cast<float>(row) * (min_item_size_.height + row_spacing_);
        const float h = min_item_size_.height;
        const float w = width * 2.0f + column_spacing_;
        if (row % 2 == 0)
        {
            return {ui::Rect{0.0f, y, width, h},
                    ui::Rect{width + column_spacing_, y, width, h},
                    ui::Rect{2.0f * (width + column_spacing_), y, w, h}};
        }
        return {ui::Rect{0.0f, y, w, h},
                ui::Rect{w + column_spacing_, y, width, h},
                ui::Rect{w + width + 2.0f * column_spacing_, y, width, h}};
    }

    float                 row_spacing_;
    float                 column_spacing_;
    ui::Size              min_item_size_;
    std::vector<ui::Rect> rects_;
    std::size_t           first_index_ = 0;
};

// VariedImageSizeLayout: columns `Width` wide, each item going to the shortest column, the bounds
// cached so a scroll only realizes what is in view.
class VariedImageSizeLayout final : public ui::VirtualizingLayout
{
public:
    explicit VariedImageSizeLayout(float width) : width_(width)
    {
    }

    void on_items_changed(ui::VirtualizingLayoutContext&, const ui::ItemsChange&) override
    {
        cached_bounds_.clear();
        first_ = last_ = 0;
        invalid_       = true;
        invalidate_measure();
    }

    ui::Size measure(ui::VirtualizingLayoutContext& context, ui::Size available) override
    {
        const ui::Rect viewport = context.realization_rect();
        if (available.width != last_width_ || invalid_)
        {
            update_cached_bounds(available);
            last_width_ = available.width;
        }
        const std::size_t columns = column_count(available.width);
        if (column_offsets_.empty())
        {
            column_offsets_.assign(columns, 0.0f);
        }
        first_                  = start_index(viewport);
        std::size_t       index = first_;
        float             next  = -1.0f;
        const std::size_t count = context.item_count();
        while (index < count && next < viewport.bottom())
        {
            ui::Element&   child   = context.get_or_create_element_at(index);
            const ui::Size desired = context.measure(child, ui::Size{width_, available.height});
            if (index >= cached_bounds_.size())
            {
                const std::size_t column = lowest_column(next);
                cached_bounds_.push_back(ui::Rect{static_cast<float>(column) * width_, next, width_, desired.height});
                column_offsets_[column] += desired.height;
            }
            else if (index + 1 == cached_bounds_.size())
            {
                (void)lowest_column(next);
            }
            else
            {
                next = cached_bounds_[index + 1].y;
            }
            last_ = index;
            ++index;
        }
        const float largest = column_offsets_.empty() ? 0.0f
                                                      : *std::max_element(column_offsets_.begin(), column_offsets_.end());
        const float width = std::isfinite(available.width) ? available.width : static_cast<float>(columns) * width_;
        return ui::Size{width, largest};
    }

    ui::Size arrange(ui::VirtualizingLayoutContext& context, ui::Size final_size) override
    {
        if (!cached_bounds_.empty())
        {
            for (std::size_t index = first_; index <= last_ && index < cached_bounds_.size(); ++index)
            {
                context.arrange(context.get_or_create_element_at(index), cached_bounds_[index]);
            }
        }
        return final_size;
    }

private:
    void update_cached_bounds(ui::Size available)
    {
        column_offsets_.assign(column_count(available.width), 0.0f);
        for (ui::Rect& r : cached_bounds_)
        {
            float             next   = 0.0f;
            const std::size_t column = lowest_column(next);
            r                        = ui::Rect{static_cast<float>(column) * width_, next, width_, r.height};
            column_offsets_[column] += r.height;
        }
        invalid_ = false;
    }
    // An unbounded width (a measure pass that asks for the natural size) gets one column: the
    // float-to-size_t conversion of floor(inf / width_) is undefined.
    [[nodiscard]] std::size_t column_count(float available_width) const
    {
        if (!std::isfinite(available_width))
        {
            return 1;
        }
        return static_cast<std::size_t>(std::max(1.0f, std::floor(available_width / width_)));
    }
    [[nodiscard]] std::size_t start_index(const ui::Rect& viewport) const
    {
        for (std::size_t i = 0; i < cached_bounds_.size(); ++i)
        {
            if (cached_bounds_[i].y < viewport.bottom() && cached_bounds_[i].bottom() > viewport.y)
            {
                return i;
            }
        }
        return 0;
    }
    std::size_t lowest_column(float& offset) const
    {
        std::size_t lowest = 0;
        offset             = column_offsets_.empty() ? 0.0f : column_offsets_[0];
        for (std::size_t i = 0; i < column_offsets_.size(); ++i)
        {
            if (offset > column_offsets_[i])
            {
                offset = column_offsets_[i];
                lowest = i;
            }
        }
        return lowest;
    }

    float                 width_;
    std::size_t           first_      = 0;
    std::size_t           last_       = 0;
    float                 last_width_ = 0.0f;
    std::vector<float>    column_offsets_;
    std::vector<ui::Rect> cached_bounds_;
    bool                  invalid_ = false;
};

// The Item Layouts sample's color list, by their standard names.
struct NamedColor
{
    const char* name;
    ui::Color   color;
};
const std::array<NamedColor, 20>& color_list()
{
    static const std::array<NamedColor, 20> Colors{{
        {"Blue", ui::Color::from_argb(0xFF0000FF)},           {"BlueViolet", ui::Color::from_argb(0xFF8A2BE2)},
        {"Crimson", ui::Color::from_argb(0xFFDC143C)},        {"DarkCyan", ui::Color::from_argb(0xFF008B8B)},
        {"DarkGoldenrod", ui::Color::from_argb(0xFFB8860B)},  {"DarkMagenta", ui::Color::from_argb(0xFF8B008B)},
        {"DarkOliveGreen", ui::Color::from_argb(0xFF556B2F)}, {"DarkRed", ui::Color::from_argb(0xFF8B0000)},
        {"DarkSlateBlue", ui::Color::from_argb(0xFF483D8B)},  {"DeepPink", ui::Color::from_argb(0xFFFF1493)},
        {"IndianRed", ui::Color::from_argb(0xFFCD5C5C)},      {"MediumSlateBlue", ui::Color::from_argb(0xFF7B68EE)},
        {"Maroon", ui::Color::from_argb(0xFF800000)},         {"MidnightBlue", ui::Color::from_argb(0xFF191970)},
        {"Peru", ui::Color::from_argb(0xFFCD853F)},           {"SaddleBrown", ui::Color::from_argb(0xFF8B4513)},
        {"SteelBlue", ui::Color::from_argb(0xFF4682B4)},      {"OrangeRed", ui::Color::from_argb(0xFFFF4500)},
        {"Firebrick", ui::Color::from_argb(0xFFB22222)},      {"DarkKhaki", ui::Color::from_argb(0xFFBDB76B)},
    }};
    return Colors;
}

// Recipe, from the Item Layouts sample, with the sample's Random made repeatable.
struct Recipe
{
    int                       num = 0;
    std::string               name;
    ui::Color                 color;
    std::vector<std::string>  ingredients;
    [[nodiscard]] std::string ingredient_text() const
    {
        std::string text;
        for (const std::string& i : ingredients)
        {
            text += "\n" + i;
        }
        return text;
    }
};

const std::array<const char*, 7>
    Fruits{"Apricots", "Bananas", "Grapes", "Strawberries", "Watermelon", "Plums", "Blueberries"};
const std::array<const char*, 7>
    Vegetables{"Broccoli", "Spinach", "Sweet potato", "Cauliflower", "Onion", "Brussels sprouts", "Carrots"};
const std::array<const char*, 7> Grains{"Rice", "Quinoa", "Pasta", "Bread", "Farro", "Oats", "Barley"};
const std::array<const char*, 7> Proteins{"Steak", "Chicken", "Tofu", "Salmon", "Pork", "Chickpeas", "Eggs"};

std::vector<Recipe> recipe_list()
{
    std::uint32_t seed = 4242u;
    const auto    next = [&seed](int lo, int hi)
    {
        seed = seed * 1664525u + 1013904223u;
        return lo + static_cast<int>((seed >> 8) % static_cast<std::uint32_t>(hi - lo));
    };
    const std::array<const char*, 7>
                        extras{"Garlic", "Lemon", "Butter", "Lime", "Feta Cheese", "Parmesan Cheese", "Breadcrumbs"};
    std::vector<Recipe> recipes;
    recipes.reserve(1000);
    for (int k = 0; k < 1000; ++k)
    {
        Recipe r;
        r.num         = k;
        r.name        = "Recipe " + std::to_string(k);
        r.color       = color_list()[static_cast<std::size_t>(next(0, 19))].color;
        r.ingredients = {Fruits[static_cast<std::size_t>(next(0, 6))],
                         Vegetables[static_cast<std::size_t>(next(0, 6))],
                         Grains[static_cast<std::size_t>(next(0, 6))],
                         Proteins[static_cast<std::size_t>(next(0, 6))]};
        // Up to three extra ingredients, so the items differ in height.
        const int extra = next(0, 4);
        for (int i = 0; i < extra; ++i)
        {
            const std::string e = extras[static_cast<std::size_t>(next(0, 6))];
            if (std::find(r.ingredients.begin(), r.ingredients.end(), e) == r.ingredients.end())
            {
                r.ingredients.push_back(e);
            }
        }
        recipes.push_back(std::move(r));
    }
    return recipes;
}

struct ItemLayoutsState
{
    // Basic, non-interactive items.
    ui::ObservableVector<Bar>     bars{Bar{300.0f}, Bar{25.0f}, Bar{175.0f}};
    std::uint32_t                 seed = 17u;
    ui::Control<ui::ForEach<Bar>> repeater;
    BarLayout                     layout = BarLayout::Vertical;
    ui::Property<bool>            can_delete{true};
    // Virtualizing list.
    ui::Control<ui::ForEach<std::size_t>> feed;
    // WrapLayoutEngine.
    ui::WrapLayoutEngine*     flow        = nullptr;
    ui::ScrollControl*        flow_scroll = nullptr;
    std::size_t               realized    = 0;
    bool                      count_dirty = true;
    ui::Property<std::string> realized_text{"Realized elements: 0 of 500"};
    // Animated scrolling.
    ui::ScrollControl*                   animated_scroll = nullptr;
    ui::Control<ui::ForEach<NamedColor>> animated;
    ui::Property<ui::Color>              rectangle{ui::colors::Transparent};
    // Varied image sizes.
    std::vector<Recipe>              recipes = recipe_list();
    ui::ObservableVector<Recipe>     shown;
    ui::Control<ui::ForEach<Recipe>> varied;
    std::string                      filter;
    bool                             descending = false;

    void apply_layout()
    {
        if (repeater == nullptr)
        {
            return;
        }
        switch (layout)
        {
            case BarLayout::Vertical:
                repeater->set_layout(std::make_unique<ui::StackLayoutEngine>())->set_spacing(8.0f);
                repeater->set_max_width(425.0f + 12.0f);
                break;
            case BarLayout::Horizontal:
            {
                auto* stack = repeater->set_layout(std::make_unique<ui::StackLayoutEngine>());
                stack->set_orientation(ui::Orientation::Horizontal);
                stack->set_spacing(8.0f);
                repeater->set_max_width(6000.0f);
                break;
            }
            case BarLayout::Grid:
            {
                auto* grid = repeater->set_layout(std::make_unique<ui::TileLayoutEngine>());
                grid->set_min_row_spacing(8.0f);
                grid->set_min_column_spacing(8.0f);
                repeater->set_max_width(540.0f);
                break;
            }
        }
        // A new layout builds every row again, and the template reads `layout` as it is then.
    }
    void update_recipes()
    {
        // Ingredients containing the filter, sorted by how many there are.
        std::vector<Recipe> matching;
        for (const Recipe& r : recipes)
        {
            const std::string text = ui::text_analyzer().to_lower(r.ingredient_text());
            const std::string f    = ui::text_analyzer().to_lower(filter);
            if (text.find(f) != std::string::npos)
            {
                matching.push_back(r);
            }
        }
        std::stable_sort(matching.begin(),
                         matching.end(),
                         [this](const Recipe& a, const Recipe& b)
                         {
                             return descending ? a.ingredients.size() > b.ingredients.size()
                                               : a.ingredients.size() < b.ingredients.size();
                         });
        shown.assign(matching); // a reset: the repeater follows the collection
    }
};

// The text and number item templates.
ui::Spec string_data(const std::string& text)
{
    return with_background<
        ui::GridControl>(ui::Grid{.margin = ui::Thickness{10.0f},
                                  .content = {ui::Cell{.row    = 0,
                                                       .column = 0,
                                                       .content = ui::Text{.text       = text,
                                                                           .wrapping   = ui::TextWrapping::Wrap,
                                                                           .foreground = on_accent,
                                                                           .margin     = ui::Thickness{10.0f},
                                                                           .horizontal_alignment = ui::Alignment::Center,
                                                                           .vertical_alignment = ui::Alignment::Center}}}},
                         system_accent);
}

ui::Spec int_data(int value)
{
    return with_background<
        ui::GridControl>(ui::Grid{.margin = ui::Thickness{10.0f},
                                  .content = {ui::Cell{.row    = 0,
                                                       .column = 0,
                                                       .content = ui::Text{.text   = std::to_string(value),
                                                                           .role   = ui::TextRole::Header,
                                                                           .margin = ui::Thickness{10.0f},
                                                                           .horizontal_alignment = ui::Alignment::Center,
                                                                           .vertical_alignment = ui::Alignment::Center}}}},
                         chrome_mid);
}

ui::Spec items_repeater_page(Context& ctx)
{
    ui::Window& window = ctx.window;
    return with_state<ItemLayoutsState>(
        [&window](ItemLayoutsState& s)
        {
            // ---- 1: basic, non-interactive items ----
            auto repeater_scroll = ui::build(
                ui::Scroll{.horizontal_scroll_mode           = ui::ScrollMode::Auto,
                           .horizontal_scroll_bar_visibility = ui::ScrollBarVisibility::Auto,
                           .max_height                       = 500.0f,
                           .content = ui::ForEach<Bar>{.items      = s.bars,
                                                       .virtualize = true,
                                                       .control    = s.repeater,
                                                       .item       = [&s](Bar& bar) -> ui::Spec
                                                       { return bar_template(bar, s.layout); }}});
            if (auto* sv = dynamic_cast<ui::ScrollControl*>(repeater_scroll.get()))
            {
                sv->set_content_orientation(ui::ScrollContentOrientation::Both);
            }
            s.apply_layout();

            // ---- 2: virtualizing, scrollable list ----
            auto feed_layout = std::make_unique<ActivityFeedLayout>(12.0f, 12.0f, ui::Size{80.0f, 108.0f});
            auto numbers     = ui::build(ui::Scroll{
                .height  = 400.0f,
                .content = ui::ForEach<
                    std::size_t>{.items      = indices(500),
                                 .layout     = std::move(feed_layout),
                                 .virtualize = true,
                                 .margin     = ui::Edges{.right = 12.0f},
                                 .control    = s.feed,
                                 .item       = [](const std::size_t& i) -> ui::Spec
                                 {
                                     // Normal for even, Accent for odd.
                                     const bool accent = i % 2 == 1;
                                     return with_background<ui::GridControl>(
                                         ui::Grid{
                                             .content =
                                                 {ui::Cell{.row    = 0,
                                                           .column = 0,
                                                           .content = ui::Text{.text = std::to_string(i),
                                                                               .foreground = accent ? on_accent : nullptr,
                                                                               .horizontal_alignment = ui::Alignment::Center,
                                                                               .vertical_alignment = ui::Alignment::Center}}}},
                                         accent ? system_accent : chrome_mid);
                                 }},
            });

            // ---- 3: WrapLayoutEngine ----
            auto flow_layout = std::make_unique<ui::WrapLayoutEngine>();
            flow_layout->set_line_spacing(8.0f);
            flow_layout->set_min_item_spacing(8.0f);
            s.flow         = flow_layout.get();
            auto flow_view = ui::build(ui::Scroll{
                .width                = 520.0f,
                .height               = 320.0f,
                .horizontal_alignment = ui::Alignment::Leading,
                .content              = ui::ForEach<std::size_t>{
                    .items                   = indices(500),
                    .layout                  = std::move(flow_layout),
                    .virtualize              = true,
                    .horizontal_cache_length = 0.0f,
                    .vertical_cache_length   = 0.0f,
                    // on_element_prepared and on_element_clearing count; the text follows on the next
                    // tick.
                    .on_element_prepared =
                        [&s]
                    {
                        ++s.realized;
                        s.count_dirty = true;
                    },
                    .on_element_clearing =
                        [&s](ui::Element&)
                    {
                        --s.realized;
                        s.count_dirty = true;
                    },
                    .item = [](const std::size_t& i) -> ui::Spec
                    {
                        // A 96x64 control-fill card with its label centered.
                        auto card = themed_box(BoxStyle::Control,
                                               ui::Thickness{},
                                               ui::Text{.text                 = std::format("Item {}", i + 1),
                                                        .horizontal_alignment = ui::Alignment::Center,
                                                        .vertical_alignment   = ui::Alignment::Center});
                        card->set_width(96.0f);
                        card->set_height(64.0f);
                        return card;
                    },
                },
            });
            s.flow_scroll  = dynamic_cast<ui::ScrollControl*>(flow_view.get());
            auto counter   = std::make_unique<Timer>();
            counter->every_tick(
                [&s]
                {
                    if (s.count_dirty)
                    {
                        s.count_dirty   = false;
                        s.realized_text = std::format("Realized elements: {} of 500", s.realized);
                    }
                });

            // ---- 4: mixed-type collection ----
            static const std::array<const char*, 4> Lorem{
                "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore "
                "et dolore magna aliqua.",
                "Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo "
                "consequat.",
                "Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla "
                "pariatur.",
                "Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id "
                "est laborum.",
            };
            auto mixed_layout = std::make_unique<ui::TileLayoutEngine>();
            mixed_layout->set_min_item_width(200.0f);
            mixed_layout->set_min_item_height(200.0f);

            // ---- 5: nested repeaters ----
            struct Category
            {
                const char*                       name;
                const std::array<const char*, 7>* items;
            };
            static const std::array<Category, 4> Categories{
                {{"Fruits", &Fruits}, {"Vegetables", &Vegetables}, {"Grains", &Grains}, {"Proteins", &Proteins}}};

            // ---- 6: animated scrolling ----
            auto animated     = ui::build(ui::Scroll{
                .width   = 250.0f,
                .height  = 175.0f,
                .content = ui::ForEach<NamedColor>{
                    .items      = std::vector<NamedColor>(color_list().begin(), color_list().end()),
                    .virtualize = true,
                    // The scale follows the item's distance from the middle of the viewport.
                    .on_element_prepared =
                        [&s](const ui::ElementPreparedArgs& args)
                    {
                        ui::Element* item = &args.element;
                        args.element.start_animation(ui::AnimatedProperty::Scale,
                                                     ui::ExpressionAnimation{ui::Callback<ui::Vector3()>{
                                                         [&s, item]
                                                         {
                                                             if (s.animated_scroll == nullptr)
                                                             {
                                                                 return ui::Vector3{1.0f, 1.0f, 1.0f};
                                                             }
                                                             const float half = s.animated_scroll->bounds().height / 2.0f;
                                                             const float middle = s.animated_scroll->bounds().y + half;
                                                             const float center = item->bounds().center().y;
                                                             const float scale = 1.0f - std::abs(middle - center) *
                                                                                            (0.25f / std::max(1.0f, half));
                                                             return ui::Vector3{scale, scale, 1.0f};
                                                         }}});
                        args.element.start_animation(ui::AnimatedProperty::CenterPoint,
                                                     ui::ExpressionAnimation{ui::Callback<ui::Vector3()>{
                                                         [item]
                                                         {
                                                             return ui::Vector3{item->bounds().width / 2.0f,
                                                                                item->bounds().height / 2.0f,
                                                                                0.0f};
                                                         }}});
                    },
                    .control = s.animated,
                    .item    = [&s](const NamedColor& c) -> ui::Spec
                    {
                        return ui::Button{.text                 = c.name,
                                          .background           = c.color,
                                          .foreground           = text_inverse,
                                          .horizontal_alignment = ui::Alignment::Stretch,
                                          .on_click             = [&s, color = c.color] { s.rectangle = color; }};
                    },
                },
            });
            s.animated_scroll = dynamic_cast<ui::ScrollControl*>(animated.get());
            // A focused item comes to the middle of the viewport, and Home/End go to the ends.
            for (const ui::Key key : {ui::Key::Home, ui::Key::End})
            {
                s.animated->add_shortcut(
                    ui::Shortcut{.keys       = key,
                                 .on_pressed = [&s, &window, key]
                                 {
                                     const std::size_t target = key == ui::Key::Home ? 0 : color_list().size() - 1;
                                     if (ui::Element* e = s.animated->get_or_create_element(target))
                                     {
                                         e->start_bring_into_view(ui::BringIntoViewOptions{.vertical_alignment_ratio = 0.5f,
                                                                                           .animation_desired = true});
                                         (void)window.focus_element(e);
                                     }
                                 }});
            }

            // ---- 7: varied sizes, filtered and sorted ----
            auto varied = ui::build(ui::Scroll{
                .content =
                    ui::ForEach<Recipe>{
                        .items      = s.shown,
                        .layout     = std::make_unique<VariedImageSizeLayout>(200.0f),
                        .virtualize = true,
                        .control    = s.varied,
                        .item       = [](const Recipe& r) -> ui::Spec
                        {
                            // A recipe: its color swatch and ingredients.
                            auto swatch = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
                            swatch->set_background(r.color);
                            swatch->set_height(75.0f);
                            swatch->set_margin(ui::Thickness{8.0f});
                            swatch->set_opacity(0.8f);
                            swatch->add(ui::Text{.text           = std::to_string(r.num),
                                                 .text_style     = ui::TextStyle{.font_size = 35.0f},
                                                 .text_alignment = ui::TextAlignment::Center,
                                                 .foreground     = page_100,
                                                 .margin         = ui::Thickness{12.0f}});
                            return with_background<
                                ui::StackControl>(ui::VerticalStack{.margin = ui::Thickness{5.0f},
                                                                    .content =
                                                                        {std::move(swatch),
                                                                         ui::Text{.text     = r.name,
                                                                                  .role     = ui::TextRole::Title,
                                                                                  .wrapping = ui::TextWrapping::Wrap,
                                                                                  .margin = ui::Edges{.left  = 15.0f,
                                                                                                      .right = 10.0f}},
                                                                         ui::Text{.text     = r.ingredient_text(),
                                                                                  .wrapping = ui::TextWrapping::Wrap,
                                                                                  .margin = ui::Edges{.left  = 15.0f,
                                                                                                      .right = 15.0f,
                                                                                                      .bottom = 15.0f}}}},
                                                  text_20);
                        },
                    },
            });
            s.update_recipes();

            ui::SpecList nested;
            return examples({
                example({
                    .title   = "Basic, non-interactive items laid out by ForEach",
                    .code    = R"cpp(
                        struct Bar
                        {
                            float length = 0.0f;
                        };
                        ui::ObservableVector<Bar>     bars{Bar{300.0f}, Bar{25.0f}, Bar{175.0f}};
                        ui::Control<ui::ForEach<Bar>> repeater;

                        ui::Scroll{
                            .horizontal_scroll_mode           = ui::ScrollMode::Auto,
                            .horizontal_scroll_bar_visibility = ui::ScrollBarVisibility::Auto,
                            .max_height                       = 500,
                            .content                          = ui::ForEach<Bar>{
                                .items = bars,
                                .layout     = ui::StackLayout{.spacing = 8},
                                .virtualize = true,
                                .control    = repeater,
                                .item  = [](Bar& bar) -> ui::Spec
                                {
                                    return ui::Border{
                                        .background           = ui::Color::from_argb(0xFF0078D4),
                                        .width                = bar.length,
                                        .height               = 24,
                                        .horizontal_alignment = ui::Alignment::Leading,
                                    };
                                },
                            },
                        }

                        // Add Item and Remove Item edit the collection, and the rows follow.
                        bars.push_back(Bar{120.0f});
                        bars.erase(0);

                        // The Layout options swap the layout through the handle.
                        auto* grid = repeater->set_layout(std::make_unique<ui::TileLayoutEngine>());
                        grid->set_min_row_spacing(8.0f);
                        grid->set_min_column_spacing(8.0f);
                    )cpp",
                    .options = options_panel(
                        200.0f,
                        {
                            ui::Button{.text     = "Add Item",
                                       .min_width = 150.0f,
                                       .on_click = [&s]
                                       {
                                           s.seed        = s.seed * 1664525u + 1013904223u;
                                           const float l = static_cast<float>((s.seed >> 8) % 425u);
                                           s.bars.push_back(Bar{l});
                                           s.can_delete = true;
                                       }},
                            ui::Button{.text      = "Remove Item",
                                       .min_width = 150.0f,
                                       .enabled   = s.can_delete,
                                       .on_click  = [&s]
                                       {
                                           if (!s.bars.empty())
                                           {
                                               s.bars.erase(0);
                                               s.can_delete = !s.bars.empty();
                                           }
                                       }},
                            ui::RadioButtons{.header         = "Layout",
                                             .items          = {"StackLayout - Vertical", "StackLayout - Horizontal", "TileLayout"},
                                             .selected_index = 0,
                                             .on_selection_changed = [&s](std::optional<std::size_t> i)
                                             {
                                                 s.layout = static_cast<BarLayout>(static_cast<int>(i.value_or(0)));
                                                 s.apply_layout();
                                             }},
                        },
                        12.0f),
                    .controls = {std::move(repeater_scroll)},
                }),
                example({
                    .title    = "Virtualizing, scrollable list of items laid out by ForEach",
                    .code     = R"cpp(
                        // ActivityFeedLayout is the app's own layout: a class deriving from
                        // ui::VirtualizingLayout that overrides measure() and arrange().
                        std::vector<std::size_t>              numbers; // 0 .. 499
                        ui::Control<ui::ForEach<std::size_t>> feed;

                        ui::Scroll{
                            .height  = 400,
                            .content = ui::ForEach<std::size_t>{
                                .items = numbers,
                                .layout     = std::make_unique<ActivityFeedLayout>(12.0f, 12.0f, ui::Size{80.0f, 108.0f}),
                                .virtualize = true,
                                .control    = feed,
                                .item  = [](const std::size_t& i) -> ui::Spec
                                {
                                    // Odd items get another template than even ones.
                                    return ui::Border{
                                        .background = i % 2 == 1 ? ui::Color::from_argb(0xFF0078D4)
                                                                 : ui::Color::from_argb(0xFFE6E6E6),
                                        .content    = ui::Text{
                                            .text                 = std::to_string(i),
                                            .horizontal_alignment = ui::Alignment::Center,
                                            .vertical_alignment   = ui::Alignment::Center,
                                        },
                                    };
                                },
                            },
                        }

                        // "Uniform grid" swaps the layout through the handle.
                        auto* grid = feed->set_layout(std::make_unique<ui::TileLayoutEngine>());
                        grid->set_min_item_width(108.0f);
                        grid->set_min_item_height(108.0f);
                        grid->set_min_row_spacing(12.0f);
                        grid->set_min_column_spacing(12.0f);
                    )cpp",
                    .stretch  = true,
                    .options  = options_panel(200.0f,
                                              {ui::RadioButtons{.items                = {"Uniform grid", "Custom virtualizing layout"},
                                                                .selected_index       = 1,
                                                                .on_selection_changed = [&s](std::optional<std::size_t> i)
                                                                {
                                                                    if (i == 0)
                                                                    {
                                                                        auto* grid = s.feed->set_layout(std::make_unique<ui::TileLayoutEngine>());
                                                                        grid->set_min_item_width(108.0f);
                                                                        grid->set_min_item_height(108.0f);
                                                                        grid->set_min_row_spacing(12.0f);
                                                                        grid->set_min_column_spacing(12.0f);
                                                                    }
                                                                    else
                                                                    {
                                                                        s.feed->set_layout(std::make_unique<ActivityFeedLayout>(12.0f, 12.0f, ui::Size{80.0f, 108.0f}));
                                                                    }
                                                                }}}),
                    .controls = {std::move(numbers)},
                }),
                example({
                    .title    = "Virtualize a wrapping collection with WrapLayout",
                    .code     = R"cpp(
                        // The layout is made by hand, so that a pointer to it can be kept and changed later.
                        auto layout = std::make_unique<ui::WrapLayoutEngine>();
                        layout->set_line_spacing(8.0f);
                        layout->set_min_item_spacing(8.0f);
                        ui::WrapLayoutEngine* flow     = layout.get();
                        std::size_t           realized = 0;

                        ui::Scroll{
                            .width                = 520,
                            .height               = 320,
                            .horizontal_alignment = ui::Alignment::Leading,
                            .content              = ui::ForEach<std::size_t>{
                                .items = numbers, .layout                  = std::move(layout),
                                .virtualize              = true,
                                .horizontal_cache_length = 0,
                                .vertical_cache_length   = 0,
                                // Count the rows that exist as they come and go.
                                .on_element_prepared = [&] { ++realized; },
                                .on_element_clearing = [&](ui::Element&) { --realized; },
                                // 0 .. 499
                                .item  = [](const std::size_t& i) -> ui::Spec
                                {
                                    return ui::Border{
                                        .width   = 96,
                                        .height  = 64,
                                        .content = ui::Text{
                                            .text                 = std::format("Item {}", i + 1),
                                            .horizontal_alignment = ui::Alignment::Center,
                                            .vertical_alignment   = ui::Alignment::Center,
                                        },
                                    };
                                },
                            },
                        }

                        // The options change the layout in place.
                        flow->set_line_alignment(ui::ItemsJustification::SpaceBetween);
                        flow->set_min_item_spacing(16.0f);
                        flow->set_line_spacing(16.0f);
                    )cpp",
                    .stacked  = true,
                    .options  = options_panel(
                        200.0f,
                        {
                            ui::RadioButtons{.header         = "Orientation",
                                             .items          = {"Horizontal", "Vertical"},
                                             .selected_index = 0,
                                             .on_selection_changed = [&s](std::optional<std::size_t> i)
                                             {
                                                 const bool horizontal = i != 1;
                                                 s.flow->set_orientation(horizontal ? ui::Orientation::Horizontal : ui::Orientation::Vertical);
                                                 s.flow_scroll->set_content_orientation(horizontal ? ui::ScrollContentOrientation::Vertical
                                                                                                   : ui::ScrollContentOrientation::Horizontal);
                                                 s.flow_scroll->set_horizontal_scroll_mode(horizontal ? ui::ScrollMode::Disabled : ui::ScrollMode::Enabled);
                                                 s.flow_scroll->set_vertical_scroll_mode(horizontal ? ui::ScrollMode::Enabled : ui::ScrollMode::Disabled);
                                             }},
                            ui::ComboBox{.header               = "Line alignment",
                                         .items                = {"Start", "Center", "Space between", "Space evenly"},
                                         .selected_index       = 0,
                                         .on_selection_changed = [&s](std::optional<std::size_t> i)
                                         {
                                             static const std::array<ui::ItemsJustification, 4> Align{ui::ItemsJustification::Start,
                                                                                                         ui::ItemsJustification::Center,
                                                                                                         ui::ItemsJustification::SpaceBetween,
                                                                                                         ui::ItemsJustification::SpaceEvenly};
                                             s.flow->set_line_alignment(Align[std::min<std::size_t>(i.value_or(0), 3)]);
                                         }},
                            ui::Slider{.header           = "Minimum item spacing",
                                       .value            = 8.0f,
                                       .max              = 32.0f,
                                       .tick_frequency   = 2.0f,
                                       .step_frequency   = 2.0f,
                                       .snaps_to         = ui::SliderSnapsTo::Ticks,
                                       .on_value_changed = [&s](double v) { s.flow->set_min_item_spacing(static_cast<float>(v)); }},
                            ui::Slider{.header           = "Line spacing",
                                       .value            = 8.0f,
                                       .max              = 32.0f,
                                       .tick_frequency   = 2.0f,
                                       .step_frequency   = 2.0f,
                                       .snaps_to         = ui::SliderSnapsTo::Ticks,
                                       .on_value_changed = [&s](double v) { s.flow->set_line_spacing(static_cast<float>(v)); }},
                        },
                        12.0f),
                    .controls = {ui::Text{.text = "Scroll through 500 items. A virtualized ForEach creates and recycles only the elements needed near the viewport.",
                                          .wrapping = ui::TextWrapping::Wrap},
                                 std::move(flow_view),
                                 ui::Text{.text = s.realized_text},
                                 std::move(counter)},
                }),
                example({
                    .title    = "ForEach with a mixed-type collection",
                    .code     = R"cpp(
                        std::vector<std::variant<int, std::string>> mixed{64, "Lorem ipsum dolor sit amet", 128, "Ut enim ad minim veniam"};

                        ui::ForEach<std::variant<int, std::string>>{
                            .items = mixed,
                            .layout     = ui::TileLayout{.min_item_width = 200.0f, .min_item_height = 200.0f},
                            .virtualize = true,
                            .item  = [](const std::variant<int, std::string>& value) -> ui::Spec
                            {
                                // The template chooses by the item: a large number, or wrapped text.
                                if (const int* number = std::get_if<int>(&value))
                                {
                                    return ui::Text{.text = std::to_string(*number), .role = ui::TextRole::Header};
                                }
                                return ui::Text{.text = std::get<std::string>(value), .wrapping = ui::TextWrapping::Wrap};
                            },
                        }
                    )cpp",
                    .stacked  = true,
                    .controls = {ui::Text{.text = "This is a ForEach that displays both integer and string items. It uses an item template selector "
                                                  "to choose the correct layout for each of its items.",
                                          .wrapping = ui::TextWrapping::Wrap},
                                 ui::ForEach<std::size_t>{.items = indices(9),
                                                   .layout     = std::move(mixed_layout),
                                                   .virtualize = true,
                                                   .margin     = ui::Edges{.right = 12.0f},
                                                   .item  = [](const std::size_t& i) -> ui::Spec
                                                   {
                                                       // A number or a text template by position: 64, "Lorem...", 128, ...
                                                       return i % 2 == 0 ? int_data(64 << (i / 2)) : string_data(Lorem[i / 2]);
                                                   }}},
                }),
                example({
                    .title    = "Laying out nested ForEach",
                    .code     = R"cpp(
                        struct Category
                        {
                            std::string              name;
                            std::vector<std::string> items;
                        };
                        std::vector<Category> categories; // Fruits, Vegetables, Grains, Proteins

                        ui::Scroll{
                            .content_orientation              = ui::ScrollContentOrientation::Both,
                            .horizontal_scroll_mode           = ui::ScrollMode::Auto,
                            .horizontal_scroll_bar_visibility = ui::ScrollBarVisibility::Auto,
                            .content                          = ui::ForEach<Category>{
                                .items = categories,
                                .virtualize         = true,
                                .vertical_alignment = ui::Alignment::Leading,
                                .item  = [](const Category& category) -> ui::Spec
                                {
                                    // The name, and an inner repeater that runs horizontally.
                                    return ui::VerticalStack{.content = {
                                        ui::Text{.text = category.name, .role = ui::TextRole::Title, .margin = ui::Thickness{8.0f}},
                                        ui::ForEach<std::string>{
                                            .items = category.items,
                                            .layout     = ui::StackLayout{.orientation = ui::Orientation::Horizontal},
                                            .virtualize = true,
                                            .item  = [](const std::string& text) -> ui::Spec
                                            { return ui::Text{.text = text, .margin = ui::Thickness{10.0f}}; },
                                        },
                                    }};
                                },
                            },
                        }
                    )cpp",
                    .stretch  = true,
                    .controls = {ui::Scroll{.content_orientation              = ui::ScrollContentOrientation::Both,
                                            .horizontal_scroll_mode           = ui::ScrollMode::Auto,
                                            .horizontal_scroll_bar_visibility = ui::ScrollBarVisibility::Auto,
                                            .content = ui::ForEach<Category>{
                                                .items = std::vector<Category>(Categories.begin(), Categories.end()),
                                                .virtualize         = true,
                                                .vertical_alignment = ui::Alignment::Leading,
                                                .item  = [](const Category& c) -> ui::Spec
                                                {
                                                    // A category: the name, and an inner horizontal repeater.
                                                    auto inner = std::make_unique<ui::StackLayoutEngine>();
                                                    inner->set_orientation(ui::Orientation::Horizontal);
                                                    return ui::VerticalStack{.content = {
                                                                            ui::Text{.text = c.name, .role = ui::TextRole::Title, .margin = ui::Thickness{8.0f}},
                                                                            ui::ForEach<std::string>{.items = std::vector<std::string>(c.items->begin(), c.items->end()),
                                                                                              .layout     = std::move(inner),
                                                                                              .virtualize = true,
                                                                                              .item  = [](const std::string& text) -> ui::Spec { return string_data(text); }},
                                                                        }};
                                                }}}},
                }),
                example({
                    .title    = "Animated Scrolling and Content Display",
                    .code     = R"cpp(
                        struct NamedColor
                        {
                            std::string name;
                            ui::Color   color;
                        };
                        std::vector<NamedColor> colors;
                        ui::Property<ui::Color> picked{ui::colors::Transparent};

                        ui::Scroll{
                            .width   = 250,
                            .height  = 175,
                            .content = ui::ForEach<NamedColor>{
                                .items = colors,
                                .virtualize = true,
                                // Each row scales with its distance from the middle of the viewport.
                                .on_element_prepared = [](const ui::ElementPreparedArgs& args)
                                {
                                    ui::Element* row = &args.element;
                                    args.element.start_animation(
                                        ui::AnimatedProperty::Scale,
                                        ui::ExpressionAnimation{ui::Callback<ui::Vector3()>{[row]
                                        {
                                            const float scale = scale_at(row->bounds().center().y); // 1 in the middle, less at the edges
                                            return ui::Vector3{scale, scale, 1.0f};
                                        }}});
                                },
                                .item  = [&](const NamedColor& c) -> ui::Spec
                                {
                                    return ui::Button{
                                        .text                 = c.name,
                                        .background           = c.color,
                                        .horizontal_alignment = ui::Alignment::Stretch,
                                        .on_click             = [&picked, color = c.color] { picked = color; },
                                    };
                                },
                            },
                        }

                        // The rectangle next to the list shows the color that was clicked.
                        ui::Rectangle{
                            .fill   = picked,
                            .width  = 150,
                            .height = 150,
                        }
                    )cpp",
                    .controls = {ui::Grid{
                        .columns = {1_fr, 1_fr},
                        .content =
                            {
                                ui::Cell{.row = 0, .column = 0, .content = std::move(animated)},
                                ui::Cell{.row     = 0,
                                 .column  = 1,
                                 .content = ui::Rectangle{.fill             = s.rectangle,
                                                          .stroke           = ui::Color::from_argb(0xFF808080),
                                                          .margin           = ui::Edges{.left = 10.0f},
                                                          .width            = 150.0f,
                                                          .height           = 150.0f,
                                                          .accessibility    = {.name = "Color rectangle"}}},
                            },
                    }},
                }),
                example({
                    .title    = "Virtualized, Content-Heavy Layout with Filtering and Sorting",
                    .code     = R"cpp(
                        std::vector<Recipe>          recipes; // all of them
                        ui::ObservableVector<Recipe> shown;   // the ones that match, in order

                        ui::Scroll{
                            .content = ui::ForEach<Recipe>{
                                .items = shown,
                                // The app's own ui::VirtualizingLayout: columns 200 wide, each item in the shortest.
                                .layout     = std::make_unique<VariedImageSizeLayout>(200.0f),
                                .virtualize = true,
                                .item  = [](const Recipe& recipe) -> ui::Spec
                                {
                                    return ui::VerticalStack{
                                        .margin  = ui::Thickness{5.0f},
                                        .content = {
                                            ui::Text{.text = recipe.name, .role = ui::TextRole::Title, .wrapping = ui::TextWrapping::Wrap},
                                            ui::Text{.text = recipe.ingredient_text(), .wrapping = ui::TextWrapping::Wrap},
                                        },
                                    };
                                },
                            },
                        }

                        ui::TextBox{
                            .header          = "Filter by ingredient...",
                            .width           = 200,
                            .on_text_changed = [&](const std::string& filter)
                            {
                                // Assigning the collection is all it takes: the repeater follows it.
                                shown.assign(matching(recipes, filter));
                            },
                        }
                    )cpp",
                    .stretch  = true,
                    .controls = {ui::Grid{
                        .columns = {1_fr, 1_fr},
                        .height  = 600.0f,
                        .content =
                            {
                                ui::Cell{.row = 0, .column = 0, .content = std::move(varied)},
                                ui::Cell{.row     = 0,
                                 .column  = 1,
                                 .content = ui::VerticalStack{.margin   = ui::Edges{.left = 10.0f},
                                                         .content = {
                                                             ui::TextBox{.header          = "Filter by ingredient...",
                                                                         .margin          = ui::Edges{.bottom = 20.0f},
                                                                         .width           = 200.0f,
                                                                         .horizontal_alignment         = ui::Alignment::Leading,
                                                                         .on_text_changed = [&s](const std::string& t)
                                                                         {
                                                                             s.filter = t;
                                                                             s.update_recipes();
                                                                         }},
                                                             ui::Text{.text = "Sort by number of ingredients", .margin = ui::Edges{.bottom = 10.0f}},
                                                             ui::Button{.text     = "Least to most",
                                                                        .margin   = ui::Edges{.bottom = 5.0f},
                                                                        .horizontal_alignment  = ui::Alignment::Leading,
                                                                        .on_click = [&s]
                                                                        {
                                                                            if (s.descending)
                                                                            {
                                                                                s.descending = false;
                                                                                s.update_recipes();
                                                                            }
                                                                        }},
                                                             ui::Button{.text     = "Most to least",
                                                                        .horizontal_alignment  = ui::Alignment::Leading,
                                                                        .on_click = [&s]
                                                                        {
                                                                            if (!s.descending)
                                                                            {
                                                                                s.descending = true;
                                                                                s.update_recipes();
                                                                            }
                                                                        }},
                                                         }}},
                            },
                    }},
                }),
            });
        });
}

// ---- TileView: layouts and invocation ----------------------------------------------------------

struct TileLayoutsState
{
    std::vector<CustomDataObject> items = data_objects(true);
    ui::Property<std::string>     basic_output;
    // Swappable layouts.
    ui::Control<ui::TileView<CustomDataObject>> swappable;
    int                layout = 0; // LinedFlowLayoutEngine, TileLayoutEngine, StackLayoutEngine
    ui::Property<bool> show_lined{true};
    ui::Property<bool> show_uniform{false};
    ui::Property<bool> show_stack{false};
    float              line_spacing   = 5.0f;
    float              item_spacing   = 5.0f;
    bool               large_lines    = true;
    float              stack_spacing  = 5.0f;
    float              column_spacing = 5.0f;
    float              row_spacing    = 5.0f;
    std::size_t        max_columns    = 3;
    // Invocation and selection.
    ui::Control<ui::TileView<CustomDataObject>> selectable;
    ui::Property<std::string>                   invocation_output;
    ui::Property<std::string>                   selection_output;

    void apply()
    {
        if (swappable == nullptr)
        {
            return;
        }
        show_lined   = layout == 0;
        show_uniform = layout == 1;
        show_stack   = layout == 2;
        switch (layout)
        {
            case 0:
            {
                auto* lined = swappable->set_layout(std::make_unique<ui::LinedFlowLayoutEngine>());
                lined->set_items_stretch(ui::LinedFlowLayoutItemsStretch::Fill);
                lined->set_line_height(large_lines ? 160.0f : 80.0f);
                lined->set_line_spacing(line_spacing);
                lined->set_min_item_spacing(item_spacing);
                swappable->set_item_template([this](std::size_t i) -> std::unique_ptr<ui::Element>
                                             { return ui::build(overlay_template(items[i], 0.0f, 70.0f)); });
                break;
            }
            case 1:
            {
                auto* grid = swappable->set_layout(std::make_unique<ui::TileLayoutEngine>());
                grid->set_min_row_spacing(row_spacing);
                grid->set_min_column_spacing(column_spacing);
                grid->set_maximum_rows_or_columns(max_columns);
                swappable->set_item_template([this](std::size_t i) -> std::unique_ptr<ui::Element>
                                             { return ui::build(overlay_template(items[i], 150.0f)); });
                break;
            }
            default:
            {
                swappable->set_layout(std::make_unique<ui::StackLayoutEngine>())->set_spacing(stack_spacing);
                swappable->set_item_template(
                    [this](std::size_t i) -> std::unique_ptr<ui::Element>
                    {
                        // An item: the photo beside its title and description.
                        const CustomDataObject& o = items[i];
                        return ui::build(ui::RelativePanel{
                            .width      = 480.0f,
                            .min_height = 80.0f,
                            .max_height = 100.0f,
                            .content    = {
                                ui::RelativeItem{.name                  = "image",
                                                 .align_left_with_panel = true,
                                                 .align_top_with_panel  = true,
                                                 .content = ui::Image{.source  = ui::ImageSource{sample_photo(o.image)},
                                                                      .stretch = ui::Stretch::UniformToFill,
                                                                      .margin  = ui::Edges{.top = 4.0f},
                                                                      .width   = 24.0f,
                                                                      .height  = 16.0f}},
                                ui::RelativeItem{.name                 = "title",
                                                 .right_of             = "image",
                                                 .align_top_with_panel = true,
                                                 .content              = ui::Text{.text   = o.title,
                                                                                  .role   = ui::TextRole::BodyStrong,
                                                                                  .margin = ui::Edges{.left = 8.0f}}},
                                ui::RelativeItem{.below = "title",
                                                 .content = ui::Text{.text     = o.description,
                                                                     .role     = ui::TextRole::Caption,
                                                                     .wrapping = ui::TextWrapping::Wrap,
                                                                     .trimming = ui::TextTrimming::WordEllipsis,
                                                                     .margin = ui::Edges{.top = 4.0f, .right = 8.0f, .bottom = 4.0f}}},
                            }});
                    });
                break;
            }
        }
        swappable->set_item_count(items.size());
    }
};

ui::Spec tile_view_layouts(Context&)
{
    return with_state<TileLayoutsState>(
        [](TileLayoutsState& s)
        {
            const auto number_box =
                [](const char* header, double value, double min, double max, ui::Callback<void(double)> changed)
            {
                return ui::NumberBox{.header                = header,
                                     .value                 = value,
                                     .min                   = min,
                                     .max                   = max,
                                     .spin_button_placement = ui::NumberBoxSpinButtonPlacementMode::Inline,
                                     .margin                = ui::Edges{.bottom = 16.0f},
                                     .max_width             = 250.0f,
                                     .on_value_changed = [changed = std::move(changed)](std::optional<double> v) mutable
                                     {
                                         if (v)
                                         {
                                             changed(*v);
                                         }
                                     }};
            };
            const auto section = [](const char* title, ui::Property<bool>& visible, ui::SpecList options)
            {
                ui::SpecList children;
                children.push_back(ui::Text{.text   = title,
                                            .role   = ui::TextRole::BodyStrong,
                                            .margin = ui::Edges{.top = 15.0f, .bottom = 10.0f}});
                for (auto& o : options.nodes)
                {
                    children.nodes.push_back(std::move(o));
                }
                return ui::build(
                    ui::VerticalStack{.min_height = 300.0f, .visible = visible, .content = std::move(children)});
            };
            const auto finite = [](double v, double fallback) { return std::isfinite(v) ? v : fallback; };

            // Stretch up to 500 rather than a fixed 500, so a narrow window does not cut the view off.
            auto view = ui::build(
                ui::TileView<CustomDataObject>{.items                = s.items,
                                               .height               = 400.0f,
                                               .max_width            = 500.0f,
                                               .horizontal_alignment = ui::Alignment::Stretch,
                                               .control              = s.swappable,
                                               .item                 = [](const CustomDataObject& o) -> ui::Spec
                                               { return overlay_template(o, 0.0f, 70.0f); }});
            s.apply();

            return examples({
                example({
                    .title    = "A TileView with a StackLayout, whose items can be invoked",
                    .code     = R"cpp(
                        struct Photo
                        {
                            std::string     title;
                            ui::ImageSource image;
                        };
                        std::vector<Photo>        photos; // filled by the app
                        ui::Property<std::string> output;

                        ui::VerticalStack{
                            .content = {
                                ui::TileView<Photo>{
                                    .items                   = photos,
                                    .selection_mode          = ui::SelectionMode::None,
                                    .layout                  = ui::StackLayout{},
                                    .width                   = 220,
                                    .height                  = 400,
                                    .horizontal_alignment    = ui::Alignment::Leading,
                                    .on_item_invoked = [&](const Photo& photo) { output = "You invoked " + photo.title + "."; },
                                    .item  = [](const Photo& photo) -> ui::Spec
                                    {
                                        return ui::Image{
                                            .source  = photo.image,
                                            .stretch = ui::Stretch::UniformToFill,
                                            .margin  = ui::Thickness{4.0f},
                                            .width   = 192,
                                            .height  = 132,
                                        };
                                    },
                                },
                                ui::Text{.text = output},
                            },
                        }
                    )cpp",
                    .stacked  = true,
                    .output   = ui::Text{.text = s.basic_output},
                    .controls = {ui::Text{.text = "A TileView whose layout is a StackLayout, with nothing selectable. "
                                                  "Click an item, or press Enter on it, to invoke it.",
                                          .margin = ui::Edges{.bottom = 15.0f}},
                                 ui::TileView<CustomDataObject>{.items = s.items,
                                               .selection_mode          = ui::SelectionMode::None,
                                               .layout                  = ui::StackLayout{},
                                               .width                   = 220.0f,
                                               .height                  = 400.0f,
                                               .horizontal_alignment                 = ui::Alignment::Leading,
                                               .on_item_invoked = [&s](const CustomDataObject& o) { s.basic_output = "You invoked " + o.title + "."; },
                                               .item  = [](const CustomDataObject& o) -> ui::Spec
                                               {
                                                   // A 200x140 cell, the photo 4 in.
                                                   return ui::Image{.source  = ui::ImageSource{sample_photo(o.image)},
                                                                    .stretch = ui::Stretch::UniformToFill,
                                                                    .margin  = ui::Thickness{4.0f},
                                                                    .width   = 192.0f,
                                                                    .height  = 132.0f};
                                               }}},
                }),
                example({
                    .title    = "A TileView with swappable layouts",
                    .code     = R"cpp(
                        ui::Control<ui::TileView<Photo>> view;

                        ui::TileView<Photo>{
                            .items = photos,
                            .layout = ui::LinedFlowLayout{
                                .line_height      = 160.0f,
                                .min_item_spacing = 5,
                                .line_spacing     = 5,
                                .items_stretch    = ui::LinedFlowLayoutItemsStretch::Fill,
                            },
                            .height               = 400,
                            .max_width            = 500,
                            .horizontal_alignment = ui::Alignment::Stretch,
                            .control              = view,
                            .item  = [](const Photo& photo) -> ui::Spec
                            {
                                return ui::Image{.source = photo.image, .stretch = ui::Stretch::UniformToFill};
                            },
                        }

                        // The Layout options swap the layout through the handle.
                        auto* grid = view->set_layout(std::make_unique<ui::TileLayoutEngine>());
                        grid->set_min_row_spacing(5.0f);
                        grid->set_min_column_spacing(5.0f);
                        grid->set_maximum_rows_or_columns(3);

                        view->set_layout(std::make_unique<ui::StackLayoutEngine>())->set_spacing(5.0f);
                    )cpp",
                    .stacked  = true,
                    .options  = options_panel(
                        300.0f,
                        {
                            ui::RadioButtons{.header         = "Layout",
                                             .items          = {"LinedFlowLayout", "TileLayout", "StackLayout"},
                                             .selected_index = 0,
                                             .on_selection_changed = [&s](std::optional<std::size_t> i)
                                             {
                                                 s.layout = static_cast<int>(i.value_or(0));
                                                 s.apply();
                                             }},
                            section("LinedFlowLayout settings",
                                    s.show_lined,
                                    {
                                        number_box("Space between lines", 5.0, 0.0, 100.0,
                                                   [&s, finite](double v)
                                                   {
                                                       s.line_spacing = static_cast<float>(finite(v, 0.0));
                                                       s.apply();
                                                   }),
                                        number_box("Minimum space between items on a line", 5.0, 0.0, 100.0,
                                                   [&s, finite](double v)
                                                   {
                                                       s.item_spacing = static_cast<float>(finite(v, 0.0));
                                                       s.apply();
                                                   }),
                                        ui::RadioButtons{.header         = "Line height",
                                                         .items          = {"Small", "Large"},
                                                         .selected_index = 1,
                                                         .on_selection_changed = [&s](std::optional<std::size_t> i)
                                                         {
                                                             s.large_lines = i == 1;
                                                             s.apply();
                                                         }},
                                    }),
                            section("StackLayout settings",
                                    s.show_stack,
                                    {number_box("Space between rows", 5.0, 0.0, 100.0,
                                                [&s, finite](double v)
                                                {
                                                    s.stack_spacing = static_cast<float>(finite(v, 0.0));
                                                    s.apply();
                                                })}),
                            section("TileLayout settings",
                                    s.show_uniform,
                                    {
                                        number_box("Minimum space between columns", 5.0, 0.0, 100.0,
                                                   [&s, finite](double v)
                                                   {
                                                       s.column_spacing = static_cast<float>(finite(v, 0.0));
                                                       s.apply();
                                                   }),
                                        number_box("Minimum space between rows", 5.0, 0.0, 100.0,
                                                   [&s, finite](double v)
                                                   {
                                                       s.row_spacing = static_cast<float>(finite(v, 0.0));
                                                       s.apply();
                                                   }),
                                        number_box("Maximum number of items per row before wrapping", 3.0, 1.0, 8.0,
                                                   [&s, finite](double v)
                                                   {
                                                       s.max_columns = static_cast<std::size_t>(finite(v, 3.0));
                                                       s.apply();
                                                   }),
                                    }),
                        },
                        0.0f),
                    .controls = {ui::Text{.text     = "Use the options on the right to swap the layout of the TileView below and change its spacing.",
                                          .wrapping = ui::TextWrapping::Wrap,
                                          .margin   = ui::Edges{.bottom = 15.0f}},
                                 std::move(view)},
                }),
                example({
                    .title    = "TileView item invocation and selection",
                    .code     = R"cpp(
                        ui::Control<ui::TileView<Photo>> view;
                        ui::Property<std::string>        invocation_output;
                        ui::Property<std::string>        selection_output;

                        ui::TileView<Photo>{
                            .items          = photos,
                            .selection_mode = ui::SelectionMode::Multiple,
                            .layout         = ui::TileLayout{
                                .min_row_spacing         = 5,
                                .min_column_spacing      = 5,
                                .maximum_rows_or_columns = 3,
                            },
                            .height               = 400,
                            .max_width            = 500,
                            .horizontal_alignment = ui::Alignment::Stretch,
                            .on_selection_changed = [&]
                            {
                                selection_output = std::format("You have selected {} item(s).", view->selected_items().size());
                            },
                            .on_item_invoked      = [&](const Photo& photo) { invocation_output = "You invoked " + photo.title + "."; },
                            .control = view,
                            .item  = [](const Photo& photo) -> ui::Spec
                            {
                                return ui::Image{.source = photo.image, .width = 150};
                            },
                        }

                        // The options change the view through its handle.
                        view->set_selection_mode(ui::SelectionMode::Extended);
                        view->set_item_invoked_enabled(true);
                    )cpp",
                    .stacked  = true,
                    .output   = ui::VerticalStack{.content = {ui::Text{.text = s.invocation_output}, ui::Text{.text = s.selection_output}}},
                    .options  = ui::Grid{
                        .columns    = {ui::AutoSize, 1_fr},
                        .rows       = {ui::AutoSize, ui::AutoSize},
                        .min_width  = 200.0f,
                        .content =
                            {
                                ui::Cell{.row = 0, .column = 0, .content = ui::Text{.text = "Selection mode", .margin = ui::Edges{.right = 10.0f}, .vertical_alignment = ui::Alignment::Center}},
                                ui::Cell{.row     = 0,
                                 .column  = 1,
                                 .content = ui::ComboBox{.items                = {"None", "Single", "Multiple", "Extended"},
                                                         .selected_index       = 2,
                                                         .accessibility        = {.name = "selection mode"},
                                                         .on_selection_changed = [&s](std::optional<std::size_t> i)
                                                         {
                                                             if (s.selectable != nullptr)
                                                             {
                                                                 s.selectable->set_selection_mode(static_cast<ui::SelectionMode>(i.value_or(0)));
                                                             }
                                                         }}},
                                ui::Cell{.row = 1, .column = 0, .content = ui::Text{.text = "Item invoked", .margin = ui::Edges{.right = 10.0f}, .vertical_alignment = ui::Alignment::Center}},
                                ui::Cell{.row     = 1,
                                 .column  = 1,
                                 .content = ui::CheckBox{.accessibility      = {.name = "is item invocation enabled?"},
                                                         .on_checked_changed = [&s](std::optional<bool> v)
                                                         {
                                                             s.invocation_output = "";
                                                             if (s.selectable != nullptr)
                                                             {
                                                                 s.selectable->set_item_invoked_enabled(v.value_or(false));
                                                             }
                                                         }}},
                            }},
                    .controls = {ui::RichText{.blocks = {
                                                  ui::Paragraph{.inlines = {ui::Inline{.text = "You can enable four different selection modes on the right."}}},
                                                  ui::Paragraph{.inlines = {ui::Inline{.text = "None", .weight = ui::FontWeight::Bold}, ui::Inline{.text = " disables selection all together."}}},
                                                  ui::Paragraph{.inlines = {ui::Inline{.text = "Single", .weight = ui::FontWeight::Bold}, ui::Inline{.text = " allows for only one item to be selected in the collection."}}},
                                                  ui::Paragraph{.inlines = {ui::Inline{.text = "Multiple", .weight = ui::FontWeight::Bold},
                                                             ui::Inline{.text = " causes checkboxes to appear within the items, so that multiple items can be chosen from the collection."}}},
                                                  ui::Paragraph{.inlines = {ui::Inline{.text = "Extended", .weight = ui::FontWeight::Bold},
                                                             ui::Inline{.text = " allows the user to select multiple items by using Ctrl+Click to select the individual items they want, "
                                                                    "or Shift+Click to select a range of contiguous items."}}},
                                              },
                                              .margin = ui::Edges{.bottom = 15.0f}},
                                 ui::TileView<CustomDataObject>{.items = s.items,
                                               .selection_mode  = ui::SelectionMode::Multiple,
                                               .layout = [] {
                                                   auto grid = std::make_unique<ui::TileLayoutEngine>();
                                                   grid->set_maximum_rows_or_columns(3);
                                                   grid->set_min_column_spacing(5.0f);
                                                   grid->set_min_row_spacing(5.0f);
                                                   return grid;
                                               }(),
                                               .height          = 400.0f,
                                               .max_width       = 500.0f,
                                               .horizontal_alignment         = ui::Alignment::Stretch,
                                               .on_selection_changed = [&s]
                                               {
                                                   if (s.selectable != nullptr)
                                                   {
                                                       s.selection_output = std::format("You have selected {} item(s).", s.selectable->selected_items().size());
                                                   }
                                               },
                                               .on_item_invoked = [&s](const CustomDataObject& o) { s.invocation_output = "You invoked " + o.title + "."; },
                                               .control = s.selectable,
                                               .item  = [](const CustomDataObject& o) -> ui::Spec
                                               { return overlay_template(o, 150.0f); }}},
                }),
            });
        });
}

// Colors filed by family. The families are mixed, and the groups follow the order each family
// first appears.
struct Swatch
{
    std::string name;
    std::string family;
    ui::Color   color;
};

ui::Spec swatch_tile(const Swatch& swatch)
{
    return ui::VerticalStack{.spacing = 4.0f,
                             .width   = 96.0f,
                             .content = {ui::Rectangle{.fill = swatch.color, .width = 96.0f, .height = 56.0f},
                                         ui::Text{.text = swatch.name}}};
}

ui::Spec tile_view_grouped(Context&)
{
    std::vector<Swatch> swatches{
        {"Crimson", "Reds", ui::Color::from_argb(0xFFDC143C)},
        {"Navy", "Blues", ui::Color::from_argb(0xFF000080)},
        {"Tomato", "Reds", ui::Color::from_argb(0xFFFF6347)},
        {"Olive", "Greens", ui::Color::from_argb(0xFF808000)},
        {"Teal", "Blues", ui::Color::from_argb(0xFF008080)},
        {"Maroon", "Reds", ui::Color::from_argb(0xFF800000)},
        {"Lime", "Greens", ui::Color::from_argb(0xFF32CD32)},
        {"Sky", "Blues", ui::Color::from_argb(0xFF87CEEB)},
        {"Coral", "Reds", ui::Color::from_argb(0xFFFF7F50)},
    };
    ui::Spec view = ui::TileView<Swatch>{
        .items                 = std::move(swatches),
        .group_by              = [](const Swatch& swatch) { return swatch.family; },
        .height                = 360.0f,
        .accessibility         = {.name = "Colors by family"},
        .item                  = [](const Swatch& swatch) { return swatch_tile(swatch); },
        .group_header_template = [](const std::string& family) -> ui::Spec
        { return ui::Text{.text = family, .role = ui::TextRole::Subtitle}; },
    };
    return example({
        .title    = "TileView with grouped headers.",
        .code     = R"cpp(
            struct Swatch
            {
                std::string name;
                std::string family;
                ui::Color   color;
            };
            std::vector<Swatch> swatches; // Crimson (Reds), Navy (Blues), Tomato (Reds), ...

            ui::TileView<Swatch>{
                .items                 = swatches,
                // One group per family, in the order the families first appear.
                .group_by              = [](const Swatch& swatch) { return swatch.family; },
                .height                = 360,
                .item                  = [](const Swatch& swatch) -> ui::Spec { /* a color and its name */ },
                .group_header_template = [](const std::string& family) -> ui::Spec
                { return ui::Text{.text = family, .role = ui::TextRole::Subtitle}; },
            }
        )cpp",
        .controls = {ui::Text{.text     = "Each family's header takes a line of its own and stays at the top while its "
                                          "colors scroll under it.",
                              .wrapping = ui::TextWrapping::Wrap},
                     std::move(view)},
    });
}

ui::Spec tile_view_page(Context& ctx)
{
    return examples({tile_view_basics(ctx), tile_view_layouts(ctx), tile_view_grouped(ctx)});
}

// ---- ListView ------------------------------------------------------------------------------------

// A contact list's own copy of the people, which its handlers change. The view follows the
// collection, so a handler only edits `items`.
struct ContactList
{
    ui::ObservableVector<Contact>      items;
    ui::Control<ui::ListView<Contact>> view;

    ContactList() = default;
    explicit ContactList(const std::vector<Contact>& people)
    {
        items.assign(people);
    }
};

ui::Spec contact_list(ContactList& list, ui::SelectionMode mode, float width, float height, ui::Alignment h = ui::Alignment::Leading)
{
    return ui::ListView<Contact>{.items                = list.items,
                                 .selection_mode       = mode,
                                 .border_brush         = list_border(),
                                 .border_thickness     = ui::Thickness{1.0f},
                                 .width                = width,
                                 .height               = height,
                                 .horizontal_alignment = h,
                                 .control              = list.view,
                                 .item = [](const Contact& contact) -> ui::Spec { return contact_template(contact); }};
}

struct Message
{
    std::string text;
    std::string time;
    bool        sent = true; // drawn at the right
};

std::string now_text()
{
    const auto now = std::chrono::zoned_time{std::chrono::current_zone(), std::chrono::system_clock::now()};
    return std::format("{:%m/%d/%Y %I:%M:%S %p}", std::chrono::floor<std::chrono::seconds>(now.get_local_time()));
}

struct ListViewState
{
    std::vector<Contact>          all = contacts();
    ContactList                   basic{all};
    ContactList                   selection{all};
    ContactList                   drag_left{all};
    ContactList                   drag_right{std::vector<Contact>{{"John", "Doe", "ABC Printers"},
                                                                  {"Jane", "Doe", "XYZ Refrigerators"},
                                                                  {"Santa", "Claus", "North Pole Toy Factory Inc."}}};
    ContactList                   grouped{};
    ContactList                   filtered{all};
    std::string                   first_filter, last_filter, company_filter;
    ContactList                   context_menu{all};
    ui::ObservableVector<Message> messages;
    ui::Control<ui::ListView<Message>>     messaging;
    int                                    message_number = 0;
    ui::ObservableVector<CustomDataObject> images;
    std::vector<std::string>               numbered5;
    std::vector<std::string>               numbered6;
    ui::Control<ui::ListView<std::string>> restore;
    std::string                            saved_position;
    ui::Property<bool>                     can_restore{false};
    ui::Control<ui::ListView<std::string>> scroll_into;
    double                                 scroll_index = 10.0;
    bool                                   leading      = false;

    ListViewState()
    {
        images.assign(data_objects());
        // Sorted by the last name's initial, so the groups come in key order.
        std::vector<Contact> sorted = all;
        std::stable_sort(sorted.begin(),
                         sorted.end(),
                         [](const Contact& a, const Contact& b) { return a.last.substr(0, 1) < b.last.substr(0, 1); });
        for (const Contact& c : sorted)
        {
            grouped.items.push_back(c);
        }
        for (std::size_t i = 0; i < all.size(); ++i)
        {
            numbered5.push_back(std::format("{}. {}", i + 1, all[i].name()));
        }
        int n = 1;
        for (int k = 0; k < 10; ++k)
        {
            for (const Contact& c : all)
            {
                numbered6.push_back(std::format("{}. {}", n++, c.name()));
            }
        }
        add_message(true); // the first message, so the log is not empty
    }
    void add_message(bool sent)
    {
        messages.push_back(Message{"Message " + std::to_string(++message_number), now_text(), sent});
    }
    [[nodiscard]] bool matches(const Contact& c) const
    {
        const auto has = [](const std::string& text, const std::string& part)
        { return ui::text_analyzer().to_lower(text).find(ui::text_analyzer().to_lower(part)) != std::string::npos; };
        return has(c.first, first_filter) && has(c.last, last_filter) && has(c.company, company_filter);
    }
    void filter()
    {
        // Remove the contacts that don't match from the end, then add back the missing ones at the end.
        for (std::size_t i = filtered.items.size(); i-- > 0;)
        {
            if (!matches(filtered.items[i]))
            {
                filtered.items.erase(i);
            }
        }
        for (const Contact& c : all)
        {
            const bool present = std::any_of(filtered.items.begin(),
                                             filtered.items.end(),
                                             [&](const Contact& f) { return f.name() == c.name(); });
            if (matches(c) && !present)
            {
                filtered.items.push_back(c);
            }
        }
    }
};

// The ListView page's drag lists: each reorders itself and takes contacts from the other.
void wire_drag_list(ContactList& self, ContactList& other, bool hide_glyph)
{
    ui::ListViewControl* view = self.view;
    view->set_can_drag_items(true);
    view->set_can_reorder_items(true);
    view->set_allow_drop(true);
    // A reorder within one list needs nothing here: the view moves the contact in `self.items`.
    view->on_drag_over(
        [hide_glyph]
        { return ui::DragResponse{.operation = ui::DataPackageOperation::Move, .glyph_visible = !hide_glyph}; });
    view->on_drop(
        [&self, &other, view](const ui::DragEventArgs& e)
        {
            const auto* payload = std::any_cast<ui::ListViewDragPayload>(&e.data.data);
            if (payload == nullptr || payload->source != other.view)
            {
                return;
            }
            // ListView_Drop: insert where it was dropped, remove it from where it came from.
            std::vector<std::size_t> from = payload->items;
            std::sort(from.begin(), from.end());
            std::size_t at = view->insertion_index_at(e.position);
            for (std::size_t index : from)
            {
                self.items.insert(at++, Contact{other.items[index]});
            }
            for (std::size_t k = from.size(); k-- > 0;)
            {
                other.items.erase(from[k]);
            }
        });
}

ui::Spec list_view_page(Context&)
{
    return with_state<ListViewState>(
        [](ListViewState& s)
        {
            const auto numbered = [](std::vector<std::string>& items, ui::Control<ui::ListView<std::string>>& ref)
            {
                return ui::ListView<std::string>{.items                = items,
                                                 .border_brush         = list_border(),
                                                 .border_thickness     = ui::Thickness{1.0f},
                                                 .width                = 400.0f,
                                                 .height               = 220.0f,
                                                 .horizontal_alignment = ui::Alignment::Leading,
                                                 .control              = ref,
                                                 .item                 = [](const std::string& text) -> ui::Spec
                                                 {
                                                     return ui::Text{.text   = text,
                                                                     .margin = ui::Symmetric{.horizontal = 0.0f,
                                                                                             .vertical   = 8.0f}};
                                                 }};
            };

            // Drag and drop between two lists.
            auto drag_left = ui::build(
                contact_list(s.drag_left, ui::SelectionMode::Single, 0.0f, 400.0f, ui::Alignment::Stretch));
            auto drag_right = ui::build(
                contact_list(s.drag_right, ui::SelectionMode::Single, 0.0f, 400.0f, ui::Alignment::Stretch));
            drag_left->set_min_width(350.0f);
            drag_right->set_min_width(350.0f);
            drag_left->set_margin(ui::Thickness{12.0f});
            wire_drag_list(s.drag_left, s.drag_right, false);
            wire_drag_list(s.drag_right, s.drag_left, true); // Target_DragEnter hides the glyph

            // Grouped headers.
            auto grouped = ui::build(
                ui::ListView<Contact>{.items    = s.grouped.items,
                                      .group_by = [](const Contact& contact) { return contact.last.substr(0, 1); },
                                      .sticky_group_headers = false,
                                      .border_brush         = list_border(),
                                      .border_thickness     = ui::Thickness{1.0f},
                                      .width                = 400.0f,
                                      .height               = 400.0f,
                                      .horizontal_alignment = ui::Alignment::Leading,
                                      .control              = s.grouped.view,
                                      .item                 = [](const Contact& contact) -> ui::Spec
                                      { return contact_template(contact); },
                                      .group_header_template = [](const std::string& key) -> ui::Spec
                                      { return ui::Text{.text = key, .role = ui::TextRole::Title}; }});

            // Messaging: inverted, kept at its end.
            auto messaging = ui::build(
                ui::ListView<Message>{.items                          = s.messages,
                                      .selection_mode                 = ui::SelectionMode::None,
                                      .items_updating_scroll_mode     = ui::ItemsUpdatingScrollMode::KeepLastItemInView,
                                      .border_brush                   = list_border(),
                                      .border_thickness               = ui::Thickness{1.0f},
                                      .items_panel_vertical_alignment = ui::Alignment::Trailing,
                                      .item_content_horizontal_alignment = ui::Alignment::Stretch,
                                      .height                            = 400.0f,
                                      .control                           = s.messaging,
                                      .item                              = [](const Message& m) -> ui::Spec
                                      {
                                          // A message: a 350-wide highlight card at the right (sent) or left (received).
                                          auto card = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
                                          card->set_background(highlight);
                                          card->set_width(350.0f);
                                          card->set_min_height(75.0f);
                                          card->add(
                                              ui::Text{.text = m.text,
                                                       .text_style = ui::TextStyle{.font_size = 20.0f,
                                                                                   .horizontal = ui::TextAlign::Leading},
                                                       .foreground = highlight_text,
                                                       .margin     = ui::Edges{.left = 10.0f, .top = 10.0f}});
                                          card->add(
                                              ui::Text{.text = m.time,
                                                       .text_style = ui::TextStyle{.font_size = 15.0f,
                                                                                   .horizontal = ui::TextAlign::Leading},
                                                       .foreground = highlight_text,
                                                       .margin     = ui::Edges{.left = 10.0f, .bottom = 20.0f}});
                                          return ui::Grid{.margin               = ui::Thickness{4.0f},
                                                          .horizontal_alignment = m.sent ? ui::Alignment::Trailing
                                                                                         : ui::Alignment::Leading,
                                                          .content              = {
                                                              ui::Cell{.row = 0, .column = 0, .content = std::move(card)}}};
                                      }});

            return examples({
                example({
                    .title    = "Basic ListView with a simple item template",
                    .code     = R"cpp(
                        struct Contact
                        {
                            std::string first;
                            std::string last;
                            std::string company;
                            std::string name() const { return first + " " + last; }
                        };
                        ui::ObservableVector<Contact> contacts; // filled by the app

                        ui::ListView<Contact>{
                            .items = contacts,
                            .border_brush = [](const ui::Theme& theme) { return theme.palette.control_strong_stroke_default; },
                            .border_thickness      = ui::Thickness{1.0f},
                            .width                 = 350,
                            .height                = 400,
                            .horizontal_alignment  = ui::Alignment::Leading,
                            .item  = [](const Contact& contact) -> ui::Spec
                            { return ui::Text{.text = contact.name(), .margin = ui::Symmetric{.horizontal = 0.0f, .vertical = 5.0f}}; },
                        }
                    )cpp",
                    .stacked  = true,
                    .controls = {ui::Text{.text = "This is a basic ListView that has the full source code below. Other samples on this page display only the "
                                                  "additional code needed to customize a simple ListView like this one.",
                                          .wrapping = ui::TextWrapping::Wrap,
                                          .margin   = ui::Edges{.bottom = 16.0f}},
                                 ui::ListView<Contact>{.items = s.basic.items,
                                              .border_brush = list_border(),
                                              .border_thickness      = ui::Thickness{1.0f},
                                              .width                 = 350.0f,
                                              .height                = 400.0f,
                                              .horizontal_alignment               = ui::Alignment::Leading,
                                              .control                   = s.basic.view,
                                              .item  = [](const Contact& contact) -> ui::Spec
                                              { return ui::Text{.text = contact.name(), .margin = ui::Symmetric{.horizontal = 0.0f, .vertical = 5.0f}}; }}},
                }),
                example({
                    .title    = "ListView with Selection Support",
                    .code     = R"cpp(
                        ui::Control<ui::ListView<Contact>> list;

                        ui::ListView<Contact>{
                            .items = contacts,
                            .selection_mode = ui::SelectionMode::Single,
                            .width          = 400,
                            .height         = 400,
                            .control        = list,
                            .item  = [](const Contact& contact) -> ui::Spec
                            {
                                return ui::VerticalStack{.content = {
                                    ui::Text{.text = contact.name(), .role = ui::TextRole::BodyStrong},
                                    ui::Text{.text = contact.company},
                                }};
                            },
                        }

                        // The SelectionMode dropdown: None, Single, Multiple or Extended.
                        list->set_selection_mode(ui::SelectionMode::Multiple);
                    )cpp",
                    .stacked  = true,
                    .options  = ui::ComboBox{.header               = "Selection mode",
                                             .items                = {"None", "Single", "Multiple", "Extended"},
                                             .selected_index       = 1,
                                             .margin               = ui::Edges{.top = 12.0f},
                                             .on_selection_changed = [&s](std::optional<std::size_t> i)
                                             {
                                                 if (s.selection.view != nullptr)
                                                 {
                                                     s.selection.view->set_selection_mode(static_cast<ui::SelectionMode>(i.value_or(0)));
                                                 }
                                             }},
                    .controls = {ui::RichText{.blocks = {
                                                  ui::Paragraph{.inlines = {ui::Inline{.text = "You can enable four different selection modes on the right."}}},
                                                  ui::Paragraph{.inlines = {ui::Inline{.text = "None", .weight = ui::FontWeight::Bold}, ui::Inline{.text = " disables selection all together."}}},
                                                  ui::Paragraph{.inlines = {ui::Inline{.text = "Single", .weight = ui::FontWeight::Bold}, ui::Inline{.text = " allows for only one item to be selected in the list."}}},
                                                  ui::Paragraph{.inlines = {ui::Inline{.text = "Multiple", .weight = ui::FontWeight::Bold},
                                                             ui::Inline{.text = " causes checkboxes to appear next to items, so that multiple items can be chosen from the list."}}},
                                                  ui::Paragraph{.inlines = {ui::Inline{.text = "Extended", .weight = ui::FontWeight::Bold},
                                                             ui::Inline{.text = " allows the user to select multiple items by using Ctrl+Click to select the individual items they want, "
                                                                    "or Shift+Click to select a range of contiguous items."}}},
                                              },
                                              .margin = ui::Edges{.bottom = 15.0f}},
                                 contact_list(s.selection, ui::SelectionMode::Single, 400.0f, 400.0f)},
                }),
                example({
                    .title    = "ListViews with Drag, Drop, and Reordering Support",
                    .code     = R"cpp(
                        ui::ObservableVector<Contact>      left_contacts;
                        ui::ObservableVector<Contact>      right_contacts;
                        ui::Control<ui::ListView<Contact>> left;
                        ui::Control<ui::ListView<Contact>> right;

                        // The right list is the same, over right_contacts.
                        ui::ListView<Contact>{
                            .items = left_contacts,
                            .can_drag_items    = true,
                            .can_reorder_items = true,
                            // the list moves the contact in the collection itself
                            .allow_drop        = true, .height            = 400,
                            .control           = left,
                            .item  = [](const Contact& contact) -> ui::Spec { return ui::Text{.text = contact.name()}; },
                        }

                        // A drop from the other list: insert where it landed, remove where it came from.
                        left->on_drag_over([] { return ui::DragResponse{.operation = ui::DataPackageOperation::Move}; });
                        left->on_drop([&](const ui::DragEventArgs& e)
                        {
                            const auto* payload = std::any_cast<ui::ListViewDragPayload>(&e.data.data);
                            if (payload == nullptr || payload->source != right)
                            {
                                return;
                            }
                            std::size_t at = left->insertion_index_at(e.position);
                            for (std::size_t index : payload->items)
                            {
                                left_contacts.insert(at++, Contact{right_contacts[index]});
                            }
                            for (std::size_t k = payload->items.size(); k-- > 0;)
                            {
                                right_contacts.erase(payload->items[k]);
                            }
                        });
                    )cpp",
                    .stretch  = true,
                    .controls = {ui::Grid{
                        .columns = {1_fr, 1_fr},
                        .rows    = {ui::AutoSize, ui::AutoSize},
                        .content =
                            {
                                ui::Cell{.row     = 0,
                                 .column  = 0,
                                 .content = ui::Text{.text = "In these ListView controls, you can drag and drop within a list to reorder items, or drag and "
                                                             "drop between lists to move items.",
                                                     .wrapping = ui::TextWrapping::Wrap}},
                                ui::Cell{.row = 1, .column = 0, .content = std::move(drag_left)},
                                ui::Cell{.row = 1, .column = 1, .content = std::move(drag_right)},
                            },
                    }},
                }),
                example({
                    .title    = "ListView with Grouped Headers",
                    .code     = R"cpp(
                        // One group per initial of the last name. The groups come in the order their
                        // keys first appear, so the contacts are sorted by initial first.
                        ui::ObservableVector<Contact>      sorted;
                        ui::Control<ui::ListView<Contact>> list;

                        ui::ListView<Contact>{
                            .items                 = sorted,
                            .group_by              = [](const Contact& contact) { return contact.last.substr(0, 1); },
                            .sticky_group_headers  = false,
                            .width                 = 400,
                            .height                = 400,
                            .control               = list,
                            .item                  = [](const Contact& contact) -> ui::Spec { return ui::Text{.text = contact.name()}; },
                            .group_header_template = [](const std::string& key) -> ui::Spec
                            { return ui::Text{.text = key, .role = ui::TextRole::Title}; },
                        }

                        // The Sticky Headers switch.
                        list->set_sticky_group_headers(true);
                    )cpp",
                    .stacked  = true,
                    .options  = ui::ToggleSwitch{.header = "Sticky Headers", .checked = false, .on_checked_changed = [&s](bool on)
                                                 {
                                                     if (s.grouped.view != nullptr)
                                                     {
                                                         s.grouped.view->set_sticky_group_headers(on);
                                                     }
                                                 }},
                    .controls = {ui::Text{.text = "Switch the toggle on the right to enable sticky group headers, which makes the headers stay put at the "
                                                  "top of the ListView while scrolling.",
                                          .wrapping = ui::TextWrapping::Wrap,
                                          .margin   = ui::Edges{.bottom = 15.0f}},
                                 std::move(grouped)},
                }),
                example({
                    .title    = "ListView with filtering",
                    .code     = R"cpp(
                        std::vector<Contact>          all;      // everyone
                        ui::ObservableVector<Contact> filtered; // the ones the list shows
                        std::string                   first_name;

                        ui::ListView<Contact>{
                            .items = filtered,
                            // Rows that leave shrink away, rows that come back grow in.
                            .item_transitions = ui::TransitionList{ui::AddRemoveTransition{},
                                                                                   ui::ContentTransition{}},
                            .width  = 400,
                            .height = 400,
                            .item  = [](const Contact& contact) -> ui::Spec { return ui::Text{.text = contact.name()}; },
                        }

                        ui::TextBox{
                            .header          = "First name",
                            .on_text_changed = [&](const std::string& text)
                            {
                                first_name = text;
                                // Take out what no longer matches. The list follows each edit.
                                for (std::size_t i = filtered.size(); i-- > 0;)
                                {
                                    if (!matches(filtered[i]))
                                    {
                                        filtered.erase(i);
                                    }
                                }
                                // ... and push_back the contacts of `all` that match again ...
                            },
                        }
                    )cpp",
                    .options  = ui::VerticalStack{
                        .width    = 200.0f,
                        .content  =
                            {
                                ui::Text{.text = "Filter by...", .role = ui::TextRole::BodyStrong, .margin = ui::Edges{.left = 8.0f, .top = 8.0f, .right = 8.0f, .bottom = 4.0f}},
                                ui::TextBox{.header          = "First name",
                                            .margin          = ui::Thickness{8.0f},
                                            .horizontal_alignment         = ui::Alignment::Stretch,
                                            .on_text_changed = [&s](const std::string& t)
                                            {
                                                s.first_filter = t;
                                                s.filter();
                                            }},
                                ui::TextBox{.header          = "Last name",
                                            .margin          = ui::Thickness{8.0f},
                                            .horizontal_alignment         = ui::Alignment::Stretch,
                                            .on_text_changed = [&s](const std::string& t)
                                            {
                                                s.last_filter = t;
                                                s.filter();
                                            }},
                                ui::TextBox{.header          = "Company",
                                            .margin          = ui::Thickness{8.0f},
                                            .horizontal_alignment         = ui::Alignment::Stretch,
                                            .on_text_changed = [&s](const std::string& t)
                                            {
                                                s.company_filter = t;
                                                s.filter();
                                            }},
                            }},
                    .controls = {ui::ListView<Contact>{.items                      = s.filtered.items,
                                              .item_transitions = ui::TransitionList{ui::AddRemoveTransition{}, ui::ContentTransition{}},
                                              .border_brush      = list_border(),
                                              .border_thickness           = ui::Thickness{1.0f},
                                              .width                      = 400.0f,
                                              .height                     = 400.0f,
                                              .control                        = s.filtered.view,
                                              .item                       = [](const Contact& contact) -> ui::Spec { return contact_template(contact); }}},
                }),
                example({
                    .title    = "ListView for Messaging or Data Logging",
                    .code     = R"cpp(
                        struct Message
                        {
                            std::string text;
                            std::string time;
                            bool        sent = true;
                        };
                        ui::ObservableVector<Message> messages;

                        ui::ListView<Message>{
                            .items = messages,
                            .selection_mode = ui::SelectionMode::None,
                            // New messages stay in view, and a short log sits at the bottom.
                            .items_updating_scroll_mode     = ui::ItemsUpdatingScrollMode::KeepLastItemInView,
                            .items_panel_vertical_alignment = ui::Alignment::Trailing,
                            .height                         = 400,
                            .item  = [](const Message& message) -> ui::Spec
                            {
                                // Sent messages sit at the right, received ones at the left.
                                return ui::VerticalStack{
                                    .margin               = ui::Thickness{4.0f},
                                    .width                = 350,
                                    .horizontal_alignment = message.sent ? ui::Alignment::Trailing : ui::Alignment::Leading,
                                    .content              = {ui::Text{.text = message.text}, ui::Text{.text = message.time}},
                                };
                            },
                        }

                        ui::Button{.text = "Send Message", .on_click = [&] { messages.push_back(Message{"Hello", "12:00", true}); }}
                    )cpp",
                    .stacked  = true,
                    .options  = ui::VerticalStack{.horizontal_alignment  = ui::Alignment::Trailing,
                                             .content = {ui::Button{.text = "Send Message", .margin = ui::Edges{.bottom = 10.0f}, .on_click = [&s] { s.add_message(true); }},
                                                          ui::Button{.text = "Receive Message", .on_click = [&s] { s.add_message(false); }}}},
                    .controls = {ui::Text{.text     = "This ListView is inverted to grow from the bottom up. It's a good way to display logs or messages, with most recent at the bottom.",
                                          .wrapping = ui::TextWrapping::Wrap,
                                          .margin   = ui::Edges{.bottom = 15.0f}},
                                 std::move(messaging)},
                }),
                example({
                    .title    = "ListView with Images",
                    .code     = R"cpp(
                        struct Photo
                        {
                            std::string     title;
                            std::string     description;
                            ui::ImageSource image;
                        };
                        ui::ObservableVector<Photo> photos;

                        ui::ListView<Photo>{
                            .items = photos,
                            // A reorder needs no handler: the list moves the item in `photos` itself.
                            .can_drag_items    = true,
                            .can_reorder_items = true,
                            .allow_drop        = true,
                            .height            = 400,
                            .min_width         = 550,
                            .item  = [](const Photo& photo) -> ui::Spec
                            {
                                return ui::Grid{
                                    .columns = {ui::AutoSize, 1_fr},
                                    .margin  = ui::Symmetric{.horizontal = 0.0f, .vertical = 12.0f},
                                    .content   = {
                                        ui::Cell{.row     = 0,
                                         .column  = 0,
                                         .content = ui::Image{.source = photo.image, .min_width = 150, .max_height = 100}},
                                        ui::Cell{.row     = 0,
                                         .column  = 1,
                                         .content = ui::VerticalStack{
                                             .margin  = ui::Edges{.left = 12.0f},
                                             .content = {
                                                 ui::Text{.text = photo.title, .role = ui::TextRole::BodyStrong},
                                                 ui::Text{
                                                     .text     = photo.description,
                                                     .wrapping = ui::TextWrapping::Wrap,
                                                     .trimming = ui::TextTrimming::CharacterEllipsis,
                                                     .width    = 350,
                                                     .max_height = 60,
                                                 },
                                             },
                                         }},
                                    },
                                };
                            },
                        }
                    )cpp",
                    .stretch  = true,
                    .controls = {ui::ListView<CustomDataObject>{
                        .items = s.images,
                        .can_drag_items        = true,
                        .can_reorder_items     = true,
                        .allow_drop            = true,
                        .border_brush = list_border(),
                        // A reorder needs no handler: the list moves the item in `s.images` itself.
                        .border_thickness = ui::Thickness{1.0f},
                        .height                = 400.0f,
                        .min_width             = 550.0f,
                        .item  = [](const CustomDataObject& o) -> ui::Spec
                        {
                            ui::Control<ui::Text> description;
                            ui::Spec                row = ui::Grid{
                                .columns       = {ui::AutoSize, 1_fr},
                                .margin        = ui::Symmetric{.horizontal = 0.0f, .vertical = 12.0f},
                                .accessibility = {.name = o.title},
                                .content =
                                    {
                                        ui::Cell{.row = 0, .column = 0, .content = ui::Image{.source = ui::ImageSource{sample_photo(o.image)}, .stretch = ui::Stretch::Fill, .min_width = 150.0f, .max_height = 100.0f}},
                                        ui::Cell{.row     = 0,
                                         .column  = 1,
                                         .content = ui::VerticalStack{
                                             .margin   = ui::Edges{.left = 12.0f},
                                             .content  = {
                                                 ui::Text{.text = o.title, .role = ui::TextRole::BodyStrong, .margin = ui::Edges{.bottom = 6.0f}, .horizontal_alignment = ui::Alignment::Leading},
                                                 ui::Text{.text     = o.description,
                                                          .wrapping = ui::TextWrapping::Wrap,
                                                          .trimming = ui::TextTrimming::CharacterEllipsis,
                                                          .margin   = ui::Edges{.bottom = 10.0f},
                                                          .width    = 350.0f,
                                                          .max_height = 60.0f,
                                                          .control      = description},
                                                 ui::HorizontalStack{.content = {ui::Text{.text = o.views + " Views ", .role = ui::TextRole::Caption},
                                                                             ui::Text{.text = " ⋅ ", .role = ui::TextRole::Caption},
                                                                             ui::Text{.text = " " + o.likes + " Likes", .role = ui::TextRole::Caption, .margin = ui::Edges{.left = 5.0f}}}},
                                             }}},
                                    },
                            };
                            // Built here so `.control` is published: the trimmed-changed handler puts the whole
                            // description in a tooltip while it is trimmed.
                            std::unique_ptr<ui::Element> built = std::move(row).build();
                            if (description != nullptr)
                            {
                                description->on_text_trimmed_changed(
                                    [description](bool trimmed) { description->set_tool_tip(trimmed ? description->text() : std::string{}); });
                            }
                            return built;
                        }}},
                }),
                example({
                    .title    = "ListView with context menus",
                    .code     = R"cpp(
                        ui::ObservableVector<Contact> contacts;

                        ui::ListView<Contact>{
                            .items  = contacts,
                            .width  = 400,
                            .height = 400,
                            .item   = [](const Contact& contact) -> ui::Spec { return ui::Text{.text = contact.name()}; },
                            // A right-click on a row, or Shift+F10 on the current one, selects it and
                            // opens its menu. A contact stays where it is while it is in the collection,
                            // so Delete can keep a reference to it.
                            .item_context_menu = [&](const Contact& contact) -> std::vector<ui::MenuItem>
                            {
                                return {{.text = "Delete", .on_click = [&contacts, &contact] { contacts.remove(contact); }}};
                            },
                        }
                    )cpp",
                    .stacked  = true,
                    .controls = {ui::Text{.text     = "This ListView allows users to right click to open a context menu. In this case, the context menu provides the option to delete an entry.",
                                          .wrapping = ui::TextWrapping::Wrap,
                                          .margin   = ui::Edges{.bottom = 15.0f}},
                                 ui::ListView<Contact>{
                                     .items                = s.context_menu.items,
                                     .border_thickness     = ui::Thickness{1.0f},
                                     .width                = 400.0f,
                                     .height               = 400.0f,
                                     .horizontal_alignment = ui::Alignment::Leading,
                                     .control              = s.context_menu.view,
                                     .item = [](const Contact& contact) -> ui::Spec { return contact_template(contact); },
                                     .item_context_menu = [&s](const Contact& contact) -> std::vector<ui::MenuItem>
                                     {
                                         return {{.text     = "Delete",
                                                  .on_click = [&s, &contact] { s.context_menu.items.remove(contact); }}};
                                     }}},
                }),
                example({
                    .title    = "Save and restore the scroll position.",
                    .code     = R"cpp(
                        std::vector<std::string>               names; // "1. Kendall Collins", ...
                        ui::Control<ui::ListView<std::string>> list;
                        std::string                            saved;

                        ui::ListView<std::string>{
                            .items   = names,
                            .width   = 400,
                            .height  = 220,
                            .control = list,
                        }

                        // Save: the item at the top, named by a key of the app's choosing, and how far past it.
                        saved = list->relative_scroll_position([&](std::size_t index) { return names[index]; });

                        // Restore: the app finds the item with that key again.
                        list->set_relative_scroll_position(saved, [&](const std::string& key) -> std::size_t
                        {
                            const auto it = std::find(names.begin(), names.end(), key);
                            return it == names.end() ? std::nullopt : static_cast<std::size_t>(it - names.begin());
                        });
                    )cpp",
                    .options  = ui::VerticalStack{
                        .spacing  = 8.0f,
                        .content  = {ui::Button{.text     = "Save position",
                                                .on_click = [&s]
                                                {
                                                    // The item at the top and how far past it.
                                                    s.saved_position = s.restore->relative_scroll_position([&s](std::size_t i) { return s.numbered5[i]; });
                                                    s.can_restore    = true;
                                                }},
                                     ui::Button{.text     = "Restore position",
                                                .enabled  = s.can_restore,
                                                .on_click = [&s]
                                                {
                                                    if (!s.saved_position.empty())
                                                    {
                                                        (void)s.restore->set_relative_scroll_position(
                                                            s.saved_position,
                                                            [&s](const std::string& key) -> std::optional<std::size_t>
                                                            {
                                                                const auto it = std::find(s.numbered5.begin(), s.numbered5.end(), key);
                                                                if (it == s.numbered5.end())
                                                                {
                                                                    return std::nullopt;
                                                                }
                                                                return static_cast<std::size_t>(it - s.numbered5.begin());
                                                            });
                                                    }
                                                }}}},
                    .controls = {numbered(s.numbered5, s.restore)},
                }),
                example({
                    .title    = "Scroll a ListView to an item from code.",
                    .code     = R"cpp(
                        std::vector<std::string>               names;
                        ui::Control<ui::ListView<std::string>> list;
                        double                                 index = 10.0;

                        ui::ListView<std::string>{
                            .items   = names,
                            .width   = 400,
                            .height  = 220,
                            .control = list,
                        }

                        ui::NumberBox{.header = "Index", .value = 10.0, .min = 0.0, .max = 999.0, .on_value_changed = [&](std::optional<double> v) { index = v.value_or(0.0); }}

                        ui::Button{
                            .text     = "Scroll into view",
                            .on_click = [&]
                            {
                                // Default scrolls just far enough; Leading puts the item at the top.
                                list->scroll_into_view(static_cast<std::size_t>(index), ui::ScrollIntoViewAlignment::Leading);
                            },
                        }
                    )cpp",
                    .options  = ui::VerticalStack{
                        .spacing  = 8.0f,
                        .content  = {ui::NumberBox{.header                = "Index",
                                                   .value                 = 10.0,
                                                   .min                   = 0.0,
                                                   .max                   = 999.0,
                                                   .large_step            = 10.0,
                                                   .spin_button_placement = ui::NumberBoxSpinButtonPlacementMode::Inline,
                                                   .on_value_changed      = [&s](std::optional<double> v)
                                                   { s.scroll_index = v.value_or(0.0); }},
                                     ui::ComboBox{.header               = "Alignment",
                                                  .items                = {"Default", "Leading"},
                                                  .selected_index       = 0,
                                                  .on_selection_changed = [&s](std::optional<std::size_t> i) { s.leading = i == 1; }},
                                     ui::Button{.text     = "Scroll into view",
                                                .on_click = [&s]
                                                {
                                                    if (!std::isfinite(s.scroll_index) || s.scroll_index < 0.0 || s.scroll_index >= static_cast<double>(s.numbered6.size()))
                                                    {
                                                        return;
                                                    }
                                                    s.scroll_into->scroll_into_view(static_cast<std::size_t>(s.scroll_index),
                                                                                    s.leading ? ui::ScrollIntoViewAlignment::Leading : ui::ScrollIntoViewAlignment::Default);
                                                }}}},
                    .controls = {numbered(s.numbered6, s.scroll_into)},
                }),
            });
        });
}

// ---- TreeView ------------------------------------------------------------------------------------

// Stand-in for the sample's folder image: a yellow folder, 20 wide.
class Folder final : public ui::Element
{
public:
    [[nodiscard]] ui::Size measure_override(ui::Size, const ui::MeasureContext&) const override
    {
        return ui::Size{20.0f, 16.0f};
    }
    void render_override(ui::Renderer& renderer, const ui::RenderContext&) const override
    {
        const ui::Rect b   = bounds();
        const float    top = b.y + (b.height - 16.0f) * 0.5f;
        renderer.fill_rounded_rect(ui::Rect{b.x + 1.0f, top + 1.0f, 8.0f, 4.0f}, 1.0f, ui::Color::from_argb(0xFFE8A93A));
        renderer.fill_rounded_rect(ui::Rect{b.x + 1.0f, top + 3.0f, 18.0f, 12.0f}, 1.5f, ui::Color::from_argb(0xFFFFC83D));
    }
};

std::vector<ui::TreeNode> documents_tree()
{
    // The sample tree: work and personal documents.
    ui::TreeNode work{.text = "Work Documents",
                      .children = {ui::TreeNode{.text = "XYZ Functional Spec"}, ui::TreeNode{.text = "Feature Schedule"}},
                      .expanded = true};
    ui::TreeNode remodel{.text     = "Home Remodel",
                         .children = {ui::TreeNode{.text = "Contractor Contact Info"},
                                      ui::TreeNode{.text = "Paint Color Scheme"}},
                         .expanded = true};
    ui::TreeNode personal{.text = "Personal Documents", .children = {remodel}, .expanded = true};
    return {work, personal};
}

// ExplorerItem: a folder or a file, with the folder's contents.
enum class ExplorerItemType
{
    Folder,
    File,
};

struct ExplorerItem
{
    std::string               name;
    ExplorerItemType          type = ExplorerItemType::File;
    std::vector<ExplorerItem> children{};
};

std::vector<ExplorerItem> explorer_items()
{
    const auto file   = [](const char* name) { return ExplorerItem{.name = name}; };
    const auto folder = [](const char* name, std::vector<ExplorerItem> children)
    { return ExplorerItem{.name = name, .type = ExplorerItemType::Folder, .children = std::move(children)}; };
    return {folder("Documents", {file("ProjectProposal"), file("BudgetReport")}),
            folder("Projects", {file("Project Plan")})};
}

ui::Spec bordered_tree(float height, ui::Spec tree)
{
    auto box = std::make_unique<ui::BorderControl>();
    box->set_height(height);
    box->set_border_thickness(ui::Thickness{1.0f});
    box->set_border_brush(text_control_border);
    box->set_child(std::move(tree).build());
    return box;
}

ui::Spec tree_view_page(Context&)
{
    const auto tree = [](ui::TreeViewSelectionMode mode, std::vector<ui::TreeNode> items)
    {
        return ui::TreeView<ui::TreeNode>{.items                = std::move(items),
                                          .selection_mode       = mode,
                                          .can_drag_items       = true,
                                          .allow_drop           = true,
                                          .margin               = ui::Edges{.top = 12.0f},
                                          .min_width            = 345.0f,
                                          .max_height           = 400.0f,
                                          .horizontal_alignment = ui::Alignment::Center,
                                          .vertical_alignment   = ui::Alignment::Leading};
    };
    // The explorer cards: the app's own items, folders open from the start.
    const auto explorer = [](ui::Callback<ui::Spec(const ExplorerItem&)> row)
    {
        return ui::TreeView<ExplorerItem>{.items    = explorer_items(),
                                          .children = &ExplorerItem::children,
                                          .key      = &ExplorerItem::name,
                                          .expanded = [](const ExplorerItem& i)
                                          { return i.type == ExplorerItemType::Folder; },
                                          .selection_mode       = ui::TreeViewSelectionMode::Single,
                                          .margin               = ui::Edges{.top = 12.0f},
                                          .min_width            = 345.0f,
                                          .max_height           = 400.0f,
                                          .horizontal_alignment = ui::Alignment::Center,
                                          .vertical_alignment   = ui::Alignment::Leading,
                                          .item                 = std::move(row)};
    };
    return examples({
        example({.title    = "A simple TreeView with drag and drop support",
                 .code     = R"cpp(
                     // Nodes can be dragged to another place.
                     ui::TreeView<ui::TreeNode>{
                         .items = {
                             {.text     = "Work Documents",
                              .children = {{.text = "XYZ Functional Spec"}, {.text = "Feature Schedule"}},
                              .expanded = true},
                             {.text     = "Personal Documents",
                              .children = {{.text     = "Home Remodel",
                                            .children = {{.text = "Contractor Contact Info"}, {.text = "Paint Color Scheme"}},
                                            .expanded = true}},
                              .expanded = true},
                         },
                         .selection_mode = ui::TreeViewSelectionMode::Single,
                         .can_drag_items = true,
                         .allow_drop     = true,
                         .min_width      = 345,
                         .max_height     = 400,
                     }
                 )cpp",
                 .stretch  = true,
                 .controls = {bordered_tree(280.0f, tree(ui::TreeViewSelectionMode::Single, documents_tree()))}}),
        example({.title    = "A TreeView with Multi-selection enabled",
                 .code     = R"cpp(
                     // Multiple puts a check box on every row.
                     ui::TreeView<ui::TreeNode>{
                         .items = {
                             {.text     = "Work Documents",
                              .children = {{.text = "XYZ Functional Spec"}, {.text = "Feature Schedule"}},
                              .expanded = true},
                             {.text = "Personal Documents", .children = {{.text = "Home Remodel"}}, .expanded = true},
                         },
                         .selection_mode = ui::TreeViewSelectionMode::Multiple,
                         .min_width      = 345,
                         .max_height     = 400,
                     }
                 )cpp",
                 .stretch  = true,
                 .controls = {bordered_tree(280.0f, tree(ui::TreeViewSelectionMode::Multiple, documents_tree()))}}),
        example(
            {.title   = "A TreeView built from data",
             .code    = R"cpp(
                     enum class ExplorerItemType { Folder, File };

                     // The app's own items: the tree shows them as they are.
                     struct ExplorerItem
                     {
                         std::string               name;
                         ExplorerItemType          type = ExplorerItemType::File;
                         std::vector<ExplorerItem> children{};
                         friend bool operator==(const ExplorerItem&, const ExplorerItem&) = default;
                     };

                     ui::TreeView<ExplorerItem>{
                         .items          = explorer_items(),
                         .children       = &ExplorerItem::children,
                         .key            = &ExplorerItem::name,
                         .expanded       = [](const ExplorerItem& i) { return i.type == ExplorerItemType::Folder; },
                         .selection_mode = ui::TreeViewSelectionMode::Single,
                         .max_height     = 400,
                         .item           = [](const ExplorerItem& i) -> ui::Spec { return ui::Text{.text = i.name}; },
                     }
                 )cpp",
             .stretch = true,
             .controls = {bordered_tree(200.0f,
                                        explorer([](const ExplorerItem& i) -> ui::Spec
                                                 { return ui::Text{.text = i.name, .accessibility = {.name = i.name}}; }))}}),
        example({.title    = "A TreeView with an item template selector",
                 .code     = R"cpp(
                 ui::TreeView<ExplorerItem>{
                     .items          = explorer_items(),
                     .children       = &ExplorerItem::children,
                     .key            = &ExplorerItem::name,
                     .selection_mode = ui::TreeViewSelectionMode::Single,
                     .max_height     = 400,
                     .item           = [](const ExplorerItem& i) -> ui::Spec
                     {
                         // The row is chosen by the item: a folder or a file icon.
                         const bool          is_folder = i.type == ExplorerItemType::Folder;
                         const ui::IconGlyph glyph     = is_folder ? ui::IconGlyph{0xE8B7} : ui::IconGlyph{0xE8A5};
                         return ui::HorizontalStack{
                             .spacing       = 10,
                             .accessibility = {.name = i.name},
                             .content       = {
                                 ui::Icon{.source = glyph, .vertical_alignment = ui::Alignment::Center},
                                 ui::Text{.text = i.name},
                             },
                         };
                     },
                 }
             )cpp",
                 .stretch  = true,
                 .controls = {bordered_tree(200.0f,
                                            explorer(
                                                [](const ExplorerItem& i) -> ui::Spec
                                                {
                                                    // A folder or a file template, by the item's type.
                                                    ui::SpecList parts;
                                                    if (i.type == ExplorerItemType::Folder)
                                                    {
                                                        auto folder = std::make_unique<Folder>();
                                                        folder->set_width(20.0f);
                                                        parts.nodes.push_back(std::move(folder));
                                                    }
                                                    else
                                                    {
                                                        parts.push_back(
                                                            ui::Icon{.source             = ui::IconGlyph{0xE8A5},
                                                                     .vertical_alignment = ui::Alignment::Center});
                                                    }
                                                    parts.push_back(
                                                        ui::Text{.text = "", .margin = ui::Edges{.right = 10.0f}});
                                                    parts.push_back(ui::Text{.text = i.name});
                                                    return ui::HorizontalStack{.accessibility = {.name = i.name},
                                                                               .content       = std::move(parts)};
                                                }))}}),
    });
}

// ---- TableView ------------------------------------------------------------------------------------

struct TableItem
{
    int         id = 0;
    std::string name;
    std::string category;
    int         value                                          = 0;
    friend bool operator==(const TableItem&, const TableItem&) = default;
};

// "Item 1".."Item n", cycling through three categories and five values, as the sample's items do.
std::vector<TableItem> table_items(int count)
{
    constexpr std::array<const char*, 3> categories{"Alpha", "Beta", "Gamma"};
    constexpr std::array<int, 5>         values{25, 80, 50, 100, 10};
    std::vector<TableItem>               items;
    items.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        const auto at = static_cast<std::size_t>(i);
        items.push_back(
            {i + 1, std::format("Item {}", i + 1), categories[at % categories.size()], values[at % values.size()]});
    }
    return items;
}

enum class TableField
{
    Id,
    Name,
    Category,
    Value,
};

// The cell text of one field.
std::function<std::string(const TableItem&)> table_text(TableField field)
{
    return [field](const TableItem& item)
    {
        switch (field)
        {
            case TableField::Id:
                return std::to_string(item.id);
            case TableField::Name:
                return item.name;
            case TableField::Category:
                return item.category;
            case TableField::Value:
                break;
        }
        return std::to_string(item.value);
    };
}

ui::TableColumn<TableItem> table_column(const char* header, ui::TableViewColumnWidth width, TableField field)
{
    return {.header = header, .width = width, .text = table_text(field)};
}

// A short title and a note of any length, for the content-sized rows card.
struct TableNote
{
    std::string title;
    std::string text;
};

std::vector<TableNote> table_notes()
{
    return {{"Release", "Tag the build."},
            {"Docs",
             "Rewrite the getting started page so the first example compiles as written and every step says what "
             "the reader sees."},
            {"Tests", "Add a case for an empty table."},
            {"Review",
             "Go through the open pull requests, answer the questions in each one and merge the ones that are "
             "ready, oldest first."},
            {"Cleanup", "Delete the old branches."}};
}

// The filter box's test: a case-insensitive (ASCII) substring match.
bool contains_folded(const std::string& text, const std::string& needle)
{
    const auto fold = [](std::string s)
    {
        for (char& c : s)
        {
            c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }
        return s;
    };
    return fold(text).find(fold(needle)) != std::string::npos;
}

// "Sorted by Name, ascending." for the basic card's status line.
std::string sort_status(std::optional<std::size_t> sorted, ui::TableViewSortDirection direction)
{
    constexpr std::array<const char*, 4> headers{"ID", "Name", "Category", "Value"};
    const std::size_t                    column = sorted.value_or(headers.size());
    if (direction == ui::TableViewSortDirection::None || column >= headers.size())
    {
        return "Rows are in source order.";
    }
    return std::format("Sorted by {}, {}.",
                       headers[column],
                       direction == ui::TableViewSortDirection::Ascending ? "ascending" : "descending");
}

// The custom-cells sample derives a status and a rating from the ID.
std::string table_status(const TableItem& i)
{
    return i.id % 2 == 0 ? "Paused" : "Active";
}

int table_rating(const TableItem& i)
{
    return (i.id - 1) % 5 + 1;
}

ui::Color subtle_secondary(const ui::Theme& t) // the secondary subtle fill
{
    return has_fluent_palette(t) ? t.palette.subtle_fill_secondary : t.surfaces.menu;
}

// The Item cell: an avatar beside the name and the category.
ui::Spec table_item_cell(const TableItem& i)
{
    return ui::HorizontalStack{.spacing = 12.0f,
                               .margin  = ui::Symmetric{.horizontal = 12.0f, .vertical = 6.0f},
                               .content = {ui::PersonPicture{.diameter           = 32.0f,
                                                             .display_name       = i.name,
                                                             .vertical_alignment = ui::Alignment::Center,
                                                             .accessibility      = {.decorative = true}},
                                           ui::VerticalStack{.vertical_alignment = ui::Alignment::Center,
                                                             .content = {ui::Text{.text = i.name, .role = ui::TextRole::BodyStrong},
                                                                         ui::Text{.text       = i.category,
                                                                                  .role       = ui::TextRole::Caption,
                                                                                  .foreground = text_secondary}}}}};
}

// The Status cell: a caption on a subtle pill.
ui::Spec table_status_cell(const TableItem& i)
{
    return ui::Grid{.margin               = ui::Symmetric{.horizontal = 12.0f, .vertical = 0.0f},
                    .horizontal_alignment = ui::Alignment::Leading,
                    .vertical_alignment   = ui::Alignment::Center,
                    .content =
                        {ui::Cell{
                             .content = shape(subtle_secondary, false, 0.0f, 0.0f, ui::Alignment::Stretch, ui::Alignment::Stretch, 4.0f)},
                         ui::Cell{.content = ui::Text{.text   = table_status(i),
                                                      .role   = ui::TextRole::Caption,
                                                      .margin = ui::Symmetric{.horizontal = 8.0f, .vertical = 4.0f}}}}};
}

// The Value cell: a progress bar and the number.
ui::Spec table_value_cell(const TableItem& i)
{
    return ui::Grid{.columns        = {1_fr, 32_dip},
                    .column_spacing = 12.0f,
                    .margin         = ui::Symmetric{.horizontal = 12.0f, .vertical = 0.0f},
                    .content        = {ui::Cell{.content = ui::ProgressBar{.value              = static_cast<double>(i.value),
                                                                           .vertical_alignment = ui::Alignment::Center,
                                                                           .accessibility      = {.decorative = true}}},
                                       ui::Cell{.column  = std::size_t{1},
                                                .content = ui::Text{.text                 = std::to_string(i.value),
                                                                    .text_alignment       = ui::TextAlignment::Right,
                                                                    .horizontal_alignment = ui::Alignment::Trailing,
                                                                    .vertical_alignment   = ui::Alignment::Center}}}};
}

struct TableViewState
{
    // The table opens on its first row (`.selected_row = 0`), so the status starts there.
    ui::Property<std::string>             selection{std::string{"Selected: Item 1 (ID 1)."}};
    ui::Property<std::string>             sorting{std::string{"Rows are in source order."}};
    ui::Property<ui::SelectionMode>       selection_mode{ui::SelectionMode::Single};
    ui::Property<bool>                    resizable{true};
    ui::Control<ui::TableView<TableItem>> basic;

    std::vector<TableItem>                filter_source = table_items(12);
    ui::Property<std::vector<TableItem>>  filtered{table_items(12)};
    ui::Property<bool>                    grouped{true};
    ui::Control<ui::TableView<TableItem>> grouping;

    ui::Property<std::vector<TableItem>> editing_items{table_items(5)};
    ui::Property<bool>                   read_only{false};
    ui::Property<bool>                   invalid{false};
    ui::Property<std::string>            validation{};

    ui::Property<std::string> resized{std::string{"Drag the edge of a header to resize its column."}};

    ui::Property<std::vector<TableItem>>           presentation_items{table_items(1000)};
    ui::Property<ui::TableViewGridLinesVisibility> grid_lines{ui::TableViewGridLinesVisibility::All};
    ui::Property<bool>                             banding{true};
    ui::Control<ui::TableView<TableItem>>          presentation;
};

ui::Spec table_view_page(Context&)
{
    using ui::AutoSize;
    return with_state<TableViewState>(
        [](TableViewState& s)
        {
            return examples({
                example({
                    .title    = "A basic TableView",
                    .code     = R"cpp(
                        struct TableItem
                        {
                            int         id = 0;
                            std::string name;
                            std::string category;
                            int         value = 0;
                        };
                        ui::Property<ui::SelectionMode> mode{ui::SelectionMode::Single};
                        ui::Property<bool>                       resizable{true};
                        ui::Property<std::string>                sorting{std::string{"Rows are in source order."}};
                        ui::Control<ui::TableView<TableItem>>    table;

                        ui::TableView<TableItem>{
                            .items   = load_items(5),
                            .columns = {
                                // The ID column stays put while the others scroll sideways.
                                {.header     = "ID",
                                 .width      = 80.0f,
                                 .text       = [](const TableItem& i) { return std::to_string(i.id); },
                                 .frozen     = true,
                                 .can_resize = false,
                                 .can_sort   = false},
                                {.header     = "Name",
                                 .width      = 220.0f,
                                 .text       = [](const TableItem& i) { return i.name; },
                                 .sort_cycle = ui::TableViewSortCycle::AscendingDescendingNone},
                                {.header     = "Category",
                                 .width      = 180.0f,
                                 .text       = [](const TableItem& i) { return i.category; },
                                 .sort_cycle = ui::TableViewSortCycle::AscendingDescendingNone},
                                // Largest first; the third click restores the source order.
                                {.header     = "Value",
                                 .width      = 140.0f,
                                 .text       = [](const TableItem& i) { return std::to_string(i.value); },
                                 .sort_cycle = ui::TableViewSortCycle::DescendingAscendingNone,
                                 .compare    = [](const TableItem& a, const TableItem& b) { return a.value <=> b.value; }},
                            },
                            .selected_row            = 0,
                            .selection_mode          = mode,
                            .can_user_resize_columns = resizable,
                            .height                  = 280,
                            .max_width               = 480,
                            .horizontal_alignment    = ui::Alignment::Leading,
                            .on_sorted = [&](const ui::TableViewSortedArgs& args)
                            { sorting = sort_status(args.column, args.direction); },
                            .control = table,
                        }

                        // The First column toggle.
                        table->set_column_frozen(0, on);
                        // The Last column toggle.
                        table->set_column_frozen_trailing(3, on);
                    )cpp",
                    .stacked  = true,
                    .output   = ui::VerticalStack{.spacing = 4,
                                                  .content = {ui::Text{.text = s.sorting, .wrapping = ui::TextWrapping::Wrap},
                                                              ui::Text{.text = s.selection, .wrapping = ui::TextWrapping::Wrap}}},
                    .options  = options_panel(
                        200.0f,
                        {ui::ComboBox{.header               = "Selection mode",
                                      .items                = {"Single", "None", "Multiple", "Extended"},
                                      .selected_index       = 0,
                                      .on_selection_changed = [&s](std::optional<std::size_t> i)
                                      {
                                          constexpr std::array<ui::SelectionMode, 4> modes{
                                              ui::SelectionMode::Single,
                                              ui::SelectionMode::None,
                                              ui::SelectionMode::Multiple,
                                              ui::SelectionMode::Extended};
                                          if (i.value_or(0) < modes.size())
                                          {
                                              // On the control first: a mode that drops rows from the
                                              // selection then runs its handler, which a bound write doesn't.
                                              if (s.basic != nullptr)
                                              {
                                                  s.basic->set_selection_mode(modes[i.value_or(0)]);
                                              }
                                              s.selection_mode = modes[i.value_or(0)];
                                          }
                                      }},
                         ui::ToggleSwitch{.header      = "Column resizing",
                                          .checked     = s.resizable,
                                          .checked_text  = "Enabled",
                                          .unchecked_text     = "Disabled"},
                         ui::ToggleSwitch{.header             = "First column",
                                          .checked            = true,
                                          .checked_text       = "Frozen",
                                          .unchecked_text     = "Scrollable",
                                          .on_checked_changed = [&s](bool on)
                                          {
                                              if (s.basic != nullptr)
                                              {
                                                  s.basic->set_column_frozen(0, on);
                                              }
                                          }},
                         ui::ToggleSwitch{.header             = "Last column",
                                          .checked            = false,
                                          .checked_text       = "Frozen",
                                          .unchecked_text     = "Scrollable",
                                          .on_checked_changed = [&s](bool on)
                                          {
                                              if (s.basic != nullptr)
                                              {
                                                  s.basic->set_column_frozen_trailing(3, on);
                                              }
                                          }}},
                        12.0f),
                    .controls = {ui::Text{.text = "Bind a collection to items and define its columns. Select a header to sort or a row to "
                                                  "select it. Drag a header's edge to resize. Freeze the first column and scroll "
                                                  "horizontally to keep ID visible, or freeze the last one to keep Value in view.",
                                          .wrapping = ui::TextWrapping::Wrap},
                                 ui::TableView<TableItem>{
                                     .items   = table_items(5),
                                     .columns = {{.header     = "ID",
                                                  .width      = 80.0f,
                                                  .text       = table_text(TableField::Id),
                                                  .frozen     = true,
                                                  .can_resize = false,
                                                  .can_sort   = false},
                                                 {.header     = "Name",
                                                  .width      = 220.0f,
                                                  .text       = table_text(TableField::Name),
                                                  .sort_cycle = ui::TableViewSortCycle::AscendingDescendingNone},
                                                 {.header     = "Category",
                                                  .width      = 180.0f,
                                                  .text       = table_text(TableField::Category),
                                                  .sort_cycle = ui::TableViewSortCycle::AscendingDescendingNone},
                                                 {.header     = "Value",
                                                  .width      = 140.0f,
                                                  .text       = table_text(TableField::Value),
                                                  .sort_cycle = ui::TableViewSortCycle::DescendingAscendingNone,
                                                  .compare = [](const TableItem& a, const TableItem& b) { return a.value <=> b.value; }}},
                                     .selected_row            = 0,
                                     .selection_mode          = s.selection_mode,
                                     .can_user_resize_columns = s.resizable,
                                     .height                  = 280.0f,
                                     .max_width               = 480.0f,
                                     .horizontal_alignment    = ui::Alignment::Leading,
                                     .on_selection_changed    = [&s](std::optional<TableItem> i)
                                     {
                                         const std::size_t count = s.basic != nullptr ? s.basic->selected_rows().size() : 0;
                                         s.selection             = count > 1      ? std::format("{} rows selected.", count)
                                                                   : i ? std::format("Selected: {} (ID {}).", i->name, i->id)
                                                                                  : std::string{"No row selected."};
                                     },
                                     .on_sorted = [&s](const ui::TableViewSortedArgs& args)
                                     { s.sorting = sort_status(args.column, args.direction); },
                                     .control = s.basic},
                                 ui::Text{.text     = "ID cannot be sorted or resized. Value sorts largest first; the third click "
                                                      "restores source order.",
                                          .wrapping = ui::TextWrapping::Wrap}},
                }),
                example({
                    .title    = "Filtering and grouping rows",
                    .code     = R"cpp(
                        using ui::operator""_fr;
                        std::vector<TableItem>                all = load_items();
                        ui::Property<std::vector<TableItem>>  filtered{all};
                        ui::Property<bool>                    grouped{true};
                        ui::Control<ui::TableView<TableItem>> table;

                        ui::TextBox{
                            .header           = "Filter by name or category",
                            .placeholder_text = "Try Alpha or Item 2",
                            .width            = 300,
                            .on_text_changed  = [&](const std::string& text)
                            {
                                std::vector<TableItem> matching;
                                for (const TableItem& i : all)
                                {
                                    if (text.empty() || contains(i.name, text) || contains(i.category, text))
                                    {
                                        matching.push_back(i);
                                    }
                                }
                                filtered = std::move(matching); // the table reads its rows again
                            },
                        }
                        ui::TableView<TableItem>{
                            .items                   = filtered,
                            .columns                 = {
                                {.header = "Name", .width = 1_fr, .text = [](const TableItem& i) { return i.name; }},
                                {.header = "Category", .width = 140.0f, .text = [](const TableItem& i) { return i.category; }},
                                {.header = "Value", .width = 100.0f, .text = [](const TableItem& i) { return std::to_string(i.value); }},
                            },
                            .selection_mode          = ui::SelectionMode::None,
                            .can_user_sort_columns   = false,
                            .can_user_resize_columns = false,
                            // One group per category, in the order they first appear.
                            .group_by                = [](const TableItem& i) { return i.category; },
                            .grouping_enabled        = grouped,
                            .height                  = 320,
                            .max_width               = 480,
                            .control                 = table,
                            // Shown while nothing matches.
                            .empty_content = ui::Text{.text = "No items match the filter.", .margin = ui::Thickness{24}},
                        }

                        // The Group by dropdown, and the two buttons.
                        grouped = index == 1;
                        table->expand_all_groups();
                        table->collapse_all_groups();
                    )cpp",
                    .stacked  = true,
                    .options  = options_panel(
                        200.0f,
                        {ui::ComboBox{.header               = "Group by",
                                      .items                = {"None", "Category"},
                                      .selected_index       = 1,
                                      .on_selection_changed = [&s](std::optional<std::size_t> i) { s.grouped = i == 1; }},
                         ui::Button{.text     = "Expand all groups",
                                    .on_click = [&s]
                                    {
                                        if (s.grouping != nullptr)
                                        {
                                            s.grouping->expand_all_groups();
                                        }
                                    }},
                         ui::Button{.text     = "Collapse all groups",
                                    .on_click = [&s]
                                    {
                                        if (s.grouping != nullptr)
                                        {
                                            s.grouping->collapse_all_groups();
                                        }
                                    }}},
                        12.0f),
                    .controls = {ui::Text{.text = "Bind the table to a property, assign it the items that match, and group its rows. "
                                                  "Filter by name or category, then change the grouping without "
                                                  "clearing the filter.",
                                          .wrapping = ui::TextWrapping::Wrap},
                                 ui::TextBox{.header               = "Filter by name or category",
                                             .placeholder_text     = "Try Alpha or Item 2",
                                             .width                = 300.0f,
                                             .horizontal_alignment = ui::Alignment::Leading,
                                             .on_text_changed      = [&s](const std::string& raw)
                                             {
                                                 const auto        first = raw.find_first_not_of(' ');
                                                 const std::string text  = first == std::string::npos
                                                                               ? std::string{}
                                                                               : raw.substr(first, raw.find_last_not_of(' ') - first + 1);
                                                 std::vector<TableItem> matching;
                                                 for (const TableItem& i : s.filter_source)
                                                 {
                                                     if (text.empty() || contains_folded(i.name, text) || contains_folded(i.category, text))
                                                     {
                                                         matching.push_back(i);
                                                     }
                                                 }
                                                 s.filtered = std::move(matching);
                                             }},
                                 ui::TableView<TableItem>{.items                   = s.filtered,
                                                          .columns                 = {table_column("Name", 1_fr, TableField::Name),
                                                                                      table_column("Category", 140.0f, TableField::Category),
                                                                                      table_column("Value", 100.0f, TableField::Value)},
                                                          .selection_mode          = ui::SelectionMode::None,
                                                          .can_user_sort_columns   = false,
                                                          .can_user_resize_columns = false,
                                                          .group_by                = [](const TableItem& i) { return i.category; },
                                                          .grouping_enabled        = s.grouped,
                                                          .height                  = 320.0f,
                                                          .max_width               = 480.0f,
                                                          .horizontal_alignment    = ui::Alignment::Leading,
                                                          .accessibility = {.name = "Filtered and grouped items"},
                                                          .control       = s.grouping,
                                                          .empty_content = ui::Text{.text = "No items match the filter.", .margin = ui::Thickness{24.0f}}}},
                }),
                example({
                    .title    = "Customizing cells with templates",
                    .code     = R"cpp(
                        ui::TableView<TableItem>{
                            .items   = load_items(5),
                            .columns = {
                                {.header = "ID", .width = 64.0f, .text = [](const TableItem& i) { return std::to_string(i.id); }},
                                // A template column still sorts by its text.
                                {.header = "Item",
                                 .width  = 200.0f,
                                 .text   = [](const TableItem& i) { return i.name; },
                                 .cell   = [](TableItem& i) -> ui::Spec
                                 {
                                     return ui::HorizontalStack{
                                         .spacing = 12,
                                         .margin  = ui::Symmetric{.horizontal = 12, .vertical = 6},
                                         .content = {
                                             ui::PersonPicture{.diameter = 32, .display_name = i.name,
                                                               .vertical_alignment = ui::Alignment::Center},
                                             ui::VerticalStack{.vertical_alignment = ui::Alignment::Center, .content = {
                                                 ui::Text{.text = i.name, .role = ui::TextRole::BodyStrong},
                                                 // In the secondary text color.
                                                 ui::Text{.text = i.category, .role = ui::TextRole::Caption},
                                             }},
                                         },
                                     };
                                 }},
                                // status(): "Active" or "Paused"; status_badge(): that text on a rounded,
                                // subtle fill.
                                {.header = "Status", .width = 120.0f, .text = status, .cell = status_badge},
                                {.header  = "Value",
                                 .width   = 220.0f,
                                 .text    = [](const TableItem& i) { return std::to_string(i.value); },
                                 .compare = [](const TableItem& a, const TableItem& b) { return a.value <=> b.value; },
                                 .cell    = [](TableItem& i) -> ui::Spec
                                 {
                                     return ui::Grid{
                                         .columns        = {1_fr, 32_dip},
                                         .column_spacing = 12,
                                         .margin         = ui::Symmetric{.horizontal = 12, .vertical = 0},
                                         .content          = {
                                             ui::Cell{.content = ui::ProgressBar{.value = double(i.value),
                                                                                 .vertical_alignment = ui::Alignment::Center}},
                                             ui::Cell{.column = 1, .content = ui::Text{.text = std::to_string(i.value),
                                                                                       .vertical_alignment = ui::Alignment::Center}},
                                         },
                                     };
                                 }},
                                // rating(): 1 to 5, from the ID.
                                {.header  = "Rating",
                                 .width   = 180.0f,
                                 .text    = [](const TableItem& i) { return std::to_string(rating(i)); },
                                 .compare = [](const TableItem& a, const TableItem& b) { return rating(a) <=> rating(b); },
                                 .cell    = [](TableItem& i) -> ui::Spec
                                 {
                                     return ui::Rating{.value = double(rating(i)), .read_only = true,
                                                       .margin = ui::Symmetric{.horizontal = 12, .vertical = 0},
                                                       .vertical_alignment = ui::Alignment::Center};
                                 }},
                            },
                            .row_height              = 48, // every row is one height; 48 fits the avatar
                            .selection_mode          = ui::SelectionMode::None,
                            .can_user_resize_columns = false,
                            .grid_lines_visibility   = ui::TableViewGridLinesVisibility::None,
                            .height                  = 340,
                            .max_width               = 900,
                        }
                    )cpp",
                    .stacked  = true,
                    .controls = {ui::Text{.text = "Combine a text column with cells built from each item: an avatar and two lines of "
                                                  "text, a status badge, a progress bar, and a read-only rating. A template "
                                                  "column's text still sorts it, and compare sorts by the underlying values.",
                                          .wrapping = ui::TextWrapping::Wrap},
                                 ui::TableView<TableItem>{
                                     .items                   = table_items(5),
                                     .columns                 = {table_column("ID", 64.0f, TableField::Id),
                                                                 {.header = "Item",
                                                                  .width  = 200.0f,
                                                                  .text   = table_text(TableField::Name),
                                                                  .cell   = [](TableItem& i) { return table_item_cell(i); }},
                                                                 {.header = "Status",
                                                                  .width  = 120.0f,
                                                                  .text   = [](const TableItem& i) { return table_status(i); },
                                                                  .cell   = [](TableItem& i) { return table_status_cell(i); }},
                                                                 {.header  = "Value",
                                                                  .width   = 220.0f,
                                                                  .text    = table_text(TableField::Value),
                                                                  .compare = [](const TableItem& a, const TableItem& b)
                                                                  { return a.value <=> b.value; },
                                                                  .cell = [](TableItem& i) { return table_value_cell(i); }},
                                                                 {.header  = "Rating",
                                                                  .width   = 180.0f,
                                                                  .text    = [](const TableItem& i) { return std::to_string(table_rating(i)); },
                                                                  .compare = [](const TableItem& a, const TableItem& b)
                                                                  { return table_rating(a) <=> table_rating(b); },
                                                                  .cell    = [](TableItem& i) -> ui::Spec
                                                                  {
                                                                      return ui::Rating{.value     = static_cast<double>(table_rating(i)),
                                                                                        .read_only = true,
                                                                                        .margin = ui::Symmetric{.horizontal = 12.0f, .vertical = 0.0f},
                                                                                        .vertical_alignment = ui::Alignment::Center,
                                                                                        .accessibility = {.name = "Item rating"}};
                                                                  }}},
                                     .row_height              = 48.0f,
                                     .selection_mode          = ui::SelectionMode::None,
                                     .can_user_resize_columns = false,
                                     .grid_lines_visibility   = ui::TableViewGridLinesVisibility::None,
                                     .height                  = 340.0f,
                                     .max_width               = 900.0f,
                                     .horizontal_alignment    = ui::Alignment::Leading,
                                     .accessibility           = {.name = "Table with custom cell templates"}}},
                }),
                example({
                    .title    = "Editing cells and validating changes",
                    .code     = R"cpp(
                        ui::Property<std::vector<TableItem>> items{load_items(5)};
                        ui::Property<bool>                   read_only{false};
                        ui::Property<bool>                   invalid{false};
                        ui::Property<std::string>            validation{};

                        ui::InfoBar{
                            .title       = "Invalid value",
                            .message     = validation,
                            .is_open     = invalid,
                            .severity    = ui::InfoBarSeverity::Error,
                            .closable    = false,
                        }
                        ui::TableView<TableItem>{
                            .items   = items,
                            .columns = {
                                {.header    = "ID",
                                 .width     = 80.0f,
                                 .text      = [](const TableItem& i) { return std::to_string(i.id); },
                                 .read_only = true},
                                {.header = "Name", .width = 180.0f, .text = [](const TableItem& i) { return i.name; }},
                                {.header = "Category", .width = 180.0f, .text = [](const TableItem& i) { return i.category; }},
                            },
                            .can_user_sort_columns   = false,
                            .can_user_resize_columns = false,
                            .read_only               = read_only,
                            .height                  = 260,
                            .on_beginning_edit       = [&] { invalid = false; },
                            // Write the value into the item, or reject it and keep the editor open.
                            .on_cell_edited = [&](const ui::TableViewCellEditedArgs<TableItem>& args)
                            {
                                if (args.column == 1 && args.text.find_first_not_of(' ') == std::string::npos)
                                {
                                    validation = "Name is required. Enter a name or press Esc to cancel the edit.";
                                    invalid    = true;
                                    return ui::Cancel;
                                }
                                invalid = false;
                                (args.column == 1 ? args.item.name : args.item.category) = args.text;
                                return ui::Proceed;
                            },
                        }
                    )cpp",
                    .stacked  = true,
                    .options  = options_panel(200.0f,
                                             {ui::ToggleSwitch{.header      = "Read-only",
                                                               .checked     = s.read_only,
                                                               .checked_text  = "Read-only",
                                                               .unchecked_text = "Editable"}},
                                             12.0f),
                    .controls = {ui::Text{.text = "Double-click a Name or Category cell, or press F2, to edit. Press Enter to save or "
                                                  "Esc to cancel. ID is always read-only. Clear a name to see the edit rejected, "
                                                  "or make the whole table read-only.",
                                          .wrapping = ui::TextWrapping::Wrap},
                                 ui::InfoBar{.title         = "Invalid value",
                                             .message       = s.validation,
                                             .is_open       = s.invalid,
                                             .severity      = ui::InfoBarSeverity::Error,
                                             .closable      = false,
                                             .accessibility = {.name = "Cell validation feedback"}},
                                 ui::TableView<TableItem>{
                                     .items                   = s.editing_items,
                                     .columns                 = {{.header    = "ID",
                                                                  .width     = 80.0f,
                                                                  .text      = table_text(TableField::Id),
                                                                  .read_only = true},
                                                                 table_column("Name", 180.0f, TableField::Name),
                                                                 table_column("Category", 180.0f, TableField::Category)},
                                     .can_user_sort_columns   = false,
                                     .can_user_resize_columns = false,
                                     .read_only               = s.read_only,
                                     .height                  = 260.0f,
                                     .horizontal_alignment    = ui::Alignment::Leading,
                                     .accessibility           = {.name = "Editable cells"},
                                     .on_beginning_edit       = [&s]
                                     {
                                         s.invalid = false;
                                     },
                                     .on_cell_edited = [&s](const ui::TableViewCellEditedArgs<TableItem>& args)
                                     {
                                         if (args.column == 1 && args.text.find_first_not_of(' ') == std::string::npos)
                                         {
                                             s.validation = std::string{"Name is required. Enter a name or press Esc to cancel the edit."};
                                             s.invalid    = true;
                                             return ui::Cancel;
                                         }
                                         s.invalid                                 = false;
                                         (args.column == 1 ? args.item.name : args.item.category) = args.text;
                                         return ui::Proceed;
                                     }}},
                }),
                example({
                    .title    = "An advanced TableView",
                    .code     = R"cpp(
                        ui::Property<std::vector<TableItem>>           items{load_items(1000)};
                        ui::Property<ui::TableViewGridLinesVisibility> grid_lines{ui::TableViewGridLinesVisibility::All};
                        ui::Property<bool>                             banding{true};
                        ui::Control<ui::TableView<TableItem>>          table;

                        ui::TableView<TableItem>{
                            .items                   = items,
                            .columns                 = {
                                {.header = "ID", .width = 80.0f, .text = [](const TableItem& i) { return std::to_string(i.id); }},
                                {.header = "Name", .width = 180.0f, .text = [](const TableItem& i) { return i.name; }},
                                {.header = "Category", .width = 140.0f, .text = [](const TableItem& i) { return i.category; }},
                                {.header = "Value", .width = 100.0f, .text = [](const TableItem& i) { return std::to_string(i.value); }},
                            },
                            .row_height              = 40,
                            .selection_mode          = ui::SelectionMode::None,
                            .can_user_sort_columns   = false,
                            .can_user_resize_columns = false,
                            .grid_lines_visibility   = grid_lines,
                            .alternating_rows        = banding,
                            .height                  = 320,
                            .control                 = table,
                            .empty_content           = ui::VerticalStack{
                                .spacing         = 8,
                                .child_alignment = ui::Alignment::Center,
                                .margin          = ui::Thickness{32},
                                .content         = {
                                    ui::Icon{.source = ui::IconGlyph{0xE9D9, "Segoe Fluent Icons", 32.0f}},
                                    ui::Text{.text = "No items", .role = ui::TextRole::Subtitle},
                                    ui::Text{.text = "Turn off the empty-state option to restore 1,000 items.",
                                             .wrapping = ui::TextWrapping::Wrap, .max_width = 320},
                                },
                            },
                        }

                        // The Density dropdown: Compact, Standard or Comfortable rows.
                        table->set_row_height(30.0f); // 40, 48
                        // The Data toggle: no items shows the empty state.
                        items = std::vector<TableItem>{};
                    )cpp",
                    .stacked  = true,
                    .options  = options_panel(
                        240.0f,
                        {ui::ComboBox{.header               = "Density",
                                      .items                = {"Compact", "Standard", "Comfortable"},
                                      .selected_index       = 1,
                                      .on_selection_changed = [&s](std::optional<std::size_t> i)
                                      {
                                          // The compact, standard and comfortable row heights.
                                          constexpr std::array<float, 3> heights{30.0f, 40.0f, 48.0f};
                                          if (s.presentation != nullptr && i.value_or(0) < heights.size())
                                          {
                                              s.presentation->set_row_height(heights[i.value_or(0)]);
                                          }
                                      }},
                         ui::ComboBox{.header               = "Grid lines",
                                      .items                = {"All", "Horizontal", "Vertical", "None"},
                                      .selected_index       = 0,
                                      .on_selection_changed = [&s](std::optional<std::size_t> i)
                                      {
                                          constexpr std::array<ui::TableViewGridLinesVisibility, 4> lines{
                                              ui::TableViewGridLinesVisibility::All,
                                              ui::TableViewGridLinesVisibility::Horizontal,
                                              ui::TableViewGridLinesVisibility::Vertical,
                                              ui::TableViewGridLinesVisibility::None};
                                          if (i.value_or(0) < lines.size())
                                          {
                                              s.grid_lines = lines[i.value_or(0)];
                                          }
                                      }},
                         ui::ToggleSwitch{.header      = "Row banding",
                                          .checked     = s.banding,
                                          .checked_text  = "Alternating rows",
                                          .unchecked_text     = "Off"},
                         ui::ToggleSwitch{.header             = "Data",
                                          .checked            = false,
                                          .checked_text       = "Empty state",
                                          .unchecked_text     = "1,000 items",
                                          .on_checked_changed = [&s](bool empty)
                                          { s.presentation_items = empty ? std::vector<TableItem>{} : table_items(1000); }}},
                        12.0f),
                    .controls = {ui::Text{.text     = "Change density, grid lines, and row banding, or clear the collection to show the "
                                                      "empty content. This table has 1,000 items; only rows near the viewport are realized.",
                                          .wrapping = ui::TextWrapping::Wrap},
                                 ui::TableView<TableItem>{
                                     .items                   = s.presentation_items,
                                     .columns                 = {table_column("ID", 80.0f, TableField::Id),
                                                                 table_column("Name", 180.0f, TableField::Name),
                                                                 table_column("Category", 140.0f, TableField::Category),
                                                                 table_column("Value", 100.0f, TableField::Value)},
                                     .row_height              = 40.0f,
                                     .selection_mode          = ui::SelectionMode::None,
                                     .can_user_sort_columns   = false,
                                     .can_user_resize_columns = false,
                                     .grid_lines_visibility   = s.grid_lines,
                                     .alternating_rows        = s.banding,
                                     .height                  = 320.0f,
                                     .horizontal_alignment    = ui::Alignment::Leading,
                                     .control                 = s.presentation,
                                     .empty_content           = ui::VerticalStack{
                                         .spacing         = 8.0f,
                                         .child_alignment = ui::Alignment::Center,
                                         .margin          = ui::Thickness{32.0f},
                                         .content         = {ui::Icon{.source = ui::IconGlyph{0xE9D9, "Segoe Fluent Icons", 32.0f}},
                                                             ui::Text{.text = "No items", .role = ui::TextRole::Subtitle},
                                                             ui::Text{.text      = "Turn off the empty-state option to restore 1,000 items.",
                                                                      .wrapping  = ui::TextWrapping::Wrap,
                                                                      .max_width = 320.0f}},
                                     }}},
                }),
                example({
                    .title    = "Rows that fit their content",
                    .code     = R"cpp(
                        struct Note
                        {
                            std::string title;
                            std::string text;
                        };

                        ui::TableView<Note>{
                            .items                = load_notes(),
                            .columns              = {{.header = "Title", .width = 120.0f, .text = [](const Note& n) { return n.title; }},
                                                     {.header = "Note", .width = 1_fr, .text = [](const Note& n) { return n.text; }}},
                            .size_rows_to_content = true,
                            .height               = 320,
                        }
                    )cpp",
                    .stacked  = true,
                    .controls = {ui::Text{.text     = "Each row grows to fit its tallest cell, and long notes wrap instead of being "
                                                      "trimmed. Make the window narrower and the rows grow taller.",
                                          .wrapping = ui::TextWrapping::Wrap},
                                 ui::TableView<TableNote>{
                                     .items   = table_notes(),
                                     .columns = {{.header = "Title", .width = 120.0f, .text = [](const TableNote& n) { return n.title; }},
                                                 {.header = "Note", .width = 1_fr, .text = [](const TableNote& n) { return n.text; }}},
                                     .size_rows_to_content = true,
                                     .selection_mode       = ui::SelectionMode::None,
                                     .height               = 320.0f,
                                     .max_width            = 480.0f,
                                     .horizontal_alignment = ui::Alignment::Leading}},
                }),
                example({
                    .title    = "Star and Auto column widths",
                    .code     = R"cpp(
                        using ui::AutoSize;
                        using ui::operator""_fr;
                        ui::Property<std::string> resized{};

                        ui::TableView<TableItem>{
                            .items   = load_items(12),
                            .columns = {
                                // As wide as its header and the cells in view.
                                {.header = "ID", .width = AutoSize, .text = [](const TableItem& i) { return std::to_string(i.id); }},
                                // The rest of the width, shared 2:1, never narrower than 120 and 80.
                                {.header = "Name", .width = 2_fr, .text = [](const TableItem& i) { return i.name; }, .min_width = 120},
                                {.header = "Category", .width = 1_fr, .text = [](const TableItem& i) { return i.category; }, .min_width = 80},
                                {.header = "Value", .width = 100.0f, .text = [](const TableItem& i) { return std::to_string(i.value); },
                                 .min_width = 60, .max_width = 200},
                            },
                            .height            = 320,
                            .on_column_resized = [&](const ui::TableViewColumnResizedArgs& args)
                            { resized = std::format("Column {} is {:.0f} DIPs wide.", args.column + 1, args.width); },
                        }
                    )cpp",
                    .stacked  = true,
                    .stretch  = true,
                    .output   = ui::Text{.text = s.resized, .wrapping = ui::TextWrapping::Wrap},
                    .controls = {ui::Text{.text = "Fixed, Auto and star widths: ID fits its content, Name and Category share what is "
                                                  "left 2:1, and Value is fixed. Resize the window, or drag a header's edge; a "
                                                  "dragged column keeps its width and the star columns share the rest.",
                                          .wrapping = ui::TextWrapping::Wrap},
                                 ui::TableView<TableItem>{
                                     .items             = table_items(12),
                                     .columns           = {table_column("ID", AutoSize, TableField::Id),
                                                           {.header    = "Name",
                                                            .width     = 2_fr,
                                                            .text      = table_text(TableField::Name),
                                                            .min_width = 120.0f},
                                                           {.header    = "Category",
                                                            .width     = 1_fr,
                                                            .text      = table_text(TableField::Category),
                                                            .min_width = 80.0f},
                                                           {.header    = "Value",
                                                            .width     = 100.0f,
                                                            .text      = table_text(TableField::Value),
                                                            .min_width = 60.0f,
                                                            .max_width = 200.0f}},
                                     .height            = 320.0f,
                                     .on_column_resized = [&s](const ui::TableViewColumnResizedArgs& args)
                                     { s.resized = std::format("Column {} is {:.0f} DIPs wide.", args.column + 1, args.width); }}},
                }),
            });
        });
}

// ---- ForEach ----------------------------------------------------------------------------------

struct Song
{
    int         id = 0;
    std::string title;
    friend bool operator==(const Song&, const Song&) = default;
};

struct ForEachState
{
    // Edited in place: every edit reaches the rows as what it is.
    ui::ObservableVector<std::string> names{"Ada", "Grace", "Alan"};
    int                               next = 1;

    // Assigned as a whole: the rows find their items again by key.
    ui::Property<std::vector<Song>> songs{
        std::vector<Song>{{1, "Blue in Green"},
                          {2, "So What"},
                          {3, "Freddie Freeloader"},
                          {4, "All Blues"},
                          {5, "Flamenco Sketches"}}};
    std::uint32_t seed = 7;
};

ui::Spec for_each_page(Context&)
{
    return with_state<ForEachState>(
        [](ForEachState& s)
        {
            return examples({
                example({
                    .title    = "A collection edited in place: one row is built or destroyed per edit",
                    .code     = R"cpp(
                        ui::ObservableVector<std::string> names{"Ada", "Grace", "Alan"};

                        ui::ForEach<std::string>{
                            .items = names,
                            .item_transitions = {ui::AddRemoveTransition{}},
                            // The reference stays valid for as long as the name is in the collection.
                            .item = [&](std::string& name) -> ui::Spec
                            {
                                return ui::HorizontalStack{
                                    .spacing         = 8,
                                    .child_alignment = ui::Alignment::Center,
                                    .content         = {
                                        ui::Text{.text = name, .width = 90},
                                        ui::TextBox{.placeholder_text = "Type, then edit the list", .width = 220},
                                        ui::Button{.text = "Remove", .on_click = [&names, &name] { names.remove(name); }},
                                    },
                                };
                            },
                        }

                        // Every edit reaches the rows as what it is. The other rows keep what was typed.
                        names.push_back("Item 1");
                        names.insert(0, "Item 2");
                        names.move(0, names.size() - 1);
                        names.clear();
                    )cpp",
                    .options  = options_panel(200.0f,
                                              {
                                                  ui::Button{.text     = "Add",
                                                             .on_click = [&s]
                                                             { s.names.push_back(std::format("Item {}", s.next++)); }},
                                                  ui::Button{.text     = "Insert first",
                                                             .on_click = [&s]
                                                             { s.names.insert(0, std::format("Item {}", s.next++)); }},
                                                  ui::Button{.text = "Move first to last",
                                                             .on_click =
                                                                 [&s]
                                                             {
                                                                if (s.names.size() > 1)
                                                                {
                                                                    s.names.move(0, s.names.size() - 1);
                                                                }
                                                             }},
                                                  ui::Button{.text = "Clear", .on_click = [&s] { s.names.clear(); }},
                                              }),
                    .controls = {ui::ForEach<std::string>{
                        .items            = s.names,
                        .item_transitions = {ui::AddRemoveTransition{}},
                        .item             = [&s](std::string& name) -> ui::Spec
                        {
                            return ui::HorizontalStack{
                                .spacing         = 8.0f,
                                .child_alignment = ui::Alignment::Center,
                                .margin          = ui::Symmetric{.horizontal = 0.0f, .vertical = 3.0f},
                                .content =
                                    {
                                        ui::Text{.text = name, .width = 90.0f},
                                        ui::TextBox{.placeholder_text = "Type, then edit the list", .width = 220.0f},
                                        ui::Button{.text = "Remove", .on_click = [&s, &name] { s.names.remove(name); }},
                                    },
                            };
                        },
                    }},
                }),
                example({
                    .title    = "A vector assigned as a whole: with a key, a row follows its item",
                    .code     = R"cpp(
                        struct Song
                        {
                            int         id = 0;
                            std::string title;
                            friend bool operator==(const Song&, const Song&) = default;
                        };
                        ui::Property<std::vector<Song>> songs{
                            std::vector<Song>{{1, "Blue in Green"}, {2, "So What"}, {3, "Freddie Freeloader"}}};

                        ui::ForEach<Song>{
                            .items = songs,
                            // The key finds a song again after a reorder, so its switch stays with it.
                            .key = &Song::id,
                            .item  = [](const Song& song) -> ui::Spec
                            {
                                return ui::HorizontalStack{
                                    .spacing         = 8,
                                    .child_alignment = ui::Alignment::Center,
                                    .content         = {ui::Text{.text = song.title, .width = 160}, ui::ToggleSwitch{}},
                                };
                            },
                        }

                        // Reverse and Sort by title assign the whole vector.
                        songs.update([](std::vector<Song>& v) { std::reverse(v.begin(), v.end()); });
                        songs.update([](std::vector<Song>& v) { std::ranges::sort(v, {}, &Song::title); });
                    )cpp",
                    .options  = options_panel(200.0f,
                                              {
                                                  ui::Text{.text     = "Flip a few switches first: they stay with their "
                                                                       "rows.",
                                                           .wrapping = ui::TextWrapping::Wrap},
                                                  ui::Button{.text = "Reverse",
                                                             .on_click =
                                                                 [&s]
                                                             {
                                                                s.songs.update([](std::vector<Song>& v)
                                                                               { std::reverse(v.begin(), v.end()); });
                                                             }},
                                                  ui::Button{.text = "Sort by title",
                                                             .on_click =
                                                                 [&s]
                                                             {
                                                                s.songs.update(
                                                                    [](std::vector<Song>& v)
                                                                    { std::ranges::sort(v, {}, &Song::title); });
                                                             }},
                                                  ui::Button{.text = "Shuffle",
                                                             .on_click =
                                                                 [&s]
                                                             {
                                                                s.songs.update(
                                                                    [&s](std::vector<Song>& v)
                                                                    {
                                                                        for (std::size_t i = v.size(); i > 1; --i)
                                                                        {
                                                                            s.seed = s.seed * 1664525u + 1013904223u;
                                                                            std::swap(v[i - 1], v[(s.seed >> 8) % i]);
                                                                        }
                                                                    });
                                                             }},
                                              }),
                    .controls = {ui::ForEach<Song>{
                        .items = s.songs,
                        .key   = &Song::id,
                        .item  = [](const Song& song) -> ui::Spec
                        {
                            return ui::HorizontalStack{
                                .spacing         = 8.0f,
                                .child_alignment = ui::Alignment::Center,
                                .content         = {ui::Text{.text = song.title, .width = 160.0f}, ui::ToggleSwitch{}},
                            };
                        },
                    }},
                }),
            });
        });
}

} // namespace

std::span<const Page> collections_pages()
{
    // The Collections group, libwinui's own controls last.
    static const auto Pages = std::to_array<Page>({
        {"TileView", "A control that presents a collection of items in rows and columns.", tile_view_page},
        {"Item Layouts",
         "How ForEach arranges its items: stack, tiles, wrap and lined flow, and custom layouts.",
         items_repeater_page},
        {"ListView", "A control that presents a collection of items in a vertical list.", list_view_page},
        {"TableView", "A control for presenting virtualized tabular data.", table_view_page},
        {"TreeView",
         "The TreeView control is a hierarchical list pattern with expanding and collapsing nodes that contain nested "
         "items.",
         tree_view_page},
        {"ForEach", "Turns a collection into children and keeps each row with its item.", for_each_page},
    });
    return Pages;
}

} // namespace gallery
