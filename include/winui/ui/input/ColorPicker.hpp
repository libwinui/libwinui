// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/ComboBox.hpp"
#include "winui/ui/input/Slider.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <array>
#include <memory>
#include <string>
#include <utility>

#include <cstddef>

namespace ui
{

class TextControl;
class IconControl;

/// A hue/saturation/value/alpha color editor with channel, alpha and hex text entry and a
/// preview swatch.
///
/// A container of independently focusable sub-controls, laid out vertically from the top: hue
/// and saturation sliders beside the preview (standing in for the 2D spectrum, which is not
/// drawn), the value slider, the alpha slider, an optional More button, then the text entry
/// section (an RGB/HSV dropdown and a `#RRGGBB` hex box, three channel boxes and an Opacity
/// box). Tab walks the parts in that order; the picker itself is never focused.
///
/// Every part has a `*_visible` switch. A hidden part is not laid out, drawn, hit-tested or
/// reachable by Tab, and showing it again needs no rebuild. A channel box applies a valid value
/// as it is typed (0-255 for RGB, 0-359 hue, 0-100 saturation, value and opacity); Enter
/// reverts invalid text. The hex box commits on Enter only: 6 or 8 hex digits (`#RRGGBBAA`,
/// '#' optional, case-insensitive); anything else reverts.
///
/// Example:
/// @code
/// auto row = std::make_unique<ui::StackControl>(ui::Orientation::Horizontal);
/// ui::ColorPickerControl* picker = row->add(std::make_unique<ui::ColorPickerControl>(ui::Color::from_rgba(0, 120, 215),
///                                                                             /*alpha_enabled=*/true));
/// picker->set_more_button_visible(true); // text entry collapsed behind "More"
/// ui::BorderControl* swatch = row->add(std::make_unique<ui::BorderControl>());
/// swatch->set_width(100.0f);
/// swatch->set_height(100.0f);
/// swatch->set_background(picker->color());
/// picker->on_color_changed([swatch](ui::Color c) { swatch->set_background(c); });
/// window.set_content(std::move(row));
///
/// // Declaratively, two-way through a Property (the equality gate stops the echo):
/// ui::Property<ui::Color> accent{ui::Color::from_rgba(0, 120, 215)};
/// window.set_content(ui::HorizontalStack{
///     .spacing  = 16,
///     .content  = {
///         ui::ColorPicker{
///             .color               = accent,
///             .alpha_enabled       = true,
///             .more_button_visible = true,
///         },
///         ui::Border{.background = ui::bind(accent, [](ui::Color c) { return std::optional{c}; }),
///                           .width      = 100.0f,
///                           .height     = 100.0f},
///     },
/// });
/// @endcode
class ColorPickerControl final : public Element
{
public:
    /// A picker showing `color`; its alpha is kept only when `alpha_enabled`.
    explicit ColorPickerControl(Color color = colors::Black, bool alpha_enabled = false);
    /// Destroys the owned sub-controls.
    ~ColorPickerControl() override;

    // ---- color ----
    /// The selected color, computed from the sliders' HSV values on every call. Opaque while
    /// alpha_enabled() is false.
    [[nodiscard]] Color color() const;
    /// Forces alpha to 1 when alpha_enabled() is false; updates every slider, box and the preview.
    /// Fires on_color_changed only if the resulting color differs from the current one.
    void set_color(Color color);

    /// Whether the alpha channel is shown and honored; false by default. While false, the alpha
    /// slider and box are hidden regardless of their own switches and the color is opaque.
    [[nodiscard]] bool alpha_enabled() const
    {
        return alpha_enabled_;
    }
    /// Turns the alpha channel on or off. Turning it off makes the color opaque, firing on_color_changed
    /// if that changes it.
    void set_alpha_enabled(bool enabled);


    // ---- visibility ----
    /// Whether the hue and saturation sliders show; true by default.
    [[nodiscard]] bool spectrum_visible() const
    {
        return spectrum_visible_;
    }
    /// Shows or hides the hue and saturation sliders standing in for the spectrum.
    /// Hiding them stretches the preview across the row at 44 DIPs tall. Updates the layout.
    void set_spectrum_visible(bool visible);

    /// Whether the preview swatch shows; true by default.
    [[nodiscard]] bool preview_visible() const
    {
        return preview_visible_;
    }
    /// Shows or hides the swatch showing the current color. Updates the layout.
    void set_preview_visible(bool visible);

    /// Whether the value slider shows; true by default.
    [[nodiscard]] bool color_slider_visible() const
    {
        return color_slider_visible_;
    }
    /// Shows or hides the value slider. Updates the layout.
    void set_color_slider_visible(bool visible);

