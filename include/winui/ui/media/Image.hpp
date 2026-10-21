// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/ImageSource.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <string>

#include <cstddef>

namespace ui
{

/// Shows an ImageSource (a still picture, an animated GIF or an SVG document) stretched into the
/// space layout gives it.
///
/// The declarative spec is `ui::Image`, and the pixels themselves are a `ui::Bitmap`. Sizing
/// follows the Stretch mode, Uniform by default. A nine-grid can keep the corners unscaled. An
/// animated source plays by itself when its `auto_play` is set, and play() and stop() control it.
/// It pauses while it is off screen (scrolled out of view, clipped away or not drawn) and goes on
/// from the frame it showed when it comes back. The image takes no input.
///
/// Example:
/// @code
/// // Declarative:
/// window.set_content(ui::Image{
///     .source  = ui::Bitmap::read_png("photo.png"),
///     .stretch = ui::Stretch::UniformToFill,
///     .width   = 320.0f,
///     .height  = 200.0f,
/// });
///
/// // Imperative, an animated GIF:
/// auto  stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto* gif   = stack->add(std::make_unique<ui::ImageControl>(ui::AnimatedImage::read("spinner.gif")));
/// gif->set_stretch(ui::Stretch::None);
/// auto* pause = stack->add(std::make_unique<ui::ButtonControl>("Pause"));
/// pause->on_click([gif] { gif->is_playing() ? gif->stop() : gif->play(); });
/// @endcode
class ImageControl final : public Element
{
public:
    /// An empty image. It measures to zero and draws nothing.
    ImageControl() = default;
    /// An image showing `source`. An animated source with `auto_play` set starts playing.
    explicit ImageControl(ImageSource source);

    /// The picture shown.
    [[nodiscard]] const ImageSource& source() const
    {
        return source_;
    }
    /// Replaces the picture and updates layout. Starts an animated source playing when its
    /// `auto_play` says so. A source still loading through ImageSource::load() draws nothing until
    /// its decode is done. Then the control updates its layout and raises on_image_opened() or
    /// on_image_failed(). The window stays idle while the picture decodes.
    void set_source(ImageSource source);

    /// Fires when a source made by ImageSource::load() has finished decoding and shows.
    void on_image_opened(Callback<void()> handler)
    {
        check_thread();
        on_image_opened_ = std::move(handler);
    }
    /// Fires when a source made by ImageSource::load() could not be read or decoded. The handler
    /// gets the reason.
    void on_image_failed(Callback<void(std::string_view)> handler)
    {
        check_thread();
        on_image_failed_ = std::move(handler);
    }

    /// How the picture fills the element's box. Uniform by default.
    [[nodiscard]] Stretch stretch() const
    {
        return stretch_;
    }
    /// Sets the stretch mode. Updates layout and redraws.
    void set_stretch(Stretch stretch)
    {
        stretch_ = stretch;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The nine-grid insets, in the picture's pixels. Zero, the default, draws the picture whole.
    /// Ignored for an SVG source.
    [[nodiscard]] const Thickness& nine_grid() const
    {
        return nine_grid_;
    }
    /// Sets the nine-grid insets. Redraws.
    void set_nine_grid(const Thickness& nine_grid)
    {
        nine_grid_ = nine_grid;
        mark_dirty(DirtyFlags::Visual);
    }

    // ---- animated sources ----
    /// Whether the source has more than one frame.
    [[nodiscard]] bool is_animated() const
    {
        const AnimatedImage* image = source_.animated();
        return image != nullptr && image->animated();
    }
    /// Whether the animation plays. play() sets it and stop() clears it. An animation that ran to
    /// its end still reports true.
    [[nodiscard]] bool is_playing() const
    {
        return is_playing_;
    }
    /// Restarts a stopped animation. Does nothing to one already running or one that ran to its
    /// end.
    void play();
    /// Stops and rewinds to the first frame.
    void stop();
    /// The index of the frame showing, 0 for a still source.
    [[nodiscard]] std::size_t current_frame() const
    {
        return frame_;
    }

    /// Returns true, since measuring reads only the source's size.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }
    void on_screen_changed(bool on_screen) override;

    // ---- automation ----
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Image;
    }

