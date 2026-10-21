// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <concepts>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <cassert>
#include <cstddef>

namespace ui
{

/// How a PipsPager shows its previous or next button.
enum class PipsPagerButtonVisibility
{
    Visible, ///< Shown whenever it has somewhere to go.
    /// Shown only while the pointer is over the pager or it has focus; its slot is
    /// reserved either way.
    VisibleOnPointerOver,
    Collapsed, ///< Not shown and takes no space (the default).
};

/// What a PipsPager's previous/next buttons and arrow keys do at the ends.
enum class PipsPagerWrapMode
{
    None, ///< Stop at the first/last page; the button at that edge hides (the default).
    Wrap, ///< Next from the last page goes to the first, Previous from the first to the last.
};

/// A strip of small dots, one per page, for paging through a
/// large item set such as a `Carousel`'s pictures.
///
/// The selected page's dot is drawn larger, in the same color. Optional previous/next buttons
/// flank the strip and hide at their own end of the range unless `PipsPagerWrapMode::Wrap`. At
/// most `max_visible_pips()` dots are shown, and the shown range follows the selection. With
/// `number_of_pages()` unset (the default) the page count is open-ended, and the list grows by one
/// whenever the selection reaches its last pip. The whole strip is a single tab stop. The arrow
/// keys along its axis step the selection, and Home/End jump to the ends. Hover never changes its
/// measured size. `ui::PipsPager` is the declarative form.
///
/// Example:
/// @code
/// ui::Property<std::size_t> page{0};
///
/// window.set_content(ui::VerticalStack{
///     .spacing  = 12,
///     .content  = {
///         ui::Text{.text = ui::bind(page, [](std::size_t p) { return std::format("Page {}", p + 1); })},
///         ui::PipsPager{
///             .selected_index             = page,
///             .number_of_pages            = 8,
///             .previous_button_visibility = ui::PipsPagerButtonVisibility::VisibleOnPointerOver,
///             .next_button_visibility     = ui::PipsPagerButtonVisibility::VisibleOnPointerOver,
///         },
///     },
/// });
///
/// // Imperatively:
/// auto pager = std::make_unique<ui::PipsPagerControl>(/*number_of_pages=*/10);
/// pager->set_orientation(ui::Orientation::Vertical);
/// pager->set_wrap_mode(ui::PipsPagerWrapMode::Wrap);
/// pager->on_selection_changed([](std::size_t index) { /* show page `index` */ });
/// @endcode
class PipsPagerControl final : public Element, private RangeValueProvider
{
public:
    /// `number_of_pages` nullopt (the default) is an unknown, open-ended count.
    explicit PipsPagerControl(std::optional<std::size_t> number_of_pages = std::nullopt, std::size_t selected_index = 0);
    /// As above, with a count given as a signed number, which must not be negative.
    template <std::signed_integral I>
    explicit PipsPagerControl(I number_of_pages, std::size_t selected_index = 0) :
        PipsPagerControl(std::optional<std::size_t>{static_cast<std::size_t>(number_of_pages)}, selected_index)
    {
        assert(number_of_pages >= 0 && "a negative page count; std::nullopt is an unknown one");
    }

    // ---- pages ----
    /// The page count; nullopt for an open-ended count (see `infinite()`).
    [[nodiscard]] std::optional<std::size_t> number_of_pages() const
    {
        return infinite() ? std::nullopt : std::optional<std::size_t>{static_cast<std::size_t>(number_of_pages_)};
    }
    /// Sets the page count; nullopt is an open-ended count (see `infinite()`). Clamps
    /// selected_index() into the new range, firing on_selection_changed() if that moves it.
    void set_number_of_pages(std::optional<std::size_t> number_of_pages);
    /// A count given as a signed number, which must not be negative. Pass std::nullopt for an
    /// open-ended count.
    template <std::signed_integral I>
    void set_number_of_pages(I number_of_pages)
    {
        assert(number_of_pages >= 0 && "a negative page count; std::nullopt is an unknown one");
        set_number_of_pages(std::optional<std::size_t>{static_cast<std::size_t>(number_of_pages)});
    }
    /// Whether the page count is open-ended: Next never hides and the pip list grows as the
    /// selection advances.
    [[nodiscard]] bool infinite() const
    {
        return number_of_pages_ < 0;
    }

    /// The current page, 0-based; 0 by default and with no pages.
    [[nodiscard]] std::size_t selected_index() const
    {
        return static_cast<std::size_t>(selected_index_);
    }
    /// Selects page `index`, clamped to [0, number_of_pages()-1], or to 0 with no pages. With an
    /// open-ended count there is no upper limit. Does nothing, and fires nothing, if the page is
    /// already selected.
    void set_selected_index(std::size_t index)
    {
        clamp_and_select(index > static_cast<std::size_t>((std::numeric_limits<int>::max)())
                             ? (std::numeric_limits<int>::max)()
                             : static_cast<int>(index));
    }

