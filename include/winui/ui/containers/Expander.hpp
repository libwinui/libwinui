// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ui
{

/// Which way an Expander opens.
enum class ExpandDirection
{
    Down, ///< The default. The content opens below the header.
    Up,   ///< The content opens above the header, which stays at the bottom.
};

/// A header row that shows or hides one piece of content below or above it.
///
/// The header is the control's single tab stop. A click on it or Space toggles expanded(). Enter
/// doesn't, since it steps into the Expander when it is a focus scope. While collapsed, the
/// content isn't part of the tree at all. It isn't arranged or drawn, and it is out of the tab
/// order and the automation tree. Opening and closing slide the content out from behind the
/// header, clipped, in 333 ms when opening and 167 ms when closing. The control's own height
/// changes at once. set_content() takes ownership of the content and returns a non-owning
/// observer.
///
/// Example:
/// @code
/// // Declarative:
/// ui::Property<bool> open{false};
/// window.set_content(ui::Expander{
///     .header             = "Advanced options",
///     .expanded           = open,
///     .vertical_alignment = ui::Alignment::Leading,
///     .content            = ui::VerticalStack{
///         .spacing  = 8,
///         .content  = {
///             ui::CheckBox{.text = "Verbose logging"},
///             ui::CheckBox{.text = "Keep temporary files"},
///         },
///     },
/// });
///
/// // Imperative:
/// auto  stack    = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto* expander = stack->add(std::make_unique<ui::ExpanderControl>("Details"));
/// expander->set_content(std::make_unique<ui::TextControl>("Shown while expanded"));
/// expander->on_expanded_changed([&](bool now) { open = now; });
/// expander->set_expanded(true); // fires the handler
/// @endcode
class ExpanderControl final : public Element, private ExpandCollapseProvider
{
public:
    /// A collapsed Expander with an empty text header and no content.
    ExpanderControl() : ExpanderControl(std::string{})
    {
    }
    /// An Expander with `header` as its text header, starting expanded or collapsed as `expanded`
    /// says. Doesn't fire on_expanded_changed().
    explicit ExpanderControl(std::string header, bool expanded = false);

    // ---- header ----
    /// The header text, drawn in the header row unless header_content() replaces it.
    [[nodiscard]] const std::string& header() const
    {
        return header_;
    }
    /// Changes the header text. Updates layout.
    void set_header(std::string header)
    {
        header_ = std::move(header);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// Sets any element as header content, drawn in the header row in place of the text and sized
    /// like it. The row stretches it, so the element's own alignment places it within the row. It
    /// is part of the header button, so it gets no input of its own, and a click on it toggles the
    /// Expander. nullptr goes back to the text header. Returns a non-owning observer.
    template <typename T>
    T* set_header_content(std::unique_ptr<T> content)
    {
        static_assert(std::is_base_of_v<Element, T>, "Expander header content must be an Element");
        T* observer = content.get();
        if (observer)
        {
            if (observer) // null clears
            {
                set_parent_of(*observer, this);
            }
        }
        header_content_ = std::move(content);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// The header content set with set_header_content(), or nullptr for a text header. Owned by
    /// the Expander.
    [[nodiscard]] Element* header_content() const
    {
        return header_content_.get();
    }

    /// Which side the content opens on. Up puts the content above the header, rounds the header's
    /// bottom corners only while open, slides the content up from behind the header and turns the
    /// chevron around.
    [[nodiscard]] ExpandDirection expand_direction() const
    {
        return expand_direction_;
    }
    /// Changes the direction. Updates layout.
    void set_expand_direction(ExpandDirection direction)
    {
        expand_direction_ = direction;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    // ---- content ----
    /// Takes ownership of the collapsible content and returns a non-owning observer, like
    /// ScrollControl::set_child(). Replaces and destroys any previous content. nullptr clears it.
    template <typename T>
    T* set_content(std::unique_ptr<T> content)
    {
        static_assert(std::is_base_of_v<Element, T>, "Expander content must be an Element");
        T* observer = content.get();
        if (observer) // null clears
        {
            set_parent_of(*observer, this);
        }
        content_ = std::move(content);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// The collapsible content, or nullptr before set_content(). Owned by the Expander.
    [[nodiscard]] Element* content() const
    {
        return content_.get();
    }

    /// Whether the content region is open or opening.
    [[nodiscard]] bool expanded() const
    {
        return expanded_;
    }
    /// Opens or closes the content region and starts the slide. Fires on_expanded_changed() when
    /// the value changes. Updates layout.
    void set_expanded(bool expanded);
    /// Fired whenever `expanded` changes, through a header click, Space or set_expanded().
    void on_expanded_changed(Callback<void(bool)> handler)
    {
        on_expanded_changed_ = std::move(handler);
    }


    /// The inset between the edges of the open content area and the content inside it, 16 DIPs by
    /// default. A dense list inside an Expander, such as a navigation pane, needs far less room
    /// than card-like content does.
    [[nodiscard]] const Thickness& content_padding() const
    {
        return content_padding_;
    }
    /// Changes the content padding. Updates layout.
    void set_content_padding(Thickness padding)
    {
        content_padding_ = padding;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The header row's own rect from the most recent arrange(), for hit-testing the header.
    [[nodiscard]] const Rect& header_bounds() const
    {
        return header_rect_;
    }
    /// The clipped area the content slides into, from the most recent arrange(). Zero height while
    /// fully collapsed.
    [[nodiscard]] const Rect& content_bounds() const
    {
        return content_rect_;
    }

    /// How far the content is through the opening or closing slide, eased. 0 is fully hidden
    /// behind the header and 1 fully in place. Read it like FlyoutControl::transition_opacity().
    ///
    /// The slide runs only in a host that ticks the control, such as ui::run()'s frame loop. Until
    /// the first tick, every set_expanded() jumps straight to the end value, so the content never
    /// stays stuck halfway behind the header.
    [[nodiscard]] float slide_progress() const;

    // ---- focus ----
    /// Whether the header has focus. The header itself is the focusable element. The focusable
    /// elements inside content() join the tab order only while expanded (see collect_focusables()).
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// The header is focusable while the Expander is enabled.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled();
    }
    /// Only a press on the header row focuses the Expander. Presses on the content don't.
    [[nodiscard]] bool takes_focus_at(Point p) const override
    {
        return header_rect_.contains(p);
    }
    /// The header, then the focusable elements inside the content while the content region exists
    /// and the Expander isn't a focus scope.
    void collect_focusables(std::vector<Element*>& out) override;

    // ---- derived visual state ----
    /// Whether the header is drawn pressed, because the pointer is down on it or Space is held.
    [[nodiscard]] bool is_pressed() const;

    // ---- the tree ----
    /// Visits the content, but only while the content region exists. A collapsed Expander's
    /// content is skipped by walks over the tree, as it is for layout, rendering and input.
    void for_each_child(ChildVisitor visit) const override
    {
        if (content_live())
        {
            visit(*content_);
        }
    }

    // ---- automation ----
    /// Exposed to UI Automation as a group.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Group;
    }
    /// The header text names the Expander when accessibility().name doesn't.
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return header_;
    }

protected:
    // ---- layout ----
    /// The header row's height plus, while the content region exists, the content's desired height
    /// and the content padding.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Splits the bounds into the header row and the content area, as expand_direction() says,
    /// and places the content at its current slide offset.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;

    // ---- input ----
    /// A click on the header row toggles expanded(). A press reaches the content only inside the
    /// area it is drawn in (content_bounds()). Moves and releases always reach it.
    bool pointer_override(const PointerEvent& e) override;
    void hit_test_children(Point p, std::vector<Element*>& out) const override;

    // ---- render ----
    /// Draws the header row and chevron, the content clipped to its area, then the focus ring.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Clears the dirty flags of this control, of the content while it exists and of the header
    /// content.
    void clear_dirty_override(DirtyFlags flags) override;
    /// Advances the open/close slide, then ticks the content while it exists.
    void tick_override(Duration delta) override;
    /// As a focus scope (Element::set_focus_scope(), off by default), the content leaves the
    /// window's tab order and gets a tab order of its own inside the Expander. Tab reaches the
    /// header, Enter steps into the content and Escape steps back out onto the header. Space
    /// toggles either way, so the keyboard can always collapse the group from its header.
    void collect_scope_focusables(std::vector<Element*>& out) override;


private:
    [[nodiscard]] bool hot() const;

    std::string              header_;
    std::unique_ptr<Element> header_content_;
    ExpandDirection          expand_direction_ = ExpandDirection::Down;
    std::unique_ptr<Element> content_;
    bool                     expanded_ = false;

    bool hovered_      = false;
    bool pointer_down_ = false;
    bool key_down_     = false;

    // 16 DIPs by default.
    Thickness content_padding_{16.0f};

    // True while the content region exists: `expanded_`, plus a collapse still sliding shut
    // (the content is collapsed only once the slide has ended).
    [[nodiscard]] bool content_live() const;
    // Re-arranges content_ at the offset slide_ implies, without a layout pass.
    void place_content();

    // Cached by measure_override() for arrange_override(), render() and pointer handling, which get
    // no Theme or TextMeasurer.
    mutable float header_height_  = 0.0f;
    mutable float content_height_ = 0.0f;
    Rect          header_rect_{};
    Rect          content_rect_{};

    // Linear [0,1] clock for the slide; slide_progress() eases it. `ticking_` gates the slide on a
    // host that drives tick_override().
    double slide_   = 0.0;
    bool   ticking_ = false;

    Callback<void(bool)> on_expanded_changed_;

protected:
    void enabled_changed() override;
    /// Called by Window as focus reaches or leaves the header. Redraws.
    void focus_changed(bool focused) override;
    /// Space toggles expanded() on key up. Enter does nothing here.
    bool key_override(const KeyEvent& e) override;

protected:
    /// The focus ring goes around the header only.
    [[nodiscard]] Rect focus_ring_rect(const Theme& /*theme*/) const override
    {
        return header_rect_;
    }
    [[nodiscard]] float focus_ring_corner_radius(const Theme& /*theme*/) const override
    {
        return 0.0f;
    }

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.expand_collapse = this;
    }
    // expanded().
    [[nodiscard]] bool expanded_state() const override
    {
        return expanded_;
    }
    // set_expanded(). Refused with false while disabled.
    bool set_expanded_state(bool expand) override
    {
        if (!is_enabled())
        {
            return false;
        }
        set_expanded(expand);
        return true;
    }
};

/// A collapsible header over one child. Builds a ui::ExpanderControl.
///
/// `.expanded = prop` is two-way, which keeps the state in a model. When the user expands or
/// collapses it, the property is written before `on_expanded_changed` runs.
///
/// Example:
/// @code
/// ui::Property<bool> open{false};
///
/// window.set_content(ui::Expander{
///     .header             = "Advanced options",
///     .expanded           = open,
///     .vertical_alignment = ui::Alignment::Leading,
///     .content            = ui::VerticalStack{.spacing = 8, .content = {
///         ui::CheckBox{.text = "Verbose logging"},
///         ui::CheckBox{.text = "Experimental renderer"},
///     }},
/// });
/// @endcode
struct Expander
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Expander>` points at.
    using control_type = ExpanderControl;

    // ---- properties ----
    /// The header text. See `header_content` for richer headers.
    Binding<std::string> header{};
    /// Whether the content shows.
    TwoWayField<bool> expanded{false};
    /// Down, the default, puts the content below the header, and Up above it.
    Binding<ExpandDirection> expand_direction{ExpandDirection::Down};
    /// The inset around the content inside the open content area, 16 DIPs by default
    /// (ui::ExpanderControl::set_content_padding()).
    Binding<Thickness> content_padding{Thickness{16.0f}};

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
    /// Fires with the new state when a header click or Space changes it. A write to the bound
    /// property doesn't run it.
    Callback<void(bool)> on_expanded_changed{};
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
    ControlOut<Expander> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// Header content in place of the `header` text (ui::ExpanderControl::set_header_content()).
    Spec header_content{};
    /// The collapsible content.
    Spec content{};

    /// Consumes the spec and returns the built ui::ExpanderControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