    /// Where the picture lands and which part of it shows, for the current bounds.
    struct Placement
    {
        /// The whole picture's rect, in DIPs. It may extend past the element.
        Rect content;
        /// The part of the element's drawing that isn't clipped away.
        Rect visible;
    };
    /// Computes the Placement for the current bounds and source.
    [[nodiscard]] Placement placement() const;

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               render_override(Renderer& renderer, const RenderContext& context) const override;
    void               tick_override(Duration delta) override;


private:
    enum class StretchDirection
    {
        Unknown,
        Horizontal,
        Vertical,
    };
    [[nodiscard]] Size scaled(Size available, StretchDirection preferred, StretchDirection* chosen) const;
    void               restart_animation();
    void               opened();     // what set_source() does once the picture is there
    void               watch_load(); // has a load() still decoding wake the window when it is done

    ImageSource      source_{};
    ImageLoadState   seen_load_    = ImageLoadState::Loaded; // as last seen by tick_override()
    bool             load_watched_ = false; // the decode wakes the window when it is done (ImageSource::when_loaded())
    Callback<void()> on_image_opened_;
    Callback<void(std::string_view)> on_image_failed_;
    Stretch                          stretch_ = Stretch::Uniform;
    Thickness                        nine_grid_{};

    mutable StretchDirection measure_direction_ = StretchDirection::Unknown;
    mutable Size             measured_{};

    // Animated playback.
    std::size_t frame_      = 0;
    int         loop_       = 0;
    double      frame_time_ = 0.0; // ms the current frame has been showing
    bool        stopped_    = true;
    bool        is_playing_ = false;
    bool        resumed_    = false; // back on screen: the next tick's delta is time spent off it
};

/// An image: a still picture, an animated GIF or an SVG document, stretched into its space.
/// Builds a ui::ImageControl. The pixels themselves are a `ui::Bitmap`.
///
/// `source` takes any of the three, since ui::ImageSource converts from each. It can be bound. A
/// Binding doesn't compare its values, only the Property behind it does, so
/// `bind(choice, [](int i) -> ui::ImageSource { ... })` swaps pictures as `choice` changes.
///
/// Example:
/// @code
/// std::vector<ui::Bitmap> photos = {ui::Bitmap::read("cliff.jpg"), ui::Bitmap::read("lake.jpg")};
/// ui::Property<int> choice{0};
///
/// window.set_content(ui::VerticalStack{.spacing = 8, .content = {
///     ui::Image{.source  = ui::bind(choice, [&photos](int i) -> ui::ImageSource { return photos[i]; }),
///               .stretch = ui::Stretch::UniformToFill,
///               .width   = 300.0f,
///               .height  = 200.0f},
///     ui::Button{.text = "Next", .on_click = [&] { choice = (choice.get() + 1) % 2; }},
/// }});
/// @endcode
struct Image
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Image>` points at.
    using control_type = ImageControl;

    // ---- properties ----
    /// The picture: a bitmap, an animated image or an SVG document. Empty draws nothing.
    Binding<ImageSource> source{};
    /// How the picture fills the element's box. Uniform, the default, keeps the aspect ratio.
    Binding<Stretch> stretch{Stretch::Uniform};
    /// The nine-grid insets, in the picture's pixels. The corners keep their size and the edges and
    /// center stretch. Zero, the default, draws the picture whole.
    Binding<Thickness> nine_grid{};

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
    Binding<std::string>              tool_tip{}; ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// A source made by ImageSource::load() finished decoding (ImageControl::on_image_opened()).
    Callback<void()> on_image_opened{};
    /// A source made by ImageSource::load() failed, with the reason (ImageControl::on_image_failed()).
    Callback<void(std::string_view)> on_image_failed{};
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
    ControlOut<Image> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::ImageControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
