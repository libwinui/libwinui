// SPDX-License-Identifier: MIT
#include "winui/ui/input/ColorPicker.hpp"

#include "../LocalText.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Strings.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/ComboBox.hpp"
#include "winui/ui/input/Slider.hpp"
#include "winui/ui/media/Icon.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ui
{

namespace
{
constexpr float DefaultWidth = 312.0f; // the spectrum's 256 px minimum width plus the preview column

// The vertical layout of the picker's parts.
constexpr float SliderGap        = 8.0f;   // between the hue and saturation sliders
constexpr float PreviewWidth     = 44.0f;  // the preview column's width
constexpr float PreviewGap       = 12.0f;  // between the spectrum and the preview column
constexpr float PreviewAlone     = 44.0f;  // the preview's height when the spectrum is hidden
constexpr float SpectrumBottom   = 16.0f;  // below the spectrum
constexpr float ValueBottom      = 6.0f;   // below the value slider
constexpr float AlphaBottom      = 16.0f;  // below the alpha slider
constexpr float EntryRowTop      = 12.0f;  // above each channel row and the alpha row
constexpr float BoxWidth         = 120.0f; // the RGB/HSV dropdown and the channel boxes
constexpr float HexWidth         = 132.0f; // the hex box
constexpr float LabelGap         = 8.0f;   // the 8px column before each label
constexpr float MoreButtonWidth  = 120.0f; // the More button's minimum width
constexpr float MoreButtonHeight = 32.0f;  // the More button's minimum height

// The channel labels, RGB then HSV.
std::array<const std::string*, 3> channel_labels(bool hsv)
{
    const Strings::ColorPickerText& t = strings().color_picker;
    return hsv ? std::array{&t.hue, &t.saturation, &t.value} : std::array{&t.red, &t.green, &t.blue};
}

// A minimal HSVA representation, private to this file.
struct ColorPickerHsv
{
    float h = 0.0f; // [0, 360]
    float s = 0.0f; // [0, 1]
    float v = 0.0f; // [0, 1]
    float a = 1.0f; // [0, 1]
};

ColorPickerHsv to_picker_hsv(const Color& c)
{
    const float maxc  = std::max({c.r, c.g, c.b});
    const float minc  = std::min({c.r, c.g, c.b});
    const float delta = maxc - minc;

    float h = 0.0f;
    if (delta > 0.0f)
    {
        if (maxc == c.r)
        {
            h = (c.g - c.b) / delta + (c.g < c.b ? 6.0f : 0.0f);
        }
        else if (maxc == c.g)
        {
            h = (c.b - c.r) / delta + 2.0f;
        }
        else
        {
            h = (c.r - c.g) / delta + 4.0f;
        }
        h *= 60.0f;
    }
    const float s = maxc > 0.0f ? delta / maxc : 0.0f;
    return {h, s, maxc, c.a};
}

// h outside [0,360] is not wrapped: every caller keeps it in range.
Color from_picker_hsv(const ColorPickerHsv& hsv)
{
    const float h = std::clamp(hsv.h, 0.0f, 360.0f);
    const float s = std::clamp(hsv.s, 0.0f, 1.0f);
    const float v = std::clamp(hsv.v, 0.0f, 1.0f);

    const float c  = v * s;
    const float hp = h / 60.0f;
    const float x  = c * (1.0f - std::abs(std::fmod(hp, 2.0f) - 1.0f));
    const float m  = v - c;

    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (hp < 1.0f)
    {
        r = c;
        g = x;
    }
    else if (hp < 2.0f)
    {
        r = x;
        g = c;
    }
    else if (hp < 3.0f)
    {
        g = c;
        b = x;
    }
    else if (hp < 4.0f)
    {
        g = x;
        b = c;
    }
    else if (hp < 5.0f)
    {
        r = x;
        b = c;
    }
    else
    {
        r = c;
        b = x;
    }

    return {r + m, g + m, b + m, std::clamp(hsv.a, 0.0f, 1.0f)};
}

int to_byte(float v)
{
    return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

std::string format_hex(const Color& c, bool with_alpha)
{
    if (with_alpha)
    {
        return std::format("#{:02X}{:02X}{:02X}{:02X}", to_byte(c.r), to_byte(c.g), to_byte(c.b), to_byte(c.a));
    }
    return std::format("#{:02X}{:02X}{:02X}", to_byte(c.r), to_byte(c.g), to_byte(c.b));
}

bool hex_digit(char ch, int& out)
{
    if (ch >= '0' && ch <= '9')
    {
        out = ch - '0';
        return true;
    }
    if (ch >= 'A' && ch <= 'F')
    {
        out = ch - 'A' + 10;
        return true;
    }
    if (ch >= 'a' && ch <= 'f')
    {
        out = ch - 'a' + 10;
        return true;
    }
    return false;
}

// Accepts RRGGBB or RRGGBBAA ('#' optional, any case). The alpha pair only counts when
// `alpha_enabled`; otherwise `fallback_alpha` is kept, so a bare RGB hex keeps the transparency.
bool try_parse_hex(std::string text, bool alpha_enabled, float fallback_alpha, Color& out)
{
    if (!text.empty() && text.front() == '#')
    {
        text.erase(0, 1);
    }
    if (text.size() != 6 && text.size() != 8)
    {
        return false;
    }

    int digits[8]{};
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (!hex_digit(text[i], digits[i]))
        {
            return false;
        }
    }
    const auto byte_at = [&](std::size_t i) { return static_cast<float>(digits[i] * 16 + digits[i + 1]) / 255.0f; };
    out = Color{byte_at(0), byte_at(2), byte_at(4), text.size() == 8 && alpha_enabled ? byte_at(6) : fallback_alpha};
    return true;
}

// Parses a channel box and checks its range: digits only,
// surrounding spaces tolerated, and a trailing '%' too when `percent` (the alpha box shows one).
std::optional<int> parse_component(std::string text, int max, bool percent)
{
    const auto first = text.find_first_not_of(' ');
    const auto last  = text.find_last_not_of(' ');
    if (first == std::string::npos)
    {
        return std::nullopt;
    }
    text = text.substr(first, last - first + 1);
    if (percent)
    {
        // What the percent pattern writes around the number, then a plain '%' typed by hand.
        const std::string& pattern = strings().color_picker.percent;
        if (const std::size_t slot = pattern.find("{0}"); slot != std::string::npos)
        {
            const std::string_view before = std::string_view{pattern}.substr(0, slot);
            const std::string_view after  = std::string_view{pattern}.substr(slot + 3);
            if (!before.empty() && text.starts_with(before))
            {
                text.erase(0, before.size());
            }
            if (!after.empty() && text.ends_with(after))
            {
                text.erase(text.size() - after.size());
            }
        }
        if (!text.empty() && text.back() == '%')
        {
            text.pop_back();
        }
        while (!text.empty() && text.back() == ' ')
        {
            text.pop_back();
        }
    }
    if (text.empty() || text.size() > 4 ||
        !std::all_of(text.begin(), text.end(), [](char ch) { return ch >= '0' && ch <= '9'; }))
    {
        return std::nullopt;
    }
    const int value = std::stoi(text);
    return value <= max ? std::optional<int>{value} : std::nullopt;
}
} // namespace

ColorPickerControl::ColorPickerControl(Color color, bool alpha_enabled) :
    alpha_enabled_(alpha_enabled),
    hue_slider_(std::make_unique<SliderControl>(Orientation::Horizontal, 0.0f, 360.0f, 0.0f)),
    saturation_slider_(std::make_unique<SliderControl>(Orientation::Horizontal, 0.0f, 100.0f, 0.0f)),
    value_slider_(std::make_unique<SliderControl>(Orientation::Horizontal, 0.0f, 100.0f, 0.0f)),
    alpha_slider_(std::make_unique<SliderControl>(Orientation::Horizontal, 0.0f, 100.0f, 100.0f)),
    more_button_(std::make_unique<ButtonControl>()),
    representation_(
        std::make_unique<ComboBoxControl>(std::vector<std::string>{strings().color_picker.rgb, strings().color_picker.hsv},
                                          0)),
    alpha_box_(std::make_unique<TextBoxControl>()),
    hex_box_(std::make_unique<TextBoxControl>())
{
    for (auto& box : channel_boxes_)
    {
        box = std::make_unique<TextBoxControl>();
    }
    for (std::size_t i = 0; i < PartCount; ++i)
    {
        set_parent_of(*part(static_cast<Part>(i)), this);
    }

    for (SliderControl* slider : {hue_slider_.get(), saturation_slider_.get(), value_slider_.get(), alpha_slider_.get()})
    {
        slider->set_step(1.0f);
        slider->on_value_changed([this](double) { on_channel_changed(); });
    }

    // None of the boxes hold words, so no spell checking.
    for (std::size_t i = 0; i < channel_boxes_.size(); ++i)
    {
        TextBoxControl& box = *channel_boxes_[i];
        box.set_spell_check(false);
        box.on_text_changed([this, i](const std::string&) { on_channel_typed(i); });
        box.on_submit([this] { sync_texts(); });
    }
    alpha_box_->set_spell_check(false);
    alpha_box_->on_text_changed([this](const std::string&) { on_alpha_typed(); });
    alpha_box_->on_submit([this] { sync_texts(); });
    hex_box_->set_spell_check(false);
    hex_box_->on_submit([this] { commit_hex(); });

    representation_->on_selection_changed(
        [this](std::optional<std::size_t>)
        {
            sync_texts();
            mark_dirty(DirtyFlags::Visual);
        });

    // A borderless toggle: "More" and a down chevron, turning into "Less" and an up one.
    more_button_->set_variant(ButtonVariant::Subtle);
    more_button_->set_min_width(MoreButtonWidth);
    more_button_->set_min_height(MoreButtonHeight);
    auto row = std::make_unique<StackControl>(Orientation::Horizontal);
    row->set_spacing(8.0f);
    row->set_child_alignment(Alignment::Center);
    more_label_ = row->add(std::make_unique<TextControl>());
    more_glyph_ = row->add(std::make_unique<IconControl>(glyphs::ChevronDown));
    more_button_->set_content(std::move(row));
    more_button_->on_click([this] { toggle_more(); });

    // The More button's label and the representation names follow set_strings(); the channel
    // labels are read as they are drawn.
    detail::follow_strings(*this,
                           [this]
                           {
                               const Strings::ColorPickerText& t = strings().color_picker;
                               more_label_->set_text(more_expanded_ ? t.less : t.more);
                               if (representation_->items() != std::vector<std::string>{t.rgb, t.hsv})
                               {
                                   const std::optional<std::size_t> selected = representation_->selected_index();
                                   representation_->set_items({t.rgb, t.hsv});
                                   representation_->set_selected_index(selected);
                               }
                           });

    setup_hue_gradient();
    write_sliders(alpha_enabled_ ? color : color.with_alpha(1.0f));
    refresh_channel_gradients();
    sync_texts();
    add_subscription(strings_changed().observe([this] { sync_texts(); })); // the percent pattern
}

ColorPickerControl::~ColorPickerControl() = default;

// ---- parts ----

Element* ColorPickerControl::part(Part p) const
{
    switch (p)
    {
        case Part::Hue:
            return hue_slider_.get();
        case Part::Saturation:
            return saturation_slider_.get();
        case Part::Value:
            return value_slider_.get();
        case Part::Alpha:
            return alpha_slider_.get();
        case Part::MoreButton:
            return more_button_.get();
        case Part::Representation:
            return representation_.get();
        case Part::Channel0:
            return channel_boxes_[0].get();
        case Part::Channel1:
            return channel_boxes_[1].get();
        case Part::Channel2:
            return channel_boxes_[2].get();
        case Part::AlphaBox:
            return alpha_box_.get();
        case Part::HexBox:
            break;
    }
    return hex_box_.get();
}

bool ColorPickerControl::text_entry_shown() const
{
    return !more_button_visible_ || more_expanded_;
}

bool ColorPickerControl::shown(Part p) const
{
    switch (p)
    {
        case Part::Hue:
        case Part::Saturation:
            return spectrum_visible_;
        case Part::Value:
            return color_slider_visible_;
        case Part::Alpha:
            return alpha_enabled_ && alpha_slider_visible_;
        case Part::MoreButton:
            return more_button_visible_;
        case Part::Representation:
        case Part::Channel0:
        case Part::Channel1:
        case Part::Channel2:
            return text_entry_shown() && channel_input_visible_;
        case Part::AlphaBox:
            return text_entry_shown() && alpha_enabled_ && alpha_input_visible_;
        case Part::HexBox:
            break;
    }
    return text_entry_shown() && hex_input_visible_;
}

bool ColorPickerControl::hsv_mode() const
{
    return representation_->selected_index() == 1;
}

// ---- color ----

Color ColorPickerControl::color() const
{
    const ColorPickerHsv hsv{static_cast<float>(hue_slider_->value()),
                             static_cast<float>(saturation_slider_->value() / 100.0),
                             static_cast<float>(value_slider_->value() / 100.0),
                             alpha_enabled_ ? static_cast<float>(alpha_slider_->value() / 100.0) : 1.0f};
    return from_picker_hsv(hsv);
}

void ColorPickerControl::set_color(Color c)
{
    if (!alpha_enabled_)
    {
        c = c.with_alpha(1.0f);
    }
    const Color before = color();
    if (c == before)
    {
        // RGB cannot carry a hue at saturation 0. A two-way binding hands every change back here,
        // and writing it would snap a hue just typed into the HSV boxes back to 0.
        return;
    }
    write_sliders(c);
    settle(before, nullptr);
}

void ColorPickerControl::set_alpha_enabled(bool enabled)
{
    if (alpha_enabled_ == enabled)
    {
        return;
    }
    const Color before = color();
    alpha_enabled_     = enabled;
    if (!alpha_enabled_)
    {
        // Turning alpha off makes the color opaque. Resetting the slider too means turning
        // alpha back on starts from opaque too, instead of from a transparency nobody can see.
        updating_ = true;
        alpha_slider_->set_value(100.0f);
        updating_ = false;
    }
    visibility_changed();
    settle(before, nullptr);
}

void ColorPickerControl::write_sliders(const Color& c)
{
    const ColorPickerHsv hsv = to_picker_hsv(c);

    updating_ = true;
    hue_slider_->set_value(hsv.h);
    saturation_slider_->set_value(hsv.s * 100.0f);
    value_slider_->set_value(hsv.v * 100.0f);
    if (alpha_enabled_)
    {
        alpha_slider_->set_value(hsv.a * 100.0f);
    }
    updating_ = false;
}

void ColorPickerControl::settle(const Color& before, const TextBoxControl* typing)
{
    refresh_channel_gradients();
    sync_texts(typing);
    mark_dirty(DirtyFlags::Visual);
    const Color after = color();
    if (after != before && on_color_changed_)
    {
        on_color_changed_(after);
    }
}

void ColorPickerControl::on_channel_changed()
{
    if (updating_)
    {
        return;
    }
    // A slider has already moved by the time this runs, so there is no "before" to compare
    // against: a slider only reports a real change.
    refresh_channel_gradients();
    sync_texts();
    mark_dirty(DirtyFlags::Visual);
    if (on_color_changed_)
    {
        on_color_changed_(color());
    }
}

void ColorPickerControl::on_channel_typed(std::size_t index)
{
    if (updating_)
    {
        return;
    }
    const TextBoxControl&    box   = *channel_boxes_[index];
    const bool               hsv   = hsv_mode();
    const int                max   = hsv ? (index == 0 ? 359 : 100) : 255;
    const std::optional<int> typed = parse_component(box.text(), max, false);
    if (!typed)
    {
        return; // left as typed until Enter reverts it
    }

    const Color before = color();
    if (hsv)
    {
        // Straight onto the slider, not through RGB: a hue typed while saturation is 0 has no RGB
        // to survive in, and would snap back to 0 on the way.
        SliderControl& slider = index == 0 ? *hue_slider_ : index == 1 ? *saturation_slider_ : *value_slider_;
        updating_             = true;
        slider.set_value(static_cast<float>(*typed));
        updating_ = false;
    }
    else
    {
        Color       c                               = before;
        const float byte                            = static_cast<float>(*typed) / 255.0f;
        (index == 0 ? c.r : index == 1 ? c.g : c.b) = byte;
        write_sliders(c);
    }
    settle(before, &box);
}

void ColorPickerControl::on_alpha_typed()
{
    if (updating_)
    {
        return;
    }
    const std::optional<int> typed = parse_component(alpha_box_->text(), 100, true);
    if (!typed)
    {
        return;
    }
    const Color before = color();
    updating_          = true;
    alpha_slider_->set_value(static_cast<float>(*typed));
    updating_ = false;
    settle(before, alpha_box_.get());
}

void ColorPickerControl::setup_hue_gradient()
{
    std::vector<GradientStop> stops;
    stops.reserve(7);
    for (int i = 0; i <= 6; ++i)
    {
        stops.push_back({static_cast<float>(i) / 6.0f, from_picker_hsv({static_cast<float>(i) * 60.0f, 1.0f, 1.0f, 1.0f})});
    }
    hue_slider_->set_track_gradient(LinearGradientBrush{.stops = std::move(stops), .axis = GradientAxis::Horizontal});
}

void ColorPickerControl::refresh_channel_gradients()
{
    const float h = static_cast<float>(hue_slider_->value());
    const float s = static_cast<float>(saturation_slider_->value() / 100.0);
    const float v = static_cast<float>(value_slider_->value() / 100.0);

    saturation_slider_->set_track_gradient(LinearGradientBrush{.stops = {{0.0f, from_picker_hsv({h, 0.0f, v, 1.0f})},
                                                                         {1.0f, from_picker_hsv({h, 1.0f, v, 1.0f})}},
                                                               .axis  = GradientAxis::Horizontal});

    value_slider_->set_track_gradient(LinearGradientBrush{.stops = {{0.0f, from_picker_hsv({h, s, 0.0f, 1.0f})},
                                                                    {1.0f, from_picker_hsv({h, s, 1.0f, 1.0f})}},
                                                          .axis  = GradientAxis::Horizontal});

    const Color rgb = from_picker_hsv({h, s, v, 1.0f});
    alpha_slider_->set_track_gradient(LinearGradientBrush{.stops = {{0.0f, rgb.with_alpha(0.0f)}, {1.0f, rgb.with_alpha(1.0f)}},
                                                          .axis = GradientAxis::Horizontal},
                                      /*checkerboard=*/true);
}

void ColorPickerControl::sync_texts(const TextBoxControl* typing)
{
    const Color c = color();

    std::array<int, 3> channels{};
    if (hsv_mode())
    {
        // From the sliders rather than from c, for the same reason on_channel_typed() writes them
        // directly. A full turn of hue reads 0, as the box's 0-359 range does.
        channels = {static_cast<int>(std::lround(hue_slider_->value())) % 360,
                    static_cast<int>(std::lround(saturation_slider_->value())),
                    static_cast<int>(std::lround(value_slider_->value()))};
    }
    else
    {
        channels = {to_byte(c.r), to_byte(c.g), to_byte(c.b)};
    }

    updating_ = true;
    for (std::size_t i = 0; i < channel_boxes_.size(); ++i)
    {
        if (channel_boxes_[i].get() != typing)
        {
            channel_boxes_[i]->set_text(std::to_string(channels[i]));
        }
    }
    if (alpha_box_.get() != typing)
    {
        alpha_box_->set_text(
            detail::fill(strings().color_picker.percent, {std::to_string(static_cast<int>(std::lround(c.a * 100.0f)))}));
    }
    if (hex_box_.get() != typing)
    {
        hex_box_->set_text(format_hex(c, alpha_enabled_));
    }
    updating_ = false;
}

void ColorPickerControl::commit_hex()
{
    Color parsed;
    if (try_parse_hex(hex_box_->text(), alpha_enabled_, color().a, parsed))
    {
        set_color(parsed);
    }
    sync_texts(); // a valid commit already synced; an invalid one reverts here
}

void ColorPickerControl::enabled_changed()
{
    hue_slider_->set_enabled(is_enabled());
    saturation_slider_->set_enabled(is_enabled());
    value_slider_->set_enabled(is_enabled());
    alpha_slider_->set_enabled(is_enabled());
    more_button_->set_enabled(is_enabled());
    representation_->set_enabled(is_enabled());
    for (auto& box : channel_boxes_)
    {
        box->set_enabled(is_enabled());
    }
    alpha_box_->set_enabled(is_enabled());
    hex_box_->set_enabled(is_enabled());
    mark_dirty(DirtyFlags::Visual);
}

// ---- visibility ----

void ColorPickerControl::visibility_changed()
{
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void ColorPickerControl::set_spectrum_visible(bool visible)
{
    spectrum_visible_ = visible;
    visibility_changed();
}

void ColorPickerControl::set_preview_visible(bool visible)
{
    preview_visible_ = visible;
    visibility_changed();
}

void ColorPickerControl::set_color_slider_visible(bool visible)
{
    color_slider_visible_ = visible;
    visibility_changed();
}

void ColorPickerControl::set_alpha_slider_visible(bool visible)
{
    alpha_slider_visible_ = visible;
    visibility_changed();
}

void ColorPickerControl::set_more_button_visible(bool visible)
{
    more_button_visible_ = visible;
    visibility_changed();
}

void ColorPickerControl::set_color_channel_text_input_visible(bool visible)
{
    channel_input_visible_ = visible;
    visibility_changed();
}

void ColorPickerControl::set_alpha_text_input_visible(bool visible)
{
    alpha_input_visible_ = visible;
    visibility_changed();
}

void ColorPickerControl::set_hex_input_visible(bool visible)
{
    hex_input_visible_ = visible;
    visibility_changed();
}

void ColorPickerControl::toggle_more()
{
    // Swaps the button between More with a down chevron and Less with an up one, and shows or
    // hides the text entry section.
    more_expanded_ = !more_expanded_;
    more_label_->set_text(more_expanded_ ? strings().color_picker.less : strings().color_picker.more);
    more_glyph_->set_source(more_expanded_ ? glyphs::ChevronUp : glyphs::ChevronDown);
    visibility_changed();
}

// ---- layout ----

ColorPickerControl::Layout ColorPickerControl::compute_layout(float left, float top, float width) const
{
    Layout l;
    float  y       = top;
    float  gap     = 0.0f; // the bottom margin of the last block placed
    bool   started = false;
    // Stacks one block of `height`, after the previous block's bottom margin, and returns its top.
    auto block = [&](float height, float margin_bottom)
    {
        if (started)
        {
            y += gap;
        }
        const float at = y;
        y += height;
        gap     = margin_bottom;
        started = true;
        return at;
    };
    auto size  = [&](Part p) { return part_size_[static_cast<std::size_t>(p)]; };
    auto place = [&](Part p, const Rect& r) { l.parts[static_cast<std::size_t>(p)] = r; };

    if (spectrum_visible_)
    {
        const float slider_width = preview_visible_ ? std::max(0.0f, width - PreviewWidth - PreviewGap) : width;
        const float hue_height   = size(Part::Hue).height;
        const float height       = hue_height + SliderGap + size(Part::Saturation).height;
        const float at           = block(height, SpectrumBottom);
        place(Part::Hue, Rect{left, at, slider_width, hue_height});
        place(Part::Saturation, Rect{left, at + hue_height + SliderGap, slider_width, size(Part::Saturation).height});
        if (preview_visible_)
        {
            l.preview = Rect{left + width - PreviewWidth, at, PreviewWidth, height};
        }
    }
    else if (preview_visible_)
    {
        l.preview = Rect{left, block(PreviewAlone, SpectrumBottom), width, PreviewAlone};
    }

    if (shown(Part::Value))
    {
        const float height = size(Part::Value).height;
        place(Part::Value, Rect{left, block(height, ValueBottom), width, height});
    }
    if (shown(Part::Alpha))
    {
        const float height = size(Part::Alpha).height;
        place(Part::Alpha, Rect{left, block(height, AlphaBottom), width, height});
    }
    if (shown(Part::MoreButton))
    {
        // Right-aligned, with the text entry section directly beneath it.
        const Size  button = size(Part::MoreButton);
        const float at     = block(button.height, 0.0f);
        const float w      = std::min(button.width, width);
        place(Part::MoreButton, Rect{left + width - w, at, w, button.height});
    }

    if (text_entry_shown())
    {
        const float box_width = std::min(BoxWidth, width);
        const bool  channels  = shown(Part::Representation);
        const bool  hex       = shown(Part::HexBox);
        if (channels || hex)
        {
            const float rep_height = channels ? size(Part::Representation).height : 0.0f;
            const float hex_height = hex ? size(Part::HexBox).height : 0.0f;
            const float at         = block(std::max(rep_height, hex_height), EntryRowTop);
            if (channels)
            {
                place(Part::Representation, Rect{left, at, box_width, rep_height});
            }
            if (hex)
            {
                // Beside the dropdown against the right edge, or on its own at the left
                // when the channel boxes are hidden.
                const float room = channels ? std::max(0.0f, width - box_width - 5.0f) : width;
                const float w    = std::min(HexWidth, room);
                place(Part::HexBox, Rect{channels ? left + width - w : left, at, w, hex_height});
            }
        }
        auto entry_row = [&](Part p, std::size_t label)
        {
            const float height = size(p).height;
            const float at     = block(height, EntryRowTop);
            place(p, Rect{left, at, box_width, height});
            l.labels[label] = Rect{left + box_width + LabelGap, at, std::max(0.0f, width - box_width - LabelGap), height};
        };
        if (channels)
        {
            entry_row(Part::Channel0, 0);
            entry_row(Part::Channel1, 1);
            entry_row(Part::Channel2, 2);
        }
        if (shown(Part::AlphaBox))
        {
            entry_row(Part::AlphaBox, 3);
        }
    }

    l.height = y - top;
    return l;
}

Size ColorPickerControl::measure_override(Size available, const MeasureContext& context) const
{
    const float width        = std::isfinite(available.width) ? available.width : DefaultWidth;
    const float slider_width = preview_visible_ ? std::max(0.0f, width - PreviewWidth - PreviewGap) : width;
    const float box_width    = std::min(BoxWidth, width);

    // Every part, shown or not, so one that comes back into view already has a current size.
    auto measure_part = [&](Part p, float w)
    { part_size_[static_cast<std::size_t>(p)] = part(p)->measure(Size{w, Unbounded}, context); };
    measure_part(Part::Hue, slider_width);
    measure_part(Part::Saturation, slider_width);
    measure_part(Part::Value, width);
    measure_part(Part::Alpha, width);
    measure_part(Part::MoreButton, 0.0f);
    measure_part(Part::Representation, box_width);
    measure_part(Part::Channel0, box_width);
    measure_part(Part::Channel1, box_width);
    measure_part(Part::Channel2, box_width);
    measure_part(Part::AlphaBox, box_width);
    measure_part(Part::HexBox, std::min(HexWidth, width));

    Size desired{width, compute_layout(0.0f, 0.0f, width).height};
    desired.height = std::min(desired.height, available.height);
    return desired;
}

void ColorPickerControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    const Layout l = compute_layout(final_rect.left(), final_rect.top(), final_rect.width);
    for (std::size_t i = 0; i < PartCount; ++i)
    {
        // A hidden part is parked on an empty rect at the origin: nothing reaches it, and a stale
        // rect of a part that used to be somewhere else cannot be mistaken for a live one.
        const Part p = static_cast<Part>(i);
        part(p)->arrange(shown(p) ? l.parts[i] : Rect{final_rect.left(), final_rect.top(), 0.0f, 0.0f});
    }
    preview_rect_ = l.preview;
    label_rects_  = l.labels;
}

// ---- focus and input ----

void ColorPickerControl::for_each_child(ChildVisitor visit) const
{
    for (std::size_t i = 0; i < PartCount; ++i)
    {
        if (shown(static_cast<Part>(i)))
        {
            visit(*part(static_cast<Part>(i)));
        }
    }
}

// ---- render ----

void ColorPickerControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    if (preview_visible_)
    {
        const Color c = color();
        if (alpha_enabled_ && c.a < 1.0f)
        {
            renderer.fill_rect_pattern(preview_rect_, Pattern::Checkerboard50, colors::White, Color::from_rgba(205, 205, 205));
        }
        renderer.fill_rounded_rect(preview_rect_, theme.corner_radius, c);
        stroke_rounded_rect_inside(renderer,
                                   preview_rect_,
                                   theme.corner_radius,
                                   theme.colors().text.with_alpha(0.16f),
                                   theme.border_width);
    }

    for (std::size_t i = 0; i < PartCount; ++i)
    {
        if (shown(static_cast<Part>(i)))
        {
            part(static_cast<Part>(i))->render(renderer, context);
        }
    }

    TextStyle style  = theme.text_style;
    style.horizontal = TextAlign::Leading;
    if (!is_enabled())
    {
        style.effect = theme.disabled_text_effect;
    }
    const Color text_color = is_enabled() ? theme.colors().text : theme.colors().text_disabled;
    if (shown(Part::Channel0))
    {
        const std::array<const std::string*, 3> labels = channel_labels(hsv_mode());
        for (std::size_t i = 0; i < 3; ++i)
        {
            renderer.draw_text(*labels[i], label_rects_[i], text_color, style);
        }
    }
    if (shown(Part::AlphaBox))
    {
        renderer.draw_text(strings().color_picker.opacity, label_rects_[3], text_color, style);
    }
}

