// SPDX-License-Identifier: MIT
#pragma once

// The window frame a theme draws under Frame::Themed: the border, the caption with its icon and
// title, and the minimize, maximize/restore and close buttons. Window owns one beside its content
// while the theme has a frame (ControlChrome::window_frame) and lays the content out inside it.
// Private: an app only ever asks for Frame::Themed.

#include "themes/ControlChrome.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace ui
{

class Window;

namespace detail
{

class WindowFrameElement final : public Element
{
public:
    // The parts of the caption a press can land on.
    enum class Part
    {
        None,
        Icon,
        Minimize,
        Maximize,
        Close,
    };

    // Repaints itself whenever `window` activates, deactivates or changes state.
    explicit WindowFrameElement(Window& window);

    // The era's frame; a change relayouts and repaints.
    void                                            set_chrome(const ControlChrome::WindowFrame& chrome);
    [[nodiscard]] const ControlChrome::WindowFrame& chrome() const
    {
        return chrome_;
    }

    // Where the content goes inside `client`: the client less the border, the caption and the
    // rule under it. All of the client while full screen.
    [[nodiscard]] Rect content_rect(const Rect& client) const;
    // The border width on each side right now: none while maximized or full screen.
    [[nodiscard]] float border() const;

    // The parts' rectangles in the current bounds. Empty when the part is not shown.
    [[nodiscard]] Rect part_rect(Part part) const;
    // Whether a shown button takes clicks (WindowStyle::minimize_button, maximize_button, closable).
    [[nodiscard]] bool part_enabled(Part part) const;
    // The part under `p`, None over the rest of the caption and the border.
    [[nodiscard]] Part part_at(Point p) const;

    [[nodiscard]] HitRegion hit_region(Point p) const override;

    // The caption for UI Automation: a TitleBar over the caption, named after the window, with a
    // Button for each caption button shown. Not one of the frame's children; a host lists it
    // beside the content.
    [[nodiscard]] Element* title_bar_node() const
    {
        return title_bar_.get();
    }
    // The caption buttons shown, as title_bar_node()'s children.
    void automation_buttons(std::vector<Element*>& out) const;
    // What a click on `part` does; false for a part not shown or disabled.
    bool invoke(Part part);

protected:
    Size measure_override(Size available, const MeasureContext& context) const override;
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    bool pointer_override(const PointerEvent& e) override;
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;

private:
    [[nodiscard]] bool fullscreen() const;
    [[nodiscard]] Rect caption_rect() const;
    // Rows of the caption art that belong to the frame above the bar (Luna) and are not drawn while
    // the border is gone.
    [[nodiscard]] float top_cut() const;
    // The part of the caption the icon, the title and the buttons are centered in.
    [[nodiscard]] Rect title_bar() const;
    void               render_button(Renderer& renderer, Part part) const;
    void               render_win3x_button(Renderer& renderer, Part part) const;
    void               render_win2x_button(Renderer& renderer, Part part) const;
    void               render_menu_box(Renderer& renderer, const Rect& box) const;
    void               render_luna_button(Renderer& renderer, Part part, bool active) const;
    void               render_aero_button(Renderer& renderer, Part part, bool active) const;
    void               render_mdl_button(Renderer& renderer, Part part, bool active) const;
    // Aero glass over the blurred desktop: the tint, the reflection streaks and the edge lines,
    // with the content area filled opaque.
    // The pixels cut from each top corner, a row at a time in DIPs: none while maximized or full
    // screen, or for a square frame.
    [[nodiscard]] std::vector<float> corner_cut() const;
    void                             render_glass(Renderer& renderer, bool active) const;
    // Whether the frame is glass right now: the theme's frame has glass and the host blurs.
    [[nodiscard]] bool glass() const;
    // The caption title's text style, with Luna's shadow while active.
    [[nodiscard]] TextStyle title_style(bool active) const;
    // The face of a glossy button (Luna, Aero Basic) for its state and the window's.
    [[nodiscard]] const ControlChrome::WindowFrame::Face& face(Part part, bool active) const;
    // Whether the caption art runs the full width and carries the top of the frame (Luna, Aero Basic).
    [[nodiscard]] bool caption_carries_top() const;
    void               click(Part part);

    Window&                    window_;
    ControlChrome::WindowFrame chrome_{};
    Part                       hovered_ = Part::None;
    Part                       pressed_ = Part::None;
    // The caption and minimize, maximize and close, as automation nodes; the frame draws them.
    std::unique_ptr<Element>                title_bar_;
    std::array<std::unique_ptr<Element>, 3> buttons_;
};

} // namespace detail
} // namespace ui
