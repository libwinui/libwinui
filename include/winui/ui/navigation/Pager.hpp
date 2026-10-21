// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/Strings.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <array>
#include <concepts>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cassert>
#include <cstddef>

namespace ui
{

class ButtonControl;
class ComboBoxControl;
class IconControl;
class NumberBoxControl;
class TextControl;
class ToolTipControl;

/// Which page selector a Pager shows.
enum class PagerDisplayMode
{
    /// ComboBox under ten pages; NumberBox from ten, or with an unknown count.
    Auto,
    /// "Page [drop-down] of N": a drop-down of page numbers.
    ComboBox,
    /// "Page [n] of N": a NumberBox the page number is typed into.
    NumberBox,
    /// A row of page-number buttons with ellipses, the current page underlined in the accent.
    ButtonPanel,
};

/// How a Pager shows one of its First/Previous/Next/Last buttons.
enum class PagerButtonVisibility
{
    /// Always shown; still disabled on its own edge.
    Visible,
    HiddenOnEdge, ///< invisible and disabled on its own edge, its room kept
    Hidden,       ///< collapsed: takes no room at all
};

/// A pager: First / Previous buttons, a page selector ("Page [n] of N" in a NumberBox or
/// drop-down, or a row of page buttons), and Next / Last buttons.
///
/// It is made of controls it owns (Buttons, a NumberBox, a ComboBox), each a tab stop of its own.
/// Page indices are 0-based, and the selector shows them 1-based. The navigation buttons disable
/// themselves on the first and last page. An unset page count is an unknown count, shown with no
/// "of N" and no Last button. The declarative form is ui::Pager.
///
/// Example:
/// @code
/// auto stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto pager = std::make_unique<ui::PagerControl>();
/// pager->set_number_of_pages(12);
/// pager->set_display_mode(ui::PagerDisplayMode::ButtonPanel);
/// pager->set_first_button_visibility(ui::PagerButtonVisibility::Hidden);
/// pager->set_last_button_visibility(ui::PagerButtonVisibility::Hidden);
/// pager->on_selection_changed([](std::size_t page) { std::println("show page {}", page); });
/// ui::PagerControl* observer = stack->add(std::move(pager));
/// observer->set_selected_index(3); // "4" underlined; fires the handler with 3
///
/// // Or declaratively:
/// ui::Property<std::size_t> page{0};
/// stack->add(ui::Pager{
///     .selected_index       = page,
///     .number_of_pages      = 25,
///     .on_selection_changed = [&](std::size_t index) { std::println("page {}", index); },
/// }.build());
/// @endcode
class PagerControl final : public Element
{
public:
    /// Zero pages, on page 0, in Auto mode, with every navigation button Visible and the default
    /// prefix and suffix text. Builds all of its parts up front.
    PagerControl();
    ~PagerControl() override;

    PagerControl(const PagerControl&)            = delete;
    PagerControl& operator=(const PagerControl&) = delete;

    // ---- pages ----
    /// The page count: 0 by default, nullopt for an unknown count.
    [[nodiscard]] std::optional<std::size_t> number_of_pages() const
    {
        return infinite() ? std::nullopt : std::optional<std::size_t>{static_cast<std::size_t>(number_of_pages_)};
    }
    /// Sets the page count; nullopt is an unknown count. A selection past the new last page moves
    /// onto it and fires on_selection_changed.
    void set_number_of_pages(std::optional<std::size_t> pages);
    /// A count given as a signed number, which must not be negative. Pass std::nullopt for an
    /// unknown count.
    template <std::signed_integral I>
    void set_number_of_pages(I pages)
    {
        assert(pages >= 0 && "a negative page count; std::nullopt is an unknown one");
        set_number_of_pages(std::optional<std::size_t>{static_cast<std::size_t>(pages)});
    }

    /// The current page, 0-based.
    [[nodiscard]] std::size_t selected_index() const
    {
        return static_cast<std::size_t>(selected_index_);
    }
    /// Selects page `index`, clamped to the last page. With an unknown count there is no upper
    /// limit. A change fires on_selection_changed.
    void set_selected_index(std::size_t index)
    {
        clamp_and_select(index > static_cast<std::size_t>((std::numeric_limits<int>::max)())
                             ? (std::numeric_limits<int>::max)()
                             : static_cast<int>(index));
    }