    /// Whether the alpha slider shows; true by default, but shown only while alpha_enabled() too.
    [[nodiscard]] bool alpha_slider_visible() const
    {
        return alpha_slider_visible_;
    }
    /// Shows or hides the alpha slider, shown only while alpha_enabled() too. Updates the layout.
    void set_alpha_slider_visible(bool visible);

    /// Whether the More button shows; false by default.
    [[nodiscard]] bool more_button_visible() const
    {
        return more_button_visible_;
    }
    /// Shows or hides the More button. While shown, the text entry section is collapsed
    /// until the button is clicked. Updates the layout.
    void set_more_button_visible(bool visible);

    /// Whether the channel inputs show; true by default.
    [[nodiscard]] bool color_channel_text_input_visible() const
    {
        return channel_input_visible_;
    }
    /// Shows or hides the RGB/HSV dropdown and the three channel boxes.
    /// Hiding them moves the hex box to the left. Updates the layout.
    void set_color_channel_text_input_visible(bool visible);

    /// Whether the Opacity box shows; true by default, but shown only while alpha_enabled() too.
    [[nodiscard]] bool alpha_text_input_visible() const
    {
        return alpha_input_visible_;
    }
    /// Shows or hides the Opacity box, shown only while alpha_enabled() too.
    /// Updates the layout.
    void set_alpha_text_input_visible(bool visible);

    /// Whether the `#RRGGBB` box shows; true by default.
    [[nodiscard]] bool hex_input_visible() const
    {
        return hex_input_visible_;
    }
    /// Shows or hides the `#RRGGBB` box. Updates the layout.
    void set_hex_input_visible(bool visible);

    /// Whether the text entry section is expanded behind the More button. Meaningless while the
    /// button is hidden, when the section is always shown.
    [[nodiscard]] bool more_expanded() const
    {
        return more_expanded_;
    }

    /// Fired with the new color whenever `color` changes, from a slider drag, a
    /// click-to-jump on any channel's track, a keyboard step, a typed channel value, a committed
    /// hex edit or set_color() alike. Replaces any previous handler.
    void on_color_changed(Callback<void(Color)> handler)
    {
        on_color_changed_ = std::move(handler);
    }

    // ---- children ----
    /// Lists the shown sub-controls in Tab order, so a hidden part gets no focus, input,
    /// animation or automation. The picker itself is never a tab stop.
    void for_each_child(ChildVisitor visit) const override;

protected:
    // ---- layout ----
    /// Stacks the shown parts at the available width; the height follows from what is shown.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Places every shown part.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;

    // ---- input ----

    // ---- render ----
    /// Draws the shown sub-controls, the channel labels and the preview swatch.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Reaches every sub-control, shown or not, unlike for_each_child(), so a part that comes
    /// back into view does not carry flags from before it was hidden.
    void clear_dirty_override(DirtyFlags flags) override;


private:
    enum class Part
    {
        Hue,
        Saturation,
        Value,
        Alpha,
        MoreButton,
        Representation,
        Channel0,
        Channel1,
        Channel2,
        AlphaBox,
        HexBox,
    };
    static constexpr std::size_t PartCount = 11;

    // Where everything goes for a given origin and width. Pure, so measure_override() takes the height
    // from the very pass arrange_override() then places children with.
    struct Layout
    {
        std::array<Rect, PartCount> parts{};
        Rect                        preview{};
        std::array<Rect, 4>         labels{}; // the three channel labels, then Opacity
        float                       height = 0.0f;
    };
    [[nodiscard]] Layout compute_layout(float left, float top, float width) const;

    [[nodiscard]] Element* part(Part p) const;
    [[nodiscard]] bool     shown(Part p) const;
    [[nodiscard]] bool     text_entry_shown() const;
    [[nodiscard]] bool     hsv_mode() const;

    void setup_hue_gradient(); // constant rainbow track, set once
    // After the sliders moved: recompute the gradients, rewrite every text box but `typing`, and
    // fire on_color_changed if color() is no longer `before`.
    void settle(const Color& before, const TextBoxControl* typing);
    void refresh_channel_gradients(); // saturation/value/alpha tracks, from current h/s/v
    // Rewrites every text box from the current color, except `typing`, the one being typed into.
    void sync_texts(const TextBoxControl* typing = nullptr);
    void commit_hex(); // parses hex_box_->text(): valid -> set_color(); invalid -> revert
    // Slider writes only, no settle(): set_color()'s and the RGB boxes' shared first half.
    void write_sliders(const Color& c);
    void on_channel_changed(); // wired to all four sliders' on_value_changed
    void on_channel_typed(std::size_t index);
    void on_alpha_typed();
    void toggle_more();
    void visibility_changed();