    /// The most pips shown at once; 5 by default.
    [[nodiscard]] std::size_t max_visible_pips() const
    {
        return static_cast<std::size_t>(max_visible_pips_);
    }
    /// Sets how many pips are shown at once. The shown range of pages keeps the selection roughly
    /// centered and stops at the ends, with no smooth scrolling. Changes the control's measured
    /// size.
    void set_max_visible_pips(std::size_t max_visible_pips);

    /// Horizontal by default.
    [[nodiscard]] Orientation orientation() const
    {
        return orientation_;
    }
    /// Swaps the strip's axes; updates the layout and redraws.
    void set_orientation(Orientation orientation);

    /// How the previous button shows; Collapsed by default.
    [[nodiscard]] PipsPagerButtonVisibility previous_button_visibility() const
    {
        return previous_button_visibility_;
    }
    /// Updates the layout. Only Collapsed gives up the reserved 24x24 DIP slot.
    void set_previous_button_visibility(PipsPagerButtonVisibility visibility);
    /// How the next button shows; Collapsed by default.
    [[nodiscard]] PipsPagerButtonVisibility next_button_visibility() const
    {
        return next_button_visibility_;
    }
    /// Updates the layout. Only Collapsed gives up the reserved 24x24 DIP slot.
    void set_next_button_visibility(PipsPagerButtonVisibility visibility);

    /// What happens at the ends; None by default.
    [[nodiscard]] PipsPagerWrapMode wrap_mode() const
    {
        return wrap_mode_;
    }
    /// Sets what happens at the ends and redraws, since the edge buttons may appear or hide.
    void set_wrap_mode(PipsPagerWrapMode wrap_mode)
    {
        wrap_mode_ = wrap_mode;
        mark_dirty(DirtyFlags::Visual);
    }

    /// Whether the previous button is drawn and can be clicked right now. It must not be
    /// Collapsed or hidden at its edge, and with VisibleOnPointerOver the pointer must be over the
    /// pager or focus in it.
    [[nodiscard]] bool previous_button_visible() const
    {
        return previous_button_shown();
    }
    /// The Next button's counterpart of `previous_button_visible()`.
    [[nodiscard]] bool next_button_visible() const
    {
        return next_button_shown();
    }


    /// Fired whenever selected_index() changes: by a pip click, a button click, an arrow key,
    /// Home/End, or set_selected_index().
    void on_selection_changed(Callback<void(std::size_t)> handler)
    {
        on_selection_changed_ = std::move(handler);
    }

    // ---- focus ----
    /// Whether the strip has keyboard focus.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// Not a tab stop while there is nothing to switch between.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled() && (number_of_pages_ > 1 || infinite());
    }

    // ---- derived visual state ----

protected:
    friend struct testing::VisualStates; // the tests read the state a control draws in
    /// The Previous button's hover/pressed/disabled state as drawn; pips have no ButtonState.
    [[nodiscard]] ButtonState previous_button_visual_state() const;
    /// The Next button's hover/pressed/disabled state as drawn.
    [[nodiscard]] ButtonState next_button_visual_state() const;

    // ---- layout ----
    /// A fixed-size strip: `min(max_visible_pips, number_of_pages)` pip slots, each 12 DIPs along
    /// the main axis by 24 across it, plus a 24x24 DIP square for each button that is not
    /// Collapsed, in either orientation. Hover never changes it, only the properties above do.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;

    // ---- input ----
    /// A click on a pip selects its page. A click on a button steps as `PipsPagerWrapMode` allows.
    bool pointer_override(const PointerEvent& e) override;
    // ---- render ----
    /// Draws the visible dots (the selected one larger), the button glyphs and the focus ring.
    void render_override(Renderer& renderer, const RenderContext& context) const override;


public:
    // ---- automation ----
    /// Exposed to accessibility tools as a spin button whose range value is the page index.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::SpinButton;
    }