    /// Which page selector shows. Auto by default.
    [[nodiscard]] PagerDisplayMode display_mode() const
    {
        return display_mode_;
    }
    /// Swaps the page selector and updates the layout.
    void set_display_mode(PagerDisplayMode mode);
    /// The mode in use, with Auto resolved.
    [[nodiscard]] PagerDisplayMode effective_display_mode() const;

    // ---- buttons ----
    /// How the First button shows: Visible by default.
    [[nodiscard]] PagerButtonVisibility first_button_visibility() const
    {
        return button_visibility_[First];
    }
    /// Shows, hides or collapses the First button and updates the layout.
    void set_first_button_visibility(PagerButtonVisibility visibility);
    /// How the Previous button shows: Visible by default.
    [[nodiscard]] PagerButtonVisibility previous_button_visibility() const
    {
        return button_visibility_[Previous];
    }
    /// Shows, hides or collapses the Previous button and updates the layout.
    void set_previous_button_visibility(PagerButtonVisibility visibility);
    /// How the Next button shows: Visible by default.
    [[nodiscard]] PagerButtonVisibility next_button_visibility() const
    {
        return button_visibility_[Next];
    }
    /// Shows, hides or collapses the Next button and updates the layout.
    void set_next_button_visibility(PagerButtonVisibility visibility);
    /// How the Last button shows: Visible by default (never shown with an unknown page count).
    [[nodiscard]] PagerButtonVisibility last_button_visibility() const
    {
        return button_visibility_[Last];
    }
    /// Shows, hides or collapses the Last button and updates the layout.
    void set_last_button_visibility(PagerButtonVisibility visibility);

    /// Whether the button panel always offers page 1 and page N, with an ellipsis between them and
    /// the pages around the current one. True by default.
    [[nodiscard]] bool button_panel_always_show_first_last_page_index() const
    {
        return always_show_first_last_;
    }
    /// Rebuilds the button panel's slots.
    void set_button_panel_always_show_first_last_page_index(bool show);

    // ---- text ----
    /// The text before the selector: the control's own, else strings().pager.prefix ("Page").
    [[nodiscard]] const std::string& prefix_text() const
    {
        return prefix_text_ ? *prefix_text_ : strings().pager.prefix;
    }
    /// Sets the text before the selector, or with nullopt goes back to strings().pager.prefix,
    /// and updates the layout.
    void set_prefix_text(std::optional<std::string> text);
    /// The text between the selector and the page count: the control's own, else
    /// strings().pager.suffix ("of").
    [[nodiscard]] const std::string& suffix_text() const
    {
        return suffix_text_ ? *suffix_text_ : strings().pager.suffix;
    }
    /// Sets the text between the selector and the page count, or with nullopt goes back to
    /// strings().pager.suffix, and updates the layout.
    void set_suffix_text(std::optional<std::string> text);


    /// Fired with the new page index when the selection changes.
    void on_selection_changed(Callback<void(std::size_t)> handler)
    {
        on_selection_changed_ = std::move(handler);
    }