    bool alpha_enabled_         = false;
    bool spectrum_visible_      = true;
    bool preview_visible_       = true;
    bool color_slider_visible_  = true;
    bool alpha_slider_visible_  = true;
    bool more_button_visible_   = false;
    bool channel_input_visible_ = true;
    bool alpha_input_visible_   = true;
    bool hex_input_visible_     = true;
    bool more_expanded_         = false;

    std::unique_ptr<SliderControl>                 hue_slider_;
    std::unique_ptr<SliderControl>                 saturation_slider_;
    std::unique_ptr<SliderControl>                 value_slider_;
    std::unique_ptr<SliderControl>                 alpha_slider_;
    std::unique_ptr<ButtonControl>                 more_button_;
    std::unique_ptr<ComboBoxControl>               representation_; // RGB / HSV
    std::array<std::unique_ptr<TextBoxControl>, 3> channel_boxes_;
    std::unique_ptr<TextBoxControl>                alpha_box_;
    std::unique_ptr<TextBoxControl>                hex_box_;
    TextControl*                                   more_label_ = nullptr; // inside more_button_'s content
    IconControl*                                   more_glyph_ = nullptr;

    // Guards writes made on the user's behalf, so gradients, texts and on_color_changed_ run once,
    // after every channel is written.
    bool updating_ = false;

    // Set by measure_override() for arrange_override() and render().
    mutable std::array<Size, PartCount> part_size_{};

    Rect                preview_rect_{};
    std::array<Rect, 4> label_rects_{}; // the three channel labels, then Opacity

    Callback<void(Color)> on_color_changed_;

protected:
    void enabled_changed() override;
};

/// A hue/saturation/value/alpha color editor with channel, alpha and hex text entry and a
/// preview swatch. Builds a ui::ColorPickerControl. Each `*_visible` field shows or hides one
/// part; ui::ColorPickerControl's class comment describes the parts.
///
/// `.color = prop` is two-way: it shows the property and writes the picked color back into it
/// before `on_color_changed` runs.
///
/// Example:
/// @code
/// ui::Property<ui::Color> accent{ui::Color::from_rgba(0, 120, 215)};
///
/// window.set_content(ui::HorizontalStack{.spacing = 16, .content = {
///     ui::ColorPicker{
///         .color                = accent,
///         .alpha_enabled        = true,
///         .more_button_visible  = true,
///         .horizontal_alignment = ui::Alignment::Leading,
///     },
///     ui::Border{.background = ui::bind(accent, [](ui::Color c) { return std::optional{c}; }),
///                .width      = 100.0f,
///                .height     = 100.0f},
/// }});
/// @endcode
struct ColorPicker
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<ColorPicker>` points at.
    using control_type = ColorPickerControl;

    // ---- properties ----
    /// The selected color. Its alpha is kept only while `alpha_enabled` is true.
    TwoWayField<Color> color{colors::Black};
    /// Show and honor the alpha channel. Applied before `color`.
    Binding<bool> alpha_enabled{false};
    Binding<bool> spectrum_visible{true};                 ///< The hue and saturation sliders.
    Binding<bool> preview_visible{true};                  ///< The preview swatch.
    Binding<bool> color_slider_visible{true};             ///< The value slider.
    Binding<bool> alpha_slider_visible{true};             ///< The alpha slider (with `alpha_enabled`).
    Binding<bool> more_button_visible{false};             ///< The More button.
    Binding<bool> color_channel_text_input_visible{true}; ///< The RGB/HSV dropdown and channel boxes.
    Binding<bool> alpha_text_input_visible{true};         ///< The Opacity box (with `alpha_enabled`).
    Binding<bool> hex_input_visible{true};                ///< The `#RRGGBB` box.

    // ---- appearance / layout ----
    Style                             style{};                ///< Named style; explicit fields win.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Min width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Min height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Max width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Max height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment in its slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment in its slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{};    ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Binding<bool>                     enabled{true}; ///< False grays it out and takes no input.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// Fires with the new color when it changes, from any part (slider drag, keyboard step, typed
    /// channel value or committed hex edit). A bound value doesn't run it.
    Callback<void(Color)> on_color_changed{};
    /// The pointer moved onto the control.
    Action on_pointer_entered{};
    /// The pointer left the control.
    Action on_pointer_exited{};
    /// Keyboard focus arrived on the control, or on anything inside it, from outside it. A
    /// control with nothing focusable in it never gets focus, and never raises this.
    Action on_got_focus{};
    /// Keyboard focus left the control and everything inside it.
    Action on_lost_focus{};
    /// A key pressed while focus is on the control or inside it, before the control's own
    /// handling; ui::Handled stops it there (Element::on_key_down()).
    Callback<Handling(const KeyEvent&)> on_key_down{};
    /// A wheel turn over the control, before the control scrolls; ui::Handled stops it there
    /// (Element::on_wheel()).
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    ControlOut<ColorPicker> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::ColorPickerControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