private:
    // Only the axis the pager runs along; the other one is free to move focus.
    // Claimed so Window's directional focus navigation leaves them alone
    // (Element::key_override).
    [[nodiscard]] bool takes_arrow_key(Key key) const
    {
        return horizontal() ? (key == Key::ArrowLeft || key == Key::ArrowRight)
                            : (key == Key::ArrowUp || key == Key::ArrowDown);
    }
    // ui::detail::NoIndex in the int form the index arithmetic below uses.
    static constexpr int NoSlot = -1;
    [[nodiscard]] bool   horizontal() const
    {
        return orientation_ == Orientation::Horizontal;
    }

    [[nodiscard]] int page_count() const
    {
        return infinite() ? infinite_items_ : number_of_pages_;
    }
    // Adds one pip past the selection once it reaches the last one. Never shrinks.
    void grow_infinite_items();
    // Whether the button is not hidden at its own edge.
    [[nodiscard]] bool previous_available() const;
    [[nodiscard]] bool next_available() const;

    [[nodiscard]] int visible_count() const;
    [[nodiscard]] int first_visible_index() const;

    [[nodiscard]] bool previous_button_reserved() const
    {
        return previous_button_visibility_ != PipsPagerButtonVisibility::Collapsed;
    }
    [[nodiscard]] bool next_button_reserved() const
    {
        return next_button_visibility_ != PipsPagerButtonVisibility::Collapsed;
    }
    // Adds VisibleOnPointerOver's hover/focus gating on top of the always-reserved slot.
    [[nodiscard]] bool previous_button_shown() const;
    [[nodiscard]] bool next_button_shown() const;

    [[nodiscard]] bool can_go_previous() const;
    [[nodiscard]] bool can_go_next() const;
    void               go_previous();
    void               go_next();
    void               select(int index);
    void               clamp_and_select(int index);

    [[nodiscard]] Rect previous_button_bounds() const;
    [[nodiscard]] Rect next_button_bounds() const;
    [[nodiscard]] Rect pip_bounds(int visible_slot) const;
    [[nodiscard]] int  pip_hit_test(Point p) const;

    int number_of_pages_  = -1;
    int selected_index_   = 0;
    int max_visible_pips_ = 5;
    int infinite_items_   = 0;

    Orientation               orientation_                = Orientation::Horizontal;
    PipsPagerButtonVisibility previous_button_visibility_ = PipsPagerButtonVisibility::Collapsed;
    PipsPagerButtonVisibility next_button_visibility_     = PipsPagerButtonVisibility::Collapsed;
    PipsPagerWrapMode         wrap_mode_                  = PipsPagerWrapMode::None;

    bool pointer_over_       = false; // whole-control hover, for VisibleOnPointerOver
    int  hovered_page_index_ = NoSlot;
    bool hovered_previous_ = false, pressed_previous_ = false;
    bool hovered_next_ = false, pressed_next_ = false;

    Callback<void(std::size_t)> on_selection_changed_;

protected:
    void enabled_changed() override;
    /// Focus also reveals VisibleOnPointerOver buttons.
    void focus_changed(bool focused) override;
    /// Arrow keys along the axis step the selection, following `PipsPagerWrapMode`. Home/End jump
    /// to the first/last page. End does nothing with an open-ended count.
    bool key_override(const KeyEvent& e) override;

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.range_value = this;
    }
    [[nodiscard]] double range_value() const override
    {
        return selected_index_;
    }
    [[nodiscard]] double range_minimum() const override
    {
        return 0.0;
    }
    [[nodiscard]] double range_maximum() const override
    {
        const int pages = page_count();
        return pages > 0 ? pages - 1 : 0.0;
    }
    [[nodiscard]] double range_small_change() const override
    {
        return 1.0;
    }
    // Selects page `value` (truncated and clamped); false while disabled.
    bool set_range_value(double value) override
    {
        if (!is_enabled())
        {
            return false;
        }
        clamp_and_select(static_cast<int>(value));
        return true;
    }
    [[nodiscard]] bool range_read_only() const override
    {
        return false;
    }
};

/// A row of page dots. Builds a ui::PipsPagerControl.
///
/// Example:
/// @code
/// ui::Property<std::size_t> picture{0};
///
/// window.set_content(ui::PipsPager{
///     .selected_index             = picture,
///     .number_of_pages            = 8,
///     .previous_button_visibility = ui::PipsPagerButtonVisibility::VisibleOnPointerOver,
///     .next_button_visibility     = ui::PipsPagerButtonVisibility::VisibleOnPointerOver,
///     .horizontal_alignment       = ui::Alignment::Center,
/// });
/// @endcode
struct PipsPager
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<PipsPager>` points at.
    using control_type = PipsPagerControl;

    // ---- properties ----
    /// The current page, 0-based.
    TwoWayField<std::size_t> selected_index{0};
    /// The page count; nullopt (the default) is open-ended.
    Binding<std::optional<std::size_t>> number_of_pages{};
    /// How many pips show at once; the window of pips follows the selection.
    Binding<std::size_t> max_visible_pips{5};
    /// Horizontal (the default) or Vertical.
    Binding<Orientation> orientation{Orientation::Horizontal};
    /// How the previous button shows: Collapsed (the default), Visible, or only on pointer-over.
    Binding<PipsPagerButtonVisibility> previous_button_visibility{PipsPagerButtonVisibility::Collapsed};
    /// How the next button shows, as `previous_button_visibility`.
    Binding<PipsPagerButtonVisibility> next_button_visibility{PipsPagerButtonVisibility::Collapsed};
    /// What happens at the ends: Wrap lets Next go from the last page to the first and Previous
    /// back around.
    Binding<PipsPagerWrapMode> wrap_mode{PipsPagerWrapMode::None};

    // ---- appearance / layout ----
    Style                             style{};                ///< Named Style; explicit fields below win over it.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Fixed width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Fixed height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Minimum width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Minimum height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Maximum width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Maximum height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment within the parent's slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment within the parent's slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{};    ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Binding<bool>                     enabled{true}; ///< False grays it out and takes no input.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// Called with the new page index, from a pip, a button or a key. A bound value doesn't run it.
    Callback<void(std::size_t)> on_selection_changed{};
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
    ControlOut<PipsPager> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::PipsPagerControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