    // ---- parts ----
    /// The First button, owned by the pager (present even while hidden).
    [[nodiscard]] ButtonControl* first_button() const;
    /// The Previous button, owned by the pager.
    [[nodiscard]] ButtonControl* previous_button() const;
    /// The Next button, owned by the pager.
    [[nodiscard]] ButtonControl* next_button() const;
    /// The Last button, owned by the pager.
    [[nodiscard]] ButtonControl* last_button() const;
    /// The NumberBox selector, owned by the pager; only laid out in NumberBox mode.
    [[nodiscard]] NumberBoxControl* number_box() const
    {
        return number_box_;
    }
    /// The drop-down selector, owned by the pager; only laid out in ComboBox mode.
    [[nodiscard]] ComboBoxControl* combo_box() const
    {
        return combo_box_;
    }
    /// Whether a navigation button is drawn (not collapsed, and not hidden on its edge).
    [[nodiscard]] bool first_button_visible() const
    {
        return part_state(nav_part(First)) == PartState::Shown;
    }
    /// As first_button_visible(), for Previous.
    [[nodiscard]] bool previous_button_visible() const
    {
        return part_state(nav_part(Previous)) == PartState::Shown;
    }
    /// As first_button_visible(), for Next.
    [[nodiscard]] bool next_button_visible() const
    {
        return part_state(nav_part(Next)) == PartState::Shown;
    }
    /// As first_button_visible(), for Last.
    [[nodiscard]] bool last_button_visible() const
    {
        return part_state(nav_part(Last)) == PartState::Shown;
    }
    /// The button panel as a caller sees it: page numbers, and 0 for an ellipsis.
    [[nodiscard]] std::vector<int> button_panel_items() const;
    /// Which button_panel_items() entry the indicator underlines, or -1.
    [[nodiscard]] int button_panel_selected_item() const
    {
        return indicator_slot_;
    }
    /// The page-number button in button panel slot `slot`; nullptr when that slot
    /// is not a page button.
    [[nodiscard]] ButtonControl* button_panel_button(std::size_t slot) const;
    /// Lists the shown parts in tab order: First, Previous, the selector, Next, Last. Hidden parts
    /// get no focus, show no popups and are left out of automation.
    void for_each_child(ChildVisitor visit) const override;

    // ---- automation ----
    /// AutomationRole::Menu, over its parts.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Menu;
    }

protected:
    // ---- Element ----
    /// One row as tall as the tallest shown part: the left button pair, the selector, the right pair.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// The selector centered, First/Previous hugging it from the left and Next/Last from the right.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    /// Draws the shown parts and, in ButtonPanel mode, the 2px accent indicator.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Clears the pager's flags and every part's, shown or not, unlike for_each_child(), so a
    /// part that is shown again does not carry flags from before it was hidden.
    void clear_dirty_override(DirtyFlags flags) override;


private:
    void clamp_and_select(int index);
    enum class PartState
    {
        Shown,
        Hidden,    // takes its room, draws nothing, takes no input
        Collapsed, // takes no room
    };

    static constexpr std::size_t First    = 0;
    static constexpr std::size_t Previous = 1;
    static constexpr std::size_t Next     = 2;
    static constexpr std::size_t Last     = 3;
    static constexpr std::size_t Slots    = 7; // the button panel never shows more than seven items

    // Part indices, in tab order: First, Previous, the selector, Next, Last.
    static constexpr std::size_t PartFirst    = 0;
    static constexpr std::size_t PartPrevious = 1;
    static constexpr std::size_t PartPrefix   = 2;
    static constexpr std::size_t PartNumber   = 3;
    static constexpr std::size_t PartCombo    = 4;
    static constexpr std::size_t PartSuffix   = 5;
    static constexpr std::size_t PartTotal    = 6;
    static constexpr std::size_t PartSlots    = 7;                     // two parts per slot: button, ellipsis
    static constexpr std::size_t PartNext     = PartSlots + 2 * Slots; // 21
    static constexpr std::size_t PartLast     = PartNext + 1;
    static constexpr std::size_t PartCount    = PartLast + 1;

    [[nodiscard]] static std::size_t nav_part(std::size_t button)
    {
        constexpr std::array<std::size_t, 4> Parts{PartFirst, PartPrevious, PartNext, PartLast};
        return Parts[button];
    }
    [[nodiscard]] PartState part_state(std::size_t part) const
    {
        return states_[part];
    }
    [[nodiscard]] bool infinite() const
    {
        return number_of_pages_ < 0;
    }

    void set_button_visibility(std::size_t button, PagerButtonVisibility visibility);
    // Bring every part in line with the pages, the selection and the mode.
    void refresh();
    void refresh_button_panel();
    void set_slot(std::size_t slot, int page); // page 0 is an ellipsis, -1 an unused slot
    void select(int index);

    // Measure/arrange a part by its visible rect: a Button's desired size includes the room
    // its focus ring paints into, which layout here takes off and puts back.
    [[nodiscard]] Size visible_size(std::size_t part) const;
    void               place(std::size_t part, const Rect& visible);