// ---- forwarding ----

void ColorPickerControl::clear_dirty_override(DirtyFlags flags)
{
    for (std::size_t i = 0; i < PartCount; ++i)
    {
        if (!shown(static_cast<Part>(i)))
        {
            clear_dirty_of(*part(static_cast<Part>(i)), flags);
        }
    }
}

std::unique_ptr<ui::Element> ColorPicker::build() &&
{
    // Both into the constructor: the initial color keeps its alpha only if alpha is already on.
    auto node = std::make_unique<ui::ColorPickerControl>(color.initial(), alpha_enabled.initial());

    // alpha_enabled ahead of color, so a binding that turns alpha on and sets a translucent
    // color in one go does not have the alpha stripped on the way in.
    apply(alpha_enabled, *node, [raw = node.get()](const bool& v) { raw->set_alpha_enabled(v); });
    apply(color, *node, [raw = node.get()](const ui::Color& v) { raw->set_color(v); });
    apply(enabled, *node, [raw = node.get()](const bool& v) { raw->set_enabled(v); });
    apply(spectrum_visible, *node, [raw = node.get()](const bool& v) { raw->set_spectrum_visible(v); });
    apply(preview_visible, *node, [raw = node.get()](const bool& v) { raw->set_preview_visible(v); });
    apply(color_slider_visible, *node, [raw = node.get()](const bool& v) { raw->set_color_slider_visible(v); });
    apply(alpha_slider_visible, *node, [raw = node.get()](const bool& v) { raw->set_alpha_slider_visible(v); });
    apply(more_button_visible, *node, [raw = node.get()](const bool& v) { raw->set_more_button_visible(v); });
    apply(color_channel_text_input_visible,
          *node,
          [raw = node.get()](const bool& v) { raw->set_color_channel_text_input_visible(v); });
    apply(alpha_text_input_visible, *node, [raw = node.get()](const bool& v) { raw->set_alpha_text_input_visible(v); });
    apply(hex_input_visible, *node, [raw = node.get()](const bool& v) { raw->set_hex_input_visible(v); });

    if (auto changed = ui::write_back(color, std::move(on_color_changed)))
    {
        node->on_color_changed(std::move(changed));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