    int                                  number_of_pages_ = 0;
    int                                  selected_index_  = 0;
    PagerDisplayMode                     display_mode_    = PagerDisplayMode::Auto;
    std::array<PagerButtonVisibility, 4> button_visibility_{PagerButtonVisibility::Visible,
                                                            PagerButtonVisibility::Visible,
                                                            PagerButtonVisibility::Visible,
                                                            PagerButtonVisibility::Visible};
    bool                                 always_show_first_last_ = true;
    std::optional<std::string>           prefix_text_;     // unset: strings().pager.prefix
    std::optional<std::string>           suffix_text_;     // unset: strings().pager.suffix
    bool                                 syncing_ = false; // refresh() writing into the selector

    std::array<std::unique_ptr<Element>, PartCount> parts_;
    std::array<PartState, PartCount>                states_{};
    std::array<ToolTipControl*, 4>                  nav_tips_{};
    std::array<ButtonControl*, 4>                   nav_buttons_{};
    TextControl*                                    prefix_     = nullptr;
    NumberBoxControl*                               number_box_ = nullptr;
    ComboBoxControl*                                combo_box_  = nullptr;
    TextControl*                                    suffix_     = nullptr;
    TextControl*                                    total_      = nullptr;
    std::array<ButtonControl*, Slots>               slot_buttons_{};
    std::array<IconControl*, Slots>                 slot_ellipses_{};
    std::array<int, Slots>                          slot_pages_{}; // -1 unused, 0 ellipsis, else page number
    int                                             indicator_slot_ = -1;
    int                                             combo_items_    = 0;

    mutable std::array<Size, PartCount> sizes_{}; // visible sizes from the last measure
    mutable float                       center_      = 0.0f;
    mutable float                       left_group_  = 0.0f;
    mutable float                       right_group_ = 0.0f;
    mutable float                       row_height_  = 0.0f;

    Callback<void(std::size_t)> on_selection_changed_;

protected:
    void enabled_changed() override;
};

/// Navigation through a numbered collection of pages: First/Previous, a page selector, and
/// Next/Last. Builds a ui::PagerControl.
///
/// `number_of_pages` is applied before `selected_index`, so a selection past the default
/// of zero pages is not clamped away.
///
/// Example:
/// @code
/// ui::Property<std::size_t> page{2};
///
/// window.set_content(ui::Pager{
///     .selected_index       = page,
///     .number_of_pages      = 12,
///     .display_mode         = ui::PagerDisplayMode::ComboBox,
///     .prefix_text          = "Page",
///     .suffix_text          = "of",
///     .on_selection_changed = [](std::size_t now) { std::println("page {}", now); },
/// });
/// @endcode
struct Pager
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Pager>` points at.
    using control_type = PagerControl;

    // ---- properties ----
    /// The current page, 0-based.
    TwoWayField<std::size_t> selected_index{0};
    /// The page count; nullopt is an unknown count.
    Binding<std::optional<std::size_t>> number_of_pages{std::size_t{0}};
    /// The page selector: Auto, ComboBox, NumberBox or ButtonPanel.
    Binding<PagerDisplayMode> display_mode{PagerDisplayMode::Auto};
    /// How the First button shows.
    Binding<PagerButtonVisibility> first_button_visibility{PagerButtonVisibility::Visible};
    /// How the Previous button shows.
    Binding<PagerButtonVisibility> previous_button_visibility{PagerButtonVisibility::Visible};
    /// How the Next button shows.
    Binding<PagerButtonVisibility> next_button_visibility{PagerButtonVisibility::Visible};
    /// How the Last button shows.
    Binding<PagerButtonVisibility> last_button_visibility{PagerButtonVisibility::Visible};
    /// Whether the button panel always offers pages 1 and N.
    Binding<bool> button_panel_always_show_first_last_page_index{true};
    /// Text before the page selector; unset uses strings().pager.prefix ("Page").
    Binding<std::optional<std::string>> prefix_text{};
    /// Text after the page selector; unset uses strings().pager.suffix ("of").
    Binding<std::optional<std::string>> suffix_text{};

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
    /// Called with the new page index. A bound value doesn't run it.
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
    ControlOut<Pager> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::PagerControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
